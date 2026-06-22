/*
 * ============================================================================
 * MORPHEUS
 * Forge the Sound of Morse.
 * ============================================================================
 *
 * File: MORPHEUS.ino
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * MORPHEUS is an open-source ESP32 Morse keyer designed around modular
 * architecture, secure wireless telemetry, and real-time decoding.
 *
 * This file is the project's central integration layer.
 *
 * Every subsystem meets here:
 *
 *   • Keying engine
 *   • Morse decoder
 *   • OLED display
 *   • BLE transport
 *   • Diagnostics services
 *
 * Contributors looking to understand the overall architecture should
 * begin here.
 *
 * The goal of MORPHEUS is not simply to send Morse code.
 * It is to build a platform that encourages experimentation, learning,
 * and innovation in modern CW technology.
 *
 * Pull requests, improvements, and new ideas are always welcome.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "config.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "display.h"
#include "transport.h"
#include "services.h"

// ----------------------------------------------------------------------------
// Event hook definitions
// ----------------------------------------------------------------------------

void events_onKeyDown(unsigned long now) {
#if FEATURE_SERIAL
  services_logKeyDown(now);
#endif
}

void events_onKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now) {
#if FEATURE_SERIAL
  services_logKeyUp(type, durMs, thresholdMs, now);
#endif
  // Hand the completed element off to the decoder. Not flag-gated - the
  // decoder is a core feature, same as the keyer.
  core_decoder_addElement(type, now);
}

void events_onCharacterComplete(char decodedChar, const char *pattern) {
#if FEATURE_SERIAL
  services_logCharacterComplete(decodedChar, pattern);
#endif
}

void events_onWordComplete(const char *word, unsigned long now) {
#if FEATURE_SERIAL
  services_logWordComplete(word, now);
#endif
#if FEATURE_OLED
  display_appendWord(word);
#endif
#if FEATURE_BLE
  transport_notifyWordCompleted(word, core_keyer_getWpm(), core_keyer_getMode(), now);
#endif
}

// ----------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------
void setup() {
#if FEATURE_SERIAL
  Serial.begin(115200);
  delay(50);
  Serial.println(F("=== MORPHEUS Boot ==="));
#endif

  core_keyer_init();
  core_decoder_init();

#if FEATURE_OLED
  display_init();
#endif

  services_init();

  // BLE last - brought up only after all other hardware is ready, exactly
  // as in the validated Stage 2 boot order.
#if FEATURE_BLE
  transport_init();
#endif

#if FEATURE_SERIAL
  Serial.println(F("Keyer ready. PJ311 jack: Tip=GPIO25, Ring=GPIO26."));
#endif
}

// ----------------------------------------------------------------------------
// Main loop - fully non-blocking, no delay() anywhere in the timing path
// ----------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  OperatingMode modeBefore = core_keyer_getMode();
  core_keyer_service(now);
  OperatingMode modeAfter = core_keyer_getMode();
  if (modeAfter != modeBefore) {
#if FEATURE_SERIAL
    services_logModeChange(modeAfter);
#endif
  }

  core_decoder_service(now);

#if FEATURE_POTENTIOMETER
  services_servicePotentiometer(now);
#endif

#if FEATURE_BLE
  transport_service(now);
#endif

#if FEATURE_OLED
  display_service(now);
#endif

#if FEATURE_SERIAL
  services_serviceDiagnostics(now);
#endif
}