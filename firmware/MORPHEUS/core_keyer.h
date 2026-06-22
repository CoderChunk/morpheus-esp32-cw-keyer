/*
 * ============================================================================
 * MORPHEUS - Keyer Interface
 * ============================================================================
 *
 * File: core_keyer.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * This file defines the public API of the keying engine.
 *
 * Operating modes, key states, timing information, and event hooks are
 * exposed here so that the rest of MORPHEUS can observe keying activity
 * without becoming coupled to the implementation.
 *
 * The keyer serves as the foundation of the entire firmware architecture.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_CORE_KEYER_H
#define MORPHEUS_CORE_KEYER_H

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Shared types - produced by the keyer's FSMs.
//   - ElementType crosses into core_decoder (decoder consumes elements).
//   - OperatingMode crosses into display/transport (status line / BLE
//     payload).
// Both stay defined here, in the producer's header. core_keyer is the
// stable foundation other modules read from - never the reverse.
// ----------------------------------------------------------------------------
enum OperatingMode { MODE_STRAIGHT, MODE_PADDLE };
enum ElementType   { ELEM_NONE, ELEM_DIT, ELEM_DAH };
enum KeyState      { STATE_IDLE, STATE_DIT, STATE_DAH };

// ----------------------------------------------------------------------------
// Lifecycle - called only from MORPHEUS.ino
// ----------------------------------------------------------------------------
void core_keyer_init();
void core_keyer_service(unsigned long now);

// ----------------------------------------------------------------------------
// Public getters - read-only window into keying state.
// ----------------------------------------------------------------------------
OperatingMode core_keyer_getMode();
int           core_keyer_getWpm();
unsigned long core_keyer_getDitLengthMs();
bool          core_keyer_isTxActive();
KeyState      core_keyer_getKeyState(unsigned long now);

// ----------------------------------------------------------------------------
// Public setter - the one write path into the keyer's timing state from
// outside. Used by services.cpp for potentiometer-driven WPM. Clamped
// internally to [WPM_MIN, WPM_MAX].
// ----------------------------------------------------------------------------
void core_keyer_setWpm(int wpm);

// ----------------------------------------------------------------------------
// Event hooks - DECLARED here (the keyer is the producer) but DEFINED in
// MORPHEUS.ino. core_keyer.cpp calls these with zero knowledge of the
// decoder, BLE, OLED, or Serial - core_keyer does not even know
// core_decoder exists. This is what lets the decoder evolve (Koch
// training, Farnsworth spacing, adaptive timing, etc.) without ever
// touching this module.
// ----------------------------------------------------------------------------
void events_onKeyDown(unsigned long now);
void events_onKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now);

#endif // MORPHEUS_CORE_KEYER_H