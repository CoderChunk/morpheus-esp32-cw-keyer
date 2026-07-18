/*
 * ============================================================================
 * MORPHEUS - Training Games Engine
 * ============================================================================
 * File: core_games.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Three arcade-style ear-training games, all drawing characters from
 * core_trainer's shared Koch charset/level (playing games advances real
 * Koch progression, not a parallel copy of it), all reusing
 * core_morseplayer for playback and core_decoder's training sink for
 * scoring - same reuse pattern as core_trainer/core_memory. Mutually
 * exclusive with core_trainer sessions (single decoder sink consumer at
 * a time, same convention core_trainer already established).
 *
 * File organized bottom-up (state -> helpers -> per-game logic ->
 * dispatcher -> public API) so every static helper is defined before its
 * first use in this translation unit.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_games.h"
#include "core_morseplayer.h"
#include "core_trainer.h"
#include "core_decoder.h"
#include "core_keyer.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>
#include <ctype.h>

static GameId activeGame = GAME_NONE;
static MorsePlayer gamePlayer;   // shared - games are mutually exclusive
static bool paused = false;
static unsigned long pauseStartMs = 0;
static unsigned long pauseAccumMs = 0;

struct GameScores {
  uint16_t version;
  uint16_t copyHigh;
  uint8_t  memoryHigh;
  uint16_t speedHigh;
};

// All phase-timer comparisons in this file read "elapsed" via now() minus
// a stored start time - this returns a pause-adjusted "now" so every
// existing elapsed-time check (copySpawnMs, spdBeatStartMs, etc.) is
// pause-correct with zero changes to their own logic.
static unsigned long effectiveNow(unsigned long now) {
  if (paused) return pauseStartMs - pauseAccumMs;
  return now - pauseAccumMs;
}

static Preferences gamesPrefs;
static GameScores scores;

static void loadScores() {
  gamesPrefs.begin("morpheus_games", false);
  GameScores defaults = { GAMES_VERSION, 0, 0, 0 };
  GameScores loaded = defaults;
  size_t got = gamesPrefs.getBytes("scores", &loaded, sizeof(loaded));
  if (got != sizeof(loaded) || loaded.version != GAMES_VERSION) loaded = defaults;
  scores = loaded;
}
static void saveScores() { gamesPrefs.putBytes("scores", &scores, sizeof(scores)); }

static char pickKoch() {
  char buf[24];
  core_trainer_getKochCharset(buf, sizeof(buf));
  uint8_t n = (uint8_t)strlen(buf);
  return buf[random(0, n)];
}

// Farnsworth-style reaction window: full at low Koch levels, tightens
// as level rises - ties difficulty to real progression rather than a
// separate parallel knob.
static unsigned long reactionBudgetMs() {
  uint8_t level = core_trainer_getKochLevel();
  long budget = 3000L - (long)level * 60L;
  if (budget < 900L) budget = 900L;
  return (unsigned long)budget;
}

// Farnsworth-style gap dit-equivalent for Memory Challenge's inter-
// character spacing only - character speed (element timing) stays at
// the real dit length, untouched.
static unsigned long memGapDitMs() {
  uint8_t level = core_trainer_getKochLevel();
  int effWpm = 18 - (level / 4);
  if (effWpm < 8) effWpm = 8;
  if (effWpm > 18) effWpm = 18;
  return 1200UL / (unsigned long)effWpm;
}

// ----------------------------------------------------------------------------
// Copy Challenge - falling orb, target hidden until feedback
// ----------------------------------------------------------------------------
static CopyPhase copyPhase = COPY_IDLE;
static char copyChar = 'K';
static unsigned long copySpawnMs = 0;
static unsigned long copyFallMs = 1500;
static unsigned long copyPhaseUntilMs = 0;
static uint16_t copyScore = 0;
static uint8_t copyLives = 0;
static uint8_t copyStreak = 0;

static void copySpawnNext(unsigned long now) {
  copyChar = pickKoch();
  char single[2] = { copyChar, '\0' };
  unsigned long ditMs = core_keyer_getDitLengthMs();
  core_morseplayer_start(gamePlayer, single, ditMs, ditMs);
  copySpawnMs = now;
  copyFallMs = reactionBudgetMs();
  copyPhase = COPY_FALLING;
}

static void copyHandleChar(char decoded) {
  if (copyPhase != COPY_FALLING) return;
  bool correct = (toupper((unsigned char)decoded) == toupper((unsigned char)copyChar));
  core_trainer_recordKochResult(correct);
  if (correct) {
    copyScore = (uint16_t)(copyScore + 10 + copyStreak);
    copyStreak++;
    copyPhase = COPY_HIT;
  } else {
    copyStreak = 0;
    if (copyLives > 0) copyLives--;
    copyPhase = COPY_MISS;
  }
  copyPhaseUntilMs = millis() + GAME_FEEDBACK_MS;
}

static void copyServiceTick(unsigned long now) {
  if (copyPhase == COPY_FALLING) {
    if (now - copySpawnMs >= copyFallMs) {
      copyStreak = 0;
      if (copyLives > 0) copyLives--;
      copyPhase = COPY_MISS;
      copyPhaseUntilMs = now + GAME_FEEDBACK_MS;
    }
  } else if (copyPhase == COPY_HIT || copyPhase == COPY_MISS) {
    if (now >= copyPhaseUntilMs) {
      if (copyLives == 0) {
        copyPhase = COPY_OVER;
        if (copyScore > scores.copyHigh) { scores.copyHigh = copyScore; saveScores(); }
      } else {
        copySpawnNext(now);
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Memory Challenge - Simon-style echo chain, tiles only, never text
// ----------------------------------------------------------------------------
static MemGamePhase memPhase = MEM_IDLE;
static char memChain[GAME_MEMORY_MAX_CHAIN + 1];
static uint8_t memChainLen = 0;
static uint8_t memInputPos = 0;
static unsigned long memRoundOkUntilMs = 0;

static void memoryPlayChain(unsigned long now) {
  (void)now;
  unsigned long ditMs = core_keyer_getDitLengthMs();
  unsigned long gapMs = memGapDitMs();
  core_morseplayer_start(gamePlayer, memChain, ditMs, gapMs);
  memPhase = MEM_PLAYBACK;
}

static void memoryAdvanceRound(unsigned long now) {
  if (memChainLen < GAME_MEMORY_MAX_CHAIN) {
    memChain[memChainLen] = pickKoch();
    memChainLen++;
    memChain[memChainLen] = '\0';
  }
  memInputPos = 0;
  memoryPlayChain(now);
}

static void memoryHandleChar(char decoded) {
  if (memPhase != MEM_INPUT) return;
  char expected = memChain[memInputPos];
  bool correct = (toupper((unsigned char)decoded) == toupper((unsigned char)expected));
  core_trainer_recordKochResult(correct);
  if (!correct) {
    uint8_t reached = (memChainLen > 0) ? (uint8_t)(memChainLen - 1) : 0;
    if (reached > scores.memoryHigh) { scores.memoryHigh = reached; saveScores(); }
    memPhase = MEM_OVER;
    return;
  }
  memInputPos++;
  if (memInputPos >= memChainLen) {
    memPhase = MEM_ROUND_OK;
    memRoundOkUntilMs = millis() + GAME_ROUND_OK_MS;
  }
}

static void memoryServiceTick(unsigned long now) {
  if (memPhase == MEM_PLAYBACK) {
    if (!core_morseplayer_isActive(gamePlayer)) { memPhase = MEM_INPUT; memInputPos = 0; }
  } else if (memPhase == MEM_ROUND_OK) {
    if (now >= memRoundOkUntilMs) memoryAdvanceRound(now);
  }
}

// ----------------------------------------------------------------------------
// Speed Challenge - Overdrive: fixed-tempo beats, shrinking interval
// ----------------------------------------------------------------------------
static SpeedGamePhase spdPhase = SPD_IDLE;
static char spdChar = 'K';
static unsigned long spdBeatStartMs = 0;
static unsigned long spdBeatMs = 1400;
static unsigned long spdFeedbackUntilMs = 0;
static bool spdLastCorrect = false;
static bool spdAnsweredThisBeat = false;
static uint16_t spdCombo = 0;
static uint8_t spdLives = 0;

static void spdSpawnBeat(unsigned long now) {
  spdChar = pickKoch();
  char single[2] = { spdChar, '\0' };
  unsigned long ditMs = core_keyer_getDitLengthMs();
  core_morseplayer_start(gamePlayer, single, ditMs, ditMs);   // crisp, full WPM
  spdBeatStartMs = now;
  spdAnsweredThisBeat = false;
  spdPhase = SPD_LISTEN;
}

static void speedHandleChar(char decoded) {
  if (spdPhase != SPD_LISTEN || spdAnsweredThisBeat) return;
  bool correct = (toupper((unsigned char)decoded) == toupper((unsigned char)spdChar));
  core_trainer_recordKochResult(correct);
  spdAnsweredThisBeat = true;
  spdLastCorrect = correct;
  if (correct) {
    spdCombo++;
    if (spdCombo % 3 == 0 && spdBeatMs > 500) spdBeatMs -= 60;   // the "Overdrive" speed-up
  } else {
    spdCombo = 0;
    if (spdLives > 0) spdLives--;
  }
  spdPhase = SPD_FEEDBACK;
  spdFeedbackUntilMs = millis() + GAME_FEEDBACK_MS;
}

static void speedServiceTick(unsigned long now) {
  if (spdPhase == SPD_LISTEN) {
    if (!spdAnsweredThisBeat && now - spdBeatStartMs >= spdBeatMs) {
      spdLastCorrect = false;
      spdCombo = 0;
      if (spdLives > 0) spdLives--;
      spdPhase = SPD_FEEDBACK;
      spdFeedbackUntilMs = now + GAME_FEEDBACK_MS;
    }
  } else if (spdPhase == SPD_FEEDBACK) {
    if (now >= spdFeedbackUntilMs) {
      if (spdLives == 0) {
        spdPhase = SPD_OVER;
        if (spdCombo > scores.speedHigh) { scores.speedHigh = spdCombo; saveScores(); }
      } else {
        spdSpawnBeat(now);
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Dispatcher - single decoder training-sink consumer, routed by activeGame
// ----------------------------------------------------------------------------
static void onGameCharDecoded(char decoded, const char *pattern) {
  (void)pattern;
  switch (activeGame) {
    case GAME_COPY:   copyHandleChar(decoded);   break;
    case GAME_MEMORY: memoryHandleChar(decoded); break;
    case GAME_SPEED:  speedHandleChar(decoded);  break;
    default: break;
  }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void core_games_init() {
  core_morseplayer_init(gamePlayer);
  loadScores();
  activeGame = GAME_NONE;
}

bool   core_games_isSessionActive() { return activeGame != GAME_NONE; }
GameId core_games_getActiveGame()   { return activeGame; }

void core_games_start(GameId game) {
  if (core_trainer_isSessionActive() || activeGame != GAME_NONE) return;
  activeGame = game;
  core_decoder_setTrainingSink(onGameCharDecoded);
  unsigned long now = millis();

  switch (game) {
    case GAME_COPY:
      copyScore = 0; copyLives = GAME_COPY_START_LIVES; copyStreak = 0;
      copySpawnNext(now);
      break;
    case GAME_MEMORY:
      memChainLen = 1; memChain[0] = pickKoch(); memChain[1] = '\0'; memInputPos = 0;
      memoryPlayChain(now);
      break;
    case GAME_SPEED:
      spdCombo = 0; spdLives = GAME_SPEED_START_LIVES; spdBeatMs = reactionBudgetMs();
      spdSpawnBeat(now);
      break;
    default:
      activeGame = GAME_NONE;
      core_decoder_setTrainingSink(nullptr);
      break;
  }
}

void core_games_stop() {
  core_morseplayer_stop(gamePlayer);
  core_decoder_setTrainingSink(nullptr);
  activeGame = GAME_NONE;
  copyPhase = COPY_IDLE;
  memPhase = MEM_IDLE;
  spdPhase = SPD_IDLE;
}

void core_games_confirmPressed() {
  unsigned long now = millis();
  switch (activeGame) {
    case GAME_COPY:
      if (copyPhase == COPY_OVER) {
        copyScore = 0; copyLives = GAME_COPY_START_LIVES; copyStreak = 0;
        copySpawnNext(now);
      }
      break;
    case GAME_MEMORY:
      if (memPhase == MEM_OVER) {
        memChainLen = 1; memChain[0] = pickKoch(); memChain[1] = '\0'; memInputPos = 0;
        memoryPlayChain(now);
      }
      break;
    case GAME_SPEED:
      if (spdPhase == SPD_OVER) {
        spdCombo = 0; spdLives = GAME_SPEED_START_LIVES; spdBeatMs = reactionBudgetMs();
        spdSpawnBeat(now);
      }
      break;
    default: break;
  }
}

bool core_games_isPaused() { return paused; }

void core_games_togglePause() {
  if (activeGame == GAME_NONE) return;
  unsigned long now = millis();
  if (!paused) {
    paused = true;
    pauseStartMs = now;
    core_morseplayer_stop(gamePlayer);   // silence mid-tone rather than leaving a stuck note
  } else {
    pauseAccumMs += (now - pauseStartMs);
    paused = false;
  }
}

void core_games_restart() {
  if (activeGame == GAME_NONE) return;
  GameId g = activeGame;
  paused = false;
  pauseAccumMs = 0;
  core_games_stop();
  core_games_start(g);
}

void core_games_service(unsigned long now) {
  if (activeGame == GAME_NONE) return;
  if (paused) return;   // freeze everything, including morseplayer ticks

  unsigned long adjNow = effectiveNow(now);
  core_morseplayer_service(gamePlayer, adjNow);
  switch (activeGame) {
    case GAME_COPY:   copyServiceTick(adjNow);   break;
    case GAME_MEMORY: memoryServiceTick(adjNow); break;
    case GAME_SPEED:  speedServiceTick(adjNow);  break;
    default: break;
  }
}

CopyPhase core_games_copy_getPhase()        { return copyPhase; }
char      core_games_copy_getFallingChar()  { return copyChar; }
uint16_t  core_games_copy_getScore()        { return copyScore; }
uint8_t   core_games_copy_getLives()        { return copyLives; }
uint16_t  core_games_copy_getHighScore()    { return scores.copyHigh; }
uint8_t core_games_copy_getFallProgressPct(unsigned long now) {
  if (copyPhase != COPY_FALLING) return 100;
  unsigned long elapsed = now - copySpawnMs;
  if (elapsed >= copyFallMs) return 100;
  return (uint8_t)((elapsed * 100UL) / copyFallMs);
}

MemGamePhase core_games_memory_getPhase()          { return memPhase; }
uint8_t      core_games_memory_getChainLength()    { return memChainLen; }
uint8_t      core_games_memory_getInputProgress()  { return memInputPos; }
uint8_t      core_games_memory_getHighScore()      { return scores.memoryHigh; }

SpeedGamePhase core_games_speed_getPhase()       { return spdPhase; }
uint16_t core_games_speed_getCombo()             { return spdCombo; }
uint8_t  core_games_speed_getLives()             { return spdLives; }
bool     core_games_speed_wasLastCorrect()       { return spdLastCorrect; }
char     core_games_speed_getLastChar()          { return spdChar; }
uint16_t core_games_speed_getHighScore()         { return scores.speedHigh; }
unsigned long core_games_speed_getBeatTotalMs()  { return spdBeatMs; }
unsigned long core_games_speed_getBeatRemainingMs(unsigned long now) {
  if (spdPhase != SPD_LISTEN) return 0;
  unsigned long elapsed = now - spdBeatStartMs;
  return (elapsed >= spdBeatMs) ? 0 : (spdBeatMs - elapsed);
}