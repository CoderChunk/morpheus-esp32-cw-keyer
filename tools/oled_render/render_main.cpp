// MORPHEUS OLED host renderer.
//
// Walks the real firmware navigation state machine (ui_state.cpp) with
// synthetic input events - exactly the events ui_input.cpp would produce
// from a real encoder/button - and after every screen change, asks the
// real ui_renderer.cpp/ui_screens.cpp to draw into a real U8G2 buffer
// (the same u8g2 library and SH1106 setup function the firmware uses),
// then dumps that buffer to a PNG. No ESP32, no OLED panel, no BLE - only
// hardware I/O (GPIO/LEDC/NVS) is stubbed; the rendering and navigation
// logic under test is the unmodified firmware source.
//
// Usage: morpheus_oled_render [output_dir] [--max-depth N] [--max-shots N]
// Output directory defaults to ../../../oled_debug relative to this
// binary's build dir, i.e. GitHub/oled_debug/ - deliberately outside both
// git repos, never inside firmware/.

#define MORPHEUS_HOST_RENDER 1
#include "ui_state.h"
#include "ui_renderer.h"
#include "ui_backend.h"
#include "ui_menu.h"
#include "config.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "core_memory.h"
#include "core_trainer.h"
#include "core_games.h"
#include "core_stats.h"
#include "core_profiles.h"
#include "core_clock.h"
#include "core_led.h"
#include <U8g2lib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

// ui_renderer.cpp owns the real u8g2 instance as a file-local static, so
// render_main can't reach it directly. It exposes the same drawing entry
// points MORPHEUS.ino's loop() uses (ui_renderer_service(), etc.) - this
// harness calls those, then reads the frame back out through
// ui_renderer_debugGetBuffer(), a host-render-only accessor
// (MORPHEUS_HOST_RENDER-gated, compiled out of the real firmware).

static const char *screenName(UiScreen s) {
  switch (s) {
    case UI_SCREEN_SPLASH: return "SPLASH";
    case UI_SCREEN_HOME: return "HOME";
    case UI_SCREEN_MENU: return "MENU";
    case UI_SCREEN_LIST: return "LIST";
    case UI_SCREEN_EDIT_VALUE: return "EDIT_VALUE";
    case UI_SCREEN_EDIT_TOGGLE: return "EDIT_TOGGLE";
    case UI_SCREEN_INFO: return "INFO";
    case UI_SCREEN_DIALOG_CONFIRM: return "DIALOG_CONFIRM";
    case UI_SCREEN_DIAG_INPUT: return "DIAG_INPUT";
    case UI_SCREEN_DIAG_DISPLAY: return "DIAG_DISPLAY";
    case UI_SCREEN_DIAG_AUDIO: return "DIAG_AUDIO";
    case UI_SCREEN_DIAG_GPIO: return "DIAG_GPIO";
    case UI_SCREEN_DIAG_LIVE: return "DIAG_LIVE";
    case UI_SCREEN_LIVE_MONITOR: return "LIVE_MONITOR";
    case UI_SCREEN_TUNE: return "TUNE";
    case UI_SCREEN_TRAIN_DRILL: return "TRAIN_DRILL";
    case UI_SCREEN_TRAIN_FARNSWORTH: return "TRAIN_FARNSWORTH";
    case UI_SCREEN_TRAIN_EXAM_RESULT: return "TRAIN_EXAM_RESULT";
    case UI_SCREEN_GAME_COPY: return "GAME_COPY";
    case UI_SCREEN_GAME_MEMORY: return "GAME_MEMORY";
    case UI_SCREEN_GAME_SPEED: return "GAME_SPEED";
    case UI_SCREEN_GAME_PAUSE: return "GAME_PAUSE";
    case UI_SCREEN_VOLUME: return "VOLUME";
    case UI_SCREEN_CALLSIGN_EDIT: return "CALLSIGN_EDIT";
    case UI_SCREEN_DISPLAY_TIMEOUT: return "DISPLAY_TIMEOUT";
    case UI_SCREEN_CLOCK_EDIT: return "CLOCK_EDIT";
    case UI_SCREEN_DATE_FORMAT: return "DATE_FORMAT";
    case UI_SCREEN_TIME_FORMAT: return "TIME_FORMAT";
  }
  return "UNKNOWN";
}

