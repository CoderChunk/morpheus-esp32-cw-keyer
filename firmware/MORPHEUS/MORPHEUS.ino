/*
 * ============================================================================
 * MORPHEUS
 * Forge the Sound of Morse.
 * ============================================================================
 * File: MORPHEUS.ino | Author: Coder Chunk | License: GPLv3
 *
 * This revision adds core_memory_init()/service() - the memory message
 * player runs unconditionally alongside the keyer/decoder, since
 * playback is designed to continue across UI screens.
 *
 * Copyright (C) 2026 Coder Chunk
 * ============================================================================
 */

#include "config.h"
#include "core_keyer.h"
#include "core_decoder.h"
#include "core_memory.h"
#include "core_trainer.h"
#include "display.h"
#include "transport.h"
#include "services.h"

void events_onKeyDown(unsigned long now) {
#if FEATURE_SERIAL
  services_logKeyDown(now);
#endif
}

void events_onKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long now) {
#if FEATURE_SERIAL
  services_logKeyUp(type, durMs, thresholdMs, now);
#endif
  core_decoder_addElement(type, now);
}

void events_onCharacterComplete(char decodedChar, const char *pattern) {
#if FEATURE_SERIAL
  services_logCharacterComplete(decodedChar, pattern);
#endif
}

void events_onWordComplete(const char *word, unsigned long now) {
#if FEATURE_SERIAL
  services_logWordComplete(word, now);
#endif
#if FEATURE_OLED
  display_appendWord(word);
#endif
#if FEATURE_BLE
  transport_notifyWordCompleted(word, core_keyer_getWpm(), core_keyer_getMode(), now);
#endif
}

#if FEATURE_DEBUG_SERIAL_COMMANDS
static void handleDebugSerialCommand() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("SET WPM ")) {
    core_keyer_setWpm(line.substring(8).toInt());
    Serial.print(F("EVT DEBUG_SET wpm=")); Serial.println(core_keyer_getWpm());
  } else if (line.startsWith("SET TONE ")) {
    core_keyer_setSidetoneFreq((uint32_t)line.substring(9).toInt());
    Serial.print(F("EVT DEBUG_SET sidetoneHz=")); Serial.println(core_keyer_getSidetoneFreq());
  } else if (line.startsWith("SET PADDLE_REV ")) {
    core_keyer_setPaddleReversed(line.substring(15).toInt() != 0);
    Serial.print(F("EVT DEBUG_SET paddleReversed=")); Serial.println(core_keyer_getPaddleReversed());
  } else if (line == "RESET SETTINGS") {
    services_factoryResetSettings();
    Serial.println(F("EVT DEBUG_FACTORY_RESET"));
  } else if (line == "RESET BOND") {
    transport_resetBond();
    Serial.println(F("EVT DEBUG_BOND_RESET"));
  }
}
#endif

void setup() {
#if FEATURE_SERIAL
  Serial.begin(115200);
  delay(50);
  Serial.println(F("=== MORPHEUS Boot ==="));
#endif

  core_keyer_init();
  core_decoder_init();
  core_memory_init();
  core_trainer_init();

  services_loadSettings();

#if FEATURE_OLED
  display_init();
#endif

  services_init();

#if FEATURE_BLE
  transport_init();
#endif

#if FEATURE_SERIAL
  Serial.println(F("Keyer ready. PJ311 jack: Tip=GPIO25, Ring=GPIO26."));
#endif
}

#if DEBUG_KEYER_TONE && FEATURE_SERIAL
static unsigned long sysHeartbeatLastMs = 0;
static const unsigned long SYS_HEARTBEAT_INTERVAL_MS = 1000;
static uint32_t sysHeartbeatLoopCount = 0;
#endif

void loop() {
  unsigned long now = millis();

  services_tickLoopCounter(now);

  core_keyer_service(now);
  core_decoder_service(now);
  core_memory_service(now);
  core_trainer_service(now);
  services_serviceSettings(now);

#if FEATURE_BLE
  transport_service(now);
#endif

#if FEATURE_OLED
  display_service(now);
#endif

#if FEATURE_SERIAL
  services_serviceDiagnostics(now);
#endif

#if FEATURE_DEBUG_SERIAL_COMMANDS
  handleDebugSerialCommand();
#endif

#if DEBUG_KEYER_TONE && FEATURE_SERIAL
  sysHeartbeatLoopCount++;
  if (now - sysHeartbeatLastMs >= SYS_HEARTBEAT_INTERVAL_MS) {
    Serial.print(F("[DBG_SYS]  t=")); Serial.print(now);
#if FEATURE_BLE
    Serial.print(F(" bleConn=")); Serial.print(transport_isConnected() ? 1 : 0);
    Serial.print(F(" bleSecure=")); Serial.print(transport_isSecure() ? 1 : 0);
#endif
    Serial.print(F(" freeHeap=")); Serial.print(services_getFreeHeapBytes());
    Serial.print(F(" loops/1000ms=")); Serial.println(sysHeartbeatLoopCount);
    sysHeartbeatLastMs = now;
    sysHeartbeatLoopCount = 0;
  }
#endif
}