# MORPHEUS Functionality Status

Firmware version at time of writing: **2.4.0** (`config.h`, `FIRMWARE_VERSION`).

`README.md` and `CHANGELOG.md` are both current as of this writing. This file
remains a code-grounded inventory of what is actually implemented, independent
of what either of those documents claims - useful for catching the next time
they drift.

---

## 1. Implemented and fully wired (core logic → NVS persistence → menu UI)

| Subsystem | Status | Notes |
|---|---|---|
| **Keying** | ✅ | Straight key + Iambic Mode A/B, WPM 5–40 (default 18), weighting 30–70% (default 50), paddle reverse, sidetone 200–2000 Hz, volume 0–100% (PWM duty, not calibrated loudness) |
| **Decoding** | ✅ | Live dit/dah → character/word decode, A–Z/0–9 + basic punctuation (37-entry table), char gap 3.0×dit, word gap 7.0×dit, runtime enable/disable. Home screen shows each character the instant it decodes (not only on word completion) and a live dit/dah readout in the header while mid-key, toggleable (Settings → Display → Live Pattern) |
| **Training** | ✅ | 6 modes: Koch (41-char standard order, level-up at 90%/20-attempt window), Characters, Words (20-word ham vocabulary), Callsigns (procedurally generated, not real), Adaptive (±1/−2 WPM), Exam (25 chars, 90% pass). Farnsworth spacing playback (approximate, not full ARRL formula) |
| **Games** | ✅ | Copy Challenge (falling-character reaction), Memory Challenge (Simon-style chain, cap 20), Speed Challenge "Overdrive" (fixed-tempo beat, accelerates every 3 combo). All: lives, scoring, persisted high scores, pause/resume |
| **Statistics** | ✅ | Session (RAM) + lifetime (persisted): chars/words/elements keyed, uptime, per-training-mode attempts/correct, exams taken/passed, best exam %, peak adaptive WPM, boot-time WPM history (6-entry ring buffer) |
| **Profiles** | ✅ | 6 named presets (Default, Portable, Contest, Practice, Outdoor, Silent) storing WPM/tone/paddle-reverse/mode/volume/sidetone/contrast, own NVS namespace, survives factory reset |
| **Memory keyer** | ✅ | 5 fixed canned-text slots (CQ CQ CQ / 599 599 / TU TU / QRZ? / AR AR), shared playback engine with Training/Games |
| **Clock** | ✅ (by design, software-only) | Manual set, no RTC chip, no NTP — drifts on internal oscillator, resets on reboot by design. Date format (3) and time format (2) choices persist; the time value itself does not |
| **Status LED** | ✅ (mostly) | Single LED, patterns for pairing (fast blink) and connect-confirm (3 pulses then dark), TX keydown pulse — gated by a user-visible BLE-LED preference |
| **BLE telemetry** | ✅ | NimBLE, one characteristic pushing JSON per completed word, MITM+bonding+LE Secure Connections, multi-device trusted allowlist (up to `BLE_TRUSTED_DEVICE_CAP`=3 remembered devices, one active connection at a time), bounded 60s pairing window, BLE on/off persisted (default off) |
| **BLE remote control** | ✅ | `ble_control.cpp` — a second read/write characteristic pair (`BLE_CONTROL_CMD/EVT_UUID`) lets a connected client start/stop/confirm Training (all 6 modes) and Games (all 3), including a virtual straight key (`key_down`/`key_up`, classified DIT/DAH server-side like a real key) so drills/games can be played entirely over BLE. Live `train_state`/`game_state` JSON pushed on change. Reference client: `tools/ble_client/` (PySide6 desktop app) |
| **Settings persistence** | ✅ | Versioned NVS blob (`SETTINGS_VERSION=9`), debounced writes (5s), clean fallback-and-upgrade on version/size mismatch, factory reset (UI-triggered only) |
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
- **`DEFAULT_LED_TRAINER_WPM`** (`config.h`) is a dead constant — never read anywhere. `core_led_trainerFlashOn()`/`Off()` themselves are *not* dead (corrected from an earlier version of this doc): `core_morseplayer.cpp`'s shared playback engine calls them to flash the LED in sync with the audible tone during Training/Farnsworth/Memory-message playback. What never got built is a standalone WPM-driven visual-copy trainer mode this constant implies was once planned.
- **`FEATURE_DEBUG_SERIAL_COMMANDS`** (serial `SET WPM`/`SET TONE`/`RESET SETTINGS`/`RESET BOND`) is off by default and described as temporary/validation-only — not a user-facing feature.

## 4. Test coverage gap

- ~~`tests/test_decoder_logic.py` tests a parallel Python reimplementation, not the actual C++ source~~ — **fixed**: `tests/native/` now compiles and links `firmware/MORPHEUS/core_decoder.cpp` itself (host g++, no ESP32 toolchain needed) and exercises it directly, including PROGMEM table lookup, reverse lookup, enable/disable state clearing, and training-sink routing — none of which the Python mirror could test since it doesn't implement them. Verified to actually catch a real decoder regression (a DIT/DAH classification bug) that the Python mirror passed cleanly through. Run with `tests/native/run.sh`. `test_decoder_logic.py` is kept as-is (still a fast, dependency-free timing-model sanity check).
- `tests/native/test_core_trainer.cpp` covers exam completion/scoring and `train_confirm` state transitions (the bug class described in `CHANGELOG.md`'s `[Unreleased]` EXAM fix). Koch level-up and adaptive-WPM logic are not covered.
- `tests/native/test_core_keyer.cpp` covers straight-key DIT/DAH classification, WPM/weight/volume/sidetone clamping, mode-change mid-element state reset, and iambic paddle behavior - including the actual Mode A vs B distinction (whether a squeeze released mid-element still sends one extra alternating element), not just that paddles produce elements at all. Verified to actually catch a real classification regression (dit/dah threshold comparison flipped) before being added.
- `tests/test_ble_json_budget.py` checks `config.h` constant arithmetic, not `transport.cpp` behavior.
- No automated tests exist for: Koch level-up / adaptive-WPM trainer logic, games, statistics persistence, profiles, clock, LED, or the UI state machine. The `tests/native/` harness (Arduino.h stub + linking the real .cpp) is reusable for these.

## 5. Stale documentation identified

An earlier version of this section flagged several gaps that have since been
resolved (kept here only so the fix is traceable) — `README.md`'s "Future
Development" section, its project-structure tree and pin table,
`ui_mockdata.h`'s callsign/date/time comment, and `core_profiles.cpp`'s
preset-count comment have all been corrected. `CHANGELOG.md` is current
through v2.4.0, not stuck at v1.1.0 as previously claimed here.

Currently open:
- None identified as of this pass (firmware v2.4.0). Re-audit whenever a
  feature lands without a matching doc update — that's how the items above
  accumulated in the first place.

---

## Recommended next steps

1. Wire a hardware bond-reset trigger, or update `transport.h`'s comment to stop referencing a pin that doesn't exist.
2. Either wire `DEFAULT_LED_TRAINER_WPM` into a standalone visual-copy training mode, or remove the unused constant.
3. Add automated test coverage for games, stats, profiles, clock, LED, the UI state machine, and trainer's Koch/adaptive-WPM logic (`tests/native/`'s pattern — Arduino.h stub + linking the real .cpp — is reusable here; keyer is now covered, see §4).
