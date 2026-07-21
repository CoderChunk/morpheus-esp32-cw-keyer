/*
 * ============================================================================
 * MORPHEUS - Status LED Driver
 * ============================================================================
 * File: core_led.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Single RED LED, GPIO27. BLE status display is gated by
 * bleLedEnabled (default OFF, user preference, independent of whether
 * BLE itself is on) - transport.cpp reports radio state
 * unconditionally; this module decides whether to ever show it.
 *
 * Connected state is a brief confirmation blink, not steady-on - an
 * ordinary established connection is not an alarm condition and
 * shouldn't hold the LED lit indefinitely. Only PAIRING (a bounded,
 * user-triggered window) blinks continuously.
 *
 * TX pulse and LED Trainer flashes both take priority over any BLE
 * display state, same arbitration as the original design.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_led.h"
#include "config.h"

static LedRadioState radioState = LED_RADIO_OFF;
static bool bleLedEnabled = false;

static unsigned long patternPhaseStartMs = 0;

static bool confirmBlinkActive = false;
static unsigned long confirmBlinkStartMs = 0;

static bool txPulseActive = false;
static unsigned long txPulseStartMs = 0;

static bool trainerOwnsLed = false;

static void writeLed(bool on) {
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

void core_led_init() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  writeLed(false);
  radioState = LED_RADIO_OFF;
  bleLedEnabled = false;
  patternPhaseStartMs = millis();
  confirmBlinkActive = false;
  txPulseActive = false;
  trainerOwnsLed = false;
}

void core_led_setBleLedEnabled(bool enabled) { bleLedEnabled = enabled; }
bool core_led_getBleLedEnabled() { return bleLedEnabled; }

void core_led_setRadioState(LedRadioState state) {
  if (state == radioState) return;

  // Entering CONNECTED from anything else triggers the brief
  // confirmation blink, once, rather than a sustained lit state.
  if (state == LED_RADIO_CONNECTED && radioState != LED_RADIO_CONNECTED) {
    confirmBlinkActive = true;
    confirmBlinkStartMs = millis();
  }
  radioState = state;
  patternPhaseStartMs = millis();
}

void core_led_pulseTx() {
  txPulseActive = true;
  txPulseStartMs = millis();
}

void core_led_trainerFlashOn() {
  trainerOwnsLed = true;
  writeLed(true);
}
void core_led_trainerFlashOff() {
  writeLed(false);
  trainerOwnsLed = false;
}

// Confirmation blink: N short on/off pulses, then done. Fully
// self-terminating - doesn't rely on radioState changing again.
static bool serviceConfirmBlink(unsigned long now) {
  unsigned long elapsed = now - confirmBlinkStartMs;
  unsigned long totalMs = (unsigned long)LED_CONNECT_CONFIRM_BLINK_COUNT * LED_CONNECT_CONFIRM_BLINK_MS * 2;
  if (elapsed >= totalMs) {
    confirmBlinkActive = false;
    writeLed(false);
    return false;
  }
  bool on = (elapsed % (LED_CONNECT_CONFIRM_BLINK_MS * 2)) < LED_CONNECT_CONFIRM_BLINK_MS;
  writeLed(on);
  return true;
}

static void serviceRadioPattern(unsigned long now) {
  if (!bleLedEnabled) { writeLed(false); return; }

  if (confirmBlinkActive) {
    if (serviceConfirmBlink(now)) return;
    // falls through once the confirm blink finishes
  }

  switch (radioState) {
    case LED_RADIO_OFF:
      writeLed(false);
      return;
    case LED_RADIO_CONNECTED:
      writeLed(false);   // confirmation already shown; steady state is dark, not lit
      return;
    case LED_RADIO_PAIRING: {
      unsigned long elapsed = now - patternPhaseStartMs;
      bool on = (elapsed % (LED_BLINK_FAST_MS * 2)) < LED_BLINK_FAST_MS;
      writeLed(on);
      return;
    }
  }
}

void core_led_service(unsigned long now) {
  if (trainerOwnsLed) return;

  if (txPulseActive) {
    if (now - txPulseStartMs < LED_TX_PULSE_MS) {
      writeLed(true);
      return;
    }
    txPulseActive = false;
  }

  serviceRadioPattern(now);
}