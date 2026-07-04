/*
 * ============================================================================
 * MORPHEUS - Secure BLE Transport
 * ============================================================================
 *
 * File: transport.cpp
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * MORPHEUS extends Morse communication beyond the key itself.
 *
 * This module provides secure Bluetooth Low Energy communication,
 * authenticated pairing, encrypted transport, and wireless telemetry.
 *
 * The transport layer allows external devices to receive decoded words,
 * operating information, and future telemetry extensions.
 *
 * Areas for future contribution include:
 *
 *   • Mobile applications
 *   • Logging gateways
 *   • Remote monitoring
 *   • Web dashboards
 *   • Contest integrations
 *   • Network bridges
 *
 * Wireless communication should enhance the operator experience without
 * compromising reliability or security.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#include "transport.h"
#include "config.h"
#include "display.h"

#if FEATURE_BLE

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string.h>

static NimBLEServer *bleServer = nullptr;
static NimBLECharacteristic *bleWordChar = nullptr;
static Preferences blePrefs;

static volatile uint16_t bleConnHandle = BLE_CONN_HANDLE_INVALID;
static volatile bool bleLinkSecure = false;
static volatile unsigned long bleStateChangeMs = 0;
static volatile bool bleAwaitingTimeout = false; // true while showing PAIR_OK/PAIR_FAIL

static bool hasTrustedDevice = false;
static char trustedAddress[24] = {0};

static const size_t BLE_ESCAPED_WORD_FIELD_CAP = BLE_WORD_FIELD_CAP * 6;

// ----------------------------------------------------------------------------
// v1.2.1 fix: bleAwaitingTimeout and bleStateChangeMs must always be read and
// written together, as one consistent pair - never one updated without the
// other. They're written from NimBLE's host task (onAuthenticationComplete(),
// possibly a different CPU core) and read from the Arduino loop() task
// (transport_service()). `volatile` alone only prevents per-core compiler
// caching; it provides no cross-task atomicity for a multi-field update, which
// is exactly the gap that produced the v1.2.0 "stuck on PAIRED" bug: a reader
// could observe a just-set `true` flag paired with a still-stale timestamp
// from a previous event, conclude the display timeout had already elapsed,
// clear the flag, and push the "settled" display state - only for the
// original writer's own display push to land immediately after and silently
// overwrite it back to PAIR_OK, with bleAwaitingTimeout now permanently
// false and nothing left to ever trigger another clear attempt.
//
// This spinlock makes the flag+timestamp+display-push update indivisible.
// NEEDS HARDWARE/COMPILE VERIFICATION on this Arduino-ESP32 core version,
// though portENTER_CRITICAL/portMUX_TYPE are the standard, widely-used
// pattern for exactly this shape of problem and are normally available
// transitively via Arduino.h on this platform.
// ----------------------------------------------------------------------------
static portMUX_TYPE bleStateMux = portMUX_INITIALIZER_UNLOCKED;

static bool isCurrentlyConnected() { return bleConnHandle != BLE_CONN_HANDLE_INVALID; }

static void pushDisplayStatus(DisplayLinkStatus status, uint32_t passkey = 0) {
  display_setTransportStatus(status, passkey);
}

// Escape only the JSON string field contents. The caller provides a buffer
// sized to the peer MTU budget, so truncation happens before the JSON envelope
// is composed and never leaves a dangling backslash escape.
static void jsonEscapeWord(const char *input, char *output, size_t outputSize) {
  if (outputSize == 0) return;

  static const char HEX[] = "0123456789abcdef";
  size_t out = 0;
  for (size_t i = 0; input[i] != '\0' && out < outputSize - 1; i++) {
    uint8_t c = (uint8_t)input[i];
    if (c == '"' || c == '\\') {
      if (out + 2 >= outputSize) break;
      output[out++] = '\\';
      output[out++] = (char)c;
    } else if (c < 0x20) {
      if (out + 6 >= outputSize) break;
      output[out++] = '\\';
      output[out++] = 'u';
      output[out++] = '0';
      output[out++] = '0';
      output[out++] = HEX[(c >> 4) & 0x0F];
      output[out++] = HEX[c & 0x0F];
    } else {
      output[out++] = (char)c;
    }
  }
  output[out] = '\0';
}

class KeyerBleServerCallbacks : public NimBLEServerCallbacks {

  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    char peerAddr[24];
    strncpy(peerAddr, connInfo.getAddress().toString().c_str(), sizeof(peerAddr) - 1);
    peerAddr[sizeof(peerAddr) - 1] = '\0';

    if (hasTrustedDevice && strcmp(peerAddr, trustedAddress) != 0) {
#if FEATURE_SERIAL
      Serial.print(F("EVT BLE_REJECT addr=")); Serial.println(peerAddr);
#endif
      pServer->disconnect(connInfo);
      return;
    }

    bleConnHandle = connInfo.getConnHandle();
    bleLinkSecure = false;
    pushDisplayStatus(DISPLAY_LINK_CONNECTED);
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_CONNECT addr=")); Serial.println(peerAddr);
#endif

    bool started = NimBLEDevice::startSecurity(connInfo.getConnHandle());
#if FEATURE_SERIAL
    Serial.print(F("  startSecurity() returned ")); Serial.println(started ? "true" : "false");
#endif
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_DISCONNECT reason=")); Serial.println(reason);
#endif
    bleConnHandle = BLE_CONN_HANDLE_INVALID;
    bleLinkSecure = false;

    if (!bleAwaitingTimeout) {
      pushDisplayStatus(DISPLAY_LINK_ADV);
    }
    NimBLEDevice::startAdvertising();
  }

  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = esp_random() % 1000000UL;
    pushDisplayStatus(DISPLAY_LINK_PAIRING, passkey);
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_PASSKEY value=")); Serial.println(passkey);
#endif
    return passkey;
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override {
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_MTU value=")); Serial.println(mtu);
#endif
  }

  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    bool secure = connInfo.isEncrypted() && connInfo.isAuthenticated() && connInfo.isBonded();
    bleLinkSecure = secure;
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_AUTH secure=")); Serial.println(secure ? 1 : 0);
#endif

    if (!secure) {
      // v1.2.1: flag + timestamp + display push updated as one atomic unit.
      portENTER_CRITICAL(&bleStateMux);
      bleAwaitingTimeout = true;
      bleStateChangeMs = millis();
      pushDisplayStatus(DISPLAY_LINK_PAIR_FAIL);
      portEXIT_CRITICAL(&bleStateMux);

      if (bleServer != nullptr) bleServer->disconnect(connInfo);
      return;
    }

    char peerAddr[24];
    strncpy(peerAddr, connInfo.getAddress().toString().c_str(), sizeof(peerAddr) - 1);
    peerAddr[sizeof(peerAddr) - 1] = '\0';

    if (!hasTrustedDevice) {
      hasTrustedDevice = true;
      strncpy(trustedAddress, peerAddr, sizeof(trustedAddress) - 1);
      trustedAddress[sizeof(trustedAddress) - 1] = '\0';
      blePrefs.putBool("hasBond", true);
      blePrefs.putString("trustedAddr", trustedAddress);
#if FEATURE_SERIAL
      Serial.print(F("EVT BLE_TRUSTED addr=")); Serial.println(trustedAddress);
#endif
    }

    // v1.2.1: flag + timestamp + display push updated as one atomic unit.
    // This is the exact sequence that produced the v1.2.0 stuck-PAIRED bug
    // when read mid-update from transport_service() on another task/core.
    portENTER_CRITICAL(&bleStateMux);
    bleAwaitingTimeout = true;
    bleStateChangeMs = millis();
    pushDisplayStatus(DISPLAY_LINK_PAIR_OK);
    portEXIT_CRITICAL(&bleStateMux);
  }
};

void transport_init() {
  blePrefs.begin("cwkeyer", false);
  hasTrustedDevice = blePrefs.getBool("hasBond", false);
  String savedAddr = blePrefs.getString("trustedAddr", "");
  strncpy(trustedAddress, savedAddr.c_str(), sizeof(trustedAddress) - 1);
  trustedAddress[sizeof(trustedAddress) - 1] = '\0';

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setSecurityAuth(true, true, true); // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setMTU(BLE_REQUESTED_MTU);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new KeyerBleServerCallbacks());

  NimBLEService *pSvc = bleServer->createService(BLE_SERVICE_UUID);
  bleWordChar = pSvc->createCharacteristic(
      BLE_WORD_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );
  bleWordChar->setValue("{}");
  pSvc->start();

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // LE General Discoverable + BR/EDR Not Supported
  advData.setName(BLE_DEVICE_NAME);
  pAdv->setAdvertisementData(advData);

  NimBLEAdvertisementData scanRespData;
  scanRespData.addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponseData(scanRespData);
  pAdv->enableScanResponse(true);

  pAdv->start();

  pushDisplayStatus(DISPLAY_LINK_ADV);

#if FEATURE_SERIAL
  Serial.print(F("BLE ready. Trusted device: "));
  Serial.println(hasTrustedDevice ? trustedAddress : "(none yet - open to first pairing)");
#endif
}

void transport_service(unsigned long now) {
  // v1.2.1: read-check-clear-and-display as one atomic unit under the same
  // lock the writers use, so this can never observe a torn flag/timestamp
  // pair from onAuthenticationComplete().
  portENTER_CRITICAL(&bleStateMux);
  if (bleAwaitingTimeout && (now - bleStateChangeMs >= BLE_PAIR_MSG_DURATION_MS)) {
    bleAwaitingTimeout = false;
    pushDisplayStatus(isCurrentlyConnected() ? DISPLAY_LINK_SECURE : DISPLAY_LINK_ADV);
  }
  portEXIT_CRITICAL(&bleStateMux);
}

void transport_notifyWordCompleted(const char *word, int wpm, OperatingMode mode, unsigned long now) {
  if (bleServer == nullptr || bleWordChar == nullptr) return;

  uint16_t connHandle = bleConnHandle;
  if (connHandle == BLE_CONN_HANDLE_INVALID) return;
  if (!bleLinkSecure) return;

  uint16_t mtu = bleServer->getPeerMTU(connHandle);
  if (mtu == 0) mtu = 23;

  int available = (int)mtu - 3 /* ATT header */ - 2 /* safety margin */;
  int maxWordPayloadChars = available - (int)BLE_JSON_OVERHEAD_BYTES;

  if (maxWordPayloadChars <= 0) {
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_SKIP reason=mtu_too_small mtu=")); Serial.println(mtu);
#endif
    return;
  }

  size_t payloadCap = BLE_ESCAPED_WORD_FIELD_CAP;
  if ((size_t)maxWordPayloadChars < payloadCap) payloadCap = (size_t)maxWordPayloadChars;

  char escapedWord[BLE_ESCAPED_WORD_FIELD_CAP + 1];
  if (payloadCap + 1 < sizeof(escapedWord)) {
    jsonEscapeWord(word, escapedWord, payloadCap + 1);
  } else {
    jsonEscapeWord(word, escapedWord, sizeof(escapedWord));
  }

  char json[BLE_ESCAPED_WORD_FIELD_CAP + BLE_JSON_OVERHEAD_BYTES + 1];
  snprintf(json, sizeof(json),
           "{\"word\":\"%s\",\"wpm\":%d,\"mode\":\"%s\",\"timestamp\":%lu}",
           escapedWord, wpm, mode == MODE_STRAIGHT ? "STRAIGHT" : "PADDLE", now);

  bleWordChar->setValue(json);
  bleWordChar->notify();

