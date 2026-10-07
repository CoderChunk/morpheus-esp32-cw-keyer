// FND-01: BLE control commands are queued by the NimBLE host task and run by the main loop.
#include <cstdio>
#include <cstring>
#include "control_cmd_queue.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main() {
  ControlCmdQueue<4, 16> q; char out[16];
  CHECK(!q.pop(out));                                          // empty
  CHECK(q.push("a", 1) && q.push("bb", 2));
  CHECK(q.pop(out) && !std::strcmp(out, "a"));                 // FIFO order
  CHECK(q.pop(out) && !std::strcmp(out, "bb"));
  CHECK(!q.pop(out));
  for (int i = 0; i < 4; ++i) CHECK(q.push("x", 1));           // fill
  CHECK(!q.push("y", 1));                                      // full: newest dropped, nothing overwritten
  for (int i = 0; i < 4; ++i) CHECK(q.pop(out) && !std::strcmp(out, "x"));
  for (int round = 0; round < 10; ++round) {                   // wrap-around many times
    char c = (char)('a' + round);
    CHECK(q.push(&c, 1) && q.push(&c, 1) && q.push(&c, 1));
    for (int i = 0; i < 3; ++i) CHECK(q.pop(out) && out[0] == c && out[1] == '\0');
  }
  CHECK(!q.push("", 0));                                       // empty command rejected
  CHECK(!q.push("0123456789abcdef", 16));                      // too long for the slot (no room for NUL)
  CHECK(q.push("0123456789abcde", 15) && q.pop(out) && std::strlen(out) == 15);
  CHECK(q.push("a", 1)); q.clear(); CHECK(!q.pop(out));        // clear on disconnect
  CHECK(q.push("z", 1) && q.pop(out) && !std::strcmp(out, "z"));
  std::printf("%s: control command queue\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
