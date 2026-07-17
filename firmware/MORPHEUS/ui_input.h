/*
 * ============================================================================
 * MORPHEUS Standalone UI - Input Driver Interface
 * ============================================================================
 *
 * File: ui_input.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * The only module that touches input GPIOs. Converts raw encoder/button
 * activity into the logical event vocabulary defined in UI_Spec §1.
 * The state machine never reads a pin.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <Arduino.h>

// Logical events [UI_Spec §1]. EV_TICK is not an enum member - the state
// machine receives time via its service() call instead.
enum UiEventType {
  UI_EV_ROTATE,   // detents: signed detent count (+CW / -CCW)
  UI_EV_SELECT,   // encoder push OR Confirm, short press (full aliases)
  UI_EV_BACK,     // Back, short press
  UI_EV_HOME      // Back, long press (fires at threshold, release swallowed)
};

struct UiEvent {
  UiEventType type;
  int8_t      detents;   // meaningful for UI_EV_ROTATE only
};

void ui_input_init();
void ui_input_service(unsigned long now);

// Drain one event from the queue. Returns false when empty.
bool ui_input_poll(UiEvent &ev);

#endif // UI_INPUT_H