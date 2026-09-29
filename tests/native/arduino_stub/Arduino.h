// Minimal host-native stand-in for Arduino.h, just enough for
// core_decoder.cpp (and the headers/config.h it pulls in) to compile
// and link outside the ESP32 toolchain. Not a general Arduino shim -
// add symbols here only as new native tests need them.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#define PROGMEM
#define memcpy_P(dst, src, n)   memcpy((dst), (src), (n))
#define strncpy_P(dst, src, n)  strncpy((dst), (src), (n))

// Test binaries provide the real definition so each test controls its
// own fake clock (see tests/native/test_core_decoder.cpp).
unsigned long millis();

// Test binaries provide these too (see tests/native/test_core_trainer.cpp) -
// deterministic by default so target-character selection is repeatable.
long random(long min, long max);
void randomSeed(unsigned long seed);

// GPIO/LEDC - no real hardware on host. pinMode/ledc* are harmless no-ops
// (core_keyer.cpp only calls them, never reads them back); digitalRead is
// test-provided (see tests/native/test_core_keyer.cpp) so each test
// controls its own simulated pin state, same pattern as millis() above.
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin);
inline void ledcSetup(uint8_t, double, uint8_t) {}
inline void ledcAttach(uint8_t, double, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}
inline void ledcWriteTone(uint8_t, uint32_t) {}
