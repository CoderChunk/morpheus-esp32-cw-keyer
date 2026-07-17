/*
 * ============================================================================
 * MORPHEUS OLED UI - Animated Main Menu Icons (Interface)
 * ============================================================================
 * File: ui_icons_anim.h | Author: Coder Chunk | License: GPLv3
 *
 * Fully replaces the static XBM main-menu icon set (ui_icons.h/.cpp,
 * deleted). Frame geometry (32x32, MSB-first bit order) is confirmed,
 * not assumed - verified against the source Adafruit_GFX reference this
 * data was authored for: Adafruit_GFX::drawBitmap() and
 * u8g2::drawBitmap() use the identical MSB-first convention, so no
 * XBM/bit-swap path is needed (unlike this project's static icon set,
 * which did require that - see UI_Spec.md history for why the two
 * differ).
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#ifndef UI_ICONS_ANIM_H
#define UI_ICONS_ANIM_H
#include <Arduino.h>

struct UiAnimIcon {
  const uint8_t *frames;    // PROGMEM, contiguous [frameCount][128]
  uint8_t        frameCount;
  uint8_t        width;     // 32
  uint8_t        height;    // 32
};

// Index-aligned with UI_MAIN_MENU in ui_menu.cpp:
// CW Keyer, Training, Statistics, Connectivity, Profiles, Settings,
// Diagnostics, Tools, Games, Help
extern const UiAnimIcon UI_MAIN_MENU_ANIM_ICONS[];
extern const uint8_t    UI_MAIN_MENU_ANIM_ICON_COUNT;

const uint8_t *ui_anim_icon_getFrame(const UiAnimIcon &icon, uint16_t frameIndex);

#endif