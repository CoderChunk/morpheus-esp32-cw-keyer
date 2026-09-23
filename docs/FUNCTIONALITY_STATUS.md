# MORPHEUS Functionality Status

Firmware version at time of writing: **2.1.0** (`config.h`, `FIRMWARE_VERSION`).

`README.md` and `CHANGELOG.md` describe an earlier state of the project (last
CHANGELOG entry is v1.1.0). Five feature commits have landed since then
(training, stats/games, profiles/audio, settings/RTC/keyer controls, BLE/LED)
that are not reflected in either document. This file is a code-grounded
inventory of what is actually implemented, to close that gap.

---

## 1. Implemented and fully wired (core logic → NVS persistence → menu UI)

| Subsystem | Status | Notes |
|---|---|---|
| **Keying** | ✅ | Straight key + Iambic Mode A/B, WPM 5–40 (default 18), weighting 30–70% (default 50), paddle reverse, sidetone 200–2000 Hz, volume 0–100% (PWM duty, not calibrated loudness) |
| **Decoding** | ✅ | Live dit/dah → character/word decode, A–Z/0–9 + basic punctuation (37-entry table), char gap 3.0×dit, word gap 7.0×dit, runtime enable/disable |
| **Training** | ✅ | 6 modes: Koch (41-char standard order, level-up at 90%/20-attempt window), Characters, Words (20-word ham vocabulary), Callsigns (procedurally generated, not real), Adaptive (±1/−2 WPM), Exam (25 chars, 90% pass). Farnsworth spacing playback (approximate, not full ARRL formula) |
| **Games** | ✅ | Copy Challenge (falling-character reaction), Memory Challenge (Simon-style chain, cap 20), Speed Challenge "Overdrive" (fixed-tempo beat, accelerates every 3 combo). All: lives, scoring, persisted high scores, pause/resume |
| **Statistics** | ✅ | Session (RAM) + lifetime (persisted): chars/words/elements keyed, uptime, per-training-mode attempts/correct, exams taken/passed, best exam %, peak adaptive WPM, boot-time WPM history (6-entry ring buffer) |
| **Profiles** | ✅ | 6 named presets (Default, Portable, Contest, Practice, Outdoor, Silent) storing WPM/tone/paddle-reverse/mode/volume/sidetone/contrast, own NVS namespace, survives factory reset |
| **Memory keyer** | ✅ | 5 fixed canned-text slots (CQ CQ CQ / 599 599 / TU TU / QRZ? / AR AR), shared playback engine with Training/Games |
| **Clock** | ✅ (by design, software-only) | Manual set, no RTC chip, no NTP — drifts on internal oscillator, resets on reboot by design. Date format (3) and time format (2) choices persist; the time value itself does not |
| **Status LED** | ✅ (mostly) | Single LED, patterns for pairing (fast blink) and connect-confirm (3 pulses then dark), TX keydown pulse — gated by a user-visible BLE-LED preference |
| **BLE telemetry** | ✅ | NimBLE, one characteristic pushing JSON per completed word, MITM+bonding+LE Secure Connections, single-trusted-device allowlist, bounded 60s pairing window, BLE on/off persisted (default off) |
| **Settings persistence** | ✅ | Versioned NVS blob (`SETTINGS_VERSION=8`), debounced writes (5s), clean fallback-and-upgrade on version/size mismatch, factory reset (UI-triggered only) |
| **Menu/UI** | ✅ | 10 top-level branches (CW Keyer, Training, Statistics, Connectivity, Profiles, Settings, Diagnostics, Tools, Games, Help), rotary encoder + confirm/back buttons, ~9 diagnostic screens |

---

## 2. Explicitly stubbed — not implemented

These appear in the menu and show **"Feature not yet"** when selected. They are intentional placeholders, not broken features:

- **Connectivity → Wi-Fi**
- **Connectivity → Keyboard** (likely USB/BLE HID keyboard output of decoded text)
- **Connectivity → Firmware Update** (OTA)
- **Tools → Battery** (battery monitoring — no fuel gauge / voltage divider wired)
- **Tools → "Coming Soon"** (unallocated slot)

## 3. Implemented in code but not reachable — loose ends

These are real gaps worth prioritizing before the next release, since the code exists but a user (or the firmware itself) can't actually trigger it:

- **No hardware bond-reset button.** `transport.h` documents a "boot-time held button (see `PIN_BOND_RESET`)" but `PIN_BOND_RESET` is not defined anywhere. Bond reset only works via the menu (Connectivity → Bluetooth → Bond Reset) or the disabled debug-serial command.
- **`core_stats_resetLifetime()` has no menu entry.** The function exists and works, but nothing in the UI calls it — a user cannot clear lifetime statistics.
- **LED Morse trainer flash is unwired.** `core_led_trainerFlashOn()`/`Off()` are defined, exported, and never called from anywhere else in the firmware. A `DEFAULT_LED_TRAINER_WPM` constant exists in `config.h` suggesting a visual-copy training mode was planned but never connected to the Training menu.
- **`LED_BLINK_SLOW_MS` is a dead constant.** Defined for "BLE advertising" but the actual LED logic uses `LED_BLINK_FAST_MS` for that state instead.
- **`FEATURE_DEBUG_SERIAL_COMMANDS`** (serial `SET WPM`/`SET TONE`/`RESET SETTINGS`/`RESET BOND`) is off by default and described as temporary/validation-only — not a user-facing feature.

## 4. Test coverage gap

- `tests/test_decoder_logic.py` tests a **parallel Python reimplementation** of the decoder's timing logic, not the actual C++ source — a real decoder regression would not be caught by this test.
- `tests/test_ble_json_budget.py` checks `config.h` constant arithmetic, not `transport.cpp` behavior.
- No automated tests exist for: keyer (iambic timing, weighting), trainer (Koch level-up, adaptive WPM, exam scoring), games, statistics persistence, profiles, clock, LED, or the UI state machine.

## 5. Stale documentation identified

- `README.md`: "Future Development" section lists Koch training, statistics, games, and adaptive timing as *ideas for contributors* — all are already implemented. Project structure diagram and pin table are also out of date (missing `PIN_STATUS_LED`, missing all newer source files).
- `CHANGELOG.md`: stops at v1.1.0; firmware is at 2.1.0 with five unlogged feature releases.
- `ui_mockdata.h` comment claims callsign/date/time have "no NVS/RTC backend" — callsign and date/time *format* are now persisted; only the live clock value has no backend, which is an intentional design choice (no RTC hardware), not a gap.
- `core_profiles.cpp` header comment says "Four fixed, named preset slots" — actual count is 6.

---

## Recommended next steps

1. Wire a hardware bond-reset trigger, or update `transport.h`'s comment to stop referencing a pin that doesn't exist.
2. Add a Statistics → Reset Lifetime menu action (with a confirm dialog, matching the existing factory-reset pattern).
3. Either wire `core_led_trainerFlashOn/Off()` into a Training mode, or remove the dead code and the unused `DEFAULT_LED_TRAINER_WPM` constant.
4. Refresh `README.md` and `CHANGELOG.md` to match the current v2.1.0 feature set (see the accompanying `USER_MANUAL.md` for the full current feature list).
5. Replace or supplement `test_decoder_logic.py` with a test that actually exercises `core_decoder.cpp`.
