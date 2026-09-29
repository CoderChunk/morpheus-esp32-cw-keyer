/*
 * ============================================================================
 * MORPHEUS - BLE Remote Control Bridge
 * ============================================================================
 * File: ble_control.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Bridges the BLE_CONTROL_CMD/EVT characteristics (transport.cpp) to
 * core_trainer/core_games/core_keyer - the BLE analogue of ui_backend.cpp,
 * which bridges the same modules to the OLED UI. transport.cpp only ever
 * carries opaque JSON strings; this file owns the command vocabulary and
 * decides what a command means.
 *
 * Command JSON (client -> device, BLE_CONTROL_CMD_UUID, write):
 *   {"cmd":"key_down"} / {"cmd":"key_up"}
 *     Virtual straight key. Classified DIT/DAH from press duration using
 *     the same STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT formula core_keyer's
 *     physical straight-key path uses, then fed into events_onKeyDown()/
 *     events_onKeyUp() - the same fan-out real keying uses, so stats/
 *     decoder/training/games all see a virtual key exactly like a real
 *     one, through the same single core_decoder - there is no second
 *     decoder for BLE input. events_onKeyUp() is called with
 *     fromVirtualKey=true, which makes core_decoder_service() apply
 *     BLE_KEY_GAP_COMPENSATION_MS of extra char/word-gap tolerance
 *     (config.h) after this element: the key_down/key_up writes for a
 *     BLE client's *next* element still have to cross the BLE link
 *     before this decoder ever sees them, and without that allowance a
 *     perfectly normal inter-element pause can read as a full character
 *     gap and split e.g. "I" (". .") into "E" + "E". Ignored while a
 *     real physical key is already down.
 *   {"cmd":"train_start","mode":"KOCH|CHARACTERS|WORDS|CALLSIGNS|ADAPTIVE|EXAM"}
 *   {"cmd":"train_stop"} / {"cmd":"train_confirm"}
 *   {"cmd":"game_start","game":"COPY|MEMORY|SPEED"}
 *   {"cmd":"game_stop"} / {"cmd":"game_pause"} / {"cmd":"game_confirm"} / {"cmd":"game_restart"}
 *
 *   Bench-diagnostic only, FEATURE_DEBUG_SERIAL_COMMANDS-gated (off by
 *   default, not part of the stable vocabulary above - see
 *   tools/oled_screencap/screencap_ble.py). One request per chunk, not a
 *   push burst: BlueZ's D-Bus notification delivery coalesces same-
 *   characteristic updates sent faster than the client processes them
 *   (confirmed by hand - a server-push design silently lost every
 *   notification but the last, even though every transport_sendControlEvent()
 *   call itself returned true), so the client drives the pacing instead.
 *   {"cmd":"dump_screen_start"} -> one {"evt":"screen_dump_start",...}
 *   {"cmd":"dump_screen_chunk","i":<0..n-1>} -> one {"evt":"screen_dump_chunk",...}
 *
 * Event JSON (device -> client, BLE_CONTROL_EVT_UUID, notify):
 *   {"evt":"train_state", ...} / {"evt":"game_state", ...} - pushed on
 *   change (rate-limited), see buildTrainStateJson()/buildGameStateJson().
 *   {"evt":"ack","cmd":"...","ok":bool} / {"evt":"error","message":"..."}
 *   {"evt":"screen_dump_start","w":128,"h":64,"bytes":1024,"n":16}
 *   {"evt":"screen_dump_chunk","i":<0..n-1>,"hex":"<up to 128 hex chars>"}
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "ble_control.h"
#include "config.h"
#include "core_keyer.h"
#include "core_trainer.h"
#include "core_games.h"
#include "transport.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if FEATURE_DEBUG_SERIAL_COMMANDS
#include "ui_renderer.h"
#endif

// ----------------------------------------------------------------------------
// Minimal JSON field extraction - the command vocabulary is small and
// flat (one or two string fields), so a hand-rolled scan avoids pulling
// in a JSON library for a handful of lookups. Not a general parser.
// ----------------------------------------------------------------------------
static bool jsonGetString(const char *json, const char *key, char *out, size_t outSize) {
  if (outSize == 0) return false;
  char pattern[24];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *p = strstr(json, pattern);
  if (p == nullptr) { out[0] = '\0'; return false; }
  p += strlen(pattern);
  // Tolerate optional whitespace after the colon (e.g. Python's default
  // json.dumps separator is ": ", not ":") - a strict no-space match
  // silently fails against any producer that formats JSON this way.
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') { out[0] = '\0'; return false; }
  p++;
  size_t i = 0;
  while (*p != '\0' && *p != '"' && i + 1 < outSize) out[i++] = *p++;
  out[i] = '\0';
  return true;
}

#if FEATURE_DEBUG_SERIAL_COMMANDS
// Bare non-negative integer field - only "i" (chunk index) needs this,
// so no sign/float handling.
static bool jsonGetInt(const char *json, const char *key, int *out) {
  char pattern[24];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *p = strstr(json, pattern);
  if (p == nullptr) return false;
  p += strlen(pattern);
  while (*p == ' ' || *p == '\t') p++;
  if (*p < '0' || *p > '9') return false;
  *out = atoi(p);
  return true;
}
#endif

// ----------------------------------------------------------------------------
// Virtual keying - see file header. Guarded against overlapping with a
// real physical key (core_keyer_isTxActive()) or a stray double key_down.
// ----------------------------------------------------------------------------
static bool virtualKeyDown = false;
static unsigned long virtualKeyDownMs = 0;

static void handleKeyDown() {
  if (core_keyer_isTxActive() || virtualKeyDown) return;
  virtualKeyDown = true;
  virtualKeyDownMs = millis();
  events_onKeyDown(virtualKeyDownMs);
}

static void handleKeyUp() {
  if (!virtualKeyDown) return;
  virtualKeyDown = false;
  unsigned long now = millis();
  unsigned long durMs = now - virtualKeyDownMs;
  unsigned long ditLenMs = core_keyer_getDitLengthMs();
  unsigned long thresholdMs = (unsigned long)(ditLenMs * STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT);
  ElementType type = (durMs < thresholdMs) ? ELEM_DIT : ELEM_DAH;
  events_onKeyUp(type, durMs, thresholdMs, now, /*fromVirtualKey=*/true);
}

