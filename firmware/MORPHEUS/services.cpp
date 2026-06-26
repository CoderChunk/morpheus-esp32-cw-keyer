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
#include <Preferences.h>

static const char *SETTINGS_NVS_NAMESPACE = "morpheus_set";
static const char *SETTINGS_NVS_KEY       = "keyerSettings";

struct OperatorSettings {
  uint16_t version;
  uint16_t wpm;
  uint16_t sidetoneHz;
  bool     paddleReverse;
};

static Preferences settingsPrefs;
static OperatorSettings lastSavedSettings;
static unsigned long lastSettingsSaveMs = 0;
static bool settingsLoaded = false;

static OperatorSettings currentSettingsSnapshot() {
  OperatorSettings s;
  s.version = SETTINGS_VERSION;
  s.wpm = (uint16_t)core_keyer_getWpm();
#if FEATURE_SIDETONE
  s.sidetoneHz = (uint16_t)core_keyer_getSidetoneFreq();
#else
  s.sidetoneHz = (uint16_t)TONE_FREQ_HZ;
#endif
  s.paddleReverse = core_keyer_getPaddleReversed();
  return s;
}

void services_loadSettings() {
  settingsPrefs.begin(SETTINGS_NVS_NAMESPACE, false);

  OperatorSettings defaults;
  defaults.version       = SETTINGS_VERSION;
  defaults.wpm           = (uint16_t)DEFAULT_WPM;
  defaults.sidetoneHz    = (uint16_t)TONE_FREQ_HZ;
  defaults.paddleReverse = DEFAULT_PADDLE_REVERSED;

  OperatorSettings loaded = defaults;
  size_t got = settingsPrefs.getBytes(SETTINGS_NVS_KEY, &loaded, sizeof(loaded));
  bool needsUpgradeWrite = (got != sizeof(loaded)) || (loaded.version != SETTINGS_VERSION);
  if (needsUpgradeWrite) loaded = defaults;

  core_keyer_setWpm(loaded.wpm);
#if FEATURE_SIDETONE
  core_keyer_setSidetoneFreq(loaded.sidetoneHz);
#endif
  core_keyer_setPaddleReversed(loaded.paddleReverse);

  lastSavedSettings  = currentSettingsSnapshot();
  lastSettingsSaveMs = millis();
  settingsLoaded     = true;

  if (needsUpgradeWrite) {
    settingsPrefs.putBytes(SETTINGS_NVS_KEY, &lastSavedSettings, sizeof(lastSavedSettings));
#if FEATURE_SERIAL
    Serial.println(F("EVT SETTINGS_UPGRADE_WRITE"));
#endif
  }
}

void services_serviceSettings(unsigned long now) {
  if (!settingsLoaded) return;
  if (now - lastSettingsSaveMs < SETTINGS_SAVE_DEBOUNCE_MS) return;

  OperatorSettings current = currentSettingsSnapshot();
  bool changed = (current.wpm != lastSavedSettings.wpm) ||
                 (current.sidetoneHz != lastSavedSettings.sidetoneHz) ||
                 (current.paddleReverse != lastSavedSettings.paddleReverse);
  if (!changed) return;

  settingsPrefs.putBytes(SETTINGS_NVS_KEY, &current, sizeof(current));
  lastSavedSettings  = current;
  lastSettingsSaveMs = now;
#if FEATURE_SERIAL
  Serial.println(F("EVT SETTINGS_SAVE"));
#endif
}

void services_factoryResetSettings() {
  settingsPrefs.remove(SETTINGS_NVS_KEY);

  core_keyer_setWpm(DEFAULT_WPM);
#if FEATURE_SIDETONE
  core_keyer_setSidetoneFreq(TONE_FREQ_HZ);
#endif
  core_keyer_setPaddleReversed(DEFAULT_PADDLE_REVERSED);

  lastSavedSettings  = currentSettingsSnapshot();
  lastSettingsSaveMs = millis();
#if FEATURE_SERIAL
  Serial.println(F("EVT SETTINGS_FACTORY_RESET"));
#endif
}

void services_init() {
  // No hardware services to initialize at present. The potentiometer ADC
  // setup that formerly lived here was removed when the WPM pot was retired
  // (GPIO34 is now reserved for the navigation keypad). Retained as a stable
  // lifecycle hook called from setup().
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