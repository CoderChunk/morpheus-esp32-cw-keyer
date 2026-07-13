# MORPHEUS Hardware Architecture & Capability Specification

**Status:** Living document — authoritative reference for MORPHEUS hardware planning
**Applies to:** All MORPHEUS hardware variants (OLED, TFT, and future revisions)
**Baseline firmware:** v1.2.2
**Document created:** 2026-07-04

This document is maintained alongside the firmware and hardware design. Any GPIO
assignment, new peripheral, or hardware variant added to MORPHEUS should update
this file in the same change set — it is not a one-time planning artifact.

---

## 1. Purpose & Scope

This specification defines:

- The GPIO resource budget for each MORPHEUS hardware variant
- Which features the current architecture supports, and at what cost
- Design rules that govern future hardware decisions
- Known open decisions and constraints for upcoming revisions

It does not define firmware implementation details, module APIs, or software
architecture — see `Guide.md` and the module headers (`core_keyer.h`,
`core_decoder.h`, `display.h`, `transport.h`, `services.h`) for that.

---

## 2. Baseline Platform

**MCU:** ESP32-WROOM-32

| Pin category | Pins | Notes |
|---|---|---|
| Total exposed GPIOs | 0,1,2,3,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39 | 26 pins on standard WROOM-32 dev pinout |
| UART0 (reserved, never repurposed) | 1, 3 | Serial programming and debug |
| Boot-strapping pins (case-by-case use only) | 0, 2, 5, 12, 15 | Sampled once at reset; safe for runtime use, risky for boot-window state — see §3 |
| **Safe general-purpose pool** | 4,13,14,16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39 | **19 pins** — the working budget for all allocation below |
| ADC1 (safe with WiFi/BLE active) | 32, 33, 34, 35, 36, 39 | Only 6 total; 34–39 are input-only, no pull resistors, no output capability |
| ADC2 (unreliable once WiFi is active) | 0, 2, 4, 12, 13, 14, 15, 25, 26, 27 | Fine for digital I/O; never assign for analog use |

---

## 3. Design Rules

These rules govern every future hardware decision for MORPHEUS. Deviating from
any of them should be a deliberate, documented choice, not a default.

1. **Never use ADC2 pins for analog input.** WiFi (implied by the roadmap's OTA
   update goal) makes ADC2 conversions unreliable. All analog inputs (nav
   ladder, battery voltage, any future analog sensor) must live on ADC1 pins
   (32, 33, 34, 35, 36, 39).
2. **Treat boot-strapping pins with explicit justification, not convenience.**
   Any assignment to GPIO0, 2, 5, 12, or 15 must document why the pin's
   strapping function is compatible with the assigned role's boot-time state.
   Never assign a strapping pin to a user-operated button — the boot-window
   risk (forcing an unintended boot mode) is specific to something a user can
   physically hold down during power-on.
3. **Preserve the I²C bus (GPIO21/22) as a shared, multi-drop resource.**
   Do not spend it on single-purpose GPIO needs. RTC, ambient light sensing,
   fuel gauge, and future sensors should all join this bus rather than
   requesting dedicated pins.
4. **SPI bus reuse is variant-specific, not automatic.** The TFT variant's
   display SPI bus (SCK/MOSI/MISO) can be shared with SD card or future SPI
   peripherals by adding only a chip-select line. The OLED variant has no SPI
   bus by default — adding one (e.g. for SD card support) is an architectural
   decision requiring 3–4 new pins, and should be made deliberately, not
   discovered mid-layout.
5. **OLED and TFT GPIO budgets are tracked independently and never combined.**
   They are separate PCBs, each with its own 19-pin safe pool. A feature being
   "affordable" on one variant does not imply it is affordable on the other.
6. **Only one display backend compiles into a given firmware build.**
   Application logic must remain independent of which display backend is
   present, per the existing architecture.
7. **Prefer zero-GPIO-cost hardware patterns over discrete, GPIO-hungry ones.**
   Addressable RGB LEDs over 3 discrete channels; I²C sensors over analog
   ones; bus-sharing over dedicated lines; status LEDs driven directly from a
   charge IC over MCU-mediated status pins. This is a standing preference, not
   a one-time optimization.
8. **Timing-critical inputs get dedicated digital pins; non-critical inputs
   may share an analog ladder or a bus.** This preserves the timing-domain
   separation that is central to the existing firmware architecture (see
   `Guide.md`) — the keyer's real-time path must never depend on ADC
   conversion latency or shared-bus arbitration.
9. **Electrically shared inputs must not require firmware to know they're
   shared.** Where a built-in control (e.g. a practice key/paddle button) is
   wired in parallel with an existing input (e.g. the external key/paddle
   jack), this must remain invisible to firmware — `core_keyer` should never
   need special-case logic to distinguish the two sources.
10. **PWM resources (LEDC channels) are not a shared constraint.** Sidetone,
    backlight, and haptic drive can each run on independent LEDC
    channels/timers concurrently. GPIO count is the real constraint, not PWM
    channel availability.

