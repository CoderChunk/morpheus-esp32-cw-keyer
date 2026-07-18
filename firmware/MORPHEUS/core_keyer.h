#ifndef MORPHEUS_CORE_KEYER_H
#define MORPHEUS_CORE_KEYER_H

#include <Arduino.h>

enum OperatingMode { MODE_STRAIGHT, MODE_PADDLE };
enum ElementType   { ELEM_NONE, ELEM_DIT, ELEM_DAH };
enum KeyState      { STATE_IDLE, STATE_DIT, STATE_DAH };

void core_keyer_init();
void core_keyer_service(unsigned long now);

OperatingMode core_keyer_getMode();
int           core_keyer_getWpm();
unsigned long core_keyer_getDitLengthMs();
bool          core_keyer_isTxActive();
KeyState      core_keyer_getKeyState(unsigned long now);
uint32_t      core_keyer_getSidetoneFreq();
bool          core_keyer_getPaddleReversed();

void core_keyer_setWpm(int wpm);
void core_keyer_setSidetoneFreq(uint32_t hz);
void core_keyer_setPaddleReversed(bool reversed);

// Mode is now explicitly set - no longer polled from a hardware switch.
// Resets in-progress keyer state on any actual change, same as the old
// hardware-switch-driven behavior.
void core_keyer_setMode(OperatingMode mode);

// Diagnostics-only additions (unchanged from prior pass)
bool core_keyer_isTipActive();
bool core_keyer_isRingActive();
bool core_keyer_diagToneStart(uint32_t hz);
void core_keyer_diagToneStop();
bool core_keyer_isDiagToneActive();

void events_onKeyDown(unsigned long now);
void events_onKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now);

uint8_t core_keyer_getVolume();
void    core_keyer_setVolume(uint8_t percent);
bool    core_keyer_getSidetoneEnabled();
void    core_keyer_setSidetoneEnabled(bool enabled);

#endif // MORPHEUS_CORE_KEYER_H