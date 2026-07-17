/*
 * ============================================================================
 * MORPHEUS - Memory Message Player
 * ============================================================================
 * File: core_memory.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Refactored to use core_morseplayer.h's shared playback engine instead
 * of its own duplicated state machine. core_memory.h's public API is
 * unchanged - nothing that calls into this module needs to change.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_memory.h"
#include "core_morseplayer.h"
#include "core_keyer.h"

static const char *MEM_TEXT[MEM_SLOT_COUNT] = {
  "CQ CQ CQ", "599 599", "TU TU", "QRZ?", "AR AR"
};

static MorsePlayer player;
static bool playing = false;
static uint8_t playingSlot = 0;

void core_memory_init() {
  core_morseplayer_init(player);
  playing = false;
}

const char *core_memory_getSlotText(uint8_t slot) {
  return (slot < MEM_SLOT_COUNT) ? MEM_TEXT[slot] : "";
}

bool core_memory_play(uint8_t slot) {
  if (slot >= MEM_SLOT_COUNT) return false;
  playingSlot = slot;
  unsigned long ditMs = core_keyer_getDitLengthMs();
  playing = core_morseplayer_start(player, MEM_TEXT[slot], ditMs, ditMs);
  return playing;
}

void core_memory_stop() {
  core_morseplayer_stop(player);
  playing = false;
}

bool core_memory_isPlaying() { return playing; }
uint8_t core_memory_getPlayingSlot() { return playingSlot; }

void core_memory_service(unsigned long now) {
  if (!playing) return;
  core_morseplayer_service(player, now);
  if (!core_morseplayer_isActive(player)) playing = false;
}