# MORPHEUS BLE Client — UI Functional Specification

**Audience:** an external UI design tool/agent producing a new visual
design for this application. This document intentionally contains
**no visual, layout, color, or component guidance**. It lists only the
functional requirements: what data exists, what a user can input or
trigger, what states things can be in, and where each piece of data
comes from. All design decisions (layout, hierarchy, color, iconography,
motion, component choice) are left entirely to the designer.

**What this app is:** a desktop companion app for the MORPHEUS ESP32 CW
(Morse code) keyer. It connects to the physical device over Bluetooth
Low Energy (BLE) to show live keying data and remotely control Training
drills, plus a fully local set of typing/arcade games that don't touch
the device at all.

**Status tags** used throughout — every field/action below is tagged
with where it actually comes from:
- **LIVE** — real-time data/commands over the BLE connection to the
  physical device, working today.
- **CLIENT** — computed or stored entirely on the computer running this
  app (no device involved), working today.
- **DEVICE-ONLY** — this data/control exists on the physical device's
  own screen and menu today, but is **not yet reachable** over BLE.
  Included here so the designer can plan a place for it, but it must
  be presented as unavailable/disabled/not-yet-implemented, not as a
  working live control.

---

## 1. Global / Application Shell

### 1.1 Connection status (LIVE)
Always-visible, regardless of which section the user is viewing.

| Field | Type | Values / Range | Notes |
|---|---|---|---|
| Connection state | enum | `Scanning`, `Connecting`, `Connected`, `Disconnected`, `Not found`, `Error` | |
| Connected device name | string | e.g. `MORPHEUS-CW` | only meaningful when Connected |
| Connected device address | string | BLE MAC/UUID address | only meaningful when Connected |
| Error/status message | string | free text | e.g. "MORPHEUS-CW not found in 10s" |

### 1.2 Connection actions (LIVE)
| Action | Inputs | Notes |
|---|---|---|
| Connect | optional: target device address (string, blank = auto-discover by name) | starts a scan-then-connect sequence, ~10s scan timeout |
| Disconnect | none | |
| Pair new device | none (Linux only) | opens the in-app pairing flow (§1.3). On non-Linux, or if the required library isn't installed, this action is unavailable — that must be communicated to the user, not hidden silently. |

### 1.3 In-app pairing flow (LIVE, Linux only)
A step-by-step guided flow, not a single screen:

| Step | Field/Input | Notes |
|---|---|---|
| 1 | Start pairing (action) | begins device discovery for pairing |
| 2 | Status message | e.g. "Looking for MORPHEUS-CW..." |
| 3a | Passkey entry field | numeric, 6 digits — user reads this off the physical device's screen and types it in |
| 3b | *(alternative to 3a)* Confirmation choice | Yes / No — "Does your MORPHEUS display show: `123456`?" — device may ask for typed entry OR yes/no confirmation depending on pairing mode, only one applies per session |
| 4 | Result message | success or failure, with reason if failed |

On non-Linux platforms or when the pairing library is unavailable, this
entire flow is unavailable and must be presented as such (with a reason),
falling back to instructions to pair via the OS's own Bluetooth settings.

### 1.4 Navigation
The application has these sections. Whether each is fully working,
partially working, or not yet implemented is stated in its own section
below — this is not a visual menu spec, just the list of destinations:

1. CW Keyer
2. Training
3. Games
4. Statistics
5. Connectivity
6. Profiles
7. Settings
8. Diagnostics
9. Tools
10. Help

### 1.5 Settings entry point
One global action (e.g. a gear/settings affordance available from
anywhere) that navigates to the Settings section (§8).

---

## 2. CW Keyer (LIVE)

Real-time display of words the operator keys on the physical device,
plus a virtual key the operator can use from the computer instead.

### 2.1 Live telemetry fields
Pushed from the device every time a keyed word completes:

| Field | Type | Values / Range | Notes |
|---|---|---|---|
| Word | string | decoded text, A–Z / 0–9 / `. , ? / = + -` | the just-completed word |
| WPM | integer | 5–40 | keying speed for that word |
| Mode | enum | `STRAIGHT`, `PADDLE` | which keying mode produced it |
| Timestamp | integer | device uptime in ms | not wall-clock time (device has no RTC) |

