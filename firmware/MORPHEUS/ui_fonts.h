/*
 * ============================================================================
 * MORPHEUS Standalone UI - Font Set (consolidated)
 * ============================================================================
 * File: ui_fonts.h | Author: Coder Chunk | License: GPLv3
 *
 * Three fonts, replacing the earlier ProFont mix. 6x10 and 7x14B are the
 * same faces already shipping in MORPHEUS v1.2.2's display.cpp - reusing
 * them here is a deliberate consistency choice, not just convenience.
 *
 *   UI_FONT_SMALL - regular, 6x10: chrome, status, footers, hints
 *   UI_FONT_BOLD  - bold, 7x14B:   titles, menu items, list rows, labels
 *   UI_FONT_HERO  - bold, logisoso24: hero numerals (editor values) and
 *                   the splash logo - anything meant to read as "the
 *                   important thing on this screen"
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#ifndef UI_FONTS_H
#define UI_FONTS_H
#include <U8g2lib.h>

#define UI_FONT_SMALL u8g2_font_6x10_tr
#define UI_FONT_BOLD  u8g2_font_7x14B_tr
#define UI_FONT_HERO  u8g2_font_logisoso24_tr

#endif