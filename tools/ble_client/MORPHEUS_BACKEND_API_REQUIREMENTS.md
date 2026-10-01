# MORPHEUS BLE Client — Backend/API Requirements

## 1. Purpose

This document defines the backend-facing data and command contract required by the MORPHEUS client application.

It is intentionally transport-agnostic. The existing backend may expose these operations through any suitable mechanism (local API, IPC, native bindings, WebSocket, RPC, etc.). The contract below defines **what data and operations must be available**, not how they are transported.

The requirements are derived from the MORPHEUS functional specification.

---

## 2. Scope

The backend contract currently needs to provide:

1. BLE connection/discovery state and connection commands.
2. Live CW keyer telemetry.
3. Virtual key down/up commands.
4. Training session control and live training state.
5. Structured asynchronous events/errors.
6. Feature/capability availability so unsupported device-side functionality can be identified explicitly.
7. Application/device version information where available.

The following areas do **not** require a backend implementation at this stage because they are currently client-local/static or not exposed by the device over BLE:

- Client-local games and game state.
- Client-local game high scores.
- Client-derived keyer session counters/history.
- Client-derived training accuracy/incorrect counts.
- Device statistics, until the device exposes them remotely.
- Device profiles, until exposed remotely.
- Device settings, until exposed remotely.
- Device diagnostics, until exposed remotely.
- Device tools, until defined/exposed remotely.
- Static Help content, except application version/documentation metadata if supplied by the backend.

---

## 3. General Contract Requirements

### 3.1 Transport independence

The contract must not depend on a specific UI framework or presentation technology.

### 3.2 Asynchronous operation

BLE discovery, connection, notifications, training state changes, and errors must be representable as asynchronous events/streams rather than requiring polling only.

### 3.3 Stable identifiers

Where applicable, device IDs, service IDs, characteristic IDs, command IDs, and error codes should be stable across releases.

### 3.4 Structured data

Prefer typed/structured fields over free-form strings. Human-readable messages may be included in addition to stable machine-readable codes.

### 3.5 Backward compatibility

Unknown fields should be safely ignorable by clients. New enum values should be handled as unknown/unsupported rather than causing protocol failure.

---

# 4. Connection and Device Discovery

## 4.1 Connection state

Expose the current connection state using the following values:

```text
SCANNING
CONNECTING
CONNECTED
DISCONNECTED
NOT_FOUND
ERROR
```

### ConnectionState

```text
state: ConnectionState
```

When `state == CONNECTED`, the following fields should be available:

```text
deviceName: string
deviceAddress: string
```

`deviceAddress` may be a BLE MAC address, platform UUID, or equivalent stable platform-specific identifier.

An optional status/error message may be supplied for any state:

```text
statusMessage: string?
```

Examples:

```text
MORPHEUS-CW not found in 10s
Connection failed
Disconnected by remote device
```

## 4.2 Required connection operations

```text
scan()

connect(deviceAddress?)

disconnect()
```

### `scan()`

Starts device discovery.

Requirements:

- Must transition through an appropriate discovery state.
- Must be able to report `NOT_FOUND` after the discovery timeout.
- Must not block the calling thread while discovery is in progress.

### `connect(deviceAddress?)`

Starts a scan-then-connect sequence when the address is omitted.

When an address is supplied, the backend may connect directly if the platform/protocol permits it.

The reference behavior specifies an approximately 10-second discovery timeout for auto-discovery.

### `disconnect()`

Terminates the current connection and emits a corresponding connection-state event.

---

# 5. Linux Pairing

The functional specification requires an in-process pairing flow on Linux when supported by the installed backend/platform capability.

The backend must expose capability information indicating whether this operation is available.

## 5.1 Pairing operations

```text
startPairing()
```

The pairing flow must be able to report one of the following input requirements:

```text
PASSKEY
CONFIRMATION
NONE
```

### Passkey input

For passkey-based pairing:

```text
submitPasskey(passkey: string)
```

The expected passkey is six numeric digits.

### Confirmation input

For confirmation-based pairing:

```text
confirmPairing(accepted: boolean)
```

The backend must report the value that requires confirmation, for example the numeric value displayed by the device, where supported.

## 5.2 Pairing result