### 2.2 Derived/session fields (CLIENT, computed from the above)
| Field | Type | Notes |
|---|---|---|
| Words received (session count) | integer | count of words received since app launch or last clear |
| Last word | string | most recent word's text |
| Running transcript | string (append-only log) | every received word, in order |
| Per-word history | list of {time received, word, WPM, mode} | one entry per received word |

### 2.3 User actions
| Action | Inputs | Effect |
|---|---|---|
| Clear | none | clears transcript and/or history and resets the session word count (CLIENT-only reset; does not affect anything on the device) |
| Adjust displayed text size | choice from a small fixed set of sizes | CLIENT-only, cosmetic, no functional effect |

### 2.4 Virtual straight key (LIVE)
| Action | Input | Effect |
|---|---|---|
| Key down | press-and-hold (mouse or a bindable key) | sends a "key down" event to the device — classified as dit/dah by hold duration, exactly like the real physical key. Has no effect while the real physical key is already down. |
| Key up | release | sends the matching "key up" event |

### 2.5 Not yet available over BLE (DEVICE-ONLY)
These exist as real settings on the device (adjustable via its own
menu) but cannot currently be read or changed remotely:

| Field | Type | Range / Values | Device default |
|---|---|---|---|
| Keying speed (WPM) | integer | 5–40 | 18 |
| Keying mode | enum | Straight / Paddle | — |
| Paddle reverse | boolean | on/off | off |
| Iambic mode | enum | A / B | B |
| Weighting | integer % | 30–70 | 50 |
| Sidetone frequency | integer Hz | 200–2000 | 600 |
| Sidetone on/off | boolean | — | on |
| Volume | integer % | 0–100 | 80 |

---

## 3. Training (LIVE)

Remote control of structured Morse-copying drills running on the
device. All six modes share one live-state feed.

### 3.1 Mode selection
| Field | Type | Values |
|---|---|---|
| Selected mode | enum, one required to start | `KOCH`, `CHARACTERS`, `WORDS`, `CALLSIGNS`, `ADAPTIVE`, `EXAM` |

Mode meanings (for descriptive copy, not for behavior — behavior is
entirely on-device):
- **KOCH** — learns characters progressively in a fixed order; a new
  character unlocks once the current set is copied accurately enough.
- **CHARACTERS** — drills a fixed device-configured character set.
- **WORDS** — copies real words from a fixed vocabulary.
- **CALLSIGNS** — copies randomly generated practice callsigns.
- **ADAPTIVE** — speed rises after 3 correct in a row, falls after a miss.
- **EXAM** — a fixed 25-character graded test, 90% required to pass.

### 3.2 Session control actions
| Action | Inputs | Notes |
|---|---|---|
| Start | selected mode (enum, §3.1) | begins a session in that mode |
| Stop | none | ends the session early |
| Confirm | none | advances past a result/prompt screen (e.g. dismissing an exam result) |
| Key down / Key up | same virtual key as §2.4 | this is how the operator answers each drill round from the computer |

### 3.3 Live session-state fields
Pushed continuously while a session is active:

| Field | Type | Values / Range | Applies to |
|---|---|---|---|
| Active | boolean | | all modes |
| Mode | enum | same as §3.1 | all modes |
| Phase | enum | e.g. `LISTENING`, `EXAM_DONE`, others per mode | all modes |
| Target | string | the current character/word to copy | all modes |
| Correct | integer | running count | all modes |
| Attempts | integer | running count | all modes |
| Koch level | integer | 2–40 | KOCH mode |
| Adaptive WPM | integer | current auto-adjusted speed | ADAPTIVE mode |
| Exam score percent | integer 0–100 | | EXAM mode, once finished |
| Exam passed | boolean | | EXAM mode, once finished |
| Exam correct count | integer | | EXAM mode, once finished |
| Exam total count | integer | fixed at 25 | EXAM mode, once finished |

