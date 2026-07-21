/*
 * ============================================================================
 * MORPHEUS Standalone UI - Navigation Engine Interface
 * ============================================================================
 * File: ui_state.h | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds: the toggle editor (S_EDIT_TOGGLE, for Paddle
 * Reverse), the info screen engine (S_INFO, for BLE Status/System
 * Info/About/Help, and as the fallback destination for every menu row
 * that still has no real backend - so nothing in the menu is a dead
 * button anymore), and the BLE status/overlay bridge that display.cpp's
 * thread-safe snapshot feeds into.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#ifndef UI_STATE_H
#define UI_STATE_H
#include <Arduino.h>
#include "ui_input.h"
#include "ui_menu.h"

enum UiScreen {
  UI_SCREEN_SPLASH, UI_SCREEN_HOME, UI_SCREEN_MENU, UI_SCREEN_LIST,
  UI_SCREEN_EDIT_VALUE, UI_SCREEN_EDIT_TOGGLE, UI_SCREEN_INFO,
  UI_SCREEN_DIALOG_CONFIRM,
  UI_SCREEN_DIAG_INPUT, UI_SCREEN_DIAG_DISPLAY, UI_SCREEN_DIAG_AUDIO,
  UI_SCREEN_DIAG_GPIO, UI_SCREEN_DIAG_LIVE,
  UI_SCREEN_LIVE_MONITOR, UI_SCREEN_TUNE,
  UI_SCREEN_TRAIN_DRILL, UI_SCREEN_TRAIN_FARNSWORTH, UI_SCREEN_TRAIN_EXAM_RESULT,
  UI_SCREEN_GAME_COPY, UI_SCREEN_GAME_MEMORY, UI_SCREEN_GAME_SPEED, UI_SCREEN_GAME_PAUSE,
  UI_SCREEN_VOLUME, UI_SCREEN_CALLSIGN_EDIT, UI_SCREEN_DISPLAY_TIMEOUT,
  UI_SCREEN_CLOCK_EDIT, UI_SCREEN_DATE_FORMAT, UI_SCREEN_TIME_FORMAT
};

void ui_state_init(unsigned long now);
void ui_state_handleEvent(const UiEvent &ev, unsigned long now);
void ui_state_service(unsigned long now);
bool ui_state_consumeDirty();

UiScreen          ui_state_getScreen();
uint8_t           ui_state_getCarouselIndex();
const UiMenuNode *ui_state_getList(uint8_t &count, uint8_t &sel, uint8_t &window);
const char       *ui_state_getListTitle();
void ui_state_getRowValueText(const UiMenuNode &n, char *out, size_t outSize);
bool ui_state_isTriggerPlaying(uint8_t paramId);   // for list row "..." vs "PLAY"

unsigned long ui_state_getSplashElapsedMs();

const char *ui_state_getEditLabel();
int         ui_state_getEditValue();
void        ui_state_getEditRange(int &lo, int &hi);

const char *ui_state_getToggleLabel();
bool        ui_state_getToggleFocusValue();
void        ui_state_getToggleOptionLabels(const char **offLabel, const char **onLabel);

const char *ui_state_getInfoTitle();
const char *ui_state_getInfoLine1();
const char *ui_state_getInfoLine2();
const char *ui_state_getInfoLine3();

void ui_state_notifyDataChanged();

enum UiBleStatus {
  UI_BLE_ADV, UI_BLE_CONNECTED, UI_BLE_PAIRING,
  UI_BLE_PAIR_OK, UI_BLE_PAIR_FAIL, UI_BLE_SECURE
};
void ui_state_setBleStatus(UiBleStatus status, uint32_t passkey, unsigned long now);

enum BleOverlayKind { BLE_OVERLAY_NONE, BLE_OVERLAY_PAIRING, BLE_OVERLAY_RESULT_OK, BLE_OVERLAY_RESULT_FAIL };
bool ui_state_getBleOverlay(BleOverlayKind &kind, uint32_t &passkey);

const char *ui_state_getDialogTitle();
const char *ui_state_getDialogMessage();
bool        ui_state_getDialogFocusYes();
bool ui_state_getActionToast(char *out, size_t outSize);

uint32_t    ui_state_getDiagInputDetentTotal();
uint32_t    ui_state_getDiagInputSelectCount();
uint32_t    ui_state_getDiagInputBackCount();
const char *ui_state_getDiagInputLastEvent();
bool        ui_state_getDiagInputTipActive();
bool        ui_state_getDiagInputRingActive();
const char *ui_state_getDiagInputModeStr();

uint8_t ui_state_getDiagDisplayPage();
uint8_t ui_state_getDiagDisplayPageCount();
bool    ui_state_getDiagDisplayContrastAdjust();
uint8_t ui_state_getDiagDisplayContrastValue();
uint8_t ui_state_getDiagDisplayIconIndex();

uint8_t  ui_state_getDiagAudioPage();
uint8_t  ui_state_getDiagAudioPageCount();
uint16_t ui_state_getDiagAudioCurrentFreq();
bool     ui_state_getDiagAudioPlaying();
bool     ui_state_getDiagAudioIsManualPage();

uint8_t ui_state_getDiagGpioPage();
uint8_t ui_state_getDiagGpioPageCount();
void    ui_state_getDiagGpioEntry(uint8_t rowIndex, const char **name, uint8_t *pin, int *level);

const char *ui_state_getDiagLiveLine(uint8_t i);
uint8_t     ui_state_getDiagLivePage();
uint8_t     ui_state_getDiagLivePageCount();

uint16_t ui_state_getMenuAnimFrame();

// ----------------------------------------------------------------------------
// CW Keyer submenu additions
// ----------------------------------------------------------------------------
const char *ui_state_getLiveMonitorLine(uint8_t i);   // 4 lines, always current

bool          ui_state_getTuneActive();
uint16_t      ui_state_getTuneFreq();
unsigned long ui_state_getTuneRemainingMs();
uint8_t       ui_state_getVolumeValue();
bool          ui_state_getVolumePreviewOn();

uint8_t     ui_state_getTrainPhase();
const char *ui_state_getTrainTarget();
const char *ui_state_getTrainTyped();
uint32_t    ui_state_getTrainCorrectCount();
uint32_t    ui_state_getTrainTotalCount();
uint8_t     ui_state_getTrainDrillId();
uint8_t     ui_state_getTrainKochLevel();
void        ui_state_getTrainKochCharset(char *out, size_t outSize);
int         ui_state_getTrainAdaptiveWpm();

uint8_t ui_state_getExamScorePercent();
uint8_t ui_state_getExamCorrectCount();
bool    ui_state_getExamPassed();
uint8_t ui_state_getExamTotalCount();
uint8_t ui_state_getExamTargetLength();

int  ui_state_getFarnsworthWpm();
bool ui_state_getFarnsworthPlaying();

const char *ui_state_getCallsignEditBuffer();   // fixed-length, NOT null-terminated - use slot count, never strlen
uint8_t     ui_state_getCallsignEditCursor();
uint8_t     ui_state_getCallsignEditSlotCount();

const char *ui_state_getDisplayTimeoutLabel();

// ----------------------------------------------------------------------------
// Games
// ----------------------------------------------------------------------------
uint8_t  ui_state_gameCopyPhase();
char     ui_state_gameCopyFallingChar();
uint8_t  ui_state_gameCopyFallProgressPct();
uint16_t ui_state_gameCopyScore();
uint8_t  ui_state_gameCopyLives();
uint16_t ui_state_gameCopyHighScore();

uint8_t ui_state_gameMemoryPhase();
uint8_t ui_state_gameMemoryChainLength();
uint8_t ui_state_gameMemoryInputProgress();
uint8_t ui_state_gameMemoryHighScore();

uint8_t       ui_state_gameSpeedPhase();
uint16_t      ui_state_gameSpeedCombo();
uint8_t       ui_state_gameSpeedLives();
unsigned long ui_state_gameSpeedBeatRemainingMs();
unsigned long ui_state_gameSpeedBeatTotalMs();
bool          ui_state_gameSpeedWasLastCorrect();
char          ui_state_gameSpeedLastChar();
uint16_t      ui_state_gameSpeedHighScore();
bool          ui_state_isGamePaused();
uint8_t       ui_state_getPauseReturnScreen();
bool          ui_state_gameShowHelp();
uint8_t       ui_state_getGamePauseFocus();

int     ui_state_getClockEditFieldValue(uint8_t fieldIndex);   // 0=Year,1=Month,2=Day,3=Hour,4=Minute
uint8_t ui_state_getClockEditFieldIndex();
const char *ui_state_getDateFormatLabel();
const char *ui_state_getTimeFormatLabel();

#endif