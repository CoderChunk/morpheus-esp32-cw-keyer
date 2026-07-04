# MORPHEUS

> **Forge the Sound of Morse.**

An open-source ESP32 CW keyer featuring real-time Morse decoding, secure Bluetooth telemetry, OLED visualization, and a fully modular architecture designed for experimentation, learning, and modern amateur radio applications.

---

## Overview

MORPHEUS is more than a Morse keyer.

It is a development platform for modern CW technology.

The firmware combines:

* Straight key operation
* Iambic paddle support (Mode B)
* Real-time Morse decoding
* OLED operator interface
* Secure Bluetooth Low Energy telemetry
* Modular event-driven architecture

Every subsystem is isolated and independently expandable, making MORPHEUS suitable for experimentation, education, and future enhancements.

---

## Features

### CW Keying

* Straight key operation
* Iambic paddle support
* Iambic Mode B memory
* Accurate timing engine
* Configurable WPM
* Sidetone generation

### Real-Time Decoding

* Live Morse decoding
* Character recognition
* Word detection
* Timing-based classification
* Morse pattern visualization

### Wireless Telemetry

* Secure BLE communication
* Encrypted pairing
* Passkey authentication
* Bonded device support
* Real-time word transmission

### Operator Display

* OLED status display
* Live decoded text
* Current WPM
* Operating mode indication
* BLE connection status
* Pairing information

### Diagnostics

* Serial diagnostics
* Event logging
* Status monitoring
* Runtime statistics
* Heap information

---

# Architecture

MORPHEUS follows a modular architecture.

```
             +----------------+
             |  MORPHEUS.ino  |
             +--------+-------+
                      |
      +---------------+---------------+
      |               |               |
+-----v-----+   +-----v-----+   +-----v-----+
| Keyer     |   | Decoder   |   | Services  |
+-----+-----+   +-----+-----+   +-----------+
      |               |
      |               |
+-----v-----+   +-----v-----+
| Display   |   | Transport |
+-----------+   +-----------+
```

Each module has a clearly defined responsibility.

This separation allows contributors to improve individual systems without affecting the entire firmware.

---

# Hardware

### Supported Platform

* ESP32

### OLED

* SH1106 OLED
* I2C Interface

### Inputs

* Straight key
* Iambic paddle

### Output

* Piezo buzzer sidetone
* BLE telemetry
* OLED display

---

# Pin Assignment

| Function             | GPIO |
| -------------------- | ---: |
| OLED SDA             |   21 |
| OLED SCL             |   22 |
| Key / DIT            |   25 |
| DAH                  |   26 |
| Buzzer               |   18 |
| Mode Switch          |   33 |
| Bond Reset Button    |   27 |
| Reserved Keypad ADC  |   34 |
| Function          | GPIO |
| ----------------- | ---: |
| OLED SDA          |   21 |
| OLED SCL          |   22 |
| Key / DIT         |   25 |
| DAH               |   26 |
| Buzzer            |   18 |
| Mode Switch       |   33 |
| Reserved keypad ADC |   34 |

---

# Project Structure

```
morpheus-esp32-cw-keyer/
├── docs
│   ├── architecture.md
│   ├── build.md
│   └── wiring.md
├── firmware
│   └── MORPHEUS
│       ├── config.h
│       ├── core_decoder.cpp
│       ├── core_decoder.h
│       ├── core_keyer.cpp
│       ├── core_keyer.h
│       ├── display.cpp
│       ├── display.h
│       ├── MORPHEUS.ino
│       ├── services.cpp
│       ├── services.h
│       ├── transport.cpp
│       └── transport.h
├── tests
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

Host-side decoder tests can be run with:

```sh
python -m unittest discover -s tests
```

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
#define FEATURE_OLED      1
#define FEATURE_BLE       1
#define FEATURE_SERIAL    1
```

---

# Bluetooth Security

MORPHEUS uses:

* Secure pairing
* MITM protection
* Bonded devices
* Encrypted communication

Only trusted devices may reconnect after pairing.

---

# Future Development

Contributors are encouraged to explore:

* Koch training
* Farnsworth spacing
* Adaptive timing
* CW training modes
* Mobile applications
* Web dashboards
* Contest logging
* Network gateways
* Statistics and analytics
* SDR integrations

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