---

## 4. Hardware Variants

MORPHEUS ships as two hardware variants sharing the same firmware core:

- **OLED variant** — compact, lower-cost, lower-power. Current production
  hardware (v1.2.2).
- **TFT variant** — premium, richer graphical interface, PWM-controlled
  backlight for adjustable brightness and power management. Planned.

Both variants support: external straight key/paddle, built-in straight
key/paddle (electrically independent of the UI buttons), BLE telemetry, and
the full navigation/menu button set.

---

## 5. GPIO Allocation — OLED Variant

| Pin | Function | Status |
|---|---|---|
| 21 | OLED SDA | Current |
| 22 | OLED SCL | Current |
| 25 | Jack tip — DIT / straight key (shared node with built-in equivalent) | Current, extended role |
| 26 | Jack ring — DAH (shared node with built-in equivalent) | Current, extended role |
| 18 | Buzzer / sidetone | Current |
| 33 | Mode switch | Current |
| 27 | Bond reset | Current |
| 34 | Nav ladder (Up/Down/Left/Right), ADC1 | Reserved |
| 13 | OK (menu, dedicated) | Planned |
| 14 | Select (menu, dedicated) | Planned |
| 16 | Back (menu, dedicated) | Planned |

**Committed: 11 of 19. Free: 8** → GPIO4, 17, 19, 23, 32, 35, 36, 39.

## GPIO Allocation — TFT Variant

| Pin | Function | Status |
|---|---|---|
| 18 | TFT SCK | Planned (default hardware-SPI mapping) |
| 23 | TFT MOSI | Planned (default hardware-SPI mapping) |
| 19 | TFT MISO | Planned (default hardware-SPI mapping) |
| 5 | TFT CS | Planned — strapping pin, reviewed low-risk for this role |
| 2 | TFT DC | Planned — strapping pin, reviewed low-risk for this role |
| 4 | TFT RST | Planned |
| 25 | Jack tip (shared node) | Carried over |
| 26 | Jack ring (shared node) | Carried over |
| 33 | Mode switch | Carried over |
| 27 | Bond reset | Carried over |
| 34 | Nav ladder, ADC1 | Reserved |
| 13 | OK (menu, dedicated) | Planned |
| 14 | Select (menu, dedicated) | Planned |
| 16 | Back (menu, dedicated) | Planned |
| 17 | Buzzer / sidetone (relocated — GPIO18 now SCK) | Planned |
| 32 | Backlight PWM → MOSFET gate | Planned |

**Committed: 14 of 19 (plus 2 of 5 strapping pins for CS/DC). Free: 5** →
GPIO21, 22, 35, 36, 39.

---

## 6. Reserved / Intentionally Unused GPIOs (both variants)

| Pins | Reason |
|---|---|
| 1, 3 | UART0 — never repurposed |
| 0, 12, 15 | Boot-strapping, unassigned on both variants — held as cautious reserve |
| 2, 5 | Strapping, assigned on TFT only (DC, CS) — validated low-risk for these specific roles; must not be assumed safe for other roles without re-review |

---

## 7. Remaining Budget

| Variant | Free pins | Composition |
|---|---|---|
| OLED | 8 | 4 flexible non-ADC (4,17,19,23) + 4 ADC1-capable (32,35,36,39) |
| TFT | 5 | I²C bus intact (21,22) + 3 ADC1-capable (35,36,39) |

Both variants retain their full ADC1 headroom minus what the nav ladder and
(TFT only) backlight already use, plus 3 unassigned strapping pins per
variant held in deep reserve.

---

## 8. Feature Classification

### Supported by Software Architecture
*No additional GPIOs or hardware required — enabled by the existing modular,
event-driven firmware architecture.*

- Koch Method
- Farnsworth Method
- Morse Decoder
- Memory Keyer
- Statistics
- Configuration System
- BLE Services
- Profiles
- Accessibility Modes
- Themes
- Future UI improvements

### Supported
*Current GPIO allocation fully supports the feature with no additional
hardware beyond the base design.*

- External straight key
- External iambic paddle
- Built-in straight key
- Built-in iambic paddle
- OLED variant
- TFT variant
- BLE
- Battery voltage monitoring *(resistor divider assumed part of base hardware design)*
- Audio sidetone
- Navigation buttons
- Menu buttons
- Firmware update capability
- Debug serial

### Supported with Hardware Variant Limitations
*Supported, but dependent on which board (OLED vs TFT).*

- PWM TFT backlight *(TFT only — no equivalent concept on OLED)*
- SD card *(TFT: shares existing SPI bus, +1 CS pin. OLED: requires a new SPI bus from scratch)*
- SPI peripherals, general *(same OLED/TFT asymmetry)*

### Supported with Additional Hardware
*GPIO budget and architecture support these; they require external circuitry beyond the base board.*

