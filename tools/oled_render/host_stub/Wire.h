// Host stand-in for Arduino's <Wire.h>. ui_renderer.cpp calls Wire.begin()
// during ui_renderer_init(), but oled_render's u8g2 byte callback
// (host_stub/glue.cpp) never actually transfers anything over it - this
// stub only needs to exist and accept that call.
#pragma once
#include <cstdint>

class TwoWire {
 public:
  bool begin(int sda = -1, int scl = -1) { (void)sda; (void)scl; return true; }
  void setClock(uint32_t) {}
};
extern TwoWire Wire;
