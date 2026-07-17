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
#include "core_keyer.h"

void core_decoder_init();
void core_decoder_service(unsigned long now);
void core_decoder_addElement(ElementType type, unsigned long now);

const char *core_decoder_getWordBuffer();
uint8_t     core_decoder_getWordLen();
const char *core_decoder_getCharPattern();
uint8_t     core_decoder_getCharPatternLen();

// ----------------------------------------------------------------------------
// Runtime enable/disable. When disabled, incoming elements are ignored
// and gap timeouts are not evaluated; in-progress pattern/word state is
// cleared on disable so no stale partial decode lingers on screen.
// ----------------------------------------------------------------------------
void core_decoder_setEnabled(bool enabled);
bool core_decoder_isEnabled();

// ----------------------------------------------------------------------------
// Reverse lookup: character -> Morse pattern, using the SAME table this
// module already maintains for pattern -> character decoding (no second
// alphabet table exists anywhere in the firmware). Case-insensitive.
// Returns false (out untouched beyond a null terminator) for characters
// with no table entry (e.g. space).
// ----------------------------------------------------------------------------
bool core_decoder_lookupPattern(char ch, char *out, size_t outSize);

// Training sink: when set, finalized characters route here instead of
// the normal events_onCharacterComplete()/word-buffer path - keeps the
// Home transcript and BLE notifications clean during a training
// session. Pass nullptr to release (resume normal decoding).
typedef void (*TrainingCharSink)(char decoded, const char *pattern);
void core_decoder_setTrainingSink(TrainingCharSink sink);

void events_onCharacterComplete(char decodedChar, const char *pattern);
void events_onWordComplete(const char *word, unsigned long now);

#endif // MORPHEUS_CORE_DECODER_H