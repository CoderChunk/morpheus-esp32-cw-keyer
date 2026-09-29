# MORPHEUS Mobile BLE Protocol — GATT-Level Reference

The exact wire-level protocol the native Dart transport driver
(`NativeBleMorpheusClient`, see `MOBILE_ARCHITECTURE.md`) must
implement using `flutter_reactive_ble` on Android/iOS. Every value
below is taken directly from `firmware/MORPHEUS/config.h` and
`transport.cpp`/`ble_control.cpp` — nothing here is inferred or
approximated. This is the same device protocol `backend.py` and
`ws_server.py` already implement for desktop; this document exists so
the mobile driver reproduces it byte-for-byte instead of creating a
second interpretation.

**Logical contract stays identical.** This document is the wire
encoding *underneath* `WS_PROTOCOL.md`'s method/event/data contract.
The mobile driver's job is: encode a `WS_PROTOCOL.md` command into the
exact bytes below, write them; and decode the exact bytes below back
into the exact data shapes `WS_PROTOCOL.md` §6 defines. §5 of this
document flags one field-renaming rule that's easy to miss and would
silently break that parity if skipped.

---

## 1. Service and characteristics

| Name | UUID | Properties |
|---|---|---|
| MORPHEUS Service | `7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b` | — |
| Word/telemetry characteristic | `7a48a2b0-0002-4ad4-9f1a-1c2d3e4f5a6b` | `READ`, `NOTIFY` (encrypted + authenticated) |
| Control-command characteristic | `7a48a2b0-0003-4ad4-9f1a-1c2d3e4f5a6b` | `WRITE` (encrypted + authenticated) |
| Control-event characteristic | `7a48a2b0-0004-4ad4-9f1a-1c2d3e4f5a6b` | `READ`, `NOTIFY` (encrypted + authenticated) |

Exact NimBLE property flags as declared in `transport.cpp`:

```
Word char:    READ | NOTIFY | READ_ENC | READ_AUTHEN
Cmd char:     WRITE | WRITE_ENC | WRITE_AUTHEN
Evt char:     READ | NOTIFY | READ_ENC | READ_AUTHEN
```

- **The control-command characteristic has no `NOTIFY` or `READ`** —
  it is write-only. Command outcomes (acks/errors) arrive as
  notifications on the *control-event* characteristic, not as a
  response to the write itself.
- **`_ENC`/`_AUTHEN` on every characteristic** means none of the three
  are accessible at all until the link is bonded, encrypted, and
  authenticated (see §3) — attempting any GATT operation before that
  fails with an ATT "insufficient authentication"/"insufficient
  encryption" error, not a protocol-level error message.

## 2. Write mode

The command characteristic's property is `WRITE` (not `WRITE_NR` /
"Write Without Response"). **Use Write With Response** —
`flutter_reactive_ble`'s `writeCharacteristicWithResponse`, not
`writeCharacteristicWithoutResponse`. This matches what the existing
desktop client already does (`bleak`'s `write_gatt_char(..., response=
True)`), and gives the driver a signal that the write was accepted at
the link layer (though not that the command was semantically valid —
see §6 for how validity is actually reported).

## 3. Connection, bonding, and security requirements

- `NimBLEDevice::setSecurityAuth(true, true, true)` — **bonding
  required, MITM protection required, LE Secure Connections
  required.**
- `NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY)` — the
  device is IO-capability "Display Only": it shows a 6-digit passkey
  on its own screen; the peer must enter or confirm it. It never
  accepts a peer-displayed passkey.
- **Practical effect:** all three characteristics are unusable until
  bonding completes. On mobile this happens automatically at the OS
  level the first time the app touches one of them post-connection —
  see `MOBILE_ARCHITECTURE.md`'s pairing table (Android: system bonding
  dialog / `createBond`; iOS: Core Bluetooth bonds transparently). The
  Dart driver does not need to implement any pairing UI of its own —
  that's the entire reason `pairing_backend.py`'s custom BlueZ Agent1
  dialog exists only for desktop Linux, which lacks this OS-level UX.
- Once bonded, the device remembers up to 3 trusted peers
  (`BLE_TRUSTED_DEVICE_CAP = 3`) and will reconnect to any of them
  without re-pairing. A 4th never-paired device cannot bond until a
  slot is freed (device-side "Bond Reset" menu action) — this is
  existing multi-device pairing behavior, unchanged for mobile.

## 4. MTU — request 247 immediately after connecting, before anything else

`config.h` sets `BLE_REQUESTED_MTU = 247`. The firmware negotiates
down to whatever the peer actually supports, but **payload size is
gated hard by whatever MTU is actually negotiated**, and the failure
mode is silent (not an error event — just nothing sent):

- **Word telemetry**: available payload = `negotiated_mtu - 3 - 2`
  bytes, then a further `- 64` (protocol overhead budget), capped at
  144 bytes (`BLE_WORD_FIELD_CAP(24) * 6`, the worst-case fully-escaped
  word). At the BLE default MTU of 23 (i.e. if the driver never
  requests a larger one), available payload is *negative* — the
  firmware detects this and **skips sending the notification
  entirely**, with no error surfaced to the client. Word telemetry
  will appear to simply never arrive.
- **Control events** (`train_state`, `ack`, `error`): sent only if
  `len(json) <= negotiated_mtu - 3` **and** `len(json) < 220`
  (`BLE_CONTROL_EVT_CAP`). At MTU 23 that's 20 usable bytes — far
  short of even the shortest `train_state` payload — so **training
  will appear completely unresponsive** (commands accepted, but no
  state ever comes back) if MTU negotiation is skipped.

**Action required:** call `flutter_reactive_ble`'s MTU-negotiation API
(`requestMtu` on the connection) for **247** immediately after
connecting, before subscribing to notifications or sending any
command. If the platform/adapter can't grant 247, request the largest
it can — anything under roughly 223 will start silently dropping
`train_state` events, and anything at the BLE default of 23 will drop
essentially everything.

## 5. Encoding rules

- All payloads are **UTF-8 text, compact JSON (no extra whitespace)**,
  written as raw bytes with **no null terminator on the wire** (the
  firmware's `setValue()` call sends exactly `strlen(json)` bytes).
- The command parser (`jsonGetString` in `ble_control.cpp`) tolerates
  an optional single space or tab immediately after a key's colon, but
  **always emit compact JSON with no whitespace** to match exactly what
  the desktop Python client already sends and is tested against
  (`json.dumps(command, separators=(",", ":"))`) — don't rely on the
  tolerance.
- The word-telemetry `word` field is escaped like a JSON string value
  by the firmware itself (quotes/backslashes as `\"`/`\\`, control
  characters `< 0x20` as `\u00XX`) — decode it as one normal JSON
  string field, no special-casing needed on the client side.
