/*
 * ============================================================================
 * MORPHEUS - ESP32 CW Keyer
 * ============================================================================
 *
 * File: config.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * This file defines the entire personality of MORPHEUS.
 *
 * Hardware assignments, feature switches, timing constants, operating
 * limits, BLE parameters, and system tuning values all originate here.
 * By changing a few values, contributors can adapt MORPHEUS to entirely
 * different hardware configurations without touching the application logic.
 *
 * Whether you are experimenting with alternative displays, adding new
 * peripherals, or tuning Morse behavior, this is the project's control
 * center.
 *
 * MORPHEUS is designed to remain modular, configurable, and easy to port.
 *
 * Copyright (C) 2026 Coder Chunk
 * Released under the GNU General Public License v3.
 *
 * ============================================================================
 */

#ifndef MORPHEUS_CONFIG_H
#define MORPHEUS_CONFIG_H

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Feature flags - toggle 0/1.
// ----------------------------------------------------------------------------
#define FEATURE_OLED          1
#define FEATURE_BLE           1
#define FEATURE_POTENTIOMETER 0   // optional hardware, not currently installed
#define FEATURE_SIDETONE      1
#define FEATURE_SERIAL        1

// ----------------------------------------------------------------------------
// Pin map (validated on ESP32-WROOM-32)
// ----------------------------------------------------------------------------
#define PIN_OLED_SDA      21
#define PIN_OLED_SCL      22
#define PIN_POT_WPM       34   // ADC1_CH6 - only read if FEATURE_POTENTIOMETER
#define PIN_MODE_SWITCH   33   // HIGH = STRAIGHT, LOW = PADDLE
#define PIN_JACK_TIP      25   // STRAIGHT: key; PADDLE: DIT
#define PIN_JACK_RING     26   // STRAIGHT: ignored; PADDLE: DAH
#define PIN_BUZZER        18
// GPIO27 intentionally unassigned - reserved for future use.

#define OLED_I2C_ADDR     0x3C

// ----------------------------------------------------------------------------
// Tunables
// ----------------------------------------------------------------------------
static const int           WPM_MIN                   = 5;
static const int           WPM_MAX                   = 40;
static const unsigned long DEBOUNCE_MS               = 5;
static const uint32_t      TONE_FREQ_HZ              = 600;
static const unsigned long DISPLAY_INTERVAL_MS       = 100;
static const unsigned long SERIAL_STATUS_INTERVAL_MS = 1000;
static const float         ADC_SMOOTHING_ALPHA       = 0.15f;
static const float         CHAR_GAP_MULT             = 3.0f;
static const float         WORD_GAP_MULT             = 7.0f;
static const uint8_t       MAX_PATTERN_LEN           = 8;
static const uint8_t       MAX_WORD_LEN              = 64;
static const uint8_t       LINE_CHARS                = 18;
static const uint8_t       TRANSCRIPT_LEN            = 48;

static const int DEFAULT_WPM = 15;

static const float STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT = 2.0f;

// ----------------------------------------------------------------------------
// BLE tunables - protocol spec values, do not change without deliberate
// reason. PLACEHOLDER UUIDs - regenerate with a fresh random v4 UUID
// before any real/multi-device deployment.
// ----------------------------------------------------------------------------
static const char     BLE_DEVICE_NAME[]        = "MORPHEUS-CW";
static const char     BLE_SERVICE_UUID[]       = "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b";
static const char     BLE_WORD_CHAR_UUID[]     = "7a48a2b0-0002-4ad4-9f1a-1c2d3e4f5a6b";
static const uint16_t BLE_REQUESTED_MTU        = 128;
static const uint8_t  BLE_WORD_FIELD_CAP       = 24;
static const uint8_t  BLE_JSON_OVERHEAD_BYTES  = 60;
static const unsigned long BLE_PAIR_MSG_DURATION_MS = 2500;
static const uint16_t BLE_CONN_HANDLE_INVALID  = 0xFFFF;

#endif // MORPHEUS_CONFIG_H