# Mobile BLE Validation Checklist (Android/iOS, real hardware)

A concrete test plan for validating `NativeBleMorpheusClient`
(`flutter_reactive_ble`) against real MORPHEUS hardware, once the
Flutter app exists. Each item has setup, steps, an expected result, and
the specific failure mode to watch for — most of these failure modes
are things `MOBILE_BLE_PROTOCOL.md` calls out as *silent* (no error
message, just missing behavior), so "nothing obviously broke" is not
sufficient to pass a step; check the specific expected result.

**This checklist has not been executed.** It was written from the spec
in `MOBILE_BLE_PROTOCOL.md`, not run against hardware — there is no
Flutter build and no mobile device/toolchain available in the
environment that produced these docs. Run every item below on both
Android and iOS before treating the mobile BLE contract as validated,
not just specified.

Prerequisites for all items: MORPHEUS flashed with current firmware,
BLE toggle on (Menu → Connectivity → Bluetooth → BLE → ON), at least
one Android device and one iOS device, both never-before-paired with
this specific MORPHEUS unit (or bond reset first — see below) so the
pairing flow in item 2 is a genuine first-time test.

---

## 1. BLE discovery/connection

- **Setup:** MORPHEUS advertising, out of range of any other already-
  connected client.
- **Steps:** Launch the app, trigger a scan/connect by device name.
- **Expected:** Device found within `SCAN_TIMEOUT_S` (10s, per
  `protocol.py`); connection reaches a stable `CONNECTED` state;
  `deviceName`/`deviceAddress` populate correctly.
- **Also test:** connecting when MORPHEUS is already connected to
  another client (e.g. the desktop app) — should fail/timeout cleanly,
  not hang indefinitely or crash (only one BLE central at a time).
- **Failure mode to watch for:** connection reported successful before
  service/characteristic discovery has actually completed, leading to
  "characteristic not found" errors on the very next step.

## 2. Bonding/authentication

- **Setup:** A MORPHEUS unit with a free trusted-device slot (`Paired:
  N/3`, checked via Menu → Diagnostics → BLE Status), and a phone that
  has never paired with it.
- **Steps:** Connect, then attempt any operation on a protected
  characteristic (subscribe to notifications, or write a command).
- **Expected:** OS-level pairing UI appears automatically (Android:
  system bonding dialog; iOS: Core Bluetooth passkey prompt) — no
  custom in-app pairing screen should be needed, per
  `MOBILE_ARCHITECTURE.md`. The 6-digit code shown must match what
  MORPHEUS's own OLED displays. After confirming, the characteristic
  operation succeeds.
- **Also test:** reconnecting later with an *already-bonded* device —
  should connect and access characteristics with **no** pairing prompt
  at all.
- **Also test:** what happens when all 3 trusted-device slots are
  full and a 4th new device tries to bond — should fail cleanly (this
  is expected device behavior, not a client bug; see `Bond Reset` in
  `docs/USER_MANUAL.md` §9).
- **Failure mode to watch for:** the app appearing to "hang" on a
  characteristic write/subscribe with no dialog ever appearing —
  usually means the plugin attempted the GATT operation before the OS
  pairing flow had a chance to trigger, or bonding silently failed.

## 3. MTU negotiation

- **Setup:** Freshly bonded connection.
- **Steps (do both):**
  1. Connect, **explicitly request MTU 247** immediately after
     connecting (before subscribing to anything), confirm the
     negotiated value the plugin reports back.
  2. As a **negative test**, connect and *skip* the MTU request
     entirely, then try to receive a `train_state` notification with a
     non-trivial payload (start Koch training, key an answer).
- **Expected:** (1) negotiates to 247, or as close as the platform BLE
  stack allows; commands and notifications all work normally. (2)
  **This is expected to fail** — `MOBILE_BLE_PROTOCOL.md` §4 states
  the firmware silently drops `train_state` notifications when
  `negotiated_mtu - 3 < len(json)`, which happens at the BLE default
  MTU of 23. Confirming (2) actually reproduces this on real Android
  and real iOS BLE stacks (not just in the spec) is the actual point
  of this test — OEM Bluetooth stack quirks are a real-world risk this
  checklist exists specifically to catch.
- **Failure mode to watch for:** MTU request silently ignored by the
  plugin/OS (reports success but negotiates a smaller value than
  requested) — check the *actual* negotiated MTU the plugin reports,
  don't just check that the request call didn't throw.

## 4. Key down/up

- **Setup:** Bonded, connected, MTU negotiated.
- **Steps:** Send `key_down`, wait ~200ms, send `key_up`. Repeat for a
  short hold (dit) and a long hold (dah) relative to the device's
  current WPM.