// ----------------------------------------------------------------------------
// Command responses
// ----------------------------------------------------------------------------
static void sendAck(const char *cmd, bool ok) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"evt\":\"ack\",\"cmd\":\"%s\",\"ok\":%s}", cmd, ok ? "true" : "false");
  transport_sendControlEvent(buf);
}

static void sendError(const char *message) {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"evt\":\"error\",\"message\":\"%s\"}", message);
  transport_sendControlEvent(buf);
}

#if FEATURE_DEBUG_SERIAL_COMMANDS
// Bench-diagnostic commands, same flag/spirit as transport_debugDumpState()
// and ui_renderer_debugDumpScreen() - not part of the stable command
// vocabulary documented at the top of this file.
//
// One request per chunk, not a push burst: an earlier server-push design
// (one command triggering 16 back-to-back notify() calls) reliably lost
// every notification except the last. Confirmed by hand this isn't a
// firmware bug - transport_sendControlEvent() returned ok=true for
// every single call - it's BlueZ's D-Bus GATT notification delivery
// (org.bluez.GattCharacteristic1's Value property + PropertiesChanged)
// coalescing same-characteristic updates that arrive faster than the
// client's event loop processes them; there is no queue guarantee at
// that layer. Making the client explicitly request each chunk and wait
// for its own response removes any possibility of that: at most one
// notification is ever in flight.
static void handleDumpScreenStart() {
  int width = 0, height = 0;
  ui_renderer_debugGetBuffer(width, height);
  int totalBytes = (width * height) / 8;
  const int chunkBytes = 64;   // matches handleDumpScreenChunk() below
  int chunkCount = (totalBytes + chunkBytes - 1) / chunkBytes;

  char msg[64];
  snprintf(msg, sizeof(msg), "{\"evt\":\"screen_dump_start\",\"w\":%d,\"h\":%d,\"bytes\":%d,\"n\":%d}",
           width, height, totalBytes, chunkCount);
  transport_sendControlEvent(msg);
}

static void handleDumpScreenChunk(int i) {
  int width = 0, height = 0;
  const uint8_t *buf = ui_renderer_debugGetBuffer(width, height);
  int totalBytes = (width * height) / 8;
  const int chunkBytes = 64;

  int off = i * chunkBytes;
  if (i < 0 || off >= totalBytes) { sendError("chunk index out of range"); return; }
  int len = (chunkBytes < totalBytes - off) ? chunkBytes : (totalBytes - off);

  char hex[chunkBytes * 2 + 1];
  for (int j = 0; j < len; j++) snprintf(hex + j * 2, 3, "%02X", buf[off + j]);
  hex[len * 2] = '\0';

  char msg[196];
  snprintf(msg, sizeof(msg), "{\"evt\":\"screen_dump_chunk\",\"i\":%d,\"hex\":\"%s\"}", i, hex);
  transport_sendControlEvent(msg);
}
#endif

