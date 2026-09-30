# Changelog

All notable changes to MORPHEUS are documented here.

Versioning follows a simple `MAJOR.MINOR.PATCH` scheme:
- **Major features** increment the minor version (e.g. v1.1.0, v1.2.0).
- **Bug fixes** increment the patch version (e.g. v1.1.1).

---

## [v2.6.0] — Live Dit/Dah Pattern Over BLE

### Added
- **BLE now also carries the live in-progress dit/dah pattern**, the
  same thing the OLED's header readout already showed locally
  (`ui_backend_getLivePattern()`). New `events_onPatternChanged()` hook
  in `core_decoder.h`, fired from `core_decoder_addElement()` (one new
  element keyed) and from `finalizeCharacter()` (character finalized,
  pattern cleared) - same push-based approach as v2.5.0's live-word
  event, just one granularity finer. `MORPHEUS.ino` wires it to the new
  `transport_notifyLivePattern()`, sharing `BLE_WORD_CHAR_UUID` again,
  this time under JSON key `"pat"`.
  - Firmware: `core_decoder.h/.cpp`, `MORPHEUS.ino`, `transport.h/.cpp`
  - Python bridge: `backend.py` (`LivePatternEvent`,
    `on_keyer_live_pattern`), `ws_server.py`
    (`keyerLivePatternReceived` event), `WS_PROTOCOL.md`,
    `MOBILE_BLE_PROTOCOL.md`
  - Flutter: `livePatternEvents` stream on `MorpheusClient`, a
    `showLivePattern` toggle (session-only, not persisted) and a
    live pattern readout docked into the CW Keyer page's metrics row,
    plus an animated VU-meter-style waveform that pulses while a
    pattern is being keyed
  - Fires once per keyed element - the most frequent of the three
    telemetry events, but still only every 30-60ms even at rapid 40 WPM
    dits, well inside a BLE connection interval.

---

## [v2.5.0] — Live Character Decode Over BLE

### Added
- **BLE now carries live, in-progress character decode**, closing a gap
  left by v2.4.0's OLED live-decode work: the OLED redesign only ever
  touched the local Home screen - BLE, the Python bridge, and the
  Flutter app still only ever saw a complete word once flushed on a
  word-gap. `events_onCharacterComplete()` (`MORPHEUS.ino`) now also
  calls `transport_notifyLiveWord()`, sending the whole in-progress
  word (not just the new character) over the existing
  `BLE_WORD_CHAR_UUID` notify characteristic, distinguished from the
  final-word event by JSON key (`"live"` vs `"word"`) rather than a new
  UUID - so a coalesced/dropped intermediate notification is harmless,
  the last one received always reflects full current state.
  `core_decoder.cpp`'s `finalizeCharacter()` now appends to `wordBuffer`
  *before* firing the character-complete event, so the live payload
  already includes the just-decoded character.
  - Firmware: `transport.h/.cpp`, `core_decoder.cpp`, `MORPHEUS.ino`
  - Python bridge: `protocol.py` (n/a, same UUID), `backend.py`
    (`LiveWordEvent`, `on_keyer_live_word`), `ws_server.py`
    (`keyerLiveWordReceived` event), `WS_PROTOCOL.md`
  - Flutter: `KeyerWordEvent`-shaped `liveWordEvents` stream on
    `MorpheusClient`/`WebSocketMorpheusClient`, `MorpheusSession.liveWord`,
    Home screen's live console display
  - Not yet implemented in `MOBILE_BLE_PROTOCOL.md`'s direct-GATT path
    (mobile client doesn't exist yet)
  - At typical/worst-case keying speed (30-40 WPM), live notifications
    fire at most every 120-160ms with a <80 byte payload - well within
    a single BLE connection interval, no coalescing risk (the earlier
    OLED-dump coalescing bug happened at microsecond-scale bursts,
    ~1000x faster than character-decode timing).

---

## [v2.4.0] — Live Decode Display, OLED Debug Tooling & Keyer Test Coverage

### Added
- **Home screen now decodes live, character by character** instead of
  only showing a complete word once it finishes. `core_decoder.cpp`
  already built the word up one character at a time internally
  (`wordBuffer`, finalized per-character, flushed to history only on a
  word-gap) - that data just never reached the Home screen's transcript,
  which only redrew on the word-complete event. Fixed by adding
  `ui_backend_getLiveTranscriptLines()` (the committed transcript with
  the in-progress word appended) and having `display.cpp`'s poll loop
  diff it every cycle, not just on word-complete.
  - Files: `ui_backend.h/.cpp`, `display.cpp`
- **Live dit/dah pattern readout**, header row, top-right, no label -
  shows whatever character is currently mid-key. New persisted setting,
  **Settings → Display → Live Pattern** (on by default), to hide it.
  Settings-schema bump (`SETTINGS_VERSION` 8→9).
  - Files: `ui_screens.cpp`, `ui_state.cpp/.h`, `ui_menu.cpp/.h`,
    `ui_backend.h/.cpp`, `services.h/.cpp`, `ui_mockdata.h`, `config.h`
  - Verified on real hardware over BLE (virtual-key simulation +
    live screenshots mid-sequence): word grows one character per
    snapshot, header pattern renders without overlapping the status
    text, footer correctly still shows Decoder ON/OFF when idle.
- **`tools/oled_render/`** - renders the real `ui_renderer.cpp`/
  `ui_screens.cpp` drawing code against the real U8g2 library on the
  host, no ESP32 or display needed. Walks the entire menu tree
  (~130 screens) to PNG in about a second.
- **`tools/oled_screencap/screencap_ble.py`** - live screenshot of the
  physical device's OLED over BLE (`dump_screen_start`/
  `dump_screen_chunk` commands, gated by
  `FEATURE_DEBUG_SERIAL_COMMANDS`, confirmed zero binary-size impact
  with it off). A USB-serial version was tried first and dropped -
  opening the serial port resets this board's ESP32 every time
  (driver-level auto-program circuit behavior, not fixable host-side),
  making repeated capture useless; BLE connect/disconnect never
  touches the reset pins. `--session` mode reuses one connection
  across many captures instead of reconnecting per shot.
  - Files: `ble_control.cpp`, `ui_renderer.h/.cpp`
