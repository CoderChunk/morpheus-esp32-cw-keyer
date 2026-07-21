#ifndef MORPHEUS_CORE_LED_H
#define MORPHEUS_CORE_LED_H
#include <Arduino.h>

enum LedRadioState : uint8_t { LED_RADIO_OFF, LED_RADIO_PAIRING, LED_RADIO_CONNECTED };

void core_led_init();
void core_led_service(unsigned long now);

// Called by transport.cpp as BLE's real state changes. Has no visible
// effect unless the LED-for-BLE preference (core_led_setBleLedEnabled)
// is on - transport.cpp calls this unconditionally and lets core_led
// own the "should this even be shown" decision, so the radio logic
// never needs to know about the display preference.
void core_led_setRadioState(LedRadioState state);

void core_led_setBleLedEnabled(bool enabled);
bool core_led_getBleLedEnabled();

void core_led_pulseTx();
void core_led_trainerFlashOn();
void core_led_trainerFlashOff();

#endif