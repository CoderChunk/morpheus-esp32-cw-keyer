# Changelog

All notable changes to MORPHEUS are documented here.

Versioning follows a simple `MAJOR.MINOR.PATCH` scheme:
- **Major features** increment the minor version (e.g. v1.1.0, v1.2.0).
- **Bug fixes** increment the patch version (e.g. v1.1.1).

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
