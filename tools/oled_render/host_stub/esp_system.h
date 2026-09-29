// Host-native stand-in for ESP-IDF's esp_system.h - just esp_random()
// (core_trainer.cpp's randomSeed(esp_random())) and esp_reset_reason()
// (ui_backend.cpp's diagnostics "reset reason" line). Not a general
// ESP-IDF shim.
#pragma once
#include <cstdint>

inline uint32_t esp_random() { return 0x5eed; }

typedef enum {
  ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
  ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
  ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO
} esp_reset_reason_t;

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
