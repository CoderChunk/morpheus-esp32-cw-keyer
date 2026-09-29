// Host-native stand-in for Arduino.h, extended from tests/native's version
// for oled_render's wider link set (ui_*.cpp, core_*.cpp, services.cpp).
// Only real hardware I/O is stubbed (GPIO/LEDC/critical sections all no-op
// or return a fixed idle value) - everything else (millis, random, the
// u8g2 glue) is real firmware logic running unmodified on the host.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <cstdio>

#define PROGMEM
typedef uint8_t byte;
#define memcpy_P(dst, src, n)   memcpy((dst), (src), (n))
#define strncpy_P(dst, src, n)  strncpy((dst), (src), (n))

unsigned long millis();
long random(long min, long max);
void randomSeed(unsigned long seed);
void delay(unsigned long ms);

// ---- GPIO / LEDC: no real hardware on host, so these are no-ops or
// return a fixed idle reading. Render output never depends on real
// button/GPIO state - screens are driven by synthetic UiEvents instead.
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return HIGH; }
inline void ledcSetup(uint8_t, double, uint8_t) {}
inline void ledcAttach(uint8_t, double, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}
inline void ledcWriteTone(uint8_t, uint32_t) {}

#define IRAM_ATTR
#define CHANGE 1
#define RISING 2
#define FALLING 3
typedef void (*voidFuncPtr)();
inline int  digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int, voidFuncPtr, int) {}
inline void detachInterrupt(int) {}

// FreeRTOS critical sections: single-threaded harness, no-op.
struct portMUX_TYPE { int _unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
inline void portENTER_CRITICAL_ISR(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE *) {}

// Minimal Arduino String - only what ui_backend.cpp's ESP.getChipModel()
// path needs (construct from const char*, read back via c_str()).
class String {
 public:
  String() { buf_[0] = '\0'; }
  String(const char *s) { snprintf(buf_, sizeof(buf_), "%s", s ? s : ""); }
  const char *c_str() const { return buf_; }
 private:
  char buf_[32];
};

// Minimal ESP object - fixed, clearly-fake values for the diagnostics
// "About/System Info" screen. Never used for anything render-affecting
// beyond displaying these strings/numbers on screen.
struct EspClass {
  const char *getChipModel() const { return "host-render"; }
  uint8_t     getChipRevision() const { return 0; }
  uint32_t    getCpuFreqMHz() const { return 240; }
  const char *getSdkVersion() const { return "host-sim"; }
  uint64_t    getEfuseMac() const { return 0; }
  uint32_t    getFlashChipSize() const { return 4 * 1024 * 1024; }
  uint32_t    getSketchSize() const { return 0; }
  uint32_t    getFreeSketchSpace() const { return 0; }
  uint32_t    getMinFreeHeap() const { return 200000; }
  uint32_t    getHeapSize() const { return 320000; }
  uint32_t    getFreeHeap() const { return 250000; }
  void        restart() const {}
};
extern EspClass ESP;
