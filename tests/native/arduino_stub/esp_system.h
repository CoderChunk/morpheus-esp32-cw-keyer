// Minimal host-native stand-in for ESP-IDF's esp_system.h - only
// esp_random() is needed, by core_trainer.cpp's core_trainer_init()
// (randomSeed(esp_random())). Not a general ESP-IDF shim.
#pragma once
#include <cstdint>

inline uint32_t esp_random() { return 0x5eed; }
