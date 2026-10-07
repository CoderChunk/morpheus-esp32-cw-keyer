# MORPHEUS BLE Test Client

> **Status: debug / diagnostics route.** The Flutter UI (`morpheus_ui` 1.4+) talks to
> MORPHEUS over native BLE on every platform and does not need this bridge. This
> tool is kept so a BLE problem can be isolated: build the UI with
> `--dart-define=MORPHEUS_TRANSPORT=bridge` (desktop only) to route it through the
> `bleak` client here, and compare. The GATT contract itself is unchanged.

A full-screen desktop GUI (PySide6) for testing MORPHEUS over Bluetooth
Low Energy: live word telemetry, plus full remote control of
**Training** and **Games** — including a virtual straight key, so
drills and games can be played entirely from the app. Launches
maximized with a sidebar-navigated, card-based dark theme.

See **[TESTING.md](TESTING.md)** for a step-by-step guide to testing
this app (pairing, connection, Training, Games) against real hardware.

## Setup

```sh
pip install -r requirements.txt
```

## Pairing

The word and control-event characteristics require a bonded, encrypted
link. Click **Pair New Device** in the app — on Linux this registers
the app itself as a BlueZ pairing agent (`ble_pairing.py`) and shows an
in-app dialog asking you to read the passkey off the MORPHEUS display
and enter/confirm it, the same experience as a smartwatch companion
app. No terminal, no separate OS Settings screen.

That button needs `dbus-next` (Linux only — see `requirements.txt`). If
it's missing, or you're on Windows/macOS, the button is disabled and
you'll need to pair once via your OS's own Bluetooth settings instead;
everything else in the app works identically either way once bonded.

## Run

```sh
python3 morpheus_ble_client.py
```

Leave the address field blank to auto-discover by name (`MORPHEUS-CW`),
or paste a specific MAC/UUID if you have multiple units.

## What's live vs. placeholder (in this Qt client)

The Flutter UI covers much more (keyer settings, device information, metrics, device
management); this table describes only this reference client.

| Sidebar section | Status |
|---|---|
| CW Keyer | Live — word telemetry (`BLE_WORD_CHAR_UUID`) |
| Training | Live — start/stop any training mode, live drill state, virtual keying |
| Games | Live — start/stop/pause/restart any of the 3 device games, live game state, virtual keying |
| Statistics, Connectivity, Profiles, Settings, Diagnostics, Tools | Placeholder — no command exists yet on the BLE control protocol for these |
| Help | Static info |

## Protocol

See `firmware/MORPHEUS/ble_control.cpp` for the authoritative command/
event schema on a new characteristic pair (`BLE_CONTROL_CMD_UUID` write,
`BLE_CONTROL_EVT_UUID` notify), separate from the pre-existing
`BLE_WORD_CHAR_UUID` word-telemetry characteristic. `protocol.py` holds
the UUID constants that must match `config.h`.

Commands are compact JSON (no whitespace after `:`, at most 96 bytes) written to
`BLE_CONTROL_CMD_UUID`; the full vocabulary is in `WS_PROTOCOL.md`:

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

- `backend.py` — `MorpheusBackend`: the transport-agnostic backend (no
  Qt import at all) implementing `BACKEND_API.md` -
  connection, keyer telemetry, virtual key, training, pairing,
  capabilities, structured errors
- `pairing_backend.py` — the BlueZ Agent1 D-Bus implementation, lazily
  imported by `backend.py` so a missing `dbus-next` only disables
  pairing, not the rest of the backend
- `ws_server.py` — local WebSocket/JSON IPC server exposing
  `MorpheusBackend` to non-Python frontends (built for the Flutter/Dart
  client on Windows/Linux/macOS - see `WS_PROTOCOL.md`; Android/iOS use
  a native `flutter_reactive_ble` driver instead, see
  `MOBILE_ARCHITECTURE.md` and `MOBILE_BLE_PROTOCOL.md` for the
  GATT-level wire format it implements); not needed to run the Qt desktop
  app
- `morpheus_ble_client.py` — main window, sidebar navigation, styling
- `ble_client_core.py` — `BleWorker`: a thin Qt-signal adapter over
  `MorpheusBackend`
- `ble_pairing.py` — `PairingWorker`: a thin Qt-signal adapter over
  `MorpheusBackend`'s pairing operations
- `pairing_dialog.py` — the in-app "Pair New Device" dialog
- `pages.py` — the CW Keyer / Training / Placeholder page widgets
- `arcade.py` — the Games tab: six client-side Morse typing/arcade
  games (no BLE involvement)
- `protocol.py` — UUID/command-vocabulary constants plus the exact
  Koch order and Morse table (copied from firmware source)

## Flutter Connectivity device management

Bridge application 1.2.0 adds exact-identifier discovery, runtime telemetry,
Linux paired-device import, PIN/confirmation/cancellation events and host bond
removal. Update requirements (`bleak>=1.0`) and restart the bridge to enable the
new Flutter device manager. Linux supports in-app six-digit PIN entry through
BlueZ; other platforms use their operating system prompt. The app keeps one
active connection and a separate saved-identity list. See WS_PROTOCOL.md and
MOBILE_BLE_PROTOCOL.md for capability limits. Firmware security is unchanged;
no firmware update/flash is required for these management additions.
