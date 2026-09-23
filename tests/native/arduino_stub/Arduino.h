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
