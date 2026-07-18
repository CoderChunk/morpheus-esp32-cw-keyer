/*
 * ============================================================================
 * MORPHEUS OLED UI - Navigation Engine
 * ============================================================================
 * File: ui_state.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds: PARAM_DECODER_EN wiring (real toggle), the Live
 * Monitor engine (auto-refreshing, reuses UI_DIAG_REFRESH_MS), the Tune
 * engine (reuses the diagnostic tone gate, 30s safety auto-timeout,
 * reconciles if real keying interrupts it), and NODE_TRIGGER handling in
 * handleList (immediate memory playback, no navigation change, toast
 * feedback). Diagnostics Audio Test's start branch gained one guard line
 * (refuses if memory is playing) to prevent a genuine new resource
 * conflict introduced by background memory playback - see project notes.
 *
 * Long-press Back (global EV_HOME) now also stops Tune and Memory
 * playback, consistent with that gesture's existing "return to known-
 * safe state" role.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "ui_state.h"
#include "ui_config.h"
#include "ui_mockdata.h"
#include "ui_backend.h"
#include "ui_renderer.h"
#include <string.h>
#include <stdio.h>

UiStatusData uiStatus = {
  18, 750, false, UI_MODE_PADDLE,
  "SECR", true, false, true,
  "CQ CQ DE MORPHEUS", "TNX FER QSO", "73", ".--",
  "VU2ABC", true,
  "2026-07-12", "21:35"
};

struct NavFrame {
  const UiMenuNode *list;
  const char       *title;
  uint8_t           count, sel, window;
};

static UiScreen      currentScreen = UI_SCREEN_SPLASH;
static unsigned long splashStartMs = 0;
static unsigned long splashElapsedMs = 0;
static unsigned long lastSplashFrameMs = 0;
static uint8_t       carouselIndex = 0;
static NavFrame      stack[UI_NAV_STACK_DEPTH];
static int8_t        stackTop = -1;
static bool          dirty = true;
static unsigned long lastEventNow = 0;

static uint8_t editingParamId = PARAM_NONE;
static int     editEntrySnapshot = 0;

static uint8_t editingToggleParamId = PARAM_NONE;
static bool    toggleFocusValue = false;

static uint8_t currentInfoId = INFO_NONE;
static char    infoTitle[24] = "";
static char    infoLine1[24] = "";
static char    infoLine2[24] = "";
static char    infoLine3[24] = "";

static BleOverlayKind bleOverlayKind = BLE_OVERLAY_NONE;
static uint32_t       bleOverlayPasskey = 0;
static unsigned long  bleOverlayStartMs = 0;

static uint16_t menuAnimFrame = 0;
static unsigned long menuAnimLastMs = 0;
static uint8_t menuAnimLastCarouselIndex = 0;
static UiScreen menuAnimLastScreen = UI_SCREEN_SPLASH;

static const uint8_t COPY_OVER = 4;   // CopyPhase: IDLE,FALLING,HIT,MISS,OVER
static const uint8_t MEM_OVER  = 4;   // MemGamePhase: IDLE,PLAYBACK,INPUT,ROUND_OK,OVER
static const uint8_t SPD_OVER  = 3;   // SpeedGamePhase: IDLE,LISTEN,FEEDBACK,OVER

static bool audioResourceBusy();
static void handleGameCopy(const UiEvent &ev);
static void handleGameMemory(const UiEvent &ev);
static void handleGameSpeed(const UiEvent &ev);

static UiScreen pauseReturnScreen = UI_SCREEN_HOME;
static uint8_t  gamePauseFocusIdx = 0;

static void handleGamePause(const UiEvent &ev);

uint16_t ui_state_getMenuAnimFrame() { return menuAnimFrame; }

static void markDirty() { dirty = true; }
bool ui_state_consumeDirty() { bool w = dirty; dirty = false; return w; }
void ui_state_notifyDataChanged() { markDirty(); }

static void setInfoTitleFrom(const char *label) {
  strncpy(infoTitle, label, sizeof(infoTitle) - 1);
  infoTitle[sizeof(infoTitle) - 1] = '\0';
}

static int getParamValue(uint8_t paramId) {
  switch (paramId) {
    case PARAM_WPM:      return ui_backend_getWpm();
    case PARAM_TONE:     return (int)ui_backend_getToneHz();
    case PARAM_CONTRAST: return (int)ui_renderer_getContrast();
    default: return 0;
  }
}
static void setParamValue(uint8_t paramId, int value) {
  switch (paramId) {
    case PARAM_WPM:      ui_backend_setWpm(value); break;
    case PARAM_TONE:     ui_backend_setToneHz((uint16_t)value); break;
    case PARAM_CONTRAST: ui_renderer_setContrast((uint8_t)value); break;
    default: break;
  }
}
static void getParamRange(uint8_t paramId, int &lo, int &hi) {
  switch (paramId) {
    case PARAM_WPM:      lo = UI_WPM_MIN;      hi = UI_WPM_MAX;      break;
    case PARAM_TONE:     lo = UI_TONE_MIN_HZ;  hi = UI_TONE_MAX_HZ;  break;
    case PARAM_CONTRAST: lo = UI_CONTRAST_MIN; hi = UI_CONTRAST_MAX; break;
    default: lo = 0; hi = 0; break;
  }
}
static int getParamStep(uint8_t paramId) {
  switch (paramId) {
    case PARAM_TONE:     return 10;
    case PARAM_CONTRAST: return 5;
    default: return 1;
  }
}
static const char *getParamLabel(uint8_t paramId) {
  switch (paramId) {
    case PARAM_WPM:      return "WPM";
    case PARAM_TONE:     return "TONE";
    case PARAM_CONTRAST: return "CONTRAST";
    default: return "";
  }
}

static bool getToggleValue(uint8_t paramId) {
  switch (paramId) {
    case PARAM_PADDLE_REV:  return ui_backend_getPaddleReversed();
    case PARAM_MODE:        return ui_backend_getModeIsPaddle();
    case PARAM_DECODER_EN:  return ui_backend_getDecoderEnabled();
    default: return false;
  }
}
static void setToggleValue(uint8_t paramId, bool v) {
  switch (paramId) {
    case PARAM_PADDLE_REV:  ui_backend_setPaddleReversed(v); break;
    case PARAM_MODE:        ui_backend_setModeIsPaddle(v); break;
    case PARAM_DECODER_EN:  ui_backend_setDecoderEnabled(v); break;
    default: break;
  }
}
static const char *getToggleLabel(uint8_t paramId) {
  switch (paramId) {
    case PARAM_PADDLE_REV: return "PADDLE REV";
    case PARAM_MODE:       return "KEYER MODE";
    case PARAM_DECODER_EN: return "DECODER";
    default: return "";
  }
}
static void getToggleOptionLabelsShort(uint8_t paramId, const char *&offLabel, const char *&onLabel) {
  switch (paramId) {
    case PARAM_MODE: offLabel = "STR"; onLabel = "PAD"; break;
    default:         offLabel = "OFF"; onLabel = "ON";  break;
  }
}
static void getToggleOptionLabelsFull(uint8_t paramId, const char *&offLabel, const char *&onLabel) {
  switch (paramId) {
    case PARAM_MODE: offLabel = "Straight Key"; onLabel = "Paddle Key"; break;
    default: getToggleOptionLabelsShort(paramId, offLabel, onLabel); break;
  }
}
const char *ui_state_getEditLabel() { return getParamLabel(editingParamId); }
int         ui_state_getEditValue() { return getParamValue(editingParamId); }
void        ui_state_getEditRange(int &lo, int &hi) { getParamRange(editingParamId, lo, hi); }
const char *ui_state_getToggleLabel() { return getToggleLabel(editingToggleParamId); }
bool        ui_state_getToggleFocusValue() { return toggleFocusValue; }
void ui_state_getToggleOptionLabels(const char **offLabel, const char **onLabel) {
  const char *off, *on;
  getToggleOptionLabelsFull(editingToggleParamId, off, on);
  *offLabel = off; *onLabel = on;
}
unsigned long ui_state_getSplashElapsedMs() { return splashElapsedMs; }

void ui_state_getRowValueText(const UiMenuNode &n, char *out, size_t outSize) {
  if (n.type == NODE_VALUE && n.paramId != PARAM_NONE) {
    snprintf(out, outSize, "%d", getParamValue(n.paramId));
  } else if (n.type == NODE_TOGGLE && n.paramId != PARAM_NONE) {
    const char *off, *on;
    getToggleOptionLabelsShort(n.paramId, off, on);
    snprintf(out, outSize, "%s", getToggleValue(n.paramId) ? on : off);
  } else {
    out[0] = '\0';
  }
}

bool ui_state_isTriggerPlaying(uint8_t paramId) {
  if (paramId == 0) return false;
  return ui_backend_isMemoryPlaying() && ((uint8_t)(ui_backend_getPlayingMemorySlot() + 1) == paramId);
}

static void buildInfoContent(uint8_t infoId) {
  infoLine1[0] = infoLine2[0] = infoLine3[0] = '\0';
  switch (infoId) {
    case INFO_ABOUT:
      snprintf(infoLine1, sizeof(infoLine1), "MORPHEUS-CW");
      snprintf(infoLine2, sizeof(infoLine2), "v1.2.2");
      snprintf(infoLine3, sizeof(infoLine3), "(c) Coder Chunk");
      break;
    case INFO_BLE_STATUS:
      snprintf(infoLine1, sizeof(infoLine1), "Link: %s", ui_backend_bleIsConnected() ? "Connected" : "Advertising");
      snprintf(infoLine2, sizeof(infoLine2), "Secure: %s", ui_backend_bleIsSecure() ? "Yes" : "No");
      snprintf(infoLine3, sizeof(infoLine3), "Bonded: %s", ui_backend_bleHasTrustedDevice() ? "Yes" : "No");
      break;
    case INFO_DEVICE_INFO:
      snprintf(infoLine1, sizeof(infoLine1), "%s", ui_backend_getDeviceName());
      snprintf(infoLine2, sizeof(infoLine2), "ESP32-WROOM-32");
      break;
    case INFO_QUICK_START:
      snprintf(infoLine1, sizeof(infoLine1), "Connect key/paddle");
      snprintf(infoLine2, sizeof(infoLine2), "Rotate to navigate");
      snprintf(infoLine3, sizeof(infoLine3), "Confirm to select");
      break;
    case INFO_CONTROLS:
      snprintf(infoLine1, sizeof(infoLine1), "Rotate: move/adjust");
      snprintf(infoLine2, sizeof(infoLine2), "Confirm: select/save");
      snprintf(infoLine3, sizeof(infoLine3), "Back: cancel/up");
      break;
    case INFO_MORSE_GUIDE:
      snprintf(infoLine1, sizeof(infoLine1), "E=. T=- A=.-");
      snprintf(infoLine2, sizeof(infoLine2), "Full chart: ARRL");
      break;
    case INFO_BUTTON_GUIDE:
      snprintf(infoLine1, sizeof(infoLine1), "Rotate=nav/adjust");
      snprintf(infoLine2, sizeof(infoLine2), "Push/Confirm=select");
      snprintf(infoLine3, sizeof(infoLine3), "Back hold=Home");
      break;
    case INFO_GAMES_GUIDE:
      snprintf(infoLine1, sizeof(infoLine1), "Confirm=Play/Retry");
      snprintf(infoLine2, sizeof(infoLine2), "Hold Back=Pause menu");
      snprintf(infoLine3, sizeof(infoLine3), "Back=Quit to menu");
      break;
    default:
      snprintf(infoLine1, sizeof(infoLine1), "Feature not yet");
      snprintf(infoLine2, sizeof(infoLine2), "available in this");
      snprintf(infoLine3, sizeof(infoLine3), "firmware version");
      break;
  }
}

static void buildGameInfoContent(uint8_t infoId, const char *rowLabel) {
  setInfoTitleFrom(rowLabel);
  infoLine1[0] = infoLine2[0] = infoLine3[0] = '\0';

  switch (infoId) {
    case INFO_GAME_COPY_CONTROLS:
      snprintf(infoLine1, sizeof(infoLine1), "Key = answer");
      snprintf(infoLine2, sizeof(infoLine2), "Confirm = retry");
      snprintf(infoLine3, sizeof(infoLine3), "Back = pause menu");
      break;
    case INFO_GAME_COPY_HELP:
      snprintf(infoLine1, sizeof(infoLine1), "Key the letter you");
      snprintf(infoLine2, sizeof(infoLine2), "hear before the orb");
      snprintf(infoLine3, sizeof(infoLine3), "reaches the line");
      break;
    case INFO_GAME_COPY_SCORE: {
      snprintf(infoLine1, sizeof(infoLine1), "Best: %u", (unsigned)ui_backend_gameCopyHighScore());
      if (ui_backend_isGameSessionActive()) {
        snprintf(infoLine2, sizeof(infoLine2), "Current: %u", (unsigned)ui_backend_gameCopyScore());
      }
      break;
    }
    case INFO_GAME_MEMORY_CONTROLS:
      snprintf(infoLine1, sizeof(infoLine1), "Key = repeat pattern");
      snprintf(infoLine2, sizeof(infoLine2), "Confirm = retry");
      snprintf(infoLine3, sizeof(infoLine3), "Back = pause menu");
      break;
    case INFO_GAME_MEMORY_HELP:
      snprintf(infoLine1, sizeof(infoLine1), "Repeat the growing");
      snprintf(infoLine2, sizeof(infoLine2), "sequence you hear,");
      snprintf(infoLine3, sizeof(infoLine3), "one letter at a time");
      break;
    case INFO_GAME_MEMORY_SCORE:
      snprintf(infoLine1, sizeof(infoLine1), "Best chain: %u", (unsigned)ui_backend_gameMemoryHighScore());
      break;
    case INFO_GAME_SPEED_CONTROLS:
      snprintf(infoLine1, sizeof(infoLine1), "Key = answer");
      snprintf(infoLine2, sizeof(infoLine2), "Confirm = retry");
      snprintf(infoLine3, sizeof(infoLine3), "Back = pause menu");
      break;
    case INFO_GAME_SPEED_HELP:
      snprintf(infoLine1, sizeof(infoLine1), "Key each letter before");
      snprintf(infoLine2, sizeof(infoLine2), "the beat runs out.");
      snprintf(infoLine3, sizeof(infoLine3), "Speed ramps up!");
      break;
    case INFO_GAME_SPEED_SCORE:
      snprintf(infoLine1, sizeof(infoLine1), "Best combo: %u", (unsigned)ui_backend_gameSpeedHighScore());
      break;
    default: break;
  }
}

const char *ui_state_getInfoTitle() { return infoTitle; }
const char *ui_state_getInfoLine1() { return infoLine1; }
const char *ui_state_getInfoLine2() { return infoLine2; }
const char *ui_state_getInfoLine3() { return infoLine3; }

void ui_state_setBleStatus(UiBleStatus status, uint32_t passkey, unsigned long now) {
  switch (status) {
    case UI_BLE_ADV:
      uiStatus.bleStatus = "ADV"; uiStatus.bleConnected = false;
      bleOverlayKind = BLE_OVERLAY_NONE; break;
    case UI_BLE_CONNECTED:
      uiStatus.bleStatus = "CONN"; uiStatus.bleConnected = true;
      bleOverlayKind = BLE_OVERLAY_NONE; break;
    case UI_BLE_PAIRING:
      uiStatus.bleStatus = "CONN"; uiStatus.bleConnected = true;
      bleOverlayKind = BLE_OVERLAY_PAIRING; bleOverlayPasskey = passkey; break;
    case UI_BLE_PAIR_OK:
      bleOverlayKind = BLE_OVERLAY_RESULT_OK; bleOverlayStartMs = now; break;
    case UI_BLE_PAIR_FAIL:
      uiStatus.bleStatus = "ADV"; uiStatus.bleConnected = false;
      bleOverlayKind = BLE_OVERLAY_RESULT_FAIL; bleOverlayStartMs = now; break;
    case UI_BLE_SECURE:
      uiStatus.bleStatus = "SECR"; uiStatus.bleConnected = true;
      bleOverlayKind = BLE_OVERLAY_NONE; break;
  }
  markDirty();
}

bool ui_state_getBleOverlay(BleOverlayKind &kind, uint32_t &passkey) {
  if (bleOverlayKind == BLE_OVERLAY_NONE) return false;
  kind = bleOverlayKind; passkey = bleOverlayPasskey;
  return true;
}

// ============================================================================
// CONFIRM DIALOG ENGINE
// ============================================================================
static uint8_t dialogActionId = ACTION_NONE;
static bool    dialogFocusYes = false;
static char    dialogTitle[24] = "";
static char    dialogMessage[24] = "";

static bool           actionToastActive = false;
static char           actionToastText[20] = "";
static unsigned long  actionToastStartMs = 0;

static bool           pendingRestart = false;
static unsigned long  pendingRestartAtMs = 0;
static const unsigned long RESTART_DELAY_MS = 600;

static void showActionToast(const char *text) {
  strncpy(actionToastText, text, sizeof(actionToastText) - 1);
  actionToastText[sizeof(actionToastText) - 1] = '\0';
  actionToastActive = true;
  actionToastStartMs = lastEventNow;
}

bool ui_state_getActionToast(char *out, size_t outSize) {
  if (!actionToastActive) return false;
  strncpy(out, actionToastText, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

static void buildDialogContent(uint8_t actionId, const char *rowLabel) {
  strncpy(dialogTitle, rowLabel, sizeof(dialogTitle) - 1);
  dialogTitle[sizeof(dialogTitle) - 1] = '\0';
  switch (actionId) {
    case ACTION_BOND_RESET:    snprintf(dialogMessage, sizeof(dialogMessage), "Clear BLE bond?"); break;
    case ACTION_FACTORY_RESET: snprintf(dialogMessage, sizeof(dialogMessage), "Reset all settings?"); break;
    case ACTION_RESTART:       snprintf(dialogMessage, sizeof(dialogMessage), "Restart device now?"); break;
    default:                   snprintf(dialogMessage, sizeof(dialogMessage), "Confirm action?"); break;
  }
}

const char *ui_state_getDialogTitle()   { return dialogTitle; }
const char *ui_state_getDialogMessage() { return dialogMessage; }
bool        ui_state_getDialogFocusYes(){ return dialogFocusYes; }

static void pushDialog(uint8_t actionId, const char *rowLabel) {
  dialogActionId = actionId;
  dialogFocusYes = false;
  buildDialogContent(actionId, rowLabel);
  currentScreen = UI_SCREEN_DIALOG_CONFIRM;
  markDirty();
}

static void executeDialogAction() {
  switch (dialogActionId) {
    case ACTION_BOND_RESET:
      ui_backend_bondReset();
      showActionToast("BOND CLEARED");
      break;
    case ACTION_FACTORY_RESET:
      ui_backend_factoryResetSettings();
      showActionToast("SETTINGS RESET");
      break;
    case ACTION_RESTART:
      showActionToast("RESTARTING...");
      pendingRestart = true;
      pendingRestartAtMs = lastEventNow + RESTART_DELAY_MS;
      break;
    default: break;
  }
}

static void exitDialog() {
  dialogActionId = ACTION_NONE;
  currentScreen = UI_SCREEN_LIST;
  markDirty();
}

// ============================================================================
// DIAGNOSTICS MODULE STATE
// ============================================================================
static uint32_t diagInputDetentTotal = 0;
static uint32_t diagInputSelectCount = 0;
static uint32_t diagInputBackCount   = 0;
static char     diagInputLastEvent[16] = "-";

uint32_t ui_state_getDiagInputDetentTotal() { return diagInputDetentTotal; }
uint32_t ui_state_getDiagInputSelectCount() { return diagInputSelectCount; }
uint32_t ui_state_getDiagInputBackCount()   { return diagInputBackCount; }
const char *ui_state_getDiagInputLastEvent() { return diagInputLastEvent; }
bool ui_state_getDiagInputTipActive()  { return ui_backend_isTipActive(); }
bool ui_state_getDiagInputRingActive() { return ui_backend_isRingActive(); }
const char *ui_state_getDiagInputModeStr() { return ui_backend_getModeStr(); }

static const uint8_t DIAG_DISPLAY_PAGE_COUNT   = 7;
static const uint8_t DIAG_DISPLAY_ICON_PAGE    = 4;
static const uint8_t DIAG_DISPLAY_CONTRAST_PAGE= 6;

static uint8_t diagDisplayPage = 0;
static bool    diagDisplayContrastAdjust = false;
static uint8_t diagIconCycleIndex = 0;
static unsigned long diagIconCycleLastMs = 0;

uint8_t ui_state_getDiagDisplayPage() { return diagDisplayPage; }
uint8_t ui_state_getDiagDisplayPageCount() { return DIAG_DISPLAY_PAGE_COUNT; }
bool    ui_state_getDiagDisplayContrastAdjust() { return diagDisplayContrastAdjust; }
uint8_t ui_state_getDiagDisplayContrastValue() { return ui_renderer_getContrast(); }
uint8_t ui_state_getDiagDisplayIconIndex() { return diagIconCycleIndex; }

static const uint16_t DIAG_AUDIO_PRESETS[] = { 300, 500, 600, 700, 800, 1000, 1500, 2000 };
static const uint8_t  DIAG_AUDIO_PRESET_COUNT = sizeof(DIAG_AUDIO_PRESETS) / sizeof(DIAG_AUDIO_PRESETS[0]);
static const uint8_t  DIAG_AUDIO_MANUAL_PAGE  = DIAG_AUDIO_PRESET_COUNT;
static const uint8_t  DIAG_AUDIO_PAGE_COUNT   = DIAG_AUDIO_PRESET_COUNT + 1;
static const int32_t  DIAG_AUDIO_MANUAL_STEP  = 25;

static uint8_t  diagAudioPage = 0;
static uint16_t diagAudioManualFreq = 600;
static bool     diagAudioPlaying = false;

uint8_t  ui_state_getDiagAudioPage() { return diagAudioPage; }
uint8_t  ui_state_getDiagAudioPageCount() { return DIAG_AUDIO_PAGE_COUNT; }
bool     ui_state_getDiagAudioIsManualPage() { return diagAudioPage == DIAG_AUDIO_MANUAL_PAGE; }
uint16_t ui_state_getDiagAudioCurrentFreq() {
  return (diagAudioPage < DIAG_AUDIO_PRESET_COUNT) ? DIAG_AUDIO_PRESETS[diagAudioPage] : diagAudioManualFreq;
}
bool ui_state_getDiagAudioPlaying() { return diagAudioPlaying; }

static const uint8_t DIAG_GPIO_ROWS_PER_PAGE = 4;
static uint8_t diagGpioPage = 0;

uint8_t ui_state_getDiagGpioPage() { return diagGpioPage; }
uint8_t ui_state_getDiagGpioPageCount() {
  uint8_t total = ui_backend_getGpioMonCount();
  return (uint8_t)((total + DIAG_GPIO_ROWS_PER_PAGE - 1) / DIAG_GPIO_ROWS_PER_PAGE);
}
void ui_state_getDiagGpioEntry(uint8_t rowIndex, const char **name, uint8_t *pin, int *level) {
  uint8_t absIndex = (uint8_t)(diagGpioPage * DIAG_GPIO_ROWS_PER_PAGE + rowIndex);
  ui_backend_getGpioMonEntry(absIndex, name, pin, level);
}

static uint8_t diagLivePage = 0;
static char    diagLiveLines[4][24];
static uint8_t diagLivePageCount = 1;

// Statistics screens reuse this exact DIAG_LIVE engine rather than
// duplicating a second paginated-live-text renderer. This flag is the
// only thing distinguishing which content switch (below) applies.
enum ContentDomain : uint8_t { DOMAIN_DIAG, DOMAIN_STATS };
static ContentDomain currentContentDomain = DOMAIN_DIAG;

static void formatUptime(unsigned long ms, char *out, size_t outSize) {
  unsigned long totalSec = ms / 1000;
  unsigned long h = totalSec / 3600, m = (totalSec % 3600) / 60, s = totalSec % 60;
  snprintf(out, outSize, "Up: %luh %lum %lus", h, m, s);
}

static void formatAccuracyLine(char *out, size_t outSize, const char *label,
                                uint32_t correct, uint32_t total) {
  uint8_t pct = (total == 0) ? 0 : (uint8_t)((correct * 100UL) / total);
  snprintf(out, outSize, "%s %lu/%lu %u%%", label,
           (unsigned long)correct, (unsigned long)total, (unsigned)pct);
}

static void refreshStatsContent(unsigned long now) {
  (void)now;
  uint8_t statsId = currentInfoId;
  diagLiveLines[0][0] = diagLiveLines[1][0] = diagLiveLines[2][0] = diagLiveLines[3][0] = '\0';

  switch (statsId) {
    case STATS_SESSION: {
      diagLivePageCount = 1;
      setInfoTitleFrom("SESSION");
      snprintf(diagLiveLines[0], 24, "Chars:%lu Words:%lu",
               (unsigned long)ui_backend_statsSessionChars(),
               (unsigned long)ui_backend_statsSessionWords());
      snprintf(diagLiveLines[1], 24, "Elements: %lu",
               (unsigned long)ui_backend_statsSessionElements());
      formatAccuracyLine(diagLiveLines[2], 24, "Train",
                          ui_backend_statsSessionTrainCorrect(),
                          ui_backend_statsSessionTrainAttempts());
      char upt[24];
      formatUptime(ui_backend_statsSessionUptimeMs(), upt, sizeof(upt));
      snprintf(diagLiveLines[3], 24, "%s", upt);
      break;
    }
    case STATS_LIFETIME: {
      diagLivePageCount = 2;
      if (diagLivePage == 0) {
        setInfoTitleFrom("LIFETIME 1/2");
        snprintf(diagLiveLines[0], 24, "Chars: %lu", (unsigned long)ui_backend_statsLifetimeChars());
        snprintf(diagLiveLines[1], 24, "Words: %lu", (unsigned long)ui_backend_statsLifetimeWords());
        snprintf(diagLiveLines[2], 24, "Elements: %lu", (unsigned long)ui_backend_statsLifetimeElements());
      } else {
        setInfoTitleFrom("LIFETIME 2/2");
        uint32_t sec = ui_backend_statsLifetimeUptimeSec();
        snprintf(diagLiveLines[0], 24, "Time: %luh %lum",
                 (unsigned long)(sec / 3600), (unsigned long)((sec % 3600) / 60));
        formatAccuracyLine(diagLiveLines[1], 24, "Train",
                            ui_backend_statsLifetimeTrainCorrect(),
                            ui_backend_statsLifetimeTrainAttempts());
        snprintf(diagLiveLines[2], 24, "Exams: %lu/%lu passed",
                 (unsigned long)ui_backend_statsExamsPassed(),
                 (unsigned long)ui_backend_statsExamsTaken());
      }
      break;
    }
    case STATS_PROGRESS: {
      diagLivePageCount = 1;
      setInfoTitleFrom("PROGRESS");
      char charset[24];
      ui_state_getTrainKochCharset(charset, sizeof(charset));
      snprintf(diagLiveLines[0], 24, "Koch Lvl %u", (unsigned)ui_backend_trainGetKochLevel());
      snprintf(diagLiveLines[1], 24, "%s", charset);
      snprintf(diagLiveLines[2], 24, "Exams: %lu taken %lu pass",
               (unsigned long)ui_backend_statsExamsTaken(),
               (unsigned long)ui_backend_statsExamsPassed());
      snprintf(diagLiveLines[3], 24, "Best score: %u%%", (unsigned)ui_backend_statsBestExamScore());
      break;
    }
    case STATS_ACCURACY: {
      diagLivePageCount = 3;
      setInfoTitleFrom("ACCURACY");
      if (diagLivePage == 0) {
        formatAccuracyLine(diagLiveLines[0], 24, "All",
                            ui_backend_statsLifetimeTrainCorrect(), ui_backend_statsLifetimeTrainAttempts());
        formatAccuracyLine(diagLiveLines[1], 24, "Koch",
                            ui_backend_statsModeCorrect(0), ui_backend_statsModeAttempts(0));
        formatAccuracyLine(diagLiveLines[2], 24, "Chars",
                            ui_backend_statsModeCorrect(1), ui_backend_statsModeAttempts(1));
      } else if (diagLivePage == 1) {
        formatAccuracyLine(diagLiveLines[0], 24, "Words",
                            ui_backend_statsModeCorrect(2), ui_backend_statsModeAttempts(2));
        formatAccuracyLine(diagLiveLines[1], 24, "Calls",
                            ui_backend_statsModeCorrect(3), ui_backend_statsModeAttempts(3));
        formatAccuracyLine(diagLiveLines[2], 24, "Adapt",
                            ui_backend_statsModeCorrect(4), ui_backend_statsModeAttempts(4));
      } else {
        formatAccuracyLine(diagLiveLines[0], 24, "Exam",
                            ui_backend_statsModeCorrect(5), ui_backend_statsModeAttempts(5));
      }
      break;
    }
    case STATS_SPEED: {
      uint8_t histCount = ui_backend_statsHistoryCount();
      uint8_t historyPages = (histCount == 0) ? 0 : (uint8_t)((histCount + 2) / 3);
      diagLivePageCount = (uint8_t)(1 + historyPages);
      setInfoTitleFrom("SPEED HISTORY");
      if (diagLivePage == 0) {
        snprintf(diagLiveLines[0], 24, "Current: %d WPM", ui_backend_getWpm());
        snprintf(diagLiveLines[1], 24, "Peak adaptive: %d WPM", ui_backend_statsPeakAdaptiveWpm());
        if (histCount == 0) snprintf(diagLiveLines[2], 24, "No history yet");
        else snprintf(diagLiveLines[2], 24, "%u session(s) recorded", (unsigned)histCount);
      } else {
        uint8_t pageIdx = (uint8_t)(diagLivePage - 1);
        for (uint8_t row = 0; row < 3; row++) {
          uint8_t entryIdx = (uint8_t)(pageIdx * 3 + row);
          if (entryIdx >= histCount) continue;
          uint16_t wpmVal = ui_backend_statsHistoryEntry(entryIdx);
          snprintf(diagLiveLines[row], 24, "Session -%u: %u WPM",
                   (unsigned)(entryIdx + 1), (unsigned)wpmVal);
        }
      }
      break;
    }
    default:
      diagLivePageCount = 1;
      setInfoTitleFrom("STATISTICS");
      snprintf(diagLiveLines[0], 24, "No data available");
      break;
  }
}

static void refreshDiagLiveContent(unsigned long now) {
  if (currentContentDomain == DOMAIN_STATS) { refreshStatsContent(now); return; }

  uint8_t diagId = currentInfoId;
  diagLiveLines[0][0] = diagLiveLines[1][0] = diagLiveLines[2][0] = diagLiveLines[3][0] = '\0';

  switch (diagId) {
    case DIAG_BLE: {
      diagLivePageCount = 1;
      setInfoTitleFrom("BLE STATUS");
      snprintf(diagLiveLines[0], 24, "Link: %s", ui_backend_bleIsConnected() ? "Connected" : "Advertising");
      snprintf(diagLiveLines[1], 24, "Secure: %s", ui_backend_bleIsSecure() ? "Yes" : "No");
      snprintf(diagLiveLines[2], 24, "Bonded: %s", ui_backend_bleHasTrustedDevice() ? "Yes" : "No");
      uint16_t mtu = ui_backend_getBleMtu();
      if (mtu > 0) snprintf(diagLiveLines[3], 24, "MTU: %u bytes", (unsigned)mtu);
      else         snprintf(diagLiveLines[3], 24, "MTU: --");
      break;
    }
    case DIAG_SYSTEM: {
      diagLivePageCount = 2;
      if (diagLivePage == 0) {
        setInfoTitleFrom("SYSTEM INFO 1/2");
        snprintf(diagLiveLines[0], 24, "FW: MORPHEUS v1.2.2");
        snprintf(diagLiveLines[1], 24, "Chip: %s r%u", ui_backend_getChipModel(), (unsigned)ui_backend_getChipRevision());
        snprintf(diagLiveLines[2], 24, "CPU: %u MHz  SDK %s",
                 (unsigned)ui_backend_getCpuFreqMHz(), ui_backend_getSdkVersionStr());
      } else {
        setInfoTitleFrom("SYSTEM INFO 2/2");
        formatUptime(ui_backend_getUptimeMs(), diagLiveLines[0], 24);
        snprintf(diagLiveLines[1], 24, "Reset: %s", ui_backend_getResetReasonStr());
        snprintf(diagLiveLines[2], 24, "Heap: %u B", (unsigned)ui_backend_getFreeHeapBytes());
      }
      break;
    }
    case DIAG_MEMORY: {
      diagLivePageCount = 1;
      setInfoTitleFrom("MEMORY / PERF");
      snprintf(diagLiveLines[0], 24, "Free heap: %u B", (unsigned)ui_backend_getFreeHeapBytes());
      snprintf(diagLiveLines[1], 24, "Min free: %u B", (unsigned)ui_backend_getMinFreeHeapBytes());
      snprintf(diagLiveLines[2], 24, "Heap size: %u B", (unsigned)ui_backend_getHeapSizeBytes());
      snprintf(diagLiveLines[3], 24, "Loop rate: %u Hz", (unsigned)ui_backend_getLoopRateHz());
      break;
    }
    case DIAG_NVS: {
      diagLivePageCount = 1;
      setInfoTitleFrom("NVS / SETTINGS");
      snprintf(diagLiveLines[0], 24, "WPM:%d Tone:%uHz", ui_backend_getWpm(), (unsigned)ui_backend_getToneHz());
      snprintf(diagLiveLines[1], 24, "Paddle Rev: %s", ui_backend_getPaddleReversed() ? "ON" : "OFF");
      snprintf(diagLiveLines[2], 24, "Loaded: %s", ui_backend_settingsWasDefaulted() ? "Defaulted" : "Saved");
      unsigned long agoSec = (now - ui_backend_getLastSettingsSaveMs()) / 1000;
      snprintf(diagLiveLines[3], 24, "Last save: %lus ago", agoSec);
      break;
    }
    case DIAG_HARDWARE: {
      diagLivePageCount = 1;
      setInfoTitleFrom("HARDWARE INFO");
      snprintf(diagLiveLines[0], 24, "MCU: ESP32-WROOM-32");
      snprintf(diagLiveLines[1], 24, "Flash: %u KB", (unsigned)(ui_backend_getFlashSizeBytes() / 1024));
      snprintf(diagLiveLines[2], 24, "Sketch: %u KB", (unsigned)(ui_backend_getSketchSizeBytes() / 1024));
      snprintf(diagLiveLines[3], 24, "Free flash: %u KB", (unsigned)(ui_backend_getFreeSketchSpaceBytes() / 1024));
      break;
    }
    default:
      diagLivePageCount = 1;
      setInfoTitleFrom("DIAGNOSTIC");
      snprintf(diagLiveLines[0], 24, "No data available");
      break;
  }
}

const char *ui_state_getDiagLiveLine(uint8_t i) { return (i > 3) ? "" : diagLiveLines[i]; }
uint8_t ui_state_getDiagLivePage() { return diagLivePage; }
uint8_t ui_state_getDiagLivePageCount() { return diagLivePageCount; }

// ============================================================================
// CW KEYER SUBMENU: LIVE MONITOR
// ============================================================================
static char liveMonLines[4][24];

static void refreshLiveMonitorContent() {
  snprintf(liveMonLines[0], 24, "WPM:%d  %s", ui_backend_getWpm(), ui_backend_getModeStr());
  snprintf(liveMonLines[1], 24, "TX: %s", ui_backend_getKeyStateStr());

  if (ui_backend_getDecoderEnabled()) {
    char pat[UI_PATTERN_CHARS + 1];
    ui_backend_getLivePattern(pat, sizeof(pat));
    snprintf(liveMonLines[2], 24, "PAT: %s", pat);

    char word[UI_LINE_CHARS + 1];
    ui_backend_getLiveWord(word, sizeof(word));
    snprintf(liveMonLines[3], 24, "WORD: %s", word);
  } else {
    snprintf(liveMonLines[2], 24, "DECODER: OFF");
    liveMonLines[3][0] = '\0';
  }
}

const char *ui_state_getLiveMonitorLine(uint8_t i) { return (i > 3) ? "" : liveMonLines[i]; }

static void pushLiveMonitor() {
  refreshLiveMonitorContent();
  currentScreen = UI_SCREEN_LIVE_MONITOR;
  markDirty();
}

static void handleLiveMonitor(const UiEvent &ev) {
  if (ev.type == UI_EV_BACK) popList_forward_declared: ;   // placeholder, replaced below
}

static uint8_t currentTrainDrillId = TRAIN_DRILL_NONE;
static uint8_t lastTrainPhaseSeen = 255;
static uint8_t lastTrainTypedLenSeen = 255;
static bool    lastFarnsworthPlayingSeen = false;

static void pushTrainDrill(uint8_t drillId, const char *label) {
  if (audioResourceBusy()) { showActionToast("AUDIO BUSY"); return; }
  setInfoTitleFrom(label);
  currentTrainDrillId = drillId;
  ui_backend_trainStartSession(drillId);
  lastTrainPhaseSeen = 255;
  lastTrainTypedLenSeen = 255;
  currentScreen = UI_SCREEN_TRAIN_DRILL;
  markDirty();
}

static void pushTrainFarnsworth(const char *label) {
  setInfoTitleFrom(label);
  currentScreen = UI_SCREEN_TRAIN_FARNSWORTH;
  markDirty();
}

static void pushGameStart(uint8_t uiGameId) {
  if (ui_backend_isGamePausedFor(uiGameId)) {
    switch (uiGameId) {
      case UI_GAME_COPY:   pauseReturnScreen = UI_SCREEN_GAME_COPY;   break;
      case UI_GAME_MEMORY: pauseReturnScreen = UI_SCREEN_GAME_MEMORY; break;
      case UI_GAME_SPEED:  pauseReturnScreen = UI_SCREEN_GAME_SPEED;  break;
      default: return;
    }
    currentScreen = UI_SCREEN_GAME_PAUSE;
    markDirty();
    return;
  }

  if (audioResourceBusy()) { showActionToast("AUDIO BUSY"); return; }
  ui_backend_gameStart(uiGameId);
  switch (uiGameId) {
    case UI_GAME_COPY:   currentScreen = UI_SCREEN_GAME_COPY;   break;
    case UI_GAME_MEMORY: currentScreen = UI_SCREEN_GAME_MEMORY; break;
    case UI_GAME_SPEED:  currentScreen = UI_SCREEN_GAME_SPEED;  break;
    default: break;
  }
  markDirty();
}

// ============================================================================
// CW KEYER SUBMENU: TUNE
// ============================================================================
static bool          tuneActive = false;
static unsigned long tuneStartMs = 0;

// Centralized audio-resource guard - fixes a real gap: with two more
// audio-owning features added (drills, Farnsworth), the previous
// pairwise "check ui_backend_isMemoryPlaying()" checks scattered across
// handleDiagAudio/handleTune/the memory trigger no longer cover every
// combination. This one helper is now used everywhere an audio-owning
// action is about to start.
static bool audioResourceBusy() {
  return diagAudioPlaying || tuneActive || ui_backend_isMemoryPlaying() ||
         ui_backend_isTrainingSessionActive() || ui_backend_isFarnsworthPlaying() ||
         ui_backend_isGameSessionActive();
}

bool ui_state_getTuneActive() { return tuneActive; }
uint16_t ui_state_getTuneFreq() { return ui_backend_getToneHz(); }
unsigned long ui_state_getTuneRemainingMs() {
  if (!tuneActive) return 0;
  unsigned long elapsed = lastEventNow - tuneStartMs;
  return (elapsed >= UI_TUNE_TIMEOUT_MS) ? 0 : (UI_TUNE_TIMEOUT_MS - elapsed);
}

// ----------------------------------------------------------------------------
// Transitions
// ----------------------------------------------------------------------------
static void goHome() {
  stackTop = -1;
  currentScreen = UI_SCREEN_HOME;
  markDirty();
}

static void pushList(const UiMenuNode *list, uint8_t count, const char *title) {
  if (stackTop >= (int8_t)(UI_NAV_STACK_DEPTH - 1)) return;
  stackTop++;
  stack[stackTop] = { list, title, count, 0, 0 };
  currentScreen = UI_SCREEN_LIST;
  markDirty();
}

static void popList() {
  if (stackTop >= 0) stackTop--;
  currentScreen = (stackTop >= 0) ? UI_SCREEN_LIST : UI_SCREEN_MENU;
  markDirty();
}

static void pushEditValue(uint8_t paramId) {
  editingParamId = paramId;
  editEntrySnapshot = getParamValue(paramId);
  currentScreen = UI_SCREEN_EDIT_VALUE;
  markDirty();
}

static void exitEditValue(bool commit) {
  if (!commit) setParamValue(editingParamId, editEntrySnapshot);
  editingParamId = PARAM_NONE;
  currentScreen = UI_SCREEN_LIST;
  markDirty();
}

static void pushEditToggle(uint8_t paramId) {
  editingToggleParamId = paramId;
  toggleFocusValue = getToggleValue(paramId);
  currentScreen = UI_SCREEN_EDIT_TOGGLE;
  markDirty();
}

static void exitEditToggle(bool commit) {
  if (commit) setToggleValue(editingToggleParamId, toggleFocusValue);
  editingToggleParamId = PARAM_NONE;
  currentScreen = UI_SCREEN_LIST;
  markDirty();
}

static void pushInfo(uint8_t infoId, const char *rowLabel) {
  currentInfoId = infoId;
  setInfoTitleFrom(rowLabel);
  buildInfoContent(infoId);
  currentScreen = UI_SCREEN_INFO;
  markDirty();
}

static void pushDiagScreen(uint8_t diagId, const char *rowLabel) {
  currentContentDomain = DOMAIN_DIAG;
  switch (diagId) {
    case DIAG_INPUT:
      diagInputDetentTotal = 0; diagInputSelectCount = 0; diagInputBackCount = 0;
      strncpy(diagInputLastEvent, "-", sizeof(diagInputLastEvent) - 1);
      setInfoTitleFrom(rowLabel);
      currentScreen = UI_SCREEN_DIAG_INPUT;
      break;
    case DIAG_DISPLAY:
      diagDisplayPage = 0; diagDisplayContrastAdjust = false;
      diagIconCycleIndex = 0; diagIconCycleLastMs = lastEventNow;
      setInfoTitleFrom(rowLabel);
      currentScreen = UI_SCREEN_DIAG_DISPLAY;
      break;
    case DIAG_AUDIO:
      diagAudioPage = 0; diagAudioPlaying = false;
      ui_backend_diagToneStop();
      setInfoTitleFrom(rowLabel);
      currentScreen = UI_SCREEN_DIAG_AUDIO;
      break;
    case DIAG_GPIO:
      diagGpioPage = 0;
      setInfoTitleFrom(rowLabel);
      currentScreen = UI_SCREEN_DIAG_GPIO;
      break;
    default:
      currentInfoId = diagId;
      diagLivePage = 0;
      refreshDiagLiveContent(lastEventNow);
      currentScreen = UI_SCREEN_DIAG_LIVE;
      break;
  }
  markDirty();
}

static void pushStatsScreen(uint8_t statsId, const char *rowLabel) {
  if (statsId == STATS_NONE) return;
  currentContentDomain = DOMAIN_STATS;
  currentInfoId = statsId;
  setInfoTitleFrom(rowLabel);
  diagLivePage = 0;
  refreshDiagLiveContent(lastEventNow);
  currentScreen = UI_SCREEN_DIAG_LIVE;
  markDirty();
}

// ----------------------------------------------------------------------------
// Per-state handlers
// ----------------------------------------------------------------------------
static void handleHome(const UiEvent &ev) {
  if (ev.type == UI_EV_SELECT) { currentScreen = UI_SCREEN_MENU; markDirty(); }
}

static void handleMenu(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: {
      int idx = (int)carouselIndex + ev.detents;
      if (idx < 0) idx = 0;
      if (idx > (int)UI_MAIN_MENU_COUNT - 1) idx = (int)UI_MAIN_MENU_COUNT - 1;
      if ((uint8_t)idx != carouselIndex) { carouselIndex = (uint8_t)idx; markDirty(); }
      break;
    }
    case UI_EV_SELECT: {
      const UiMainItem &m = UI_MAIN_MENU[carouselIndex];
      pushList(m.children, m.childCount, m.title);
      break;
    }
    case UI_EV_BACK: goHome(); break;
    default: break;
  }
}

static void handleList(const UiEvent &ev) {
  NavFrame &f = stack[stackTop];
  switch (ev.type) {
    case UI_EV_ROTATE: {
      int next = (int)f.sel + ev.detents;
      if (next < 0) next = 0;
      if (next > f.count - 1) next = f.count - 1;
      if ((uint8_t)next != f.sel) {
        f.sel = (uint8_t)next;
        if (f.sel < f.window) f.window = f.sel;
        else if (f.sel >= f.window + UI_LIST_VISIBLE_ROWS)
          f.window = (uint8_t)(f.sel - (UI_LIST_VISIBLE_ROWS - 1));
        markDirty();
      }
      break;
    }
    case UI_EV_SELECT: {
      const UiMenuNode &n = f.list[f.sel];
      if (n.type == NODE_SUBMENU) {
        pushList(n.children, n.childCount, n.label);
      } else if (n.type == NODE_VALUE && n.paramId != PARAM_NONE) {
        pushEditValue(n.paramId);
      } else if (n.type == NODE_TOGGLE && n.paramId != PARAM_NONE) {
        pushEditToggle(n.paramId);
      } else if (n.type == NODE_ACTION && n.paramId != ACTION_NONE) {
        pushDialog(n.paramId, n.label);
      } else if (n.type == NODE_DIAG) {
        pushDiagScreen(n.paramId, n.label);
      } else if (n.type == NODE_MONITOR) {
        pushLiveMonitor();
      } else if (n.type == NODE_TUNE) {
        tuneActive = false;
        currentScreen = UI_SCREEN_TUNE;
        markDirty();
      } else if (n.type == NODE_TRAIN_DRILL && n.paramId != TRAIN_DRILL_NONE) {
        pushTrainDrill(n.paramId, n.label);
      } else if (n.type == NODE_TRAIN_FARNSWORTH) {
        pushTrainFarnsworth(n.label);
      } else if (n.type == NODE_STATS && n.paramId != STATS_NONE) {
        pushStatsScreen(n.paramId, n.label);
      } else if (n.type == NODE_GAME_START && n.paramId != UI_GAME_NONE) {
        pushGameStart(n.paramId);
      } else if (n.type == NODE_GAME_INFO && n.paramId != INFO_NONE) {
        currentInfoId = n.paramId;
        buildGameInfoContent(n.paramId, n.label);
        currentScreen = UI_SCREEN_INFO;
        markDirty();
      } else if (n.type == NODE_TRIGGER && n.paramId != 0) {
        // Immediate, non-destructive: play now, stay on this list, toast.
        uint8_t slot = (uint8_t)(n.paramId - 1);
        if (audioResourceBusy() && !ui_backend_isMemoryPlaying()) {
          showActionToast("AUDIO BUSY");
        } else {
          bool ok = ui_backend_playMemory(slot);
          char toast[20];
          if (ok) snprintf(toast, sizeof(toast), "PLAYING %u/%u", (unsigned)n.paramId, 5);
          else    snprintf(toast, sizeof(toast), "BUSY - CANT PLAY");
          showActionToast(toast);
        }
        markDirty();
      } else if (n.type == NODE_INFO) {
        pushInfo(n.paramId, n.label);
      } else {
        pushInfo(INFO_NONE, n.label);
      }
      break;
    }
    case UI_EV_BACK: popList(); break;
    default: break;
  }
}

static void handleEditValue(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: {
      int lo, hi;
      getParamRange(editingParamId, lo, hi);
      int step = getParamStep(editingParamId);
      int v = getParamValue(editingParamId) + ev.detents * step;
      if (v < lo) v = lo;
      if (v > hi) v = hi;
      if (v != getParamValue(editingParamId)) { setParamValue(editingParamId, v); markDirty(); }
      break;
    }
    case UI_EV_SELECT: exitEditValue(true);  break;
    case UI_EV_BACK:   exitEditValue(false); break;
    default: break;
  }
}

static void handleEditToggle(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: toggleFocusValue = !toggleFocusValue; markDirty(); break;
    case UI_EV_SELECT: exitEditToggle(true);  break;
    case UI_EV_BACK:   exitEditToggle(false); break;
    default: break;
  }
}

static void handleInfo(const UiEvent &ev) {
  if (ev.type == UI_EV_BACK) {
    currentScreen = (stackTop >= 0) ? UI_SCREEN_LIST : UI_SCREEN_MENU;
    markDirty();
  }
}

static void handleDialogConfirm(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: dialogFocusYes = !dialogFocusYes; markDirty(); break;
    case UI_EV_SELECT:
      if (dialogFocusYes) executeDialogAction();
      exitDialog();
      break;
    case UI_EV_BACK: exitDialog(); break;
    default: break;
  }
}

static void handleDiagInput(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE:
      diagInputDetentTotal += ev.detents;
      snprintf(diagInputLastEvent, sizeof(diagInputLastEvent), "ROT %+d", ev.detents);
      markDirty(); break;
    case UI_EV_SELECT:
      diagInputSelectCount++;
      strncpy(diagInputLastEvent, "SELECT", sizeof(diagInputLastEvent) - 1);
      markDirty(); break;
    case UI_EV_BACK:
      diagInputBackCount++;
      strncpy(diagInputLastEvent, "BACK", sizeof(diagInputLastEvent) - 1);
      markDirty(); break;
    default: break;
  }
}

static void handleDiagDisplay(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE:
      if (diagDisplayPage == DIAG_DISPLAY_CONTRAST_PAGE && diagDisplayContrastAdjust) {
        int v = (int)ui_renderer_getContrast() + ev.detents * 5;
        if (v < UI_CONTRAST_MIN) v = UI_CONTRAST_MIN;
        if (v > UI_CONTRAST_MAX) v = UI_CONTRAST_MAX;
        ui_renderer_setContrast((uint8_t)v);
      } else {
        int p = ((int)diagDisplayPage + ev.detents) % (int)DIAG_DISPLAY_PAGE_COUNT;
        if (p < 0) p += DIAG_DISPLAY_PAGE_COUNT;
        diagDisplayPage = (uint8_t)p;
      }
      markDirty();
      break;
    case UI_EV_SELECT:
      if (diagDisplayPage == DIAG_DISPLAY_CONTRAST_PAGE) {
        diagDisplayContrastAdjust = !diagDisplayContrastAdjust;
        markDirty();
      }
      break;
    case UI_EV_BACK:
      diagDisplayContrastAdjust = false;
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleDiagAudio(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE:
      if (diagAudioPlaying) {
        if (diagAudioPage == DIAG_AUDIO_MANUAL_PAGE) {
          int32_t v = (int32_t)diagAudioManualFreq + ev.detents * DIAG_AUDIO_MANUAL_STEP;
          if (v < UI_TONE_MIN_HZ) v = UI_TONE_MIN_HZ;
          if (v > UI_TONE_MAX_HZ) v = UI_TONE_MAX_HZ;
          diagAudioManualFreq = (uint16_t)v;
          ui_backend_diagToneStart(diagAudioManualFreq);
          markDirty();
        }
      } else {
        int p = ((int)diagAudioPage + ev.detents) % (int)DIAG_AUDIO_PAGE_COUNT;
        if (p < 0) p += DIAG_AUDIO_PAGE_COUNT;
        diagAudioPage = (uint8_t)p;
        markDirty();
      }
      break;
    case UI_EV_SELECT:
      if (diagAudioPlaying) {
        ui_backend_diagToneStop();
        diagAudioPlaying = false;
      } else if (audioResourceBusy()) {
        showActionToast("AUDIO BUSY");
      } else {
        uint16_t freq = (diagAudioPage < DIAG_AUDIO_PRESET_COUNT)
                          ? DIAG_AUDIO_PRESETS[diagAudioPage] : diagAudioManualFreq;
        diagAudioPlaying = ui_backend_diagToneStart(freq);
      }
      markDirty();
      break;
    case UI_EV_BACK:
      if (diagAudioPlaying) { ui_backend_diagToneStop(); diagAudioPlaying = false; }
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleDiagGpio(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: {
      uint8_t pageCount = ui_state_getDiagGpioPageCount();
      int p = ((int)diagGpioPage + ev.detents) % (int)pageCount;
      if (p < 0) p += pageCount;
      diagGpioPage = (uint8_t)p;
      markDirty();
      break;
    }
    case UI_EV_BACK: currentScreen = UI_SCREEN_LIST; markDirty(); break;
    default: break;
  }
}

static void handleDiagLive(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE:
      if (diagLivePageCount > 1) {
        int p = ((int)diagLivePage + ev.detents) % (int)diagLivePageCount;
        if (p < 0) p += diagLivePageCount;
        diagLivePage = (uint8_t)p;
        refreshDiagLiveContent(lastEventNow);
        markDirty();
      }
      break;
    case UI_EV_BACK: currentScreen = UI_SCREEN_LIST; markDirty(); break;
    default: break;
  }
}

// --- Live Monitor: correct handler (replaces the placeholder stub above) -----
static void handleLiveMonitorReal(const UiEvent &ev) {
  if (ev.type == UI_EV_BACK) { currentScreen = UI_SCREEN_LIST; markDirty(); }
}

// --- Tune ---------------------------------------------------------------------
static void handleTune(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_SELECT:
      if (tuneActive) {
        ui_backend_diagToneStop();
        tuneActive = false;
      } else if (audioResourceBusy()) {
        showActionToast("AUDIO BUSY");
      } else if (ui_backend_diagToneStart(ui_backend_getToneHz())) {
        tuneActive = true;
        tuneStartMs = lastEventNow;
      }
      markDirty();
      break;
    case UI_EV_BACK:
      if (tuneActive) { ui_backend_diagToneStop(); tuneActive = false; }
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;   // rotate is a no-op on this screen
  }
}

static void handleTrainDrill(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_SELECT: ui_backend_trainConfirmPressed(); markDirty(); break;
    case UI_EV_BACK:   ui_backend_trainStopSession(); currentScreen = UI_SCREEN_LIST; markDirty(); break;
    default: break;   // rotate unused during a drill
  }
}

static void handleTrainFarnsworth(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_ROTATE: {
      int v = ui_backend_farnsworthGetWpm() + ev.detents;
      if (v < UI_WPM_MIN) v = UI_WPM_MIN;
      if (v > UI_WPM_MAX) v = UI_WPM_MAX;
      ui_backend_farnsworthSetWpm(v);
      if (ui_backend_isFarnsworthPlaying()) {
        ui_backend_farnsworthTogglePlay();   // stop
        ui_backend_farnsworthTogglePlay();   // restart at new timing
      }
      markDirty();
      break;
    }
    case UI_EV_SELECT:
      if (audioResourceBusy() && !ui_backend_isFarnsworthPlaying()) {
        showActionToast("AUDIO BUSY");
      } else {
        ui_backend_farnsworthTogglePlay();
      }
      markDirty();
      break;
    case UI_EV_BACK:
      if (ui_backend_isFarnsworthPlaying()) ui_backend_farnsworthTogglePlay();
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleTrainExamResult(const UiEvent &ev) {
  if (ev.type == UI_EV_BACK || ev.type == UI_EV_SELECT) {
    ui_backend_trainClearExamResult();
    currentScreen = (stackTop >= 0) ? UI_SCREEN_LIST : UI_SCREEN_MENU;
    markDirty();
  }
}

static void handleGameCopy(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_SELECT:
      if (ui_backend_gameCopyPhase() == COPY_OVER) ui_backend_gameConfirmPressed();
      markDirty();
      break;
    case UI_EV_BACK:
      ui_backend_gameStop();
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleGameMemory(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_SELECT:
      if (ui_backend_gameMemoryPhase() == MEM_OVER) ui_backend_gameConfirmPressed();
      markDirty();
      break;
    case UI_EV_BACK:
      ui_backend_gameStop();
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleGameSpeed(const UiEvent &ev) {
  switch (ev.type) {
    case UI_EV_SELECT:
      if (ui_backend_gameSpeedPhase() == SPD_OVER) ui_backend_gameConfirmPressed();
      markDirty();
      break;
    case UI_EV_BACK:
      ui_backend_gameStop();
      currentScreen = UI_SCREEN_LIST;
      markDirty();
      break;
    default: break;
  }
}

static void handleGamePause(const UiEvent &ev) {
  static uint8_t gamePauseFocusIdx = 0;
  switch (ev.type) {
    case UI_EV_ROTATE: {
      int idx = (int)gamePauseFocusIdx + ev.detents;
      if (idx < 0) idx = 0;
      if (idx > 2) idx = 2;
      gamePauseFocusIdx = (uint8_t)idx;
      markDirty();
      break;
    }
    case UI_EV_SELECT:
      if (gamePauseFocusIdx == 0) {
        ui_backend_gameTogglePause();
        currentScreen = pauseReturnScreen;
      } else if (gamePauseFocusIdx == 1) {
        ui_backend_gameRestart();
        currentScreen = pauseReturnScreen;
      } else {
        ui_backend_gameStop();
        currentScreen = UI_SCREEN_LIST;
      }
      gamePauseFocusIdx = 0;
      markDirty();
      break;
    case UI_EV_BACK:
      // Short Back from the pause menu resumes - mirrors "Back cancels,
      // doesn't commit" everywhere else in the UI (the pause menu itself
      // is the thing being "backed out of").
      ui_backend_gameTogglePause();
      currentScreen = pauseReturnScreen;
      gamePauseFocusIdx = 0;
      markDirty();
      break;
    default: break;
  }
}

// ----------------------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------------------
void ui_state_handleEvent(const UiEvent &ev, unsigned long now) {
  lastEventNow = now;

  if (currentScreen == UI_SCREEN_SPLASH) {
    if (ev.type == UI_EV_SELECT || ev.type == UI_EV_BACK) goHome();
    return;
  }
  if (ev.type == UI_EV_HOME) {
    if (currentScreen == UI_SCREEN_EDIT_VALUE) {
      setParamValue(editingParamId, editEntrySnapshot);
      editingParamId = PARAM_NONE;
    }
    if (currentScreen == UI_SCREEN_DIALOG_CONFIRM) {
      dialogActionId = ACTION_NONE;
    }
    if (currentScreen == UI_SCREEN_DIAG_AUDIO && diagAudioPlaying) {
      ui_backend_diagToneStop();
      diagAudioPlaying = false;
    }
    if (currentScreen == UI_SCREEN_TUNE && tuneActive) {
      ui_backend_diagToneStop();
      tuneActive = false;
    }
    if (currentScreen == UI_SCREEN_TRAIN_DRILL) ui_backend_trainStopSession();
    if (currentScreen == UI_SCREEN_TRAIN_FARNSWORTH && ui_backend_isFarnsworthPlaying()) {
      ui_backend_farnsworthTogglePlay();
    }
    if ((currentScreen == UI_SCREEN_GAME_COPY || currentScreen == UI_SCREEN_GAME_MEMORY ||
         currentScreen == UI_SCREEN_GAME_SPEED)) {
      pauseReturnScreen = currentScreen;
      ui_backend_gameTogglePause();
      currentScreen = UI_SCREEN_GAME_PAUSE;
      markDirty();
      return;   // do not fall through to goHome() below - pausing, not quitting
    }
    ui_backend_stopMemory();   // long-Back is a "return to safe state" panic gesture
    if (currentScreen != UI_SCREEN_HOME) goHome();
    return;
  }

  switch (currentScreen) {
    case UI_SCREEN_HOME:              handleHome(ev);             break;
    case UI_SCREEN_MENU:              handleMenu(ev);             break;
    case UI_SCREEN_LIST:              handleList(ev);             break;
    case UI_SCREEN_EDIT_VALUE:        handleEditValue(ev);        break;
    case UI_SCREEN_EDIT_TOGGLE:       handleEditToggle(ev);       break;
    case UI_SCREEN_INFO:              handleInfo(ev);             break;
    case UI_SCREEN_DIALOG_CONFIRM:    handleDialogConfirm(ev);    break;
    case UI_SCREEN_DIAG_INPUT:        handleDiagInput(ev);        break;
    case UI_SCREEN_DIAG_DISPLAY:      handleDiagDisplay(ev);      break;
    case UI_SCREEN_DIAG_AUDIO:        handleDiagAudio(ev);        break;
    case UI_SCREEN_DIAG_GPIO:         handleDiagGpio(ev);         break;
    case UI_SCREEN_DIAG_LIVE:         handleDiagLive(ev);         break;
    case UI_SCREEN_LIVE_MONITOR:      handleLiveMonitorReal(ev);  break;
    case UI_SCREEN_TUNE:              handleTune(ev);             break;
    case UI_SCREEN_TRAIN_DRILL:       handleTrainDrill(ev);       break;
    case UI_SCREEN_TRAIN_FARNSWORTH:  handleTrainFarnsworth(ev);  break;
    case UI_SCREEN_TRAIN_EXAM_RESULT: handleTrainExamResult(ev);  break;
    case UI_SCREEN_GAME_COPY:         handleGameCopy(ev);         break;
    case UI_SCREEN_GAME_MEMORY:       handleGameMemory(ev);       break;
    case UI_SCREEN_GAME_SPEED:        handleGameSpeed(ev);        break;
    case UI_SCREEN_GAME_PAUSE:        handleGamePause(ev);        break;
    default: break;
  }
}

void ui_state_service(unsigned long now) {
  lastEventNow = now;   // keep fresh for getters like ui_state_getTuneRemainingMs
  static unsigned long gameAnimLastMs = 0;

  // Main menu icon animation - restarts at frame 0 on entering the
  // carousel or on rotating to a new item; otherwise advances on its own
  // timer, independent of navigation events.
  bool enteringMenu = (currentScreen == UI_SCREEN_MENU && menuAnimLastScreen != UI_SCREEN_MENU);
  menuAnimLastScreen = currentScreen;

  if (currentScreen == UI_SCREEN_MENU) {
    if (enteringMenu || carouselIndex != menuAnimLastCarouselIndex) {
      menuAnimLastCarouselIndex = carouselIndex;
      menuAnimFrame = 0;
      menuAnimLastMs = now;
      markDirty();
    } else if (now - menuAnimLastMs >= UI_ICON_ANIM_FRAME_MS) {
      menuAnimLastMs = now;
      menuAnimFrame++;
      markDirty();
    }
  }

  if (currentScreen == UI_SCREEN_SPLASH) {
    splashElapsedMs = now - splashStartMs;
    if (now - lastSplashFrameMs >= UI_SPLASH_FRAME_MS) { lastSplashFrameMs = now; markDirty(); }
    if (splashElapsedMs >= UI_SPLASH_MS) goHome();
  }

  if (bleOverlayKind == BLE_OVERLAY_RESULT_OK || bleOverlayKind == BLE_OVERLAY_RESULT_FAIL) {
    if (now - bleOverlayStartMs >= UI_BLE_OVERLAY_MS) { bleOverlayKind = BLE_OVERLAY_NONE; markDirty(); }
  }

  if (actionToastActive && (now - actionToastStartMs >= UI_SAVED_TOAST_MS)) {
    actionToastActive = false;
    markDirty();
  }

  if (pendingRestart && now >= pendingRestartAtMs) {
    ui_backend_restartDevice();
  }

  static unsigned long diagRefreshLastMs = 0;
  bool needsLiveRefresh = (currentScreen == UI_SCREEN_DIAG_LIVE) ||
                          (currentScreen == UI_SCREEN_DIAG_GPIO) ||
                          (currentScreen == UI_SCREEN_DIAG_INPUT) ||
                          (currentScreen == UI_SCREEN_LIVE_MONITOR) ||
                          (currentScreen == UI_SCREEN_TUNE);
  if (needsLiveRefresh && (now - diagRefreshLastMs >= UI_DIAG_REFRESH_MS)) {
    diagRefreshLastMs = now;
    if (currentScreen == UI_SCREEN_DIAG_LIVE) refreshDiagLiveContent(now);
    if (currentScreen == UI_SCREEN_LIVE_MONITOR) refreshLiveMonitorContent();
    markDirty();
  }

  if (currentScreen == UI_SCREEN_DIAG_AUDIO) {
    bool actuallyPlaying = ui_backend_isDiagToneActive();
    if (diagAudioPlaying != actuallyPlaying) { diagAudioPlaying = actuallyPlaying; markDirty(); }
  }

  if (currentScreen == UI_SCREEN_TUNE && tuneActive) {
    bool actuallyOn = ui_backend_isDiagToneActive();
    if (!actuallyOn) { tuneActive = false; markDirty(); }   // interrupted by real keying
    else if (now - tuneStartMs >= UI_TUNE_TIMEOUT_MS) {
      ui_backend_diagToneStop();
      tuneActive = false;
      showActionToast("TUNE TIMEOUT");
      markDirty();
    }
  }

  if (currentScreen == UI_SCREEN_DIAG_DISPLAY && diagDisplayPage == DIAG_DISPLAY_ICON_PAGE) {
    if (now - diagIconCycleLastMs >= UI_DIAG_ICON_CYCLE_MS) {
      diagIconCycleLastMs = now;
      diagIconCycleIndex = (uint8_t)((diagIconCycleIndex + 1) % UI_MAIN_MENU_COUNT);
      markDirty();
    }
  }

  if (currentScreen == UI_SCREEN_TRAIN_DRILL) {
    uint8_t curPhase = ui_backend_trainGetPhase();
    uint8_t curTypedLen = (uint8_t)strlen(ui_backend_trainGetTyped());
    if (curPhase != lastTrainPhaseSeen || curTypedLen != lastTrainTypedLenSeen) {
      lastTrainPhaseSeen = curPhase;
      lastTrainTypedLenSeen = curTypedLen;
      markDirty();
    }
    if (ui_backend_trainIsExamResultReady()) {
      currentScreen = UI_SCREEN_TRAIN_EXAM_RESULT;
      markDirty();
    }
  }
  if (currentScreen == UI_SCREEN_TRAIN_FARNSWORTH) {
    bool nowPlaying = ui_backend_isFarnsworthPlaying();
    if (nowPlaying != lastFarnsworthPlayingSeen) {
      lastFarnsworthPlayingSeen = nowPlaying;
      markDirty();
    }
  }
  if ((currentScreen == UI_SCREEN_GAME_COPY || currentScreen == UI_SCREEN_GAME_MEMORY ||
       currentScreen == UI_SCREEN_GAME_SPEED || currentScreen == UI_SCREEN_GAME_PAUSE) &&
      (now - gameAnimLastMs >= UI_GAME_ANIM_MS)) {
    gameAnimLastMs = now;
    markDirty();
  }
}

void ui_state_init(unsigned long now) {
  currentScreen = UI_SCREEN_SPLASH;
  splashStartMs = now;
  splashElapsedMs = 0;
  lastSplashFrameMs = now;
  carouselIndex = 0;
  stackTop = -1;
  editingParamId = PARAM_NONE;
  editingToggleParamId = PARAM_NONE;
  currentInfoId = INFO_NONE;
  bleOverlayKind = BLE_OVERLAY_NONE;
  dialogActionId = ACTION_NONE;
  actionToastActive = false;
  pendingRestart = false;
  tuneActive = false;
  currentTrainDrillId = TRAIN_DRILL_NONE;
  lastTrainPhaseSeen = 255;
  lastTrainTypedLenSeen = 255;
  dirty = true;
  menuAnimFrame = 0;
  menuAnimLastMs = now;
  menuAnimLastCarouselIndex = carouselIndex;
  menuAnimLastScreen = UI_SCREEN_SPLASH;
}

UiScreen ui_state_getScreen()        { return currentScreen; }
uint8_t  ui_state_getCarouselIndex() { return carouselIndex; }

const UiMenuNode *ui_state_getList(uint8_t &count, uint8_t &sel, uint8_t &window) {
  if (stackTop < 0) { count = sel = window = 0; return nullptr; }
  NavFrame &f = stack[stackTop];
  count = f.count; sel = f.sel; window = f.window;
  return f.list;
}

const char *ui_state_getListTitle() {
  return (stackTop >= 0) ? stack[stackTop].title : "";
}

uint8_t     ui_state_getTrainPhase()        { return ui_backend_trainGetPhase(); }
const char *ui_state_getTrainTarget()       { return ui_backend_trainGetTarget(); }
const char *ui_state_getTrainTyped()        { return ui_backend_trainGetTyped(); }
uint32_t    ui_state_getTrainCorrectCount() { return ui_backend_trainGetCorrectCount(); }
uint32_t    ui_state_getTrainTotalCount()   { return ui_backend_trainGetTotalCount(); }
uint8_t     ui_state_getTrainDrillId()      { return currentTrainDrillId; }
uint8_t     ui_state_getTrainKochLevel()    { return ui_backend_trainGetKochLevel(); }
void        ui_state_getTrainKochCharset(char *out, size_t outSize) { ui_backend_trainGetKochCharset(out, outSize); }
int         ui_state_getTrainAdaptiveWpm()  { return ui_backend_trainGetAdaptiveWpm(); }

uint8_t ui_state_getExamScorePercent() { return ui_backend_trainGetExamScorePercent(); }
uint8_t ui_state_getExamCorrectCount() { return ui_backend_trainGetExamCorrectCount(); }
bool    ui_state_getExamPassed()       { return ui_backend_trainGetExamPassed(); }
uint8_t ui_state_getExamTotalCount()   { return ui_backend_trainGetExamTotalCount(); }
uint8_t ui_state_getExamTargetLength() { return ui_backend_trainGetExamTargetLength(); }

int  ui_state_getFarnsworthWpm()     { return ui_backend_farnsworthGetWpm(); }
bool ui_state_getFarnsworthPlaying() { return ui_backend_isFarnsworthPlaying(); }

uint8_t  ui_state_gameCopyPhase()           { return ui_backend_gameCopyPhase(); }
char     ui_state_gameCopyFallingChar()     { return ui_backend_gameCopyFallingChar(); }
uint8_t  ui_state_gameCopyFallProgressPct() { return ui_backend_gameCopyFallProgressPct(); }
uint16_t ui_state_gameCopyScore()           { return ui_backend_gameCopyScore(); }
uint8_t  ui_state_gameCopyLives()           { return ui_backend_gameCopyLives(); }
uint16_t ui_state_gameCopyHighScore()       { return ui_backend_gameCopyHighScore(); }

uint8_t ui_state_gameMemoryPhase()          { return ui_backend_gameMemoryPhase(); }
uint8_t ui_state_gameMemoryChainLength()    { return ui_backend_gameMemoryChainLength(); }
uint8_t ui_state_gameMemoryInputProgress()  { return ui_backend_gameMemoryInputProgress(); }
uint8_t ui_state_gameMemoryHighScore()      { return ui_backend_gameMemoryHighScore(); }

uint8_t       ui_state_gameSpeedPhase()           { return ui_backend_gameSpeedPhase(); }
uint16_t      ui_state_gameSpeedCombo()           { return ui_backend_gameSpeedCombo(); }
uint8_t       ui_state_gameSpeedLives()           { return ui_backend_gameSpeedLives(); }
unsigned long ui_state_gameSpeedBeatRemainingMs() { return ui_backend_gameSpeedBeatRemainingMs(); }
unsigned long ui_state_gameSpeedBeatTotalMs()     { return ui_backend_gameSpeedBeatTotalMs(); }
bool          ui_state_gameSpeedWasLastCorrect()  { return ui_backend_gameSpeedWasLastCorrect(); }
char          ui_state_gameSpeedLastChar()        { return ui_backend_gameSpeedLastChar(); }
uint16_t      ui_state_gameSpeedHighScore()       { return ui_backend_gameSpeedHighScore(); }
bool          ui_state_isGamePaused()             { return ui_backend_isGamePaused(); }
uint8_t       ui_state_getPauseReturnScreen()     { return (uint8_t)pauseReturnScreen; }
uint8_t       ui_state_getGamePauseFocus()        { return gamePauseFocusIdx; }