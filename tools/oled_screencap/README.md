# OLED screencap (live, real hardware, over BLE)

The `adb shell screencap` equivalent for MORPHEUS: grabs a screenshot of
what's actually on the physical device's OLED right now. Unlike
`tools/oled_render/` (which simulates the firmware on this machine with
no hardware involved), this talks to a real, running board - over BLE.

There used to be a USB-serial version of this. It's gone: opening the
serial port resets the ESP32 on every single connect (a driver-level
side effect of this board's auto-program circuit, not something
fixable from the host side - confirmed by hand, see the commit history
if you want the full story), making it useless for repeated
screenshots. BLE connect/disconnect never touches the reset pins at
all, so it doesn't have this problem.

## Requires

- A debug build: `config.h`'s `FEATURE_DEBUG_SERIAL_COMMANDS` set to `1`
  (it's `0` by default - flash a debug build first, ask before flashing
  a device you don't own or that's in regular use). Despite the name,
  this flag also gates the BLE dump commands, not just serial ones -
  it's the project's one existing bench-diagnostic gate.
- `FEATURE_BLE` on (it is, by default).
- The board powered on and within BLE range - no USB connection needed.
- `pip install bleak pillow` (both already present on this machine).

## Usage

```sh
python3 screencap_ble.py -o screenshot.png
```

Scans for a device named `MORPHEUS-CW`, connects, requests the screen
dump one chunk at a time, and decodes the reassembled buffer into a
PNG. Safe to run repeatedly back-to-back - no reboot, no waiting for
boot/splash to clear.

Options: `-n/--name` (device name, default `MORPHEUS-CW`), `-t/--timeout`
(seconds, default 15), `-v/--verbose` (log every BLE request/response).

## Wire protocol (why request-per-chunk, not a push)

`ble_control.cpp`'s `dump_screen_start` / `dump_screen_chunk` commands
(see that file's header comment for the exact JSON). The OLED buffer is
1024 bytes, far bigger than one BLE notification can carry, so it has
to be split into chunks either way. An earlier version tried the
obvious design - one command, sixteen back-to-back `notify()` calls -
and it reliably lost every chunk but the last, even though the
firmware's own `transport_sendControlEvent()` reported success on every
call. Confirmed by hand this wasn't a firmware bug: it's BlueZ's D-Bus
GATT notification delivery, which represents a notification as a
`Value` property change and coalesces updates arriving faster than the
client's event loop processes them - there's no queue guarantee at that
layer. Making the client explicitly request each chunk and wait for its
own response before asking for the next one sidesteps this entirely:
at most one notification is ever in flight.
