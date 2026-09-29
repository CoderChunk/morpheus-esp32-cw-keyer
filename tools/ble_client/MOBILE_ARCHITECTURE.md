# MORPHEUS Mobile (Android/iOS) Architecture Decision

## Architecture at a glance

```
Windows/Linux/macOS  →  Flutter  →  WebSocket (ws_server.py)  →  Python backend (MorpheusBackend/bleak)  →  MORPHEUS
Android/iOS           →  Flutter  →  flutter_reactive_ble       →  MORPHEUS BLE directly (no WebSocket, no Python)
```

**There is no Python process, no WebSocket, and no `ws_server.py`
anywhere in the Android/iOS path.** On mobile, Flutter talks straight
to the MORPHEUS device's GATT characteristics via
`flutter_reactive_ble` — see `MOBILE_BLE_PROTOCOL.md` for that wire
format. Every other mention of "Python" or "WebSocket" below is either
describing the desktop-only path, or explaining why that path
specifically cannot be reused on mobile.

## The question

`WS_PROTOCOL.md` documents a local `ws://127.0.0.1:8765` WebSocket server
(`ws_server.py`) wrapping the Python `MorpheusBackend`. That works for
Windows/Linux/macOS by spawning it as a bundled subprocess. Android and
iOS need a different answer, because **neither "embed the existing
Python runtime" nor "bridge to the existing Python implementation" is
possible in production on both mobile OSes** — this section explains
why, with the specific technical blocker for each.

## Decision

- **Desktop (Windows/Linux/macOS): unchanged.** Spawn `ws_server.py`,
  connect over the documented WebSocket/JSON protocol. Nothing in
  `WS_PROTOCOL.md`/`BACKEND_API.md` changes for these three platforms.
- **Mobile (Android/iOS): a native Dart implementation of the identical
  protocol contract**, using **`flutter_reactive_ble`** (BSD 3-Clause —
  standardized on for license compatibility with a free, commercial-
  friendly app) instead of a WebSocket connection to a Python process.
  No Python runs on mobile at all. See `MOBILE_BLE_PROTOCOL.md` for the
  exact GATT-level wire format this plugin must implement against.

This is option 3 from the three you listed, precisely because option 1
fails outright on iOS (and buys nothing on Android), and option 2 —
"bridge to the existing BLE/backend implementation" — has nothing
working to bridge *to* on either mobile OS, for the reasons below.

## Why option 1 (embedded Python runtime) doesn't work

**iOS: dead end, not a limitation to work around.**
- `bleak` (the Python BLE library `backend.py` uses) has **no iOS
  backend at all** — its supported platforms are Windows (WinRT),
  Linux (BlueZ), and macOS (Core Bluetooth). There is no Core
  Bluetooth-via-Python bridge for iOS in bleak, maintained or
  otherwise.
- Even setting BLE aside: Apple's `Process`/`NSTask` API — the only
  way to spawn a subprocess on an Apple platform — is explicitly
  documented as **unavailable on iOS**. An iOS app cannot launch an
  independent long-lived helper process for a Flutter app to connect
  to over a local socket, which is exactly the pattern `ws_server.py`
  needs. This isn't a sandboxing inconvenience to route around; the
  API doesn't exist on the platform.
- `dbus-next` (pairing) requires D-Bus + BlueZ, neither of which
  exists on iOS as an OS facility. `pairing_backend.py` has no path
  forward there under any packaging strategy.

**Android: technically embeddable, but buys nothing.**
- Python *can* be embedded in an Android app (Chaquopy is a real,
  production-used SDK for exactly this), and Android does allow
  background processes — more permissive than iOS.
- But `bleak`'s Android support is a community-contributed backend
  built on `python-for-android`, not a primary-supported target of the
  library — not something to build production BLE reliability on.
- `dbus-next`/D-Bus/BlueZ still don't exist on Android (it uses its
  own Bluetooth stack with Java/Kotlin APIs), so `pairing_backend.py`
  is inapplicable there too, same as iOS.
- Net result: embedding Python on Android would still end up calling
  Android's native BLE APIs underneath (via whatever bridges bleak's
  Android backend uses), just with an extra Python/Chaquopy layer, a
  second runtime bundled into the app, and a fragile Kotlin↔Python glue
  boundary — for zero capability gained over calling those native APIs
  directly.

## Why option 2 (native bridge to the existing implementation) doesn't work as stated

If "bridge to the existing BLE/backend implementation" means native
Kotlin/Swift code that calls into `backend.py`'s Python logic for the
actual GATT operations — there's nothing there to call into. The part
that's genuinely OS-specific (opening the connection, discovering
characteristics, subscribing to notifications) has no working Python
implementation on either mobile OS, per above. A bridge needs two
working ends; only one exists here.

The salvageable half of this idea is real, and is what we're
recommending — just relocated: bridge to the **protocol/domain
knowledge** (what a command means, what an event contains, the Morse
table), not to the Python runtime. That knowledge is fully specified
and frozen (`WS_PROTOCOL.md`, `BACKEND_API.md`, `protocol.py`) — a
native Dart implementation transcribes it rather than re-deriving
anything.

