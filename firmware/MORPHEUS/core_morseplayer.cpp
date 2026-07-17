/*
 * ============================================================================
 * MORPHEUS - Shared Morse Playback Engine
 * ============================================================================
 * File: core_morseplayer.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Extracted from core_memory.cpp's original playback state machine so it
 * can be reused, unmodified in behavior, by the Training module.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_morseplayer.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "config.h"
#include <string.h>

void core_morseplayer_init(MorsePlayer &p) {
  p.cursor = nullptr;
  p.curPatternLen = 0;
  p.curPatternPos = 0;
  p.phase = MorsePlayer::P_IDLE;
  p.phaseStartMs = 0;
  p.phaseDurationMs = 0;
  p.active = false;
  p.elementDitMs = 80;
  p.gapDitMs = 80;
}

static bool loadNextChar(MorsePlayer &p) {
  for (;;) {
    while (*p.cursor == ' ') p.cursor++;
    if (*p.cursor == '\0') return false;
    char c = *p.cursor++;
    if (core_decoder_lookupPattern(c, p.curPattern, sizeof(p.curPattern))) {
      p.curPatternLen = (uint8_t)strlen(p.curPattern);
      p.curPatternPos = 0;
      return true;
    }
  }
}

static void startElementTone(MorsePlayer &p, unsigned long now) {
  if (!core_keyer_diagToneStart(core_keyer_getSidetoneFreq())) {
    p.active = false;   // real keying took priority
    return;
  }
  p.phase = MorsePlayer::P_TONE_ON;
  p.phaseStartMs = now;
  bool isDah = (p.curPattern[p.curPatternPos] == '-');
  p.phaseDurationMs = isDah ? p.elementDitMs * 3UL : p.elementDitMs;
}

static void startGap(MorsePlayer &p, unsigned long now, unsigned long durationMs) {
  core_keyer_diagToneStop();
  p.phase = MorsePlayer::P_GAP;
  p.phaseStartMs = now;
  p.phaseDurationMs = durationMs;
}

bool core_morseplayer_start(MorsePlayer &p, const char *text,
                             unsigned long elementDitMs, unsigned long gapDitMs) {
  if (core_keyer_isTxActive()) return false;
  core_morseplayer_stop(p);

  p.cursor = text;
  p.elementDitMs = elementDitMs;
  p.gapDitMs = gapDitMs;
  p.active = true;

  if (!loadNextChar(p)) { p.active = false; return false; }
  startElementTone(p, millis());
  return p.active;
}

void core_morseplayer_stop(MorsePlayer &p) {
  core_keyer_diagToneStop();
  p.active = false;
  p.phase = MorsePlayer::P_IDLE;
  p.cursor = nullptr;
}

bool core_morseplayer_isActive(const MorsePlayer &p) { return p.active; }

void core_morseplayer_service(MorsePlayer &p, unsigned long now) {
  if (!p.active) return;
  if (now - p.phaseStartMs < p.phaseDurationMs) return;

  if (p.phase == MorsePlayer::P_TONE_ON) {
    p.curPatternPos++;
    if (p.curPatternPos < p.curPatternLen) {
      startGap(p, now, p.gapDitMs);
    } else {
      bool nextIsSpace = (*p.cursor == ' ');
      bool nextIsEnd    = (*p.cursor == '\0');
      unsigned long gapMs = (nextIsSpace || nextIsEnd)
        ? (unsigned long)(p.gapDitMs * WORD_GAP_MULT)
        : (unsigned long)(p.gapDitMs * CHAR_GAP_MULT);
      startGap(p, now, gapMs);
    }
    return;
  }

  if (p.curPatternPos < p.curPatternLen) { startElementTone(p, now); return; }
  if (!loadNextChar(p)) { core_morseplayer_stop(p); return; }
  startElementTone(p, now);
}