- **`tests/native/test_core_keyer.cpp`** - 11 native regression tests
  against the real `core_keyer.cpp`: straight-key DIT/DAH
  classification, WPM/weight/volume/sidetone clamping, mode-change
  mid-element state reset, and iambic paddle behavior including the
  actual Mode A vs B distinction (a squeeze released mid-element sends
  one extra alternating element in Mode B, not in Mode A). Verified to
  actually catch a regression (flipped classification comparison)
  before being added.
  - Files: `tests/native/test_core_keyer.cpp`, `run.sh`,
    `arduino_stub/Arduino.h`

### Fixed
- **Several stale comments and one dead code path**, found during a
  full firmware audit: a `handleLiveMonitor()` stub left over from an
  earlier refactor (superseded by, but never replaced with,
  `handleLiveMonitorReal()` - renamed back to `handleLiveMonitor()` now
  that the stub is gone) containing an unused placeholder label;
  `ui_mockdata.h` claiming callsign/date/time had no backend when
  `services.cpp`/`core_clock.cpp` already provide one;
  `core_profiles.cpp` saying "four" preset slots (actual: six);
  `docs/architecture.md` describing a "live pattern footer" this
  release moved to the header. `docs/FUNCTIONALITY_STATUS.md` had
  accumulated the most drift - stale version numbers, features it
  claimed were unreachable that were already wired up
  (`core_stats_resetLifetime()`, `core_led_trainerFlashOn/Off()`), and
  a "stale documentation identified" section that was itself stale
  (claiming `CHANGELOG.md`/`README.md` were out of date when they'd
  already been fixed in an earlier pass). `docs/build.md` showed
  `FEATURE_SERIAL` defaulting to `1`; actual default is `0`.

