/*
 * ============================================================================
 * MORPHEUS Standalone UI - UI Model Render Cache
 * ============================================================================
 * File: ui_mockdata.h | Author: Coder Chunk | License: GPLv3
 *
 * INTEGRATION NOTE: transcriptA/B, liveWord, and livePattern are now
 * writable char buffers instead of string literal pointers - they hold
 * real, live data synced from MORPHEUS by display.cpp/ui_backend, not
 * mock content. wpm/mode/bleStatus/bleConnected/isReceiving are likewise
 * kept fresh by display.cpp's polling loop. callsign/date/time remain
 * genuine placeholders - no NVS/RTC backend exists for them yet.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#ifndef UI_MOCKDATA_H
#define UI_MOCKDATA_H
#include <Arduino.h>
#include "ui_config.h"

enum UiMockMode { UI_MODE_STRAIGHT, UI_MODE_PADDLE };

struct UiStatusData {
  uint8_t     wpm;
  uint16_t    toneHz;
  bool        paddleReverse;
  UiMockMode  mode;

  const char *bleStatus;
  bool        bleConnected;
  bool        isReceiving;
  bool        decoderEnabled;   // new - real state, polled by display.cpp

  char transcriptA[UI_LINE_CHARS + 1];
  char transcriptB[UI_LINE_CHARS + 1];
  char liveWord[UI_LINE_CHARS + 1];
  char livePattern[UI_PATTERN_CHARS + 1];

  char callsign[12];
  bool callsignEnabled;
  char date[11];
  char time[9];   // was 6 - "HH:MM AM/PM" (12-hour format) needs 8 chars + null
};

extern UiStatusData uiStatus;

#endif