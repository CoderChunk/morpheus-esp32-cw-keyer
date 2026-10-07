# Wiring

This document mirrors the firmware pin map in `firmware/MORPHEUS/config.h` and
`firmware/MORPHEUS/ui_config.h`. The assignments are intended for an ESP32-WROOM-32
style development board.

## Pin map

| Function | GPIO | Direction | Active level | Notes |
| --- | ---: | --- | --- | --- |
| OLED SDA | 21 | I2C | n/a | SH1106 display data line. |
| OLED SCL | 22 | I2C | n/a | SH1106 display clock line. |
| Key / DIT (jack tip) | 25 | Input | LOW | Straight-key input in straight mode; DIT input in paddle mode. Internal pull-up enabled. |
| DAH (jack ring) | 26 | Input | LOW | DAH input in paddle mode; ignored in straight mode. Internal pull-up enabled. |
| Buzzer | 18 | Output | n/a | Sidetone output driven by ESP32 LEDC PWM. |
| Status LED | 27 | Output | HIGH | Single red LED, pattern-driven (not PWM). |
| Rotary encoder A | 19 | Input | n/a | Menu navigation. |
| Rotary encoder B | 23 | Input | n/a | Menu navigation. |
| Encoder push | 4 | Input | LOW | Select. |
| Confirm button | 14 | Input | LOW | Confirm / OK. |
| Back button | 13 | Input | LOW | Back. |

## Key and paddle wiring

- Wire straight keys between GPIO25 and ground.
- Wire paddle DIT between GPIO25 and ground.
- Wire paddle DAH between GPIO26 and ground.
- Inputs are active low and use ESP32 internal pull-ups, so external pull-up
  resistors are not required for normal short cable runs.
- Paddle reversal is a saved setting; the default physical mapping is
  tip = DIT and ring = DAH.
- Straight-key versus paddle operation is a setting in the menu, not a switch.

## Bluetooth bonds

BLE bonds are cleared from the menu (Connectivity > Bluetooth > Bond Reset), not by
a hardware button. Bond reset affects BLE bonding and trusted-device storage only;
it does not factory-reset operator settings such as WPM or sidetone frequency.

## OLED and sidetone

- The OLED uses I2C address `0x3C` and is initialized on GPIO21/GPIO22.
- Keep I2C wires short and share ground with the ESP32.
- GPIO18 carries the sidetone PWM output. Use suitable drive circuitry if the
  buzzer or speaker requires more current than the ESP32 pin can supply.
