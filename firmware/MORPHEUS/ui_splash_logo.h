/*
 * ============================================================================
 * MORPHEUS OLED UI - Boot Splash Logo (Interface)
 * ============================================================================
 * File: ui_splash_logo.h | Author: Coder Chunk | License: GPLv3
 *
 * UiIcon is now defined locally here rather than pulled from ui_icons.h,
 * which was deleted when the static main-menu icon set was replaced by
 * the animated set (ui_icons_anim.h). This 128x64 boot logo is unrelated
 * to the menu icons and stays a single static bitmap.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#ifndef UI_SPLASH_LOGO_H
#define UI_SPLASH_LOGO_H
#include <Arduino.h>

struct UiIcon {
  const uint8_t *bitmap;
  uint8_t        width;
  uint8_t        height;
};

extern const UiIcon ui_logo_morpheus;

#endif