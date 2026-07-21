/*
 * ============================================================================
 * MORPHEUS - Service Interface
 * ============================================================================
 *
 * File: services.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * DIAGNOSTICS ADDITION: NVS status getters (was this boot's settings
 * loaded or defaulted, when was the last save) and a permanent, silent
 * loop-rate counter for the Memory/Performance diagnostic page. The
 * counter costs one increment per loop() iteration - no Serial I/O,
 * unlike the temporary DEBUG_KEYER_TONE instrumentation.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_SERVICES_H
#define MORPHEUS_SERVICES_H

#include <Arduino.h>
#include "core_keyer.h"

void services_init();

void services_serviceDiagnostics(unsigned long now);
void services_logModeChange(OperatingMode newMode);
void services_logKeyDown(unsigned long now);
void services_logKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now);
void services_logCharacterComplete(char decodedChar, const char *pattern);
void services_logWordComplete(const char *word, unsigned long now);

unsigned long services_getUptimeMs();
uint32_t      services_getFreeHeapBytes();

void services_loadSettings();
void services_serviceSettings(unsigned long now);
void services_factoryResetSettings();

bool          services_wasSettingsDefaulted();
unsigned long services_getLastSettingsSaveMs();
const char   *services_getSettingsNamespace();

void     services_tickLoopCounter(unsigned long now);
uint32_t services_getLoopRateHz();

bool    services_getDisplayInvert();
void    services_setDisplayInvert(bool inverted);
uint8_t services_getDisplayTimeoutIndex();
void    services_setDisplayTimeoutIndex(uint8_t index);

void services_getCallsign(char *out, size_t outSize);
void services_setCallsign(const char *value);
bool services_getCallsignEnabled();
void services_setCallsignEnabled(bool enabled);

uint8_t services_getDateFormat();
void    services_setDateFormat(uint8_t index);
uint8_t services_getTimeFormat();
void    services_setTimeFormat(uint8_t index);

// BLE preferences - the actual on/off state and connection logic live in
// transport.cpp; services.cpp only persists the user's chosen preference
// and restores it via transport_setBleEnabled()/core_led_setBleLedEnabled()
// at boot, same pattern as every other setting in this file.
bool services_getBleEnabled();
void services_setBleEnabled(bool enabled);
bool services_getBleLedEnabled();
void services_setBleLedEnabled(bool enabled);

#endif // MORPHEUS_SERVICES_H