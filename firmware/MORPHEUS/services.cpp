/*
 * ============================================================================
 * MORPHEUS - Diagnostics and Services
 * ============================================================================
 * File: services.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds bleEnabled/bleLedEnabled persistence. Unlike
 * displayInvert/displayTimeoutIndex/callsign (whose live values are held
 * directly in this file as statics, since no other module owns them),
 * BLE's live values are owned by transport.cpp (bleEnabled - the actual
 * radio state) and core_led.cpp (bleLedEnabled - the LED preference).
 * services.cpp's snapshot/load/reset functions read and write through to
 * those modules' own getters/setters, exactly as it already does for
 * core_keyer/core_decoder/core_trainer - services.cpp is the persistence
 * layer, not necessarily the value's sole owner.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "services.h"
#include "core_decoder.h"
#include "core_trainer.h"
#include "core_clock.h"
#include "core_led.h"
#include "transport.h"
#include "config.h"
#include <string.h>
#include <Preferences.h>

static const char *SETTINGS_NVS_NAMESPACE = "morpheus_set";
static const char *SETTINGS_NVS_KEY       = "keyerSettings";

struct OperatorSettings {
  uint16_t      version;
  uint16_t      wpm;
  uint16_t      sidetoneHz;
  bool          paddleReverse;
  OperatingMode mode;
  bool          decoderEnabled;
  uint8_t       kochLevel;
  uint8_t       volumePercent;
  bool          sidetoneEnabled;
  IambicMode    iambicMode;
  uint8_t       weightPercent;
  bool          displayInvert;
  uint8_t       displayTimeoutIndex;
  char          callsign[CALLSIGN_MAX_LEN];
  bool          callsignEnabled;
  uint8_t       dateFormat;
  uint8_t       timeFormat;
  bool          bleEnabled;
  bool          bleLedEnabled;
};

static Preferences settingsPrefs;
static OperatorSettings lastSavedSettings;
static unsigned long lastSettingsSaveMs = 0;
static bool settingsLoaded = false;
static bool settingsWasDefaulted = false;

// Live values with no other module owner - services.cpp holds these
// directly, same as before this revision.
static bool    liveDisplayInvert       = DEFAULT_DISPLAY_INVERT;
static uint8_t liveDisplayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
static char    liveCallsign[CALLSIGN_MAX_LEN] = "";
static bool    liveCallsignEnabled     = DEFAULT_CALLSIGN_ENABLED;

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
  s.mode = core_keyer_getMode();
  s.decoderEnabled = core_decoder_isEnabled();
  s.kochLevel = core_trainer_getKochLevel();
  s.volumePercent = core_keyer_getVolume();
  s.sidetoneEnabled = core_keyer_getSidetoneEnabled();
  s.iambicMode = core_keyer_getIambicMode();
  s.weightPercent = core_keyer_getWeightPercent();
  s.displayInvert = liveDisplayInvert;
  s.displayTimeoutIndex = liveDisplayTimeoutIndex;
  strncpy(s.callsign, liveCallsign, CALLSIGN_MAX_LEN - 1);
  s.callsign[CALLSIGN_MAX_LEN - 1] = '\0';
  s.callsignEnabled = liveCallsignEnabled;
  s.dateFormat = core_clock_getDateFormat();
  s.timeFormat = core_clock_getTimeFormat();
  s.bleEnabled = transport_getBleEnabled();
  s.bleLedEnabled = core_led_getBleLedEnabled();
  return s;
}

void services_loadSettings() {
  settingsPrefs.begin(SETTINGS_NVS_NAMESPACE, false);

  OperatorSettings defaults;
  defaults.version        = SETTINGS_VERSION;
  defaults.wpm            = (uint16_t)DEFAULT_WPM;
  defaults.sidetoneHz     = (uint16_t)TONE_FREQ_HZ;
  defaults.paddleReverse  = DEFAULT_PADDLE_REVERSED;
  defaults.mode           = MODE_STRAIGHT;
  defaults.decoderEnabled = true;
  defaults.kochLevel      = DEFAULT_KOCH_LEVEL;
  defaults.volumePercent  = DEFAULT_VOLUME_PERCENT;
  defaults.sidetoneEnabled = DEFAULT_SIDETONE_ENABLED;
  defaults.iambicMode = IAMBIC_MODE_B;
  defaults.weightPercent = DEFAULT_WEIGHT_PERCENT;
  defaults.displayInvert = DEFAULT_DISPLAY_INVERT;
  defaults.displayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
  defaults.callsign[0] = '\0';
  defaults.callsignEnabled = DEFAULT_CALLSIGN_ENABLED;
  defaults.dateFormat = DEFAULT_DATE_FORMAT;
  defaults.timeFormat = DEFAULT_TIME_FORMAT;
  defaults.bleEnabled = false;       // radio silent by default
  defaults.bleLedEnabled = false;    // LED silent by default, independent preference

  OperatorSettings loaded = defaults;
  size_t got = settingsPrefs.getBytes(SETTINGS_NVS_KEY, &loaded, sizeof(loaded));
  bool needsUpgradeWrite = (got != sizeof(loaded)) || (loaded.version != SETTINGS_VERSION);
  if (needsUpgradeWrite) loaded = defaults;
  settingsWasDefaulted = needsUpgradeWrite;

  core_keyer_setWpm(loaded.wpm);
#if FEATURE_SIDETONE
  core_keyer_setSidetoneFreq(loaded.sidetoneHz);
#endif
  core_keyer_setPaddleReversed(loaded.paddleReverse);
  core_keyer_setMode(loaded.mode);
  core_decoder_setEnabled(loaded.decoderEnabled);
  core_trainer_setKochLevel(loaded.kochLevel);
  core_keyer_setVolume(loaded.volumePercent);
  core_keyer_setSidetoneEnabled(loaded.sidetoneEnabled);
  core_keyer_setIambicMode(loaded.iambicMode);
  core_keyer_setWeightPercent(loaded.weightPercent);
  liveDisplayInvert = loaded.displayInvert;
  liveDisplayTimeoutIndex = loaded.displayTimeoutIndex;
  strncpy(liveCallsign, loaded.callsign, CALLSIGN_MAX_LEN - 1);
  liveCallsign[CALLSIGN_MAX_LEN - 1] = '\0';
  liveCallsignEnabled = loaded.callsignEnabled;
  core_clock_setDateFormat(loaded.dateFormat);
  core_clock_setTimeFormat(loaded.timeFormat);

  // NOTE ON ORDERING: transport_init() and core_led_init() must both
  // have already run before this point (see MORPHEUS.ino setup() order)
  // - these two calls are what actually restore last session's BLE
  // radio state and LED preference, including auto-resuming advertising
  // if bleEnabled was true.
  core_led_setBleLedEnabled(loaded.bleLedEnabled);
  transport_setBleEnabled(loaded.bleEnabled);

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
                 (current.paddleReverse != lastSavedSettings.paddleReverse) ||
                 (current.mode != lastSavedSettings.mode) ||
                 (current.decoderEnabled != lastSavedSettings.decoderEnabled) ||
                 (current.kochLevel != lastSavedSettings.kochLevel) ||
                 (current.volumePercent != lastSavedSettings.volumePercent) ||
                 (current.sidetoneEnabled != lastSavedSettings.sidetoneEnabled) ||
                 (current.iambicMode != lastSavedSettings.iambicMode) ||
                 (current.weightPercent != lastSavedSettings.weightPercent) ||
                 (current.displayInvert != lastSavedSettings.displayInvert) ||
                 (current.displayTimeoutIndex != lastSavedSettings.displayTimeoutIndex) ||
                 (current.callsignEnabled != lastSavedSettings.callsignEnabled) ||
                 (strcmp(current.callsign, lastSavedSettings.callsign) != 0) ||
                 (current.dateFormat != lastSavedSettings.dateFormat) ||
                 (current.timeFormat != lastSavedSettings.timeFormat) ||
                 (current.bleEnabled != lastSavedSettings.bleEnabled) ||
                 (current.bleLedEnabled != lastSavedSettings.bleLedEnabled);
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
  core_keyer_setMode(MODE_STRAIGHT);
  core_decoder_setEnabled(true);
  core_trainer_resetKochProgress();
  core_keyer_setVolume(DEFAULT_VOLUME_PERCENT);
  core_keyer_setSidetoneEnabled(DEFAULT_SIDETONE_ENABLED);
  core_keyer_setIambicMode(IAMBIC_MODE_B);
  core_keyer_setWeightPercent(DEFAULT_WEIGHT_PERCENT);
  liveDisplayInvert = DEFAULT_DISPLAY_INVERT;
  liveDisplayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
  liveCallsign[0] = '\0';
  liveCallsignEnabled = DEFAULT_CALLSIGN_ENABLED;
  core_clock_setDateFormat(DEFAULT_DATE_FORMAT);
  core_clock_setTimeFormat(DEFAULT_TIME_FORMAT);
  core_led_setBleLedEnabled(false);
  transport_setBleEnabled(false);   // factory reset returns BLE to silent-by-default

  lastSavedSettings  = currentSettingsSnapshot();
  lastSettingsSaveMs = millis();
  settingsWasDefaulted = true;
#if FEATURE_SERIAL
  Serial.println(F("EVT SETTINGS_FACTORY_RESET"));
#endif
}

bool          services_wasSettingsDefaulted()   { return settingsWasDefaulted; }
unsigned long services_getLastSettingsSaveMs()  { return lastSettingsSaveMs; }
const char   *services_getSettingsNamespace()   { return SETTINGS_NVS_NAMESPACE; }

void services_init() {}

static uint32_t      loopCounter          = 0;
static unsigned long loopRateWindowStartMs = 0;
static uint32_t       lastLoopRate         = 0;

void services_tickLoopCounter(unsigned long now) {
  loopCounter++;
  if (now - loopRateWindowStartMs >= 1000) {
    lastLoopRate = loopCounter;
    loopCounter = 0;
    loopRateWindowStartMs = now;
  }
}

uint32_t services_getLoopRateHz() { return lastLoopRate; }

bool    services_getDisplayInvert()        { return liveDisplayInvert; }
void    services_setDisplayInvert(bool v)  { liveDisplayInvert = v; }
uint8_t services_getDisplayTimeoutIndex()  { return liveDisplayTimeoutIndex; }
void    services_setDisplayTimeoutIndex(uint8_t i) { liveDisplayTimeoutIndex = i; }

void services_getCallsign(char *out, size_t outSize) {
  strncpy(out, liveCallsign, outSize - 1);
  out[outSize - 1] = '\0';
}
void services_setCallsign(const char *value) {
  strncpy(liveCallsign, value, CALLSIGN_MAX_LEN - 1);
  liveCallsign[CALLSIGN_MAX_LEN - 1] = '\0';
}
bool services_getCallsignEnabled()       { return liveCallsignEnabled; }
void services_setCallsignEnabled(bool v) { liveCallsignEnabled = v; }

uint8_t services_getDateFormat()          { return core_clock_getDateFormat(); }
void    services_setDateFormat(uint8_t i) { core_clock_setDateFormat(i); }
uint8_t services_getTimeFormat()          { return core_clock_getTimeFormat(); }
void    services_setTimeFormat(uint8_t i) { core_clock_setTimeFormat(i); }

bool services_getBleEnabled()        { return transport_getBleEnabled(); }
void services_setBleEnabled(bool v)  { transport_setBleEnabled(v); }
bool services_getBleLedEnabled()     { return core_led_getBleLedEnabled(); }
void services_setBleLedEnabled(bool v) { core_led_setBleLedEnabled(v); }

#if FEATURE_SERIAL

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
                 mode != lastStatusMode || wpm != lastStatusWpm ||
                 tx != lastStatusTx || ks != lastStatusKeyState ||
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
  Serial.print(F(" pat=")); Serial.println(patNow);
}

#else

void services_logModeChange(OperatingMode newMode) { (void)newMode; }
void services_logKeyDown(unsigned long now) { (void)now; }
void services_logKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now) {
  (void)type; (void)durMs; (void)thresholdMs; (void)now;
}
void services_logCharacterComplete(char decodedChar, const char *pattern) { (void)decodedChar; (void)pattern; }
void services_logWordComplete(const char *word, unsigned long now) { (void)word; (void)now; }
void services_serviceDiagnostics(unsigned long now) { (void)now; }

#endif

unsigned long services_getUptimeMs() { return millis(); }
uint32_t services_getFreeHeapBytes() { return (uint32_t)ESP.getFreeHeap(); }