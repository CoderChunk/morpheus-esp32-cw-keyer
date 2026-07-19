/*
 * ============================================================================
 * MORPHEUS Standalone UI - Renderer Interface
 * ============================================================================
 *
 * File: ui_renderer.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Owns the display device and the redraw policy. Draws only when the
 * state machine reports a change (dirty flag) - the SH1106 is never
 * refreshed redundantly, keeping the loop responsive for input.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef UI_RENDERER_H
#define UI_RENDERER_H
#include <Arduino.h>

void ui_renderer_init();
void ui_renderer_service(unsigned long now);

// Real display contrast control - u8g2's own setContrast(), not a
// MORPHEUS core value. Range enforced by ui_state's PARAM_CONTRAST
// editor (UI_CONTRAST_MIN/MAX in ui_config.h).
uint8_t ui_renderer_getContrast();
void    ui_renderer_setContrast(uint8_t value);

void ui_renderer_setSleeping(bool sleeping);
bool ui_renderer_isSleeping();

void ui_renderer_setInverted(bool inverted);
bool ui_renderer_getInverted();

#endif // UI_RENDERER_H