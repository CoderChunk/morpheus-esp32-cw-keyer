/*
 * ============================================================================
 * MORPHEUS - Software Clock (Option A: manual set, no RTC)
 * ============================================================================
 * File: core_clock.cpp | Author: Coder Chunk | License: GPLv3
 *
 * No RTC/NTP on this hardware. Clock is a plain millis() offset from the
 * last manually-entered value - accurate within a session, drifts at
 * the ESP32's internal-oscillator rate, and is lost on every reboot by
 * design (surfaced to the user via core_clock_isSet(), not hidden).
 *
 * Deliberately not persisted to NVS: a stored wall-clock time becomes
 * wrong the instant the device is powered off for any length of time,
 * and silently showing a stale-but-plausible date/time on boot would be
 * worse than honestly showing "not set."
 *
 * Uses a simple proleptic Gregorian day-count (not <time.h>'s mktime,
 * to avoid pulling in the full libc time zone machinery for one
 * feature) - valid for any date this device will plausibly see.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_clock.h"
#include "config.h"
#include <stdio.h>

static bool clockIsSet = false;
static unsigned long entryMillis = 0;
static uint32_t entryEpochSec = 0;
static uint8_t dateFormatIndex = DEFAULT_DATE_FORMAT;
static uint8_t timeFormatIndex = DEFAULT_TIME_FORMAT;

static const uint8_t DAYS_IN_MONTH[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

static bool isLeapYear(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static uint32_t toEpochSeconds(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  uint32_t days = 0;
  for (uint16_t y = 1970; y < year; y++) days += isLeapYear(y) ? 366 : 365;
  for (uint8_t m = 1; m < month; m++) {
    days += DAYS_IN_MONTH[m - 1];
    if (m == 2 && isLeapYear(year)) days += 1;
  }
  days += (day - 1);
  return (uint32_t)days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL;
}

static void fromEpochSeconds(uint32_t epoch, uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute) {
  uint32_t days = epoch / 86400UL;
  uint32_t rem = epoch % 86400UL;
  hour = (uint8_t)(rem / 3600UL);
  minute = (uint8_t)((rem % 3600UL) / 60UL);

  year = 1970;
  for (;;) {
    uint16_t yLen = isLeapYear(year) ? 366 : 365;
    if (days < yLen) break;
    days -= yLen;
    year++;
  }
  month = 1;
  for (;;) {
    uint8_t mLen = DAYS_IN_MONTH[month - 1];
    if (month == 2 && isLeapYear(year)) mLen += 1;
    if (days < mLen) break;
    days -= mLen;
    month++;
  }
  day = (uint8_t)(days + 1);
}

static uint32_t currentEpoch() {
  if (!clockIsSet) return 0;
  return entryEpochSec + (uint32_t)((millis() - entryMillis) / 1000UL);
}

void core_clock_init() {
  clockIsSet = false;
  entryMillis = millis();
  entryEpochSec = 0;
}

void core_clock_setDateFormat(uint8_t index) {
  if (index >= DATE_FORMAT_COUNT) index = DEFAULT_DATE_FORMAT;
  dateFormatIndex = index;
}
uint8_t core_clock_getDateFormat() { return dateFormatIndex; }

void core_clock_setTimeFormat(uint8_t index) {
  if (index >= TIME_FORMAT_COUNT) index = DEFAULT_TIME_FORMAT;
  timeFormatIndex = index;
}
uint8_t core_clock_getTimeFormat() { return timeFormatIndex; }

void core_clock_set(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  entryEpochSec = toEpochSeconds(year, month, day, hour, minute);
  entryMillis = millis();
  clockIsSet = true;
}

bool core_clock_isSet() { return clockIsSet; }

void core_clock_getComponents(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute) {
  if (!clockIsSet) { year = 2026; month = 1; day = 1; hour = 0; minute = 0; return; }
  fromEpochSeconds(currentEpoch(), year, month, day, hour, minute);
}

void core_clock_getDateStr(char *out, size_t outSize) {
  if (!clockIsSet) { snprintf(out, outSize, "----- --"); return; }
  uint16_t y; uint8_t mo, d, h, mi;
  fromEpochSeconds(currentEpoch(), y, mo, d, h, mi);
  switch (dateFormatIndex) {
    case 1: snprintf(out, outSize, "%02u-%02u-%04u", (unsigned)d, (unsigned)mo, (unsigned)y); break;
    case 2: snprintf(out, outSize, "%02u-%02u-%04u", (unsigned)mo, (unsigned)d, (unsigned)y); break;
    default: snprintf(out, outSize, "%04u-%02u-%02u", (unsigned)y, (unsigned)mo, (unsigned)d); break;
  }
}

void core_clock_getTimeStr(char *out, size_t outSize) {
  if (!clockIsSet) { snprintf(out, outSize, "--:--"); return; }
  uint16_t y; uint8_t mo, d, h, mi;
  fromEpochSeconds(currentEpoch(), y, mo, d, h, mi);
  if (timeFormatIndex == 1) {
    uint8_t h12 = h % 12;
    if (h12 == 0) h12 = 12;
    const char *ampm = (h < 12) ? "AM" : "PM";
    snprintf(out, outSize, "%02u:%02u %s", (unsigned)h12, (unsigned)mi, ampm);
  } else {
    snprintf(out, outSize, "%02u:%02u", (unsigned)h, (unsigned)mi);
  }
}