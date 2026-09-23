# MORPHEUS User Manual

**Firmware version 2.1.0** — ESP32 CW keyer with real-time decoding, OLED
interface, training modes, games, and secure Bluetooth telemetry.

---

## 1. Getting Started

### Controls

| Control | Location | Function |
|---|---|---|
| Rotary encoder | Front panel | Turn to scroll lists/menus, adjust values |
| Encoder push | Press the knob | Same as Confirm |
| Confirm button | Front panel | Select / enter |
| Back button | Front panel | Go back one level. **Hold ~1 second** to jump straight to the Home screen |
| Key jack (Tip) | Side jack | Straight key input, or paddle DIT |
| Key jack (Ring) | Side jack | Paddle DAH (iambic mode only) |

### First boot

On first power-up, MORPHEUS loads factory defaults:

- 18 WPM, straight key mode, 600 Hz sidetone at 80% volume
- Decoder enabled, Bluetooth **off**
- No callsign, no date/time set

Nothing needs to be configured to start keying — plug in a straight key or
paddle and go.

---

## 2. CW Keying

From the **Home** screen, keying works immediately.

- **Straight key mode**: tap the Tip contact. MORPHEUS classifies dits vs.
  dahs from hold duration.
- **Paddle (iambic) mode**: Tip = DIT, Ring = DAH, Iambic Mode B by default
  (squeeze both paddles for alternating dit-dah memory).
- Switch keying mode: **Menu → CW Keyer → Keyer Mode**.
- Decoded text appears live on the Home screen as you key.

### Adjustable keying parameters (Menu → Settings → Keyer)

| Setting | Range | Default |
|---|---|---|
| WPM | 5–40 | 18 |
| Paddle Reverse | on/off | off |
| Iambic Mode | A / B | B |
| Weighting | 30–70% | 50% (standard 1:1) |

### Audio (Menu → Settings → Audio)

| Setting | Range | Default |
|---|---|---|
| Sidetone frequency | 200–2000 Hz | 600 Hz |
| Sidetone on/off | — | on |
| Volume | 0–100% | 80% |

> Volume is a PWM loudness approximation (no amplifier/DAC on this
> hardware) — it is not a calibrated dB scale.

### Memory Keyer (Menu → CW Keyer → Memory Msgs)

Five fixed canned messages, triggered with one button press each:

1. CQ CQ CQ
2. 599 599
3. TU TU
4. QRZ?
5. AR AR

Touching the key or paddle during playback stops it immediately and hands
control back to you.

### Tune mode (Menu → CW Keyer → Tune)

Emits a continuous test tone for antenna/sidetone checks without keying the
transmitter logic.

### Live Monitor (Menu → CW Keyer → Live Monitor)

Full-screen live decode view, larger text than the Home screen.

---

## 3. Real-Time Decoder

Decodes A–Z, 0–9, and basic punctuation (`. , ? / = + -`) from your keying,
finalizing a character after a 3-dit silence and a word after a 7-dit
silence. Unrecognized patterns show as `?`.

Toggle on/off: **Menu → CW Keyer → Decoder**.

---

## 4. Training

**Menu → Training** — six modes, all scored and tracked in Statistics.

| Mode | What it drills |
|---|---|
| **Koch Method** | Standard Koch character order (K, M, U, R, ...). Starts at level 2. Get ≥90% correct over a rolling 20-attempt window to advance a level. |
| **Characters** | Random single character from the full A–Z/0–9 set |
| **Words** | Random word from a 20-word ham-radio vocabulary (CQ, DE, QTH, RST, 73, ...) |
| **Callsigns** | Randomly generated practice callsigns (not real assigned calls) |
| **Adaptive** | Speed auto-adjusts: +1 WPM after 3 correct in a row, −2 WPM on a miss |
| **Exam Mode** | Fixed 25-character test, 90% required to pass. Result (score, pass/fail) stays on screen until dismissed |

Each round: MORPHEUS plays a target character/word, you key it back, and get
immediate correct/incorrect feedback.

### Farnsworth spacing (Menu → Training → Farnsworth)

Plays a fixed practice phrase ("PARIS PARIS CQ DE TEST") at full character
speed but with stretched spacing between letters/words, so you learn
character sounds at a real WPM while getting extra thinking time. This is an
approximation of the classic Farnsworth timing formula, not the exact ARRL
specification.

---

## 5. Games

**Menu → Games** — three arcade-style drills. Playing them also advances
your real Koch training progress. Games are mutually exclusive with
Training sessions (only one can run at a time).

| Game | How it works | Lives |
|---|---|---|
| **Copy Challenge** | A character "falls" — key it back before time runs out. Faster at higher Koch levels. Correct = points + streak bonus. | 3 |
| **Memory Challenge** | Simon-style: MORPHEUS plays a growing chain of characters, you key it back in full each round (max chain length 20). One mistake ends the run. | — |
| **Speed Challenge (Overdrive)** | A character plays on a fixed beat; key it in time. Every 3 correct in a row, the beat speeds up. | 3 |

Each game screen has **Start**, **Controls**, **Help**, and **High Score**
options. Pause/resume and restart are available mid-game via the Confirm/
Back buttons. High scores persist across reboots and are **not** cleared by
Factory Reset.

---

## 6. Statistics

**Menu → Statistics**

- **Session** — characters, words, and elements keyed, and uptime, since
  the current power-on (resets every reboot).
- **Lifetime** — cumulative totals, saved to flash: chars/words/elements
  keyed, total uptime, training attempts/correct per mode, exams taken/
  passed, best exam score, peak adaptive-training WPM.
- **Progress** / **Accuracy** — per-mode training breakdown.
- **Speed History** — your WPM at the start of your last 6 sessions
  (boot-time snapshots, not a live trend — the firmware has no RTC to
  timestamp a real trend line).

