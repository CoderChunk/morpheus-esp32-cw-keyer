# MORPHEUS Backend WebSocket/JSON Protocol (Windows/Linux/macOS only)

> **Status: debug / diagnostics route.** The Flutter UI (`morpheus_ui` 1.4+) talks to
> MORPHEUS over native BLE on every platform and does not need this bridge. This
> tool is kept so a BLE problem can be isolated: build the UI with
> `--dart-define=MORPHEUS_TRANSPORT=bridge` (desktop only) to route it through the
> `bleak` client here, and compare. The GATT contract itself is unchanged.

## Additive CW measurements — firmware 2.8.4

`probeKeyerMetrics` accepts `{"id":"<1..24 ASCII letters/digits>","reset":false}`.
`reset` is optional and defaults to false. The usual request response confirms
dispatch only; it does **not** constitute a device measurement. The server does
not await BLE in its request dispatcher, so remote key-up remains dispatchable.
The existing default event subscription includes `keyerMetricsReceived`:

```json
{"type":"event","event":"keyerMetricsReceived","data":{"evt":"keyer_metrics","id":"a123","ts":456,"seq":3,"ditMs":73,"dahMs":213,"gapMs":218,"virtual":false,"bleRoundTripMs":43.0}}
```

Example values describe a protocol fixture, not device defaults. Firmware sends
one correlated response through existing GATT 0004 for `probe_keyer` or
`reset_keyer_metrics`. The latter clears observations before replying. `ts` is
uint32 ESP32 uptime; `seq` is a uint32 completed-element/reset counter. `ditMs`
and `dahMs` are latest actual completed key-down durations, classified by the
existing device keyer. `gapMs` is the actual silence between the last key-up of
a decoded character and the next key-down, after the decoder finalized a
character and before a word boundary. Word gaps are excluded. No duration is
computed from WPM. Each sample is null before measurement or after 30 seconds.
`virtual` identifies the observed physical/remote source; switching source
clears earlier samples. Active firmware training/games yield null samples.

The Python BLE central adds `bleRoundTripMs`, measured with its monotonic clock
immediately before GATT write until the matching device response. Writer queue
delay is excluded; BLE/device processing and notification delivery are included.
Only outstanding matching IDs are accepted, once; cached/unsolicited packets
cannot supply latency. After five seconds without a response, or on disconnect,
the event is `{"id":"a123","unavailable":true}` with no invented timing fields.
The Flutter desktop client separately measures application request-to-device
reply RTT, including the WebSocket bridge, and correlates both request dispatch
and device reply. Neither value is a one-way latency estimate.

Minimum firmware for these commands is 2.8.4. Older firmware retains all prior
workflows; the CW page suppresses unsupported probes and shows unavailable
measurements. No new characteristic, pairing policy, decoder or settings format
is introduced. Probe writes have lower priority than session controls/key edges.

**This document describes the desktop transport only.** Android and
iOS do not use a WebSocket or a Python process at all — see
`MOBILE_BLE_PROTOCOL.md` for the mobile transport, and
`MOBILE_ARCHITECTURE.md` for why the two platforms need different
transports.

The language-neutral IPC boundary for non-Python **desktop** frontends
(built for the Flutter/Dart client on Windows/Linux/macOS, usable by
anything that speaks WebSocket + JSON). Implemented by `ws_server.py`,
which wraps `backend.py`'s `MorpheusBackend` - **all BLE/device logic
stays in Python**; this server only translates it to/from JSON over a
local socket.

```
python3 ws_server.py --host 127.0.0.1 --port 8765
```

Start it in a separate terminal and build the UI with
`--dart-define=MORPHEUS_TRANSPORT=bridge`; this protocol doesn't care who
starts it.

