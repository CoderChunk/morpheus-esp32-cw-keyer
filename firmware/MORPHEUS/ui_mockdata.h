/*
 * ============================================================================
 * MORPHEUS Standalone UI - UI Model Render Cache
 * ============================================================================
 * File: ui_mockdata.h | Author: Coder Chunk | License: GPLv3
 *
 * Despite the file name, every field here holds real, live data synced
 * from MORPHEUS by display.cpp's polling loop (pollLiveData()) - none of
 * it is mock content. callsign/date/time come from services.cpp (NVS)
 * and core_clock.cpp's no-RTC software clock, not a real-time clock chip,
 * but that's a genuine backend, not a placeholder.
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
  bool        decoderEnabled;     // CW Keyer > Decoder toggle
  bool        livePatternEnabled; // Settings > Display > Live Pattern toggle

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