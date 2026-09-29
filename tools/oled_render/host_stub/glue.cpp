// Host glue: the small pieces Arduino.h declares but doesn't define, plus
// the two U8g2 hardware callbacks the linker needs for the
// *_HW_I2C constructor firmware's ui_renderer.cpp uses. u8g2's I2C byte/
// gpio callbacks are never invoked for anything render_main.cpp reads -
// drawing writes straight into u8g2's in-RAM buffer, and these two only
// run during u8g2.begin()/sendBuffer(), which the harness calls (to match
// real firmware behavior) but whose actual byte traffic goes nowhere.
#include "Arduino.h"
#include "Wire.h"
#include "clib/u8x8.h"
#include <chrono>
#include <cstdlib>

EspClass ESP;
TwoWire Wire;

static const auto kStart = std::chrono::steady_clock::now();

unsigned long millis() {
  auto now = std::chrono::steady_clock::now();
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(now - kStart).count();
}

long random(long min, long max) {
  if (max <= min) return min;
  return min + (std::rand() % (max - min));
}

void randomSeed(unsigned long seed) { std::srand((unsigned)seed); }

void delay(unsigned long) {}

extern "C" uint8_t u8x8_gpio_and_delay_arduino(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
  return 1;
}

extern "C" uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
  return 1;
}

// Real implementation (from U8x8lib.cpp) - just u8x8_SetPin() (clib, real)
// three times, no hardware access. Reproduced here rather than compiling
// all of U8x8lib.cpp, which pulls in the real Wire/SPI-backed byte
// callbacks this harness deliberately doesn't need.
void u8x8_SetPin_HW_I2C(u8x8_t *u8x8, uint8_t reset, uint8_t clock, uint8_t data) {
  u8x8_SetPin(u8x8, U8X8_PIN_RESET, reset);
  u8x8_SetPin(u8x8, U8X8_PIN_I2C_CLOCK, clock);
  u8x8_SetPin(u8x8, U8X8_PIN_I2C_DATA, data);
}
