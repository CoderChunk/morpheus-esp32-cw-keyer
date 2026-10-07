// NEGATIVE tests of the real core_decoder.cpp: hostile or impossible input must be
// contained (no overflow, no stuck state, no spurious output). Run hardened by
// run_sanitized.sh. Test IDs NEG-F01 .. NEG-F10 (docs/TEST_SUITE.md).
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "core_decoder.h"
#include "config.h"

static unsigned long g_ms = 0;
unsigned long millis() { return g_ms; }
unsigned long core_keyer_getDitLengthMs() { return 60; }
bool core_keyer_isTxActive() { return false; }
static std::vector<char> g_chars; static std::vector<std::string> g_words, g_patterns;
void events_onCharacterComplete(char c, const char *p) { g_chars.push_back(c); g_patterns.push_back(p); }
void events_onWordComplete(const char *w, unsigned long) { g_words.push_back(w); }
void events_onPatternChanged(const char *, unsigned long) {}

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void reset() { g_chars.clear(); g_words.clear(); g_patterns.clear(); core_decoder_init(); core_decoder_setEnabled(true); core_decoder_setTrainingSink(nullptr); }
static void element(ElementType t, unsigned long hold) { g_ms += 60; core_decoder_addElement(t, g_ms); g_ms += hold; core_decoder_service(g_ms); }

int main() {
  // NEG-F01 a character of 40 dits cannot overflow the pattern buffer
  reset();
  for (int i = 0; i < 40; ++i) { g_ms += 60; core_decoder_addElement(ELEM_DIT, g_ms); }
  CHECK(core_decoder_getCharPatternLen() <= MAX_PATTERN_LEN);
  g_ms += 400; core_decoder_service(g_ms);
  CHECK(core_decoder_getCharPatternLen() == 0);              // finalised and cleared

  // NEG-F02 a word of 400 characters with no word gap cannot overflow the word buffer
  reset();
  for (int i = 0; i < 400; ++i) { g_ms += 60; core_decoder_addElement(ELEM_DIT, g_ms); g_ms += 200; core_decoder_service(g_ms); }
  CHECK(core_decoder_getWordLen() <= MAX_WORD_LEN);
  CHECK(strlen(core_decoder_getWordBuffer()) <= MAX_WORD_LEN);

  // NEG-F03 a disabled decoder ignores elements and emits nothing
  reset(); core_decoder_setEnabled(false);
  for (int i = 0; i < 10; ++i) element(ELEM_DIT, 80);
  g_ms += 2000; core_decoder_service(g_ms);
  CHECK(g_chars.empty() && g_words.empty());
  CHECK(core_decoder_getCharPatternLen() == 0 && core_decoder_getWordLen() == 0);

  // NEG-F04 disabling mid-character discards the partial decode (no stale output after re-enable)
  reset(); element(ELEM_DIT, 20); core_decoder_setEnabled(false);
  CHECK(core_decoder_getCharPatternLen() == 0);
  core_decoder_setEnabled(true); g_ms += 2000; core_decoder_service(g_ms);
  CHECK(g_chars.empty());

  // NEG-F05 clock wraparound (millis() rolls over) still decodes correctly
  reset(); g_ms = 0xFFFFFF00UL;
  for (int i = 0; i < 3; ++i) element(ELEM_DIT, 60);          // 'S' = ...
  g_ms += 400; core_decoder_service(g_ms);                    // crosses 2^32 -> small values on 32-bit
  CHECK(g_chars.size() == 1 && g_chars[0] == 'S');

  // NEG-F06 time going backwards in service() must not crash or fabricate characters
  reset(); element(ELEM_DIT, 20);
  core_decoder_service(g_ms - 5000);
  core_decoder_service(0);
  CHECK(g_chars.size() <= 1);

  // NEG-F07 an unknown/impossible pattern decodes to a placeholder, never garbage or a crash
  reset(); for (int i = 0; i < 8; ++i) element(i % 2 ? ELEM_DAH : ELEM_DIT, 60);
  g_ms += 400; core_decoder_service(g_ms);
  for (char c : g_chars) CHECK((c >= 32 && c < 127) || c == '?');

  // NEG-F08 reverse lookup: unmapped characters and tiny/zero buffers are safe
  { char* b = new char[1]; CHECK(!core_decoder_lookupPattern('~', b, 1)); delete[] b; }
  { char* b = new char[2]; core_decoder_lookupPattern('S', b, 2); CHECK(strlen(b) < 2); delete[] b; }
  { char b[1] = {'x'}; core_decoder_lookupPattern('S', b, 0); CHECK(b[0] == 'x'); }   // zero size: untouched
  for (int c : {0, 1, 31, 32, 127, 128, 255}) { char out[16]; CHECK(!core_decoder_lookupPattern((char)c, out, sizeof out) || strlen(out) <= MAX_PATTERN_LEN); }

  // NEG-F09 a training sink that is released mid-session leaves normal decoding intact
  reset();
  static std::vector<char> sunk; sunk.clear();
  core_decoder_setTrainingSink([](char c, const char *) { sunk.push_back(c); });
  for (int i = 0; i < 3; ++i) { element(ELEM_DIT, 60); }
  g_ms += 400; core_decoder_service(g_ms);
  CHECK(sunk.size() == 1 && g_chars.empty());                 // routed to the sink only
  core_decoder_setTrainingSink(nullptr);
  for (int i = 0; i < 3; ++i) { element(ELEM_DIT, 60); }
  g_ms += 400; core_decoder_service(g_ms);
  CHECK(g_chars.size() == 1);

  // NEG-F10 a million random elements/service calls never crash or break invariants
  reset(); uint32_t s = 7;
  for (int i = 0; i < 1000000; ++i) {
    s = s * 1664525u + 1013904223u; g_ms += (s >> 16) % 700;
    if ((s >> 3) % 3 == 0) core_decoder_addElement((s >> 9) & 1 ? ELEM_DAH : ELEM_DIT, g_ms, (s >> 11) & 1);
    else core_decoder_service(g_ms);
    if ((s >> 20) % 5000 == 0) core_decoder_setEnabled((s >> 5) & 1);
  }
  CHECK(core_decoder_getCharPatternLen() <= MAX_PATTERN_LEN && core_decoder_getWordLen() <= MAX_WORD_LEN);

  std::printf("%s: decoder negative tests, %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_n, g_fail);
  return g_fail ? 1 : 0;
}