static std::string g_outDir;
static int g_shotCount = 0;
static int g_maxShots = 200;
static int g_maxDepth = 5;
static unsigned long g_now = 0;

static void sanitize(std::string &s) {
  for (char &c : s) {
    if (!isalnum((unsigned char)c)) c = '_';
  }
}

static void capture(const char *label) {
  if (g_shotCount >= g_maxShots) return;
  ui_renderer_service(g_now);

  int w = 0, h = 0;
  const uint8_t *buf = ui_renderer_debugGetBuffer(w, h);
  if (!buf || w <= 0 || h <= 0) return;

  char base[512];
  std::string safeLabel(label);
  sanitize(safeLabel);
  snprintf(base, sizeof(base), "%s/%03d_%s", g_outDir.c_str(), g_shotCount, safeLabel.c_str());

  std::string pgmPath = std::string(base) + ".pgm";
  FILE *f = fopen(pgmPath.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int byteIdx = (y >> 3) * w + x;
      int bit = (buf[byteIdx] >> (y & 7)) & 1;
      unsigned char px = bit ? 255 : 0;
      fwrite(&px, 1, 1, f);
    }
  }
  fclose(f);

  std::string pngPath = std::string(base) + ".png";
  std::string cmd = "convert \"" + pgmPath + "\" \"" + pngPath + "\" 2>/dev/null && rm -f \"" + pgmPath + "\"";
  system(cmd.c_str());

  g_shotCount++;
  printf("  [%03d] %s -> %s\n", g_shotCount - 1, label, pngPath.c_str());
}

static void sendEvent(UiEventType type, int8_t detents = 0) {
  UiEvent ev{type, detents};
  ui_state_handleEvent(ev, g_now);
  g_now += 20;
  ui_state_service(g_now);
}

// Some leaf screens (e.g. UI_SCREEN_DIAG_INPUT) deliberately *consume*
// UI_EV_BACK/SELECT as diagnostic input instead of navigating - there's
// no single exit convention that works for every screen. So rather than
// track "how do I get back from here", every capture replays its full
// path from a freshly re-initialized state machine: reset, HOME -> MENU,
// then re-select carouselIdx, then each list row index in turn. Cheap
// (synthetic events, no real waiting) and immune to any given screen's
// BACK/HOME quirks, at the cost of redoing the ancestor chain per node -
// fine at this tree's size (a couple hundred nodes at most).
static UiScreen replay(const std::vector<uint8_t> &path) {
  // Full reset, not just ui_state - core_trainer/core_keyer/etc. state
  // (e.g. "is a training session active") persists across these C++
  // statics otherwise, and a stale "busy" flag from one path can silently
  // block a sibling path's transition (session guards like
  // pushTrainDrill()'s audioResourceBusy() check real core state).
  core_keyer_init();
  core_decoder_init();
  core_memory_init();
  core_trainer_init();
  core_games_init();
  core_stats_init();
  core_profiles_init();
  core_clock_init();
  core_led_init();
  // *_init() alone isn't enough: real firmware calls it exactly once at
  // boot, before any session could be active, so e.g. core_trainer_init()
  // deliberately leaves sessionActive untouched. Replaying many sessions
  // in one process needs those explicitly cleared too, or a session
  // started by an earlier path leaves ui_state's audioResourceBusy()
  // guard stuck true and silently blocks every later path that also
  // wants an audio resource (training/tune/games/memory playback).
  core_trainer_stopSession();
  core_trainer_farnsworthStop();
  core_memory_stop();
  core_games_stop();
  core_keyer_diagToneStop();
  ui_state_init(g_now);
  for (int i = 0; i < 200 && ui_state_getScreen() == UI_SCREEN_SPLASH; i++) {
    g_now += 20;
    ui_state_service(g_now);
  }
  if (path.empty()) return ui_state_getScreen();  // HOME

  sendEvent(UI_EV_SELECT);  // HOME -> MENU (carousel)
  for (size_t i = 0; i < path.size(); i++) {
    uint8_t idx = path[i];
    if (i == 0) {
      int delta = (int)idx - (int)ui_state_getCarouselIndex();
      while (delta > 0) { sendEvent(UI_EV_ROTATE, 1); delta--; }
      while (delta < 0) { sendEvent(UI_EV_ROTATE, -1); delta++; }
      sendEvent(UI_EV_SELECT);  // carousel item -> its list
    } else {
      if (ui_state_getScreen() != UI_SCREEN_LIST) return ui_state_getScreen();  // dead path
      uint8_t count = 0, sel = 0, window = 0;
      ui_state_getList(count, sel, window);
      if (idx >= count) return ui_state_getScreen();
      int delta = (int)idx - (int)sel;
      while (delta > 0) { sendEvent(UI_EV_ROTATE, 1); delta--; }
      while (delta < 0) { sendEvent(UI_EV_ROTATE, -1); delta++; }
      sendEvent(UI_EV_SELECT);
    }
  }
  return ui_state_getScreen();
}

