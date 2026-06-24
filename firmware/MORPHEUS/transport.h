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

// Clears the BLE bond: disconnects any currently connected peer, wipes
// NimBLE's own internal bond store, clears this app's trusted-device
// allowlist, and reopens advertising to a new pairing. Never touches
// operator settings (services.cpp's NVS namespace) - that separation is
// load-bearing, not incidental. Called from MORPHEUS.ino, either after a
// boot-time held button (see PIN_BOND_RESET) or via the temporary
// FEATURE_DEBUG_SERIAL_COMMANDS "RESET BOND" command.
void transport_resetBond();

#endif // MORPHEUS_TRANSPORT_H