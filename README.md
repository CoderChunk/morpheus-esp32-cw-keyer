# MORPHEUS

> **Forge the Sound of Morse.**

An open-source ESP32 CW keyer with real-time Morse decoding, adaptive Koch/Farnsworth training, three device arcade games, an OLED operator interface, and secure Bluetooth Low Energy remote control and telemetry.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-ESP32-red)

Current firmware: **2.8.8** (see [CHANGELOG.md](CHANGELOG.md)). Operating instructions are in [docs/USER_MANUAL.md](docs/USER_MANUAL.md).
The companion app is [MORPHEUS UI](https://github.com/CoderChunk/morpheus_ui) (Flutter; Linux tested, other platforms implemented).

---

## Overview

MORPHEUS combines:

* Straight key and iambic paddle (Mode A/B) keying
* Real-time Morse decoding
* Eight training modes (Koch, Characters, Words, Callsigns, Adaptive, Exam, Listening, Combined) plus Farnsworth spacing
* Three device arcade games with persisted high scores
* Session and lifetime statistics
* Six named operating profiles (Default, Portable, Contest, Practice, Outdoor, Silent)
* A 5-slot memory keyer
* A rotary-encoder-driven OLED menu system
* Secure BLE: word telemetry plus a JSON control channel for remote keying, training, games and settings
* A modular, event-driven architecture

Every subsystem is isolated and independently expandable, making MORPHEUS suitable for experimentation, education and further development.

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

* Live Morse decoding with character and word recognition
* Timing-based classification and runtime enable/disable
* Measured key durations and inter-character silence reported over BLE

### Training

* Koch, Characters, Words, Callsigns, Adaptive and Exam modes on the device
* Listening and Combined modes driven from the companion app
* Farnsworth spacing practice
* Per-mode statistics and a 90%-to-pass Exam mode

### Games

* Copy Challenge (falling-character reaction game)
* Memory Challenge (Simon-style growing chain)
* Speed Challenge "Overdrive" (accelerating fixed-tempo beat)
* Persisted high scores, which survive Factory Reset

### Statistics & Profiles

* Session and lifetime statistics, boot-time WPM history
* Six named operating profiles bundling WPM/tone/paddle-reverse/mode/volume/contrast
* 5-slot memory keyer for canned CQ/exchange messages

### Wireless

* Secure BLE: bonding, MITM protection, LE Secure Connections with a passkey shown on the OLED
* Up to 3 remembered devices, one active connection at a time
* Bounded, auto-expiring pairing window (60 s); BLE is off by default
* Real-time word and live-pattern notifications (JSON)
* Remote control channel: virtual key, training and game control, keyer settings, device information. A held virtual key is released if the link drops.
* Preferred connection interval of 7.5–15 ms for responsive remote keying

### Operator Display

* OLED status display with live decoded text, WPM and operating mode
* BLE connection status and pairing information
* Status LED: key-down pulse, BLE pairing/connect feedback, training/playback flash

### Diagnostics

* Serial diagnostics (opt-in, off by default)
* Input, display, audio, BLE, GPIO and LED diagnostic screens
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
│   ├── TEST_SUITE.md
│   ├── test-reports/
│   ├── wiring.md
│   └── USER_MANUAL.md
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
│       ├── transport.{h,cpp}
│       └── ble_control.{h,cpp} # BLE remote-control command/event bridge
├── tests
│   ├── run_all.sh              # one entry point for every test level
│   ├── native/                 # host g++ tests linking the real firmware .cpp
│   ├── hardware/               # real-device system and integration tests
│   └── test_*.py               # bridge, protocol and timing-model tests
├── tools
│   ├── ble_client/             # protocol specs; Python BLE bridge (debug route) and Qt reference client
│   ├── oled_render/            # host OLED simulator (no hardware)
│   └── oled_screencap/         # live OLED screenshot over BLE
├── LICENSE
├── CHANGELOG.md
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

Release builds are published as GitHub releases: a full 4 MB flash image (write at `0x0`)
and an app-only binary.

## Testing

```sh
tests/run_all.sh --host --build                 # host tests, ESP32 build, size budget, warning baseline
tests/run_all.sh --device AA:BB:CC:DD:EE:FF     # real-device system tests (never flashes)
```

The suite covers functional, non-functional (fuzzing, hardened builds, benchmarks), regression,
integration, system and negative tests. See [docs/TEST_SUITE.md](docs/TEST_SUITE.md) and the
reports in [docs/test-reports/](docs/test-reports/). Native tests need only a host C++17 compiler.

See `docs/build.md` for more detail.

---

# Required Libraries

* NimBLE-Arduino (2.x)
* U8g2
* Preferences (ESP32 Core)

Tested with ESP32 Arduino core 3.3.x, NimBLE-Arduino 2.5.1 and U8g2 2.36.19.

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

Only trusted devices may reconnect after pairing. BLE is off by default and must be explicitly enabled; pairing mode advertises only during a bounded, auto-expiring window.

The GATT service and the control-channel command and event formats are specified in
[`tools/ble_client/WS_PROTOCOL.md`](tools/ble_client/WS_PROTOCOL.md) and
[`tools/ble_client/MOBILE_BLE_PROTOCOL.md`](tools/ble_client/MOBILE_BLE_PROTOCOL.md).

---

# Future Development

The following are present in the menu today as explicit "Feature not yet" placeholders:

* Wi-Fi
* Keyboard (HID) output
* Firmware Update (OTA)
* Battery monitoring

Contributors are also encouraged to explore:

* OTA firmware updates
* Test coverage for the games, stats, profiles and UI state machine (the keyer, decoder, trainer and BLE control protocol are covered)
* Web dashboards, contest logging, network gateways, SDR integrations

Known issue: about 5–10 % of reconnects to an already-bonded device need a retry; see the latest
test report.

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
