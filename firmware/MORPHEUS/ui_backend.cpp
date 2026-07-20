/*
 * ============================================================================
 * MORPHEUS - UI Model Bridge Implementation
 * ============================================================================
 * File: ui_backend.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "ui_backend.h"
#include "ui_config.h"
#include "ui_renderer.h"
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
#include "transport.h"
#include "services.h"
#include <esp_system.h>
#include <string.h>
#include <stdio.h>

int ui_backend_getWpm() { return core_keyer_getWpm(); }
void ui_backend_setWpm(int wpm) { core_keyer_setWpm(wpm); }

uint16_t ui_backend_getToneHz() { return (uint16_t)core_keyer_getSidetoneFreq(); }
void ui_backend_setToneHz(uint16_t hz) { core_keyer_setSidetoneFreq(hz); }

bool ui_backend_getPaddleReversed() { return core_keyer_getPaddleReversed(); }
void ui_backend_setPaddleReversed(bool reversed) { core_keyer_setPaddleReversed(reversed); }

const char *ui_backend_getModeStr() {
  return (core_keyer_getMode() == MODE_STRAIGHT) ? "STR" : "PAD";
}

void ui_backend_getLiveWord(char *out, size_t outSize) {
  const char *w = core_decoder_getWordBuffer();
  uint8_t len = core_decoder_getWordLen();
  if (len > UI_LINE_CHARS) w = w + (len - UI_LINE_CHARS);
  strncpy(out, w, outSize - 1);
  out[outSize - 1] = '\0';
}

void ui_backend_getLivePattern(char *out, size_t outSize) {
  uint8_t len = core_decoder_getCharPatternLen();
  const char *p = (len > 0) ? core_decoder_getCharPattern() : "-";
  strncpy(out, p, outSize - 1);
  out[outSize - 1] = '\0';
}

bool ui_backend_isReceiving() {
  return core_decoder_getCharPatternLen() > 0;
}

static char transcriptFull[TRANSCRIPT_LEN];

void ui_backend_appendWord(const char *word) {
  if (word == nullptr || word[0] == '\0') return;

  char temp[TRANSCRIPT_LEN + MAX_WORD_LEN + 2];
  if (transcriptFull[0] == '\0') {
    snprintf(temp, sizeof(temp), "%s", word);
  } else {
    snprintf(temp, sizeof(temp), "%s %s", transcriptFull, word);
  }

  size_t newLen = strlen(temp);
  if (newLen < TRANSCRIPT_LEN) {
    strncpy(transcriptFull, temp, TRANSCRIPT_LEN - 1);
  } else {
    size_t start = newLen - (TRANSCRIPT_LEN - 1);
    strncpy(transcriptFull, temp + start, TRANSCRIPT_LEN - 1);
  }
  transcriptFull[TRANSCRIPT_LEN - 1] = '\0';
}

void ui_backend_getTranscriptLines(char *lineA, size_t aSize, char *lineB, size_t bSize) {
  size_t fullLen = strlen(transcriptFull);

  if (fullLen <= UI_LINE_CHARS) {
    strncpy(lineB, transcriptFull, bSize - 1);
    lineB[bSize - 1] = '\0';
    lineA[0] = '\0';
    return;
  }

  size_t bStart = fullLen - UI_LINE_CHARS;
  strncpy(lineB, transcriptFull + bStart, bSize - 1);
  lineB[bSize - 1] = '\0';

  size_t aLen = (bStart > UI_LINE_CHARS) ? UI_LINE_CHARS : bStart;
  size_t aStart = bStart - aLen;
  size_t copyLen = (aLen < aSize - 1) ? aLen : (aSize - 1);
  strncpy(lineA, transcriptFull + aStart, copyLen);
  lineA[copyLen] = '\0';
}

bool ui_backend_bleIsConnected()     { return transport_isConnected(); }
bool ui_backend_bleIsSecure()        { return transport_isSecure(); }
bool ui_backend_bleHasTrustedDevice(){ return transport_hasTrustedDevice(); }
const char *ui_backend_getDeviceName() { return BLE_DEVICE_NAME; }
uint16_t ui_backend_getBleMtu() { return transport_getCurrentMtu(); }

unsigned long ui_backend_getUptimeMs()   { return services_getUptimeMs(); }
uint32_t      ui_backend_getFreeHeapBytes() { return services_getFreeHeapBytes(); }

bool ui_backend_isTipActive()  { return core_keyer_isTipActive(); }
bool ui_backend_isRingActive() { return core_keyer_isRingActive(); }

bool ui_backend_diagToneStart(uint16_t hz) { return core_keyer_diagToneStart(hz); }
void ui_backend_diagToneStop() { core_keyer_diagToneStop(); }
bool ui_backend_isDiagToneActive() { return core_keyer_isDiagToneActive(); }

const char *ui_backend_getChipModel() {
  static char buf[24];
  String m = ESP.getChipModel();
  strncpy(buf, m.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

uint8_t ui_backend_getChipRevision() { return ESP.getChipRevision(); }
uint32_t ui_backend_getCpuFreqMHz() { return ESP.getCpuFreqMHz(); }

const char *ui_backend_getSdkVersionStr() {
  static char buf[24];
  strncpy(buf, ESP.getSdkVersion(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

const char *ui_backend_getResetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_EXT:       return "EXT_PIN";
    default:                return "UNKNOWN";
  }
}

const char *ui_backend_getChipIdHex() {
  static char buf[20];
  uint64_t mac = ESP.getEfuseMac();
  uint32_t hi = (uint32_t)(mac >> 32);
  uint32_t lo = (uint32_t)(mac & 0xFFFFFFFFUL);
  snprintf(buf, sizeof(buf), "%04X%08X", hi, lo);
  return buf;
}

uint32_t ui_backend_getFlashSizeBytes()       { return ESP.getFlashChipSize(); }
uint32_t ui_backend_getSketchSizeBytes()      { return ESP.getSketchSize(); }
uint32_t ui_backend_getFreeSketchSpaceBytes() { return ESP.getFreeSketchSpace(); }
uint32_t ui_backend_getMinFreeHeapBytes()     { return ESP.getMinFreeHeap(); }
uint32_t ui_backend_getHeapSizeBytes()        { return ESP.getHeapSize(); }
uint32_t ui_backend_getLoopRateHz()           { return services_getLoopRateHz(); }

bool          ui_backend_settingsWasDefaulted()  { return services_wasSettingsDefaulted(); }
unsigned long ui_backend_getLastSettingsSaveMs() { return services_getLastSettingsSaveMs(); }
const char   *ui_backend_getSettingsNamespace()  { return services_getSettingsNamespace(); }

struct GpioMonEntry { const char *name; uint8_t pin; };

static const GpioMonEntry GPIO_MON_TABLE[] = {
  { "KEY/DIT",  PIN_JACK_TIP },
  { "DAH",      PIN_JACK_RING },
  { "BUZZER",   PIN_BUZZER },
  { "ENC A",    UI_PIN_ENC_A },
  { "ENC B",    UI_PIN_ENC_B },
  { "ENC PUSH", UI_PIN_ENC_PUSH },
  { "BACK",     UI_PIN_BACK },
  { "CONFIRM",  UI_PIN_CONFIRM },
};
static const uint8_t GPIO_MON_COUNT = sizeof(GPIO_MON_TABLE) / sizeof(GPIO_MON_TABLE[0]);

uint8_t ui_backend_getGpioMonCount() { return GPIO_MON_COUNT; }

void ui_backend_getGpioMonEntry(uint8_t idx, const char **nameOut, uint8_t *pinOut, int *levelOut) {
  if (idx >= GPIO_MON_COUNT) {
    *nameOut = ""; *pinOut = 0; *levelOut = -1;
    return;
  }
  *nameOut = GPIO_MON_TABLE[idx].name;
  *pinOut  = GPIO_MON_TABLE[idx].pin;
  *levelOut = digitalRead(GPIO_MON_TABLE[idx].pin);
}

bool ui_backend_getModeIsPaddle() { return core_keyer_getMode() == MODE_PADDLE; }

void ui_backend_setModeIsPaddle(bool paddle) {
  OperatingMode newMode = paddle ? MODE_PADDLE : MODE_STRAIGHT;
  OperatingMode oldMode = core_keyer_getMode();
  core_keyer_setMode(newMode);
#if FEATURE_SERIAL
  if (newMode != oldMode) services_logModeChange(newMode);
#endif
}

void ui_backend_bondReset()             { transport_resetBond(); }
void ui_backend_factoryResetSettings()  { services_factoryResetSettings(); }
void ui_backend_restartDevice()         { core_stats_forceFlush(); ESP.restart(); }

// ----------------------------------------------------------------------------
// CW Keyer submenu additions
// ----------------------------------------------------------------------------
bool ui_backend_getDecoderEnabled() { return core_decoder_isEnabled(); }
void ui_backend_setDecoderEnabled(bool enabled) { core_decoder_setEnabled(enabled); }

const char *ui_backend_getKeyStateStr() {
  switch (core_keyer_getKeyState(millis())) {
    case STATE_DIT: return "DIT";
    case STATE_DAH: return "DAH";
    default:        return "IDLE";
  }
}

bool ui_backend_isMemoryPlaying()      { return core_memory_isPlaying(); }
uint8_t ui_backend_getPlayingMemorySlot() { return core_memory_getPlayingSlot(); }
const char *ui_backend_getMemorySlotText(uint8_t slot) { return core_memory_getSlotText(slot); }
bool ui_backend_playMemory(uint8_t slot) { return core_memory_play(slot); }
void ui_backend_stopMemory()            { core_memory_stop(); }

// ----------------------------------------------------------------------------
// Training - uiDrillId values match ui_menu.h's UiTrainDrillId ordering
// (TRAIN_DRILL_KOCH=1 .. TRAIN_DRILL_EXAM=6): documented positional
// mapping, not a shared header - ui_state.cpp may not include
// core_trainer.h directly, per this project's frozen constraint.
// ----------------------------------------------------------------------------
void ui_backend_trainStartSession(uint8_t uiDrillId) {
  TrainMode mode;
  switch (uiDrillId) {
    case 1: mode = TRAIN_MODE_KOCH;       break;
    case 2: mode = TRAIN_MODE_CHARACTERS; break;
    case 3: mode = TRAIN_MODE_WORDS;      break;
    case 4: mode = TRAIN_MODE_CALLSIGNS;  break;
    case 5: mode = TRAIN_MODE_ADAPTIVE;   break;
    case 6: mode = TRAIN_MODE_EXAM;       break;
    default: return;
  }
  core_trainer_startSession(mode);
}

void        ui_backend_trainStopSession()           { core_trainer_stopSession(); }
bool        ui_backend_isTrainingSessionActive()     { return core_trainer_isSessionActive(); }
void        ui_backend_trainConfirmPressed()         { core_trainer_confirmPressed(); }
uint8_t     ui_backend_trainGetPhase()               { return (uint8_t)core_trainer_getPhase(); }
const char *ui_backend_trainGetTarget()              { return core_trainer_getTargetText(); }
const char *ui_backend_trainGetTyped()               { return core_trainer_getTypedText(); }
uint32_t    ui_backend_trainGetCorrectCount()        { return core_trainer_getCorrectCount(); }
uint32_t    ui_backend_trainGetTotalCount()          { return core_trainer_getTotalCount(); }
uint8_t     ui_backend_trainGetKochLevel()           { return core_trainer_getKochLevel(); }
void        ui_backend_trainGetKochCharset(char *out, size_t outSize) { core_trainer_getKochCharset(out, outSize); }
int         ui_backend_trainGetAdaptiveWpm()         { return core_trainer_getAdaptiveWpm(); }

bool    ui_backend_trainIsExamResultReady()   { return core_trainer_isExamResultReady(); }
uint8_t ui_backend_trainGetExamScorePercent() { return core_trainer_getExamScorePercent(); }
uint8_t ui_backend_trainGetExamCorrectCount() { return core_trainer_getExamCorrectCount(); }
bool    ui_backend_trainGetExamPassed()       { return core_trainer_getExamPassed(); }
uint8_t ui_backend_trainGetExamTotalCount()   { return core_trainer_getExamTotalCount(); }
uint8_t ui_backend_trainGetExamTargetLength() { return core_trainer_getExamTargetLength(); }
void    ui_backend_trainClearExamResult()     { core_trainer_clearExamResult(); }

void ui_backend_farnsworthSetWpm(int wpm)     { core_trainer_farnsworthSetEffectiveWpm(wpm); }
int  ui_backend_farnsworthGetWpm()            { return core_trainer_farnsworthGetEffectiveWpm(); }
bool ui_backend_farnsworthTogglePlay()        { return core_trainer_farnsworthTogglePlay(); }
bool ui_backend_isFarnsworthPlaying()         { return core_trainer_farnsworthIsPlaying(); }

// Statistics
uint32_t ui_backend_statsSessionChars()          { return core_stats_session_getCharsKeyed(); }
uint32_t ui_backend_statsSessionWords()          { return core_stats_session_getWordsKeyed(); }
uint32_t ui_backend_statsSessionElements()       { return core_stats_session_getElementsKeyed(); }
uint32_t ui_backend_statsSessionTrainAttempts()  { return core_stats_session_getTrainingAttempts(); }
uint32_t ui_backend_statsSessionTrainCorrect()   { return core_stats_session_getTrainingCorrect(); }
unsigned long ui_backend_statsSessionUptimeMs()  { return core_stats_session_getUptimeMs(); }

uint32_t ui_backend_statsLifetimeChars()         { return core_stats_lifetime_getCharsKeyed(); }
uint32_t ui_backend_statsLifetimeWords()         { return core_stats_lifetime_getWordsKeyed(); }
uint32_t ui_backend_statsLifetimeElements()      { return core_stats_lifetime_getElementsKeyed(); }
uint32_t ui_backend_statsLifetimeUptimeSec()     { return core_stats_lifetime_getUptimeSec(); }
uint32_t ui_backend_statsLifetimeTrainAttempts() { return core_stats_lifetime_getTrainingAttempts(); }
uint32_t ui_backend_statsLifetimeTrainCorrect()  { return core_stats_lifetime_getTrainingCorrect(); }
uint32_t ui_backend_statsModeAttempts(uint8_t mode) { return core_stats_lifetime_getModeAttempts((TrainMode)mode); }
uint32_t ui_backend_statsModeCorrect(uint8_t mode)  { return core_stats_lifetime_getModeCorrect((TrainMode)mode); }
uint32_t ui_backend_statsExamsTaken()            { return core_stats_lifetime_getExamsTaken(); }
uint32_t ui_backend_statsExamsPassed()           { return core_stats_lifetime_getExamsPassed(); }
uint8_t  ui_backend_statsBestExamScore()         { return core_stats_lifetime_getBestExamScore(); }
int      ui_backend_statsPeakAdaptiveWpm()       { return core_stats_lifetime_getPeakAdaptiveWpm(); }

uint8_t  ui_backend_statsHistoryCount()            { return core_stats_history_getCount(); }
uint16_t ui_backend_statsHistoryEntry(uint8_t idx) { return core_stats_history_getEntry(idx); }

// Games
bool ui_backend_isGameSessionActive() { return core_games_isSessionActive(); }

void ui_backend_gameStart(uint8_t uiGameId) {
  GameId g;
  switch (uiGameId) {
    case 1: g = GAME_COPY;   break;
    case 2: g = GAME_MEMORY; break;
    case 3: g = GAME_SPEED;  break;
    default: return;
  }
  core_games_start(g);
}
void ui_backend_gameStop()            { core_games_stop(); }
void ui_backend_gameConfirmPressed()  { core_games_confirmPressed(); }

uint8_t  ui_backend_gameCopyPhase()             { return (uint8_t)core_games_copy_getPhase(); }
char     ui_backend_gameCopyFallingChar()       { return core_games_copy_getFallingChar(); }
uint8_t  ui_backend_gameCopyFallProgressPct()   { return core_games_copy_getFallProgressPct(millis()); }
uint16_t ui_backend_gameCopyScore()             { return core_games_copy_getScore(); }
uint8_t  ui_backend_gameCopyLives()             { return core_games_copy_getLives(); }
uint16_t ui_backend_gameCopyHighScore()         { return core_games_copy_getHighScore(); }

uint8_t  ui_backend_gameMemoryPhase()           { return (uint8_t)core_games_memory_getPhase(); }
uint8_t  ui_backend_gameMemoryChainLength()     { return core_games_memory_getChainLength(); }
uint8_t  ui_backend_gameMemoryInputProgress()   { return core_games_memory_getInputProgress(); }
uint8_t  ui_backend_gameMemoryHighScore()       { return core_games_memory_getHighScore(); }

uint8_t       ui_backend_gameSpeedPhase()           { return (uint8_t)core_games_speed_getPhase(); }
uint16_t      ui_backend_gameSpeedCombo()           { return core_games_speed_getCombo(); }
uint8_t       ui_backend_gameSpeedLives()           { return core_games_speed_getLives(); }
unsigned long ui_backend_gameSpeedBeatRemainingMs() { return core_games_speed_getBeatRemainingMs(millis()); }
unsigned long ui_backend_gameSpeedBeatTotalMs()     { return core_games_speed_getBeatTotalMs(); }
bool          ui_backend_gameSpeedWasLastCorrect()  { return core_games_speed_wasLastCorrect(); }
char          ui_backend_gameSpeedLastChar()        { return core_games_speed_getLastChar(); }
uint16_t      ui_backend_gameSpeedHighScore()       { return core_games_speed_getHighScore(); }
bool          ui_backend_isGamePaused()         { return core_games_isPaused(); }
void          ui_backend_gameTogglePause()      { core_games_togglePause(); }
void          ui_backend_gameRestart()          { core_games_restart(); }

bool ui_backend_isGamePausedFor(uint8_t uiGameId) {
  if (!core_games_isSessionActive() || !core_games_isPaused()) return false;
  GameId active = core_games_getActiveGame();
  switch (uiGameId) {
    case 1: return active == GAME_COPY;
    case 2: return active == GAME_MEMORY;
    case 3: return active == GAME_SPEED;
    default: return false;
  }
}

uint8_t ui_backend_getVolume()              { return core_keyer_getVolume(); }
void    ui_backend_setVolume(uint8_t v)     { core_keyer_setVolume(v); }
bool    ui_backend_getSidetoneEnabled()     { return core_keyer_getSidetoneEnabled(); }
void    ui_backend_setSidetoneEnabled(bool e) { core_keyer_setSidetoneEnabled(e); }

// uiProfileId values match ui_menu.h's UiProfileId ordering
// (UI_PROFILE_DEFAULT=1 .. UI_PROFILE_PRACTICE=4) - same documented
// positional-mapping convention as ui_backend_trainStartSession().
static ProfileId toCoreProfileId(uint8_t uiProfileId) {
  switch (uiProfileId) {
    case 1: return PROFILE_DEFAULT;
    case 2: return PROFILE_PORTABLE;
    case 3: return PROFILE_CONTEST;
    case 4: return PROFILE_PRACTICE;
    default: return PROFILE_DEFAULT;
  }
}

void ui_backend_profileLoad(uint8_t uiProfileId) {
  ProfileId id = toCoreProfileId(uiProfileId);
  core_profiles_load(id);
  ui_renderer_setContrast(core_profiles_getContrast(id));
}

void ui_backend_profileSave(uint8_t uiProfileId) {
  ProfileId id = toCoreProfileId(uiProfileId);
  core_profiles_setContrastForSave(id, ui_renderer_getContrast());
  core_profiles_save(id);
}

int      ui_backend_profileGetWpm(uint8_t uiProfileId)            { return core_profiles_getWpm(toCoreProfileId(uiProfileId)); }
uint16_t ui_backend_profileGetToneHz(uint8_t uiProfileId)         { return core_profiles_getToneHz(toCoreProfileId(uiProfileId)); }
bool     ui_backend_profileGetPaddleReversed(uint8_t uiProfileId) { return core_profiles_getPaddleReversed(toCoreProfileId(uiProfileId)); }
const char *ui_backend_profileGetModeStr(uint8_t uiProfileId) {
  return (core_profiles_getMode(toCoreProfileId(uiProfileId)) == MODE_STRAIGHT) ? "STR" : "PAD";
}
uint8_t  ui_backend_profileGetVolume(uint8_t uiProfileId)          { return core_profiles_getVolume(toCoreProfileId(uiProfileId)); }
bool     ui_backend_profileGetSidetoneEnabled(uint8_t uiProfileId) { return core_profiles_getSidetoneEnabled(toCoreProfileId(uiProfileId)); }
uint8_t  ui_backend_profileGetContrast(uint8_t uiProfileId)        { return core_profiles_getContrast(toCoreProfileId(uiProfileId)); }

bool ui_backend_getIambicModeIsB() { return core_keyer_getIambicMode() == IAMBIC_MODE_B; }
void ui_backend_setIambicModeIsB(bool isB) { core_keyer_setIambicMode(isB ? IAMBIC_MODE_B : IAMBIC_MODE_A); }
uint8_t ui_backend_getWeightPercent() { return core_keyer_getWeightPercent(); }
void    ui_backend_setWeightPercent(uint8_t p) { core_keyer_setWeightPercent(p); }

bool    ui_backend_getDisplayInvert()      { return services_getDisplayInvert(); }
void    ui_backend_setDisplayInvert(bool v){ services_setDisplayInvert(v); ui_renderer_setInverted(v); }
uint8_t ui_backend_getDisplayTimeoutIndex() { return services_getDisplayTimeoutIndex(); }
void    ui_backend_setDisplayTimeoutIndex(uint8_t i) { services_setDisplayTimeoutIndex(i); }

unsigned long ui_backend_getDisplayTimeoutActualMs() {
  uint8_t idx = services_getDisplayTimeoutIndex();
  if (idx >= DISPLAY_TIMEOUT_OPTION_COUNT) idx = DEFAULT_DISPLAY_TIMEOUT_INDEX;
  uint16_t sec = DISPLAY_TIMEOUT_SECONDS[idx];
  return (sec == 0) ? 0 : (unsigned long)sec * 1000UL;
}

void ui_backend_getCallsign(char *out, size_t outSize) { services_getCallsign(out, outSize); }
void ui_backend_setCallsign(const char *value)         { services_setCallsign(value); }
bool ui_backend_getCallsignEnabled()                   { return services_getCallsignEnabled(); }
void ui_backend_setCallsignEnabled(bool enabled)       { services_setCallsignEnabled(enabled); }

bool ui_backend_clockIsSet() { return core_clock_isSet(); }
void ui_backend_clockSet(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi) { core_clock_set(y, mo, d, h, mi); }
void ui_backend_clockGetComponents(uint16_t &y, uint8_t &mo, uint8_t &d, uint8_t &h, uint8_t &mi) {
  core_clock_getComponents(y, mo, d, h, mi);
}
void ui_backend_clockGetDateStr(char *out, size_t outSize) { core_clock_getDateStr(out, outSize); }
void ui_backend_clockGetTimeStr(char *out, size_t outSize) { core_clock_getTimeStr(out, outSize); }
uint8_t ui_backend_getDateFormat()        { return services_getDateFormat(); }
void    ui_backend_setDateFormat(uint8_t i) { services_setDateFormat(i); }
uint8_t ui_backend_getTimeFormat()        { return services_getTimeFormat(); }
void    ui_backend_setTimeFormat(uint8_t i) { services_setTimeFormat(i); }

bool ui_backend_getBleEnabled()        { return services_getBleEnabled(); }
void ui_backend_setBleEnabled(bool v)  { services_setBleEnabled(v); }
bool ui_backend_getBleLedEnabled()     { return services_getBleLedEnabled(); }
void ui_backend_setBleLedEnabled(bool v) { services_setBleLedEnabled(v); }
void ui_backend_startBlePairing()      { transport_startPairingWindow(); }
bool ui_backend_isBlePairingActive()   { return transport_isPairingActive(); }