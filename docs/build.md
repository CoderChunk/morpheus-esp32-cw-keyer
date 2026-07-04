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
#define FEATURE_SERIAL    1
```

Disable features by setting the corresponding value to `0` before compiling.
For example, setting `FEATURE_BLE` to `0` removes NimBLE-dependent code paths
and leaves transport functions as no-op stubs.

## Host-side tests

The repository includes a small Python `unittest` suite for decoder timing
behavior that can run without ESP32 hardware:

```sh
python -m unittest discover -s tests
```

The host tests model element accumulation, character-gap finalization,
word-gap finalization, TX-active gating, and pattern-length bounds. They do not
replace an Arduino/ESP32 compile or hardware validation for GPIO, LEDC, OLED,
NimBLE, NVS, or pairing behavior.

## Suggested validation before a pull request

Run the host tests and perform at least one firmware compile with the feature
set you changed. For BLE, display, sidetone, settings persistence, or wiring
changes, also validate on hardware because those paths depend on ESP32
peripherals and attached devices.
