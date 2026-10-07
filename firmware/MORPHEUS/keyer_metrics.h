#ifndef MORPHEUS_KEYER_METRICS_H
#define MORPHEUS_KEYER_METRICS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Observes the existing decoder/keyer events. Does not classify or decode CW.
// Samples are the latest actual completed element or inter-character silence.
class KeyerMetrics {
  struct Sample {
    uint32_t duration = 0, at = 0;
    bool valid = false;
    void set(uint32_t value, uint32_t now) { duration = value; at = now; valid = value > 0; }
    void json(char *out, size_t cap, uint32_t now) const {
      if (valid && uint32_t(now - at) <= 30000U) snprintf(out, cap, "%lu", (unsigned long)duration);
      else snprintf(out, cap, "null");
    }
  };
  Sample dit, dah, gap;
  uint32_t lastEnd = 0, sequence = 0;
  bool haveEnd = false, characterEnded = false, wordEnded = false;
  bool sourceKnown = false, virtualSource = false, down = false;
public:
  void reset() {
    dit = Sample(); dah = Sample(); gap = Sample();
    haveEnd = characterEnded = wordEnded = sourceKnown = down = false;
    ++sequence;
  }
  void keyDown(uint32_t now, bool fromVirtual = false) {
    if (sourceKnown && fromVirtual != virtualSource) reset();
    sourceKnown = true; virtualSource = fromVirtual;
    if (haveEnd && characterEnded && !wordEnded) gap.set(uint32_t(now - lastEnd), now);
    characterEnded = wordEnded = false;
    down = true;
  }
  void element(bool isDah, uint32_t duration, uint32_t now) {
    // A reset during a held key cannot create a partial/misleading sample.
    if (!down) return;
    (isDah ? dah : dit).set(duration, now);
    lastEnd = now; haveEnd = true; down = false; ++sequence;
  }
  void character() { characterEnded = true; }
  void word() { wordEnded = true; }
  static bool validId(const char *id) {
    const size_t len = strlen(id);
    if (len == 0 || len > 24) return false;
    for (size_t i = 0; i < len; ++i) {
      const char c = id[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
  }
  bool encode(char *out, size_t cap, const char *id, uint32_t now) const {
    if (!validId(id)) return false;
    char d[12], h[12], g[12];
    dit.json(d, sizeof(d), now); dah.json(h, sizeof(h), now); gap.json(g, sizeof(g), now);
    const int n = snprintf(out, cap,
      "{\"evt\":\"keyer_metrics\",\"id\":\"%s\",\"ts\":%lu,\"seq\":%lu,\"ditMs\":%s,\"dahMs\":%s,\"gapMs\":%s,\"virtual\":%s}",
      id, (unsigned long)now, (unsigned long)sequence, d, h, g, virtualSource ? "true" : "false");
    return n > 0 && size_t(n) < cap;
  }
};
#endif