Expose structured pairing events:

```text
PAIRING_STARTED
PAIRING_WAITING_FOR_PASSKEY
PAIRING_WAITING_FOR_CONFIRMATION
PAIRING_SUCCEEDED
PAIRING_FAILED
PAIRING_UNAVAILABLE
```

For failure:

```text
errorCode: string
errorMessage: string
```

On platforms where the backend cannot provide in-process pairing, capability reporting must indicate that the pairing operation is unavailable.

---

# 6. CW Keyer Telemetry

The backend must publish an event every time a keyed word is completed by the device.

## 6.1 KeyerWordEvent

```text
word: string
wpm: integer
mode: KeyingMode
timestamp: integer
```

### Constraints

`word`:

- Decoded text.
- Supported characters are `A-Z`, `0-9`, `. , ? / = + -`.

`wpm`:

```text
5..40
```

`mode`:

```text
STRAIGHT
PADDLE
```

`timestamp`:

- Device uptime in milliseconds.
- This is **not wall-clock time**.
- The device has no RTC according to the functional specification.

## 6.2 Keyer event delivery

The backend should expose a continuously consumable event stream:

```text
keyerWordReceived -> KeyerWordEvent
```

Events must preserve order.

---

# 7. Virtual Straight Key Commands

The backend must provide separate key-down and key-up operations.

```text
keyDown()
keyUp()
```

Requirements:

- `keyDown()` sends the equivalent key-down event to the device.
- `keyUp()` sends the corresponding key-up event.
- The implementation must preserve press/hold duration accurately enough for device-side dit/dah classification.
- A key event must have no effect while the physical device key is already down, according to device behavior.
- The backend should not synthesize a complete character/word from these operations; the device remains authoritative for key classification and decoding.

The same operations are used for training input.

---

# 8. Training

## 8.1 Training modes

Expose the following exact values:

```text
KOCH
CHARACTERS
WORDS
CALLSIGNS
ADAPTIVE
EXAM
LISTENING
COMBINED
```

`LISTENING` is pure comprehension: the device plays a character and the
round is decided entirely by `answerTraining()` (§8.2b) - no keying is
involved. `COMBINED` requires both: an `answerTraining()` call first
(identification), which gates into a keying stage using the same
`keyDown()`/`keyUp()` path every other mode uses; the round only counts
as correct when both stages are.

## 8.2 Training start/stop/control operations

```text
startTraining(mode: TrainingMode)
stopTraining()
confirmTraining()
```

The backend must reject an invalid/unsupported mode with a structured error.

## 8.2b Training identification answer (LISTENING/COMBINED only)

```text
answerTraining(text: string)
```

Only meaningful while `phase == "AWAIT_ANSWER"` (§8.3); a call at any
other time is a silent no-op on the device side. `text` must be exactly
one character to ever match - this mirrors the device's own targets for
these two modes, which are always single characters.

## 8.3 Training live state

Publish the complete current session state whenever a relevant value changes.

### TrainingState

```text
active: boolean
mode: TrainingMode
phase: string/enum
target: string
correct: integer
attempts: integer
kochLevel: integer?
adaptiveWpm: integer?
wpm: integer?
examScorePercent: integer?
examPassed: boolean?
examCorrectCount: integer?
examTotalCount: integer?
```

### Field applicability

`active`, `mode`, `phase`, `target`, `correct`, and `attempts` apply to all modes.

`kochLevel` applies to `KOCH`.

`adaptiveWpm` applies to `ADAPTIVE`.

`wpm` (since firmware v2.7.1) applies to every mode - the base keyer
speed (`core_keyer_getWpm()`) that every mode except `ADAPTIVE`
actually plays `target` at. Lets a client synthesize its own listening
audio matched to the device's real speed instead of assuming a fixed
one.

The following apply when an exam has finished:

```text
examScorePercent
examPassed
examCorrectCount
examTotalCount
```

For an exam, `examTotalCount` is expected to be `25`.

## 8.4 Training event delivery

Expose a stream/event such as:

```text
trainingStateChanged -> TrainingState
```

Events must be ordered.

## 8.5 Shared key input

Training uses the same key operations as the virtual straight key:

```text
keyDown()
keyUp()
```