### 3.4 Derived fields (CLIENT, computed from §3.3)
| Field | Type | Notes |
|---|---|---|
| Incorrect count | integer | Attempts − Correct |
| Accuracy percent | integer 0–100 | Correct ÷ Attempts × 100, 0 if no attempts yet |

### 3.5 Reference data (CLIENT, static — not fabricated, exact copies of the device's own tables)
| Data | Content |
|---|---|
| Koch character order | the fixed 40-character unlock sequence, in order |
| Morse code table | the dot/dash pattern for every character the device can send/decode (A–Z, 0–9, `. , ? / = + -`) |

These exist so the UI can show, e.g., which characters are unlocked at
the current Koch level, or the actual dot/dash pattern for the current
target character — using real reference tables, not live device output.

### 3.6 Not yet available over BLE (DEVICE-ONLY)
| Field | Notes |
|---|---|
| Set training speed remotely | speed is a device-side setting today |
| Select a specific "lesson" beyond the mode itself | no such device-side concept exists beyond Koch level |
| Switch an audio-only "Listen" vs. typed "Type" answer input | the device always expects a keyed answer; there is no typed-letter answer mode |
| Reset training statistics | no reset command exists; these numbers are lifetime device state (§5) |
| Farnsworth spacing drill | a distinct device-side drill (fixed phrase "PARIS PARIS CQ DE TEST" at full character speed with stretched letter/word spacing); not exposed over BLE |

---

## 4. Games (CLIENT)

Six standalone typing/arcade games. **None of these use the BLE
connection or any device data** — they run entirely on the computer,
using the same Morse-code table as Training (§3.5) to check answers.
(The device itself separately has its own 3 games — Copy Challenge,
Memory Challenge, Speed Challenge — playable only from its own screen;
those are unrelated to this section and not reachable from this app.)

### 4.1 Game catalog
| Field | Type | Notes |
|---|---|---|
| Game identity | one of 6 fixed entries | see §4.2 |
| Title | string | |
| Description | string | one sentence, what the game asks the player to do |
| Tags/skills | list of short strings | e.g. "Speed", "Accuracy" — descriptive only |
| Best score | integer | persisted locally per game, across app restarts |

### 4.2 The six games and their distinct mechanics
| Game | Mechanic | What the player does |
|---|---|---|
| Space War | targets fall/approach | type the Morse pattern for the shown character before it lands |
| Fruit Ninja (Morse) | targets fall | same mechanic as above, different theme |
| Meteor Catch | targets fall, only one is "wanted" at a time | solve only the current wanted character; solving a wrong one costs a life |
| Signal Rescue | a Morse pattern (not a letter) falls | press the letter key that pattern decodes to |
| Morse Invaders | targets fall/approach | same mechanic as Space War, different theme |
| Word Rush | a whole word falls | type the full Morse pattern for the entire word (letters separated by a space) |

### 4.3 Per-session configuration (CLIENT, real — actually changes gameplay)
| Field | Type | Values |
|---|---|---|
| Difficulty | enum | `Easy`, `Medium`, `Hard` — affects spawn rate and fall speed |
| Game speed | integer (WPM-like scale) | `10, 15, 20, 25, 30` — affects fall speed |
| Lives | integer | `1, 3, 5` |
| Duration | integer seconds | `60, 120, 180` |

### 4.4 Live in-game state
| Field | Type | Notes |
|---|---|---|
| Score | integer | increases on each correct solve; combo-scaled (consecutive correct solves increase points per solve, up to a cap) |
| Lives remaining | integer | decreases on a miss or (in Meteor Catch) solving a wrong target |
| Time remaining | integer seconds, counting down | game ends at 0 |
| Active target(s) | one or more of: character, its Morse pattern, or a word + its Morse pattern (depends on game, §4.2) | may be more than one on screen at once (e.g. Meteor Catch) |
| Current input buffer | string of `.`/`-` being typed, or nothing (decode-mode games use direct letter keypresses instead) | cleared on submit or backspace |
| Combo counter | integer | consecutive correct solves without a miss; resets to 0 on any miss |