- **Field-name rename to preserve logical-contract parity:** the
  firmware's wire JSON for `train_state` uses the keys `examCorrect`
  and `examTotal`. The existing desktop backend (`backend.py`'s
  `_on_control_notify`) renames these to `examCorrectCount` and
  `examTotalCount` when building the logical `TrainingState` object
  that `WS_PROTOCOL.md` §6.3 documents. **The mobile driver must apply
  this same rename** — parsing the raw wire keys directly into a
  `TrainingState` without renaming them would silently produce a
  different field-name contract than desktop and break requirement 3
  (keep the logical contract identical) without any error ever
  appearing.

## 6. Command payloads (write to the control-command characteristic)

Only these five commands are relevant to the mobile client. `game_*`
commands exist in the firmware's vocabulary but are **out of scope for
mobile** — Flutter implements all six games entirely client-side (per
requirement 4), never talking to the device's own 3-game protocol, the
same as the existing desktop app.

| Command | Exact bytes | Gets an ack? |
|---|---|---|
| Key down | `{"cmd":"key_down"}` | **No** |
| Key up | `{"cmd":"key_up"}` | **No** |
| Start training | `{"cmd":"train_start","mode":"KOCH"}` (mode ∈ `KOCH`\|`CHARACTERS`\|`WORDS`\|`CALLSIGNS`\|`ADAPTIVE`\|`EXAM`) | Yes |
| Stop training | `{"cmd":"train_stop"}` | Yes |
| Confirm training | `{"cmd":"train_confirm"}` | Yes |

`key_down`/`key_up` are **deliberately silent** — no ack, no error, no
response of any kind at the protocol level, matching a real physical
key (which also doesn't "acknowledge" a keystroke). Do not wait for a
response after writing them.

`train_start`/`train_stop`/`train_confirm` **do** produce an
`{"evt":"ack",...}` notification (§7), but the existing desktop backend
deliberately does not surface acks into its logical event contract
(`WS_PROTOCOL.md` has no `ack` channel) — the real signal that a
command succeeded is the next `train_state` notification. The mobile
driver should mirror this: parse and discard `ack` notifications (or
log them for diagnostics) rather than exposing them as a new event type
that doesn't exist in `WS_PROTOCOL.md`.

### Server-side validation the client should expect

- Sending `train_start` while a mode string doesn't match one of the
  six values → `{"evt":"error","message":"bad or missing mode"}`.
- Any unrecognized `cmd` value → `{"evt":"error","message":"unknown cmd"}`.
- A payload with no `"cmd"` field at all → `{"evt":"error","message":"missing cmd"}`.

### Key-down/key-up semantics (unchanged from desktop, restated for the mobile driver)

- `key_down` is ignored (no-op, not an error) if a real physical key is
  already down, or if a virtual key-down is already pending — there is
  no queuing.
- `key_up` is ignored if no virtual key-down is currently pending.
- The hold duration between `key_down` and `key_up` is classified
  DIT/DAH device-side, relative to the device's **current** dit length
  (which depends on its currently configured WPM) times a threshold
  multiplier of `2.0` — the mobile driver has no way to know the exact
  millisecond threshold in advance and doesn't need to; just hold and
  release like a real straight key.

## 7. Notification payloads (control-event characteristic)

Four distinct `"evt"` values can arrive on this characteristic. Only
`train_state` and `error` are part of the logical contract; `ack` and
`game_state` should be parsed (to avoid choking on unrecognized JSON)
but not surfaced as new event types.

```json
{"evt":"train_state","active":true,"mode":"KOCH","phase":"LISTENING","target":"K","kochLevel":2,"correct":0,"attempts":0,"adaptiveWpm":18,"examScorePercent":0,"examPassed":false,"examCorrect":0,"examTotal":0}
{"evt":"train_state","active":false}
{"evt":"ack","cmd":"train_start","ok":true}
{"evt":"error","message":"bad or missing mode"}
{"evt":"game_state", ...}
```

- When `active` is `false`, **only** `active` is present — every other
  field is simply absent (not `null`), matching `WS_PROTOCOL.md` §6.3's
  optional fields.
- Remember the `examCorrect`→`examCorrectCount` /
  `examTotal`→`examTotalCount` rename from §5.
- `game_state` notifications can arrive if something else somehow
  triggers the device's own game protocol (there is no code path for
  this app to do so, but the field exists in the firmware) — ignore
  them; they carry no meaning for this client.