- **EXAM training results never reached any BLE/WebSocket client, and
  `train_confirm` silently did nothing once they were ready** — both
  bugs shared one root cause: `sessionActive` and
  `phase=DRILL_EXAM_DONE` flip together, atomically, in the same
  transition (`core_trainer.cpp`'s exam-completion branch). Once that
  happens, `buildTrainStateJson()` (`ble_control.cpp`) saw
  `!core_trainer_isSessionActive()` and emitted the bare
  `{"evt":"train_state","active":false}` shape with no `phase`,
  `examScorePercent`, `examPassed`, `examCorrect`, or `examTotal` —
  and since this push is rate-limited/deduped with no earlier frame
  carrying the result, that was the *only* push for the whole
  transition. The exam getters still held the correct values
  internally; they just never got serialized. Separately,
  `core_trainer_confirmPressed()` (called from BLE's `train_confirm`)
  opened with `if (!sessionActive) return;`, which now fired
  immediately in `DRILL_EXAM_DONE` — the OLED never hit this because
  its own exam-result screen dismisses via
  `ui_backend_trainClearExamResult()` directly
  (`ui_state.cpp`'s `handleTrainExamResult()`), bypassing
  `confirmPressed()` entirely, so only the BLE/WebSocket/mobile path
  was ever affected.
  Fixed by checking `core_trainer_isExamResultReady()` (not
  `sessionActive`) at the two points that mattered:
  `buildTrainStateJson()` now still emits the full `active:true`-shaped
  payload (mode/phase/target/exam fields) while an exam result is
  unconfirmed, and `confirmPressed()` now clears the result and resets
  to `DRILL_IDLE` *before* the `sessionActive` guard, instead of being
  silently swallowed by it. A related gap found via the new test
  suite's own state-isolation failing: `core_trainer_stopSession()`
  (the `train_stop` command) didn't clear the exam result either —
  stopping instead of confirming while an exam result was showing left
  it stuck under a `phase="IDLE"` payload no client shape expects; now
  fixed alongside the primary bug.
  - Files: `firmware/MORPHEUS/core_trainer.cpp`, `ble_control.cpp`
  - Tests: new `tests/native/test_core_trainer.cpp` (6 tests, first
    native coverage for `core_trainer.cpp`), each fix verified to
    actually fail against the pre-fix logic before confirming it
    passes against the fix. Real hardware: a full 25-round EXAM session
    driven over live BLE, confirming the terminal push carries
    `phase="EXAM_DONE"`, `examScorePercent=100`, `examPassed=true`,
    `examCorrectCount=25`, `examTotalCount=25`, and that
    `confirmTraining()` afterward resets to a clean `active:false`
    state instead of no-op'ing.

- **BLE virtual straight key could split one character into several**
  — e.g. ".." (should decode "I") sometimes decoded as "E" then "E".
  Root cause: both the physical and virtual key paths already fed the
  *same* single decoder (`core_decoder_addElement()`, via the shared
  `events_onKeyUp()` fan-out in `MORPHEUS.ino` — there was never a
  second decoder for BLE input), but the decoder's character/word-gap
  timeout is stamped from BLE-command-*arrival* time, not the
  operator's true release moment. A perfectly normal short pause
  between two virtual elements could exceed the gap threshold once BLE
  round-trip latency (connection interval + write-with-response ACK,
  for both writes of the *next* element) was added on top of it -
  especially at moderate/high WPM, where the gap window is only a few
  hundred milliseconds. Fixed by threading a `fromVirtualKey` flag
  (default `false`, physical call sites unchanged) through
  `events_onKeyUp()` into `core_decoder_addElement()`, and applying a
  new flat `BLE_KEY_GAP_COMPENSATION_MS` (250 ms, `config.h`) allowance
  to the gap check only when the pending character's/word's most
  recent element came from the virtual key. DIT/DAH classification
  itself, physical-key timing, and the BLE/WebSocket protocol contract
  are all unchanged.
  - Files: `firmware/MORPHEUS/config.h`, `core_decoder.h`,
    `core_decoder.cpp`, `core_keyer.h`, `MORPHEUS.ino`, `ble_control.cpp`
  - Tests: two new native regression tests in
    `tests/native/test_core_decoder.cpp`
    (`test_virtual_key_gap_compensation_prevents_premature_split`,
    `test_physical_key_gap_is_not_widened_by_compensation`), verified
    to actually fail against the pre-fix logic before confirming they
    pass against the fix.

### Changed
- **EXAM training results never reached any BLE/WebSocket client, and
  `train_confirm` silently did nothing once they were ready** — both
  bugs shared one root cause: `sessionActive` and
  `phase=DRILL_EXAM_DONE` flip together, atomically, in the same
  transition (`core_trainer.cpp`'s exam-completion branch). Once that
  happens, `buildTrainStateJson()` (`ble_control.cpp`) saw
  `!core_trainer_isSessionActive()` and emitted the bare
  `{"evt":"train_state","active":false}` shape with no `phase`,
  `examScorePercent`, `examPassed`, `examCorrect`, or `examTotal` —
  and since this push is rate-limited/deduped with no earlier frame
  carrying the result, that was the *only* push for the whole
  transition. The exam getters still held the correct values
  internally; they just never got serialized. Separately,
  `core_trainer_confirmPressed()` (called from BLE's `train_confirm`)
  opened with `if (!sessionActive) return;`, which now fired
  immediately in `DRILL_EXAM_DONE` — the OLED never hit this because
  its own exam-result screen dismisses via
  `ui_backend_trainClearExamResult()` directly
  (`ui_state.cpp`'s `handleTrainExamResult()`), bypassing
  `confirmPressed()` entirely, so only the BLE/WebSocket/mobile path
  was ever affected.
  Fixed by checking `core_trainer_isExamResultReady()` (not
  `sessionActive`) at the two points that mattered:
  `buildTrainStateJson()` now still emits the full `active:true`-shaped
  payload (mode/phase/target/exam fields) while an exam result is
  unconfirmed, and `confirmPressed()` now clears the result and resets
  to `DRILL_IDLE` *before* the `sessionActive` guard, instead of being
  silently swallowed by it. A related gap found via the new test
  suite's own state-isolation failing: `core_trainer_stopSession()`
  (the `train_stop` command) didn't clear the exam result either —
  stopping instead of confirming while an exam result was showing left
  it stuck under a `phase="IDLE"` payload no client shape expects; now
  fixed alongside the primary bug.
  - Files: `firmware/MORPHEUS/core_trainer.cpp`, `ble_control.cpp`
  - Tests: new `tests/native/test_core_trainer.cpp` (6 tests, first
    native coverage for `core_trainer.cpp`), each fix verified to
    actually fail against the pre-fix logic before confirming it
    passes against the fix. Real hardware: a full 25-round EXAM session
    driven over live BLE, confirming the terminal push carries
    `phase="EXAM_DONE"`, `examScorePercent=100`, `examPassed=true`,
    `examCorrectCount=25`, `examTotalCount=25`, and that
    `confirmTraining()` afterward resets to a clean `active:false`
    state instead of no-op'ing.

- **BLE virtual straight key could split one character into several**
  — e.g. ".." (should decode "I") sometimes decoded as "E" then "E".
  Root cause: both the physical and virtual key paths already fed the
  *same* single decoder (`core_decoder_addElement()`, via the shared
  `events_onKeyUp()` fan-out in `MORPHEUS.ino` — there was never a
  second decoder for BLE input), but the decoder's character/word-gap
  timeout is stamped from BLE-command-*arrival* time, not the
  operator's true release moment. A perfectly normal short pause
  between two virtual elements could exceed the gap threshold once BLE
  round-trip latency (connection interval + write-with-response ACK,
  for both writes of the *next* element) was added on top of it -
  especially at moderate/high WPM, where the gap window is only a few
  hundred milliseconds. Fixed by threading a `fromVirtualKey` flag
  (default `false`, physical call sites unchanged) through
  `events_onKeyUp()` into `core_decoder_addElement()`, and applying a
  new flat `BLE_KEY_GAP_COMPENSATION_MS` (250 ms, `config.h`) allowance
  to the gap check only when the pending character's/word's most
  recent element came from the virtual key. DIT/DAH classification
  itself, physical-key timing, and the BLE/WebSocket protocol contract
  are all unchanged.
  - Files: `firmware/MORPHEUS/config.h`, `core_decoder.h`,
    `core_decoder.cpp`, `core_keyer.h`, `MORPHEUS.ino`, `ble_control.cpp`
  - Tests: two new native regression tests in
    `tests/native/test_core_decoder.cpp`
    (`test_virtual_key_gap_compensation_prevents_premature_split`,
    `test_physical_key_gap_is_not_widened_by_compensation`), verified
    to actually fail against the pre-fix logic before confirming they
    pass against the fix.

### Changed
- **Keyer Mode moved from `CW Keyer` to `Settings → Keyer`** — it was the
  one keying-behavior setting living outside the group its siblings
  (WPM, Paddle Reverse, Iambic Mode, Weighting) already occupied. New
  path: **Menu → Settings → Keyer → Keyer Mode**. `PARAM_MODE` is
  dispatched by enum value everywhere it's read/written, not by menu
  position, so this is a pure menu-tree relocation with no change to
  keying behavior, persistence, or the settings schema.

---

## [v2.3.0] — LED Fixes & Diagnostics

**Status:** Hardware Verified
**Release:** Approved

### Fixed
- **Training/Farnsworth/Memory playback is now visible, not just audible** — `core_led_trainerFlashOn()`/`Off()` existed since the LED subsystem was introduced but had no caller anywhere; the status LED never flashed during training. It is now wired into the shared Morse playback engine (`core_morseplayer.cpp`), so every dit/dah played during a Training drill, Farnsworth practice, or Memory-keyer message now flashes the LED in sync with the tone.
- **BLE pairing-status LED no longer gets stuck blinking after the pairing window closes** — the LED inferred "pairing" from `bleEnabled + not connected` alone, never checking whether the radio was actually still advertising. Once the 60-second pairing window auto-expired without a connection, the radio went quiet but the LED kept blinking indefinitely. The LED now tracks real advertising state and goes dark exactly when the device stops being discoverable.

### Added
- **Diagnostics → LED Test** — instant LED ON / LED OFF / BLINK TEST actions for verifying the status LED on the bench, independent of BLE or training state. Blink test is a bounded, self-terminating pattern.
- `docs/FUNCTIONALITY_STATUS.md` — code-grounded audit of implemented vs. stubbed vs. implemented-but-unreachable functionality.
- `docs/USER_MANUAL.md` — full operator manual for the current feature set.

### Housekeeping
- `FIRMWARE_VERSION` bumped to match this release (was stale at `2.1.0` since v2.2.0).
- Removed the unused `LED_BLINK_SLOW_MS` constant.
- `transport.h`'s bond-reset comment no longer references a `PIN_BOND_RESET` that was never defined; documents the actual (menu-only) trigger.
- Added **Statistics → Reset Lifetime** — `core_stats_resetLifetime()` existed with no menu entry; a user could not previously clear lifetime statistics. Now reachable with a confirm dialog, same pattern as Factory Reset.
- README.md refreshed to match the current feature set (previously described the v1.1.0-era firmware).

### Compatibility
- No settings schema changes (`SETTINGS_VERSION` unchanged).
- No BLE protocol or JSON payload changes.

---

## [v2.2.0] — BLE Controls, Status LED & Date/Time

**Status:** Hardware Verified
**Release:** Approved

### Added
- Enhanced BLE controls and status handling, with status LED integration for BLE connection state.
- Callsign configuration, RTC (software clock) configuration, display configuration options, advanced keyer settings.
- **Outdoor** and **Silent** operator profiles, with per-profile display-contrast persistence.
- Date/time formatting support (3 date formats, 2 time formats) and improved RTC integration.
- Various UI and configuration-workflow improvements.

### Compatibility
- Users upgrading from v2.1.0 can update normally without additional migration steps.

---

## [v2.1.0] — Training, Statistics, Games & Profiles

**Status:** Hardware Verified
**Release:** Approved

### Added
- **Training subsystem**: Koch Method, Farnsworth, Character drills, Word drills, Callsign drills, Adaptive learning, Exam mode, progress tracking.
- **Statistics system**: session statistics, lifetime statistics, training accuracy, exam history, WPM history, persistent high scores.
- **CW Games**: Copy Challenge, Memory Challenge, Speed Challenge — with pause/resume, high-score persistence, and integrated statistics.
- **Operator Profiles**: four initial profile slots (Default, Portable, Contest, Practice), independently persisted, preserved during Factory Reset.
- **Audio improvements**: adjustable sidetone volume with live preview, sidetone enable/disable, persistent audio settings.
- New UI screens: volume adjustment, profile management, statistics, game interfaces.
- New module: `core_profiles`. Settings schema bumped to version 5.

---

## [v2.0.0] — OLED UI & Firmware Architecture Overhaul

**Status:** Hardware Verified
**Release:** Approved

### Added
- Fully redesigned 128×64 monochrome, Nokia-inspired OLED UI with rotary-encoder navigation, icon-based main menu, three-row submenus, and a full-screen parameter editor.
- New modular UI framework: `ui_backend`, `ui_renderer`, `ui_state`, `ui_menu`, `ui_screens`, `ui_input`, `ui_config`, `ui_layout`, `ui_fonts`, `ui_icons_anim`, `ui_splash_logo`, plus `core_memory`.
- Integrated CW Keyer menu (straight key/paddle configuration, Memory Messages, Live Monitor, Decoder toggle, Tune mode).
- Comprehensive Diagnostics module (system info, hardware diagnostics, runtime status, engineering tools).

### Breaking Changes
- Removed legacy WPM potentiometer support.
- OLED navigation moved to the rotary encoder module; straight key/DIT/DAH are no longer used for UI navigation and are dedicated exclusively to CW operation.

### Resource Usage
- ~697 KB flash (53%), ~40 KB RAM (12%) at release.

---

## [v1.2.2] — Legacy WPM Potentiometer Cleanup

**Status:** Hardware Verified
**Release:** Approved

### Removed
- Legacy analog WPM potentiometer support, its ADC initialization, and smoothing logic — freed GPIO34 for the (then) upcoming navigation keypad.

No functional regressions; internal architectural cleanup ahead of the v2.0.0 UI work.

---

## [v1.2.1] — BLE Bond Management & Stability

**Status:** Hardware Verified
**Release:** Approved

### Added
- Secure BLE bond reset functionality, first-time pairing workflow with passkey authentication, trusted-device management.
- Physical bond-reset button support (later superseded — see v2.3.0, no hardware pin was ultimately wired).
- Optional serial debug bond-reset command.
- Enhanced OLED pairing-status indicators.

### Fixed
- BLE pairing reliability and internal stability issues found during hardware validation.

---

## [v1.1.0] — Operator Settings Persistence

**Status:** Hardware Verified
**Release:** Approved

### Added
- **Operator settings persistence** — WPM, sidetone frequency, and paddle-reverse now survive a reboot, stored in NVS under a dedicated `morpheus_set` namespace (key `keyerSettings`), fully isolated from BLE bonding data.
- **Versioned settings storage** — a `SETTINGS_VERSION` field guards the stored format. A missing or version-mismatched blob falls back to defaults and is immediately rewritten in the current format, instead of silently reverting every boot.
- **Debounced NVS writes** — settings are written to flash only when something actually changed, and at most once per `SETTINGS_SAVE_DEBOUNCE_MS` (default 5000ms), to minimize flash wear.
- **Paddle reverse** — DIT/DAH can be swapped in iambic paddle mode; the setting persists across reboot. Straight key mode is unaffected (no second contact to swap).
- **Runtime sidetone frequency** — previously a compile-time constant, now adjustable at runtime and persisted across reboot.
- **Settings migration support** — `services_loadSettings()` detects outdated/invalid stored data via the version field and recovers cleanly rather than reading garbage.
- `services_factoryResetSettings()` — restores WPM/sidetone/paddle-reverse to factory defaults. No hardware trigger is wired to it yet; planned for v1.1 Feature 2 (Bond Reset).
- Temporary `FEATURE_DEBUG_SERIAL_COMMANDS` flag (off by default), with a serial command parser (`SET WPM`, `SET TONE`, `SET PADDLE_REV`, `RESET SETTINGS`) used to validate this feature on real hardware ahead of v1.2's real configuration menu. Requires `FEATURE_SERIAL=1`.

### Fixed
- **Sidetone range validation** — sidetone frequency is now clamped to `SIDETONE_FREQ_MIN_HZ`–`SIDETONE_FREQ_MAX_HZ` (200–2000 Hz) instead of accepting any value.
- **Version compatibility handling** — a version mismatch in stored settings is now explicitly detected and handled via automatic fallback-and-rewrite, rather than left unguarded.
- **Separate operator settings namespace** — settings storage uses its own NVS namespace (`morpheus_set`), distinct from BLE bonding's `cwkeyer` namespace, preventing any cross-contamination between settings reset and bond reset.
- **Improved settings recovery** — any NVS read failure now produces a clean, immediate fallback to defaults rather than undefined behavior.

### Compatibility
- No BLE protocol or JSON payload changes.
- No architectural changes — split-core structure (`core_keyer` / `core_decoder` / `display` / `transport` / `services`) unchanged.
- First-boot/unconfigured-board behavior is unchanged: falls back to existing `config.h` defaults (15 WPM, 600 Hz, paddle not reversed).
- BLE bonding/pairing state is unaffected by any part of this feature.
- All current feature flags (`FEATURE_OLED`, `FEATURE_BLE`, `FEATURE_SIDETONE`, `FEATURE_SERIAL`, `FEATURE_DEBUG_SERIAL_COMMANDS`) continue to work in their supported combinations.

### Files changed
`config.h`, `core_keyer.h`, `core_keyer.cpp`, `services.h`, `services.cpp`, `MORPHEUS.ino`

### Validated on real hardware
NVS settings persistence, debounced settings save, WPM persistence, sidetone persistence, paddle reverse persistence, settings migration logic, factory defaults, settings reset, BLE bonding unaffected, version upgrade handling, feature flag behavior, normal boot behavior, debug command validation.
