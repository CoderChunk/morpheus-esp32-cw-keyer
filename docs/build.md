# Build and Test

## Firmware build

Open `firmware/MORPHEUS/MORPHEUS.ino` in Arduino IDE with ESP32 board support installed, then install the required libraries listed in the README before compiling and uploading.

## Host-side tests

A small Python test suite covers decoder timing behavior that can be exercised without ESP32 hardware:

```sh
python -m unittest discover -s tests
```

These tests model the decoder's public behavior: element accumulation, character-gap finalization, word-gap finalization, and pattern-length bounds. Hardware-specific modules such as OLED, BLE, GPIO, and LEDC still require Arduino/ESP32 compile or device validation.
