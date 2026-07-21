#ifndef MORPHEUS_CORE_PROFILES_H
#define MORPHEUS_CORE_PROFILES_H
#include <Arduino.h>
#include "core_keyer.h"

enum ProfileId : uint8_t { PROFILE_DEFAULT = 0, PROFILE_PORTABLE, PROFILE_CONTEST, PROFILE_PRACTICE,
                           PROFILE_OUTDOOR, PROFILE_SILENT, PROFILE_COUNT };

void core_profiles_init();

// Applies the slot's stored values to live keyer settings. Normal
// debounced settings persistence (services.cpp) picks up the change on
// its own - no explicit save call needed here.
void core_profiles_load(ProfileId id);

// Overwrites the slot with CURRENT live keyer settings. Writes
// immediately (infrequent, deliberate action - same convention as
// core_stats_forceFlush), not the debounced settings path.
void core_profiles_save(ProfileId id);

int           core_profiles_getWpm(ProfileId id);
uint16_t      core_profiles_getToneHz(ProfileId id);
bool          core_profiles_getPaddleReversed(ProfileId id);
OperatingMode core_profiles_getMode(ProfileId id);
uint8_t       core_profiles_getVolume(ProfileId id);
bool          core_profiles_getSidetoneEnabled(ProfileId id);
uint8_t       core_profiles_getContrast(ProfileId id);
void 		  core_profiles_setContrastForSave(ProfileId id, uint8_t contrast);


#endif