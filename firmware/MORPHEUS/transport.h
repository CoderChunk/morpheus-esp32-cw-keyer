/*
 * ============================================================================
 * MORPHEUS - Transport Interface
 * ============================================================================
 *
 * File: transport.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * Defines the communication interface used by the transport subsystem.
 *
 * The transport layer provides a clean boundary between the firmware core
 * and external communication technologies, allowing future transport
 * methods to be added without affecting the rest of the system.
 *
 * INTEGRATION ADDITION: three read-only status getters, for the UI's
 * Connectivity > Bluetooth > Status info page (via ui_backend.cpp). They
 * return the module's existing internal state - no new BLE behavior.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef MORPHEUS_TRANSPORT_H
#define MORPHEUS_TRANSPORT_H

#include <Arduino.h>
#include "core_keyer.h"   // OperatingMode - needed for the word-event
                          // payload type. transport never needs
                          // core_decoder - it only ever sees completed
                          // words via transport_notifyWordCompleted().

void transport_init();
void transport_service(unsigned long now);

// Called from MORPHEUS.ino's events_onWordComplete() fan-out.
void transport_notifyWordCompleted(const char *word, int wpm, OperatingMode mode, unsigned long now);

// Clears the BLE bond: disconnects any currently connected peer, wipes
// NimBLE's own internal bond store, clears this app's trusted-device
// allowlist, and reopens advertising to a new pairing. Never touches
// operator settings (services.cpp's NVS namespace) - that separation is
// load-bearing, not incidental. Reachable today via the menu
// (Connectivity > Bluetooth > Bond Reset, ACTION_BOND_RESET in
// ui_menu.cpp) or the temporary FEATURE_DEBUG_SERIAL_COMMANDS
// "RESET BOND" command - there is no dedicated hardware button for
// this; add one (and its own PIN_* define) if that's ever needed.
void transport_resetBond();

// Read-only status - added for UI info screens (ui_backend.cpp).
bool transport_isConnected();
bool transport_isSecure();
bool transport_hasTrustedDevice();

// Multi-device pairing (config.h's BLE_TRUSTED_DEVICE_CAP). Count/cap
// for a "2/3 devices" style display; address-by-index for a future
// per-device "forget this one" screen (transport_resetBond() today
// only forgets all of them at once).
uint8_t     transport_getTrustedDeviceCount();
uint8_t     transport_getTrustedDeviceCap();
const char *transport_getTrustedDeviceAddress(uint8_t index);

// Diagnostics addition - current negotiated MTU, 0 if not connected.
uint16_t transport_getCurrentMtu();

bool transport_getBleEnabled();
void transport_setBleEnabled(bool enabled);
void transport_startPairingWindow();
bool transport_isPairingActive();

// ----------------------------------------------------------------------------
// Remote-control channel (ble_control.cpp) - Training/Games/virtual
// keying. transport.cpp only carries bytes on BLE_CONTROL_CMD/EVT_UUID;
// it has no idea what a "command" means, same boundary as
// transport_notifyWordCompleted() staying ignorant of core_decoder.
// ----------------------------------------------------------------------------
typedef void (*BleControlCommandHandler)(const char *json);

// Self-registration, same pattern as core_decoder_setTrainingSink():
// ble_control_init() calls this once, transport.cpp never needs to know
// ble_control.h exists.
void transport_setControlCommandHandler(BleControlCommandHandler handler);

// Sends one JSON event on the control-notify characteristic. Silently
// does nothing (returns false) if not connected+secure, or if json is
// longer than the current negotiated MTU allows - callers are expected
// to keep payloads well within BLE_CONTROL_EVT_CAP and just accept a
// dropped update rather than corrupt one on the wire.
bool transport_sendControlEvent(const char *json);

#if FEATURE_DEBUG_SERIAL_COMMANDS
void transport_debugDumpState();
#endif

#endif // MORPHEUS_TRANSPORT_H