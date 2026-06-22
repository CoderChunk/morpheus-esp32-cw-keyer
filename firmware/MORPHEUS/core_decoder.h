/*
 * ============================================================================
 * MORPHEUS - Decoder Interface
 * ============================================================================
 *
 * File: core_decoder.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * This header defines the public interface of the Morse decoder.
 *
 * The decoder listens to completed keying elements and transforms timing
 * information into characters and words. The interface intentionally
 * remains small and stable, allowing the decoding engine to evolve without
 * affecting the rest of the firmware.
 *
 * Contributors can improve decoding algorithms freely while preserving
 * compatibility with the existing architecture.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_CORE_DECODER_H
#define MORPHEUS_CORE_DECODER_H

#include <Arduino.h>
#include "core_keyer.h"   // ElementType - the decoder consumes keyer-produced
                           // elements; this is the decoder's one allowed
                           // dependency, in this direction only.

// ----------------------------------------------------------------------------
// Lifecycle - called only from MORPHEUS.ino
// ----------------------------------------------------------------------------
void core_decoder_init();
void core_decoder_service(unsigned long now);

// ----------------------------------------------------------------------------
// Feed path - called from MORPHEUS.ino's events_onKeyUp() fan-out, once per
// completed keying element. This is the ONLY way elements reach the
// decoder; core_keyer never calls this directly, so core_keyer stays fully
// ignorant of the decoder's existence.
// ----------------------------------------------------------------------------
void core_decoder_addElement(ElementType type, unsigned long now);

// ----------------------------------------------------------------------------
// Public getters - read-only window into decode-in-progress state, for
// display's live-word/pattern footer and services' STATUS heartbeat. Do
// not expose the lookup table or any future Koch/adaptive-timing internals
// here - keep this surface to "what's currently in progress."
// ----------------------------------------------------------------------------
const char *core_decoder_getWordBuffer();
uint8_t     core_decoder_getWordLen();
const char *core_decoder_getCharPattern();
uint8_t     core_decoder_getCharPatternLen();

// ----------------------------------------------------------------------------
// Event hooks - DECLARED here (the decoder is the producer) but DEFINED in
// MORPHEUS.ino.
// ----------------------------------------------------------------------------
void events_onCharacterComplete(char decodedChar, const char *pattern);
void events_onWordComplete(const char *word, unsigned long now);

#endif // MORPHEUS_CORE_DECODER_H