**Platform scope:** this WebSocket transport is a desktop debug route
for **Windows, Linux and macOS only**. Android and iOS cannot host this Python process at
all (no iOS `bleak` backend, no D-Bus/BlueZ on either mobile OS, and
iOS cannot spawn a persistent separate process in the first place) —
see `MOBILE_ARCHITECTURE.md` for the mobile decision. The method/event/
data-shape definitions below (§4-§6) are the authoritative
protocol spec for both transports; the native Dart client (the default
on every platform) implements them directly over GATT instead of over
this WebSocket. Only §1 (Transport) and §10
(Dart client sketch, which assumes this WebSocket) are bridge-specific.

---

## Connectivity device management — bridge 1.2.0

These additive host operations use the existing WebSocket boundary; firmware
GATT UUIDs, security policy and command formats are unchanged. Restart an older
bridge after updating its source/dependencies (`bleak>=1.0`). A client must check
`getDeviceRuntime.deviceManagement`; an older bridge reports an unsupported
method instead of accepting a fictitious pairing.

| Method | Params | Result / completion |
|---|---|---|
| `discoverDevices` | `{}` | `{ "devices": [ManagedDevice] }`, after a five-second discovery; preserves the current application connection |
| `listPairedDevices` | `{}` | `{ "devices": [ManagedDevice] }`, actual MORPHEUS BlueZ host bonds when `canListPairings` is true; otherwise an empty array with capability false |
| `getDeviceRuntime` | `{}` | Runtime object below; bridge cache/status, not a device-latency probe |
| `startPairing` | `{ "targetDeviceName": string?, "deviceAddress": string?, "attemptId": string? }` | `null` after dispatch; wait for correlated `pairingStateChanged` |
| `submitPasskey` | `{ "passkey": string, "attemptId": string? }` | `null`; exactly six ASCII digits, including leading zeros |
| `confirmPairing` | `{ "accepted": boolean, "attemptId": string? }` | `null`; strict boolean |
| `cancelPairing` | `{ "attemptId": string? }` | `null` after stopping the pending agent/system-pairing worker |
| `removePairing` | `{ "deviceAddress": string }` | `null` after actual host bond removal; requires disconnection of that device first |

`ManagedDevice` contains `identifier`, `name`, nullable `paired`, nullable
`lastSeen` (ISO8601 UTC) and nullable `rssi` (advertisement dBm). No secret is
included. Multiple devices with the same advertised name are selected by
identifier. Linux/Windows use Bluetooth MAC addresses; macOS uses the
CoreBluetooth UUID. Legacy name-only `startPairing` remains supported on Linux.
The new UI always supplies both an exact identifier and a fresh attempt token.

```json
{
  "deviceManagement": true,
  "bridgeRunning": true,
  "usesSystemPairing": false,
  "canListPairings": true,
  "canRemovePairing": true,
  "scanActive": false,
  "notificationsReady": false,
  "lastSeen": null,
  "rssi": null,
  "signalPercent": null
}
```

Flags describe actual host capabilities: the Linux BlueZ agent supplies custom
PIN entry, bond enumeration and removal. Windows uses the system prompt and
Bleak unpair; macOS uses the system prompt and requires system Bluetooth
settings for removal. A Linux installation without the optional agent also
uses system pairing. Bond enumeration is not claimed on those fallback paths.

`bridgeRunning` means this RPC was served by the running bridge. The app measures
its own WebSocket request/response round trip for **Bridge latency**, separately
from the existing correlated hardware BLE round trip. `notificationsReady`
becomes true only after subscriptions and an authenticated control read succeed.
`lastSeen` records actual received device frames/readback. Scan RSSI is exposed
only for the active identifier while its advertisement is younger than 30s.
Neither firmware nor the central currently supplies a calibrated percentage;
`signalPercent` is null, never derived from RSSI.

Pairing events add `deviceAddress` and `attemptId`. Clients ignore previous
attempts or other devices. States include existing STARTED/PASSKEY/CONFIRMATION/
SUCCEEDED/FAILED/UNAVAILABLE plus `PAIRING_SYSTEM_PROMPT` and
`PAIRING_CANCELLED`. A passkey may appear only for an active display/comparison
step; terminal state models discard it. Requests are not logged with PINs and
the UI stores identities only. Wrong PIN or timeout produces FAILED with retry;
cancellation cancels BlueZ Pair/agent futures and unregisters the agent.

