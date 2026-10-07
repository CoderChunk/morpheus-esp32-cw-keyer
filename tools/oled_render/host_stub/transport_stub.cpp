// Host stand-in for transport.h's read-only status getters. transport.cpp
// itself needs NimBLE (ESP-only) so it isn't linked here; this only
// supplies the handful of status functions ui_backend.cpp calls to feed
// the OLED's Connectivity/Diagnostics screens, as fixed "disconnected,
// no bond" values. Never linked with the real transport.cpp/ble_control.cpp.
#include "transport.h"
#include "core_keyer.h"

// MORPHEUS.ino's events_on*() fan-out (real keying/decoding side effects,
// normally defined in the .ino we don't link here). The render harness
// only ever drives navigation events (UI_EV_ROTATE/SELECT/BACK), never a
// virtual key, so these never actually fire during a render walk - they
// exist purely to satisfy the linker for core_keyer.cpp/core_decoder.cpp.
void events_onKeyDown(unsigned long, bool) {}
void events_onKeyUp(ElementType, unsigned long, unsigned long, unsigned long, bool) {}
void events_onCharacterComplete(char, const char *) {}
void events_onWordComplete(const char *, unsigned long) {}

void     transport_resetBond() {}
bool     transport_isConnected() { return false; }
bool     transport_isSecure() { return false; }
bool     transport_hasTrustedDevice() { return false; }
uint8_t  transport_getTrustedDeviceCount() { return 0; }
uint8_t  transport_getTrustedDeviceCap() { return 3; }
uint16_t transport_getCurrentMtu() { return 0; }
void     transport_startPairingWindow() {}
bool     transport_isPairingActive() { return false; }

static bool bleEnabled = true;
bool transport_getBleEnabled() { return bleEnabled; }
void transport_setBleEnabled(bool enabled) { bleEnabled = enabled; }