#if FEATURE_SERIAL
  Serial.print(F("EVT BLE_NOTIFY payload=")); Serial.println(json);
#endif
}

// ----------------------------------------------------------------------------
// Bond Reset (v1.2.0)
// ----------------------------------------------------------------------------
void transport_resetBond() {
#if FEATURE_SERIAL
  Serial.println(F("EVT BLE_BOND_RESET_START"));
#endif

  if (bleServer != nullptr && isCurrentlyConnected()) {
    NimBLEConnInfo connInfo = bleServer->getPeerInfo(0);
    bleServer->disconnect(connInfo);
  }

  NimBLEDevice::deleteAllBonds();

  blePrefs.remove("hasBond");
  blePrefs.remove("trustedAddr");
  hasTrustedDevice = false;
  trustedAddress[0] = '\0';

  NimBLEDevice::startAdvertising();

  pushDisplayStatus(DISPLAY_LINK_ADV);

#if FEATURE_SERIAL
  Serial.println(F("EVT BLE_BOND_RESET_COMPLETE"));
#endif
}

#else // !FEATURE_BLE - stub implementations, no NimBLE/Preferences dependency at all

void transport_init() {}
void transport_service(unsigned long now) { (void)now; }
void transport_notifyWordCompleted(const char *word, int wpm, OperatingMode mode, unsigned long now) {
  (void)word; (void)wpm; (void)mode; (void)now;
}
void transport_resetBond() {}

#endif
