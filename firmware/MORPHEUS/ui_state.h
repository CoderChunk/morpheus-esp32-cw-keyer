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
  UI_SCREEN_LIVE_MONITOR, UI_SCREEN_TUNE
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

#endif