// Exercises the real game dispatcher and scoring with host IO substitutes.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core_games.h"
#include "core_decoder.h"
#include "core_morseplayer.h"
#include "core_trainer.h"
#include "transport.h"
static unsigned long ticks = 100;
static TrainingCharSink sink = nullptr;
static bool trainerActive = false;
struct Input { std::string game, pattern; uint32_t run, seq; char ch; };
static std::vector<Input> inputs;
unsigned long millis() { return ticks; }
long random(long min, long) { return min; }
void randomSeed(unsigned long) {}
void core_decoder_setTrainingSink(TrainingCharSink s) { sink = s; }
bool core_trainer_isSessionActive() { return trainerActive; }
uint8_t core_trainer_getKochLevel() { return 2; }
void core_trainer_getKochCharset(char *out, size_t n) { std::strncpy(out, "KA", n); }
void core_trainer_recordKochResult(bool) {}
unsigned long core_keyer_getDitLengthMs() { return 60; }
void core_morseplayer_init(MorsePlayer &p) { p.active = false; }
bool core_morseplayer_start(MorsePlayer &p, const char *, unsigned long, unsigned long) { p.active = true; return true; }
void core_morseplayer_stop(MorsePlayer &p) { p.active = false; }
void core_morseplayer_service(MorsePlayer &p, unsigned long) { p.active = false; }
bool core_morseplayer_isActive(const MorsePlayer &p) { return p.active; }
void transport_notifyGameMorse(const char *g, uint32_t run, uint32_t seq, char ch,
                              const char *pattern, unsigned long) {
  inputs.push_back({g, pattern, run, seq, ch});
}
int main() {
  core_games_init();
  trainerActive = true;
  core_games_start(GAME_COPY);
  assert(!core_games_isSessionActive() && sink == nullptr);
  trainerActive = false;
  core_games_start(GAME_COPY);
  assert(sink != nullptr && core_games_copy_getScore() == 0);
  sink('K', "-.-");
  assert(inputs.size() == 1 && inputs.back().game == "COPY" && inputs.back().seq == 1);
  assert(inputs.back().pattern == "-.-" && core_games_copy_getScore() == 10);
  assert(core_games_copy_getPhase() == COPY_HIT && core_games_copy_getLives() == 3);
  sink('A', ".-"); // Observed even when the current feedback phase ignores it.
  assert(inputs.back().seq == 2 && core_games_copy_getScore() == 10);
  const auto firstRun = inputs.back().run;
  core_games_restart();
  sink('A', ".-");
  assert(inputs.back().run == firstRun + 1 && inputs.back().seq == 1);
  assert(core_games_copy_getLives() == 2 && core_games_copy_getPhase() == COPY_MISS);
  core_games_stop();
  assert(sink == nullptr && !core_games_isSessionActive());
  core_games_start(GAME_MEMORY);
  core_games_service(ticks);
  assert(core_games_memory_getPhase() == MEM_INPUT);
  sink('K', "-.-");
  assert(inputs.back().game == "MEMORY" && core_games_memory_getPhase() == MEM_ROUND_OK);
  ticks += 2000;
  core_games_service(ticks); core_games_service(ticks);
  assert(core_games_memory_getChainLength() == 2);
  sink('A', ".-");
  assert(core_games_memory_getPhase() == MEM_OVER && core_games_memory_getHighScore() == 1);
  const auto memoryRun = inputs.back().run;
  core_games_confirmPressed(); core_games_service(ticks); sink('K', "-.-");
  assert(inputs.back().run == memoryRun + 1 && inputs.back().seq == 1);
  core_games_stop();
  core_games_start(GAME_SPEED);
  sink('K', "-.-");
  assert(inputs.back().game == "SPEED" && core_games_speed_getCombo() == 1);
  core_games_stop();
  puts("core_games input observer/scoring/exclusivity regression: PASS");
}
