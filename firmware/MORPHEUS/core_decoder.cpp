/*
 * ============================================================================
 * MORPHEUS - Morse Decoder Engine
 * ============================================================================
 * File: core_decoder.cpp | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds runtime enable/disable (core_decoder_setEnabled) and
 * a reverse pattern lookup (core_decoder_lookupPattern) reusing the same
 * PROGMEM morseTable already defined below - core_memory.cpp's message
 * player calls this instead of maintaining a second alphabet table.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "core_decoder.h"
#include "config.h"
#include <string.h>

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

static bool decoderEnabled = true;

bool core_decoder_isEnabled() { return decoderEnabled; }

static char charPattern[MAX_PATTERN_LEN];
static uint8_t charPatternLen = 0;
static bool charPending = false;
static unsigned long lastElementEndMs = 0;

static char wordBuffer[MAX_WORD_LEN];
static uint8_t wordLen = 0;
static TrainingCharSink trainingSink = nullptr;

void core_decoder_setTrainingSink(TrainingCharSink sink) { trainingSink = sink; }

void core_decoder_setEnabled(bool enabled) {
  decoderEnabled = enabled;
  if (!enabled) {
    charPattern[0] = '\0';
    charPatternLen = 0;
    charPending = false;
    wordBuffer[0] = '\0';
    wordLen = 0;
  }
}

bool core_decoder_lookupPattern(char ch, char *out, size_t outSize) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
  for (uint8_t i = 0; i < MORSE_TABLE_SIZE; i++) {
    MorseEntry entry;
    memcpy_P(&entry, &morseTable[i], sizeof(MorseEntry));
    if (entry.ch == ch) {
      strncpy_P(out, entry.code, outSize - 1);
      out[outSize - 1] = '\0';
      return true;
    }
  }
  if (outSize > 0) out[0] = '\0';
  return false;
}

static void finalizeCharacter() {
  char decoded = lookupMorse(charPattern);
  if (decoded == 0) decoded = '?';

  if (trainingSink != nullptr) {
    trainingSink(decoded, charPattern);
    // Training active: word buffer/normal events deliberately untouched.
  } else {
    events_onCharacterComplete(decoded, charPattern);
    if (wordLen < MAX_WORD_LEN - 1) { wordBuffer[wordLen++] = decoded; wordBuffer[wordLen] = '\0'; }
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

void core_decoder_addElement(ElementType type, unsigned long now) {
  if (!decoderEnabled) return;
  if (charPatternLen < (uint8_t)(MAX_PATTERN_LEN - 1)) {
    charPattern[charPatternLen++] = (type == ELEM_DIT) ? '.' : '-';
    charPattern[charPatternLen] = '\0';
  }
  lastElementEndMs = now;
  charPending = true;
}

void core_decoder_service(unsigned long now) {
  if (!decoderEnabled) return;
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

void core_decoder_init() {
  charPattern[0] = '\0';
  charPatternLen = 0;
  charPending = false;
  wordBuffer[0] = '\0';
  wordLen = 0;
  lastElementEndMs = millis();
}

const char *core_decoder_getWordBuffer() { return wordBuffer; }
uint8_t core_decoder_getWordLen() { return wordLen; }
const char *core_decoder_getCharPattern() { return charPattern; }
uint8_t core_decoder_getCharPatternLen() { return charPatternLen; }