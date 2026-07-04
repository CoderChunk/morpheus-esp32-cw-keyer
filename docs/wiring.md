# Wiring

This document mirrors the firmware pin map in `firmware/MORPHEUS/config.h`.
The assignments are intended for an ESP32-WROOM-32 style development board.

## Pin map

| Function | GPIO | Direction | Active level | Notes |
| --- | ---: | --- | --- | --- |
| OLED SDA | 21 | I2C | n/a | SH1106 display data line. |
| OLED SCL | 22 | I2C | n/a | SH1106 display clock line. |
| Key / DIT | 25 | Input | LOW | Straight-key input in straight mode; DIT input in paddle mode. Internal pull-up enabled. |
| DAH | 26 | Input | LOW | DAH input in paddle mode; ignored in straight mode. Internal pull-up enabled. |
| Buzzer | 18 | Output | n/a | Sidetone output driven by ESP32 LEDC PWM. |
| Mode Switch | 33 | Input | HIGH/LOW | HIGH selects straight-key mode; LOW selects paddle mode. Internal pull-up enabled. |
| Bond Reset Button | 27 | Input | LOW | Momentary button to ground. Hold during boot for `BOND_RESET_HOLD_MS` to clear BLE bonds. |
| Reserved Keypad ADC | 34 | Input | Analog | Reserved for a future analog navigation keypad. No current electrical assignment. |

## Key and paddle wiring

- Wire straight keys between GPIO25 and ground.
- Wire paddle DIT between GPIO25 and ground.
- Wire paddle DAH between GPIO26 and ground.
- Inputs are active low and use ESP32 internal pull-ups, so external pull-up
  resistors are not required for normal short cable runs.
- Paddle reversal is handled in firmware by the saved `paddleReversed` setting;
  the default physical mapping is tip = DIT and ring = DAH.

## Mode and bond-reset controls

- GPIO33 selects the operating mode: HIGH for straight key and LOW for paddle.
- GPIO27 is the BLE bond-reset input. Hold it low at boot until the configured
  hold interval elapses to delete BLE bonds and reopen first-pairing behavior.
- The bond-reset action affects BLE bonding and trusted-device storage only; it
  does not factory-reset operator settings such as WPM or sidetone frequency.

## OLED and sidetone

- The OLED uses I2C address `0x3C` and is initialized on GPIO21/GPIO22.
- Keep I2C wires short and share ground with the ESP32.
- GPIO18 carries the sidetone PWM output. Use suitable drive circuitry if the
  buzzer or speaker requires more current than the ESP32 pin can supply.

## Reserved GPIO34

GPIO34 is input-only on ESP32. MORPHEUS currently reserves it for a future
analog navigation keypad resistor ladder. It is not used for WPM potentiometer
control in the current firmware.
