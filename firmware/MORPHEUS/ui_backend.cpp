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
#include "config.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "core_memory.h"
#include "core_trainer.h"
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
void ui_backend_restartDevice()         { ESP.restart(); }

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