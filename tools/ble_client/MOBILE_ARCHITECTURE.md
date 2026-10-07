# MORPHEUS Client Transport Architecture

The MORPHEUS UI (`morpheus_ui`) talks to the device through one abstract Dart
interface, `MorpheusClient`, with two interchangeable implementations. Every screen and
all application state are written once; only the transport differs.

```
                    ┌──────────────────────────┐
                    │  Flutter UI / MorpheusSession  │   identical on every platform
                    └────────────┬─────────────┘
                                 │  MorpheusClient (command/event surface of WS_PROTOCOL.md)
                     ┌───────────┴────────────┐
                     │                        │
     ┌───────────────▼───────────┐  ┌─────────▼───────────────────┐
     │ NativeBleMorpheusClient    │  │ WebSocketMorpheusClient      │
     │ DEFAULT, every platform    │  │ DEBUG FALLBACK, desktop only │
     │                            │  │                              │
     │ BleLink abstraction:       │  │ ws://127.0.0.1:8765          │
     │  Android/iOS: flutter_     │  │   → ws_server.py             │
     │   reactive_ble             │  │   → backend.py (bleak)       │
     │  Windows/Linux/macOS:      │  │                              │
     │   universal_ble            │  │                              │
     └───────────────┬───────────┘  └─────────┬───────────────────┘
                     │                         │
                     └────────► MORPHEUS (BLE GATT) ◄────────┘
```

## Decision

- **Native BLE is the default on every platform.** The app talks to the device's GATT
  characteristics directly (`MOBILE_BLE_PROTOCOL.md` is the wire-level reference). There
  is no helper process to start.
- **The Python bridge is kept as a debug route on desktop.** If a connection problem
  might be in the app's BLE stack rather than the device, build the UI with
  `--dart-define=MORPHEUS_TRANSPORT=bridge`. It routes through `ws_server.py` and
  `backend.py` (`bleak`), which removes the Flutter BLE stack from the picture. The
  method/event contract is documented in `WS_PROTOCOL.md` and `BACKEND_API.md`.
- **The bridge cannot exist on mobile.** `bleak` has no iOS backend, iOS cannot spawn a
  helper process, and `dbus-next`/BlueZ do not exist on Android or iOS. Android and iOS
  therefore always use native BLE and ignore `MORPHEUS_TRANSPORT`.

## Native client structure

`NativeBleMorpheusClient` is platform-neutral and works against a small `BleLink`
interface (adapter state, scan, connect, service discovery, subscribe, read, write,
pairing and bonding). Two implementations exist:

| Platform | `BleLink` | Library |
|---|---|---|
| Android, iOS | `ReactiveBleLink` | `flutter_reactive_ble` |
| Windows, Linux, macOS | `UniversalBleLink` | `universal_ble` |

The protocol knowledge (command vocabulary, event shapes, Morse table, Koch order) is
mirrored from `protocol.py` and the firmware's `ble_control.cpp`, which remain the source
of truth. A test (`tests/test_protocol_consistency.py`) checks that the documents, the
bridge and the firmware stay consistent.

## Pairing

| Platform | Mechanism |
|---|---|
| Android | OS bonding; the system dialog asks for the code shown on the MORPHEUS display. |
| iOS | Core Bluetooth bonds the first time an encrypted characteristic is touched; the system passkey prompt appears. |
| Windows, macOS | The app requests pairing; the operating system shows the prompt. |
| Linux | The app does not call `Pair()`; the pairing agent or desktop environment presents the PIN prompt and the app waits for the bond. The Python bridge can register its own BlueZ agent for in-app PIN entry. |

On every platform the device shows a 6-digit passkey that must be entered within about
30 seconds (the Bluetooth security-manager timeout).