static std::string labelFor(const std::vector<uint8_t> &path, UiScreen screen) {
  std::string label;
  for (uint8_t idx : path) { label += std::to_string(idx); label += "-"; }
  label += screenName(screen);
  const char *title = ui_state_getListTitle();
  if (title && title[0]) { label += "_"; label += title; }
  return label;
}

// (screen, list title) signature - used to detect a "self-loop" row: a
// NODE_TRIGGER-style instant action (e.g. Memory Msgs' "PLAY") that
// SELECTs without leaving the current list. Left unchecked, BFS treats
// every such row as a fresh list to expand and re-enqueues its own
// siblings under itself, forever (bounded only by --max-depth).
static std::string signatureOf(UiScreen screen) {
  std::string sig = screenName(screen);
  const char *title = ui_state_getListTitle();
  if (title) sig += title;
  return sig;
}

int main(int argc, char **argv) {
  g_outDir = "../../../oled_debug";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--max-depth") && i + 1 < argc) { g_maxDepth = atoi(argv[++i]); }
    else if (!strcmp(argv[i], "--max-shots") && i + 1 < argc) { g_maxShots = atoi(argv[++i]); }
    else if (argv[i][0] != '-') { g_outDir = argv[i]; }
  }
  mkdir(g_outDir.c_str(), 0755);

  printf("MORPHEUS OLED render -> %s\n", g_outDir.c_str());
  ui_renderer_init();

  // The bare carousel (MENU) screen: replay() only stops there mid-path
  // (path.size()==1 lands past it, in a pushed LIST), so it never gets
  // captured as a BFS node on its own otherwise - grab it once directly.
  replay({});
  sendEvent(UI_EV_SELECT);
  capture(labelFor({}, ui_state_getScreen()).c_str());

  struct QueueEntry { std::vector<uint8_t> path; std::string parentSig; };
  std::vector<QueueEntry> queue;
  queue.push_back({{}, ""});  // HOME

  size_t qi = 0;
  while (qi < queue.size() && g_shotCount < g_maxShots) {
    QueueEntry entry = queue[qi++];
    const std::vector<uint8_t> &path = entry.path;
    UiScreen screen = replay(path);
    capture(labelFor(path, screen).c_str());

    if ((int)path.size() >= g_maxDepth) continue;

    std::string mySig = signatureOf(screen);
    bool isSelfLoop = (screen == UI_SCREEN_LIST) && !entry.parentSig.empty() && mySig == entry.parentSig;

    if (path.empty()) {
      // HOME's only child is the carousel itself - enqueue each category.
      for (uint8_t i = 0; i < UI_MAIN_MENU_COUNT; i++) {
        std::vector<uint8_t> child = path; child.push_back(i);
        queue.push_back({child, mySig});
      }
    } else if (screen == UI_SCREEN_LIST && !isSelfLoop) {
      uint8_t count = 0, sel = 0, window = 0;
      ui_state_getList(count, sel, window);
      for (uint8_t i = 0; i < count; i++) {
        std::vector<uint8_t> child = path; child.push_back(i);
        queue.push_back({child, mySig});
      }
    }
    // Any other screen, or a self-loop list, is a leaf as far as this
    // walker is concerned - nothing more to enqueue under it.
  }

  printf("Done: %d screenshot(s), %zu node(s) visited, in %s\n",
         g_shotCount, queue.size(), g_outDir.c_str());
  return 0;
}
