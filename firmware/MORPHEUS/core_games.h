#ifndef MORPHEUS_CORE_GAMES_H
#define MORPHEUS_CORE_GAMES_H
#include <Arduino.h>

enum GameId : uint8_t { GAME_NONE = 0, GAME_COPY, GAME_MEMORY, GAME_SPEED };

void core_games_init();
void core_games_service(unsigned long now);

bool   core_games_isSessionActive();
GameId core_games_getActiveGame();
void   core_games_start(GameId game);
void   core_games_stop();
void   core_games_confirmPressed();   // restarts the current game if it's over

enum CopyPhase : uint8_t { COPY_IDLE, COPY_FALLING, COPY_HIT, COPY_MISS, COPY_OVER };
CopyPhase core_games_copy_getPhase();
char      core_games_copy_getFallingChar();   // valid only during HIT/MISS/OVER feedback
uint8_t   core_games_copy_getFallProgressPct(unsigned long now);
uint16_t  core_games_copy_getScore();
uint8_t   core_games_copy_getLives();
uint16_t  core_games_copy_getHighScore();

enum MemGamePhase : uint8_t { MEM_IDLE, MEM_PLAYBACK, MEM_INPUT, MEM_ROUND_OK, MEM_OVER };
MemGamePhase core_games_memory_getPhase();
uint8_t      core_games_memory_getChainLength();
uint8_t      core_games_memory_getInputProgress();
uint8_t      core_games_memory_getHighScore();

enum SpeedGamePhase : uint8_t { SPD_IDLE, SPD_LISTEN, SPD_FEEDBACK, SPD_OVER };
SpeedGamePhase core_games_speed_getPhase();
uint16_t      core_games_speed_getCombo();
uint8_t       core_games_speed_getLives();
unsigned long core_games_speed_getBeatRemainingMs(unsigned long now);
unsigned long core_games_speed_getBeatTotalMs();
bool          core_games_speed_wasLastCorrect();
char          core_games_speed_getLastChar();
uint16_t      core_games_speed_getHighScore();
bool 		  core_games_isPaused();
void 		  core_games_togglePause();
void 		  core_games_restart();   // re-starts the currently active game from scratch

#endif