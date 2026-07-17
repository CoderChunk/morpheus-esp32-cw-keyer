/*
 * ============================================================================
 * MORPHEUS - CW Keying Engine
 * ============================================================================
 *
 * File: core_keyer.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * HARDWARE SIMPLIFICATION NOTE: the physical Mode Switch (formerly
 * GPIO33) is no longer read. Mode is now explicitly set via
 * core_keyer_setMode(), called from the UI layer (toggle editor commit)
 * and from services_loadSettings() at boot (persisted value). The
 * per-tick debounce/compare logic that used to poll the switch is
 * removed entirely - resetKeyerState() still runs on any actual mode
 * change, preserving the same "never leave a mid-element keying state
 * behind a mode flip" guarantee as before.
 *
 * DEBUG_KEYER_TONE instrumentation (ongoing buzzer investigation)
 * preserved unchanged.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "core_keyer.h"
#include "config.h"
#include "services.h"

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define USE_LEDC_V3_API 1
#else
  #define USE_LEDC_V3_API 0
#endif

#if !USE_LEDC_V3_API
static const int BUZZER_LEDC_CHANNEL = 0;
static const int BUZZER_LEDC_RES_BITS = 10;
#endif

static void buzzerInit() {
#if FEATURE_SIDETONE
  #if USE_LEDC_V3_API
    ledcAttach(PIN_BUZZER, TONE_FREQ_HZ, 10);
    ledcWriteTone(PIN_BUZZER, 0);
  #else
    ledcSetup(BUZZER_LEDC_CHANNEL, TONE_FREQ_HZ, BUZZER_LEDC_RES_BITS);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
  #endif
#endif
}

static void buzzerOn(uint32_t freqHz) {
#if FEATURE_SIDETONE
  #if USE_LEDC_V3_API
    ledcWriteTone(PIN_BUZZER, freqHz);
  #else
    ledcWriteTone(BUZZER_LEDC_CHANNEL, freqHz);
  #endif
#else
  (void)freqHz;
#endif
#if DEBUG_KEYER_TONE && FEATURE_SERIAL
  Serial.print(F("[DBG_TONE] BUZZER_ON  freq=")); Serial.print(freqHz);
  Serial.print(F("Hz t=")); Serial.println(millis());
#endif
}

static void buzzerOff() {
#if FEATURE_SIDETONE
  #if USE_LEDC_V3_API
    ledcWriteTone(PIN_BUZZER, 0);
  #else
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
  #endif
#endif
#if DEBUG_KEYER_TONE && FEATURE_SERIAL
  Serial.print(F("[DBG_TONE] BUZZER_OFF t=")); Serial.println(millis());
#endif
}

enum KeyerPhase { PHASE_IDLE, PHASE_SENDING, PHASE_SPACING };

struct Debouncer {
  uint8_t pin;
  int stableState;
  int lastFlickerState;
  unsigned long lastChangeMs;
};

struct KeyElement {
  ElementType type;
  unsigned long durationMs;
  unsigned long thresholdMs;
  unsigned long endMs;
};

static void debouncerInit(Debouncer &d, uint8_t pin) {
  d.pin = pin;
  d.stableState = digitalRead(pin);
  d.lastFlickerState = d.stableState;
  d.lastChangeMs = millis();
}

static int debouncerRead(Debouncer &d) {
  int raw = digitalRead(d.pin);
  unsigned long now = millis();
  if (raw != d.lastFlickerState) {
    d.lastFlickerState = raw;
    d.lastChangeMs = now;
  }
  if ((now - d.lastChangeMs) >= DEBOUNCE_MS) {
    d.stableState = d.lastFlickerState;
  }
  return d.stableState;
}

static Debouncer tipDeb, ringDeb;   // modeDeb removed

static OperatingMode currentMode = MODE_STRAIGHT;

static ElementType   currentElement    = ELEM_NONE;
static KeyerPhase    keyerPhase        = PHASE_IDLE;
static unsigned long phaseStartMs      = 0;
static unsigned long currentElementMs  = 0;
static bool ditMemory = false;
static bool dahMemory = false;

static bool txActive = false;
static unsigned long keyDownStartMs = 0;

static int currentWpm = DEFAULT_WPM;
static unsigned long ditLengthMs = 1200UL / (unsigned long)DEFAULT_WPM;

static uint32_t currentSidetoneFreq = TONE_FREQ_HZ;
static bool     paddleReversed      = DEFAULT_PADDLE_REVERSED;

static bool lastTipActive  = false;
static bool lastRingActive = false;
static bool diagToneActive = false;

static void elementStart(unsigned long nowMs) {
  if (diagToneActive) diagToneActive = false;
  buzzerOn(currentSidetoneFreq);
  txActive = true;
  events_onKeyDown(nowMs);
}

static void elementComplete(const KeyElement &ev) {
  buzzerOff();
  txActive = false;
  events_onKeyUp(ev.type, ev.durationMs, ev.thresholdMs, ev.endMs);
}

static void applyWpm(int wpm) {
  currentWpm = wpm;
  ditLengthMs = 1200UL / (unsigned long)currentWpm;
}

void core_keyer_setWpm(int wpm) {
  if (wpm < WPM_MIN) wpm = WPM_MIN;
  if (wpm > WPM_MAX) wpm = WPM_MAX;
  applyWpm(wpm);
}

uint32_t core_keyer_getSidetoneFreq() { return currentSidetoneFreq; }

void core_keyer_setSidetoneFreq(uint32_t hz) {
  if (hz < SIDETONE_FREQ_MIN_HZ) hz = SIDETONE_FREQ_MIN_HZ;
  if (hz > SIDETONE_FREQ_MAX_HZ) hz = SIDETONE_FREQ_MAX_HZ;
#if DEBUG_KEYER_TONE && FEATURE_SERIAL
  if (hz != currentSidetoneFreq) {
    Serial.print(F("[DBG_TONE] FREQ_CHANGE old=")); Serial.print(currentSidetoneFreq);
    Serial.print(F(" new=")); Serial.print(hz);
    Serial.print(F(" t=")); Serial.println(millis());
  }
#endif
  currentSidetoneFreq = hz;
}

bool core_keyer_getPaddleReversed() { return paddleReversed; }
void core_keyer_setPaddleReversed(bool reversed) { paddleReversed = reversed; }

static void resetKeyerState() {
  buzzerOff();
  txActive = false;
  diagToneActive = false;
  keyerPhase = PHASE_IDLE;
  currentElement = ELEM_NONE;
  ditMemory = false;
  dahMemory = false;
}

// ----------------------------------------------------------------------------
// Mode - now explicitly set, not hardware-polled
// ----------------------------------------------------------------------------
void core_keyer_setMode(OperatingMode mode) {
  if (mode != currentMode) {
    currentMode = mode;
    resetKeyerState();
  }
}

static void runStraightKey(bool keyDown, unsigned long now) {
  if (keyDown && !txActive) {
    keyDownStartMs = now;
    elementStart(now);
  } else if (!keyDown && txActive) {
    unsigned long durMs = now - keyDownStartMs;
    unsigned long threshold =
        (unsigned long)(ditLengthMs * STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT);
    ElementType classified = (durMs < threshold) ? ELEM_DIT : ELEM_DAH;
    KeyElement ev = { classified, durMs, threshold, now };
    elementComplete(ev);
  }
}

static void startElement(ElementType e, unsigned long now) {
  currentElement = e;
  currentElementMs = (e == ELEM_DIT) ? ditLengthMs : (ditLengthMs * 3UL);
  keyerPhase = PHASE_SENDING;
  phaseStartMs = now;
  elementStart(now);
}

static void runIambicPaddle(bool ditPressed, bool dahPressed, unsigned long now) {
  switch (keyerPhase) {
    case PHASE_IDLE:
      if (ditPressed) startElement(ELEM_DIT, now);
      else if (dahPressed) startElement(ELEM_DAH, now);
      break;

    case PHASE_SENDING:
      if (currentElement == ELEM_DIT && dahPressed) dahMemory = true;
      if (currentElement == ELEM_DAH && ditPressed) ditMemory = true;

      if (now - phaseStartMs >= currentElementMs) {
        unsigned long durMs = now - phaseStartMs;
        unsigned long threshold =
            (unsigned long)(ditLengthMs * STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT);
        KeyElement ev = { currentElement, durMs, threshold, now };
        elementComplete(ev);
        keyerPhase = PHASE_SPACING;
        phaseStartMs = now;
      }
      break;

    case PHASE_SPACING:
      if (currentElement == ELEM_DIT && dahPressed) dahMemory = true;
      if (currentElement == ELEM_DAH && ditPressed) ditMemory = true;

      if (now - phaseStartMs >= ditLengthMs) {
        if (dahMemory) { dahMemory = false; startElement(ELEM_DAH, now); }
        else if (ditMemory) { ditMemory = false; startElement(ELEM_DIT, now); }
        else if (ditPressed && dahPressed) startElement(currentElement == ELEM_DIT ? ELEM_DAH : ELEM_DIT, now);
        else if (ditPressed) startElement(ELEM_DIT, now);
        else if (dahPressed) startElement(ELEM_DAH, now);
        else { keyerPhase = PHASE_IDLE; currentElement = ELEM_NONE; }
      }
      break;
  }
}

OperatingMode core_keyer_getMode() { return currentMode; }
int core_keyer_getWpm() { return currentWpm; }
unsigned long core_keyer_getDitLengthMs() { return ditLengthMs; }
bool core_keyer_isTxActive() { return txActive; }

KeyState core_keyer_getKeyState(unsigned long now) {
  if (!txActive) return STATE_IDLE;
  if (currentMode == MODE_PADDLE) {
    return (currentElement == ELEM_DAH) ? STATE_DAH : STATE_DIT;
  }
  unsigned long heldMs = now - keyDownStartMs;
  unsigned long threshold =
      (unsigned long)(ditLengthMs * STRAIGHT_KEY_CLASSIFY_THRESHOLD_MULT);
  return (heldMs < threshold) ? STATE_DIT : STATE_DAH;
}

bool core_keyer_isTipActive()  { return lastTipActive; }
bool core_keyer_isRingActive() { return lastRingActive; }

bool core_keyer_diagToneStart(uint32_t hz) {
  if (txActive) return false;
  if (hz < SIDETONE_FREQ_MIN_HZ) hz = SIDETONE_FREQ_MIN_HZ;
  if (hz > SIDETONE_FREQ_MAX_HZ) hz = SIDETONE_FREQ_MAX_HZ;
  buzzerOn(hz);
  diagToneActive = true;
  return true;
}

void core_keyer_diagToneStop() {
  if (diagToneActive) { buzzerOff(); diagToneActive = false; }
}

bool core_keyer_isDiagToneActive() { return diagToneActive; }

void core_keyer_init() {
  // Mode Switch pin removed - no pinMode/debouncer for it anymore.
  pinMode(PIN_JACK_TIP, INPUT_PULLUP);
  pinMode(PIN_JACK_RING, INPUT_PULLUP);

  debouncerInit(tipDeb, PIN_JACK_TIP);
  debouncerInit(ringDeb, PIN_JACK_RING);

  applyWpm(DEFAULT_WPM);
  buzzerInit();

  // currentMode keeps its static-initializer default (MODE_STRAIGHT) until
  // services_loadSettings() restores the persisted value.
  resetKeyerState();
}

#if DEBUG_KEYER_TONE && FEATURE_SERIAL
static unsigned long toneHeartbeatLastMs = 0;
static uint32_t       toneHeartbeatLoopCount = 0;
static const unsigned long TONE_HEARTBEAT_INTERVAL_MS = 500;
#endif

void core_keyer_service(unsigned long now) {
#if DEBUG_KEYER_TONE && FEATURE_SERIAL
  toneHeartbeatLoopCount++;
  if (txActive && (now - toneHeartbeatLastMs >= TONE_HEARTBEAT_INTERVAL_MS)) {
    Serial.print(F("[DBG_TONE] HOLD_HB t=")); Serial.print(now);
    Serial.print(F(" mode=")); Serial.print(currentMode == MODE_STRAIGHT ? "STR" : "PAD");
    Serial.print(F(" elem=")); Serial.print(
        currentElement == ELEM_DIT ? "DIT" : currentElement == ELEM_DAH ? "DAH" : "-");
    Serial.print(F(" freq=")); Serial.print(currentSidetoneFreq);
    Serial.print(F(" loops/500ms=")); Serial.print(toneHeartbeatLoopCount);
    Serial.print(F(" freeHeap=")); Serial.println(services_getFreeHeapBytes());
    toneHeartbeatLastMs = now;
    toneHeartbeatLoopCount = 0;
  } else if (!txActive) {
    toneHeartbeatLastMs = now;
    toneHeartbeatLoopCount = 0;
  }
#endif

  bool tipActive  = (debouncerRead(tipDeb)  == LOW);
  bool ringActive = (debouncerRead(ringDeb) == LOW);
  lastTipActive  = tipActive;
  lastRingActive = ringActive;

  // No per-tick mode polling anymore - currentMode only changes via
  // explicit core_keyer_setMode() calls (UI/settings-driven).

  if (currentMode == MODE_STRAIGHT) {
    runStraightKey(tipActive, now);
  } else {
    bool ditActive = paddleReversed ? ringActive : tipActive;
    bool dahActive = paddleReversed ? tipActive  : ringActive;
    runIambicPaddle(ditActive, dahActive, now);
  }
}