> Lifetime statistics currently have no reset option in the menu — they are
> preserved indefinitely (and survive Factory Reset by design).

---

## 7. Profiles

**Menu → Profiles** — six presets bundling WPM, tone, paddle-reverse,
keying mode, volume, sidetone on/off, and display contrast into one saved
setup. Load instantly switches your active configuration; Save overwrites a
slot with your current settings.

| Profile | WPM | Tone | Mode | Volume | Notes |
|---|---|---|---|---|---|
| Default | 18 | 600 Hz | Straight | 80% | |
| Portable | 15 | 700 Hz | Paddle | 50% | |
| Contest | 25 | 800 Hz | Paddle | 90% | |
| Practice | 12 | 600 Hz | Straight | 70% | |
| Outdoor | 18 | 800 Hz | Paddle | Max | Max contrast, for sunlight |
| Silent | 15 | 600 Hz | Straight | Min | Sidetone off, low contrast |

Profiles are not affected by Factory Reset — they're a recovery path if you
reset your live settings by mistake.

---

## 8. Display & System Settings

**Menu → Settings → Display**

- Contrast: 10–255 (default 128)
- Invert on/off
- Screen timeout: 15s / 30s / 60s / 120s / 5min / 10min / 15min / 30min /
  Never (default: Never)

**Menu → Settings → System**

- **Callsign**: up to 12 characters, with a show/hide toggle on the Home screen
- **Date & Time**: manually set date/time (see note below), plus format
  pickers:
  - Date format: YYYY-MM-DD / DD-MM-YYYY / MM-DD-YYYY
  - Time format: 24-hour / 12-hour
- **About**: firmware version and build info
- **Restart**: soft reboot
- **Factory Reset**: restores keyer, decoder, Koch level, audio, display,
  callsign, date/time format, and BLE-enabled settings to defaults.
  **Does not** erase Profiles, lifetime Statistics, or Game high scores.

> MORPHEUS has no real-time-clock chip and does not sync over the network.
> The clock is a software counter set manually from the menu — it drifts
> slightly over time and resets to "unset" on every reboot. Date/time
> *format* preferences persist; the actual date/time value does not.

---

## 9. Bluetooth (BLE)

**Menu → Connectivity → Bluetooth**

Bluetooth is **off by default**. When enabled, MORPHEUS broadcasts each
completed word (text, current WPM, keying mode, timestamp) to whichever
paired device is currently connected — only **one active connection at
a time**, even though multiple devices can be remembered (see below).

### Pairing multiple devices

MORPHEUS remembers up to **3 paired devices** (e.g. your phone and a
laptop) and any of them may reconnect later — but still only one at a
time can actually be connected and receiving data.

1. Turn Bluetooth on (**Bluetooth → BLE toggle**).
2. Select **Pair Now** — the device becomes discoverable for 60 seconds
   (auto-closes if nothing connects).
3. On your phone/computer, connect and confirm the 6-digit passkey shown
   on the MORPHEUS display.
4. Once paired, that device is added to the trusted list and can
   reconnect on its own from then on (no passkey needed again).
5. Repeat with up to 2 more devices whenever you like — pairing a new
   one doesn't remove an existing one.

Once all 3 slots are used, a brand-new (never-paired) device cannot pair
until you free a slot — see Bond Reset below. Any of your 3 already-
trusted devices can still reconnect at any time, slots full or not.

**Menu → Diagnostics → BLE Status** (or Connectivity → Bluetooth →
Status) shows how many devices are currently paired, e.g. `Paired: 2/3`.

### Bond Reset

**Bluetooth → Bond Reset** forgets **all** paired devices at once (there
is no per-device "forget this one" option yet), letting new devices pair
from a clean slate. There is currently no physical button for this — it
must be done through the menu.

### Status LED

A separate toggle (**Bluetooth → Status-LED**) controls whether the status
LED shows BLE activity:
- Fast blink — pairing window open
- Three quick pulses, then off — a device just connected
- Off otherwise (a steady connection is not shown as an ongoing alarm)

The LED also gives a brief pulse on every real keydown, independent of the
BLE setting.

### Not yet available

The following appear in the Connectivity/Tools menus but are placeholders
("Feature not yet") in this firmware version:

- Wi-Fi
- Keyboard output
- Firmware Update (OTA)
- Battery monitoring
- One unallocated "Coming Soon" slot

---

## 10. Diagnostics

**Menu → Diagnostics** — nine read-only test/status screens for
troubleshooting:

- Input Test — live paddle/button state
- Display Test — screen render check
- Audio Test — tone/buzzer check
- BLE Status — connection, security, trusted-device state
- GPIO Monitor — raw pin states
- System Info / Hardware Info — board and firmware details
- Memory/Perf — free heap, loop rate
- NVS/Settings — persisted-settings status (defaulted? last save time?)

---

## 11. Help

**Menu → Help** provides on-device reference screens: Quick Start,
Controls, Morse Code Guide, Button Guide, Games Guide, and About.

---

## Appendix: Hardware Pinout

| Function | GPIO |
|---|---:|
| OLED SDA | 21 |
| OLED SCL | 22 |
| Key / DIT (Tip) | 25 |
| DAH (Ring) | 26 |
| Buzzer | 18 |
| Status LED | 27 |
| Rotary Encoder A | 19 |
| Rotary Encoder B | 23 |
| Encoder Push | 4 |
| Confirm Button | 14 |
| Back Button | 13 |

See `docs/wiring.md` and `docs/Hardware_Architecture.md` for full wiring
diagrams and design rationale.

---

*For a breakdown of what's implemented vs. still in progress, see
`docs/FUNCTIONALITY_STATUS.md`.*