- **Expected:** No response of any kind at the protocol level (this is
  correct — see `MOBILE_BLE_PROTOCOL.md` §6, no ack for these two
  commands). Verify success indirectly: the device's own OLED should
  show the keyed element, and it should eventually surface in a
  `KeyerWordEvent` once a full word completes (item 5).
- **Also test:** sending `key_down` twice without an intervening
  `key_up` — should be a no-op the second time, not an error and not
  two elements.
- **Also test:** sending `key_up` with no prior `key_down` — should be
  a silent no-op.
- **Failure mode to watch for:** the driver waiting for a response
  after `key_down`/`key_up` that never comes (a bug in the driver, not
  the device) — these two commands must be genuinely fire-and-forget.

## 5. Keyer telemetry

- **Setup:** Bonded, connected, MTU negotiated, subscribed to the
  word-telemetry characteristic (§8 — subscription must happen before
  keying, or the notification is simply never sent).
- **Steps:** Key a short word on the physical device (or via
  `key_down`/`key_up` sequences from the app) and let it complete
  (pause past the word gap).
- **Expected:** A `KeyerWordEvent` arrives with the correct `word`,
  `wpm`, `mode` (`STRAIGHT` or `PADDLE` depending on how it was keyed),
  and a plausible `timestamp` (device uptime ms, not wall-clock).
- **Failure mode to watch for:** notification never arrives at all —
  check subscription happened (§8) and MTU was negotiated (§3) before
  assuming a driver bug.

## 6. Training state/events

- **Setup:** Bonded, connected, MTU negotiated, subscribed to the
  control-event characteristic.
- **Steps:** `train_start` with mode `KOCH`, key a few correct and
  incorrect answers, `train_confirm` on an exam if testing `EXAM` mode,
  `train_stop`.
- **Expected:** `train_state` notifications arrive reflecting
  `active`, `phase`, `target`, `correct`/`attempts` changing correctly;
  **verify the field rename** — the wire JSON's `examCorrect`/
  `examTotal` must appear in the parsed Dart object as
  `examCorrectCount`/`examTotalCount` (§5's easy-to-miss trap). After
  `train_stop`, the next event should show `active: false` with no
  other fields present.
- **Also test:** sending `train_start` with an invalid mode string —
  should produce an `{"evt":"error",...}` → mapped to
  `BackendError(code: "TRAINING_START_FAILED")`, per §7.
- **Failure mode to watch for:** events arriving with the raw wire
  field names instead of the renamed logical ones — this is the
  specific drift `MOBILE_BLE_PROTOCOL.md` was written to prevent, and
  won't throw an error, just silently produce wrong-shaped data.

## 7. Reconnect after disconnect

- **Setup:** A bonded, previously-connected device.
- **Steps:** Disconnect deliberately (app-side), then reconnect.
  Separately: walk out of BLE range to force an unexpected disconnect,
  then walk back in range and let the app reconnect.
- **Expected:** Both reconnect without re-pairing (already bonded).
  State (subscriptions, MTU) must be re-established on each fresh
  connection — MTU negotiation and characteristic subscription are
  **per-connection**, not persistent, so a reconnect that skips
  re-requesting MTU 247 or re-subscribing will silently reproduce the
  item-3/item-5 failure modes on the second connection even though the
  first connection worked fine.
- **Failure mode to watch for:** first connection works, reconnect
  "looks" successful but telemetry/training events silently stop
  arriving — almost always a skipped re-subscribe or re-negotiate step.

## 8. Realistic maximum telemetry payloads

- **Setup:** Bonded, connected, MTU negotiated to 247 (or the max the
  platform grants).
- **Steps:** Key the longest reasonable word/callsign (up to
  `BLE_WORD_FIELD_CAP = 24` characters), including at least one
  character that requires JSON escaping if practical to arrange
  (unlikely in normal Morse content, but worth trying a word with
  punctuation like `?`, `/`, or `-`).
- **Expected:** Full word arrives intact and correctly decoded/
  unescaped, even at the theoretical worst case of `24 * 6 = 144` bytes
  fully escaped (§4).
- **Also test:** the longest `train_state` payload shape (`EXAM` mode,
  which has the most fields populated at once) — should still fit
  under the negotiated MTU and `BLE_CONTROL_EVT_CAP = 220`.
- **Failure mode to watch for:** truncated word text, or a `train_state`
  event that silently never arrives specifically in `EXAM` mode when it
  arrived fine in other modes — indicates the negotiated MTU is high
  enough for short payloads but not the longest ones.
