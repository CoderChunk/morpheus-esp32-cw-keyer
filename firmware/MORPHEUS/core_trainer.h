#ifndef MORPHEUS_CORE_TRAINER_H
#define MORPHEUS_CORE_TRAINER_H
#include <Arduino.h>

enum TrainMode : uint8_t {
  TRAIN_MODE_KOCH, TRAIN_MODE_CHARACTERS, TRAIN_MODE_WORDS,
  TRAIN_MODE_CALLSIGNS, TRAIN_MODE_ADAPTIVE, TRAIN_MODE_EXAM
};

// Stats hooks - core_stats.cpp registers these to observe per-character
// scoring and exam completion. core_trainer has no knowledge of
// core_stats (same inversion pattern as core_decoder's training sink).
typedef void (*TrainerStatsHook)(TrainMode mode, bool correct);
void core_trainer_setStatsHook(TrainerStatsHook hook);

typedef void (*TrainerExamHook)(uint8_t correctCount, uint8_t totalCount, uint8_t scorePercent, bool passed);
void core_trainer_setExamCompleteHook(TrainerExamHook hook);

enum DrillPhase : uint8_t {
  DRILL_IDLE, DRILL_PLAYING, DRILL_LISTENING, DRILL_FEEDBACK, DRILL_EXAM_DONE
};

void      core_trainer_init();
void      core_trainer_service(unsigned long now);
void      core_trainer_startSession(TrainMode mode);
void      core_trainer_stopSession();
bool      core_trainer_isSessionActive();
TrainMode core_trainer_getMode();
void      core_trainer_confirmPressed();

DrillPhase  core_trainer_getPhase();
const char *core_trainer_getTargetText();
const char *core_trainer_getTypedText();
uint32_t    core_trainer_getCorrectCount();
uint32_t    core_trainer_getTotalCount();

uint8_t core_trainer_getKochLevel();
void    core_trainer_setKochLevel(uint8_t level);
void    core_trainer_getKochCharset(char *out, size_t outSize);
void    core_trainer_resetKochProgress();
void    core_trainer_recordKochResult(bool correct);
int     core_trainer_getAdaptiveWpm();
bool    core_trainer_isExamResultReady();
uint8_t core_trainer_getExamScorePercent();
uint8_t core_trainer_getExamCorrectCount();
bool    core_trainer_getExamPassed();
uint8_t core_trainer_getExamTargetLength();
uint8_t core_trainer_getExamTotalCount();
void    core_trainer_clearExamResult();

void core_trainer_farnsworthSetEffectiveWpm(int wpm);
int  core_trainer_farnsworthGetEffectiveWpm();
bool core_trainer_farnsworthTogglePlay();
bool core_trainer_farnsworthIsPlaying();
void core_trainer_farnsworthStop();

#endif