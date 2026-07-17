/*
 * ============================================================================
 * MORPHEUS - Memory Message Player
 * ============================================================================
 *
 * File: core_memory.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Plays fixed canned CW messages through the real sidetone hardware,
 * reusing core_keyer's gated diagnostic tone path so playback always
 * yields the instant a human touches the key/paddle. Timing tracks the
 * live WPM setting via core_keyer_getDitLengthMs(); character-to-pattern
 * encoding reuses core_decoder's existing Morse table via
 * core_decoder_lookupPattern() - no second alphabet table exists.
 *
 * Fixed canned messages only - free-text entry would need a dedicated
 * character-wheel text editor, out of scope for this module.
 *
 * Slot count/order here must match MEM_TEXT[] in core_memory.cpp, and
 * ui_menu.cpp's MENU_MEM row count/paramId values must match this too
 * (ui_menu.cpp cannot include this header - see ui_state.cpp's frozen
 * "no direct core includes" rule - so the two are kept in sync manually,
 * documented at both sites).
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_CORE_MEMORY_H
#define MORPHEUS_CORE_MEMORY_H

#include <Arduino.h>

static const uint8_t MEM_SLOT_COUNT = 5;

void core_memory_init();
void core_memory_service(unsigned long now);

const char *core_memory_getSlotText(uint8_t slot);

// Interrupts any current playback and starts the given slot (0-based).
// Refuses (returns false) if the slot is invalid or real keying is
// currently active - playback never overrides a human at the key.
bool core_memory_play(uint8_t slot);

void    core_memory_stop();
bool    core_memory_isPlaying();
uint8_t core_memory_getPlayingSlot();   // valid only while isPlaying()

#endif // MORPHEUS_CORE_MEMORY_H