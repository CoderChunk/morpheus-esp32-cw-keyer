/*
 * ============================================================================
 * MORPHEUS - Profiles (Named Preset Slots)
 * ============================================================================
 * File: core_profiles.cpp | Author: Coder Chunk | License: GPLv3
 *
 * Four fixed, named preset slots bundling the operating parameters a
 * user switches between together: WPM, sidetone frequency, paddle
 * reverse, keyer mode, volume, sidetone enable. Deliberately excludes
 * decoderEnabled and kochLevel - those are standing preferences /
 * training progression, not part of an "operating profile."
 *
 * Own NVS namespace, versioned independently of SETTINGS_VERSION. Not
 * touched by Settings > Factory Reset (see config.h note) - has no
 * periodic service function, since it's a pure request/response data
 * store with no live state to tick.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */
#include "core_profiles.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>

struct ProfileData {
  uint16_t      wpm;
  uint16_t      toneHz;
  bool          paddleReverse;
  OperatingMode mode;
  uint8_t       volumePercent;
  bool          sidetoneEnabled;
};

struct ProfilesBlob {
  uint16_t    version;
  ProfileData slots[PROFILE_COUNT];
};

static Preferences profilesPrefs;
static ProfilesBlob blob;

static void seedDefaults() {
  blob.version = PROFILES_VERSION;
  blob.slots[PROFILE_DEFAULT]  = { (uint16_t)DEFAULT_WPM, (uint16_t)TONE_FREQ_HZ, DEFAULT_PADDLE_REVERSED, MODE_STRAIGHT, DEFAULT_VOLUME_PERCENT, DEFAULT_SIDETONE_ENABLED };
  blob.slots[PROFILE_PORTABLE] = { 15, 700, false, MODE_PADDLE,   50, true };
  blob.slots[PROFILE_CONTEST]  = { 25, 800, false, MODE_PADDLE,   90, true };
  blob.slots[PROFILE_PRACTICE] = { 12, 600, false, MODE_STRAIGHT, 70, true };
}

void core_profiles_init() {
  profilesPrefs.begin("morpheus_profiles", false);

  ProfilesBlob loaded;
  size_t got = profilesPrefs.getBytes("slots", &loaded, sizeof(loaded));
  if (got != sizeof(loaded) || loaded.version != PROFILES_VERSION) {
    seedDefaults();
    profilesPrefs.putBytes("slots", &blob, sizeof(blob));
  } else {
    blob = loaded;
  }
}

void core_profiles_load(ProfileId id) {
  if (id >= PROFILE_COUNT) return;
  const ProfileData &p = blob.slots[id];
  core_keyer_setWpm(p.wpm);
  core_keyer_setSidetoneFreq(p.toneHz);
  core_keyer_setPaddleReversed(p.paddleReverse);
  core_keyer_setMode(p.mode);
  core_keyer_setVolume(p.volumePercent);
  core_keyer_setSidetoneEnabled(p.sidetoneEnabled);
}

void core_profiles_save(ProfileId id) {
  if (id >= PROFILE_COUNT) return;
  ProfileData &p = blob.slots[id];
  p.wpm             = (uint16_t)core_keyer_getWpm();
  p.toneHz          = (uint16_t)core_keyer_getSidetoneFreq();
  p.paddleReverse   = core_keyer_getPaddleReversed();
  p.mode            = core_keyer_getMode();
  p.volumePercent   = core_keyer_getVolume();
  p.sidetoneEnabled = core_keyer_getSidetoneEnabled();
  profilesPrefs.putBytes("slots", &blob, sizeof(blob));
}

int           core_profiles_getWpm(ProfileId id)             { return (id < PROFILE_COUNT) ? blob.slots[id].wpm : 0; }
uint16_t      core_profiles_getToneHz(ProfileId id)          { return (id < PROFILE_COUNT) ? blob.slots[id].toneHz : 0; }
bool          core_profiles_getPaddleReversed(ProfileId id)  { return (id < PROFILE_COUNT) ? blob.slots[id].paddleReverse : false; }
OperatingMode core_profiles_getMode(ProfileId id)            { return (id < PROFILE_COUNT) ? blob.slots[id].mode : MODE_STRAIGHT; }
uint8_t       core_profiles_getVolume(ProfileId id)          { return (id < PROFILE_COUNT) ? blob.slots[id].volumePercent : 0; }
bool          core_profiles_getSidetoneEnabled(ProfileId id) { return (id < PROFILE_COUNT) ? blob.slots[id].sidetoneEnabled : true; }