// FND-03: a virtual key whose client is gone must be released. NEG-F11.
#include <cstdio>
#include "virtual_key_guard.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main() {
  CHECK(virtualKeyOrphaned(true, false));    // key down, link gone  -> release
  CHECK(!virtualKeyOrphaned(true, true));    // key down, link up    -> a client holds it
  CHECK(!virtualKeyOrphaned(false, false));  // key up, no link      -> nothing to release
  CHECK(!virtualKeyOrphaned(false, true));   // key up, link up
  std::printf("%s: virtual key guard\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
