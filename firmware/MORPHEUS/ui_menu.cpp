/*
 * ============================================================================
 * MORPHEUS OLED UI - Menu Tree Data
 * ============================================================================
 * File: ui_menu.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision: MEMORY MSGS is now a real submenu of 5 fixed canned
 * messages (NODE_TRIGGER rows - immediate, non-destructive, no confirm
 * dialog). DECODER is now a real toggle (PARAM_DECODER_EN). LIVE MONITOR
 * and TUNE are now real dedicated screens (NODE_MONITOR / NODE_TUNE).
 *
 * MENU_MEM's row count/order must match MEM_SLOT_COUNT/MEM_TEXT[] in
 * core_memory.cpp - this file cannot include that header (ui_state.cpp's
 * frozen "no direct core includes" rule extends to ui_menu.cpp by the
 * same reasoning), so the two are kept in sync manually.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "ui_menu.h"

#define N_SUB(lbl, tbl)  { lbl, NODE_SUBMENU, tbl, (uint8_t)(sizeof(tbl)/sizeof(UiMenuNode)), 0 }
#define N_ROW(lbl, typ)  { lbl, typ, nullptr, 0, 0 }
#define N_VAL(lbl, pid)  { lbl, NODE_VALUE, nullptr, 0, pid }
#define N_TGL(lbl, pid)  { lbl, NODE_TOGGLE, nullptr, 0, pid }
#define N_INF(lbl, iid)  { lbl, NODE_INFO, nullptr, 0, iid }
#define N_DIAG(lbl, did) { lbl, NODE_DIAG, nullptr, 0, did }
#define N_ACT(lbl, aid)  { lbl, NODE_ACTION, nullptr, 0, aid }
#define N_MON(lbl)       { lbl, NODE_MONITOR, nullptr, 0, 0 }
#define N_TUNE(lbl)      { lbl, NODE_TUNE, nullptr, 0, 0 }
#define N_TRIG(lbl, s1)  { lbl, NODE_TRIGGER, nullptr, 0, s1 }   // s1 = 1-based slot

// --- Memory Msgs: 5 fixed canned messages, must match core_memory.cpp ---------
static const UiMenuNode MENU_MEM[] = {
  N_TRIG("CQ CQ CQ", 1),
  N_TRIG("599 599",  2),
  N_TRIG("TU TU",    3),
  N_TRIG("QRZ?",     4),
  N_TRIG("AR AR",    5),
};

static const UiMenuNode MENU_KEYER[] = {
  N_TGL("KEYER MODE",   PARAM_MODE),
  N_SUB("MEMORY MSGS",  MENU_MEM),
  N_TGL("DECODER",      PARAM_DECODER_EN),
  N_MON("LIVE MONITOR"),
  N_TUNE("TUNE"),
};

static const UiMenuNode MENU_TRAINING[] = {
  N_ROW("KOCH METHOD",  NODE_STUB),
  N_ROW("FARNSWORTH",   NODE_STUB),
  N_ROW("CHARACTERS",   NODE_STUB),
  N_ROW("WORDS",        NODE_STUB),
  N_ROW("CALLSIGNS",    NODE_STUB),
  N_ROW("ADAPTIVE",     NODE_STUB),
  N_ROW("EXAM MODE",    NODE_STUB),
};

static const UiMenuNode MENU_STATS[] = {
  N_ROW("SESSION",      NODE_INFO),
  N_ROW("LIFETIME",     NODE_INFO),
  N_ROW("PROGRESS",     NODE_STUB),
  N_ROW("ACCURACY",     NODE_STUB),
  N_ROW("SPEED HIST",   NODE_STUB),
};

static const UiMenuNode MENU_BLUETOOTH[] = {
  N_INF("STATUS",       INFO_BLE_STATUS),
  N_ROW("PAIRING",      NODE_STUB),
  N_ACT("BOND RESET",   ACTION_BOND_RESET),
};
static const UiMenuNode MENU_CONNECTIVITY[] = {
  N_SUB("BLUETOOTH",    MENU_BLUETOOTH),
  N_ROW("WI-FI",        NODE_STUB),
  N_ROW("KEYBOARD",     NODE_STUB),
  N_ROW("FIRMWARE UPD", NODE_STUB),
  N_INF("DEVICE INFO",  INFO_DEVICE_INFO),
};

static const UiMenuNode MENU_PROFILES[] = {
  N_ROW("DEFAULT",      NODE_STUB),
  N_ROW("PORTABLE",     NODE_STUB),
  N_ROW("CONTEST",      NODE_STUB),
  N_ROW("PRACTICE",     NODE_STUB),
  N_ROW("SAVE CURRENT", NODE_ACTION),
};

static const UiMenuNode MENU_SET_KEYER[] = {
  N_VAL("WPM",          PARAM_WPM),
  N_TGL("PADDLE REV",   PARAM_PADDLE_REV),
  N_ROW("IAMBIC MODE",  NODE_STUB),
  N_ROW("WEIGHTING",    NODE_STUB),
};
static const UiMenuNode MENU_SET_AUDIO[] = {
  N_VAL("TONE",         PARAM_TONE),
  N_ROW("SIDETONE",     NODE_TOGGLE),
  N_ROW("VOLUME",       NODE_STUB),
};
static const UiMenuNode MENU_SET_DISPLAY[] = {
  N_VAL("CONTRAST",     PARAM_CONTRAST),
  N_ROW("INVERT",       NODE_TOGGLE),
  N_ROW("TIMEOUT",      NODE_STUB),
};
static const UiMenuNode MENU_SET_SYSTEM[] = {
  N_ROW("CALLSIGN",      NODE_TOGGLE),
  N_INF("ABOUT",         INFO_ABOUT),
  N_ACT("RESTART",       ACTION_RESTART),
  N_ACT("FACTORY RESET", ACTION_FACTORY_RESET),
};
static const UiMenuNode MENU_SETTINGS[] = {
  N_SUB("KEYER",        MENU_SET_KEYER),
  N_SUB("AUDIO",        MENU_SET_AUDIO),
  N_SUB("DISPLAY",      MENU_SET_DISPLAY),
  N_SUB("SYSTEM",       MENU_SET_SYSTEM),
};

static const UiMenuNode MENU_DIAG[] = {
  N_DIAG("INPUT TEST",    DIAG_INPUT),
  N_DIAG("DISPLAY TEST",  DIAG_DISPLAY),
  N_DIAG("AUDIO TEST",    DIAG_AUDIO),
  N_DIAG("BLE STATUS",    DIAG_BLE),
  N_DIAG("GPIO MONITOR",  DIAG_GPIO),
  N_DIAG("SYSTEM INFO",   DIAG_SYSTEM),
  N_DIAG("MEMORY/PERF",   DIAG_MEMORY),
  N_DIAG("NVS/SETTINGS",  DIAG_NVS),
  N_DIAG("HARDWARE INFO", DIAG_HARDWARE),
};

static const UiMenuNode MENU_TOOLS[] = {
  N_ROW("BATTERY",      NODE_STUB),
  N_ROW("COMING SOON",  NODE_STUB),
};

static const UiMenuNode MENU_GAMES[] = {
  N_ROW("COPY CHALLNG", NODE_STUB),
  N_ROW("MEMORY CHLNG", NODE_STUB),
  N_ROW("SPEED CHLNG",  NODE_STUB),
};

static const UiMenuNode MENU_HELP[] = {
  N_INF("QUICK START",  INFO_QUICK_START),
  N_INF("CONTROLS",     INFO_CONTROLS),
  N_INF("MORSE GUIDE",  INFO_MORSE_GUIDE),
  N_INF("BUTTON GUIDE", INFO_BUTTON_GUIDE),
  N_INF("ABOUT",        INFO_ABOUT),
};

const UiMainItem UI_MAIN_MENU[] = {
  { "CW Keyer",     &UI_MAIN_MENU_ANIM_ICONS[0], MENU_KEYER,        sizeof(MENU_KEYER)/sizeof(UiMenuNode) },
  { "Training",     &UI_MAIN_MENU_ANIM_ICONS[1], MENU_TRAINING,     sizeof(MENU_TRAINING)/sizeof(UiMenuNode) },
  { "Statistics",   &UI_MAIN_MENU_ANIM_ICONS[2], MENU_STATS,        sizeof(MENU_STATS)/sizeof(UiMenuNode) },
  { "Connectivity", &UI_MAIN_MENU_ANIM_ICONS[3], MENU_CONNECTIVITY, sizeof(MENU_CONNECTIVITY)/sizeof(UiMenuNode) },
  { "Profiles",     &UI_MAIN_MENU_ANIM_ICONS[4], MENU_PROFILES,     sizeof(MENU_PROFILES)/sizeof(UiMenuNode) },
  { "Settings",     &UI_MAIN_MENU_ANIM_ICONS[5], MENU_SETTINGS,     sizeof(MENU_SETTINGS)/sizeof(UiMenuNode) },
  { "Diagnostics",  &UI_MAIN_MENU_ANIM_ICONS[6], MENU_DIAG,         sizeof(MENU_DIAG)/sizeof(UiMenuNode) },
  { "Tools",        &UI_MAIN_MENU_ANIM_ICONS[7], MENU_TOOLS,        sizeof(MENU_TOOLS)/sizeof(UiMenuNode) },
  { "Games",        &UI_MAIN_MENU_ANIM_ICONS[8], MENU_GAMES,        sizeof(MENU_GAMES)/sizeof(UiMenuNode) },
  { "Help",         &UI_MAIN_MENU_ANIM_ICONS[9], MENU_HELP,         sizeof(MENU_HELP)/sizeof(UiMenuNode) },
};

const uint8_t UI_MAIN_MENU_COUNT = sizeof(UI_MAIN_MENU) / sizeof(UI_MAIN_MENU[0]);