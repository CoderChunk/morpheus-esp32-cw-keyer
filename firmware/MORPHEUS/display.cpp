/*
 * ============================================================================
 * MORPHEUS - Operator Display System
 * ============================================================================
 *
 * File: display.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Morse is meant to be heard, but MORPHEUS also makes it visible.
 *
 * This module renders operating information, decoded text, live patterns,
 * connection status, and pairing information on the OLED display.
 *
 * The display system was designed to remain independent from the transport
 * and decoding layers, allowing contributors to experiment with:
 *
 *   • Alternative display technologies
 *   • Different layouts
 *   • Large-screen interfaces
 *   • Spectrum displays
 *   • Training visualizations
 *
 * User interfaces evolve. The architecture allows them to evolve freely.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "display.h"
#include "config.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include <string.h>

#if FEATURE_OLED
#include <Wire.h>
#include <U8g2lib.h>
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

// Shared state - written unconditionally so transport.cpp can always call
// display_setTransportStatus()/display_appendWord() without its own
// FEATURE_OLED guard; the heavy rendering work below is what's actually
// gated.
static char displayTranscript[TRANSCRIPT_LEN];
static DisplayLinkStatus linkStatus = DISPLAY_LINK_ADV;
static uint32_t lastPasskey = 0;

void display_setTransportStatus(DisplayLinkStatus status, uint32_t passkey) {
  linkStatus = status;
  lastPasskey = passkey;
}

void display_appendWord(const char *word) {
  if (word == NULL || word[0] == '\0') return;

#if FEATURE_OLED
  char temp[TRANSCRIPT_LEN + MAX_WORD_LEN + 2];
  if (displayTranscript[0] == '\0') {
    snprintf(temp, sizeof(temp), "%s", word);
  } else {
    snprintf(temp, sizeof(temp), "%s %s", displayTranscript, word);
  }

  size_t newLen = strlen(temp);
  if (newLen < TRANSCRIPT_LEN) {
    strncpy(displayTranscript, temp, TRANSCRIPT_LEN - 1);
  } else {
    size_t start = newLen - (TRANSCRIPT_LEN - 1);
    strncpy(displayTranscript, temp + start, TRANSCRIPT_LEN - 1);
  }
  displayTranscript[TRANSCRIPT_LEN - 1] = '\0';
#endif
}

#if FEATURE_OLED

static unsigned long lastDisplayUpdateMs = 0;

static void renderPairingOverlay() {
  char line[24];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "BLE PAIRING REQUEST");

  u8g2.setFont(u8g2_font_7x14B_tr);
  snprintf(line, sizeof(line), "%06u", (unsigned)lastPasskey);
  int w = u8g2.getStrWidth(line);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, 38, line);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 60, "Enter on device");

  u8g2.sendBuffer();
}

static void renderPairResult(bool success) {
  const char *msg = success ? "PAIRED" : "PAIRING FAILED";
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  int w = u8g2.getStrWidth(msg);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, 36, msg);
  u8g2.sendBuffer();
}

static void renderOperatorScreen() {
  char line[32];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tr);

#if FEATURE_BLE
  const char *bleCode = (linkStatus == DISPLAY_LINK_SECURE)    ? "SECR" :
                        (linkStatus == DISPLAY_LINK_CONNECTED) ? "CONN" : "ADV";
  snprintf(line, sizeof(line), "%s %dWPM %s",
           core_keyer_getMode() == MODE_STRAIGHT ? "STR" : "PAD",
           core_keyer_getWpm(), bleCode);
#else
  snprintf(line, sizeof(line), "%s %dWPM",
           core_keyer_getMode() == MODE_STRAIGHT ? "STR" : "PAD",
           core_keyer_getWpm());
#endif
  u8g2.drawStr(0, 9, line);

  size_t fullLen = strlen(displayTranscript);
  char lineA[LINE_CHARS + 1];
  char lineB[LINE_CHARS + 1];

  if (fullLen <= LINE_CHARS) {
    strncpy(lineB, displayTranscript, LINE_CHARS);
    lineB[LINE_CHARS] = '\0';
    lineA[0] = '\0';
  } else {
    size_t bStart = fullLen - LINE_CHARS;
    strncpy(lineB, displayTranscript + bStart, LINE_CHARS);
    lineB[LINE_CHARS] = '\0';

    size_t aLen = (bStart > LINE_CHARS) ? LINE_CHARS : bStart;
    size_t aStart = bStart - aLen;
    strncpy(lineA, displayTranscript + aStart, aLen);
    lineA[aLen] = '\0';
  }

  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 24, lineA);
  u8g2.drawStr(0, 39, lineB);

  const char *liveWord = core_decoder_getWordBuffer();
  uint8_t wLen = core_decoder_getWordLen();
  if (wLen > LINE_CHARS) {
    liveWord = liveWord + (wLen - LINE_CHARS);
  }
  u8g2.setFont(u8g2_font_7x14_tr);
  u8g2.drawStr(0, 54, liveWord);

  u8g2.setFont(u8g2_font_6x10_tr);
  uint8_t patLen = core_decoder_getCharPatternLen();
  snprintf(line, sizeof(line), "PAT: %s", patLen > 0 ? core_decoder_getCharPattern() : "-");
  u8g2.drawStr(0, 63, line);

  u8g2.sendBuffer();
}

void display_init() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  u8g2.begin();
  u8g2.setI2CAddress(OLED_I2C_ADDR << 1);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 30, "MORPHEUS");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 46, "Booting...");
  u8g2.sendBuffer();
  delay(800);

  displayTranscript[0] = '\0';
}

void display_service(unsigned long now) {
  if (now - lastDisplayUpdateMs < DISPLAY_INTERVAL_MS) return;
  lastDisplayUpdateMs = now;

  if (linkStatus == DISPLAY_LINK_PAIRING)   { renderPairingOverlay();    return; }
  if (linkStatus == DISPLAY_LINK_PAIR_OK)   { renderPairResult(true);    return; }
  if (linkStatus == DISPLAY_LINK_PAIR_FAIL) { renderPairResult(false);   return; }

  renderOperatorScreen();
}

#else // !FEATURE_OLED - stub implementations so the public API stays linkable

void display_init() {}
void display_service(unsigned long now) { (void)now; }

#endif