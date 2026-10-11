#ifndef MORPHEUS_TRUSTED_LIST_H
#define MORPHEUS_TRUSTED_LIST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The trusted-address list (NVS) and NimBLE's bond store (the keys) are kept separately.
// NimBLE evicts its oldest bond when a new device pairs and its store is full, so the
// list could keep an address with no keys behind it. That entry used a slot and turned
// new devices away as "list full" (FND-05, real hardware: list 3/3, bonds 2). This keeps
// only the entries hasBond(addr) accepts, in order, and returns the new count.
template <size_t N, size_t W, typename HasBond>
uint8_t pruneUnbondedTrusted(char (&list)[N][W], uint8_t count, HasBond hasBond) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count && i < N; i++) {
    if (!hasBond(list[i])) continue;
    if (kept != i) memcpy(list[kept], list[i], W);
    kept++;
  }
  return kept;
}

#endif
