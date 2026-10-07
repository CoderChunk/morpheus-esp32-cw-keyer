// Decoder throughput/latency benchmark (non-functional). Host numbers are a
// relative regression indicator, NOT ESP32 timings (an ESP32 at 240 MHz is
// roughly 10-30x slower per operation than this host).
#include <chrono>
#include <cstdio>
#include <cstring>
#include "core_decoder.h"
#include "config.h"
static unsigned long g_ms = 0;
unsigned long millis() { return g_ms; }
unsigned long core_keyer_getDitLengthMs() { return 60; }
bool core_keyer_isTxActive() { return false; }
static unsigned long chars = 0, words = 0;
void events_onCharacterComplete(char, const char*) { ++chars; }
void events_onWordComplete(const char*, unsigned long) { ++words; }
void events_onPatternChanged(const char*, unsigned long) {}
int main() {
  core_decoder_init(); core_decoder_setEnabled(true);
  const int N = 200000;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < N; ++i) {
    for (int k = 0; k < 3; ++k) { g_ms += 60; core_decoder_addElement(ELEM_DIT, g_ms); g_ms += 60; core_decoder_service(g_ms); }
    g_ms += 200; core_decoder_service(g_ms);          // character gap
    if (i % 5 == 4) { g_ms += 450; core_decoder_service(g_ms); }   // word gap
  }
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
  double per = double(ns) / (N * 3.0);
  std::printf("decoder: %.0f elements in %.1f ms = %.0f ns/element (host); chars %lu words %lu\n",
              N * 3.0, ns / 1e6, per, chars, words);
  if (chars < (unsigned long)N * 9 / 10) { std::printf("FAIL: decoder dropped characters under load\n"); return 1; }
  if (per > 20000) { std::printf("FAIL: decoder slower than 20 us/element on host\n"); return 1; }
  std::printf("PASS: decoder throughput within budget\n");
}
