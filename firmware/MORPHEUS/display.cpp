/*
 * ============================================================================
 * MORPHEUS - Operator Display System (UI Integration Adapter)
 * ============================================================================
 *
 * File: display.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * INTEGRATION COMPLETE (this pass):
 *
 * 1. Thread-safe BLE bridge: display_setTransportStatus() (called from
 *    NimBLE's host task) now snapshots under portMUX, exactly the
 *    pattern this file's earlier version documented as mandatory. The
 *    loop()-task side reads the snapshot and translates it into
 *    ui_state's BLE vocabulary.
 *
 * 2. Live data polling: WPM, mode, decoder live-word/pattern, and the
 *    receiving indicator are pulled from ui_backend (which wraps
 *    core_keyer/core_decoder) on a throttled, change-detected basis
 *    (reusing config.h's existing DISPLAY_INTERVAL_MS, unused since the
 *    original adapter rewrite - restored to its original purpose here).
 *
 * 3. display_appendWord() now feeds the real transcript via
 *    ui_backend_appendWord()/getTranscriptLines(), replacing the
 *    previous no-op stub.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "display.h"
#include "config.h"

#if FEATURE_OLED
#include "ui_input.h"
#include "ui_state.h"
#include "ui_renderer.h"
#include "ui_backend.h"
#include "ui_mockdata.h"
#include <string.h>
#endif

#if FEATURE_OLED
static portMUX_TYPE bleDisplayMux = portMUX_INITIALIZER_UNLOCKED;
static volatile DisplayLinkStatus pendingBleStatus = DISPLAY_LINK_ADV;
static volatile uint32_t pendingBlePasskey = 0;
static volatile bool bleStatusChanged = false;
#endif

void display_setTransportStatus(DisplayLinkStatus status, uint32_t passkey) {
#if FEATURE_OLED
  portENTER_CRITICAL(&bleDisplayMux);
  pendingBleStatus = status;
  pendingBlePasskey = passkey;
  bleStatusChanged = true;
  portEXIT_CRITICAL(&bleDisplayMux);
#else
  (void)status; (void)passkey;
#endif
}

void display_appendWord(const char *word) {
#if FEATURE_OLED
  ui_backend_appendWord(word);
  ui_state_notifyDataChanged();
#else
  (void)word;
#endif
}

#if FEATURE_OLED

static UiBleStatus mapBleStatus(DisplayLinkStatus s) {
  switch (s) {
    case DISPLAY_LINK_ADV:       return UI_BLE_ADV;
    case DISPLAY_LINK_CONNECTED: return UI_BLE_CONNECTED;
    case DISPLAY_LINK_PAIRING:   return UI_BLE_PAIRING;
    case DISPLAY_LINK_PAIR_OK:   return UI_BLE_PAIR_OK;
    case DISPLAY_LINK_PAIR_FAIL: return UI_BLE_PAIR_FAIL;
    case DISPLAY_LINK_SECURE:    return UI_BLE_SECURE;
    default:                     return UI_BLE_ADV;
  }
}

static unsigned long lastPollMs = 0;
static uint8_t lastWpm = 0xFF;
static UiMockMode lastMode = (UiMockMode)0xFF;
static bool lastReceiving = false;
static bool lastDecoderEnabled = true;
static char lastLiveWord[UI_LINE_CHARS + 1] = "";
static char lastLivePattern[UI_PATTERN_CHARS + 1] = "";

static void pollLiveData(unsigned long now) {
  if (now - lastPollMs < DISPLAY_INTERVAL_MS) return;
  lastPollMs = now;

  bool changed = false;

  uint8_t wpm = (uint8_t)ui_backend_getWpm();
  if (wpm != lastWpm) { uiStatus.wpm = wpm; lastWpm = wpm; changed = true; }

  UiMockMode mode = (strcmp(ui_backend_getModeStr(), "STR") == 0) ? UI_MODE_STRAIGHT : UI_MODE_PADDLE;
  if (mode != lastMode) { uiStatus.mode = mode; lastMode = mode; changed = true; }

  bool receiving = ui_backend_isReceiving();
  if (receiving != lastReceiving) { uiStatus.isReceiving = receiving; lastReceiving = receiving; changed = true; }

  bool decoderEn = ui_backend_getDecoderEnabled();
  if (decoderEn != lastDecoderEnabled) { uiStatus.decoderEnabled = decoderEn; lastDecoderEnabled = decoderEn; changed = true; }

  char liveWord[UI_LINE_CHARS + 1];
  ui_backend_getLiveWord(liveWord, sizeof(liveWord));
  if (strcmp(liveWord, lastLiveWord) != 0) {
    strncpy(uiStatus.liveWord, liveWord, sizeof(uiStatus.liveWord) - 1);
    uiStatus.liveWord[sizeof(uiStatus.liveWord) - 1] = '\0';
    strncpy(lastLiveWord, liveWord, sizeof(lastLiveWord) - 1);
    lastLiveWord[sizeof(lastLiveWord) - 1] = '\0';
    changed = true;
  }

  char livePattern[UI_PATTERN_CHARS + 1];
  ui_backend_getLivePattern(livePattern, sizeof(livePattern));
  if (strcmp(livePattern, lastLivePattern) != 0) {
    strncpy(uiStatus.livePattern, livePattern, sizeof(uiStatus.livePattern) - 1);
    uiStatus.livePattern[sizeof(uiStatus.livePattern) - 1] = '\0';
    strncpy(lastLivePattern, livePattern, sizeof(lastLivePattern) - 1);
    lastLivePattern[sizeof(lastLivePattern) - 1] = '\0';
    changed = true;
  }

  ui_backend_getTranscriptLines(uiStatus.transcriptA, sizeof(uiStatus.transcriptA),
                                 uiStatus.transcriptB, sizeof(uiStatus.transcriptB));

  if (changed) ui_state_notifyDataChanged();
}

void display_init() {
  ui_input_init();
  ui_state_init(millis());
  ui_renderer_init();
}

void display_service(unsigned long now) {
  ui_input_service(now);

  UiEvent ev;
  while (ui_input_poll(ev)) {
    ui_state_handleEvent(ev, now);
  }

  DisplayLinkStatus statusSnapshot;
  uint32_t passkeySnapshot;
  bool changed;
  portENTER_CRITICAL(&bleDisplayMux);
  changed = bleStatusChanged;
  statusSnapshot = pendingBleStatus;
  passkeySnapshot = pendingBlePasskey;
  bleStatusChanged = false;
  portEXIT_CRITICAL(&bleDisplayMux);
  if (changed) {
    ui_state_setBleStatus(mapBleStatus(statusSnapshot), passkeySnapshot, now);
  }

  pollLiveData(now);

  ui_state_service(now);
  ui_renderer_service(now);
}

#else

void display_init() {}
void display_service(unsigned long now) { (void)now; }

#endif