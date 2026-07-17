/*
 * ============================================================================
 * MORPHEUS - UI Model Bridge
 * ============================================================================
 * File: ui_backend.h | Author: Coder Chunk | License: GPLv3
 *
 * DIAGNOSTICS ADDITION: system/chip/flash/heap/reset-reason wrappers,
 * raw key/paddle state, diagnostic tone control, NVS status, BLE MTU,
 * and a GPIO Monitor table. All are read-only or explicitly gated
 * (diagnostic tone never overrides real keying - see core_keyer.h).
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#ifndef UI_BACKEND_H
#define UI_BACKEND_H

#include <Arduino.h>

int      ui_backend_getWpm();
void     ui_backend_setWpm(int wpm);
uint16_t ui_backend_getToneHz();
void     ui_backend_setToneHz(uint16_t hz);
bool     ui_backend_getPaddleReversed();
void     ui_backend_setPaddleReversed(bool reversed);
const char *ui_backend_getModeStr();

void ui_backend_getLiveWord(char *out, size_t outSize);
void ui_backend_getLivePattern(char *out, size_t outSize);
bool ui_backend_isReceiving();

void ui_backend_appendWord(const char *word);
void ui_backend_getTranscriptLines(char *lineA, size_t aSize, char *lineB, size_t bSize);

bool ui_backend_bleIsConnected();
bool ui_backend_bleIsSecure();
bool ui_backend_bleHasTrustedDevice();
const char *ui_backend_getDeviceName();

unsigned long ui_backend_getUptimeMs();
uint32_t      ui_backend_getFreeHeapBytes();

bool ui_backend_isTipActive();
bool ui_backend_isRingActive();

bool ui_backend_diagToneStart(uint16_t hz);
void ui_backend_diagToneStop();
bool ui_backend_isDiagToneActive();

const char *ui_backend_getChipModel();
uint8_t     ui_backend_getChipRevision();
uint32_t    ui_backend_getCpuFreqMHz();
const char *ui_backend_getSdkVersionStr();
const char *ui_backend_getResetReasonStr();
const char *ui_backend_getChipIdHex();
uint32_t    ui_backend_getFlashSizeBytes();
uint32_t    ui_backend_getSketchSizeBytes();
uint32_t    ui_backend_getFreeSketchSpaceBytes();
uint32_t    ui_backend_getMinFreeHeapBytes();
uint32_t    ui_backend_getHeapSizeBytes();
uint32_t    ui_backend_getLoopRateHz();

bool          ui_backend_settingsWasDefaulted();
unsigned long ui_backend_getLastSettingsSaveMs();
const char   *ui_backend_getSettingsNamespace();

uint16_t ui_backend_getBleMtu();

uint8_t ui_backend_getGpioMonCount();
void    ui_backend_getGpioMonEntry(uint8_t idx, const char **nameOut, uint8_t *pinOut, int *levelOut);

bool ui_backend_getModeIsPaddle();
void ui_backend_setModeIsPaddle(bool paddle);

void ui_backend_bondReset();
void ui_backend_factoryResetSettings();
void ui_backend_restartDevice();

// ----------------------------------------------------------------------------
// CW Keyer submenu additions
// ----------------------------------------------------------------------------
bool ui_backend_getDecoderEnabled();
void ui_backend_setDecoderEnabled(bool enabled);
const char *ui_backend_getKeyStateStr();   // "IDLE" / "DIT" / "DAH"

bool        ui_backend_isMemoryPlaying();
uint8_t     ui_backend_getPlayingMemorySlot();
const char *ui_backend_getMemorySlotText(uint8_t slot);
bool        ui_backend_playMemory(uint8_t slot);
void        ui_backend_stopMemory();

void        ui_backend_trainStartSession(uint8_t uiDrillId);
void        ui_backend_trainStopSession();
bool        ui_backend_isTrainingSessionActive();
void        ui_backend_trainConfirmPressed();
uint8_t     ui_backend_trainGetPhase();
const char *ui_backend_trainGetTarget();
const char *ui_backend_trainGetTyped();
uint32_t    ui_backend_trainGetCorrectCount();
uint32_t    ui_backend_trainGetTotalCount();
uint8_t     ui_backend_trainGetKochLevel();
void        ui_backend_trainGetKochCharset(char *out, size_t outSize);
int         ui_backend_trainGetAdaptiveWpm();

bool    ui_backend_trainIsExamResultReady();
uint8_t ui_backend_trainGetExamScorePercent();
uint8_t ui_backend_trainGetExamCorrectCount();
bool    ui_backend_trainGetExamPassed();
uint8_t ui_backend_trainGetExamTotalCount();
uint8_t ui_backend_trainGetExamTargetLength();
void    ui_backend_trainClearExamResult();

void ui_backend_farnsworthSetWpm(int wpm);
int  ui_backend_farnsworthGetWpm();
bool ui_backend_farnsworthTogglePlay();
bool ui_backend_isFarnsworthPlaying();

#endif // UI_BACKEND_H