### 4.5 Input
| Input | Meaning |
|---|---|
| `.` key | append a dot to the current guess |
| `-` key | append a dash to the current guess |
| Backspace | remove the last symbol from the current guess |
| A guess is automatically checked once it reaches the target pattern's length | correct → target cleared, score increases; incorrect → counts as a miss, guess clears |
| A–Z letter keys (Signal Rescue only) | directly answers the current falling Morse pattern; correct/incorrect checked immediately, no build-up buffer |

### 4.6 End-of-game result
| Field | Type | Notes |
|---|---|---|
| Final score | integer | |
| Outcome | enum | `Cleared` (time ran out with lives remaining) or `Out of lives` |
| New best score reached | boolean | if final score exceeds the previously stored best (§4.1) |

---

## 5. Statistics (DEVICE-ONLY — not yet exposed over BLE)

All fields below exist and are tracked on the physical device today,
but there is currently no BLE command to read them remotely. Include
in the design as a fully specified but "not yet connected" section.

### 5.1 Session (resets every device power-on)
| Field | Type |
|---|---|
| Characters keyed | integer |
| Words keyed | integer |
| Elements (dits/dahs) keyed | integer |
| Uptime | duration |

### 5.2 Lifetime (persisted on the device, survives reboot and factory reset)
| Field | Type |
|---|---|
| Total characters keyed | integer |
| Total words keyed | integer |
| Total elements keyed | integer |
| Total uptime | duration |
| Training attempts per mode | integer, one per mode from §3.1 |
| Training correct per mode | integer, one per mode from §3.1 |
| Exams taken | integer |
| Exams passed | integer |
| Best exam score | integer percent |
| Peak adaptive-training WPM | integer |

### 5.3 Progress / Accuracy
| Field | Type |
|---|---|
| Per-mode training accuracy | percent, one per mode from §3.1 |

### 5.4 Speed history
| Field | Type | Notes |
|---|---|---|
| WPM at start of session, last 6 sessions | list of 6 integers | boot-time snapshots, not a continuous live trend (device has no real-time clock) |

---

## 6. Connectivity (DEVICE-ONLY for management; connection itself is LIVE via §1)

### 6.1 Bluetooth toggle (DEVICE-ONLY)
| Field | Type |
|---|---|
| BLE enabled | boolean, device-side, off by default |

### 6.2 Trusted device management (DEVICE-ONLY — no BLE command exists to list/forget individual devices)
| Field | Type | Notes |
|---|---|---|
| Trusted device count | integer, 0–3 | device remembers up to 3 |
| Trusted device list | up to 3 entries | no per-device identifying info or per-device "forget" exposed yet — only a single "forget all" action |
| Forget all (Bond Reset) | action | clears all trusted devices at once |

### 6.3 Status LED behavior toggle (DEVICE-ONLY)
| Field | Type | Notes |
|---|---|---|
| BLE-activity LED indication | boolean | separate from the LED's unconditional keydown pulse, which cannot be turned off |

### 6.4 Known-unimplemented placeholders (DEVICE-ONLY, explicitly not working on the device itself yet)
Wi-Fi, Keyboard output, Firmware Update (OTA), Battery monitoring, one
unallocated slot. These should be represented as clearly non-functional
if included at all.

---

## 7. Profiles (DEVICE-ONLY — not yet exposed over BLE)

Six fixed preset slots, each bundling these fields as one saved unit:

| Field | Type | Range |
|---|---|---|
| WPM | integer | 5–40 |
| Tone frequency | integer Hz | 200–2000 |
| Keying mode | enum | Straight / Paddle |
| Volume | integer % | 0–100 |
| Sidetone on/off | boolean | |
| Display contrast | integer | 10–255 |

| Action | Notes |
|---|---|
| Load a profile | makes it the active configuration |
| Save a profile | overwrites that slot with the current live configuration |

The six named default slots and their factory values:

| Profile | WPM | Tone | Mode | Volume | Notes |
|---|---|---|---|---|---|
| Default | 18 | 600 Hz | Straight | 80% | |
| Portable | 15 | 700 Hz | Paddle | 50% | |
| Contest | 25 | 800 Hz | Paddle | 90% | |
| Practice | 12 | 600 Hz | Straight | 70% | |
| Outdoor | 18 | 800 Hz | Paddle | Max | max display contrast |
| Silent | 15 | 600 Hz | Straight | Min | sidetone off, low contrast |

---

## 8. Settings (DEVICE-ONLY — not yet exposed over BLE)

### 8.1 Keyer
See §2.5 (Keying speed, Keying mode, Paddle reverse, Iambic mode, Weighting).

### 8.2 Audio
See §2.5 (Sidetone frequency, Sidetone on/off, Volume).

### 8.3 Display
| Field | Type | Range | Default |
|---|---|---|---|
| Contrast | integer | 10–255 | 128 |
| Invert | boolean | | off |
| Screen timeout | enum | `15s, 30s, 60s, 120s, 5min, 10min, 15min, 30min, Never` | Never |

### 8.4 System
| Field | Type | Range/Values | Notes |
|---|---|---|---|
| Callsign | string | up to 12 characters | shown/hidden via a separate toggle |
| Callsign visibility | boolean | show/hide | |
| Date | date value | | device has no RTC — resets to "unset" every reboot |
| Time | time value | | same caveat as Date |
| Date format | enum | `YYYY-MM-DD, DD-MM-YYYY, MM-DD-YYYY` | this preference persists even though the value doesn't |
| Time format | enum | `24-hour, 12-hour` | persists |
| Firmware version / build info | string, read-only | | |

### 8.5 System actions
| Action | Effect |
|---|---|
| Restart | soft reboot of the device |
| Factory Reset | resets keyer, decoder, Koch level, audio, display, callsign, date/time format, and BLE-enabled settings to defaults. Does **not** erase Profiles, lifetime Statistics, or Game high scores. |

---

## 9. Diagnostics (DEVICE-ONLY — not yet exposed over BLE)

Nine read-only status/test screens:

| Screen | Content |
|---|---|
| Input Test | live paddle/button contact state |
| Display Test | screen render check |
| Audio Test | tone/buzzer check |
| BLE Status | connection state, security state, trusted-device count/list |
| GPIO Monitor | raw pin states |
| System Info / Hardware Info | board and firmware identification |
| Memory/Perf | free heap memory, loop rate |
| NVS/Settings | persisted-settings status (defaulted vs. customized, last save time) |

---

## 10. Tools (DEVICE-ONLY — currently empty on the device itself)

No fields or actions defined yet on the device side either. Present as
an intentionally empty/placeholder section.

---

## 11. Help (CLIENT, static reference content)

Static informational content, no live data:

| Item | Type |
|---|---|
| App name/version | string |
| Summary of what's live vs. not yet connected | text, mirrors the status tags used throughout this document |
| Link/reference to fuller documentation | text |

(The device itself separately has its own on-device Help screens —
Quick Start, Controls, Morse Code Guide, Button Guide, Games Guide,
About — which are not reachable from this app.)

---

## Appendix A: Enumerations used above

- **Keying mode**: `STRAIGHT`, `PADDLE`
- **Training mode**: `KOCH`, `CHARACTERS`, `WORDS`, `CALLSIGNS`, `ADAPTIVE`, `EXAM`
- **Connection state**: `Scanning`, `Connecting`, `Connected`, `Disconnected`, `Not found`, `Error`
- **Games difficulty**: `Easy`, `Medium`, `Hard`
- **Games speed scale**: `10, 15, 20, 25, 30` (WPM-equivalent)
- **Games lives options**: `1, 3, 5`
- **Games duration options (seconds)**: `60, 120, 180`

## Appendix B: Reference tables (exact, not approximate)

**Koch character order** (40 characters, unlock sequence):
`K M U R E S N A P T L W I . J Z = F O Y , V G Q 5 / H 3 8 B ? 4 7 C 1 D 6 X 9 2`

**Morse code table** (character → pattern):
A-Z and 0-9 use the standard international Morse alphabet; punctuation
supported: `. (period) , (comma) ? (question mark) / (slash) = (equals) + (plus) - (hyphen)`.
