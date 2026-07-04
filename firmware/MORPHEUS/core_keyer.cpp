/*
 * ============================================================================
 * MORPHEUS - CW Keying Engine
 * ============================================================================
 *
 * File: core_keyer.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * The heart of MORPHEUS.
 *
 * This module converts physical operator input into precise Morse timing.
 * It supports straight keys, iambic paddles, automatic element timing,
 * memory functions, sidetone generation, and state management.
 *
 * Great Morse operators depend on timing.
 * Great firmware guarantees timing.
 *
 * The architecture intentionally separates the keyer from the decoder,
 * display, BLE transport, and diagnostics systems, allowing future
 * contributors to experiment with advanced keying techniques without
 * changing the rest of the project.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "core_keyer.h"
#include "config.h"

// ----------------------------------------------------------------------------
// Sidetone - LEDC PWM, gated by FEATURE_SIDETONE. Compatibility shim
// unchanged from earlier stages.
// ----------------------------------------------------------------------------
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
}

static void buzzerOff() {
#if FEATURE_SIDETONE
  #if USE_LEDC_V3_API
    ledcWriteTone(PIN_BUZZER, 0);
  #else
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
  #endif
#endif
}

// ----------------------------------------------------------------------------
// Internal-only types (not exposed via core_keyer.h)
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Debounce
// ----------------------------------------------------------------------------
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

static Debouncer modeDeb, tipDeb, ringDeb;

// ----------------------------------------------------------------------------
// Keyer state
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Common element interface. elementComplete() ends here - it no longer
// calls into a decoder directly. The decoder finds out about each
// completed element through MORPHEUS.ino's events_onKeyUp() fan-out, which
// is the only thing that knows both this module and core_decoder exist.
// ----------------------------------------------------------------------------
static void elementStart(unsigned long nowMs) {
  buzzerOn(currentSidetoneFreq);
  txActive = true;
  events_onKeyDown(nowMs);
}

static void elementComplete(const KeyElement &ev) {
  buzzerOff();
  txActive = false;
  events_onKeyUp(ev.type, ev.durationMs, ev.thresholdMs, ev.endMs);
}

// ----------------------------------------------------------------------------
// WPM
// ----------------------------------------------------------------------------
static void applyWpm(int wpm) {
  currentWpm = wpm;
  ditLengthMs = 1200UL / (unsigned long)currentWpm;
}

void core_keyer_setWpm(int wpm) {
  if (wpm < WPM_MIN) wpm = WPM_MIN;
  if (wpm > WPM_MAX) wpm = WPM_MAX;
  applyWpm(wpm);
}

// ----------------------------------------------------------------------------
// Sidetone
// ----------------------------------------------------------------------------
uint32_t core_keyer_getSidetoneFreq() { return currentSidetoneFreq; }

void core_keyer_setSidetoneFreq(uint32_t hz) {
  if (hz < SIDETONE_FREQ_MIN_HZ) hz = SIDETONE_FREQ_MIN_HZ;
  if (hz > SIDETONE_FREQ_MAX_HZ) hz = SIDETONE_FREQ_MAX_HZ;
  currentSidetoneFreq = hz;
}

// ----------------------------------------------------------------------------
// Paddle Reverse
// ----------------------------------------------------------------------------
bool core_keyer_getPaddleReversed() { return paddleReversed; }
void core_keyer_setPaddleReversed(bool reversed) { paddleReversed = reversed; }

// ----------------------------------------------------------------------------
// Keyer reset
// ----------------------------------------------------------------------------
static void resetKeyerState() {
  buzzerOff();
  txActive = false;
  keyerPhase = PHASE_IDLE;
  currentElement = ELEM_NONE;
  ditMemory = false;
  dahMemory = false;
}

// ----------------------------------------------------------------------------
// Input handling - straight key
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Input handling - iambic paddle, Mode B
// ----------------------------------------------------------------------------
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
      if (ditPressed) {
        startElement(ELEM_DIT, now);
      } else if (dahPressed) {
        startElement(ELEM_DAH, now);
      }
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
        if (dahMemory) {
          dahMemory = false;
          startElement(ELEM_DAH, now);
        } else if (ditMemory) {
          ditMemory = false;
          startElement(ELEM_DIT, now);
        } else if (ditPressed && dahPressed) {
          startElement(currentElement == ELEM_DIT ? ELEM_DAH : ELEM_DIT, now);
        } else if (ditPressed) {
          startElement(ELEM_DIT, now);
        } else if (dahPressed) {
          startElement(ELEM_DAH, now);
        } else {
          keyerPhase = PHASE_IDLE;
          currentElement = ELEM_NONE;
        }
      }
      break;
  }
}

// ----------------------------------------------------------------------------
// Public getters
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
void core_keyer_init() {
  pinMode(PIN_MODE_SWITCH, INPUT_PULLUP);
  pinMode(PIN_JACK_TIP, INPUT_PULLUP);
  pinMode(PIN_JACK_RING, INPUT_PULLUP);

  debouncerInit(modeDeb, PIN_MODE_SWITCH);
  debouncerInit(tipDeb, PIN_JACK_TIP);
  debouncerInit(ringDeb, PIN_JACK_RING);

  applyWpm(DEFAULT_WPM);

  buzzerInit();

  currentMode = (digitalRead(PIN_MODE_SWITCH) == HIGH) ? MODE_STRAIGHT : MODE_PADDLE;
  resetKeyerState();
}

void core_keyer_service(unsigned long now) {
  bool modeStraight = (debouncerRead(modeDeb) == HIGH);
  bool tipActive    = (debouncerRead(tipDeb)  == LOW);
  bool ringActive   = (debouncerRead(ringDeb) == LOW);

  OperatingMode newMode = modeStraight ? MODE_STRAIGHT : MODE_PADDLE;
  if (newMode != currentMode) {
    currentMode = newMode;
    resetKeyerState();
  }

  if (currentMode == MODE_STRAIGHT) {
    runStraightKey(tipActive, now);
  } else {
    bool ditActive = paddleReversed ? ringActive : tipActive;
    bool dahActive = paddleReversed ? tipActive  : ringActive;
    runIambicPaddle(ditActive, dahActive, now);
  }
}