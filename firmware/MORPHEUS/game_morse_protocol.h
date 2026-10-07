#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
// Bounded wire encoder. Morse decoding remains entirely in core_decoder.
inline bool encodeGameMorse(char *out, size_t cap, const char *game,
    uint32_t run, uint32_t seq, char ch, const char *pattern, unsigned long now) {
  if (!game || !pattern || strlen(pattern) > 8 || ch < 32 || ch > 126 || ch == '"' || ch == '\\') return false;
  for (const char *p = pattern; *p; ++p) if (*p != '.' && *p != '-') return false;
  if (strcmp(game, "COPY") && strcmp(game, "MEMORY") && strcmp(game, "SPEED")) return false;
  int n = snprintf(out, cap, "{\"evt\":\"game_morse\",\"game\":\"%s\",\"run\":%lu,\"seq\":%lu,\"char\":\"%c\",\"pattern\":\"%s\",\"timestamp\":%lu}",
      game, (unsigned long)run, (unsigned long)seq, ch, pattern, now);
  return n > 0 && (size_t)n < cap;
}
