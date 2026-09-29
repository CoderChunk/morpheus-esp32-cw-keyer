// Host stand-in for ESP32 Arduino core's <Preferences.h> (NVS wrapper).
// Only the blob-style API services.cpp/core_profiles.cpp/core_stats.cpp/
// core_games.cpp actually use (begin/getBytes/putBytes/remove) is
// implemented, as a per-process in-memory map - nothing is written to
// disk, so every render run starts from firmware defaults. Not a general
// Preferences shim.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class Preferences {
 public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}

  size_t getBytes(const char *key, void *buf, size_t maxLen) {
    auto it = store_.find(key);
    if (it == store_.end()) return 0;
    size_t n = std::min(maxLen, it->second.size());
    memcpy(buf, it->second.data(), n);
    return n;
  }

  size_t putBytes(const char *key, const void *buf, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    store_[key] = std::vector<uint8_t>(p, p + len);
    return len;
  }

  bool remove(const char *key) {
    store_.erase(key);
    return true;
  }

 private:
  std::map<std::string, std::vector<uint8_t>> store_;
};
