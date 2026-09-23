# MORPHEUS

> **Forge the Sound of Morse.**

An open-source ESP32 CW keyer featuring real-time Morse decoding, Koch/Farnsworth/adaptive training, three CW arcade games, an OLED operator interface, and secure Bluetooth Low Energy telemetry.

Current firmware version: **v2.3.0**. See `docs/USER_MANUAL.md` for full operating instructions and `docs/FUNCTIONALITY_STATUS.md` for a code-grounded audit of what's implemented vs. still stubbed.

---

## Overview

MORPHEUS is more than a Morse keyer.

It is a development platform for modern CW technology.

The firmware combines:

* Straight key and iambic paddle (Mode A/B) keying
* Real-time Morse decoding
* Six training modes (Koch, Characters, Words, Callsigns, Adaptive, Exam) plus Farnsworth spacing
* Three CW arcade games with persisted high scores
* Session and lifetime statistics
* Six named operating profiles (Default, Portable, Contest, Practice, Outdoor, Silent)
* A 5-slot memory keyer
* A rotary-encoder-driven OLED menu system
* Secure Bluetooth Low Energy telemetry
* Modular event-driven architecture

Every subsystem is isolated and independently expandable, making MORPHEUS suitable for experimentation, education, and future enhancements.

---

## Features

### CW Keying

* Straight key operation
* Iambic paddle support, Mode A and Mode B
* Configurable WPM (5–40)
* Adjustable keying weighting (30–70%)
* Paddle reverse
* Sidetone generation (200–2000 Hz, adjustable volume)

### Real-Time Decoding

* Live Morse decoding
* Character and word recognition
* Timing-based classification
* Runtime enable/disable

### Training

* Koch Method, Characters, Words, Callsigns, Adaptive, and Exam modes
* Farnsworth spacing practice
* Per-mode statistics and a 90%-to-pass Exam mode

### Games

* Copy Challenge (falling-character reaction game)
* Memory Challenge (Simon-style growing chain)
* Speed Challenge "Overdrive" (accelerating fixed-tempo beat)
* Persisted high scores, survive Factory Reset

### Statistics & Profiles

* Session and lifetime statistics, boot-time WPM history
* Six named operating profiles bundling WPM/tone/paddle-reverse/mode/volume/contrast
* 5-slot memory keyer for canned CQ/exchange messages

### Wireless Telemetry

* Secure BLE communication (bonding, MITM protection, LE Secure Connections)
* Passkey authentication, multi-device trusted allowlist (up to 3 remembered devices, one active connection at a time)
* Bounded, auto-expiring pairing window
* Real-time word transmission (JSON payload per completed word)

### Operator Display

* OLED status display with live decoded text
* Current WPM and operating mode
* BLE connection status and pairing information
* Status LED: keydown pulse, BLE pairing/connect feedback, and training/playback flash

### Diagnostics

* Serial diagnostics (opt-in, off by default)
* Input, display, audio, BLE, GPIO, and LED diagnostic screens
* Runtime statistics and heap information

---

# Architecture

MORPHEUS follows a modular, event-driven architecture. See `docs/architecture.md` for the full breakdown.

```
             +----------------+
             |  MORPHEUS.ino  |
             +--------+-------+
                      |
   +----------+----------+----------+----------+----------+
   |          |          |          |          |          |
+--v--+   +---v---+  +---v---+  +---v---+  +---v---+  +---v---+
|Keyer|   |Decoder|  |Trainer|  | Games |  | Stats |  |Profiles|
+--+--+   +---+---+  +-------+  +-------+  +-------+  +-------+
   |          |
+--v--+   +---v-----+   +----------+   +---------+
|Clock|   |Transport|   | Services |   |Status LED|
+-----+   +---------+   +----------+   +---------+
                      |
                 +----v----+
                 |    UI   |
                 | (menu / |
                 | screens)|
                 +---------+
```

Each module has a clearly defined responsibility. This separation allows contributors to improve individual systems without affecting the entire firmware.

---

# Hardware

### Supported Platform

* ESP32

### OLED

* SH1106 OLED, I2C interface

### Inputs

* Straight key or iambic paddle
* Rotary encoder (navigation)
* Confirm and Back buttons

### Output

* Piezo buzzer sidetone
* Status LED
* BLE telemetry
* OLED display

