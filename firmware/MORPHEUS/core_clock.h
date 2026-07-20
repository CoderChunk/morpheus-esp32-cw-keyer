#ifndef MORPHEUS_CORE_CLOCK_H
#define MORPHEUS_CORE_CLOCK_H
#include <Arduino.h>

void core_clock_init();

// Sets the clock to the given calendar values (seconds always 0).
// month: 1-12, day: 1-31 (not validated against actual days-in-month -
// operator is trusted to enter a sane date, same as every other numeric
// editor in this UI).
void core_clock_set(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

bool core_clock_isSet();   // false until core_clock_set() has been called this session

void core_clock_getDateStr(char *out, size_t outSize);   // "YYYY-MM-DD"
void core_clock_getTimeStr(char *out, size_t outSize);   // "HH:MM"

// Current calendar values, for the editor's live redraw while adjusting
// (editor keeps its own pending values separately - see ui_state.cpp -
// these getters are for reading the currently-committed clock only).
void core_clock_getComponents(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute);
void core_clock_setDateFormat(uint8_t index);
uint8_t core_clock_getDateFormat();
void core_clock_setTimeFormat(uint8_t index);
uint8_t core_clock_getTimeFormat();

#endif