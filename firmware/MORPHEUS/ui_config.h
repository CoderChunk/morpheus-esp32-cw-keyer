/*
 * ============================================================================
 * MORPHEUS OLED UI - Configuration
 * ============================================================================
 *
 * File: ui_config.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * All pins and tunable parameters for the OLED UI. This is the single
 * accumulated version of this file across all delivery passes - pin map,
 * encoder/input timing, boot animation, integration-pass additions
 * (transcript sizing, contrast range, BLE overlay), and Diagnostics-pass
 * additions (refresh cadences) all live here together.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef UI_CONFIG_H
#define UI_CONFIG_H

#include <Arduino.h>

#define UI_PIN_ENC_A      19
#define UI_PIN_ENC_B      23
#define UI_PIN_ENC_PUSH    4
#define UI_PIN_BACK       13
#define UI_PIN_CONFIRM    14

#define UI_PIN_OLED_SDA   21
#define UI_PIN_OLED_SCL   22
#define UI_OLED_I2C_ADDR  0x3C

static const int8_t UI_ENC_COUNTS_PER_DETENT = 4;
static const bool   UI_ENC_REVERSED          = false;

static const unsigned long UI_BUTTON_DEBOUNCE_MS = 30;
static const unsigned long UI_LONG_PRESS_MS      = 1000;

static const unsigned long UI_SPLASH_MS            = 3000;
static const unsigned long UI_SPLASH_FRAME_MS      = 50;
static const unsigned long UI_BOOT_BORDER_MS       = 250;
static const unsigned long UI_BOOT_LOGO_MS         = 900;
static const unsigned long UI_BOOT_LOADER_START_MS = 900;
static const unsigned long UI_BOOT_LOADER_END_MS   = 1550;
static const unsigned long UI_BOOT_VERSION_MS      = 1600;
static const unsigned long UI_WPM_OVERLAY_MS       = 1500;
static const unsigned long UI_SAVED_TOAST_MS       = 800;

static const int      UI_WPM_MIN     = 5;
static const int      UI_WPM_MAX     = 40;
static const uint16_t UI_TONE_MIN_HZ = 200;
static const uint16_t UI_TONE_MAX_HZ = 2000;

static const uint8_t UI_NAV_STACK_DEPTH   = 4;
static const uint8_t UI_LIST_VISIBLE_ROWS = 3;

static const unsigned long UI_BLE_OVERLAY_MS   = 2500;
static const uint8_t       UI_LINE_CHARS       = 18;
static const uint8_t       UI_PATTERN_CHARS    = 8;
static const uint8_t       UI_CONTRAST_MIN     = 10;
static const uint8_t       UI_CONTRAST_MAX     = 255;
static const uint8_t       UI_CONTRAST_DEFAULT = 128;

static const unsigned long UI_DIAG_REFRESH_MS    = 150;
static const unsigned long UI_DIAG_ICON_CYCLE_MS = 1200;

// ----------------------------------------------------------------------------
// CW Keyer submenu additions - Tune safety auto-timeout. Reuses the same
// UI_DIAG_REFRESH_MS cadence (above) for Live Monitor's live refresh -
// no new refresh constant needed there.
// ----------------------------------------------------------------------------
static const unsigned long UI_TUNE_TIMEOUT_MS = 30000;

// Adopted from the reference animation source (FRAME_DELAY=42, ~24fps),
// tuned for this specific frame set - the XBM/bit-order fallback from
// the previous pass is removed: confirmed unnecessary, since
// Adafruit_GFX::drawBitmap() and u8g2::drawBitmap() share the same
// MSB-first convention.
static const unsigned long UI_ICON_ANIM_FRAME_MS = 42;

#endif // UI_CONFIG_H