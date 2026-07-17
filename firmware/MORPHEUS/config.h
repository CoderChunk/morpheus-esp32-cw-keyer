/*
 * ============================================================================
 * MORPHEUS - ESP32 CW Keyer
 * ============================================================================
 *
 * File: config.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * SETTINGS_VERSION bumped 2->3: OperatorSettings gained a decoderEnabled
 * field. Existing saved blobs fail the byte-size/version check in
 * services_loadSettings() and cleanly fall back to defaults - no crash,
 * same upgrade-write path already used for the prior mode-field bump.
 *
 * Copyright (C) 2026 Coder Chunk
 * Released under the GNU General Public License v3.
 *
 * ============================================================================
 */

#ifndef MORPHEUS_CONFIG_H
#define MORPHEUS_CONFIG_H

#include <Arduino.h>

#define FEATURE_OLED                  1
#define FEATURE_BLE                   1
#define FEATURE_SIDETONE              1
#define FEATURE_SERIAL                1
#define FEATURE_DEBUG_SERIAL_COMMANDS 0

#define DEBUG_KEYER_TONE 1

#define PIN_OLED_SDA      21
#define PIN_OLED_SCL      22
#define PIN_JACK_TIP      25
#define PIN_JACK_RING     26
#define PIN_BUZZER        18

#define OLED_I2C_ADDR     0x3C

static const int           WPM_MIN                   = 5;
static const int           WPM_MAX                   = 40;
static const unsigned long DEBOUNCE_MS               = 5;
static const uint32_t      TONE_FREQ_HZ              = 600;
static const unsigned long DISPLAY_INTERVAL_MS       = 100;
static const unsigned long SERIAL_STATUS_INTERVAL_MS = 1000;
static const float         CHAR_GAP_MULT             = 3.0f;
static const float         WORD_GAP_MULT             = 7.0f;
static const uint8_t       MAX_PATTERN_LEN           = 8;
static const uint8_t       MAX_WORD_LEN              = 64;
static const uint8_t       LINE_CHARS                = 18;
static const uint8_t       TRANSCRIPT_LEN            = 48;
static const unsigned long SETTINGS_SAVE_DEBOUNCE_MS = 5000;
static const bool          DEFAULT_PADDLE_REVERSED   = false;
static const uint16_t      SETTINGS_VERSION          = 3;   // bumped: +decoderEnabled
static const uint32_t      SIDETONE_FREQ_MIN_HZ      = 200;
static const uint32_t      SIDETONE_FREQ_MAX_HZ      = 2000;

static const int DEFAULT_WPM = 18;

static const float STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT = 2.0f;

static const char     BLE_DEVICE_NAME[]        = "MORPHEUS-CW";
static const char     BLE_SERVICE_UUID[]       = "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b";
static const char     BLE_WORD_CHAR_UUID[]     = "7a48a2b0-0002-4ad4-9f1a-1c2d3e4f5a6b";
static const uint16_t BLE_REQUESTED_MTU        = 128;
static const uint8_t  BLE_WORD_FIELD_CAP       = 24;
static const uint8_t  BLE_JSON_OVERHEAD_BYTES  = 64;
static const unsigned long BLE_PAIR_MSG_DURATION_MS = 2500;
static const uint16_t BLE_CONN_HANDLE_INVALID  = 0xFFFF;

#endif // MORPHEUS_CONFIG_H