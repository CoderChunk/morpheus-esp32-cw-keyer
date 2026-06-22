/*
 * ============================================================================
 * MORPHEUS - Morse Decoder Engine
 * ============================================================================
 *
 * File: core_decoder.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * The decoder transforms raw keying elements into meaningful language.
 *
 * Dits and dahs become characters.
 * Characters become words.
 * Words become communication.
 *
 * This module contains the Morse decoding engine, timing analysis,
 * character detection, and word reconstruction logic.
 *
 * Future contributors can extend this subsystem with:
 *
 *   • Koch training
 *   • Farnsworth spacing
 *   • Adaptive timing
 *   • Learning statistics
 *   • Signal quality analysis
 *   • Intelligent decoding algorithms
 *
 * The decoder remains completely isolated from hardware, making it easy
 * to improve and experiment with new decoding techniques.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "core_decoder.h"
#include "config.h"
#include <string.h>

// ----------------------------------------------------------------------------
// Morse lookup table - flash-resident (PROGMEM). This, the char-pattern/
// word-buffer accumulation below, and the gap-timing logic in
// core_decoder_service() are exactly the kind of decoder-internal detail
// this split is meant to isolate: future Koch training, Farnsworth
// spacing, adaptive timing, and learning statistics all land in this file
// (or new files alongside it) without ever touching core_keyer.
// ----------------------------------------------------------------------------
static const char MORSE_A[] PROGMEM = ".-";
static const char MORSE_B[] PROGMEM = "-...";
static const char MORSE_C[] PROGMEM = "-.-.";
static const char MORSE_D[] PROGMEM = "-..";
static const char MORSE_E[] PROGMEM = ".";
static const char MORSE_F[] PROGMEM = "..-.";
static const char MORSE_G[] PROGMEM = "--.";
static const char MORSE_H[] PROGMEM = "....";
static const char MORSE_I[] PROGMEM = "..";
static const char MORSE_J[] PROGMEM = ".---";
static const char MORSE_K[] PROGMEM = "-.-";
static const char MORSE_L[] PROGMEM = ".-..";
static const char MORSE_M[] PROGMEM = "--";
static const char MORSE_N[] PROGMEM = "-.";
static const char MORSE_O[] PROGMEM = "---";
static const char MORSE_P[] PROGMEM = ".--.";
static const char MORSE_Q[] PROGMEM = "--.-";
static const char MORSE_R[] PROGMEM = ".-.";
static const char MORSE_S[] PROGMEM = "...";
static const char MORSE_T[] PROGMEM = "-";
static const char MORSE_U[] PROGMEM = "..-";
static const char MORSE_V[] PROGMEM = "...-";
static const char MORSE_W[] PROGMEM = ".--";
static const char MORSE_X[] PROGMEM = "-..-";
static const char MORSE_Y[] PROGMEM = "-.--";
static const char MORSE_Z[] PROGMEM = "--..";

static const char MORSE_0[] PROGMEM = "-----";
static const char MORSE_1[] PROGMEM = ".----";
static const char MORSE_2[] PROGMEM = "..---";
static const char MORSE_3[] PROGMEM = "...--";
static const char MORSE_4[] PROGMEM = "....-";
static const char MORSE_5[] PROGMEM = ".....";
static const char MORSE_6[] PROGMEM = "-....";
static const char MORSE_7[] PROGMEM = "--...";
static const char MORSE_8[] PROGMEM = "---..";
static const char MORSE_9[] PROGMEM = "----.";

static const char MORSE_PERIOD[]   PROGMEM = ".-.-.-";
static const char MORSE_COMMA[]    PROGMEM = "--..--";
static const char MORSE_QUESTION[] PROGMEM = "..--..";
static const char MORSE_SLASH[]    PROGMEM = "-..-.";
static const char MORSE_EQUAL[]    PROGMEM = "-...-";
static const char MORSE_PLUS[]     PROGMEM = ".-.-.";
static const char MORSE_HYPHEN[]   PROGMEM = "-....-";

struct MorseEntry {
  char ch;
  const char *code;
};

static const MorseEntry morseTable[] PROGMEM = {
  {'A', MORSE_A}, {'B', MORSE_B}, {'C', MORSE_C}, {'D', MORSE_D}, {'E', MORSE_E},
  {'F', MORSE_F}, {'G', MORSE_G}, {'H', MORSE_H}, {'I', MORSE_I}, {'J', MORSE_J},
  {'K', MORSE_K}, {'L', MORSE_L}, {'M', MORSE_M}, {'N', MORSE_N}, {'O', MORSE_O},
  {'P', MORSE_P}, {'Q', MORSE_Q}, {'R', MORSE_R}, {'S', MORSE_S}, {'T', MORSE_T},
  {'U', MORSE_U}, {'V', MORSE_V}, {'W', MORSE_W}, {'X', MORSE_X}, {'Y', MORSE_Y}, {'Z', MORSE_Z},
  {'0', MORSE_0}, {'1', MORSE_1}, {'2', MORSE_2}, {'3', MORSE_3}, {'4', MORSE_4},
  {'5', MORSE_5}, {'6', MORSE_6}, {'7', MORSE_7}, {'8', MORSE_8}, {'9', MORSE_9},
  {'.', MORSE_PERIOD}, {',', MORSE_COMMA}, {'?', MORSE_QUESTION},
  {'/', MORSE_SLASH},  {'=', MORSE_EQUAL}, {'+', MORSE_PLUS}, {'-', MORSE_HYPHEN},
};

static const uint8_t MORSE_TABLE_SIZE = sizeof(morseTable) / sizeof(MorseEntry);

static char lookupMorse(const char *pattern) {
  for (uint8_t i = 0; i < MORSE_TABLE_SIZE; i++) {
    MorseEntry entry;
    memcpy_P(&entry, &morseTable[i], sizeof(MorseEntry));
    char buf[8];
    strncpy_P(buf, entry.code, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (strcmp(buf, pattern) == 0) {
      return entry.ch;
    }
  }
  return 0;
}

// ----------------------------------------------------------------------------
// Decoder state
// ----------------------------------------------------------------------------
static char charPattern[MAX_PATTERN_LEN];
static uint8_t charPatternLen = 0;
static bool charPending = false;
static unsigned long lastElementEndMs = 0;

static char wordBuffer[MAX_WORD_LEN];
static uint8_t wordLen = 0;

// ----------------------------------------------------------------------------
// Character/word finalization - the two places that call into the event
// hooks (defined in MORPHEUS.ino).
// ----------------------------------------------------------------------------
static void finalizeCharacter() {
  char decoded = lookupMorse(charPattern);
  if (decoded == 0) decoded = '?';

  events_onCharacterComplete(decoded, charPattern);

  if (wordLen < MAX_WORD_LEN - 1) {
    wordBuffer[wordLen++] = decoded;
    wordBuffer[wordLen] = '\0';
  }

  charPattern[0] = '\0';
  charPatternLen = 0;
  charPending = false;
}

static void finalizeWord() {
  if (wordLen == 0) return;
  events_onWordComplete(wordBuffer, lastElementEndMs);
  wordBuffer[0] = '\0';
  wordLen = 0;
}

// ----------------------------------------------------------------------------
// Feed path + service tick
// ----------------------------------------------------------------------------
void core_decoder_addElement(ElementType type, unsigned long now) {
  if (charPatternLen < (uint8_t)(MAX_PATTERN_LEN - 1)) {
    charPattern[charPatternLen++] = (type == ELEM_DIT) ? '.' : '-';
    charPattern[charPatternLen] = '\0';
  }
  lastElementEndMs = now;
  charPending = true;
}

void core_decoder_service(unsigned long now) {
  // Mirrors the original txActive guard: don't evaluate gap timeouts while
  // a key element is actively being sent - there's no real "silence" to
  // measure yet, and lastElementEndMs isn't meaningful mid-element.
  if (core_keyer_isTxActive()) return;

  unsigned long ditLengthMs = core_keyer_getDitLengthMs();
  unsigned long silence = now - lastElementEndMs;

  if (charPending && silence >= (unsigned long)(ditLengthMs * CHAR_GAP_MULT)) {
    finalizeCharacter();
  }
  if (!charPending && wordLen > 0 && silence >= (unsigned long)(ditLengthMs * WORD_GAP_MULT)) {
    finalizeWord();
  }
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
void core_decoder_init() {
  charPattern[0] = '\0';
  charPatternLen = 0;
  charPending = false;
  wordBuffer[0] = '\0';
  wordLen = 0;
  lastElementEndMs = millis();
}

// ----------------------------------------------------------------------------
// Public getters
// ----------------------------------------------------------------------------
const char *core_decoder_getWordBuffer() { return wordBuffer; }
uint8_t core_decoder_getWordLen() { return wordLen; }
const char *core_decoder_getCharPattern() { return charPattern; }
uint8_t core_decoder_getCharPatternLen() { return charPatternLen; }