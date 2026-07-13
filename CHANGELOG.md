# Changelog

All notable changes to MORPHEUS are documented here.

Versioning follows a simple `MAJOR.MINOR.PATCH` scheme:
- **Major features** increment the minor version (e.g. v1.1.0, v1.2.0).
- **Bug fixes** increment the patch version (e.g. v1.1.1).

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