/*
 * ============================================================================
 * MORPHEUS - Service Interface
 * ============================================================================
 *
 * File: services.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * This interface exposes diagnostic, utility, and support services used
 * throughout the MORPHEUS firmware.
 *
 * The service layer helps keep the core modules focused while providing
 * optional capabilities such as logging, monitoring, and maintenance.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_SERVICES_H
#define MORPHEUS_SERVICES_H

#include <Arduino.h>
#include "core_keyer.h"   // ElementType/OperatingMode/KeyState - hook
                          // payload types for the log functions below.

void services_init();

// Periodic, throttled, change-detected Serial STATUS heartbeat - call every
// loop() iteration; no-ops internally until SERIAL_STATUS_INTERVAL_MS has
// elapsed or something actually changed. Pulls keyer state (mode/wpm/tx/
// key-state/dit-length) from core_keyer and the in-progress pattern from
// core_decoder.
void services_serviceDiagnostics(unsigned long now);

// One-shot diagnostic line for an immediate mode change - called by
// MORPHEUS.ino's loop() right after it detects core_keyer_getMode() changed.
void services_logModeChange(OperatingMode newMode);

// Per-event Serial diagnostics - called from MORPHEUS.ino's event-hook
// fan-out (each wrapped there in #if FEATURE_SERIAL).
void services_logKeyDown(unsigned long now);
void services_logKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now);
void services_logCharacterComplete(char decodedChar, const char *pattern);
void services_logWordComplete(const char *word, unsigned long now);

// Session info - exposed per the architecture's stated scope, not currently
// auto-printed anywhere (kept unwired to preserve identical behavior
// pending a decision on where to surface it).
unsigned long services_getUptimeMs();
uint32_t      services_getFreeHeapBytes();

// Operator settings persistence - loads saved settings during startup.
void services_loadSettings();

// Periodic settings service - performs debounced NVS saves when settings change.
void services_serviceSettings(unsigned long now);

// Restores operator settings to defaults without affecting BLE bonding data.
void services_factoryResetSettings();

#endif // MORPHEUS_SERVICES_H