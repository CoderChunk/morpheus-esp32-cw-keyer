#include "core_decoder.h"
#include "keyer_metrics.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static unsigned long clockMs = 0;
unsigned long millis() { return clockMs; }
unsigned long core_keyer_getDitLengthMs() { return 60; }
bool core_keyer_isTxActive() { return false; }
static KeyerMetrics metrics;
void events_onCharacterComplete(char, const char *) { metrics.character(); }
void events_onWordComplete(const char *, unsigned long) { metrics.word(); }
void events_onPatternChanged(const char *, unsigned long) {}

static void sample(ElementType type, uint32_t start, uint32_t end, bool remote = false) {
  clockMs = start; metrics.keyDown(start, remote);
  clockMs = end; metrics.element(type == ELEM_DAH, end - start, end);
  core_decoder_addElement(type, end, remote);
}
static void service(uint32_t now) { clockMs = now; core_decoder_service(now); }
static void expect(const char *fragment, uint32_t now) {
  char packet[240];
  assert(metrics.encode(packet, sizeof(packet), "probe123", now));
  assert(std::strstr(packet, fragment));
}
int main() {
  core_decoder_init();
  expect("\"ditMs\":null", 0);
  sample(ELEM_DIT, 100, 173);  // Actual 73 ms, not the configured 60 ms.
  expect("\"ditMs\":73", 173);
  service(355); // The real decoder declares the previous character complete.
  sample(ELEM_DAH, 391, 604); // Actual character silence: 391-173 = 218 ms.
  expect("\"dahMs\":213", 604);
  expect("\"gapMs\":218", 604);
  service(1200); // Word gap: must not replace the measured character gap.
  sample(ELEM_DIT, 2000, 2081);
  expect("\"gapMs\":218", 2081);
  expect("\"ditMs\":null", 40000); // Every sample expires independently.
  expect("\"gapMs\":null", 40000);
  metrics.reset();
  sample(ELEM_DAH, 41000, 41207, true); // Source switch discards physical samples.
  expect("\"ditMs\":null", 41207);
  expect("\"dahMs\":207", 41207);
  expect("\"virtual\":true", 41207);
  metrics.reset(); metrics.keyDown(42000); metrics.reset(); metrics.element(false, 50, 42050);
  expect("\"ditMs\":null", 42050); // No partial sample after reset while held.
  metrics.reset();
  metrics.keyDown(0xfffffff0U); metrics.element(false, 40, 24); metrics.character(); metrics.keyDown(225);
  expect("\"ditMs\":40", 225);
  expect("\"gapMs\":201", 225); // uint32 millis rollover is safe.
  char packet[240];
  assert(!metrics.encode(packet, sizeof(packet), "bad\"token", 225));
  assert(!metrics.encode(packet, 10, "probe123", 225));
  assert(metrics.encode(packet, sizeof(packet), "abcdefghijklmnopqrstuvwx", 0xffffffffU));
  assert(std::strlen(packet) < 240);
  std::puts("PASS real decoder boundaries, measured keyer durations, expiry, reset, rollover and packet budget");
}
