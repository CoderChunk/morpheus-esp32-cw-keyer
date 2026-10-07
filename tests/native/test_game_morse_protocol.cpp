#include <cassert>
#include <cstring>
#include "game_morse_protocol.h"
int main() {
  char out[192];
  assert(encodeGameMorse(out, sizeof(out), "MEMORY", 0xffffffff, 0xffffffff, '?', "..--..", 0xffffffffUL));
  assert(strlen(out) <= 244); // Fits MTU 247 without modifying existing event budgets.
  assert(strstr(out, "\"evt\":\"game_morse\""));
  assert(!encodeGameMorse(out, 20, "COPY", 1, 1, 'A', ".-", 1));
  assert(!encodeGameMorse(out, sizeof(out), "OTHER", 1, 1, 'A', ".-", 1));
  assert(!encodeGameMorse(out, sizeof(out), "COPY", 1, 1, '"', ".-", 1));
  assert(!encodeGameMorse(out, sizeof(out), "COPY", 1, 1, 'A', "x", 1));
}
