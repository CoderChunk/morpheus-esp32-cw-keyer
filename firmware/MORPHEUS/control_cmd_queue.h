#ifndef MORPHEUS_CONTROL_CMD_QUEUE_H
#define MORPHEUS_CONTROL_CMD_QUEUE_H

#include <stddef.h>
#include <string.h>

// Fixed-size FIFO that hands BLE control commands from the NimBLE host task to the main
// loop. The GATT write callback runs on the host task, whose stack is small: running the
// command handler there (96-byte buffer plus JSON parsing, snprintf and notify frames)
// overflowed it and crashed the device a few hundred ms after connecting (FND-01, "stack
// canary watchpoint triggered (nimble_host)"). The callback now only copies the command
// in here; the main loop runs it. The caller provides any locking (one producer, one
// consumer); this type is plain data so it can be tested on the host.
template <size_t SLOTS, size_t CAP>
struct ControlCmdQueue {
  char slot[SLOTS][CAP];
  size_t head = 0, count = 0;

  // False (command dropped) if the queue is full or the command does not fit with its
  // terminator.
  bool push(const char *data, size_t len) {
    if (count == SLOTS || len == 0 || len >= CAP) return false;
    size_t i = (head + count) % SLOTS;
    memcpy(slot[i], data, len);
    slot[i][len] = '\0';
    count++;
    return true;
  }
  // Copies the oldest command into out (CAP bytes); False if empty.
  bool pop(char *out) {
    if (count == 0) return false;
    memcpy(out, slot[head], CAP);
    head = (head + 1) % SLOTS;
    count--;
    return true;
  }
  void clear() { head = 0; count = 0; }
};

#endif
