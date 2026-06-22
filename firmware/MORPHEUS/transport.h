/*
 * ============================================================================
 * MORPHEUS - Transport Interface
 * ============================================================================
 *
 * File: transport.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Defines the communication interface used by the transport subsystem.
 *
 * The transport layer provides a clean boundary between the firmware core
 * and external communication technologies, allowing future transport
 * methods to be added without affecting the rest of the system.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_TRANSPORT_H
#define MORPHEUS_TRANSPORT_H

#include <Arduino.h>
#include "core_keyer.h"   // OperatingMode - needed for the word-event
                          // payload type. transport never needs
                          // core_decoder - it only ever sees completed
                          // words via transport_notifyWordCompleted().

void transport_init();
void transport_service(unsigned long now);

// Called from MORPHEUS.ino's events_onWordComplete() fan-out.
void transport_notifyWordCompleted(const char *word, int wpm, OperatingMode mode, unsigned long now);

#endif // MORPHEUS_TRANSPORT_H