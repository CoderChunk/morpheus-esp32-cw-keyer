#ifndef MORPHEUS_CORE_MORSEPLAYER_H
#define MORPHEUS_CORE_MORSEPLAYER_H
#include <Arduino.h>

// Shared non-blocking Morse playback engine. Extracted from the original
// core_memory.cpp so drills/Farnsworth (core_trainer.cpp) and fixed
// memory messages (core_memory.cpp) share one implementation instead of
// duplicating the phase state machine. Reuses core_keyer_diagToneStart()/
// Stop() internally, so playback always yields to real keying - same
// guarantee as before this extraction.
struct MorsePlayer {
  const char   *cursor;
  char          curPattern[8];
  uint8_t       curPatternLen;
  uint8_t       curPatternPos;
  enum Phase : uint8_t { P_IDLE, P_TONE_ON, P_GAP } phase;
  unsigned long phaseStartMs;
  unsigned long phaseDurationMs;
  bool          active;
  unsigned long elementDitMs;   // dot/dash duration (character speed)
  unsigned long gapDitMs;       // all spacing - independent, for Farnsworth
};

void core_morseplayer_init(MorsePlayer &p);

// Starts playing `text`. Characters with no decoder table entry are
// silently skipped. Refuses (false) if real keying is active.
bool core_morseplayer_start(MorsePlayer &p, const char *text,
                             unsigned long elementDitMs, unsigned long gapDitMs);
void core_morseplayer_stop(MorsePlayer &p);
void core_morseplayer_service(MorsePlayer &p, unsigned long now);
bool core_morseplayer_isActive(const MorsePlayer &p);

#endif