/*
 * ============================================================================
 * MORPHEUS OLED UI - Screen Layouts
 * ============================================================================
 * File: ui_screens.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision: drawHome's "Decoder:" line is now real (was hardcoded
 * "ON"). drawList's tag logic gains NODE_MONITOR/NODE_TUNE (both show
 * ">") and NODE_TRIGGER (shows "PLAY" or "..." if that slot is currently
 * playing). New drawLiveMonitor and drawTune screens.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "ui_screens.h"
#include "ui_config.h"
#include "ui_layout.h"
#include "ui_state.h"
#include "ui_menu.h"
#include "ui_mockdata.h"
#include "ui_fonts.h"
#include "ui_icons_anim.h"
#include "ui_splash_logo.h"
#include <stdio.h>
#include <string.h>

static void drawBarRule(U8G2 &u8g2) {
  u8g2.drawHLine(UI_CONTENT_X0, UI_HEADER_RULE_Y, UI_CONTENT_WIDTH);
}

static void drawCenteredBarTitle(U8G2 &u8g2, const char *text) {
  u8g2.setFont(UI_FONT_SMALL);
  int w = u8g2.getStrWidth(text);
  int x = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - w) / 2;
  u8g2.drawStr(x, UI_HEADER_BASELINE, text);
  drawBarRule(u8g2);
}

// A small filled dot in the content area's top-right corner, signaling
// "this screen is currently live" (tone playing, Tune on, a value being
// actively adjusted) without needing instructional text underneath it.
static void drawActiveDot(U8G2 &u8g2) {
  u8g2.drawDisc(UI_CONTENT_X1 - 6, UI_HEADER_RULE_Y + 6, 3);
}

// Vertical position scrollbar for multi-page screens
static void drawPageScrollbar(U8G2 &u8g2, uint8_t page, uint8_t pageCount) {
  if (pageCount <= 1) return;
  const uint8_t trackY = (uint8_t)(UI_HEADER_RULE_Y + 2);
  const uint8_t trackBottom = (uint8_t)(UI_CONTENT_Y1 - 2);
  const uint8_t trackH = (uint8_t)(trackBottom - trackY);
  uint8_t thumbH = (uint8_t)(trackH / pageCount);
  if (thumbH < 6) thumbH = 6;
  uint8_t maxTop = (uint8_t)((trackH > thumbH) ? (trackH - thumbH) : 0);
  uint8_t thumbY = (uint8_t)(trackY + ((uint32_t)maxTop * page) / (pageCount - 1));
  u8g2.drawBox(UI_LIST_SCROLLBAR_X, thumbY, 2, thumbH);
}

static float phaseProgress(unsigned long elapsed, unsigned long start, unsigned long end) {
  if (elapsed <= start) return 0.0f;
  if (elapsed >= end)   return 1.0f;
  return (float)(elapsed - start) / (float)(end - start);
}

void ui_screens_drawSplash(U8G2 &u8g2, unsigned long elapsed) {
  if (elapsed < UI_BOOT_BORDER_MS) {
    float borderP = phaseProgress(elapsed, 0, UI_BOOT_BORDER_MS);
    int halfW = (int)(((int)UI_CONTENT_WIDTH / 2) * borderP);
    int cx = UI_CONTENT_X0 + UI_CONTENT_WIDTH / 2;
    u8g2.drawHLine(cx - halfW, UI_CONTENT_Y0, halfW * 2);
    u8g2.drawHLine(cx - halfW, UI_CONTENT_Y1 - 1, halfW * 2);
    return;
  }
  float logoP = phaseProgress(elapsed, UI_BOOT_BORDER_MS, UI_BOOT_LOGO_MS);
  int revealW = (int)(OLED_WIDTH * logoP);
  if (revealW > 0) {
    u8g2.setClipWindow(0, 0, revealW, OLED_HEIGHT);
    u8g2.drawXBMP(0, 0, ui_logo_morpheus.width, ui_logo_morpheus.height, ui_logo_morpheus.bitmap);
    u8g2.setMaxClipWindow();
  }
  if (elapsed >= UI_BOOT_LOGO_MS) {
    float loadP = phaseProgress(elapsed, UI_BOOT_LOADER_START_MS, UI_BOOT_LOADER_END_MS);
    const int barX = UI_CONTENT_X0 + 24, barY = 59;
    const int barW = (int)UI_CONTENT_WIDTH - 48, barH = 3;
    u8g2.drawFrame(barX, barY, barW, barH);
    int fillW = (int)(barW * loadP) - 2;
    if (fillW > 0) u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);
  }
  if (elapsed >= UI_BOOT_VERSION_MS) {
    u8g2.setFont(UI_FONT_SMALL);
    const char *ver = "v1.2.2";
    int w = u8g2.getStrWidth(ver);
    u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - w) / 2, 7, ver);
  }
}

void ui_screens_drawHome(U8G2 &u8g2) {
  char line[24];
  u8g2.setFont(UI_FONT_SMALL);
  snprintf(line, sizeof(line), "%d WPM %s %s",
           uiStatus.wpm,
           (uiStatus.mode == UI_MODE_STRAIGHT) ? "STR" : "PAD",
           uiStatus.bleStatus);
  u8g2.drawStr(UI_CONTENT_X0, UI_HEADER_BASELINE, line);
  drawBarRule(u8g2);

  const int infoY = UI_HEADER_RULE_Y + 9;
  u8g2.setFont(UI_FONT_SMALL);
  if (uiStatus.callsignEnabled && uiStatus.callsign[0] != '\0') {
    u8g2.drawStr(UI_CONTENT_X0, infoY, uiStatus.callsign);
  }
  int timeW = u8g2.getStrWidth(uiStatus.time);
  u8g2.drawStr(UI_CONTENT_X1 - timeW, infoY, uiStatus.time);

  u8g2.setFont(UI_FONT_BOLD);
  const int lineH  = 14;
  const int lineAY = infoY + lineH;
  const int lineBY = lineAY + lineH;
  u8g2.drawStr(UI_CONTENT_X0, lineAY, uiStatus.transcriptA);
  u8g2.drawStr(UI_CONTENT_X0, lineBY, uiStatus.transcriptB);

  const int footerRuleY = lineBY + 2;
  u8g2.drawHLine(UI_CONTENT_X0, footerRuleY, UI_CONTENT_WIDTH);
  u8g2.setFont(UI_FONT_SMALL);
  const int footerY = footerRuleY + 8;
  snprintf(line, sizeof(line), "Decoder: %s", uiStatus.decoderEnabled ? "ON" : "OFF");
  u8g2.drawStr(UI_CONTENT_X0, footerY, line);
  if (uiStatus.isReceiving) u8g2.drawStr(UI_CONTENT_X1 - 14, footerY, "RX");
}

void ui_screens_drawMenu(U8G2 &u8g2) {
  uint8_t idx = ui_state_getCarouselIndex();
  const UiMainItem &m = UI_MAIN_MENU[idx];
  char hdr[24];
  // snprintf(hdr, sizeof(hdr), "MENU %u/%u", (unsigned)(idx + 1), (unsigned)UI_MAIN_MENU_COUNT);
  // drawCenteredBarTitle(u8g2, hdr);
  // Zero-pad the numerator to match the denominator's digit width, so
  // "9/10" and "10/10" render at identical character count/width -
  // avoids the visual jitter a variable-width "N/10" caused.
  char totalBuf[4];
  snprintf(totalBuf, sizeof(totalBuf), "%u", (unsigned)UI_MAIN_MENU_COUNT);
  uint8_t totalDigits = (uint8_t)strlen(totalBuf);
  snprintf(hdr, sizeof(hdr), "[   MENU %0*u/%u   ]", totalDigits,
           (unsigned)(idx + 1), (unsigned)UI_MAIN_MENU_COUNT);
  u8g2.setFont(UI_FONT_SMALL);
  int hdrW = u8g2.getStrWidth(hdr);
  int hdrX = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hdrW) / 2;
  u8g2.drawStr(hdrX, UI_HEADER_BASELINE, hdr);

  const UiAnimIcon &anim = *m.icon;
  const uint8_t *frame = ui_anim_icon_getFrame(anim, ui_state_getMenuAnimFrame());

  int iconX = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - (int)anim.width) / 2;
  int diff  = (int)UI_ICON_AREA_HEIGHT - (int)anim.height;
  int iconY = UI_ICON_AREA_Y + diff / 2;

  // u8g2's drawBitmap() takes byte-width (not pixel-width) and reads
  // MSB-first - the same convention as the Adafruit_GFX code this
  // animation data was authored for. anim.width is byte-aligned
  // (32/8=4 exactly), so no rounding is needed.
  u8g2.drawBitmap(iconX, iconY, anim.width / 8, anim.height, frame);

  u8g2.setFont(UI_FONT_BOLD);
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - u8g2.getStrWidth(m.title)) / 2,
               UI_TITLE_Y, m.title);

  const int chevY = UI_ICON_AREA_Y + UI_ICON_AREA_HEIGHT / 2;
  if (idx > 0) {
    u8g2.drawTriangle(UI_CONTENT_X0 + UI_CHEVRON_INSET, chevY - 6,
                       UI_CONTENT_X0 + UI_CHEVRON_INSET, chevY + 6,
                       UI_CONTENT_X0 + UI_CHEVRON_INSET - 6, chevY);
  }
  if (idx < UI_MAIN_MENU_COUNT - 1) {
    u8g2.drawTriangle(UI_CONTENT_X1 - UI_CHEVRON_INSET, chevY - 6,
                       UI_CONTENT_X1 - UI_CHEVRON_INSET, chevY + 6,
                       UI_CONTENT_X1 - UI_CHEVRON_INSET + 6, chevY);
  }
}

void ui_screens_drawList(U8G2 &u8g2) {
  uint8_t count, sel, window;
  const UiMenuNode *list = ui_state_getList(count, sel, window);
  if (list == nullptr) return;

  drawCenteredBarTitle(u8g2, ui_state_getListTitle());

  u8g2.setFont(UI_FONT_BOLD);
  for (uint8_t row = 0; row < UI_LIST_VISIBLE_ROWS; row++) {
    uint8_t item = (uint8_t)(window + row);
    if (item >= count) break;

    const UiMenuNode &n = list[item];
    uint8_t y = (uint8_t)(UI_LIST_ROW_Y0 + row * UI_LIST_ROW_HEIGHT);
    uint8_t baseline = (uint8_t)(y + 12);
    bool selected = (item == sel);

    if (selected) {
      u8g2.drawBox(UI_CONTENT_X0, y, UI_LIST_SCROLLBAR_X - UI_CONTENT_X0, UI_LIST_ROW_HEIGHT);
      u8g2.setDrawColor(0);
    }

    u8g2.drawStr(UI_CONTENT_X0 + 3, baseline, n.label);

    char valueBuf[8] = { 0 };
    ui_state_getRowValueText(n, valueBuf, sizeof(valueBuf));
    const char *tag = nullptr;

    if (n.type == NODE_SUBMENU || n.type == NODE_DIAG ||
        n.type == NODE_MONITOR || n.type == NODE_TUNE) {
      tag = ">";
    } else if (n.type == NODE_ACTION && n.paramId != ACTION_NONE) {
      tag = ">";
    } else if (n.type == NODE_TRIGGER && n.paramId != 0) {
      tag = ui_state_isTriggerPlaying(n.paramId) ? "..." : "PLAY";
    } else if (valueBuf[0] != '\0') {
      tag = valueBuf;
    } else if (n.type == NODE_STUB || n.type == NODE_ACTION || n.type == NODE_INFO) {
      tag = "--";
    }

    if (tag != nullptr && tag[0] != '\0') {
      u8g2.drawStr(UI_LIST_TEXT_RIGHT - u8g2.getStrWidth(tag), baseline, tag);
    }

    if (selected) u8g2.setDrawColor(1);
  }

  if (count > UI_LIST_VISIBLE_ROWS) {
    const uint8_t trackY = UI_LIST_ROW_Y0;
    const uint8_t trackH = (uint8_t)(UI_LIST_VISIBLE_ROWS * UI_LIST_ROW_HEIGHT);
    uint8_t thumbH = (uint8_t)((trackH * UI_LIST_VISIBLE_ROWS) / count);
    if (thumbH < 6) thumbH = 6;
    uint8_t maxTop = (uint8_t)(count - UI_LIST_VISIBLE_ROWS);
    uint8_t thumbY = (uint8_t)(trackY + ((trackH - thumbH) * window) / maxTop);
    u8g2.drawBox(UI_LIST_SCROLLBAR_X, thumbY, 2, thumbH);
  }
}

void ui_screens_drawEditValue(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getEditLabel());
  int value = ui_state_getEditValue();
  int lo, hi;
  ui_state_getEditRange(lo, hi);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", value);
  u8g2.setFont(UI_FONT_HERO);
  int w = u8g2.getStrWidth(buf);
  const int valueBaseline = UI_HEADER_RULE_Y + 32;
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - w) / 2, valueBaseline, buf);

  const int chevY = valueBaseline - 9;
  if (value > lo) {
    u8g2.drawTriangle(UI_CONTENT_X0 + UI_CHEVRON_INSET, chevY - 6,
                       UI_CONTENT_X0 + UI_CHEVRON_INSET, chevY + 6,
                       UI_CONTENT_X0 + UI_CHEVRON_INSET - 6, chevY);
  }
  if (value < hi) {
    u8g2.drawTriangle(UI_CONTENT_X1 - UI_CHEVRON_INSET, chevY - 6,
                       UI_CONTENT_X1 - UI_CHEVRON_INSET, chevY + 6,
                       UI_CONTENT_X1 - UI_CHEVRON_INSET + 6, chevY);
  }
}

void ui_screens_drawEditToggle(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getToggleLabel());
  const char *offLabel, *onLabel;
  ui_state_getToggleOptionLabels(&offLabel, &onLabel);   // full wording, per-screen
  bool focus = ui_state_getToggleFocusValue();

  u8g2.setFont(UI_FONT_BOLD);
  int offW = u8g2.getStrWidth(offLabel);
  int onW  = u8g2.getStrWidth(onLabel);

  const int pillW = 44, pillH = 20, gap = 8;
  const int minClearance = 6;   // padding a label needs inside a compact pill
  bool fitsSideBySide = (offW + minClearance <= pillW) && (onW + minClearance <= pillW);

  if (fitsSideBySide) {
    const int pillY = UI_HEADER_RULE_Y + 18;
    int totalW = pillW * 2 + gap;
    int startX = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - totalW) / 2;

    if (!focus) { u8g2.drawBox(startX, pillY, pillW, pillH); u8g2.setDrawColor(0); }
    else          u8g2.drawFrame(startX, pillY, pillW, pillH);
    u8g2.drawStr(startX + (pillW - offW) / 2, pillY + 14, offLabel);
    if (!focus) u8g2.setDrawColor(1);

    int onX = startX + pillW + gap;
    if (focus) { u8g2.drawBox(onX, pillY, pillW, pillH); u8g2.setDrawColor(0); }
    else         u8g2.drawFrame(onX, pillY, pillW, pillH);
    u8g2.drawStr(onX + (pillW - onW) / 2, pillY + 14, onLabel);
    if (focus) u8g2.setDrawColor(1);
  } else {
    const int rowW = (int)UI_CONTENT_WIDTH - 8;
    const int rowH = 18;
    const int rowGap = 6;
    const int rowX = UI_CONTENT_X0 + 4;
    const int row1Y = UI_HEADER_RULE_Y + 8;
    const int row2Y = row1Y + rowH + rowGap;

    if (!focus) { u8g2.drawBox(rowX, row1Y, rowW, rowH); u8g2.setDrawColor(0); }
    else          u8g2.drawFrame(rowX, row1Y, rowW, rowH);
    u8g2.drawStr(rowX + (rowW - offW) / 2, row1Y + 14, offLabel);
    if (!focus) u8g2.setDrawColor(1);

    if (focus) { u8g2.drawBox(rowX, row2Y, rowW, rowH); u8g2.setDrawColor(0); }
    else         u8g2.drawFrame(rowX, row2Y, rowW, rowH);
    u8g2.drawStr(rowX + (rowW - onW) / 2, row2Y + 14, onLabel);
    if (focus) u8g2.setDrawColor(1);
  }
}

void ui_screens_drawInfo(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getInfoTitle());
  u8g2.setFont(UI_FONT_SMALL);
  const int lineH = 12;
  int y = UI_HEADER_RULE_Y + 12;
  u8g2.drawStr(UI_CONTENT_X0, y, ui_state_getInfoLine1());
  y += lineH;
  u8g2.drawStr(UI_CONTENT_X0, y, ui_state_getInfoLine2());
  y += lineH;
  u8g2.drawStr(UI_CONTENT_X0, y, ui_state_getInfoLine3());
}

static void wrapTwoLines(U8G2 &u8g2, const char *msg, int maxWidth,
                          char *line1, size_t line1Size,
                          char *line2, size_t line2Size) {
  int fullW = u8g2.getStrWidth(msg);
  if (fullW <= maxWidth) {
    strncpy(line1, msg, line1Size - 1);
    line1[line1Size - 1] = '\0';
    line2[0] = '\0';
    return;
  }

  size_t len = strlen(msg);
  size_t bestBreak = 0;
  for (size_t i = 0; i < len; i++) {
    if (msg[i] != ' ') continue;
    char probe[40];
    size_t probeLen = (i < sizeof(probe) - 1) ? i : sizeof(probe) - 1;
    strncpy(probe, msg, probeLen);
    probe[probeLen] = '\0';
    if (u8g2.getStrWidth(probe) <= maxWidth) {
      bestBreak = i;
    } else {
      break;
    }
  }

  if (bestBreak == 0) {
    strncpy(line1, msg, line1Size - 1);
    line1[line1Size - 1] = '\0';
    line2[0] = '\0';
    return;
  }

  size_t l1Len = (bestBreak < line1Size - 1) ? bestBreak : line1Size - 1;
  strncpy(line1, msg, l1Len);
  line1[l1Len] = '\0';

  const char *rest = msg + bestBreak + 1;   // skip the space
  strncpy(line2, rest, line2Size - 1);
  line2[line2Size - 1] = '\0';
}

void ui_screens_drawDialogConfirm(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getDialogTitle());

  u8g2.setFont(UI_FONT_BOLD);
  char line1[24], line2[24];
  wrapTwoLines(u8g2, ui_state_getDialogMessage(), (int)UI_CONTENT_WIDTH,
               line1, sizeof(line1), line2, sizeof(line2));

  int msgY1 = UI_HEADER_RULE_Y + 12;
  int w1 = u8g2.getStrWidth(line1);
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - w1) / 2, msgY1, line1);

  int pillY = msgY1 + 8;   // default: right under a single-line message
  if (line2[0] != '\0') {
    int msgY2 = msgY1 + 13;
    int w2 = u8g2.getStrWidth(line2);
    u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - w2) / 2, msgY2, line2);
    pillY = msgY2 + 8;   // push the pills down to clear the second line
  }

  bool focusYes = ui_state_getDialogFocusYes();
  const int pillW = 44, pillH = 18, gap = 8;
  int totalW = pillW * 2 + gap;
  int startX = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - totalW) / 2;

  if (!focusYes) { u8g2.drawBox(startX, pillY, pillW, pillH); u8g2.setDrawColor(0); }
  else            u8g2.drawFrame(startX, pillY, pillW, pillH);
  int noW = u8g2.getStrWidth("NO");
  u8g2.drawStr(startX + (pillW - noW) / 2, pillY + 13, "NO");
  if (!focusYes) u8g2.setDrawColor(1);

  int yesX = startX + pillW + gap;
  if (focusYes) { u8g2.drawBox(yesX, pillY, pillW, pillH); u8g2.setDrawColor(0); }
  else           u8g2.drawFrame(yesX, pillY, pillW, pillH);
  int yesW = u8g2.getStrWidth("YES");
  u8g2.drawStr(yesX + (pillW - yesW) / 2, pillY + 13, "YES");
  if (focusYes) u8g2.setDrawColor(1);

  // u8g2.setFont(UI_FONT_SMALL);
  // u8g2.drawStr(UI_CONTENT_X0, UI_CONTENT_Y1 - 2, "Back");
  // const char *confirmLbl = "Confirm";
  // int cw = u8g2.getStrWidth(confirmLbl);
  // u8g2.drawStr(UI_CONTENT_X1 - cw, UI_CONTENT_Y1 - 2, confirmLbl);
}

void ui_screens_drawDiagInput(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getInfoTitle());
  char line[24];
  u8g2.setFont(UI_FONT_SMALL);
  int y = UI_HEADER_RULE_Y + 11;

  snprintf(line, sizeof(line), "ENC DETENTS: %ld", (long)ui_state_getDiagInputDetentTotal());
  u8g2.drawStr(UI_CONTENT_X0, y, line); y += 11;

  snprintf(line, sizeof(line), "SELECT:%lu  BACK:%lu",
           (unsigned long)ui_state_getDiagInputSelectCount(),
           (unsigned long)ui_state_getDiagInputBackCount());
  u8g2.drawStr(UI_CONTENT_X0, y, line); y += 11;

  snprintf(line, sizeof(line), "KEY:%s  DAH:%s",
           ui_state_getDiagInputTipActive() ? "ACT" : "idle",
           ui_state_getDiagInputRingActive() ? "ACT" : "idle");
  u8g2.drawStr(UI_CONTENT_X0, y, line); y += 11;

  snprintf(line, sizeof(line), "MODE:%s  LAST:%s",
           ui_state_getDiagInputModeStr(), ui_state_getDiagInputLastEvent());
  u8g2.drawStr(UI_CONTENT_X0, y, line);

  // const char *hint = "HOLD BACK TO EXIT";
  // int hw = u8g2.getStrWidth(hint);
  // u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hw) / 2, UI_CONTENT_Y1 - 2, hint);
}

void ui_screens_drawDiagDisplay(U8G2 &u8g2) {
  uint8_t page = ui_state_getDiagDisplayPage();
  uint8_t pageCount = ui_state_getDiagDisplayPageCount();
  char hdr[20];
  snprintf(hdr, sizeof(hdr), "DISPLAY TEST %u/%u", (unsigned)(page + 1), (unsigned)pageCount);

  switch (page) {
    case 0: u8g2.drawBox(0, 0, OLED_WIDTH, OLED_HEIGHT); return;
    case 1: {
      for (int y = 0; y < OLED_HEIGHT; y += 8)
        for (int x = ((y / 8) % 2) * 8; x < OLED_WIDTH; x += 16)
          u8g2.drawBox(x, y, 8, 8);
      return;
    }
    case 2: {
      for (int i = 0; i < OLED_HEIGHT / 2; i += 4)
        u8g2.drawFrame(i, i, OLED_WIDTH - 2 * i, OLED_HEIGHT - 2 * i);
      return;
    }
    case 3: {
      drawCenteredBarTitle(u8g2, hdr);
      u8g2.setFont(UI_FONT_SMALL);
      u8g2.drawStr(UI_CONTENT_X0, UI_HEADER_RULE_Y + 10, "6x10 ABCxyz 0123");
      u8g2.setFont(UI_FONT_BOLD);
      u8g2.drawStr(UI_CONTENT_X0, UI_HEADER_RULE_Y + 26, "7x14B ABCxyz 012");
      u8g2.setFont(UI_FONT_HERO);
      u8g2.drawStr(UI_CONTENT_X0, UI_HEADER_RULE_Y + 55, "8890");
      return;
    }
    case 4: {
      drawCenteredBarTitle(u8g2, hdr);
      uint8_t idx = ui_state_getDiagDisplayIconIndex();
      const UiMainItem &m = UI_MAIN_MENU[idx];
      const UiAnimIcon &anim = *m.icon;
      const uint8_t *frame0 = ui_anim_icon_getFrame(anim, 0);
      int iconX = UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - (int)anim.width) / 2;
      u8g2.drawBitmap(iconX, UI_HEADER_RULE_Y + 4, anim.width / 8, anim.height, frame0);
      char cap[20];
      snprintf(cap, sizeof(cap), "%u/%u %s", (unsigned)(idx + 1), (unsigned)UI_MAIN_MENU_COUNT, m.title);
      u8g2.setFont(UI_FONT_SMALL);
      int cw = u8g2.getStrWidth(cap);
      u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - cw) / 2, UI_CONTENT_Y1 - 2, cap);
      return;
    }
    case 5: {
      u8g2.drawBox(UI_CONTENT_X0, UI_CONTENT_Y0, UI_CONTENT_WIDTH, UI_CONTENT_HEIGHT);
      u8g2.setDrawColor(0);
      u8g2.setFont(UI_FONT_SMALL);
      u8g2.drawStr(UI_CONTENT_X0 + 4, UI_CONTENT_Y0 + 10, "INVERT TEST 6/7");
      u8g2.setFont(UI_FONT_BOLD);
      u8g2.drawStr(UI_CONTENT_X0 + 4, UI_CONTENT_Y0 + 30, "Sample Text");
      u8g2.setFont(UI_FONT_SMALL);
      u8g2.drawStr(UI_CONTENT_X0 + 4, UI_CONTENT_Y1 - 2, "0123456789");
      u8g2.setDrawColor(1);
      return;
    }
    case 6: {
      drawCenteredBarTitle(u8g2, hdr);
      char val[8];
      snprintf(val, sizeof(val), "%u", (unsigned)ui_state_getDiagDisplayContrastValue());
      u8g2.setFont(UI_FONT_HERO);
      int vw = u8g2.getStrWidth(val);
      u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - vw) / 2, UI_HEADER_RULE_Y + 30, val);
      // u8g2.setFont(UI_FONT_SMALL);
      // const char *hint = ui_state_getDiagDisplayContrastAdjust() ? "ROTATE=VALUE  CONFIRM=DONE" : "CONFIRM=ADJUST";
      // int hw = u8g2.getStrWidth(hint);
      // u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hw) / 2, UI_CONTENT_Y1 - 2, hint);
      if (ui_state_getDiagDisplayContrastAdjust()) drawActiveDot(u8g2);
      return;
    }
    default: return;
  }
}

void ui_screens_drawDiagAudio(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getInfoTitle());
  char val[10];
  snprintf(val, sizeof(val), "%u", (unsigned)ui_state_getDiagAudioCurrentFreq());
  u8g2.setFont(UI_FONT_HERO);
  int vw = u8g2.getStrWidth(val);
  const int valueBaseline = UI_HEADER_RULE_Y + 28;
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - vw) / 2, valueBaseline, val);

  u8g2.setFont(UI_FONT_SMALL);
  const char *hzLbl = "Hz";
  int hzW = u8g2.getStrWidth(hzLbl);
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hzW) / 2, valueBaseline + 12, hzLbl);

  if (ui_state_getDiagAudioIsManualPage()) {
    const char *manual = "MANUAL";
    int mw = u8g2.getStrWidth(manual);
    u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - mw) / 2, UI_CONTENT_Y1 - 2, manual);
  }

  if (ui_state_getDiagAudioPlaying()) drawActiveDot(u8g2);

  drawPageScrollbar(u8g2, ui_state_getDiagAudioPage(), ui_state_getDiagAudioPageCount());
}

void ui_screens_drawDiagGpio(U8G2 &u8g2) {
  char hdr[20];
  snprintf(hdr, sizeof(hdr), "GPIO %u/%u", (unsigned)(ui_state_getDiagGpioPage() + 1),
           (unsigned)ui_state_getDiagGpioPageCount());
  drawCenteredBarTitle(u8g2, hdr);

  u8g2.setFont(UI_FONT_SMALL);
  int y = UI_HEADER_RULE_Y + 11;
  for (uint8_t row = 0; row < 4; row++) {
    const char *name; uint8_t pin; int level;
    ui_state_getDiagGpioEntry(row, &name, &pin, &level);
    if (name[0] == '\0') break;
    char line[24];
    snprintf(line, sizeof(line), "%-8s P%-2u %s", name, (unsigned)pin, level == HIGH ? "HIGH" : "LOW");
    u8g2.drawStr(UI_CONTENT_X0, y, line);
    y += 12;
  }
  drawPageScrollbar(u8g2, ui_state_getDiagGpioPage(), ui_state_getDiagGpioPageCount());
}

void ui_screens_drawDiagLive(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, ui_state_getInfoTitle());
  u8g2.setFont(UI_FONT_SMALL);
  int y = UI_HEADER_RULE_Y + 11;
  bool paginated = ui_state_getDiagLivePageCount() > 1;
  uint8_t maxLines = paginated ? 3 : 4;
  for (uint8_t i = 0; i < maxLines; i++) {
    const char *line = ui_state_getDiagLiveLine(i);
    if (line[0] == '\0') continue;
    u8g2.drawStr(UI_CONTENT_X0, y, line);
    y += 12;
  }
  // if (paginated) {
  //   const char *hint = "ROTATE=PAGE";
  //   int hw = u8g2.getStrWidth(hint);
  //   u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hw) / 2, UI_CONTENT_Y1 - 2, hint);
  // }
  drawPageScrollbar(u8g2, ui_state_getDiagLivePage(), ui_state_getDiagLivePageCount());
}

// ----------------------------------------------------------------------------
// Live Monitor - real-time keyer/decoder state, auto-refreshing
// ----------------------------------------------------------------------------
void ui_screens_drawLiveMonitor(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, "LIVE MONITOR");
  u8g2.setFont(UI_FONT_SMALL);
  int y = UI_HEADER_RULE_Y + 11;
  for (uint8_t i = 0; i < 4; i++) {
    const char *line = ui_state_getLiveMonitorLine(i);
    if (line[0] == '\0') continue;
    u8g2.drawStr(UI_CONTENT_X0, y, line);
    y += 12;
  }
}

// ----------------------------------------------------------------------------
// Tune - continuous sidetone at the configured Tone frequency, 30s
// safety auto-timeout, reuses the diagnostic tone gate.
// ----------------------------------------------------------------------------
void ui_screens_drawTune(U8G2 &u8g2) {
  drawCenteredBarTitle(u8g2, "TUNE");

  bool active = ui_state_getTuneActive();
  const char *status = active ? "ON" : "OFF";
  u8g2.setFont(UI_FONT_HERO);
  int sw = u8g2.getStrWidth(status);
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - sw) / 2, UI_HEADER_RULE_Y + 30, status);

  char line[24];
  u8g2.setFont(UI_FONT_SMALL);
  snprintf(line, sizeof(line), "%u Hz", (unsigned)ui_state_getTuneFreq());
  int lw = u8g2.getStrWidth(line);
  u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - lw) / 2, UI_HEADER_RULE_Y + 42, line);

  if (active) {
    unsigned long remainSec = ui_state_getTuneRemainingMs() / 1000;
    snprintf(line, sizeof(line), "AUTO-OFF IN %lus", remainSec);   // real countdown, not an instruction - kept
    int rw = u8g2.getStrWidth(line);
    u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - rw) / 2, UI_CONTENT_Y1 - 2, line);
    drawActiveDot(u8g2);
  }

  // const char *hint = "CONFIRM=ON/OFF";
  // int hw = u8g2.getStrWidth(hint);
  // u8g2.drawStr(UI_CONTENT_X0 + ((int)UI_CONTENT_WIDTH - hw) / 2, UI_CONTENT_Y1 - 2, hint);
}