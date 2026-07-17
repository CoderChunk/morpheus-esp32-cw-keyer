/*
 * ============================================================================
 * MORPHEUS Standalone UI - Display Layout Constants
 * ============================================================================
 * File: ui_layout.h | Author: Coder Chunk | License: GPLv3
 *
 * Single source of pixel geometry. UI_MARGIN insets every screen (header,
 * lists, menu, editors) equally on all four sides - raise it for OLED
 * modules with a cropped visible window. Default of 2px is the largest
 * value that still lets every existing screen's row stack fit without
 * compression; see UI_ICON_AREA_HEIGHT and Home's layout math in
 * ui_screens.cpp for the specific constraint. Larger margins are fully
 * supported by the constants below (nothing hardcodes 128/64 downstream)
 * but Home's fixed-height row stack doesn't auto-shrink the way the
 * carousel's icon area does - re-check Home's fit before raising past 2.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H
#include <Arduino.h>

// ----------------------------------------------------------------------------
// Physical display
// ----------------------------------------------------------------------------
constexpr uint8_t OLED_WIDTH  = 128;
constexpr uint8_t OLED_HEIGHT = 64;

// ----------------------------------------------------------------------------
// Safe margin - the one knob every layout below respects
// ----------------------------------------------------------------------------
constexpr uint8_t UI_MARGIN = 2;

constexpr uint8_t UI_CONTENT_X0 = UI_MARGIN;
constexpr uint8_t UI_CONTENT_X1 = OLED_WIDTH - UI_MARGIN;
constexpr uint8_t UI_CONTENT_Y0 = UI_MARGIN;
constexpr uint8_t UI_CONTENT_Y1 = OLED_HEIGHT - UI_MARGIN;
constexpr uint8_t UI_CONTENT_WIDTH  = UI_CONTENT_X1 - UI_CONTENT_X0;
constexpr uint8_t UI_CONTENT_HEIGHT = UI_CONTENT_Y1 - UI_CONTENT_Y0;

// ----------------------------------------------------------------------------
// Safe-area border - drawn once by the renderer before screen content,
// for every screen except splash (which animates its own border in).
// ----------------------------------------------------------------------------
constexpr bool    UI_DRAW_BORDER  = false;
constexpr uint8_t UI_BORDER_WIDTH = 1;

// ----------------------------------------------------------------------------
// Shared chrome (header/title bar used on Menu, List, and Editor screens)
// ----------------------------------------------------------------------------
constexpr uint8_t UI_HEADER_HEIGHT   = 10;
constexpr uint8_t UI_HEADER_BASELINE = UI_CONTENT_Y0 + 8;
constexpr uint8_t UI_HEADER_RULE_Y   = UI_CONTENT_Y0 + UI_HEADER_HEIGHT;

// ----------------------------------------------------------------------------
// Main menu carousel - icon area height is DERIVED (content height minus
// header minus title reserve), not hardcoded, so it tracks UI_MARGIN
// automatically. At the default margin=2 this yields 34px for a 32px
// icon: 1px of clearance top and bottom.
// ----------------------------------------------------------------------------
constexpr uint8_t UI_TITLE_RESERVE_HEIGHT = 15;   // bold title line + pad
constexpr uint8_t UI_ICON_AREA_Y = UI_HEADER_RULE_Y + 1;
constexpr uint8_t UI_ICON_AREA_HEIGHT =
    (UI_CONTENT_Y1 > (uint8_t)(UI_TITLE_RESERVE_HEIGHT + UI_ICON_AREA_Y))
      ? (uint8_t)(UI_CONTENT_Y1 - UI_TITLE_RESERVE_HEIGHT - UI_ICON_AREA_Y)
      : (uint8_t)0;
constexpr uint8_t UI_TITLE_Y       = UI_CONTENT_Y1 - 2;
constexpr uint8_t UI_CHEVRON_INSET = 8;

// ----------------------------------------------------------------------------
// Submenu lists
// ----------------------------------------------------------------------------
constexpr uint8_t UI_LIST_ROW_HEIGHT  = 16;
constexpr uint8_t UI_LIST_ROW_Y0      = UI_HEADER_RULE_Y + 2;
constexpr uint8_t UI_LIST_SCROLLBAR_X = UI_CONTENT_X1 - 2;
constexpr uint8_t UI_LIST_TEXT_RIGHT  = UI_CONTENT_X1 - 4;

#endif