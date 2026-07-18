/*
 * ============================================================================
 * MORPHEUS - Training Engine
 * ============================================================================
 * File: core_trainer.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Koch Method, Characters, Words, Callsigns, Adaptive, Exam Mode, and
 * Farnsworth practice, built as one shared drill engine + a small
 * Farnsworth sub-feature. Reuses core_morseplayer (playback, shared with
 * core_memory), core_decoder's training sink (scoring - same decode
 * logic as real operation, not duplicated), and core_keyer's gated
 * diagnostic tone path (via core_morseplayer).
 *
 * See file header history / project notes for two documented
 * simplifications: the per-character keyed-response interaction model
 * (a hardware adaptation of Koch method) and the Farnsworth timing
 * approximation (not the full ARRL proportional formula).
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_trainer.h"
#include "core_morseplayer.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "config.h"
#include <string.h>
#include <ctype.h>
#include <esp_system.h>

static const char KOCH_ORDER[] = "KMURESNAPTLWI.JZ=FOY,VGQ5/H38B?47C1D6X92";
static const uint8_t KOCH_ORDER_LEN = sizeof(KOCH_ORDER) - 1;

static const char FULL_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const uint8_t FULL_CHARSET_LEN = sizeof(FULL_CHARSET) - 1;

static const char *TRAIN_WORDS[] = {
  "THE", "AND", "CQ", "DE", "QTH", "RST", "TNX", "UR", "NAME", "HR",
  "WX", "FB", "OM", "GM", "GE", "GN", "73", "SRI", "PSE", "AGN"
};
static const uint8_t TRAIN_WORD_COUNT = sizeof(TRAIN_WORDS) / sizeof(TRAIN_WORDS[0]);

static const char CALLSIGN_PREFIXES[] = "WKNA";

static void generateCallsign(char *out, size_t outSize) {
  uint8_t suffixLen = (uint8_t)random(2, 4);
  uint8_t i = 0;
  if (i < outSize - 1) out[i++] = CALLSIGN_PREFIXES[random(0, 4)];
  if (i < outSize - 1) out[i++] = (char)('0' + random(0, 10));
  for (uint8_t s = 0; s < suffixLen && i < outSize - 1; s++) {
    out[i++] = (char)('A' + random(0, 26));
  }
  out[i] = '\0';
}

static MorsePlayer player;

static TrainMode  currentMode = TRAIN_MODE_CHARACTERS;
static bool       sessionActive = false;
static DrillPhase phase = DRILL_IDLE;
static unsigned long phaseStartMs = 0;

static char    targetBuf[24];
static uint8_t targetLen = 0;
static uint8_t targetPos = 0;
static char    typedBuf[24];
static uint8_t typedLen = 0;

static uint32_t correctCount = 0;
static uint32_t totalCount = 0;

static uint8_t  kochLevel = 2;
static uint16_t kochRollingCorrect = 0;
static uint16_t kochRollingTotal = 0;

static int     adaptiveWpm = 15;
static uint8_t adaptiveStreak = 0;

static uint8_t examCorrectCount = 0;
static uint8_t examTotalCount = 0;
static bool    examResultReady = false;

static MorsePlayer farnsworthPlayer;
static int  farnsworthEffectiveWpm = DEFAULT_FARNSWORTH_WPM;
static bool farnsworthPlaying = false;
static const char *FARNSWORTH_SAMPLE = "PARIS PARIS CQ DE TEST";

static TrainerStatsHook statsHook = nullptr;
static TrainerExamHook  examHook  = nullptr;

static void generateNextTarget() {
  switch (currentMode) {
    case TRAIN_MODE_KOCH: {
      uint8_t n = (kochLevel < 2) ? 2 : kochLevel;
      if (n > KOCH_ORDER_LEN) n = KOCH_ORDER_LEN;
      targetBuf[0] = KOCH_ORDER[random(0, n)];
      targetBuf[1] = '\0';
      break;
    }
    case TRAIN_MODE_WORDS: {
      const char *w = TRAIN_WORDS[random(0, TRAIN_WORD_COUNT)];
      strncpy(targetBuf, w, sizeof(targetBuf) - 1);
      targetBuf[sizeof(targetBuf) - 1] = '\0';
      break;
    }
    case TRAIN_MODE_CALLSIGNS:
      generateCallsign(targetBuf, sizeof(targetBuf));
      break;
    case TRAIN_MODE_CHARACTERS:
    case TRAIN_MODE_ADAPTIVE:
    case TRAIN_MODE_EXAM:
    default:
      targetBuf[0] = FULL_CHARSET[random(0, FULL_CHARSET_LEN)];
      targetBuf[1] = '\0';
      break;
  }
  targetLen = (uint8_t)strlen(targetBuf);
  targetPos = 0;
  typedBuf[0] = '\0';
  typedLen = 0;
}

static void startPlayback() {
  unsigned long ditMs = (currentMode == TRAIN_MODE_ADAPTIVE)
    ? (1200UL / (unsigned long)adaptiveWpm)
    : core_keyer_getDitLengthMs();
  core_morseplayer_start(player, targetBuf, ditMs, ditMs);
  phase = DRILL_PLAYING;
  phaseStartMs = millis();
}

static void advanceRound() { generateNextTarget(); startPlayback(); }

static void maybeLevelUpKoch() {
  if (kochRollingTotal < TRAIN_KOCH_WINDOW) return;
  uint8_t pct = (uint8_t)((kochRollingCorrect * 100UL) / kochRollingTotal);
  kochRollingCorrect = 0;
  kochRollingTotal = 0;
  if (pct >= TRAIN_KOCH_LEVELUP_PCT && kochLevel < KOCH_ORDER_LEN) kochLevel++;
}

void core_trainer_recordKochResult(bool correct) {
  kochRollingTotal++;
  if (correct) kochRollingCorrect++;
  maybeLevelUpKoch();
}

static void adjustAdaptiveWpm(bool correct) {
  if (correct) {
    adaptiveStreak++;
    if (adaptiveStreak >= TRAIN_ADAPTIVE_STREAK) {
      adaptiveStreak = 0;
      if (adaptiveWpm + TRAIN_ADAPTIVE_STEP_UP <= WPM_MAX) adaptiveWpm += TRAIN_ADAPTIVE_STEP_UP;
    }
  } else {
    adaptiveStreak = 0;
    adaptiveWpm -= TRAIN_ADAPTIVE_STEP_DOWN;
    if (adaptiveWpm < WPM_MIN) adaptiveWpm = WPM_MIN;
  }
}

void core_trainer_setStatsHook(TrainerStatsHook hook) { statsHook = hook; }
void core_trainer_setExamCompleteHook(TrainerExamHook hook) { examHook = hook; }

static void onTrainingCharDecoded(char decoded, const char *pattern) {
  (void)pattern;
  if (!sessionActive || phase != DRILL_LISTENING) return;

  if (typedLen < sizeof(typedBuf) - 1) {
    typedBuf[typedLen++] = decoded;
    typedBuf[typedLen] = '\0';
  }

  char expected = targetBuf[targetPos];
  bool correct = (toupper((unsigned char)decoded) == toupper((unsigned char)expected));
  totalCount++;
  if (correct) correctCount++;
  targetPos++;

  if (statsHook != nullptr) statsHook(currentMode, correct);

  if (currentMode == TRAIN_MODE_KOCH) {
    core_trainer_recordKochResult(correct);
  }
  if (currentMode == TRAIN_MODE_ADAPTIVE) adjustAdaptiveWpm(correct);

  bool targetComplete = (targetPos >= targetLen);

  if (currentMode == TRAIN_MODE_EXAM) {
    examTotalCount++;
    if (correct) examCorrectCount++;
    if (examTotalCount >= TRAIN_EXAM_LENGTH) {
      uint8_t scorePercent = (uint8_t)((examCorrectCount * 100UL) / examTotalCount);
      bool passed = scorePercent >= TRAIN_EXAM_PASS_PCT;
      if (examHook != nullptr) examHook(examCorrectCount, examTotalCount, scorePercent, passed);
      examResultReady = true;
      phase = DRILL_EXAM_DONE;
      sessionActive = false;
      core_decoder_setTrainingSink(nullptr);
      core_morseplayer_stop(player);
      return;
    }
    if (targetComplete) advanceRound();
    return;
  }

  if (targetComplete) { phase = DRILL_FEEDBACK; phaseStartMs = millis(); }
}

void core_trainer_startSession(TrainMode mode) {
  if (farnsworthPlaying) core_trainer_farnsworthStop();

  currentMode = mode;
  sessionActive = true;
  correctCount = 0; totalCount = 0;
  kochRollingCorrect = 0; kochRollingTotal = 0;
  examCorrectCount = 0; examTotalCount = 0; examResultReady = false;
  adaptiveStreak = 0;
  if (mode == TRAIN_MODE_ADAPTIVE) adaptiveWpm = core_keyer_getWpm();

  core_decoder_setTrainingSink(onTrainingCharDecoded);
  generateNextTarget();
  startPlayback();
}

void core_trainer_stopSession() {
  sessionActive = false;
  phase = DRILL_IDLE;
  core_morseplayer_stop(player);
  core_decoder_setTrainingSink(nullptr);
}

bool core_trainer_isSessionActive() { return sessionActive; }
TrainMode core_trainer_getMode() { return currentMode; }
DrillPhase core_trainer_getPhase() { return phase; }
const char *core_trainer_getTargetText() { return targetBuf; }
const char *core_trainer_getTypedText() { return typedBuf; }
uint32_t core_trainer_getCorrectCount() { return correctCount; }
uint32_t core_trainer_getTotalCount() { return totalCount; }

void core_trainer_confirmPressed() {
  if (!sessionActive) return;
  switch (phase) {
    case DRILL_PLAYING:
      core_morseplayer_stop(player);
      phase = DRILL_LISTENING;
      phaseStartMs = millis();
      break;
    case DRILL_LISTENING:
      startPlayback();   // replay
      break;
    case DRILL_FEEDBACK:
      advanceRound();
      break;
    default: break;
  }
}

uint8_t core_trainer_getKochLevel() { return kochLevel; }
void core_trainer_setKochLevel(uint8_t level) {
  if (level < 2) level = 2;
  if (level > KOCH_ORDER_LEN) level = KOCH_ORDER_LEN;
  kochLevel = level;
}
void core_trainer_getKochCharset(char *out, size_t outSize) {
  uint8_t n = (kochLevel < 2) ? 2 : kochLevel;
  if (n > KOCH_ORDER_LEN) n = KOCH_ORDER_LEN;
  size_t copyLen = (n < outSize - 1) ? n : outSize - 1;
  strncpy(out, KOCH_ORDER, copyLen);
  out[copyLen] = '\0';
}
void core_trainer_resetKochProgress() {
  kochLevel = DEFAULT_KOCH_LEVEL;
  kochRollingCorrect = 0; kochRollingTotal = 0;
}

int core_trainer_getAdaptiveWpm() { return adaptiveWpm; }

bool core_trainer_isExamResultReady() { return examResultReady; }
uint8_t core_trainer_getExamTotalCount() { return examTotalCount; }
uint8_t core_trainer_getExamCorrectCount() { return examCorrectCount; }
uint8_t core_trainer_getExamTargetLength() { return TRAIN_EXAM_LENGTH; }
uint8_t core_trainer_getExamScorePercent() {
  return (examTotalCount == 0) ? 0 : (uint8_t)((examCorrectCount * 100UL) / examTotalCount);
}
bool core_trainer_getExamPassed() { return core_trainer_getExamScorePercent() >= TRAIN_EXAM_PASS_PCT; }
void core_trainer_clearExamResult() { examResultReady = false; }

void core_trainer_farnsworthSetEffectiveWpm(int wpm) {
  if (wpm < WPM_MIN) wpm = WPM_MIN;
  if (wpm > WPM_MAX) wpm = WPM_MAX;
  farnsworthEffectiveWpm = wpm;
}
int core_trainer_farnsworthGetEffectiveWpm() { return farnsworthEffectiveWpm; }

bool core_trainer_farnsworthTogglePlay() {
  if (farnsworthPlaying) { core_trainer_farnsworthStop(); return false; }
  if (sessionActive) return false;
  unsigned long elementDitMs = core_keyer_getDitLengthMs();
  unsigned long gapDitMs = 1200UL / (unsigned long)farnsworthEffectiveWpm;
  farnsworthPlaying = core_morseplayer_start(farnsworthPlayer, FARNSWORTH_SAMPLE, elementDitMs, gapDitMs);
  return farnsworthPlaying;
}
bool core_trainer_farnsworthIsPlaying() { return farnsworthPlaying; }
void core_trainer_farnsworthStop() {
  core_morseplayer_stop(farnsworthPlayer);
  farnsworthPlaying = false;
}

void core_trainer_init() {
  randomSeed(esp_random());
  core_morseplayer_init(player);
  core_morseplayer_init(farnsworthPlayer);
  kochLevel = DEFAULT_KOCH_LEVEL;
}

void core_trainer_service(unsigned long now) {
  if (sessionActive) {
    switch (phase) {
      case DRILL_PLAYING:
        core_morseplayer_service(player, now);
        if (!core_morseplayer_isActive(player)) { phase = DRILL_LISTENING; phaseStartMs = now; }
        break;
      case DRILL_FEEDBACK:
        if (now - phaseStartMs >= TRAIN_FEEDBACK_MS) advanceRound();
        break;
      default: break;
    }
  }
  if (farnsworthPlaying) {
    core_morseplayer_service(farnsworthPlayer, now);
    if (!core_morseplayer_isActive(farnsworthPlayer)) farnsworthPlaying = false;
  }
}