#ifndef UI_MENU_H
#define UI_MENU_H
#include <Arduino.h>
#include "ui_icons_anim.h"

enum UiNodeType : uint8_t {
  NODE_SUBMENU, NODE_VALUE, NODE_TOGGLE, NODE_ACTION, NODE_INFO, NODE_STUB,
  NODE_DIAG, NODE_MONITOR, NODE_TUNE, NODE_TRIGGER,
  NODE_TRAIN_DRILL, NODE_TRAIN_FARNSWORTH
};

// Consumed only within ui_state.cpp/ui_backend.cpp - never crosses into
// core_trainer.h directly (ui_state.cpp's frozen no-core-includes rule).
// Positional mapping documented at ui_backend_trainStartSession().
enum UiTrainDrillId : uint8_t {
  TRAIN_DRILL_NONE = 0, TRAIN_DRILL_KOCH, TRAIN_DRILL_CHARACTERS,
  TRAIN_DRILL_WORDS, TRAIN_DRILL_CALLSIGNS, TRAIN_DRILL_ADAPTIVE, TRAIN_DRILL_EXAM
};

enum UiParamId : uint8_t {
  PARAM_NONE = 0, PARAM_WPM, PARAM_TONE, PARAM_PADDLE_REV, PARAM_CONTRAST,
  PARAM_MODE, PARAM_DECODER_EN
};

enum UiInfoId : uint8_t {
  INFO_NONE = 0, INFO_ABOUT, INFO_BLE_STATUS, INFO_SYSTEM_INFO,
  INFO_DEVICE_INFO, INFO_QUICK_START, INFO_CONTROLS, INFO_MORSE_GUIDE,
  INFO_BUTTON_GUIDE
};

enum UiDiagId : uint8_t {
  DIAG_NONE = 0, DIAG_INPUT, DIAG_DISPLAY, DIAG_AUDIO, DIAG_BLE,
  DIAG_GPIO, DIAG_SYSTEM, DIAG_MEMORY, DIAG_NVS, DIAG_HARDWARE
};

enum UiActionId : uint8_t {
  ACTION_NONE = 0, ACTION_BOND_RESET, ACTION_FACTORY_RESET, ACTION_RESTART
};

struct UiMenuNode {
  const char       *label;
  UiNodeType        type;
  const UiMenuNode *children;
  uint8_t           childCount;
  uint8_t           paramId;   // ParamId/InfoId/DiagId/ActionId, or for
                                // NODE_TRIGGER: memory slot index + 1
};

struct UiMainItem {
  const char       *title;
  const UiAnimIcon *icon;
  const UiMenuNode *children;
  uint8_t           childCount;
};

extern const UiMainItem UI_MAIN_MENU[];
extern const uint8_t    UI_MAIN_MENU_COUNT;

#endif