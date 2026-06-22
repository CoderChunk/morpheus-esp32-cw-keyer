/*
 * ============================================================================
 * MORPHEUS - Diagnostics and Services
 * ============================================================================
 *
 * File: services.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Reliable systems require visibility.
 *
 * This module provides diagnostics, logging, runtime information,
 * operator feedback, and optional hardware services.
 *
 * Contributors can expand this subsystem with:
 *
 *   • Performance monitoring
 *   • Event recording
 *   • Statistics collection
 *   • Debug interfaces
 *   • Telemetry systems
 *   • Maintenance tools
 *
 * MORPHEUS treats diagnostics as a first-class feature.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "services.h"
#include "core_decoder.h"   // only needed here, for the STATUS heartbeat's
                            // in-progress-pattern field - not part of
                            // services.h's public surface.
#include "config.h"
#include <string.h>

#if FEATURE_POTENTIOMETER
static float smoothedAdc = 0.0f;
#endif

void services_init() {
#if FEATURE_POTENTIOMETER
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  smoothedAdc = (float)analogRead(PIN_POT_WPM);
#endif
}

void services_servicePotentiometer(unsigned long now) {
  (void)now;
#if FEATURE_POTENTIOMETER
  int raw = analogRead(PIN_POT_WPM);
  smoothedAdc += (raw - smoothedAdc) * ADC_SMOOTHING_ALPHA;

  int wpm = map((long)smoothedAdc, 0, 4095, WPM_MIN, WPM_MAX);
  wpm = constrain(wpm, WPM_MIN, WPM_MAX);

  core_keyer_setWpm(wpm);
#endif
}

#if FEATURE_SERIAL

// Change-detection snapshot for the STATUS heartbeat - suppresses repeated
// identical lines while idle; a real change (or the very first call) still
// prints immediately.
static unsigned long lastSerialStatusMs = 0;
static OperatingMode lastStatusMode     = MODE_STRAIGHT;
static int           lastStatusWpm      = 0;
static bool          lastStatusTx       = false;
static KeyState      lastStatusKeyState = STATE_IDLE;
static char          lastStatusPat[MAX_PATTERN_LEN] = {0};
static bool          statusEverPrinted  = false;

void services_logModeChange(OperatingMode newMode) {
  Serial.print(F("EVT MODE mode="));
  Serial.println(newMode == MODE_STRAIGHT ? "STRAIGHT" : "PADDLE");
}

void services_logKeyDown(unsigned long now) {
  Serial.print(F("EVT KEYDOWN t=")); Serial.println(now);
}

void services_logKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now) {
  Serial.print(F("EVT KEYUP   t=")); Serial.print(now);
  Serial.print(F(" dur=")); Serial.println(durMs);

  Serial.print(F("EVT CLASS_"));
  Serial.print(type == ELEM_DIT ? "DIT" : "DAH");
  Serial.print(F(" dur=")); Serial.print(durMs);
  Serial.print(F(" thresh=")); Serial.println(thresholdMs);

  Serial.print(F("EVT ELEMDUR type="));
  Serial.print(type == ELEM_DIT ? "DIT" : "DAH");
  Serial.print(F(" dur=")); Serial.println(durMs);
}

void services_logCharacterComplete(char decodedChar, const char *pattern) {
  Serial.print(F("EVT CHAR pattern=")); Serial.print(pattern);
  Serial.print(F(" char=")); Serial.println(decodedChar);
}

void services_logWordComplete(const char *word, unsigned long now) {
  (void)now;
  Serial.print(F("EVT WORD text=")); Serial.println(word);
}

void services_serviceDiagnostics(unsigned long now) {
  if (now - lastSerialStatusMs < SERIAL_STATUS_INTERVAL_MS) return;
  lastSerialStatusMs = now;

  OperatingMode mode = core_keyer_getMode();
  int wpm = core_keyer_getWpm();
  bool tx = core_keyer_isTxActive();
  KeyState ks = core_keyer_getKeyState(now);
  const char *patNow = core_decoder_getCharPatternLen() > 0 ? core_decoder_getCharPattern() : "-";

  bool changed = !statusEverPrinted ||
                 mode != lastStatusMode ||
                 wpm  != lastStatusWpm  ||
                 tx   != lastStatusTx   ||
                 ks   != lastStatusKeyState ||
                 strcmp(patNow, lastStatusPat) != 0;
  if (!changed) return;

  statusEverPrinted  = true;
  lastStatusMode     = mode;
  lastStatusWpm      = wpm;
  lastStatusTx       = tx;
  lastStatusKeyState = ks;
  strncpy(lastStatusPat, patNow, sizeof(lastStatusPat) - 1);
  lastStatusPat[sizeof(lastStatusPat) - 1] = '\0';

  const char *stateStr = (ks == STATE_DIT) ? "DIT" : (ks == STATE_DAH) ? "DAH" : "IDLE";

  Serial.print(F("STATUS mode="));
  Serial.print(mode == MODE_STRAIGHT ? "STRAIGHT" : "PADDLE");
  Serial.print(F(" wpm=")); Serial.print(wpm);
  Serial.print(F(" ditMs=")); Serial.print(core_keyer_getDitLengthMs());
  Serial.print(F(" tx=")); Serial.print(tx ? 1 : 0);
  Serial.print(F(" state=")); Serial.print(stateStr);
  Serial.print(F(" pat="));
  Serial.println(patNow);
}

#else // !FEATURE_SERIAL - stub implementations

void services_logModeChange(OperatingMode newMode) { (void)newMode; }
void services_logKeyDown(unsigned long now) { (void)now; }
void services_logKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now) {
  (void)type; (void)durMs; (void)thresholdMs; (void)now;
}
void services_logCharacterComplete(char decodedChar, const char *pattern) { (void)decodedChar; (void)pattern; }
void services_logWordComplete(const char *word, unsigned long now) { (void)word; (void)now; }
void services_serviceDiagnostics(unsigned long now) { (void)now; }

#endif

unsigned long services_getUptimeMs() {
  return millis();
}

uint32_t services_getFreeHeapBytes() {
  return (uint32_t)ESP.getFreeHeap();
}