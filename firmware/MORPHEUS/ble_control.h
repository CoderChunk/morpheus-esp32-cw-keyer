#ifndef MORPHEUS_BLE_CONTROL_H
#define MORPHEUS_BLE_CONTROL_H
#include <Arduino.h>

// Remote-control bridge for the BLE_CONTROL_CMD/EVT characteristics -
// the BLE analogue of ui_backend.cpp (which bridges the same core
// modules to the OLED UI). Owns the JSON command vocabulary and the
// live training/game state push; transport.cpp only ever sees opaque
// byte strings in both directions.
void ble_control_init();
void ble_control_service(unsigned long now);

// Called by transport.cpp (via the handler registered in
// ble_control_init()) whenever a command write arrives.
void ble_control_handleCommand(const char *json);

#endif // MORPHEUS_BLE_CONTROL_H
