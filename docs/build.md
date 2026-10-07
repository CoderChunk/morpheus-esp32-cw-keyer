# Build and Test

## Arduino IDE build

1. Install Arduino IDE 2.x or newer.
2. Install ESP32 board support through Boards Manager.
3. Install the required libraries:
   - NimBLE-Arduino
   - U8g2
   - Preferences from the ESP32 Arduino core
4. Open `firmware/MORPHEUS/MORPHEUS.ino`.
5. Select an ESP32 board profile appropriate for the target hardware.
6. Compile the sketch.
7. Connect the ESP32 over USB and upload.

## Feature configuration

Firmware feature switches and hardware constants live in
`firmware/MORPHEUS/config.h`. The most common toggles are:

```cpp
#define FEATURE_OLED      1
#define FEATURE_BLE       1
#define FEATURE_SIDETONE  1
#define FEATURE_SERIAL    0
```

Disable features by setting the corresponding value to `0` before compiling.
For example, setting `FEATURE_BLE` to `0` removes NimBLE-dependent code paths
and leaves transport functions as no-op stubs.

## Tests

The complete suite (functional, non-functional, regression, integration, system and
negative tests) is described in [TEST_SUITE.md](TEST_SUITE.md) and run through one entry
point:

```sh
tests/run_all.sh --host                         # host tests, no hardware
tests/run_all.sh --host --build                 # ... plus the ESP32 build, size budget and warning baseline
tests/run_all.sh --device AA:BB:CC:DD:EE:FF     # real-device system tests (never flashes)
```

The pieces can also be run directly:

```sh
python3 -m unittest discover -s tests           # Python bridge, protocol and timing-model tests
tests/native/run.sh                             # host g++ tests linking the real firmware .cpp files
tests/native/run_sanitized.sh                   # hardened native build, fuzzing and benchmark
```

`tests/native/run.sh` compiles and links the actual firmware sources (an `Arduino.h`
stub in `tests/native/arduino_stub/`, no ESP32 toolchain) and exercises them directly:
decoder, trainer, keyer, games input, the keyer-settings and game-input protocols,
keyer metrics and the virtual-key guard. It needs only a host C++17 compiler (`g++`).

Host tests do not replace an ESP32 compile or hardware validation for GPIO, LEDC, OLED,
NimBLE, NVS or pairing behavior.

## Suggested validation before a pull request

Run `tests/run_all.sh --host --build`, which also compiles the firmware. For BLE, display,
sidetone, settings persistence or wiring changes, also validate on hardware because those
paths depend on ESP32 peripherals and attached devices.