The app disconnects its active device and observes DISCONNECTED before starting
pairing or connecting another saved device. `connect` waits for old worker
cleanup before launching a new one. CONNECTED is emitted only after secured
GATT is usable. A saved identity is not proof of a current OS bond: where OS
enumeration exists, missing bonds are demoted to saved/unpaired identities.
Removing a pairing completes OS removal before removing the local identity;
a failed removal leaves the saved identity visible.

The current BLE events do not report complete decoded-character duration.
Connectivity displays that field as unavailable instead of calculating a value
from configured WPM. Received time uses the application's actual receipt clock.

## 1. Transport

- Plain WebSocket (`ws://`, not `wss://` — this is a local-only
  loopback service, not exposed to a network).
- Every message in both directions is a single JSON object per
  WebSocket text frame (no batching, no newline framing).
- Default endpoint: `ws://127.0.0.1:8765`.

## 2. Message envelope

Three message shapes exist, distinguished by `"type"`:

### 2.1 Request (client → server)

```json
{
  "id": "<string or number, echoed back verbatim>",
  "method": "<method name, §4>",
  "params": { "...": "method-specific, §4" }
}
```

`id` is caller-assigned (a UUID, an incrementing counter, whatever the
client finds convenient) and is not interpreted by the server beyond
echoing it back on the matching response. `params` is omitted or `{}`
for methods that take no arguments.

### 2.2 Response (server → client, one per request)

Success:

```json
{ "type": "response", "id": "<same id>", "ok": true, "result": { "...": "method-specific, §4" } }
```

Failure:

```json
{
  "type": "response", "id": "<same id>", "ok": false,
  "error": { "code": "<ErrorCode, §8>", "message": "<human-readable>" }
}
```

`result` is `null` for methods that have nothing to return (most
commands — see §4; they're fire-and-forget, with outcomes observed
through events, not through the response).

### 2.3 Event (server → client, unprompted, any time)

```json
{ "type": "event", "event": "<channel name, §5>", "data": { "...": "channel-specific, §5" } }
```

---

## 3. Subscription lifecycle

Each WebSocket connection has its own independent subscription set.
**A new connection starts subscribed to every channel** (§5) —
call `unsubscribe` immediately after connecting if a client wants a
narrower default.

```json
{ "id": "1", "method": "subscribe", "params": { "events": ["keyerWordReceived", "trainingStateChanged"] } }
{ "id": "2", "method": "unsubscribe", "params": { "events": ["backendError"] } }
```

Both return `{ "subscribed": ["<sorted list of currently-subscribed channel names>"] }`.
Omitting `params.events` (or passing an empty/absent array) means "all
channels" for either call — `subscribe` with no `events` subscribes to
everything, `unsubscribe` with no `events` unsubscribes from
everything.

---

## 4. Methods (requests)