### Error → logical error-code mapping

The firmware has exactly one generic error event shape
(`{"evt":"error","message":"..."}`) with no machine-readable code of
its own. The existing desktop backend maps **every** control-channel
error to `TRAINING_START_FAILED` regardless of which command actually
triggered it (`backend.py`'s `_on_control_notify`) — this is an
existing simplification, not something to "fix" independently on
mobile, since diverging here would itself be a second interpretation.
Mirror it exactly: any `{"evt":"error",...}` → `BackendError(code:
"TRAINING_START_FAILED", message: <the message field>)`.

### Word telemetry (word/telemetry characteristic)

```json
{"word":"CQ","wpm":18,"mode":"STRAIGHT","timestamp":1234567}
```

No `"evt"` wrapper on this characteristic — it only ever carries this
one shape. `mode` is exactly `"STRAIGHT"` or `"PADDLE"`. `timestamp` is
device uptime in milliseconds, not wall-clock time (no RTC on-device).

## 8. Notification subscription

Both the word-telemetry and control-event characteristics require the
client to enable notifications (write the CCCD, `flutter_reactive_ble`
does this via `subscribeToCharacteristic`) before any data will arrive.
The firmware does not treat "notifications not enabled" as an error —
it simply never calls `notify()`'s underlying transmission for a peer
that hasn't subscribed, so an un-subscribed client will see nothing at
all, including in response to commands it sends. **Subscribe to both
characteristics before sending any command**, not after.

## 9. Timing / ordering requirements

1. **Connect → bond (if not already bonded) → request MTU 247 → subscribe to both notify characteristics → only then send commands.** Every step before it is a silent-failure mode for the step after it if skipped (§3, §4, §8).
2. `key_down` must precede a matching `key_up` — there is no
   independent "tap" command.
3. `train_state` pushes are **rate-limited to at most once per 150 ms**,
   and only sent when the content actually changed since the last push
   — the device does not spam identical state on every internal tick.
   A client that expects a push on every single keystroke will
   sometimes see nothing change for up to ~150 ms even during active
   input; this is expected device behavior, not a dropped notification.
4. `train_start` server-side stops any active device-side game session
   first (mutual exclusivity the firmware enforces) — not reachable
   from this app in practice since mobile never sends `game_start`, but
   documented for completeness in case another BLE peer (e.g. the
   desktop app) is somehow connected to the same device session-adjacent
   history; only one BLE central can hold the connection at a time
   regardless.

## 10. Summary table for implementation

| Concern | Value |
|---|---|
| Service UUID | `7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b` |
| Word char UUID | `7a48a2b0-0002-4ad4-9f1a-1c2d3e4f5a6b` |
| Cmd char UUID | `7a48a2b0-0003-4ad4-9f1a-1c2d3e4f5a6b` |
| Evt char UUID | `7a48a2b0-0004-4ad4-9f1a-1c2d3e4f5a6b` |
| Write type | Write With Response |
| Security | Bonded + MITM + LE Secure Connections, IO cap Display-Only |
| MTU to request | 247 (immediately post-connect, before subscribe/write) |
| Encoding | UTF-8, compact JSON, no null terminator |
| Commands used by mobile | `key_down`, `key_up`, `train_start`, `train_stop`, `train_confirm` only |
| Commands NOT used by mobile | `game_start`/`game_stop`/`game_pause`/`game_confirm`/`game_restart` (games are entirely client-side) |
| Field rename required | wire `examCorrect`/`examTotal` → logical `examCorrectCount`/`examTotalCount` |
| Error code mapping | any control-channel error → `TRAINING_START_FAILED` |
