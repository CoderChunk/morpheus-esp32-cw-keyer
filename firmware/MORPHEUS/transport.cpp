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
static NimBLECharacteristic *bleControlCmdChar = nullptr;
static NimBLECharacteristic *bleControlEvtChar = nullptr;
static BleControlCommandHandler controlCommandHandler = nullptr;
static Preferences blePrefs;
static volatile uint16_t bleConnHandle = BLE_CONN_HANDLE_INVALID;
static volatile bool bleLinkSecure = false;
static volatile unsigned long bleStateChangeMs = 0;
static volatile bool bleAwaitingTimeout = false; // true while showing PAIR_OK/PAIR_FAIL
// Multi-device pairing: up to BLE_TRUSTED_DEVICE_CAP remembered bonded
// addresses. Still exactly one ACTIVE connection at a time (see the
// isCurrentlyConnected() guard in onConnect() below) - this list only
// controls who is allowed to connect, never how many at once.
static uint8_t trustedDeviceCount = 0;
static char trustedAddresses[BLE_TRUSTED_DEVICE_CAP][24];
static const size_t BLE_ESCAPED_WORD_FIELD_CAP = BLE_WORD_FIELD_CAP * 6;

static bool isTrustedAddress(const char *addr) {
  for (uint8_t i = 0; i < trustedDeviceCount; i++) {
    if (strcmp(trustedAddresses[i], addr) == 0) return true;
  }
  return false;
}

// Returns false if addr is new AND the list is already full - caller
// must reject the connection in that case, not just skip remembering it.
static bool addTrustedAddress(const char *addr) {
  if (isTrustedAddress(addr)) return true;
  if (trustedDeviceCount >= BLE_TRUSTED_DEVICE_CAP) return false;
  strncpy(trustedAddresses[trustedDeviceCount], addr, sizeof(trustedAddresses[0]) - 1);
  trustedAddresses[trustedDeviceCount][sizeof(trustedAddresses[0]) - 1] = '\0';
  trustedDeviceCount++;
  return true;
}

static void saveTrustedDevices() {
  blePrefs.putUChar("trustedCount", trustedDeviceCount);
  for (uint8_t i = 0; i < trustedDeviceCount; i++) {
    char key[16];
    snprintf(key, sizeof(key), "trustedAddr%u", (unsigned)i);
    blePrefs.putString(key, trustedAddresses[i]);
  }
}

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

// v2.1.1 fix: the LED previously inferred "pairing" from bleEnabled +
// not-connected alone, so once the bounded pairing window auto-expired
// (advertising genuinely stopped - see transport_service() below) the LED
// kept blinking forever even though the radio was no longer discoverable.
// Tracking the real advertising state directly keeps the LED honest about
// whether the device can actually be found and connected to right now.
static bool advertisingActive = false;

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
  } else if (advertisingActive) {
    core_led_setRadioState(LED_RADIO_PAIRING);   // genuinely discoverable right now
  } else {
    core_led_setRadioState(LED_RADIO_OFF);       // BLE on, but not currently advertising
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
    // One active connection at a time, regardless of how many devices
    // are trusted - a second incoming connection while another is
    // already live gets turned away rather than silently stomping the
    // single global bleConnHandle/bleLinkSecure state below.
    if (isCurrentlyConnected()) {
#if FEATURE_SERIAL
      Serial.print(F("EVT BLE_REJECT_BUSY addr=")); Serial.println(peerAddr);
#endif
      pServer->disconnect(connInfo);
      return;
    }
    // Any already-trusted device may reconnect. An unknown device may
    // only connect (and become trusted on successful auth, see
    // onAuthenticationComplete) while there is still a free slot.
    if (trustedDeviceCount >= BLE_TRUSTED_DEVICE_CAP && !isTrustedAddress(peerAddr)) {
#if FEATURE_SERIAL
      Serial.print(F("EVT BLE_REJECT_FULL addr=")); Serial.println(peerAddr);
#endif
      pServer->disconnect(connInfo);
      return;
    }
    bleConnHandle = connInfo.getConnHandle();
    bleLinkSecure = false;
    pairingWindowActive = false;   // a real connection ends the pairing window
    advertisingActive = false;     // NimBLE stops advertising once connected
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
      advertisingActive = NimBLEDevice::startAdvertising();
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
    if (!isTrustedAddress(peerAddr)) {
      // onConnect() already rejected this address if the list was full,
      // so addTrustedAddress() here should always succeed - the check
      // is defensive, not expected to fail in normal operation.
      if (addTrustedAddress(peerAddr)) {
        saveTrustedDevices();
#if FEATURE_SERIAL
        Serial.print(F("EVT BLE_TRUSTED addr=")); Serial.print(peerAddr);
        Serial.print(F(" count=")); Serial.println(trustedDeviceCount);
#endif
      }
    }
    portENTER_CRITICAL(&bleStateMux);
    bleAwaitingTimeout = true;
    bleStateChangeMs = millis();
    pushDisplayStatus(DISPLAY_LINK_PAIR_OK);
    updateLedForCurrentState();   // secure connection -> LED_RADIO_CONNECTED (brief confirm blink)
    portEXIT_CRITICAL(&bleStateMux);
  }
};