| Method | Params | Result | Notes |
|---|---|---|---|
| `scan` | `{}` | `null` | discovery only, no connect |
| `connect` | `{ "deviceAddress": string? }` | `null` | omit `deviceAddress` for auto-discover by name |
| `disconnect` | `{}` | `null` | |
| `keyDown` | `{}` | `null` | |
| `keyUp` | `{}` | `null` | |
| `startTraining` | `{ "mode": TrainingMode }` | `null` | invalid mode → `INVALID_PARAMETER` error response |
| `stopTraining` | `{}` | `null` | |
| `confirmTraining` | `{}` | `null` | |
| `answerTraining` | `{ "text": string }` | `null` | identification answer for `LISTENING`/`COMBINED`, only meaningful while `phase: "AWAIT_ANSWER"`; a no-op otherwise. `text` must be exactly one character to ever match |
| `startGame` | `{ "game": GameId }` | `null` | `GameId`: `COPY\|MEMORY\|SPEED`; invalid id → `INVALID_PARAMETER`. Stops any active Training session first (device-side exclusivity) |
| `stopGame` | `{}` | `null` | |
| `pauseGame` | `{}` | `null` | toggles pause |
| `confirmGame` | `{}` | `null` | restarts the game once it's over |
| `restartGame` | `{}` | `null` | restarts the game immediately, any time |
| `startPairing` | `{ "targetDeviceName": string?, "deviceAddress": string?, "attemptId": string? }` | `null` | see Connectivity device management above |
| `submitPasskey` | `{ "passkey": string, "attemptId": string? }` | `null` | 6 numeric digits |
| `confirmPairing` | `{ "accepted": boolean, "attemptId": string? }` | `null` | |
| `requestDeviceInfo` | `{}` | `null` | fire-and-forget, same as the commands above — result arrives as a `deviceInfoChanged` event (§5, §6.7). Unlike `getSnapshot`, this **is** a real BLE round trip |
| `getSnapshot` | `{}` | `Snapshot` (§4.1) | call once right after connecting |
| `getMorseTable` | `{}` | `{ "table": {char: pattern}, "reverse": {pattern: char} }` | §7 |
| `getKochSequence` | `{}` | `{ "sequence": string }` | the 40-char Koch order |
| `subscribe` | `{ "events": string[]? }` | `{ "subscribed": string[] }` | §3 |
| `unsubscribe` | `{ "events": string[]? }` | `{ "subscribed": string[] }` | §3 |

Every command (`scan` through `confirmPairing`) is **fire-and-forget**:
its response only confirms the request was accepted and dispatched,
never that the underlying BLE operation succeeded. Outcomes always
arrive as events on the matching channel (§5) — a client must not
treat `"ok": true` on `connect` as "connected," for example; wait for a
`connectionChanged` event with `state: "CONNECTED"`.

### 4.1 `getSnapshot` result shape

```json
{
  "connection": ConnectionInfo,
  "training": TrainingState,
  "game": GameState,
  "capabilities": Capabilities,
  "metadata": { "applicationVersion": string, "deviceName": string?, "deviceFirmwareVersion": string? }
}
```

Call this once immediately after connecting to the WebSocket (and
again after any reconnect) to get current state without racing events
that were emitted before the client subscribed — `MorpheusBackend`
caches the latest `ConnectionInfo`, `TrainingState`, and `GameState`
internally specifically so this is always answerable instantly, with
no BLE round trip.

---

## 5. Events

| Channel | `data` shape | Emitted when |
|---|---|---|
| `connectionChanged` | `ConnectionInfo` (§6.1) | connection state changes |
| `keyerWordReceived` | `KeyerWordEvent` (§6.2) | the device completes a keyed word |
| `keyerLiveWordReceived` | `LiveWordEvent` (§6.2a) | the device decodes one more character of the in-progress word |
| `keyerLivePatternReceived` | `LivePatternEvent` (§6.2b) | the device keys one more dit/dah element (or finalizes a character, clearing it) |
| `trainingStateChanged` | `TrainingState` (§6.3) | training state changes |
| `gameStateChanged` | `GameState` (§6.3a) | a device game's (`COPY`/`MEMORY`/`SPEED`) state changes |
| `pairingStateChanged` | `PairingEvent` (§6.4) | pairing flow progresses |
| `deviceInfoChanged` | `DeviceInfo` (§6.7) | a `requestDeviceInfo` round trip completes |
| `backendError` | `BackendError` (§6.5) | any operation fails |

## 6. Data shapes

### 6.1 ConnectionInfo

```json
{ "state": "SCANNING|CONNECTING|CONNECTED|DISCONNECTED|NOT_FOUND|ERROR",
  "deviceName": string?, "deviceAddress": string?, "statusMessage": string? }
```

