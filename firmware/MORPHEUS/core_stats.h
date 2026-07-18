#ifndef MORPHEUS_CORE_STATS_H
#define MORPHEUS_CORE_STATS_H
#include <Arduino.h>
#include "core_trainer.h"

void core_stats_init();
void core_stats_service(unsigned long now);
void core_stats_recordSessionStart();   // call once, after services_loadSettings()
void core_stats_forceFlush();           // call before a deliberate restart

// Real operational activity only - never fires during a training
// session, since core_decoder routes training characters through its
// training sink instead (see core_decoder.h/.cpp history).
void core_stats_notifyCharKeyed();
void core_stats_notifyWordKeyed();
void core_stats_notifyElementKeyed();

uint32_t core_stats_session_getCharsKeyed();
uint32_t core_stats_session_getWordsKeyed();
uint32_t core_stats_session_getElementsKeyed();
uint32_t core_stats_session_getTrainingAttempts();
uint32_t core_stats_session_getTrainingCorrect();
unsigned long core_stats_session_getUptimeMs();

uint32_t core_stats_lifetime_getCharsKeyed();
uint32_t core_stats_lifetime_getWordsKeyed();
uint32_t core_stats_lifetime_getElementsKeyed();
uint32_t core_stats_lifetime_getUptimeSec();
uint32_t core_stats_lifetime_getTrainingAttempts();
uint32_t core_stats_lifetime_getTrainingCorrect();
uint32_t core_stats_lifetime_getModeAttempts(TrainMode mode);
uint32_t core_stats_lifetime_getModeCorrect(TrainMode mode);
uint32_t core_stats_lifetime_getExamsTaken();
uint32_t core_stats_lifetime_getExamsPassed();
uint8_t  core_stats_lifetime_getBestExamScore();
int      core_stats_lifetime_getPeakAdaptiveWpm();

// Boot-time WPM snapshots, newest first - NOT a real-time trend (no RTC
// on this hardware). indexFromNewest: 0 = most recent boot.
uint8_t  core_stats_history_getCount();
uint16_t core_stats_history_getEntry(uint8_t indexFromNewest);

// Available but deliberately unwired to any menu action this pass -
// lifetime stats are operational history and survive Settings > Factory
// Reset by design, unlike operator settings/Koch progress.
void core_stats_resetLifetime();

#endif