/*
 * ============================================================================
 * MORPHEUS Standalone UI - Input Driver
 * ============================================================================
 *
 * File: ui_input.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Encoder: interrupt-driven full-quadrature decode. Edges accumulate in
 * an ISR so no detent is lost during the ~25 ms an SH1106 full-frame I2C
 * transfer takes. The 4-state transition table rejects invalid (bounce)
 * transitions inherently, so the encoder needs no time-based debounce.
 *
 * Buttons: polled, 30 ms software debounce, events fired on release.
 * Back distinguishes short press (EV_BACK) from long press (EV_HOME,
 * fired at threshold crossing, release swallowed).
 *
 * Spec rules implemented here [UI_Spec §1]:
 *   - Encoder push and Confirm are indistinguishable aliases (EV_SELECT).
 *   - Only the first of simultaneously held buttons is honored.
 *   - Rotation while any button is held is discarded.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "ui_input.h"
#include "ui_config.h"

// ----------------------------------------------------------------------------
// Event queue - small static ring buffer, ISR-free (filled from task context)
// ----------------------------------------------------------------------------
static const uint8_t QUEUE_SIZE = 8;
static UiEvent queueBuf[QUEUE_SIZE];
static uint8_t queueHead = 0;
static uint8_t queueTail = 0;

static void queuePush(UiEventType type, int8_t detents) {
  uint8_t next = (uint8_t)((queueHead + 1) % QUEUE_SIZE);
  if (next == queueTail) return;  // full: drop newest (should never happen)
  queueBuf[queueHead].type = type;
  queueBuf[queueHead].detents = detents;
  queueHead = next;
}

bool ui_input_poll(UiEvent &ev) {
  if (queueTail == queueHead) return false;
  ev = queueBuf[queueTail];
  queueTail = (uint8_t)((queueTail + 1) % QUEUE_SIZE);
  return true;
}

// ----------------------------------------------------------------------------
// Encoder - ISR quadrature accumulation
// ----------------------------------------------------------------------------
static volatile int32_t encCount = 0;
static volatile uint8_t encPrevState = 0;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

// Transition table: index = (prevState << 2) | newState, where a state is
// (A << 1) | B. Valid transitions yield +/-1; invalid (bounce/skip) yield 0.
static const int8_t QUAD_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

static void IRAM_ATTR encoderIsr() {
  uint8_t state = (uint8_t)((digitalRead(UI_PIN_ENC_A) << 1) |
                             digitalRead(UI_PIN_ENC_B));
  portENTER_CRITICAL_ISR(&encMux);
  encCount += QUAD_TABLE[(encPrevState << 2) | state];
  encPrevState = state;
  portEXIT_CRITICAL_ISR(&encMux);
}

static int32_t encResidual = 0;

// ----------------------------------------------------------------------------
// Buttons
// ----------------------------------------------------------------------------
enum ButtonId { BTN_PUSH = 0, BTN_CONFIRM = 1, BTN_BACK = 2, BTN_COUNT = 3 };

struct UiButton {
  uint8_t       pin;
  int           stableState;
  int           lastFlickerState;
  unsigned long lastChangeMs;
  unsigned long pressStartMs;
  bool          longFired;
};

static UiButton buttons[BTN_COUNT];
static int8_t activeButton = -1;

static void buttonInit(UiButton &b, uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
  b.pin = pin;
  b.stableState = digitalRead(pin);
  b.lastFlickerState = b.stableState;
  b.lastChangeMs = millis();
  b.pressStartMs = 0;
  b.longFired = false;
}

static int buttonRead(UiButton &b, unsigned long now) {
  int raw = digitalRead(b.pin);
  if (raw != b.lastFlickerState) {
    b.lastFlickerState = raw;
    b.lastChangeMs = now;
  }
  if ((now - b.lastChangeMs) >= UI_BUTTON_DEBOUNCE_MS) {
    b.stableState = b.lastFlickerState;
  }
  return b.stableState;
}

static void serviceButton(uint8_t id, unsigned long now) {
  UiButton &b = buttons[id];
  bool wasPressed = (b.pressStartMs != 0);
  bool isPressed  = (buttonRead(b, now) == LOW);

  if (isPressed && !wasPressed) {
    if (activeButton == -1) {
      activeButton = (int8_t)id;
      b.pressStartMs = now;
      b.longFired = false;
    }
    return;
  }

  if (isPressed && wasPressed && activeButton == (int8_t)id) {
    if (id == BTN_BACK && !b.longFired &&
        (now - b.pressStartMs) >= UI_LONG_PRESS_MS) {
      b.longFired = true;
      queuePush(UI_EV_HOME, 0);
    }
    return;
  }

  if (!isPressed && wasPressed) {
    if (activeButton == (int8_t)id) {
      bool shortPress = ((now - b.pressStartMs) < UI_LONG_PRESS_MS);
      if (shortPress) {
        queuePush((id == BTN_BACK) ? UI_EV_BACK : UI_EV_SELECT, 0);
      }
      activeButton = -1;
    }
    b.pressStartMs = 0;
    b.longFired = false;
  }
}

// ----------------------------------------------------------------------------
// Lifecycle / service
// ----------------------------------------------------------------------------
void ui_input_init() {
  pinMode(UI_PIN_ENC_A, INPUT_PULLUP);
  pinMode(UI_PIN_ENC_B, INPUT_PULLUP);
  encPrevState = (uint8_t)((digitalRead(UI_PIN_ENC_A) << 1) |
                            digitalRead(UI_PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(UI_PIN_ENC_A), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(UI_PIN_ENC_B), encoderIsr, CHANGE);

  buttonInit(buttons[BTN_PUSH],    UI_PIN_ENC_PUSH);
  buttonInit(buttons[BTN_CONFIRM], UI_PIN_CONFIRM);
  buttonInit(buttons[BTN_BACK],    UI_PIN_BACK);
}

void ui_input_service(unsigned long now) {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    serviceButton(i, now);
  }

  int32_t taken;
  portENTER_CRITICAL(&encMux);
  taken = encCount;
  encCount = 0;
  portEXIT_CRITICAL(&encMux);

  if (activeButton != -1) {
    encResidual = 0;
    return;
  }

  encResidual += (UI_ENC_REVERSED ? -taken : taken);

  int32_t detents = encResidual / UI_ENC_COUNTS_PER_DETENT;
  if (detents != 0) {
    encResidual -= detents * UI_ENC_COUNTS_PER_DETENT;
    if (detents >  127) detents =  127;
    if (detents < -127) detents = -127;
    queuePush(UI_EV_ROTATE, (int8_t)detents);
  }
}