static bool parseTrainMode(const char *s, TrainMode *out) {
  if      (!strcmp(s, "KOCH"))       *out = TRAIN_MODE_KOCH;
  else if (!strcmp(s, "CHARACTERS")) *out = TRAIN_MODE_CHARACTERS;
  else if (!strcmp(s, "WORDS"))      *out = TRAIN_MODE_WORDS;
  else if (!strcmp(s, "CALLSIGNS"))  *out = TRAIN_MODE_CALLSIGNS;
  else if (!strcmp(s, "ADAPTIVE"))   *out = TRAIN_MODE_ADAPTIVE;
  else if (!strcmp(s, "EXAM"))       *out = TRAIN_MODE_EXAM;
  else return false;
  return true;
}

static bool parseGameId(const char *s, GameId *out) {
  if      (!strcmp(s, "COPY"))   *out = GAME_COPY;
  else if (!strcmp(s, "MEMORY")) *out = GAME_MEMORY;
  else if (!strcmp(s, "SPEED"))  *out = GAME_SPEED;
  else return false;
  return true;
}

void ble_control_handleCommand(const char *json) {
  char cmd[24];
  if (!jsonGetString(json, "cmd", cmd, sizeof(cmd))) {
    sendError("missing cmd");
    return;
  }

  if (!strcmp(cmd, "key_down")) {
    handleKeyDown();
  } else if (!strcmp(cmd, "key_up")) {
    handleKeyUp();
  } else if (!strcmp(cmd, "train_start")) {
    char modeStr[16];
    TrainMode mode;
    if (jsonGetString(json, "mode", modeStr, sizeof(modeStr)) && parseTrainMode(modeStr, &mode)) {
      // Mirrors core_games_start()'s own trainer-vs-game exclusivity
      // check, in the other direction: core_trainer_startSession()
      // does not itself check for an active game (only core_games_start
      // checks for an active trainer session), so a remote client could
      // otherwise wedge both into "active" at once, fighting over the
      // one shared decoder training sink.
      if (core_games_isSessionActive()) core_games_stop();
      core_trainer_startSession(mode);
      sendAck("train_start", core_trainer_isSessionActive());
    } else {
      sendError("bad or missing mode");
    }
  } else if (!strcmp(cmd, "train_stop")) {
    core_trainer_stopSession();
    sendAck("train_stop", true);
  } else if (!strcmp(cmd, "train_confirm")) {
    core_trainer_confirmPressed();
    sendAck("train_confirm", true);
  } else if (!strcmp(cmd, "game_start")) {
    char gameStr[12];
    GameId game;
    if (jsonGetString(json, "game", gameStr, sizeof(gameStr)) && parseGameId(gameStr, &game)) {
      core_games_start(game);   // silently refuses if a trainer session is active
      sendAck("game_start", core_games_getActiveGame() == game);
    } else {
      sendError("bad or missing game");
    }
  } else if (!strcmp(cmd, "game_stop")) {
    core_games_stop();
    sendAck("game_stop", true);
  } else if (!strcmp(cmd, "game_pause")) {
    core_games_togglePause();
    sendAck("game_pause", true);
  } else if (!strcmp(cmd, "game_confirm")) {
    core_games_confirmPressed();
    sendAck("game_confirm", true);
  } else if (!strcmp(cmd, "game_restart")) {
    core_games_restart();
    sendAck("game_restart", true);
#if FEATURE_DEBUG_SERIAL_COMMANDS
  } else if (!strcmp(cmd, "dump_screen_start")) {
    handleDumpScreenStart();
  } else if (!strcmp(cmd, "dump_screen_chunk")) {
    int i;
    if (jsonGetInt(json, "i", &i)) handleDumpScreenChunk(i);
    else sendError("missing i");
#endif
  } else {
    sendError("unknown cmd");
  }
}

// ----------------------------------------------------------------------------
// Live state push - rate-limited to MIN_SEND_INTERVAL_MS, but a changed
// snapshot is never dropped: if a send is skipped for being too soon,
// the unsent buffer keeps comparing "changed" on every subsequent call
// until an interval opens up, at which point the LATEST state (not a
// stale one) goes out.
// ----------------------------------------------------------------------------
static const unsigned long MIN_SEND_INTERVAL_MS = 150;