| Feature | Additional Hardware | GPIO Cost | Interface | Priority | Comments |
|---|---|---|---|---|---|
| USB-C charging | USB-C connector + charge management IC (e.g. BQ24075-class) | 0 | Standalone | Core | Runs independently of firmware; add a GPIO only if software needs to gate/enable charging |
| USB-C charging status | Status LEDs off the charge IC directly, or GPIO inputs if firmware reads state | 0–2 | GPIO | Recommended | Zero-GPIO baseline via dedicated LEDs; spend pins only for an on-screen charge icon |
| Battery fuel gauge | Fuel gauge IC (e.g. MAX17048-class) | 0 | I²C | Recommended | Shares existing bus; materially better runtime estimate than the ADC divider alone |
| RGB status LED | Addressable RGB LED (WS2812-class) | 1 | GPIO (single-wire) | Optional | Prefer over 3 discrete LEDs; timing-sensitive protocol, keep data line short |
| Haptic feedback | Coin vibration motor + MOSFET + flyback diode | 1 | GPIO (PWM) | Optional | Same driver pattern as backlight; motor is inductive, needs flyback protection unlike an LED |
| Ambient light sensor | BH1750-class sensor | 0 | I²C | Optional | Shares existing bus; pairs naturally with TFT's PWM backlight for auto-brightness |
| RTC | RTC IC (e.g. DS3231-class) + backup cell | 0 (poll) / 1 (interrupt) | I²C (+ GPIO if using alarm) | Optional | Interrupt pin only needed for wake/alarm use; simple polling needs nothing extra |
| I²C peripherals, general | Varies (sensor, RTC, fuel gauge, etc.) | 0 per device | I²C | Recommended | Multi-drop bus; each added device just needs a unique address |
| PTT output | Transistor or optocoupler + connector | 1 | GPIO | Recommended | Optocoupler preferred for galvanic isolation from connected radio equipment |
| Line-out | Coupling capacitor + resistor network + audio jack | 0 | Existing PWM | Recommended | Reuses the existing sidetone signal; simple RC filtering removes the PWM carrier |
| Headphone output | Small headphone amplifier + coupling caps + jack | 0 | Existing PWM (or I²S) | Optional | PWM alone can't drive headphone impedance directly; I²S upgrade path costs 3 GPIOs |
| Speaker amplifier | Class-D amp IC, analog-in or I²S digital-in (e.g. MAX98357-class) | 0 (analog-in) / 3 (I²S) | Existing PWM, or I²S | Optional | Mirrors headphone trade-off; I²S is higher quality but a larger architectural step |
| GPS | GPS module | 2 | UART (second instance) | Optional | UART0 stays reserved for programming/debug; a second UART is available via the GPIO matrix |

### Not Currently Supported
*Would require significant redesign of the hardware architecture, not just added components.*

- Touchscreen
- Camera
- Microphone
- Cellular
- Wi-Fi features requiring additional hardware

### Reserved for Future Expansion
- Future expansion header

---

## 9. Future Expansion Strategy

- **Expansion header:** Reserve a small header (2–3 spare GPIOs + 3.3V + GND
  + the I²C bus) on future revisions rather than letting roadmap items
  quietly consume every remaining pin. Whether this is populated in the first
  revision or left as unpopulated pads is an open decision (see §10).
- **I²C-first for sensors:** Ambient light, RTC, and fuel gauge should default
  to I²C parts specifically because they compound — every I²C addition after
  the first costs zero further GPIOs, unlike ADC or discrete-GPIO parts.
- **ADC1 stewardship:** Only 6 ADC1 pins exist across the entire platform.
  Track their use carefully as features accumulate; do not let a future
  feature "just borrow" an ADC1 pin for a purely digital purpose the way
  GPIO33 (mode switch) already does today (see §10).
- **SPI on OLED is a decision point, not a default.** If SD card or other SPI
  peripherals become a near-term commitment rather than speculative, adding a
  dedicated SPI bus to the OLED variant should be scoped and reviewed on its
  own, since it is the one place the two variants diverge in capability by
  default.

---

## 10. Open Decisions Requiring Sign-off Before Layout

1. **OLED + SD card:** Is SD card support in scope for the OLED variant? If
   yes, a dedicated SPI bus (3–4 pins) must be added to that variant's
   allocation before layout.
2. **Fuel gauge vs. ADC divider:** Does the first battery-powered revision
   ship with the simple resistor-divider approach, or commit to a fuel gauge
   IC from the start? Both are supported; this is a cost/accuracy trade-off,
   not a GPIO question.
3. **Expansion header:** Populate in the first revision, or reserve pads only?
4. **GPIO33 reassignment:** The mode switch currently occupies an ADC1-capable
   pin for a purely digital function. Not urgent, but flagged as a candidate
   to reassign to a non-ADC pin if ADC1 demand tightens in a future revision.

---

## 11. Revision History

| Version | Date | Summary |
|---|---|---|
| 1.0 | 2026-07-04 | Initial consolidation: OLED/TFT GPIO allocation, design rules, feature classification, and open decisions from hardware architecture review. |