---

# Pin Assignment

| Function          | GPIO |
| ----------------- | ---: |
| OLED SDA           |   21 |
| OLED SCL           |   22 |
| Key / DIT          |   25 |
| DAH                |   26 |
| Buzzer             |   18 |
| Status LED         |   27 |
| Rotary Encoder A   |   19 |
| Rotary Encoder B   |   23 |
| Encoder Push       |    4 |
| Confirm Button     |   14 |
| Back Button        |   13 |

See `docs/wiring.md` for the full wiring diagram.

---

# Project Structure

```
morpheus-esp32-cw-keyer/
├── docs
│   ├── architecture.md
│   ├── build.md
│   ├── Hardware_Architecture.md
│   ├── wiring.md
│   ├── USER_MANUAL.md
│   └── FUNCTIONALITY_STATUS.md
├── firmware
│   └── MORPHEUS
│       ├── MORPHEUS.ino
│       ├── config.h
│       ├── core_*.{h,cpp}      # keyer, decoder, trainer, games, stats,
│       │                       # profiles, memory, morseplayer, clock, led
│       ├── ui_*.{h,cpp}        # menu, screens, state, renderer, input,
│       │                       # backend, icons, fonts, splash
│       ├── display.{h,cpp}
│       ├── services.{h,cpp}
│       └── transport.{h,cpp}
├── tests
│   ├── test_ble_json_budget.py
│   └── test_decoder_logic.py
├── LICENSE
└── README.md
```

---

# Building

## Arduino IDE

1. Install ESP32 board support.
2. Install required libraries.
3. Open `firmware/MORPHEUS/MORPHEUS.ino`.
4. Select your ESP32 board.
5. Compile and upload.

## arduino-cli

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/MORPHEUS
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/MORPHEUS
```

Host-side decoder timing-model tests can be run with:

```sh
python -m unittest discover -s tests
```

A native test that compiles and exercises the actual `core_decoder.cpp`
(not a reimplementation) can be run with:

```sh
tests/native/run.sh
```

Requires only a host C++17 compiler (`g++`) - no other dependencies.

See `docs/build.md` for more detail.

---

# Required Libraries

* NimBLE-Arduino
* U8g2
* Preferences (ESP32 Core)

---

# Configuration

Most project settings are located in:

```cpp
config.h
```

Features can be enabled or disabled:

```cpp
#define FEATURE_OLED                  1
#define FEATURE_BLE                   1
#define FEATURE_SIDETONE              1
#define FEATURE_SERIAL                0   // opt-in diagnostic logging
#define FEATURE_DEBUG_SERIAL_COMMANDS 0   // opt-in serial debug commands
```

---

# Bluetooth Security

MORPHEUS uses:

* Secure pairing
* MITM protection
* Bonded devices
* Encrypted communication

Only trusted devices may reconnect after pairing. BLE is off by default and must be explicitly enabled; even when enabled, advertising only runs during a bounded, auto-expiring pairing window.

---

# Future Development

The following are present in the menu today as explicit "Feature not yet" placeholders:

* Wi-Fi
* Keyboard (HID) output
* Firmware Update (OTA)
* Battery monitoring

Contributors are also encouraged to explore:

* OTA firmware updates
* A hardware bond-reset trigger
* Test coverage for the keyer, trainer, games, stats, profiles, and UI state machine
* Mobile applications, web dashboards, contest logging, network gateways, SDR integrations

See `docs/FUNCTIONALITY_STATUS.md` for the full, current gap analysis.

---

# Contributing

Contributions are welcome.

Whether you are:

* A CW operator
* An embedded developer
* A radio amateur
* A UI designer
* A BLE developer
* A student learning Morse

Your ideas and pull requests are encouraged.

Please:

1. Fork the repository.
2. Create a feature branch.
3. Submit a pull request.
4. Share your ideas.

---

# License

This project is licensed under the GNU General Public License Version 3.

You may:

* Use
* Modify
* Distribute
* Improve

Provided that derivative works remain open under the GPLv3.

---

# Author

**Coder Chunk**

---

# Project Motto

> "Forge the Sound of Morse."

MORPHEUS exists to preserve the art of CW while exploring what modern embedded systems can contribute to the future of Morse communication.