// Command characteristic write handler - forwards the raw JSON string
// to whoever registered via transport_setControlCommandHandler() (only
// ble_control.cpp does, today). transport.cpp never interprets the
// bytes itself, matching the same ignorance-of-payload boundary as the
// word-notify characteristic in the other direction.
class ControlCmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pChar, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    if (controlCommandHandler == nullptr) return;
    std::string value = pChar->getValue();
    if (value.empty() || value.size() >= BLE_CONTROL_CMD_CAP) return;
    char buf[BLE_CONTROL_CMD_CAP];
    memcpy(buf, value.data(), value.size());
    buf[value.size()] = '\0';
    controlCommandHandler(buf);
  }
};

// Advertising start is now conditional, not automatic - called from
// transport_init() (only if bleEnabled was true last session),
// transport_setBleEnabled(true), and transport_startPairingWindow().
static void beginAdvertisingIfEnabled() {
  if (!bleEnabled) return;
  advertisingActive = NimBLEDevice::getAdvertising()->start();
#if FEATURE_SERIAL
  if (!advertisingActive) Serial.println(F("EVT BLE_ADV_START_FAILED"));
#endif
  pushDisplayStatus(DISPLAY_LINK_ADV);
  updateLedForCurrentState();
}

void transport_init() {
  blePrefs.begin("cwkeyer", false);

  trustedDeviceCount = blePrefs.getUChar("trustedCount", 0);
  if (trustedDeviceCount > BLE_TRUSTED_DEVICE_CAP) trustedDeviceCount = BLE_TRUSTED_DEVICE_CAP;
  for (uint8_t i = 0; i < trustedDeviceCount; i++) {
    char key[16];
    snprintf(key, sizeof(key), "trustedAddr%u", (unsigned)i);
    String addr = blePrefs.getString(key, "");
    strncpy(trustedAddresses[i], addr.c_str(), sizeof(trustedAddresses[i]) - 1);
    trustedAddresses[i][sizeof(trustedAddresses[i]) - 1] = '\0';
  }

  // One-time migration from the pre-multi-device single-address scheme
  // ("hasBond"/"trustedAddr") so an already-bonded phone isn't silently
  // forgotten by this firmware upgrade - NimBLE's own bond store (the
  // actual crypto keys) is untouched either way, only this app-level
  // allowlist changed shape.
  if (trustedDeviceCount == 0 && blePrefs.getBool("hasBond", false)) {
    String legacyAddr = blePrefs.getString("trustedAddr", "");
    if (legacyAddr.length() > 0) {
      addTrustedAddress(legacyAddr.c_str());
      saveTrustedDevices();
    }
    blePrefs.remove("hasBond");
    blePrefs.remove("trustedAddr");
  }

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

  bleControlCmdChar = pSvc->createCharacteristic(
      BLE_CONTROL_CMD_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
  );
  bleControlCmdChar->setCallbacks(new ControlCmdCallbacks());

  bleControlEvtChar = pSvc->createCharacteristic(
      BLE_CONTROL_EVT_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );
  bleControlEvtChar->setValue("{}");

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
  Serial.print(F("BLE ready. Trusted devices: "));
  Serial.print(trustedDeviceCount);
  Serial.print(F("/")); Serial.println(BLE_TRUSTED_DEVICE_CAP);
  for (uint8_t i = 0; i < trustedDeviceCount; i++) {
    Serial.print(F("  ")); Serial.println(trustedAddresses[i]);
  }
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
      advertisingActive = false;
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

void transport_setControlCommandHandler(BleControlCommandHandler handler) {
  controlCommandHandler = handler;
}

bool transport_sendControlEvent(const char *json) {
  if (bleServer == nullptr || bleControlEvtChar == nullptr) return false;
  uint16_t connHandle = bleConnHandle;
  if (connHandle == BLE_CONN_HANDLE_INVALID) return false;
  if (!bleLinkSecure) return false;
  size_t len = strlen(json);
  uint16_t mtu = bleServer->getPeerMTU(connHandle);
  if (mtu == 0) mtu = 23;
  size_t available = (mtu > 3) ? (size_t)(mtu - 3) : 0;
  if (len == 0 || len > available || len >= BLE_CONTROL_EVT_CAP) {
#if FEATURE_SERIAL
    Serial.print(F("EVT BLE_CTRL_SKIP reason=too_large len=")); Serial.print(len);
    Serial.print(F(" mtu=")); Serial.println(mtu);
#endif
    return false;
  }
  bleControlEvtChar->setValue(json);
  bleControlEvtChar->notify();
  return true;
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
  // Forgets ALL trusted devices, not just one - there is no per-device
  // "forget this one" action yet (see transport_getTrustedDeviceCount()/
  // transport_getTrustedDeviceAddress() if adding one later).
  NimBLEDevice::deleteAllBonds();
  for (uint8_t i = 0; i < BLE_TRUSTED_DEVICE_CAP; i++) {
    char key[16];
    snprintf(key, sizeof(key), "trustedAddr%u", (unsigned)i);
    blePrefs.remove(key);
  }
  blePrefs.remove("trustedCount");
  trustedDeviceCount = 0;
  memset(trustedAddresses, 0, sizeof(trustedAddresses));
  if (bleEnabled) {
    advertisingActive = NimBLEDevice::startAdvertising();
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

#if FEATURE_DEBUG_SERIAL_COMMANDS
// Temporary bench-diagnostic dump - not part of the stable API.
void transport_debugDumpState() {
  Serial.print(F("EVT BLE_STATE bleEnabled=")); Serial.print(bleEnabled);
  Serial.print(F(" pairingWindowActive=")); Serial.print(pairingWindowActive);
  Serial.print(F(" advertisingActive=")); Serial.print(advertisingActive);
  Serial.print(F(" connected=")); Serial.print(isCurrentlyConnected());
  Serial.print(F(" trustedCount=")); Serial.println(trustedDeviceCount);
}
#endif

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
    advertisingActive = false;
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
  advertisingActive = NimBLEDevice::getAdvertising()->start();
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
bool transport_hasTrustedDevice() { return trustedDeviceCount > 0; }
uint8_t transport_getTrustedDeviceCount() { return trustedDeviceCount; }
uint8_t transport_getTrustedDeviceCap()   { return BLE_TRUSTED_DEVICE_CAP; }
const char *transport_getTrustedDeviceAddress(uint8_t index) {
  if (index >= trustedDeviceCount) return "";
  return trustedAddresses[index];
}
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
void transport_setControlCommandHandler(BleControlCommandHandler handler) { (void)handler; }
bool transport_sendControlEvent(const char *json) { (void)json; return false; }
void transport_resetBond() {}
bool transport_isConnected()      { return false; }
bool transport_isSecure()         { return false; }
bool transport_hasTrustedDevice() { return false; }
uint8_t transport_getTrustedDeviceCount() { return 0; }
uint8_t transport_getTrustedDeviceCap()   { return 0; }
const char *transport_getTrustedDeviceAddress(uint8_t index) { (void)index; return ""; }
uint16_t transport_getCurrentMtu() { return 0; }

bool transport_getBleEnabled() { return false; }
void transport_setBleEnabled(bool enabled) { (void)enabled; }
void transport_startPairingWindow() {}
bool transport_isPairingActive() { return false; }

#endif