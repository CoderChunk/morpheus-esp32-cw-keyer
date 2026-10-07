# Architecture

MORPHEUS is an event-driven ESP32 CW keyer. The Arduino sketch at
`firmware/MORPHEUS/MORPHEUS.ino` is the integration layer: it initializes each
module, calls each module's service function from `loop()`, and owns the event
hooks that connect otherwise independent subsystems.

```text
firmware/MORPHEUS/MORPHEUS.ino
├── core_keyer        GPIO input, debounce, straight/paddle FSMs, sidetone timing
├── core_decoder      DIT/DAH patterns, character lookup, word-gap detection
├── core_memory       5-slot memory keyer
├── core_trainer      Koch, characters, words, callsigns, adaptive, exam,
│                     listening and combined training modes
├── core_games        Copy, Memory and Speed challenge games
├── core_stats        session and lifetime statistics
├── core_profiles     six named operating profiles
├── core_clock        shared timing helpers
├── core_led          status LED patterns
├── transport         secure BLE pairing, bond allowlist, word notifications
├── ble_control       BLE control channel: JSON commands in, events out
├── display + ui_*    OLED renderer, menu system, screens, rotary-encoder input
└── services          settings persistence, serial diagnostics, utilities
```

`config.h` holds feature flags, pin assignments, limits and `FIRMWARE_VERSION`.

## Startup order

`setup()` brings modules up in a deliberate order:

1. Start Serial diagnostics when `FEATURE_SERIAL` is enabled.
2. Initialize the core modules: keyer, decoder, memory keyer, trainer, games,
   statistics, profiles, clock and status LED.
3. Initialize BLE transport (`transport_init()`) and then the BLE control
   channel (`ble_control_init()`).
4. Load persisted settings (`services_loadSettings()`) and record the session
   start. This must come after `transport_init()`: loading a persisted
   "BLE enabled" setting starts advertising, which needs the BLE stack to exist.
5. Initialize the OLED display when `FEATURE_OLED` is enabled.
6. Initialize services.

## Main loop

`loop()` stays non-blocking. Each pass runs, in order: the loop counter, the keyer
(debounce and key events), the decoder (character and word gaps), the memory keyer,
trainer, games, statistics, settings persistence and status LED, then BLE transport,
the BLE control channel, the OLED/UI, Serial diagnostics (`FEATURE_SERIAL`) and the
optional debug serial commands (`FEATURE_DEBUG_SERIAL_COMMANDS`).

## Event flow

1. `core_keyer_service()` samples the key and paddle inputs. A key may also be
   pressed remotely through the BLE `key_down` / `key_up` commands, which feed the
   same path (marked `fromVirtualKey`).
2. A completed DIT or DAH is emitted through `events_onKeyUp()` in `MORPHEUS.ino`,
   which updates the BLE keyer metrics and statistics and feeds the element to
   `core_decoder_addElement()`.
3. `core_decoder_service()` waits for character and word gaps based on the current
   dit length.
4. `events_onCharacterComplete()` updates statistics and sends a live-word update
   over BLE; `events_onPatternChanged()` sends the in-progress pattern.
5. `events_onWordComplete()` fans the completed word out to the OLED transcript,
   the statistics and the BLE word notification.
6. Training and game modules can consume decoded input through their own sinks;
   while one is active, the decoder output goes to it instead of the transcript.

## BLE control channel

`transport` only carries bytes: the GATT service, pairing, the bond allowlist and
notification framing. `ble_control` owns the meaning of the control characteristics:
JSON commands (virtual key, training and game control, keyer settings, device
information, measurement probes) and the matching state, `ack` and `error` events.
The wire format is specified in `tools/ble_client/WS_PROTOCOL.md` and
`tools/ble_client/MOBILE_BLE_PROTOCOL.md`. A virtual key that is still held when the
link drops is released by `ble_control_service()`.

## Module boundaries

- `core_keyer` owns physical input state and timing. It does not include or call
  `core_decoder`; it only emits key events through hooks declared in `core_keyer.h`.
- `core_decoder` consumes `ElementType` values from the keyer and exposes only
  read-only decode-in-progress getters.
- `transport` owns NimBLE setup, pairing, bond reset, trusted-device storage and
  MTU-aware JSON notification construction, and pushes status to the display.
- `ble_control` owns the control-channel vocabulary and its state machines.
- `display` and the `ui_*` modules own OLED rendering, menus and navigation. The UI
  talks to the rest of the firmware through `ui_backend`.
- `services` owns Serial diagnostics, settings persistence, factory reset, uptime
  and heap helpers. Its settings namespace is separate from BLE bond storage.

## Feature flags

Most optional behavior is controlled in `firmware/MORPHEUS/config.h`:

- `FEATURE_OLED` gates OLED initialization and rendering.
- `FEATURE_BLE` gates the NimBLE transport and word notifications.
- `FEATURE_SIDETONE` gates LEDC sidetone output.
- `FEATURE_SERIAL` gates Serial diagnostics (off by default).
- `FEATURE_DEBUG_SERIAL_COMMANDS` enables temporary settings and bond-reset
  commands for hardware validation (off by default).

When a feature is disabled, its module still provides stub functions where needed so
the public interfaces remain linkable.