## 8.6 Device games (on-device ear training: COPY/MEMORY/SPEED)

Three additional, device-authoritative sessions, mutually exclusive
with Training (starting one while the other is active stops it first -
same single-decoder-consumer rule as Training itself). Distinct from
any client-owned/client-rendered game catalog, which remains out of
scope (§18) - these three run their scoring and character selection on
the device, exactly like Training does.

```text
GameId: COPY | MEMORY | SPEED
startGame(game: GameId)
stopGame()
pauseGame()
confirmGame()   // restarts the game once it's over
restartGame()   // restarts the game immediately, any time
```

### GameState

```text
active: boolean
game: GameId?
paused: boolean
phase: string        // per-game enum, see below
highScore: integer
wpm: integer?        // since firmware v2.7.1 - same base keyer speed as
                      // TrainingState.wpm, present for all three games
```

Per-game fields, present only for the matching `game`:

- `COPY`: `target` (string, the falling character), `score` (integer),
  `lives` (integer), `fallProgressPct` (integer, 0-100).
  `phase`: `IDLE | FALLING | HIT | MISS | OVER`.
- `MEMORY`: `chainLength` (integer), `inputProgress` (integer), `chain`
  (string, the full echo-chain sequence so a client can play matching
  audio locally - see the field-applicability note below).
  `phase`: `IDLE | PLAYBACK | INPUT | ROUND_OK | OVER`.
- `SPEED`: `combo` (integer), `lives` (integer), `beatRemainingMs`
  (integer), `lastChar` (string), `wasLastCorrect` (boolean).
  `phase`: `IDLE | LISTEN | FEEDBACK | OVER`.

`target`/`chain`/`lastChar` are always present in the wire payload
regardless of phase (including while the round is still "live" and
unresolved) - same precedent as Training's `target` field. A UI must
choose not to render them as visible text during the listening phase
if it wants a genuine comprehension exercise; the field being present
is for audio synthesis and post-round feedback, not an instruction to
display it early.

Input during these games is the same `keyDown()`/`keyUp()` virtual key
used by Training and the straight key.

The backend must associate these key events with the currently active training session when one exists.

---

# 9. Reference Data

The specification requires two static reference tables.

These do not have to be runtime BLE data; they may be bundled into the application. The backend only needs to supply them if it is intended to be the authoritative source.

## 9.1 Koch character order

Exact 40-character order:

```text
K M U R E S N A P T L W I . J Z = F O Y , V G Q 5 / H 3 8 B ? 4 7 C 1 D 6 X 9 2
```

## 9.2 Morse code table

Characters supported by the functional specification:

```text
A-Z
0-9
. , ? / = + -
```

If the backend exposes this table, it should be immutable/reference data and use a simple mapping:

```text
character: string
pattern: string   // sequence containing only '.' and '-'
```

---

# 10. Capability / Feature Availability

The backend must expose machine-readable capability information so clients can determine which functions are currently supported by the connected platform/device/backend.

Recommended structure:

```text
Capabilities
  connection: boolean
  pairing: boolean
  keyer: boolean
  training: boolean
  statistics: boolean
  connectivityManagement: boolean
  profiles: boolean
  settings: boolean
  diagnostics: boolean
  tools: boolean
```

Additional capability flags may be added as individual operations become available.

The important requirement is that unsupported functionality is explicitly distinguishable from a runtime failure.

Example:

```json
{
  "statistics": false,
  "profiles": false,
  "settings": false,
  "diagnostics": false
}
```

A capability of `false` means the function is not currently available through this backend/device path; it must not be interpreted as a transient connection error.

---

# 11. Error Model

All backend operations that can fail should return/emit a structured error.

Recommended schema:

```text
BackendError
  code: string
  message: string
  severity: ERROR | WARNING | INFO
  recoverable: boolean
  operation: string?
```

## 11.1 Recommended stable error codes

At minimum, support the following categories:

```text
DEVICE_NOT_FOUND
CONNECTION_FAILED
CONNECTION_LOST
CONNECTION_TIMEOUT
PAIRING_UNAVAILABLE
PAIRING_FAILED
INVALID_COMMAND
INVALID_PARAMETER
UNSUPPORTED_OPERATION
DEVICE_BUSY
TRAINING_NOT_ACTIVE
TRAINING_START_FAILED
KEYER_COMMAND_FAILED
BACKEND_UNAVAILABLE
INTERNAL_ERROR
```

