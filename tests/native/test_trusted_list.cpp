// FND-05: trusted addresses without a bond must not hold a slot.
#include <cstdio>
#include <cstring>
#include "trusted_list.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static const char *bonded[] = {"04:34:f6:44:4c:67", "4c:6f:9c:51:4c:c4"};
static bool hasBond(const char *a) {
  for (const char *b : bonded) if (!std::strcmp(a, b)) return true;
  return false;
}
static void set(char (&l)[3][24], const char *a, const char *b, const char *c) {
  std::strcpy(l[0], a); std::strcpy(l[1], b); std::strcpy(l[2], c);
}
int main() {
  char l[3][24];
  set(l, "2c:3b:70:6b:8f:d4", "04:34:f6:44:4c:67", "4c:6f:9c:51:4c:c4");  // the hardware case
  CHECK(pruneUnbondedTrusted(l, 3, hasBond) == 2);
  CHECK(!std::strcmp(l[0], "04:34:f6:44:4c:67") && !std::strcmp(l[1], "4c:6f:9c:51:4c:c4"));
  set(l, "04:34:f6:44:4c:67", "4c:6f:9c:51:4c:c4", "x");
  CHECK(pruneUnbondedTrusted(l, 2, hasBond) == 2);                         // all bonded: unchanged
  CHECK(!std::strcmp(l[0], "04:34:f6:44:4c:67") && !std::strcmp(l[1], "4c:6f:9c:51:4c:c4"));
  set(l, "a", "b", "c");
  CHECK(pruneUnbondedTrusted(l, 3, hasBond) == 0);                         // no bonds left
  CHECK(pruneUnbondedTrusted(l, 0, hasBond) == 0);                         // empty list
  set(l, "4c:6f:9c:51:4c:c4", "a", "04:34:f6:44:4c:67");
  CHECK(pruneUnbondedTrusted(l, 3, hasBond) == 2);                         // gap in the middle closes
  CHECK(!std::strcmp(l[0], "4c:6f:9c:51:4c:c4") && !std::strcmp(l[1], "04:34:f6:44:4c:67"));
  CHECK(pruneUnbondedTrusted(l, 200, hasBond) <= 3);                       // corrupt count stays in bounds
  std::printf("%s: trusted list pruning\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