`deviceName`/`deviceAddress` are only populated once `state` reaches
`CONNECTED` (or, after a bare `scan()`, once a matching device is
found — see `ws_server.py`'s `scan` handling).

### 6.2 KeyerWordEvent

```json
{ "word": string, "wpm": 5-40, "mode": "STRAIGHT|PADDLE", "timestamp": integer }
```

`timestamp` is device uptime in milliseconds, **not** wall-clock time
(no RTC on the device).

### 6.2a LiveWordEvent

```json
{ "word": string, "wpm": 5-40, "mode": "STRAIGHT|PADDLE", "timestamp": integer }
```

Same shape as `KeyerWordEvent` (§6.2) — `word` here is the **whole
in-progress word so far**, not yet finalized by a word-gap. Fires once
per decoded character (firmware `events_onCharacterComplete()`), far
more often than `keyerWordReceived` and never itself authoritative:
`keyerWordReceived` remains the one event that means "this word is
done." A client that only cares about completed words can ignore this
channel entirely; a client showing live per-character decode (e.g. the
Home screen's live console) should prefer this event's `word` and fall
back to the last `keyerWordReceived` value once it stops updating.

At typical/worst-case keying speed (30-40 WPM) this fires at most every
120-160ms — well within a BLE connection interval, no notification-
coalescing risk (unlike the much burstier OLED-screenshot dump feature,
whose chunks were microseconds apart).

### 6.2b LivePatternEvent

```json
{ "pattern": string, "timestamp": integer }
```

`pattern` is the in-progress dit/dah pattern for the character
currently being keyed, using `.`/`-` (e.g. `".-"` mid-way through "A")
— empty once the character finalizes (mirrors the OLED's live pattern
readout, `ui_backend_getLivePattern()`). Fires once per keyed element
(firmware `events_onPatternChanged()`, called from
`core_decoder_addElement()`), the most frequent of the three keyer
telemetry events — at worst case (rapid dits at 40 WPM) roughly every
30-60ms, still well inside a BLE connection interval.

### 6.3 TrainingState

```json
{
  "active": boolean, "mode": "KOCH|CHARACTERS|WORDS|CALLSIGNS|ADAPTIVE|EXAM|LISTENING|COMBINED"?,
  "phase": string?, "target": string?, "correct": integer, "attempts": integer,
  "kochLevel": integer?, "adaptiveWpm": integer?, "wpm": integer?,
  "examScorePercent": integer?, "examPassed": boolean?,
  "examCorrectCount": integer?, "examTotalCount": integer?
}
```

`kochLevel` only meaningful for `KOCH`; `adaptiveWpm` only for
`ADAPTIVE`; the four `exam*` fields only once an `EXAM` session has
finished (`examTotalCount` is always `25` when present).

`wpm` (since firmware v2.7.1) is the base keyer speed
(`core_keyer_getWpm()`) - what every mode except `ADAPTIVE` actually
plays `target` at (`ADAPTIVE` uses `adaptiveWpm` instead). Present for
every active mode, always. A client synthesizing its own listening
audio (e.g. so an operator not next to the device can still hear it)
should use this to compute element timing (`1200 / wpm` ms per dit)
rather than assuming a fixed speed.

`phase` includes `"AWAIT_ANSWER"` for `LISTENING`/`COMBINED` rounds —
the device has played `target` and is waiting for `answerTraining`
(§4), not a keyed reply (`"LISTENING"` phase, despite the name, is
always the *keyed-reply* wait state for every mode, including the
second stage of `COMBINED`). `target` is present and populated during
`AWAIT_ANSWER` exactly like every other phase — a client that wants a
genuine comprehension exercise must choose not to render it as visible
text until the round resolves; the protocol does not hide it (same
precedent as `GameState.target`/`lastChar`, §6.3a).

For `LISTENING`, a round is entirely decided by one `answerTraining`
call: `correct`/`attempts` update immediately and `phase` moves to
`"FEEDBACK"`. For `COMBINED`, `answerTraining` only resolves the
identification half and moves `phase` to `"LISTENING"` — the operator
must then key the character back (the ordinary keyed-reply path every
mode uses); the round counts as correct only when both halves were.

### 6.3a GameState

Device-authoritative ear-training games (`COPY`/`MEMORY`/`SPEED`,
started with `startGame`, §4) — distinct from the client-owned Games
catalog, which remains out of scope for this protocol (§9).

```json
{
  "active": boolean, "game": "COPY|MEMORY|SPEED"?, "paused": boolean,
  "phase": string?, "highScore": integer,
  "target": string?, "score": integer?, "lives": integer?, "fallProgressPct": integer?,
  "chainLength": integer?, "inputProgress": integer?, "chain": string?,
  "combo": integer?, "beatRemainingMs": integer?, "lastChar": string?, "wasLastCorrect": boolean?,
  "wpm": integer?
}
```

`wpm` (since firmware v2.7.1) is the same base keyer speed as
`TrainingState.wpm` (§6.3) - present for every active game, all three
of which play their target character(s) at this speed.

Only the fields for the active `game` are meaningful:

- `COPY`: `target` (the falling character), `score`, `lives`,
  `fallProgressPct` (0-100). `phase`: `IDLE|FALLING|HIT|MISS|OVER`.
- `MEMORY`: `chainLength`, `inputProgress`, `chain` (the full echo-chain
  sequence — see the visibility note below).
  `phase`: `IDLE|PLAYBACK|INPUT|ROUND_OK|OVER`.
- `SPEED`: `combo`, `lives`, `beatRemainingMs`, `lastChar`,
  `wasLastCorrect`. `phase`: `IDLE|LISTEN|FEEDBACK|OVER`.

`target`/`chain`/`lastChar` are always present in the payload
regardless of phase, including mid-round while the character hasn't
been revealed on the device's own OLED yet — the same "field present,
client chooses not to render it" precedent as `TrainingState.target`
during `AWAIT_ANSWER` (§6.3). They exist so a client can synthesize
matching audio locally and show feedback after the round; withholding
them as visible text during the listening phase is a UI choice this
protocol doesn't enforce.

Input during these games is the same `keyDown`/`keyUp` (§4) used by
Training and the virtual straight key.

### 6.4 PairingEvent

```json
{
  "type": "PAIRING_STARTED|PAIRING_WAITING_FOR_PASSKEY|PAIRING_WAITING_FOR_CONFIRMATION|PAIRING_SUCCEEDED|PAIRING_FAILED|PAIRING_UNAVAILABLE|PAIRING_SYSTEM_PROMPT|PAIRING_CANCELLED",
  "devicePath": string?, "passkey": integer?, "errorCode": string?, "errorMessage": string?,
  "deviceAddress": string?, "attemptId": string?
}
```

- `PAIRING_WAITING_FOR_PASSKEY` **with** `passkey` present: the device
  is *displaying* a passkey the human should read and confirm matches
  on their phone/computer's own OS pairing prompt — informational only,
  don't show an entry field for this case.
- `PAIRING_WAITING_FOR_PASSKEY` **without** `passkey`: this app must
  supply the passkey — show an entry field, then call `submitPasskey`.
- `PAIRING_WAITING_FOR_CONFIRMATION`: show yes/no with `passkey`
  displayed for the human to compare against the device's own screen,
  then call `confirmPairing`.

### 6.5 BackendError

```json
{ "code": string, "message": string, "severity": "ERROR|WARNING|INFO",
  "recoverable": boolean, "operation": string? }
```

`operation` is the method name (§4) that triggered the failure, when
known (absent for spontaneous errors like a lost connection).

### 6.6 Capabilities

```json
{
  "connection": true, "pairing": boolean, "keyer": true, "training": true,
  "statistics": false, "connectivityManagement": true, "profiles": false,
  "settings": false, "diagnostics": false, "tools": false
}
```

`pairing` reflects whether `dbus-next` is actually importable on this
machine right now (checked live, not hardcoded by platform). This is custom
agent pairing; `getDeviceRuntime.usesSystemPairing` describes the OS fallback.
`connectivityManagement` is true in bridge 1.2.0 for host discovery/management. Every
`false` flag is a section with no BLE support yet — see
what those sections need once the firmware exposes them;
don't hide the section, disable it and say why.

### 6.7 DeviceInfo

```json
{
  "firmwareVersion": "2.7.1",
  "wpm": 20,
  "sidetoneHz": 600,
  "sidetoneEnabled": true,
  "volume": 80,
  "paddleReversed": false,
  "mode": "PADDLE",
  "iambicMode": "IAMBIC_B",
  "weightPercent": 50
}
```

The firmware's current live keyer configuration (`core_keyer.h`
getters — ble_control.cpp's `sendDeviceInfo()`), not a saved profile:
if the operator has tweaked a setting via the OLED menu since the last
profile load, this reflects that live value. Only populated after a
`requestDeviceInfo` round trip (§4) — there is no unprompted push, so a
fresh connection has no `DeviceInfo` until a client asks for one.
`metadata.deviceFirmwareVersion` (§4.1) is filled from this same round
trip's `firmwareVersion` field; it stays `null` until then.

---

## 7. Morse reference data (§6 of the Flutter integration request)

`getMorseTable` returns both lookup directions in one call, both exact
copies of the firmware's own `core_decoder.cpp` table (via
`protocol.py`, not re-derived or approximated):

```json
{
  "table":   { "A": ".-", "B": "-...", "...": "..." },
  "reverse": { ".-": "A", "-...": "B", "...": "..." }
}
```

`getKochSequence` returns the exact 40-character Koch unlock order from
`core_trainer.cpp`, as a single string, in order:

```json
{ "sequence": "KMURESNAPTLWI.JZ=FOY,VGQ5/H38B?47C1D6X92" }
```

Both are static for the lifetime of a connection (they don't change at
runtime) — fetch once and cache client-side.

---

## 8. Error codes

Same codes as `BACKEND_API.md` §8; the ones this server actually returns today:

| Code | When |
|---|---|
| `INVALID_COMMAND` | malformed JSON, or `method` missing from a request |
| `UNSUPPORTED_OPERATION` | unknown `method` name |
| `INVALID_PARAMETER` | a required param is missing/invalid (e.g. `startTraining` with no `mode`, or an unrecognized mode) |
| `DEVICE_NOT_FOUND` | `connect()`/`scan()` timed out |
| `CONNECTION_FAILED` | the GATT connection attempt raised |
| `DEVICE_BUSY` | a command was sent with no active connection |
| `TRAINING_START_FAILED` | the device's control channel reported an error |
| `PAIRING_UNAVAILABLE` | pairing attempted where `Capabilities.pairing` is `false` |
| `PAIRING_FAILED` | any step of the BlueZ pairing flow failed |
| `INTERNAL_ERROR` | an unexpected exception either in the backend or the server itself |

---

## 9. The client-owned Games catalog remains out of scope for this protocol

Per explicit requirement: Flutter owns its six client-side games
completely (mechanics, animation, input, targets, scoring, combo,
lives, timers, difficulty, progression, local high scores). The server
exposes nothing about *that* catalog — the only thing one of those
games needs from this protocol is `getMorseTable` (§7), used the same
way `arcade.py`'s Python games use `protocol.MORSE_TABLE` today.

This is separate from the three device-authoritative games
(`COPY`/`MEMORY`/`SPEED`, §4/§6.3a) — those run their scoring and
character selection on the device itself, exactly like Training, and
are very much in scope (`startGame`/`stopGame`/`gameStateChanged`
etc.). "Games are out of scope" here means specifically the client's
own arcade catalog, not every feature that happens to be called a
game.

---

## 10. Minimal Dart client sketch

Illustrative only — not a package, just enough to show the protocol in
use (`web_socket_channel` is the standard Dart/Flutter package for
this):

```dart
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';

class MorpheusClient {
  final WebSocketChannel _channel;
  int _nextId = 1;
  final _pending = <String, void Function(Map<String, dynamic>)>{};
  final _eventHandlers = <String, void Function(Map<String, dynamic>)>{};

  MorpheusClient(Uri uri) : _channel = WebSocketChannel.connect(uri) {
    _channel.stream.listen((raw) {
      final msg = jsonDecode(raw as String) as Map<String, dynamic>;
      if (msg['type'] == 'response') {
        _pending.remove(msg['id'].toString())?.call(msg);
      } else if (msg['type'] == 'event') {
        _eventHandlers[msg['event']]?.call(msg['data'] as Map<String, dynamic>);
      }
    });
  }

  void on(String event, void Function(Map<String, dynamic> data) handler) {
    _eventHandlers[event] = handler;
  }

  Future<Map<String, dynamic>> call(String method, [Map<String, dynamic>? params]) {
    final id = (_nextId++).toString();
    final completer = Completer<Map<String, dynamic>>();
    _pending[id] = (resp) => completer.complete(resp);
    _channel.sink.add(jsonEncode({'id': id, 'method': method, 'params': params ?? {}}));
    return completer.future;
  }
}

// Usage:
final client = MorpheusClient(Uri.parse('ws://127.0.0.1:8765'));
client.on('connectionChanged', (data) => print('connection: $data'));
client.on('keyerWordReceived', (data) => print('word: $data'));
await client.call('connect', {});
final snapshot = await client.call('getSnapshot');
```

---

## 11. What this server does not do

- No authentication/authorization — it's a loopback-only local service.
- No reconnection/retry logic for the WebSocket itself — that's the
  client's responsibility (same expectation as any local IPC socket).
- No persistence of subscription state across a reconnect — a client
  that reconnects starts with a fresh "subscribed to everything"
  session and should call `getSnapshot` again.


## Additive 2.8.2 game-input observation (2026-10-02)

Optional GATT characteristic `7a48a2b0-0005-4ad4-9f1a-1c2d3e4f5a6b`, READ/NOTIFY
with existing authenticated/encrypted access, emits:

```json
{"evt":"game_morse","game":"COPY","run":1,"seq":1,"char":"K","pattern":"-.-","timestamp":123456}
```

This observes finalized input to firmware COPY/MEMORY/SPEED. It does not change
normal word/live/pattern telemetry, commands, ACKs or authoritative game state.
The Python backend offers `on_game_morse`; WebSocket channel is
`gameMorseReceived` (included in default subscriptions). Mobile clients subscribe
to 0005 directly when discovered. Older firmware without this characteristic is
tolerated; older clients can continue using existing channels. NOTIFY is bounded
and best effort; run/sequence permit dedup and unobserved-position accounting.
No physical Morse decoding or firmware scoring is moved into Flutter.


## Additive UI-control support — firmware source2.8.3 (2026-10-03)

The optional game-input0005 channel from2.8.2 remains. `set_keyer` adds validated
existing core setter access, e.g. `{"cmd":"set_keyer","field":"wpm","value":20}`.
Fields/ranges: wpm5..40, tone200..2000, volume0..100, mode/reversed/iambic/sidetone0..1,
weight30..70. Reject unknown/bad integer values and TX/key/training/game busy states.
Accepted writes use existing debounced NVS and publish actual DeviceInfo; settings
version9 is unchanged. WS method `setKeyerSetting` maps to backend
`set_keyer_setting(field,value)`. Updated clients reconcile0004 and await actual
matching info; command dispatch alone is not device success.

`startTraining`/backend `start_training` optionally accepts `kochLevel:2..40`,
forwarded as `{"cmd":"train_start","mode":"LISTENING","kochLevel":10}`. KOCH
selects the existing level; LISTENING/COMBINED select that Koch prefix. Omitted
argument keeps original mode behavior/full pool. Validate before stopping games.
These are necessary for the new real keyer/lesson controls, not a trainer/decoder
rewrite. Older firmware supports baseline commands but cannot provide configured
lessons or setter confirmation; clients gate2.8.3. Local Farnsworth remains local.


