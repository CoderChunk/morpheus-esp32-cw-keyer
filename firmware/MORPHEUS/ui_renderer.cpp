/*
 * ============================================================================
 * MORPHEUS Standalone UI - Renderer
 * ============================================================================
 * File: ui_renderer.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds the action toast overlay (post Bond-Reset/Factory-
 * Reset/Restart confirmation) and the UI_SCREEN_DIALOG_CONFIRM dispatch
 * case. BLE overlay takes priority if both happen to be active at once
 * (extremely unlikely in practice).
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "ui_renderer.h"
#include "ui_config.h"
#include "ui_layout.h"
#include "ui_state.h"
#include "ui_screens.h"

#include <Wire.h>
#include <U8g2lib.h>
#include <stdio.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static uint8_t currentContrast = UI_CONTRAST_DEFAULT;

static void drawSafeAreaBorder(U8G2 &u8g2) {
  if (!UI_DRAW_BORDER) return;
  for (uint8_t i = 0; i < UI_BORDER_WIDTH; i++) {
    u8g2.drawFrame(UI_CONTENT_X0 + i, UI_CONTENT_Y0 + i,
                    UI_CONTENT_WIDTH - 2 * i, UI_CONTENT_HEIGHT - 2 * i);
  }
}

static void drawBleOverlay(U8G2 &u8g2) {
  BleOverlayKind kind;
  uint32_t passkey;
  if (!ui_state_getBleOverlay(kind, passkey)) return;

  char buf[24];
  if (kind == BLE_OVERLAY_PAIRING) snprintf(buf, sizeof(buf), "PAIR CODE %06u", (unsigned)passkey);
  else if (kind == BLE_OVERLAY_RESULT_OK) snprintf(buf, sizeof(buf), "BLE PAIRED");
  else snprintf(buf, sizeof(buf), "BLE PAIR FAILED");

  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, OLED_WIDTH, UI_HEADER_HEIGHT + UI_MARGIN);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tr);
  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((OLED_WIDTH - w) / 2, UI_HEADER_BASELINE, buf);
  u8g2.setDrawColor(1);
}

static void drawActionToast(U8G2 &u8g2) {
  char text[20];
  if (!ui_state_getActionToast(text, sizeof(text))) return;

  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, OLED_WIDTH, UI_HEADER_HEIGHT + UI_MARGIN);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tr);
  int w = u8g2.getStrWidth(text);
  u8g2.drawStr((OLED_WIDTH - w) / 2, UI_HEADER_BASELINE, text);
  u8g2.setDrawColor(1);
}

void ui_renderer_init() {
  Wire.begin(UI_PIN_OLED_SDA, UI_PIN_OLED_SCL);
  Wire.setClock(400000);
  u8g2.begin();
  u8g2.setI2CAddress(UI_OLED_I2C_ADDR << 1);
  u8g2.setContrast(currentContrast);
}

uint8_t ui_renderer_getContrast() { return currentContrast; }

void ui_renderer_setContrast(uint8_t value) {
  currentContrast = value;
  u8g2.setContrast(currentContrast);
}

void ui_renderer_service(unsigned long now) {
  (void)now;
  if (!ui_state_consumeDirty()) return;

  u8g2.clearBuffer();

  UiScreen screen = ui_state_getScreen();
  bool skipBorder = (screen == UI_SCREEN_SPLASH) || (screen == UI_SCREEN_DIAG_DISPLAY);
  if (!skipBorder) drawSafeAreaBorder(u8g2);

  switch (screen) {
    case UI_SCREEN_SPLASH:            ui_screens_drawSplash(u8g2, ui_state_getSplashElapsedMs()); break;
    case UI_SCREEN_HOME:              ui_screens_drawHome(u8g2);            break;
    case UI_SCREEN_MENU:              ui_screens_drawMenu(u8g2);            break;
    case UI_SCREEN_LIST:              ui_screens_drawList(u8g2);            break;
    case UI_SCREEN_EDIT_VALUE:        ui_screens_drawEditValue(u8g2);       break;
    case UI_SCREEN_EDIT_TOGGLE:       ui_screens_drawEditToggle(u8g2);      break;
    case UI_SCREEN_INFO:              ui_screens_drawInfo(u8g2);            break;
    case UI_SCREEN_DIALOG_CONFIRM:    ui_screens_drawDialogConfirm(u8g2);   break;
    case UI_SCREEN_DIAG_INPUT:        ui_screens_drawDiagInput(u8g2);       break;
    case UI_SCREEN_DIAG_DISPLAY:      ui_screens_drawDiagDisplay(u8g2);     break;
    case UI_SCREEN_DIAG_AUDIO:        ui_screens_drawDiagAudio(u8g2);       break;
    case UI_SCREEN_DIAG_GPIO:         ui_screens_drawDiagGpio(u8g2);        break;
    case UI_SCREEN_DIAG_LIVE:         ui_screens_drawDiagLive(u8g2);        break;
    case UI_SCREEN_LIVE_MONITOR:      ui_screens_drawLiveMonitor(u8g2);     break;
    case UI_SCREEN_TUNE:              ui_screens_drawTune(u8g2);            break;
    case UI_SCREEN_TRAIN_DRILL:       ui_screens_drawTrainDrill(u8g2);      break;
    case UI_SCREEN_TRAIN_FARNSWORTH:  ui_screens_drawTrainFarnsworth(u8g2); break;
    case UI_SCREEN_TRAIN_EXAM_RESULT: ui_screens_drawTrainExamResult(u8g2); break;
    case UI_SCREEN_GAME_COPY:         ui_screens_drawGameCopy(u8g2);        break;
    case UI_SCREEN_GAME_MEMORY:       ui_screens_drawGameMemory(u8g2);      break;
    case UI_SCREEN_GAME_SPEED:        ui_screens_drawGameSpeed(u8g2);       break;
    case UI_SCREEN_GAME_PAUSE:        ui_screens_drawGamePause(u8g2);       break;
    case UI_SCREEN_VOLUME:            ui_screens_drawVolume(u8g2);          break;
    default: break;
  }

  if (screen != UI_SCREEN_SPLASH) {
    drawBleOverlay(u8g2);
    BleOverlayKind k; uint32_t pk;
    if (!ui_state_getBleOverlay(k, pk)) drawActionToast(u8g2);
  }

  u8g2.sendBuffer();
}