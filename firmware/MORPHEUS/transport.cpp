#include "transport.h"
#include "config.h"
#include "core_led.h"
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
// BLE on/off (new) - MORPHEUS no longer advertises unconditionally at boot.
// bleEnabled is a persisted user preference (services.cpp); NimBLE itself
// (NimBLEDevice::init, server/service/characteristic setup) is still
// initialized unconditionally in transport_init(), since that setup has no
// radio-visible side effect on its own - only advertising does. Only
// advertising is gated by bleEnabled.
//
// pairingWindowActive layers on top: even with bleEnabled true, MORPHEUS
// doesn't advertise indefinitely - transport_startPairingWindow() opens a
// bounded discoverable window (BLE_PAIRING_WINDOW_MS), and
// transport_service() closes it again if nothing connects in time.
// ----------------------------------------------------------------------------
static bool bleEnabled = false;
static bool pairingWindowActive = false;
static unsigned long pairingWindowStartMs = 0;

// v1.2.1 fix: bleAwaitingTimeout and bleStateChangeMs must always be read and
// written together, as one consistent pair - never one updated without the
// other. See original file header note (unchanged) for the full v1.2.0 bug
// history this spinlock exists to prevent.
static portMUX_TYPE bleStateMux = portMUX_INITIALIZER_UNLOCKED;

static bool isCurrentlyConnected() { return bleConnHandle != BLE_CONN_HANDLE_INVALID; }

static void pushDisplayStatus(DisplayLinkStatus status, uint32_t passkey = 0) {
  display_setTransportStatus(status, passkey);
}

// Maps to the new LedRadioState model (LED_RADIO_OFF/PAIRING/CONNECTED) -
// replaces every old core_led_setStatus(LED_STATUS_*) call site. Centralized
// here so every caller below shares one mapping decision rather than
// repeating the bleEnabled/pairingWindowActive logic inline at each site.
static void updateLedForCurrentState() {
  if (!bleEnabled) {
    core_led_setRadioState(LED_RADIO_OFF);
  } else if (isCurrentlyConnected()) {
    core_led_setRadioState(LED_RADIO_CONNECTED);
  } else {
    core_led_setRadioState(LED_RADIO_PAIRING);   // advertising (pairingWindowActive or otherwise) reads as "pairing"
  }
}

static void jsonEscapeWord(const char *input, char *output, size_t outputSize) {
  if (outputSize == 0) return;
  static const char HEX_DIGITS[] = "0123456789abcdef";
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
      output[out++] = HEX_DIGITS[(c >> 4) & 0x0F];
      output[out++] = HEX_DIGITS[c & 0x0F];
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
    pairingWindowActive = false;   // a real connection ends the pairing window
    pushDisplayStatus(DISPLAY_LINK_CONNECTED);
    updateLedForCurrentState();
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
    // Only resume advertising if BLE is still meant to be discoverable -
    // an explicit transport_setBleEnabled(false) already stopped advertising
    // and disconnected everyone; that path must not have this callback
    // silently restart it.
    if (bleEnabled) {
      NimBLEDevice::startAdvertising();
    }
    updateLedForCurrentState();
  }

  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = esp_random() % 1000000UL;
    pushDisplayStatus(DISPLAY_LINK_PAIRING, passkey);
    updateLedForCurrentState();
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
      portENTER_CRITICAL(&bleStateMux);
      bleAwaitingTimeout = true;
      bleStateChangeMs = millis();
      pushDisplayStatus(DISPLAY_LINK_PAIR_FAIL);
      updateLedForCurrentState();
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
    portENTER_CRITICAL(&bleStateMux);
    bleAwaitingTimeout = true;
    bleStateChangeMs = millis();
    pushDisplayStatus(DISPLAY_LINK_PAIR_OK);
    updateLedForCurrentState();   // secure connection -> LED_RADIO_CONNECTED (brief confirm blink)
    portEXIT_CRITICAL(&bleStateMux);
  }
};

// Advertising start is now conditional, not automatic - called from
// transport_init() (only if bleEnabled was true last session),
// transport_setBleEnabled(true), and transport_startPairingWindow().
static void beginAdvertisingIfEnabled() {
  if (!bleEnabled) return;
  NimBLEDevice::getAdvertising()->start();
  pushDisplayStatus(DISPLAY_LINK_ADV);
  updateLedForCurrentState();
}

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

  // NOTE: bleEnabled here still holds its static-initializer default
  // (false). services_loadSettings() calls transport_setBleEnabled(loaded)
  // right after this, which is what actually restores last session's
  // preference and starts advertising if it was on - see services.cpp
  // ordering requirement (must run transport_init() first, then
  // services_loadSettings()).
  updateLedForCurrentState();

