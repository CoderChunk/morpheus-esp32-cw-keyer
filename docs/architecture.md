# Architecture

MORPHEUS is an event-driven ESP32 CW keyer. The Arduino sketch at
`firmware/MORPHEUS/MORPHEUS.ino` is the integration layer: it initializes each
module, calls each service function from `loop()`, and owns the event hooks that
connect otherwise independent subsystems.

```text
firmware/MORPHEUS/MORPHEUS.ino
├── core_keyer      GPIO input, debounce, straight/paddle FSMs, sidetone timing
├── core_decoder    DIT/DAH patterns, character lookup, word-gap detection
├── display         OLED transcript, live pattern footer, BLE status overlays
├── transport       secure BLE pairing, bond allowlist, word notifications
└── services        settings persistence, serial diagnostics, utility services
```

## Startup order

`setup()` brings modules up in a deliberate order:

1. Start Serial diagnostics when `FEATURE_SERIAL` is enabled.
2. Sample the active-low bond-reset button before BLE is initialized.
3. Initialize the keyer and decoder.
4. Load persisted operator settings, which may update WPM, sidetone frequency,
   and paddle reversal.
5. Initialize the OLED display when `FEATURE_OLED` is enabled.
6. Initialize services.
7. Initialize BLE transport last, then clear BLE bonds if the boot-time
   bond-reset hold was confirmed.

Keeping BLE last ensures the display, settings, and keyer state are ready before
external devices can pair or receive notifications.

## Main loop

`loop()` stays non-blocking for normal keying operation. Each pass:

1. Captures the keyer mode before servicing input.
2. Runs `core_keyer_service(now)` to debounce GPIOs and emit key events.
3. Logs mode changes when Serial diagnostics are enabled.
4. Runs `core_decoder_service(now)` to finalize characters and words after
   silence gaps.
5. Runs settings persistence, BLE transport, OLED rendering, and Serial
   diagnostics according to their feature flags.
6. Optionally handles temporary debug serial commands when
   `FEATURE_DEBUG_SERIAL_COMMANDS` is enabled.

## Event flow

1. `core_keyer_service()` samples the mode switch, key tip, and key ring inputs.
2. A completed DIT or DAH is emitted through `events_onKeyUp()` in
   `MORPHEUS.ino`.
3. `events_onKeyUp()` logs the element and feeds it to `core_decoder_addElement()`.
4. `core_decoder_service()` waits for character and word gaps based on the
   current keyer dit length.
5. `events_onCharacterComplete()` logs decoded characters.
6. `events_onWordComplete()` fans completed words out to the OLED transcript and
   BLE notification path when those features are enabled.

## Module boundaries

- `core_keyer` owns physical input state and timing. It does not include or call
  `core_decoder`; it only emits key events through hooks declared in
  `core_keyer.h`.
- `core_decoder` consumes `ElementType` values produced by the keyer and exposes
  only read-only decode-in-progress getters for display and diagnostics.
- `display` owns OLED rendering and the display-specific BLE status vocabulary.
  It snapshots BLE status under a short critical section before doing I2C work.
- `transport` owns NimBLE setup, pairing, bond reset, trusted-device storage,
  MTU-aware JSON notification construction, and BLE status updates pushed to the
  display module.
- `services` owns Serial diagnostics, operator settings persistence, factory
  reset, uptime, and heap helpers. Its settings namespace is separate from BLE
  bond/trusted-device storage.

## Feature flags

Most optional behavior is controlled in `firmware/MORPHEUS/config.h`:

- `FEATURE_OLED` gates OLED initialization and rendering.
- `FEATURE_BLE` gates NimBLE transport and word notifications.
- `FEATURE_SIDETONE` gates LEDC sidetone output.
- `FEATURE_SERIAL` gates Serial diagnostics.
- `FEATURE_DEBUG_SERIAL_COMMANDS` enables temporary settings and bond-reset
  commands for hardware validation.

When a feature is disabled, its module still provides stub functions where
needed so the public interfaces remain linkable.