Additional device-specific error codes may be added.

## 11.2 Error events

Expose asynchronous errors through an event stream:

```text
backendError -> BackendError
```

An asynchronous error event must not require the caller to poll for failures.

---

# 12. Application / Device Metadata

Expose the following values where available:

```text
applicationVersion: string?
deviceName: string?
deviceFirmwareVersion: string?
```

The device firmware version is read-only metadata.

---

# 13. Event Summary

The backend should expose the following asynchronous event categories:

```text
connectionChanged(ConnectionState)

pairingStateChanged(PairingState)

keyerWordReceived(KeyerWordEvent)

trainingStateChanged(TrainingState)

backendError(BackendError)
```

The event mechanism may be implemented using callbacks, streams, observables, messages, subscriptions, or another equivalent mechanism.

---

# 14. Command Summary

Required commands:

```text
scan()
connect(deviceAddress?)
disconnect()

startPairing()
submitPasskey(passkey)
confirmPairing(accepted)

keyDown()
keyUp()

startTraining(mode)
stopTraining()
confirmTraining()
```

Optional/reference-data operations:

```text
getKochSequence()
getMorseTable()
```

---

# 15. State Consistency Requirements

The backend must maintain a single authoritative state for the active device connection and active training session.

Requirements:

1. Connection events must be emitted whenever connection state changes.
2. Keyer events must be emitted in device order.
3. Training state must represent the latest authoritative device state.
4. Commands must not silently fail.
5. Unsupported operations must return `UNSUPPORTED_OPERATION` or an equivalent explicit capability/operation error.
6. Device disconnection must terminate or invalidate any active operation as appropriate and emit a connection-state change.
7. Reconnection must result in a fresh authoritative state being published.

---

# 16. Minimum Backend Deliverable

The backend team can consider the contract complete for the current application when the following are available:

### Connection

- Connection state stream.
- Scan.
- Connect.
- Disconnect.
- Device name/address when connected.

### Keyer

- Completed-word event stream.
- Key-down command.
- Key-up command.

### Training

- Training state stream.
- Start.
- Stop.
- Confirm.
- Shared key-down/key-up commands.

### Cross-cutting

- Capability information.
- Structured error model.
- Asynchronous error events.
- Application/device version metadata where available.

No implementation of the client-local games is required from the backend.

---

# 17. Example End-to-End Event Sequence

## Connection

```text
scan()
  -> SCANNING
  -> CONNECTING
  -> CONNECTED
     deviceName = "MORPHEUS-CW"
     deviceAddress = "<platform identifier>"
```

## Keyer

```text
keyDown()
keyUp()

...device decodes word...

keyerWordReceived
{
  "word": "CQ",
  "wpm": 18,
  "mode": "STRAIGHT",
  "timestamp": 1234567
}
```

## Training

```text
startTraining(KOCH)

trainingStateChanged
{
  "active": true,
  "mode": "KOCH",
  "phase": "LISTENING",
  "target": "K",
  "correct": 0,
  "attempts": 0,
  "kochLevel": 2
}

keyDown()
keyUp()

trainingStateChanged
{
  "active": true,
  "mode": "KOCH",
  "phase": "LISTENING",
  "target": "K",
  "correct": 1,
  "attempts": 1,
  "kochLevel": 2
}
```

---

# 18. Out of Scope for This Backend Contract

The following are explicitly outside the current backend requirement unless a future device/backend revision exposes them:

- Rendering/presentation decisions.
- Layout or navigation.
- Visual assets.
- Icons/logos/emblems.
- Animation.
- Game rendering.
- Client-side game mechanics.
- Client-side game high scores.
- Client-derived keyer history/counts.
- Client-derived training accuracy.
- Device statistics retrieval.
- Device profile management.
- Device settings management.
- Device diagnostics.
- Device tools.

Future backend capabilities may be added without changing the existing contract, provided they follow the same principles of stable typed data, explicit capabilities, asynchronous events, and structured errors.