#if FEATURE_SERIAL
  Serial.print(F("BLE ready. Trusted device: "));
  Serial.println(hasTrustedDevice ? trustedAddress : "(none yet - open to first pairing)");
#endif
}

void transport_service(unsigned long now) {
  portENTER_CRITICAL(&bleStateMux);
  if (bleAwaitingTimeout && (now - bleStateChangeMs >= BLE_PAIR_MSG_DURATION_MS)) {
    bleAwaitingTimeout = false;
    if (isCurrentlyConnected()) {
      pushDisplayStatus(DISPLAY_LINK_SECURE);
    } else {
      pushDisplayStatus(DISPLAY_LINK_ADV);
    }
    updateLedForCurrentState();
  }
  portEXIT_CRITICAL(&bleStateMux);

  // Pairing window auto-close - if nothing connected within
  // BLE_PAIRING_WINDOW_MS, stop advertising and go quiet rather than
  // leaving the radio (and LED) blinking indefinitely.
  if (pairingWindowActive && !isCurrentlyConnected()) {
    if (now - pairingWindowStartMs >= BLE_PAIRING_WINDOW_MS) {
      pairingWindowActive = false;
      NimBLEDevice::getAdvertising()->stop();
      updateLedForCurrentState();
#if FEATURE_SERIAL
      Serial.println(F("EVT BLE_PAIRING_WINDOW_EXPIRED"));
#endif
    }
  }
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
  if (bleEnabled) {
    NimBLEDevice::startAdvertising();
  }
  pushDisplayStatus(DISPLAY_LINK_ADV);
  updateLedForCurrentState();
#if FEATURE_SERIAL
  Serial.println(F("EVT BLE_BOND_RESET_COMPLETE"));
#endif
}

// ----------------------------------------------------------------------------
// BLE on/off + pairing window (new)
// ----------------------------------------------------------------------------
bool transport_getBleEnabled() { return bleEnabled; }

void transport_setBleEnabled(bool enabled) {
  if (enabled == bleEnabled) return;
  bleEnabled = enabled;
  if (enabled) {
    beginAdvertisingIfEnabled();
  } else {
    pairingWindowActive = false;
    if (bleServer != nullptr && isCurrentlyConnected()) {
      NimBLEConnInfo connInfo = bleServer->getPeerInfo(0);
      bleServer->disconnect(connInfo);
    }
    NimBLEDevice::getAdvertising()->stop();
    updateLedForCurrentState();
#if FEATURE_SERIAL
    Serial.println(F("EVT BLE_DISABLED"));
#endif
  }
}

void transport_startPairingWindow() {
  if (!bleEnabled) return;
  pairingWindowActive = true;
  pairingWindowStartMs = millis();
  NimBLEDevice::getAdvertising()->start();
  pushDisplayStatus(DISPLAY_LINK_ADV);
  updateLedForCurrentState();
#if FEATURE_SERIAL
  Serial.println(F("EVT BLE_PAIRING_WINDOW_START"));
#endif
}

bool transport_isPairingActive() { return pairingWindowActive; }

// ----------------------------------------------------------------------------
// Read-only status getters
// ----------------------------------------------------------------------------
bool transport_isConnected()      { return isCurrentlyConnected(); }
bool transport_isSecure()         { return bleLinkSecure; }
bool transport_hasTrustedDevice() { return hasTrustedDevice; }
uint16_t transport_getCurrentMtu() {
  if (bleServer == nullptr || !isCurrentlyConnected()) return 0;
  return bleServer->getPeerMTU(bleConnHandle);
}

#else // !FEATURE_BLE - stub implementations, no NimBLE/Preferences dependency at all

void transport_init() {}
void transport_service(unsigned long now) { (void)now; }
void transport_notifyWordCompleted(const char *word, int wpm, OperatingMode mode, unsigned long now) {
  (void)word; (void)wpm; (void)mode; (void)now;
}
void transport_resetBond() {}
bool transport_isConnected()      { return false; }
bool transport_isSecure()         { return false; }
bool transport_hasTrustedDevice() { return false; }
uint16_t transport_getCurrentMtu() { return 0; }

bool transport_getBleEnabled() { return false; }
void transport_setBleEnabled(bool enabled) { (void)enabled; }
void transport_startPairingWindow() {}
bool transport_isPairingActive() { return false; }

#endif