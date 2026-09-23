# MORPHEUS BLE Test Client

A desktop GUI (PySide6) for testing MORPHEUS over Bluetooth Low Energy:
live word telemetry, plus full remote control of **Training** and
**Games** — including a virtual straight key, so drills and games can
be played entirely from the app.

## Setup

```sh
pip install -r requirements.txt
```

The device must already be **bonded** at the OS level (pair once via
your system's Bluetooth settings or `bluetoothctl`, confirming the
passkey shown on the MORPHEUS display) — this tool connects to an
existing bond, it does not perform pairing itself.

## Run

```sh
python3 morpheus_ble_client.py
```

Leave the address field blank to auto-discover by name (`MORPHEUS-CW`),
or paste a specific MAC/UUID if you have multiple units.

## What's live vs. placeholder

| Sidebar section | Status |
|---|---|
| CW Keyer | Live — word telemetry (`BLE_WORD_CHAR_UUID`) |
| Training | Live — start/stop any of the 6 modes, live drill state, virtual keying |
| Games | Live — start/stop/pause/restart any of the 3 games, live game state, virtual keying |
| Statistics, Connectivity, Profiles, Settings, Diagnostics, Tools | Placeholder — no command exists yet on the BLE control protocol for these |
| Help | Static info |

## Protocol

See `firmware/MORPHEUS/ble_control.cpp` for the authoritative command/
event schema on a new characteristic pair (`BLE_CONTROL_CMD_UUID` write,
`BLE_CONTROL_EVT_UUID` notify), separate from the pre-existing
`BLE_WORD_CHAR_UUID` word-telemetry characteristic. `protocol.py` holds
the UUID constants that must match `config.h`.

Commands are compact JSON (no whitespace after `:`) written to
`BLE_CONTROL_CMD_UUID`:

```json
{"cmd":"train_start","mode":"KOCH"}
{"cmd":"key_down"} / {"cmd":"key_up"}
{"cmd":"game_start","game":"COPY"}
```

State/ack/error events are pushed as JSON notifications on
`BLE_CONTROL_EVT_UUID`:

```json
{"evt":"train_state","active":true,"mode":"KOCH","phase":"LISTENING","target":"K", ...}
{"evt":"game_state","game":"COPY","active":true,"phase":"FALLING","target":"M", ...}
{"evt":"ack","cmd":"train_start","ok":true}
{"evt":"error","message":"unknown cmd"}
```

Extending this to Settings/Profiles/Diagnostics/Statistics means adding
new `cmd`/`evt` pairs to `ble_control.cpp` (the firmware-side bridge,
architecturally the BLE analogue of `ui_backend.cpp`) and a
corresponding page in `pages.py`.

## Files

- `morpheus_ble_client.py` — main window, sidebar navigation, styling
- `ble_client_core.py` — `BleWorker`: runs `bleak` on a background
  thread, exposes Qt signals to the GUI thread
- `pages.py` — the CW Keyer / Training / Games / Placeholder page widgets
- `protocol.py` — UUID and command-vocabulary constants