static void trainModeStr(TrainMode m, char *out, size_t n) {
  const char *s;
  switch (m) {
    case TRAIN_MODE_KOCH:       s = "KOCH"; break;
    case TRAIN_MODE_CHARACTERS: s = "CHARACTERS"; break;
    case TRAIN_MODE_WORDS:      s = "WORDS"; break;
    case TRAIN_MODE_CALLSIGNS:  s = "CALLSIGNS"; break;
    case TRAIN_MODE_ADAPTIVE:   s = "ADAPTIVE"; break;
    case TRAIN_MODE_EXAM:       s = "EXAM"; break;
    default:                    s = "?"; break;
  }
  strncpy(out, s, n - 1); out[n - 1] = '\0';
}

static void trainPhaseStr(DrillPhase p, char *out, size_t n) {
  const char *s;
  switch (p) {
    case DRILL_IDLE:      s = "IDLE"; break;
    case DRILL_PLAYING:   s = "PLAYING"; break;
    case DRILL_LISTENING: s = "LISTENING"; break;
    case DRILL_FEEDBACK:  s = "FEEDBACK"; break;
    case DRILL_EXAM_DONE: s = "EXAM_DONE"; break;
    default:              s = "?"; break;
  }
  strncpy(out, s, n - 1); out[n - 1] = '\0';
}

static void buildTrainStateJson(char *out, size_t outSize) {
  // An unconfirmed EXAM result must still be reported even though
  // sessionActive already went false in the same transition that set
  // phase=DRILL_EXAM_DONE (core_trainer.cpp's EXAM branch of
  // onTrainingCharDecoded()) - this is the only train_state push for
  // that transition (deduped/rate-limited pushes mean no earlier frame
  // carries the result either), so without this check
  // examScorePercent/examPassed/examCorrect/examTotal and
  // phase="EXAM_DONE" could never reach any BLE/WebSocket client, even
  // though the exam getters below still hold the correct values.
  // core_trainer_confirmPressed() clears the exam result and resets
  // phase to DRILL_IDLE, at which point this correctly falls back to
  // the bare {"active":false} shape.
  if (!core_trainer_isSessionActive() && !core_trainer_isExamResultReady()) {
    snprintf(out, outSize, "{\"evt\":\"train_state\",\"active\":false}");
    return;
  }
  char modeStr[12];  trainModeStr(core_trainer_getMode(), modeStr, sizeof(modeStr));
  char phaseStr[12]; trainPhaseStr(core_trainer_getPhase(), phaseStr, sizeof(phaseStr));
  const char *target = core_trainer_getTargetText();
  if (target == nullptr) target = "";

  snprintf(out, outSize,
    "{\"evt\":\"train_state\",\"active\":true,\"mode\":\"%s\",\"phase\":\"%s\",\"target\":\"%s\","
    "\"kochLevel\":%u,\"correct\":%lu,\"attempts\":%lu,\"adaptiveWpm\":%d,"
    "\"examScorePercent\":%u,\"examPassed\":%s,\"examCorrect\":%u,\"examTotal\":%u}",
    modeStr, phaseStr, target,
    (unsigned)core_trainer_getKochLevel(),
    (unsigned long)core_trainer_getCorrectCount(),
    (unsigned long)core_trainer_getTotalCount(),
    core_trainer_getAdaptiveWpm(),
    (unsigned)core_trainer_getExamScorePercent(),
    core_trainer_getExamPassed() ? "true" : "false",
    (unsigned)core_trainer_getExamCorrectCount(),
    (unsigned)core_trainer_getExamTotalCount());
}

