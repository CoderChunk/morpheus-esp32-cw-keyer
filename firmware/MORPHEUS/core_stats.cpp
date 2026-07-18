/*
 * ============================================================================
 * MORPHEUS - Statistics Engine
 * ============================================================================
 * File: core_stats.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Owns its own NVS namespace ("morpheus_stats"), separate from operator
 * settings - different persistence lifecycle (stats change on every
 * keystroke; settings change rarely), so a longer debounce
 * (STATS_SAVE_DEBOUNCE_MS) protects flash wear without coupling to
 * services.cpp's settings blob.
 *
 * Hooks into core_trainer via the same inversion-of-control pattern as
 * core_decoder's training sink: core_trainer knows nothing about
 * core_stats, it just calls a registered function pointer.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_stats.h"
#include "core_keyer.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>

static const char *STATS_NVS_NAMESPACE = "morpheus_stats";
static const char *STATS_NVS_KEY       = "lifetimeStats";

struct LifetimeStats {
  uint16_t version;
  uint32_t totalCharsKeyed;
  uint32_t totalWordsKeyed;
  uint32_t totalElementsKeyed;
  uint32_t totalUptimeSec;
  uint32_t trainingAttempts;
  uint32_t trainingCorrect;
  uint32_t modeAttempts[6];
  uint32_t modeCorrect[6];
  uint32_t examsTaken;
  uint32_t examsPassed;
  uint8_t  bestExamScorePercent;
  uint16_t peakAdaptiveWpm;
  uint16_t wpmHistory[8];   // physically sized for headroom; only
                            // STATS_WPM_HISTORY_CAP slots are used
  uint8_t  wpmHistoryCount;
  uint8_t  wpmHistoryHead;
};

struct SessionStats {
  uint32_t charsKeyed;
  uint32_t wordsKeyed;
  uint32_t elementsKeyed;
  uint32_t trainingAttempts;
  uint32_t trainingCorrect;
  unsigned long startMs;
};

static Preferences statsPrefs;
static LifetimeStats lifetime;
static SessionStats session;
static bool statsDirty = false;
static unsigned long lastFlushMs = 0;
static unsigned long uptimeCheckpointMs = 0;

static void flushToNvs(unsigned long now) {
  lifetime.totalUptimeSec += (uint32_t)((now - uptimeCheckpointMs) / 1000UL);
  uptimeCheckpointMs = now;
  statsPrefs.putBytes(STATS_NVS_KEY, &lifetime, sizeof(lifetime));
  lastFlushMs = now;
  statsDirty = false;
#if FEATURE_SERIAL
  Serial.println(F("EVT STATS_SAVE"));
#endif
}

static void onTrainerCharScored(TrainMode mode, bool correct) {
  session.trainingAttempts++;
  if (correct) session.trainingCorrect++;
  lifetime.trainingAttempts++;
  if (correct) lifetime.trainingCorrect++;
  uint8_t idx = (uint8_t)mode;
  if (idx < 6) {
    lifetime.modeAttempts[idx]++;
    if (correct) lifetime.modeCorrect[idx]++;
  }
  statsDirty = true;
}

static void onTrainerExamComplete(uint8_t correctCount, uint8_t totalCount, uint8_t scorePercent, bool passed) {
  (void)correctCount; (void)totalCount;
  lifetime.examsTaken++;
  if (passed) lifetime.examsPassed++;
  if (scorePercent > lifetime.bestExamScorePercent) lifetime.bestExamScorePercent = scorePercent;
  statsDirty = true;
}

void core_stats_notifyCharKeyed()    { session.charsKeyed++;    lifetime.totalCharsKeyed++;    statsDirty = true; }
void core_stats_notifyWordKeyed()    { session.wordsKeyed++;    lifetime.totalWordsKeyed++;    statsDirty = true; }
void core_stats_notifyElementKeyed() { session.elementsKeyed++; lifetime.totalElementsKeyed++; statsDirty = true; }

void core_stats_recordSessionStart() {
  int wpm = core_keyer_getWpm();
  lifetime.wpmHistory[lifetime.wpmHistoryHead] = (uint16_t)wpm;
  lifetime.wpmHistoryHead = (uint8_t)((lifetime.wpmHistoryHead + 1) % STATS_WPM_HISTORY_CAP);
  if (lifetime.wpmHistoryCount < STATS_WPM_HISTORY_CAP) lifetime.wpmHistoryCount++;
  statsDirty = true;
}

void core_stats_init() {
  statsPrefs.begin(STATS_NVS_NAMESPACE, false);

  LifetimeStats defaults;
  memset(&defaults, 0, sizeof(defaults));
  defaults.version = STATS_VERSION;

  LifetimeStats loaded = defaults;
  size_t got = statsPrefs.getBytes(STATS_NVS_KEY, &loaded, sizeof(loaded));
  if (got != sizeof(loaded) || loaded.version != STATS_VERSION) loaded = defaults;
  lifetime = loaded;

  memset(&session, 0, sizeof(session));
  session.startMs = millis();
  uptimeCheckpointMs = millis();
  lastFlushMs = millis();
  statsDirty = false;

  core_trainer_setStatsHook(onTrainerCharScored);
  core_trainer_setExamCompleteHook(onTrainerExamComplete);
}

void core_stats_service(unsigned long now) {
  // Peak adaptive WPM - polled rather than hooked, since the stats hook
  // signature (mode, correct) doesn't carry the live adaptive WPM value.
  if (core_trainer_isSessionActive() && core_trainer_getMode() == TRAIN_MODE_ADAPTIVE) {
    int cur = core_trainer_getAdaptiveWpm();
    if (cur > lifetime.peakAdaptiveWpm) { lifetime.peakAdaptiveWpm = (uint16_t)cur; statsDirty = true; }
  }

  if (!statsDirty) return;
  if (now - lastFlushMs < STATS_SAVE_DEBOUNCE_MS) return;
  flushToNvs(now);
}

void core_stats_forceFlush() { flushToNvs(millis()); }

uint32_t core_stats_session_getCharsKeyed()         { return session.charsKeyed; }
uint32_t core_stats_session_getWordsKeyed()         { return session.wordsKeyed; }
uint32_t core_stats_session_getElementsKeyed()      { return session.elementsKeyed; }
uint32_t core_stats_session_getTrainingAttempts()   { return session.trainingAttempts; }
uint32_t core_stats_session_getTrainingCorrect()    { return session.trainingCorrect; }
unsigned long core_stats_session_getUptimeMs()      { return millis() - session.startMs; }

uint32_t core_stats_lifetime_getCharsKeyed()        { return lifetime.totalCharsKeyed; }
uint32_t core_stats_lifetime_getWordsKeyed()        { return lifetime.totalWordsKeyed; }
uint32_t core_stats_lifetime_getElementsKeyed()     { return lifetime.totalElementsKeyed; }
uint32_t core_stats_lifetime_getUptimeSec()         { return lifetime.totalUptimeSec; }
uint32_t core_stats_lifetime_getTrainingAttempts()  { return lifetime.trainingAttempts; }
uint32_t core_stats_lifetime_getTrainingCorrect()   { return lifetime.trainingCorrect; }
uint32_t core_stats_lifetime_getModeAttempts(TrainMode m) { uint8_t i=(uint8_t)m; return (i<6)?lifetime.modeAttempts[i]:0; }
uint32_t core_stats_lifetime_getModeCorrect(TrainMode m)  { uint8_t i=(uint8_t)m; return (i<6)?lifetime.modeCorrect[i]:0; }
uint32_t core_stats_lifetime_getExamsTaken()        { return lifetime.examsTaken; }
uint32_t core_stats_lifetime_getExamsPassed()       { return lifetime.examsPassed; }
uint8_t  core_stats_lifetime_getBestExamScore()     { return lifetime.bestExamScorePercent; }
int      core_stats_lifetime_getPeakAdaptiveWpm()   { return lifetime.peakAdaptiveWpm; }

uint8_t core_stats_history_getCount() { return lifetime.wpmHistoryCount; }
uint16_t core_stats_history_getEntry(uint8_t indexFromNewest) {
  if (indexFromNewest >= lifetime.wpmHistoryCount) return 0;
  int16_t pos = (int16_t)lifetime.wpmHistoryHead - 1 - (int16_t)indexFromNewest;
  while (pos < 0) pos += STATS_WPM_HISTORY_CAP;
  return lifetime.wpmHistory[pos];
}

void core_stats_resetLifetime() {
  memset(&lifetime, 0, sizeof(lifetime));
  lifetime.version = STATS_VERSION;
  statsPrefs.putBytes(STATS_NVS_KEY, &lifetime, sizeof(lifetime));
  statsDirty = false;
}