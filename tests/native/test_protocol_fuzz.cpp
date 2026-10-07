// Robustness test of the BLE wire parsers/encoders (non-functional).
// Deterministic pseudo-random inputs (fixed seeds) against the REAL headers.
// Meant to run under AddressSanitizer + UBSan (tests/native/run_sanitized.sh):
// every output buffer is heap-allocated at exactly `cap` bytes, so any overrun
// or read past NUL is a hard failure, not a silent pass.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "keyer_settings_protocol.h"
#include "keyer_metrics.h"
#include "game_morse_protocol.h"

static uint64_t s = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return uint32_t(s >> 11); }
static int g_fail = 0, g_cases = 0;
#define CHECK(c) do { ++g_cases; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// Output must be one NUL-terminated, balanced JSON-looking object inside cap.
static bool wellFormed(const char* out, size_t cap) {
  size_t n = strnlen(out, cap);
  if (n >= cap || n < 2 || out[0] != '{' || out[n - 1] != '}') return false;
  int quotes = 0;
  for (size_t i = 0; i < n; ++i) { if ((unsigned char)out[i] < 0x20) return false; if (out[i] == '"') ++quotes; }
  return quotes % 2 == 0;
}

int main() {
  // A. keyerProtocolValue: exact on well-formed input, never accepts junk.
  for (int v = 0; v <= 9999; ++v) {
    char buf[40]; snprintf(buf, sizeof buf, "{\"value\":%d}", v);
    int out = -1; CHECK(keyerProtocolValue(buf, &out) && out == v);
  }
  for (int v : {10000, 12345, 99999, 123456789}) {
    char buf[40]; snprintf(buf, sizeof buf, "{\"value\":%d}", v);
    int out = -7; CHECK(!keyerProtocolValue(buf, &out) && out == -7);
  }
  static const char* tok[] = {"{", "}", "\"value\"", ":", ",", " ", "\t", "-", "+", ".", "e", "0", "1", "9", "99999", "true", "null",
                              "\"", "\\", "[", "]", "\"field\"", "\"wpm\"", "\xff", "\x01"};
  for (int i = 0; i < 300000; ++i) {
    std::string in; int parts = rnd() % 12;
    for (int k = 0; k < parts; ++k) in += tok[rnd() % (sizeof tok / sizeof *tok)];
    int out = -7; bool ok = keyerProtocolValue(in.c_str(), &out);
    CHECK(!ok || (out >= 0 && out <= 9999));
    CHECK(ok || out == -7);                // never writes on failure
  }
  // B. keyerProtocolValid: whole range against the documented table.
  struct R { const char* f; int lo, hi; };
  for (R r : {R{"wpm", 5, 40}, R{"tone", 200, 2000}, R{"volume", 0, 100}, R{"weight", 30, 70}, R{"mode", 0, 1}, R{"iambic", 0, 1},
              R{"reversed", 0, 1}, R{"sidetone", 0, 1}})
    for (int v = -100; v <= 3000; ++v) CHECK(keyerProtocolValid(r.f, v) == (v >= r.lo && v <= r.hi));
  for (const char* f : {"", "WPM", "wpm ", "unknown", "mode\n"}) for (int v : {0, 1, 20}) CHECK(!keyerProtocolValid(f, v));
  // C. KeyerMetrics id validation + encoding at every capacity, extreme values.
  for (int i = 0; i < 100000; ++i) {
    std::string id; int len = rnd() % 40;
    for (int k = 0; k < len; ++k) id += char(rnd() % 3 == 0 ? rnd() % 256 : 'a' + rnd() % 26);
    bool ref = !id.empty() && id.size() <= 24;
    for (char c : id) ref = ref && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    if (id.find('\0') != std::string::npos) continue;
    CHECK(KeyerMetrics::validId(id.c_str()) == ref);
  }
  KeyerMetrics m;
  m.keyDown(0xFFFFFF00u, true); m.element(false, 0xFFFFFFFFu, 0xFFFFFFFFu); m.character();
  m.keyDown(0xFFFFFFFFu, true); m.element(true, 4294967295u, 0xFFFFFFFFu);
  for (size_t cap = 0; cap <= 220; ++cap) {
    char* out = new char[cap ? cap : 1];
    bool ok = m.encode(out, cap, "AbC123", 0xFFFFFFFFu);
    if (ok) CHECK(wellFormed(out, cap));
    CHECK(!m.encode(out, cap, "bad id!", 0) && !m.encode(out, cap, "", 0));
    delete[] out;
  }
  // D. encodeGameMorse: hostile characters/patterns/games at every capacity.
  const char* games[] = {"COPY", "MEMORY", "SPEED", "copy", "", "COPY\"", nullptr};
  const char* pats[] = {"", ".", "-.-.", "........", ".........", ".x.", "\"", nullptr};
  for (int i = 0; i < 20000; ++i) {
    size_t cap = rnd() % 200; char* out = new char[cap ? cap : 1];
    const char* g = games[rnd() % 7]; const char* p = pats[rnd() % 8]; char ch = char(rnd() % 256);
    bool ok = encodeGameMorse(out, cap, g, rnd(), rnd(), ch, p, rnd());
    if (ok) {
      CHECK(wellFormed(out, cap));
      CHECK(g && (!strcmp(g, "COPY") || !strcmp(g, "MEMORY") || !strcmp(g, "SPEED")));
      CHECK(ch >= 32 && ch <= 126 && ch != '"' && ch != '\\');
    }
    delete[] out;
  }
  std::printf("%s: protocol fuzz, %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_cases, g_fail);
  return g_fail ? 1 : 0;
}