## The resulting architecture

```
                    ┌─────────────────────────┐
                    │   Flutter UI / state     │   <- identical on
                    │   management (all 5 OSes)│      every platform
                    └────────────┬─────────────┘
                                 │  MorpheusClient (abstract Dart interface,
                                 │  same methods/events as WS_PROTOCOL.md §4-§6)
                     ┌───────────┴───────────┐
                     │                       │
     ┌───────────────▼──────────┐  ┌────────────▼───────────────┐
     │ WebSocketMorpheusClient   │  │ NativeBleMorpheusClient     │
     │ (Windows/Linux/macOS)     │  │ (Android/iOS)               │
     │                           │  │                             │
     │ spawns ws_server.py,      │  │ flutter_reactive_ble,       │
     │ connects to               │  │ talks GATT directly         │
     │ ws://127.0.0.1:8765       │  │ (see MOBILE_BLE_PROTOCOL.md)│
     └───────────────┬──────────┘  └─────────────┬───────────────┘
                      │                       │
              ┌───────▼───────┐      ┌────────▼────────┐
              │ backend.py     │      │ MORPHEUS device   │
              │ (bleak)        │─────▶│ (BLE GATT)        │
              └────────────────┘      └───────────────────┘
```

Both driver classes implement the **same** Dart interface — the exact
command/event surface already documented in `WS_PROTOCOL.md` §4
(Methods) and §5-§6 (Events/data shapes). Every screen, every bit of
app state management, is written once. Only the two driver classes
differ, and each is a thin transport adapter — same relationship
`ws_server.py` already has to `backend.py`, just implemented in Dart
instead of Python for the mobile case.

## What gets duplicated into Dart, and how to keep it from drifting

| Source of truth (Python) | Dart copy | Drift risk |
|---|---|---|
| `protocol.py`'s `SERVICE_UUID`, `WORD_CHAR_UUID`, `CONTROL_CMD_UUID`, `CONTROL_EVT_UUID` | a small `lib/morpheus_protocol.dart` constants file | Low — frozen firmware values, changes only alongside a firmware `config.h` change |
| Command JSON vocabulary (`{"cmd":"key_down"}`, `{"cmd":"train_start","mode":...}`, etc.) | encode/decode logic in `NativeBleMorpheusClient` | None — already fully specified in `WS_PROTOCOL.md` §4, mechanical transcription |
| Event JSON shapes (`ConnectionInfo`, `KeyerWordEvent`, `TrainingState`, `BackendError`) | Dart data classes matching `WS_PROTOCOL.md` §6 | None — same reason |
| `protocol.MORSE_TABLE`, `protocol.KOCH_ORDER` | static Dart constants | Low, but recommend generating the Dart file from the Python source with a small script if the team wants zero-maintenance parity, rather than hand-copying |

None of this is new domain modeling. `backend.py`/`ble_control.cpp`
remain the single source of truth for what the protocol *means* — only
its *transport* is reimplemented per platform, exactly mirroring how
`ws_server.py` is already a thin adapter rather than a re-derivation.

## Pairing on mobile (does not involve `pairing_backend.py` at all)

| Platform | Pairing mechanism |
|---|---|
| Linux desktop | Existing BlueZ Agent1 D-Bus flow (`pairing_backend.py`) — unchanged |
| Windows/macOS desktop | OS's own Bluetooth Settings, as already documented (fallback when `dbus-next` pairing is unavailable) |
| Android | Standard OS bonding — the BLE plugin's pair/create-bond call, or the system pairing dialog auto-triggers on first access to an encrypted characteristic. The phone's own UI shows "Enter the code on MORPHEUS's display." |
| iOS | Core Bluetooth bonds automatically the first time the app touches an encrypted characteristic — the system passkey prompt appears with no app-level pairing code needed |

Mobile pairing UX ends up **simpler** than desktop: both mobile OSes
already own the passkey UI for BLE bonding. Linux is the one platform
that lacks that built-in UX, which is exactly why `pairing_backend.py`
exists in the first place — it isn't a mobile requirement to replicate.

## Scope correction to `WS_PROTOCOL.md`

`WS_PROTOCOL.md` should be read as **the desktop transport contract**
(Windows/Linux/macOS) plus **the protocol specification** that the
mobile native driver implements directly over GATT instead of over
WebSocket. Its method/event/data-shape definitions (§4-§6) apply
unchanged to both; only §1 (Transport) and §10 (Dart client sketch)
are desktop-specific and should be read with that scope in mind.

## Summary answer

**Option 3.** Desktop keeps the documented Python/WebSocket backend
exactly as-is — no changes. Android/iOS get a native Dart
implementation of the same command/event contract via a Flutter BLE
plugin, because `bleak` has no iOS backend, `dbus-next`/D-Bus/BlueZ
exist on neither mobile OS, and iOS cannot spawn a persistent separate
process for a Flutter app to connect to in the first place. This
requires Flutter-side engineering work (a second driver class) but
**zero API/domain changes** — `WS_PROTOCOL.md` and `BACKEND_API.md`
remain the spec both drivers implement.