static void buildGameStateJson(char *out, size_t outSize, unsigned long now) {
  if (!core_games_isSessionActive()) {
    snprintf(out, outSize, "{\"evt\":\"game_state\",\"active\":false}");
    return;
  }
  bool paused = core_games_isPaused();
  GameId game = core_games_getActiveGame();

  if (game == GAME_COPY) {
    const char *phaseStr;
    switch (core_games_copy_getPhase()) {
      case COPY_IDLE:    phaseStr = "IDLE"; break;
      case COPY_FALLING: phaseStr = "FALLING"; break;
      case COPY_HIT:     phaseStr = "HIT"; break;
      case COPY_MISS:    phaseStr = "MISS"; break;
      case COPY_OVER:    phaseStr = "OVER"; break;
      default:           phaseStr = "?"; break;
    }
    char targetBuf[2] = { core_games_copy_getFallingChar(), '\0' };
    snprintf(out, outSize,
      "{\"evt\":\"game_state\",\"game\":\"COPY\",\"active\":true,\"paused\":%s,\"phase\":\"%s\","
      "\"target\":\"%s\",\"score\":%u,\"lives\":%u,\"highScore\":%u,\"fallProgressPct\":%u}",
      paused ? "true" : "false", phaseStr, targetBuf,
      (unsigned)core_games_copy_getScore(), (unsigned)core_games_copy_getLives(),
      (unsigned)core_games_copy_getHighScore(), (unsigned)core_games_copy_getFallProgressPct(now));
    return;
  }

  if (game == GAME_MEMORY) {
    const char *phaseStr;
    switch (core_games_memory_getPhase()) {
      case MEM_IDLE:     phaseStr = "IDLE"; break;
      case MEM_PLAYBACK: phaseStr = "PLAYBACK"; break;
      case MEM_INPUT:    phaseStr = "INPUT"; break;
      case MEM_ROUND_OK: phaseStr = "ROUND_OK"; break;
      case MEM_OVER:     phaseStr = "OVER"; break;
      default:           phaseStr = "?"; break;
    }
    snprintf(out, outSize,
      "{\"evt\":\"game_state\",\"game\":\"MEMORY\",\"active\":true,\"paused\":%s,\"phase\":\"%s\","
      "\"chainLength\":%u,\"inputProgress\":%u,\"highScore\":%u}",
      paused ? "true" : "false", phaseStr,
      (unsigned)core_games_memory_getChainLength(), (unsigned)core_games_memory_getInputProgress(),
      (unsigned)core_games_memory_getHighScore());
    return;
  }

  if (game == GAME_SPEED) {
    const char *phaseStr;
    switch (core_games_speed_getPhase()) {
      case SPD_IDLE:     phaseStr = "IDLE"; break;
      case SPD_LISTEN:   phaseStr = "LISTEN"; break;
      case SPD_FEEDBACK: phaseStr = "FEEDBACK"; break;
      case SPD_OVER:     phaseStr = "OVER"; break;
      default:           phaseStr = "?"; break;
    }
    char lastCharBuf[2] = { core_games_speed_getLastChar(), '\0' };
    snprintf(out, outSize,
      "{\"evt\":\"game_state\",\"game\":\"SPEED\",\"active\":true,\"paused\":%s,\"phase\":\"%s\","
      "\"combo\":%u,\"lives\":%u,\"highScore\":%u,\"beatRemainingMs\":%lu,\"lastChar\":\"%s\",\"wasLastCorrect\":%s}",
      paused ? "true" : "false", phaseStr,
      (unsigned)core_games_speed_getCombo(), (unsigned)core_games_speed_getLives(),
      (unsigned)core_games_speed_getHighScore(), core_games_speed_getBeatRemainingMs(now),
      lastCharBuf, core_games_speed_wasLastCorrect() ? "true" : "false");
    return;
  }

  snprintf(out, outSize, "{\"evt\":\"game_state\",\"active\":false}");
}

static char lastTrainJson[BLE_CONTROL_EVT_CAP] = "";
static char lastGameJson[BLE_CONTROL_EVT_CAP]  = "";
static unsigned long lastTrainSendMs = 0;
static unsigned long lastGameSendMs  = 0;

static void serviceTrainingStatePush(unsigned long now) {
  char buf[BLE_CONTROL_EVT_CAP];
  buildTrainStateJson(buf, sizeof(buf));
  if (strcmp(buf, lastTrainJson) == 0) return;
  if (now - lastTrainSendMs < MIN_SEND_INTERVAL_MS) return;
  if (transport_sendControlEvent(buf)) {
    strncpy(lastTrainJson, buf, sizeof(lastTrainJson) - 1);
    lastTrainJson[sizeof(lastTrainJson) - 1] = '\0';
    lastTrainSendMs = now;
  }
}

static void serviceGameStatePush(unsigned long now) {
  char buf[BLE_CONTROL_EVT_CAP];
  buildGameStateJson(buf, sizeof(buf), now);
  if (strcmp(buf, lastGameJson) == 0) return;
  if (now - lastGameSendMs < MIN_SEND_INTERVAL_MS) return;
  if (transport_sendControlEvent(buf)) {
    strncpy(lastGameJson, buf, sizeof(lastGameJson) - 1);
    lastGameJson[sizeof(lastGameJson) - 1] = '\0';
    lastGameSendMs = now;
  }
}

void ble_control_init() {
  transport_setControlCommandHandler(ble_control_handleCommand);
  virtualKeyDown = false;
  lastTrainJson[0] = '\0';
  lastGameJson[0]  = '\0';
  lastTrainSendMs = 0;
  lastGameSendMs  = 0;
}

void ble_control_service(unsigned long now) {
  serviceTrainingStatePush(now);
  serviceGameStatePush(now);
}
