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
static const uint16_t      SETTINGS_VERSION          = 5;   // bumped: +volumePercent, +sidetoneEnabled
static const uint32_t      SIDETONE_FREQ_MIN_HZ      = 200;
static const uint32_t      SIDETONE_FREQ_MAX_HZ      = 2000;
// ----------------------------------------------------------------------------
// Statistics module - own NVS versioning, separate from
// SETTINGS_VERSION/operator settings (different persistence lifecycle).
// ----------------------------------------------------------------------------
static const uint16_t      STATS_VERSION          	 = 1;
static const unsigned long STATS_SAVE_DEBOUNCE_MS 	 = 60000;   // 1 min - protects flash wear
static const uint8_t       STATS_WPM_HISTORY_CAP  	 = 6;

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

// ----------------------------------------------------------------------------
// Training module tunables
// ----------------------------------------------------------------------------
static const uint8_t       DEFAULT_KOCH_LEVEL       = 2;
static const uint8_t       TRAIN_KOCH_WINDOW        = 20;
static const uint8_t       TRAIN_KOCH_LEVELUP_PCT   = 90;
static const uint8_t       TRAIN_EXAM_LENGTH        = 25;
static const uint8_t       TRAIN_EXAM_PASS_PCT      = 90;
static const unsigned long TRAIN_FEEDBACK_MS        = 1200;
static const uint8_t       TRAIN_ADAPTIVE_STREAK    = 3;
static const int           TRAIN_ADAPTIVE_STEP_UP   = 1;
static const int           TRAIN_ADAPTIVE_STEP_DOWN = 2;
static const int           DEFAULT_FARNSWORTH_WPM   = 10;

// ----------------------------------------------------------------------------
// Games module tunables - own NVS versioning, high scores survive
// Factory Reset by design (same precedent as lifetime stats).
// ----------------------------------------------------------------------------
static const uint16_t      GAMES_VERSION           = 1;
static const uint8_t       GAME_COPY_START_LIVES   = 3;
static const uint8_t       GAME_SPEED_START_LIVES  = 3;
static const unsigned long GAME_FEEDBACK_MS        = 500;
static const unsigned long GAME_ROUND_OK_MS        = 700;
static const uint8_t       GAME_MEMORY_MAX_CHAIN   = 20;

// ----------------------------------------------------------------------------
// Audio - volume (PWM duty-cycle scaling; no DAC/amp on this hardware,
// so this is an approximation of loudness, not a calibrated curve - see
// project notes) and runtime sidetone on/off (keying sidetone only -
// Diagnostics/Tune/Training/Games are unaffected by this toggle).
// ----------------------------------------------------------------------------
static const uint8_t DEFAULT_VOLUME_PERCENT  = 80;
static const uint8_t VOLUME_MIN              = 0;
static const uint8_t VOLUME_MAX              = 100;
static const uint8_t VOLUME_STEP             = 5;
static const bool    DEFAULT_SIDETONE_ENABLED = true;

// ----------------------------------------------------------------------------
// Profiles module - 4 fixed preset slots, own NVS namespace. Deliberately
// NOT touched by Settings > Factory Reset - a saved profile is a
// recovery path after resetting live settings, not something a reset
// should destroy (same precedent as Statistics/Games high scores).
// ----------------------------------------------------------------------------
static const uint16_t PROFILES_VERSION = 1;

#endif // MORPHEUS_CONFIG_H