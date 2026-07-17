/*
 * ============================================================================
 * MORPHEUS - Memory Message Player
 * ============================================================================
 * File: core_memory.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Non-blocking playback state machine, serviced every loop() tick like
 * core_keyer/core_decoder. Playback continues across UI screens (design
 * choice - like a real keyer's memory macro), so MORPHEUS.ino services
 * it unconditionally regardless of what the OLED is currently showing.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "core_memory.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "config.h"
#include <string.h>

static const char *MEM_TEXT[MEM_SLOT_COUNT] = {
  "CQ CQ CQ", "599 599", "TU TU", "QRZ?", "AR AR"
};

enum MemPhase { MEM_IDLE, MEM_TONE_ON, MEM_GAP };

static bool          playing         = false;
static uint8_t       playingSlot     = 0;
static const char   *msgCursor       = nullptr;
static char          curPattern[8];
static uint8_t       curPatternLen   = 0;
static uint8_t       curPatternPos   = 0;
static MemPhase      phase           = MEM_IDLE;
static unsigned long  phaseStartMs    = 0;
static unsigned long  phaseDurationMs = 0;

static void stopInternal() {
  core_keyer_diagToneStop();
  playing = false;
  phase = MEM_IDLE;
  msgCursor = nullptr;
}

// Advances msgCursor past spaces, loads the next character's pattern
// into curPattern. Returns false once the message is exhausted.
static bool loadNextChar() {
  for (;;) {
    while (*msgCursor == ' ') msgCursor++;
    if (*msgCursor == '\0') return false;
    char c = *msgCursor++;
    if (core_decoder_lookupPattern(c, curPattern, sizeof(curPattern))) {
      curPatternLen = (uint8_t)strlen(curPattern);
      curPatternPos = 0;
      return true;
    }
    // unsupported character (shouldn't occur with the canned messages) - skip it
  }
}

static void startElementTone(unsigned long now) {
  uint32_t hz = core_keyer_getSidetoneFreq();
  if (!core_keyer_diagToneStart(hz)) {
    stopInternal();   // real keying took priority - abort rather than fight it
    return;
  }
  phase = MEM_TONE_ON;
  phaseStartMs = now;
  unsigned long ditMs = core_keyer_getDitLengthMs();
  bool isDah = (curPattern[curPatternPos] == '-');
  phaseDurationMs = isDah ? ditMs * 3UL : ditMs;
}

static void startGap(unsigned long now, unsigned long durationMs) {
  core_keyer_diagToneStop();
  phase = MEM_GAP;
  phaseStartMs = now;
  phaseDurationMs = durationMs;
}

void core_memory_init() {
  playing = false;
  phase = MEM_IDLE;
}

const char *core_memory_getSlotText(uint8_t slot) {
  if (slot >= MEM_SLOT_COUNT) return "";
  return MEM_TEXT[slot];
}

bool core_memory_play(uint8_t slot) {
  if (slot >= MEM_SLOT_COUNT) return false;
  if (core_keyer_isTxActive()) return false;   // never override a human at the key

  stopInternal();   // interrupt any current playback first

  playingSlot = slot;
  msgCursor = MEM_TEXT[slot];
  playing = true;

  unsigned long now = millis();
  if (!loadNextChar()) { playing = false; return false; }
  startElementTone(now);
  return playing;   // startElementTone may have aborted (real keying) - reflect that
}

void core_memory_stop() { stopInternal(); }
bool core_memory_isPlaying() { return playing; }
uint8_t core_memory_getPlayingSlot() { return playingSlot; }

void core_memory_service(unsigned long now) {
  if (!playing) return;
  if (now - phaseStartMs < phaseDurationMs) return;

  unsigned long ditMs = core_keyer_getDitLengthMs();

  if (phase == MEM_TONE_ON) {
    curPatternPos++;
    if (curPatternPos < curPatternLen) {
      startGap(now, ditMs);   // intra-character element gap = 1 dit
    } else {
      bool nextIsSpace = (*msgCursor == ' ');
      bool nextIsEnd    = (*msgCursor == '\0');
      unsigned long gapMs = (nextIsSpace || nextIsEnd)
        ? (unsigned long)(ditMs * WORD_GAP_MULT)
        : (unsigned long)(ditMs * CHAR_GAP_MULT);
      startGap(now, gapMs);
    }
    return;
  }

  // phase == MEM_GAP
  if (curPatternPos < curPatternLen) {
    startElementTone(now);
    return;
  }

  if (!loadNextChar()) {
    stopInternal();   // message exhausted
    return;
  }
  startElementTone(now);
}