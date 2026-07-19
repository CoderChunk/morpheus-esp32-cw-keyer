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

uint32_t ui_backend_statsSessionChars();
uint32_t ui_backend_statsSessionWords();
uint32_t ui_backend_statsSessionElements();
uint32_t ui_backend_statsSessionTrainAttempts();
uint32_t ui_backend_statsSessionTrainCorrect();
unsigned long ui_backend_statsSessionUptimeMs();

uint32_t ui_backend_statsLifetimeChars();
uint32_t ui_backend_statsLifetimeWords();
uint32_t ui_backend_statsLifetimeElements();
uint32_t ui_backend_statsLifetimeUptimeSec();
uint32_t ui_backend_statsLifetimeTrainAttempts();
uint32_t ui_backend_statsLifetimeTrainCorrect();
uint32_t ui_backend_statsModeAttempts(uint8_t mode);
uint32_t ui_backend_statsModeCorrect(uint8_t mode);
uint32_t ui_backend_statsExamsTaken();
uint32_t ui_backend_statsExamsPassed();
uint8_t  ui_backend_statsBestExamScore();
int      ui_backend_statsPeakAdaptiveWpm();

uint8_t  ui_backend_statsHistoryCount();
uint16_t ui_backend_statsHistoryEntry(uint8_t indexFromNewest);

bool    ui_backend_isGameSessionActive();
void    ui_backend_gameStart(uint8_t uiGameId);
void    ui_backend_gameStop();
void    ui_backend_gameConfirmPressed();

uint8_t  ui_backend_gameCopyPhase();
char     ui_backend_gameCopyFallingChar();
uint8_t  ui_backend_gameCopyFallProgressPct();
uint16_t ui_backend_gameCopyScore();
uint8_t  ui_backend_gameCopyLives();
uint16_t ui_backend_gameCopyHighScore();

uint8_t  ui_backend_gameMemoryPhase();
uint8_t  ui_backend_gameMemoryChainLength();
uint8_t  ui_backend_gameMemoryInputProgress();
uint8_t  ui_backend_gameMemoryHighScore();

uint8_t       ui_backend_gameSpeedPhase();
uint16_t      ui_backend_gameSpeedCombo();
uint8_t       ui_backend_gameSpeedLives();
unsigned long ui_backend_gameSpeedBeatRemainingMs();
unsigned long ui_backend_gameSpeedBeatTotalMs();
bool          ui_backend_gameSpeedWasLastCorrect();
char          ui_backend_gameSpeedLastChar();
uint16_t      ui_backend_gameSpeedHighScore();
bool 		  ui_backend_isGamePaused();
void 		  ui_backend_gameTogglePause();
void 		  ui_backend_gameRestart();
bool 		  ui_backend_isGamePausedFor(uint8_t uiGameId);   // true if that specific game is the active+paused one

uint8_t ui_backend_getVolume();
void    ui_backend_setVolume(uint8_t percent);
bool    ui_backend_getSidetoneEnabled();
void    ui_backend_setSidetoneEnabled(bool enabled);

void ui_backend_profileLoad(uint8_t uiProfileId);
void ui_backend_profileSave(uint8_t uiProfileId);

int         ui_backend_profileGetWpm(uint8_t uiProfileId);
uint16_t    ui_backend_profileGetToneHz(uint8_t uiProfileId);
bool        ui_backend_profileGetPaddleReversed(uint8_t uiProfileId);
const char *ui_backend_profileGetModeStr(uint8_t uiProfileId);
uint8_t     ui_backend_profileGetVolume(uint8_t uiProfileId);
bool        ui_backend_profileGetSidetoneEnabled(uint8_t uiProfileId);
uint8_t 	ui_backend_profileGetContrast(uint8_t uiProfileId);

#endif // UI_BACKEND_H