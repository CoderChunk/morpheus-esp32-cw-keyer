/*
 * ============================================================================
 * MORPHEUS - Display Interface
 * ============================================================================
 *
 * File: display.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Defines the public display interface used throughout MORPHEUS.
 *
 * The display system receives information from other modules while
 * remaining entirely separated from their internal implementation.
 * This keeps the user interface flexible and encourages experimentation.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_DISPLAY_H
#define MORPHEUS_DISPLAY_H

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Link-status vocabulary that THIS module understands, for rendering the BLE
// status code / passkey overlay. Deliberately display-owned (not a BLE type)
// so display.h never includes transport.h. transport.cpp pushes updates into
// this representation via display_setTransportStatus() - the one explicitly
// allowed direction of coupling (transport -> display).
// ----------------------------------------------------------------------------
enum DisplayLinkStatus {
  DISPLAY_LINK_ADV,        // advertising, not connected
  DISPLAY_LINK_CONNECTED,  // connected, link not yet secured
  DISPLAY_LINK_PAIRING,    // passkey shown, waiting for entry on the central
  DISPLAY_LINK_PAIR_OK,    // pairing just succeeded - brief confirmation
  DISPLAY_LINK_PAIR_FAIL,  // pairing just failed - brief message
  DISPLAY_LINK_SECURE      // encrypted + authenticated + bonded
};

void display_init();
void display_service(unsigned long now);

// Called from MORPHEUS.ino's events_onWordComplete() fan-out.
void display_appendWord(const char *word);

// Called from transport.cpp's BLE callbacks to push link-state for
// rendering (header status code, passkey overlay, pair-result overlay).
void display_setTransportStatus(DisplayLinkStatus status, uint32_t passkey);

#endif // MORPHEUS_DISPLAY_H