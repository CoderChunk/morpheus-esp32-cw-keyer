# Changelog

All notable changes to MORPHEUS are documented here.

Versioning follows a simple `MAJOR.MINOR.PATCH` scheme:
- **Major features** increment the minor version (e.g. v1.1.0, v1.2.0).
- **Bug fixes** increment the patch version (e.g. v1.1.1).

---

## [v2.7.1] — Fix: Listening Audio Now Matches the Device's Real Keyer Speed

v2.7.0 shipped client-side Morse audio synthesis for `LISTENING`/
`COMBINED` training and the device games, but had no way to know the
device's actual keyer speed - `Capabilities.settings` is still `false`,
so it hardcoded 18 WPM (`config.h`'s `DEFAULT_WPM`), which could be
audibly wrong (too fast or too slow) versus whatever the operator had
actually set.

### Added
- **`"wpm"` field in `train_state` and `game_state`** (`ble_control.cpp`),
  reporting `core_keyer_getWpm()` - the same base keyer speed
  `startPlayback()` (training) and `copySpawnNext()`/`memoryPlayChain()`/
  `spdSpawnBeat()` (the three device games) already derive their actual
  playback dit length from. Present on every active session/game,
  unconditionally, no new command needed - it rides along on state
  pushes that were already happening.
- `backend.py`: `wpm` on `TrainingState`/`GameState`, parsed in
  `_handle_control_payload()`.
- `morpheus_ui`: `wpm` on the Dart `TrainingState`/`GameState` models
  (`lib/models/training_state.dart`, `lib/models/game_state.dart`).
  `training_panel.dart`/`device_games_panel.dart` now call
  `ditDurationForWpm(training.wpm ?? _kFallbackAudioWpm)` instead of a
  flat constant - `_kFallbackAudioWpm` (still 18) only applies against
  older firmware that doesn't send the field yet.
- 4 new Dart tests (`test/wire_models_test.dart`) locking in the wire
  contract: `wpm` parses when present, stays `null` against an
  older-firmware-shaped payload missing the field.

### Changed
- `FIRMWARE_VERSION`: 2.7.0 → 2.7.1.

---

## [v2.7.0] — Feature: Listening Training, Combined Mode, and Device Ear-Training Games

Every existing Training mode and client-owned game tested *sending*
(key it, device decodes and scores it); comprehension - copying by ear
alone - had no dedicated exercise anywhere in the app. This adds it at
two levels: two new Training modes, and Flutter-side wiring for three
on-device games that already existed in firmware but were never
exposed to the app.

### Added
- **Two new Training modes: `LISTENING` and `COMBINED`**
  (`core_trainer.h`/`.cpp`, `ble_control.cpp`). `LISTENING` is pure
  comprehension - the device plays a character, phase moves to the new
  `AWAIT_ANSWER`, and the round is decided entirely by the new
  `train_answer` command (`core_trainer_submitAnswer()`), no keying at
  all. `COMBINED` chains both skills: the same identification stage
  gates into the existing keyed-reply path (`DRILL_LISTENING`), and a
  round only counts as correct when both the identification and the
  keyed reply were right (`pendingIdentificationCorrect` AND-gated into
  `onTrainingCharDecoded()`). Covered by 7 new native tests in
  `tests/native/test_core_trainer.cpp` exercising both modes' state
  machine directly (correct/incorrect answers, replay-mid-identification,
  the AND-gate, keyed input being ignored during `AWAIT_ANSWER`).
- **`train_answer` BLE command** (`{"cmd":"train_answer","text":"K"}`) -
  only meaningful while `phase == "AWAIT_ANSWER"`; a no-op otherwise.
  Backend: `MorpheusBackend.answer_training()`; WS method `answerTraining`.
- **Flutter wiring for the on-device COPY/MEMORY/SPEED games**
  (`game_start`/`game_stop`/`game_pause`/`game_confirm`/`game_restart`,
  `game_state` push) - this protocol already existed in firmware
  (`core_games.cpp`) but had no client anywhere; `backend.py`/
  `ws_server.py` never parsed `game_state` at all. Added `GameState`/
  `GameId` end to end (backend.py, ws_server.py, `lib/models/game_state.dart`,
  `MorpheusClient`/`WebSocketMorpheusClient`/`FakeMorpheusClient`,
  `MorpheusSession.game`), and a new "Listening Arena" tab on the Games
  page (`lib/widgets/device_games_panel.dart`) distinct from the
  client-owned local arcade - these three are device-authoritative and
  need a live connection, exactly like Training.
- **Client-side Morse audio synthesis** (`lib/audio/morse_audio.dart`,
  new `audioplayers` dependency) - real sidetone audio (600Hz sine,
  correct dit/dah/gap timing) rendered through the computer's speakers
  for every listening exercise (Training's `AWAIT_ANSWER`, and COPY's
  FALLING / MEMORY's PLAYBACK / SPEED's LISTEN phases), not just the
  device's own buzzer - useful when the operator isn't within earshot
  of the physical device. WAV synthesis is a pure/testable function
  (`synthesizeMorseWav`), covered by `test/morse_audio_test.dart`.
- **`core_games_memory_getChain()`** (`core_games.h`/`.cpp`) and a new
  `"chain"` field in `MEMORY`'s `game_state` JSON - the echo chain's
  full character sequence was never exposed over BLE before (only
  `chainLength`/`inputProgress`), so the client had no way to
  synthesize matching audio for it locally.

### Fixed
- **`ble_control.cpp`: `train_state`'s `phase` field truncated
  `"AWAIT_ANSWER"` to `"AWAIT_ANSWE"`.** `buildTrainStateJson()` copied
  it into a local `char phaseStr[12]` - exactly 12 characters is one
  too many for an 11-char-plus-NUL buffer, so `trainPhaseStr()`'s
  `strncpy()` silently dropped the final "R". Caught by live
  verification against real hardware after flashing v2.7.0 (not by the
  native test suite - `ble_control.cpp` isn't natively testable, it
  pulls in the full NimBLE stack); every other phase string was short
  enough to fit, which is why nothing caught this earlier. Fixed by
  bumping the buffer to 16 bytes.

### Changed
- `BLE_CONTROL_EVT_CAP` (`config.h`): 220 → 240 bytes - `MEMORY`'s new
  `chain` field and `AWAIT_ANSWER` (longer than any previous phase
  string) both grew the worst-case `game_state`/`train_state` JSON.
- `FIRMWARE_VERSION`: 2.6.2 → 2.7.0.

### Known issues / backlog
- **Intermittent mis-keying of longer/mixed Morse patterns (e.g. "K"
  `-.-`, "Y" `-.--`, "7" `--...`) observed during live verification,
  via a one-off Python WebSocket test script driving `keyDown`/`keyUp`
  with `asyncio.sleep`-timed gaps.** Reproduced on both the new
  `COMBINED` mode's keying stage and on plain `KOCH` (untouched this
  release) - same script, same intermittent failure mode - which rules
  out a `LISTENING`/`COMBINED`-specific regression; the scoring logic
  itself is independently covered by 7 deterministic native tests with
  no timing involved at all (`tests/native/test_core_trainer.cpp`).
  Working theory: BLE round-trip jitter between the `key_down` and
  `key_up` writes' arrival at the firmware (each element's duration is
  measured from the firmware's own `millis()` at write-arrival time,
  in `ble_control.cpp`'s `handleKeyDown()`/`handleKeyUp()`) occasionally
  pushes a short dit over the dit/dah classification threshold for
  characters with several elements. Not yet confirmed against the real
  Flutter app's keying path (`VirtualKey`/`backend.py`'s priority
  queue), which orders writes more carefully than the raw test script
  did and has tested reliably in prior live sessions. Flagged for a
  later focused pass - reproduce against the actual Flutter UI (not a
  synthetic script), and if real, investigate
  `BLE_KEY_GAP_COMPENSATION_MS` (`config.h`) headroom or firmware-side
  per-element duration tolerance.

### Design notes
- `target`/`chain`/`lastChar` are present in the wire payload
  unconditionally, including mid-round before a human would "know" the
  answer (same as every other mode's `target` field always was). The
  protocol doesn't hide them - the Flutter UI does, by choosing not to
  render them as text during the listening phase
  (`_TargetCard.hideTarget` in `training_panel.dart`,
  `hideTarget`/`visibleTarget` in `device_games_panel.dart`). This is
  documented as an explicit contract in `WS_PROTOCOL.md` §6.3/§6.3a and
  `MORPHEUS_BACKEND_API_REQUIREMENTS.md` §8.6, not an implicit
  convention a future client could silently break.
- Firmware-authoritative rather than Flutter-owned, unlike the
  client-side games - this matches Training's existing "device is
  authoritative for the current lesson and scoring" model instead of
  the Games catalog's "Flutter owns it completely" model, since
  `LISTENING`/`COMBINED` are new Training modes, and COPY/MEMORY/SPEED
  were already firmware-owned games (just unwired).
- No live WPM is exposed over the protocol yet (`Capabilities.settings`
  is `false`), so synthesized audio uses a hardcoded 18 WPM
  (`config.h`'s own `DEFAULT_WPM`) rather than whatever the operator's
  keyer is actually set to - a real limitation until settings are wired
  up, noted inline at both `_kListeningAudioWpm`/`_kDeviceGameAudioWpm`
  call sites.

---

## [v2.6.2] — Fix: Stop Training Intermittently Had No Visible Effect

Follow-up to v2.6.1. That fix addressed one real cause of "Stop doesn't
work" (BLE_WORD_CHAR_UUID flooding during training), but live testing
against real hardware - reproducing the user's exact WORDS session,
including a background-keying stress test and a scripted 12-cycle
start/stop loop - showed Stop could *still* silently do nothing. Root
cause turned out to be two independent bugs, found and fixed in order:

### Fixed
- **`tools/ble_client/backend.py`: `train_stop`/`train_start`/
  `train_confirm` could get stuck behind a backlog of `key_down`/
  `key_up` writes.** All of these share one GATT characteristic
  (`BLE_CONTROL_CMD_UUID`), and a held virtual key (paddle/straight key
  in the Flutter UI) keeps writing to it continuously; with no explicit
  ordering, concurrent asyncio tasks landed on the wire in whatever order
  they happened to be scheduled. Fixed with a single `asyncio.PriorityQueue`
  drained by one writer coroutine (`_command_writer`): control commands
  always jump ahead of already-queued key events (capped at
  `MAX_KEY_BACKLOG = 4`, beyond which excess key commands are dropped
  rather than grown unbounded), and a 3s timeout around the one in-flight
  `write_gatt_char` call turns a peripheral that never acks into a
  reported error instead of a permanently wedged queue.
- **The real remaining cause, found via direct serial capture against
  the physical device: `sendAck()` and the periodic train_state/
  game_state push both write the same `BLE_CONTROL_EVT_UUID`
  characteristic value, from two different FreeRTOS tasks** (the NimBLE
  host task processing the incoming command vs. the main `loop()` task's
  `ble_control_service()`). Whichever writes *last* wins the
  characteristic's value - confirmed live that `sendAck("train_stop",...)`
  could race ahead of the state push and leave the characteristic holding
  a stale `{"evt":"ack",...}` instead of `{"active":false}`, with nothing
  to correct it since the state itself had already settled and wouldn't
  naturally change again. `ble_control.cpp` now calls a new
  `forceTrainStatePush()`/`forceGameStatePush()` immediately after every
  train_*/game_* `sendAck()`, guaranteeing the state push is always the
  last write for that command, bypassing the periodic pushes'
  dedup/rate-limit (a real user command isn't the high-frequency case
  that limiter exists for).
- **Defense in depth:** `backend.py` now also does a guaranteed GATT
  *read* of `BLE_CONTROL_EVT_UUID` ~300ms after every train_*/game_*
  write, independent of whether the corresponding NOTIFY was delivered -
  the characteristic's underlying value is always current once the above
  ordering fix lands, so a plain read can never observe the staleness a
  dropped NOTIFY does. This is the backstop, not the primary fix - BLE
  Notify has no delivery guarantee at the protocol level, confirmed via
  live capture that it was intermittently (~25-50% of the time) losing
  the training-stopped confirmation even when the firmware sent it
  correctly.

### Tried and reverted
- Switching `BLE_CONTROL_EVT_UUID` from NOTIFY to INDICATE (acknowledged,
  host-retried delivery) was tried first as the fix for BLE's
  no-delivery-guarantee problem. It triggered a NimBLE-Arduino host-stack
  issue on this board - `BLE_GAP_EVENT_NOTIFY_TX`'s status callback
  entered a runaway repeat loop under our usage pattern - so it was
  reverted back to NOTIFY in favor of the read-back approach above.

---

## [v2.6.1] — Fix: Stop Training/Game Ignored Mid-Round

### Fixed
- **`train_stop`/`game_stop` could go unanswered while the user was mid-word
  (WORDS/CALLSIGNS training) or mid-chain (fast-paced games).** Root cause:
  `events_onPatternChanged()` (added in v2.6.0) fired its BLE notify on
  *every* keyed element and character finalize unconditionally, including
  while a training/game session had the decoder's training sink installed.
  During a multi-character WORDS round this produced a sustained burst of
  notifications on `BLE_WORD_CHAR_UUID`, which could starve the
  `BLE_CONTROL_CMD_UUID` write carrying the stop command until the round's
  keying paused — so Stop appeared to do nothing until the user finished
  the word.
- `core_decoder.cpp`: both call sites of `events_onPatternChanged()`
  (`finalizeCharacter()`, `core_decoder_addElement()`) are now suppressed
  whenever a training sink is set, mirroring the existing suppression of
  `events_onCharacterComplete()`/word-buffer updates during training.

### Changed
- `morpheus_ui`: Training panel's live-session Stop button no longer
  stretches to match the virtual-key card's full height — it now sits at a
  normal 44px height, centered inside its own card.

---

## [v2.6.0] — Live Dit/Dah Pattern Over BLE

### Added
- **BLE now also carries the live in-progress dit/dah pattern**, the
  same thing the OLED's header readout already showed locally
  (`ui_backend_getLivePattern()`). New `events_onPatternChanged()` hook
  in `core_decoder.h`, fired from `core_decoder_addElement()` (one new
  element keyed) and from `finalizeCharacter()` (character finalized,
  pattern cleared) - same push-based approach as v2.5.0's live-word
  event, just one granularity finer. `MORPHEUS.ino` wires it to the new
  `transport_notifyLivePattern()`, sharing `BLE_WORD_CHAR_UUID` again,
  this time under JSON key `"pat"`.
  - Firmware: `core_decoder.h/.cpp`, `MORPHEUS.ino`, `transport.h/.cpp`
  - Python bridge: `backend.py` (`LivePatternEvent`,
    `on_keyer_live_pattern`), `ws_server.py`
    (`keyerLivePatternReceived` event), `WS_PROTOCOL.md`,
    `MOBILE_BLE_PROTOCOL.md`
  - Flutter: `livePatternEvents` stream on `MorpheusClient`, a
    `showLivePattern` toggle (session-only, not persisted) and a
    live pattern readout docked into the CW Keyer page's metrics row,
    plus an animated VU-meter-style waveform that pulses while a
    pattern is being keyed
  - Fires once per keyed element - the most frequent of the three
    telemetry events, but still only every 30-60ms even at rapid 40 WPM
    dits, well inside a BLE connection interval.

---

## [v2.5.0] — Live Character Decode Over BLE

### Added
- **BLE now carries live, in-progress character decode**, closing a gap
  left by v2.4.0's OLED live-decode work: the OLED redesign only ever
  touched the local Home screen - BLE, the Python bridge, and the
  Flutter app still only ever saw a complete word once flushed on a
  word-gap. `events_onCharacterComplete()` (`MORPHEUS.ino`) now also
  calls `transport_notifyLiveWord()`, sending the whole in-progress
  word (not just the new character) over the existing
  `BLE_WORD_CHAR_UUID` notify characteristic, distinguished from the
  final-word event by JSON key (`"live"` vs `"word"`) rather than a new
  UUID - so a coalesced/dropped intermediate notification is harmless,
  the last one received always reflects full current state.
  `core_decoder.cpp`'s `finalizeCharacter()` now appends to `wordBuffer`
  *before* firing the character-complete event, so the live payload
  already includes the just-decoded character.
  - Firmware: `transport.h/.cpp`, `core_decoder.cpp`, `MORPHEUS.ino`
  - Python bridge: `protocol.py` (n/a, same UUID), `backend.py`
    (`LiveWordEvent`, `on_keyer_live_word`), `ws_server.py`
    (`keyerLiveWordReceived` event), `WS_PROTOCOL.md`
  - Flutter: `KeyerWordEvent`-shaped `liveWordEvents` stream on
    `MorpheusClient`/`WebSocketMorpheusClient`, `MorpheusSession.liveWord`,
    Home screen's live console display
  - Not yet implemented in `MOBILE_BLE_PROTOCOL.md`'s direct-GATT path
    (mobile client doesn't exist yet)
  - At typical/worst-case keying speed (30-40 WPM), live notifications
    fire at most every 120-160ms with a <80 byte payload - well within
    a single BLE connection interval, no coalescing risk (the earlier
    OLED-dump coalescing bug happened at microsecond-scale bursts,
    ~1000x faster than character-decode timing).

---

## [v2.4.0] — Live Decode Display, OLED Debug Tooling & Keyer Test Coverage

### Added
- **Home screen now decodes live, character by character** instead of
  only showing a complete word once it finishes. `core_decoder.cpp`
  already built the word up one character at a time internally
  (`wordBuffer`, finalized per-character, flushed to history only on a
  word-gap) - that data just never reached the Home screen's transcript,
  which only redrew on the word-complete event. Fixed by adding
  `ui_backend_getLiveTranscriptLines()` (the committed transcript with
  the in-progress word appended) and having `display.cpp`'s poll loop
  diff it every cycle, not just on word-complete.
  - Files: `ui_backend.h/.cpp`, `display.cpp`
- **Live dit/dah pattern readout**, header row, top-right, no label -
  shows whatever character is currently mid-key. New persisted setting,
  **Settings → Display → Live Pattern** (on by default), to hide it.
  Settings-schema bump (`SETTINGS_VERSION` 8→9).
  - Files: `ui_screens.cpp`, `ui_state.cpp/.h`, `ui_menu.cpp/.h`,
    `ui_backend.h/.cpp`, `services.h/.cpp`, `ui_mockdata.h`, `config.h`
  - Verified on real hardware over BLE (virtual-key simulation +
    live screenshots mid-sequence): word grows one character per
    snapshot, header pattern renders without overlapping the status
    text, footer correctly still shows Decoder ON/OFF when idle.
- **`tools/oled_render/`** - renders the real `ui_renderer.cpp`/
  `ui_screens.cpp` drawing code against the real U8g2 library on the
  host, no ESP32 or display needed. Walks the entire menu tree
  (~130 screens) to PNG in about a second.
- **`tools/oled_screencap/screencap_ble.py`** - live screenshot of the
  physical device's OLED over BLE (`dump_screen_start`/
  `dump_screen_chunk` commands, gated by
  `FEATURE_DEBUG_SERIAL_COMMANDS`, confirmed zero binary-size impact
  with it off). A USB-serial version was tried first and dropped -
  opening the serial port resets this board's ESP32 every time
  (driver-level auto-program circuit behavior, not fixable host-side),
  making repeated capture useless; BLE connect/disconnect never
  touches the reset pins. `--session` mode reuses one connection
  across many captures instead of reconnecting per shot.
  - Files: `ble_control.cpp`, `ui_renderer.h/.cpp`
- **`tests/native/test_core_keyer.cpp`** - 11 native regression tests
  against the real `core_keyer.cpp`: straight-key DIT/DAH
  classification, WPM/weight/volume/sidetone clamping, mode-change
  mid-element state reset, and iambic paddle behavior including the
  actual Mode A vs B distinction (a squeeze released mid-element sends
  one extra alternating element in Mode B, not in Mode A). Verified to
  actually catch a regression (flipped classification comparison)
  before being added.
  - Files: `tests/native/test_core_keyer.cpp`, `run.sh`,
    `arduino_stub/Arduino.h`

### Fixed
- **Several stale comments and one dead code path**, found during a
  full firmware audit: a `handleLiveMonitor()` stub left over from an
  earlier refactor (superseded by, but never replaced with,
  `handleLiveMonitorReal()` - renamed back to `handleLiveMonitor()` now
  that the stub is gone) containing an unused placeholder label;
  `ui_mockdata.h` claiming callsign/date/time had no backend when
  `services.cpp`/`core_clock.cpp` already provide one;
  `core_profiles.cpp` saying "four" preset slots (actual: six);
  `docs/architecture.md` describing a "live pattern footer" this
  release moved to the header. `docs/FUNCTIONALITY_STATUS.md` had
  accumulated the most drift - stale version numbers, features it
  claimed were unreachable that were already wired up
  (`core_stats_resetLifetime()`, `core_led_trainerFlashOn/Off()`), and
  a "stale documentation identified" section that was itself stale
  (claiming `CHANGELOG.md`/`README.md` were out of date when they'd
  already been fixed in an earlier pass). `docs/build.md` showed
  `FEATURE_SERIAL` defaulting to `1`; actual default is `0`.

- **EXAM training results never reached any BLE/WebSocket client, and
  `train_confirm` silently did nothing once they were ready** — both
  bugs shared one root cause: `sessionActive` and
  `phase=DRILL_EXAM_DONE` flip together, atomically, in the same
  transition (`core_trainer.cpp`'s exam-completion branch). Once that
  happens, `buildTrainStateJson()` (`ble_control.cpp`) saw
  `!core_trainer_isSessionActive()` and emitted the bare
  `{"evt":"train_state","active":false}` shape with no `phase`,
  `examScorePercent`, `examPassed`, `examCorrect`, or `examTotal` —
  and since this push is rate-limited/deduped with no earlier frame
  carrying the result, that was the *only* push for the whole
  transition. The exam getters still held the correct values
  internally; they just never got serialized. Separately,
  `core_trainer_confirmPressed()` (called from BLE's `train_confirm`)
  opened with `if (!sessionActive) return;`, which now fired
  immediately in `DRILL_EXAM_DONE` — the OLED never hit this because
  its own exam-result screen dismisses via
  `ui_backend_trainClearExamResult()` directly
  (`ui_state.cpp`'s `handleTrainExamResult()`), bypassing
  `confirmPressed()` entirely, so only the BLE/WebSocket/mobile path
  was ever affected.
  Fixed by checking `core_trainer_isExamResultReady()` (not
  `sessionActive`) at the two points that mattered:
  `buildTrainStateJson()` now still emits the full `active:true`-shaped
  payload (mode/phase/target/exam fields) while an exam result is
  unconfirmed, and `confirmPressed()` now clears the result and resets
  to `DRILL_IDLE` *before* the `sessionActive` guard, instead of being
  silently swallowed by it. A related gap found via the new test
  suite's own state-isolation failing: `core_trainer_stopSession()`
  (the `train_stop` command) didn't clear the exam result either —
  stopping instead of confirming while an exam result was showing left
  it stuck under a `phase="IDLE"` payload no client shape expects; now
  fixed alongside the primary bug.
  - Files: `firmware/MORPHEUS/core_trainer.cpp`, `ble_control.cpp`
  - Tests: new `tests/native/test_core_trainer.cpp` (6 tests, first
    native coverage for `core_trainer.cpp`), each fix verified to
    actually fail against the pre-fix logic before confirming it
    passes against the fix. Real hardware: a full 25-round EXAM session
    driven over live BLE, confirming the terminal push carries
    `phase="EXAM_DONE"`, `examScorePercent=100`, `examPassed=true`,
    `examCorrectCount=25`, `examTotalCount=25`, and that
    `confirmTraining()` afterward resets to a clean `active:false`
    state instead of no-op'ing.

- **BLE virtual straight key could split one character into several**
  — e.g. ".." (should decode "I") sometimes decoded as "E" then "E".
  Root cause: both the physical and virtual key paths already fed the
  *same* single decoder (`core_decoder_addElement()`, via the shared
  `events_onKeyUp()` fan-out in `MORPHEUS.ino` — there was never a
  second decoder for BLE input), but the decoder's character/word-gap
  timeout is stamped from BLE-command-*arrival* time, not the
  operator's true release moment. A perfectly normal short pause
  between two virtual elements could exceed the gap threshold once BLE
  round-trip latency (connection interval + write-with-response ACK,
  for both writes of the *next* element) was added on top of it -
  especially at moderate/high WPM, where the gap window is only a few
  hundred milliseconds. Fixed by threading a `fromVirtualKey` flag
  (default `false`, physical call sites unchanged) through
  `events_onKeyUp()` into `core_decoder_addElement()`, and applying a
  new flat `BLE_KEY_GAP_COMPENSATION_MS` (250 ms, `config.h`) allowance
  to the gap check only when the pending character's/word's most
  recent element came from the virtual key. DIT/DAH classification
  itself, physical-key timing, and the BLE/WebSocket protocol contract
  are all unchanged.
  - Files: `firmware/MORPHEUS/config.h`, `core_decoder.h`,
    `core_decoder.cpp`, `core_keyer.h`, `MORPHEUS.ino`, `ble_control.cpp`
  - Tests: two new native regression tests in
    `tests/native/test_core_decoder.cpp`
    (`test_virtual_key_gap_compensation_prevents_premature_split`,
    `test_physical_key_gap_is_not_widened_by_compensation`), verified
    to actually fail against the pre-fix logic before confirming they
    pass against the fix.

### Changed
- **EXAM training results never reached any BLE/WebSocket client, and
  `train_confirm` silently did nothing once they were ready** — both
  bugs shared one root cause: `sessionActive` and
  `phase=DRILL_EXAM_DONE` flip together, atomically, in the same
  transition (`core_trainer.cpp`'s exam-completion branch). Once that
  happens, `buildTrainStateJson()` (`ble_control.cpp`) saw
  `!core_trainer_isSessionActive()` and emitted the bare
  `{"evt":"train_state","active":false}` shape with no `phase`,
  `examScorePercent`, `examPassed`, `examCorrect`, or `examTotal` —
  and since this push is rate-limited/deduped with no earlier frame
  carrying the result, that was the *only* push for the whole
  transition. The exam getters still held the correct values
  internally; they just never got serialized. Separately,
  `core_trainer_confirmPressed()` (called from BLE's `train_confirm`)
  opened with `if (!sessionActive) return;`, which now fired
  immediately in `DRILL_EXAM_DONE` — the OLED never hit this because
  its own exam-result screen dismisses via
  `ui_backend_trainClearExamResult()` directly
  (`ui_state.cpp`'s `handleTrainExamResult()`), bypassing
  `confirmPressed()` entirely, so only the BLE/WebSocket/mobile path
  was ever affected.
  Fixed by checking `core_trainer_isExamResultReady()` (not
  `sessionActive`) at the two points that mattered:
  `buildTrainStateJson()` now still emits the full `active:true`-shaped
  payload (mode/phase/target/exam fields) while an exam result is
  unconfirmed, and `confirmPressed()` now clears the result and resets
  to `DRILL_IDLE` *before* the `sessionActive` guard, instead of being
  silently swallowed by it. A related gap found via the new test
  suite's own state-isolation failing: `core_trainer_stopSession()`
  (the `train_stop` command) didn't clear the exam result either —
  stopping instead of confirming while an exam result was showing left
  it stuck under a `phase="IDLE"` payload no client shape expects; now
  fixed alongside the primary bug.
  - Files: `firmware/MORPHEUS/core_trainer.cpp`, `ble_control.cpp`
  - Tests: new `tests/native/test_core_trainer.cpp` (6 tests, first
    native coverage for `core_trainer.cpp`), each fix verified to
    actually fail against the pre-fix logic before confirming it
    passes against the fix. Real hardware: a full 25-round EXAM session
    driven over live BLE, confirming the terminal push carries
    `phase="EXAM_DONE"`, `examScorePercent=100`, `examPassed=true`,
    `examCorrectCount=25`, `examTotalCount=25`, and that
    `confirmTraining()` afterward resets to a clean `active:false`
    state instead of no-op'ing.

- **BLE virtual straight key could split one character into several**
  — e.g. ".." (should decode "I") sometimes decoded as "E" then "E".
  Root cause: both the physical and virtual key paths already fed the
  *same* single decoder (`core_decoder_addElement()`, via the shared
  `events_onKeyUp()` fan-out in `MORPHEUS.ino` — there was never a
  second decoder for BLE input), but the decoder's character/word-gap
  timeout is stamped from BLE-command-*arrival* time, not the
  operator's true release moment. A perfectly normal short pause
  between two virtual elements could exceed the gap threshold once BLE
  round-trip latency (connection interval + write-with-response ACK,
  for both writes of the *next* element) was added on top of it -
  especially at moderate/high WPM, where the gap window is only a few
  hundred milliseconds. Fixed by threading a `fromVirtualKey` flag
  (default `false`, physical call sites unchanged) through
  `events_onKeyUp()` into `core_decoder_addElement()`, and applying a
  new flat `BLE_KEY_GAP_COMPENSATION_MS` (250 ms, `config.h`) allowance
  to the gap check only when the pending character's/word's most
  recent element came from the virtual key. DIT/DAH classification
  itself, physical-key timing, and the BLE/WebSocket protocol contract
  are all unchanged.
  - Files: `firmware/MORPHEUS/config.h`, `core_decoder.h`,
    `core_decoder.cpp`, `core_keyer.h`, `MORPHEUS.ino`, `ble_control.cpp`
  - Tests: two new native regression tests in
    `tests/native/test_core_decoder.cpp`
    (`test_virtual_key_gap_compensation_prevents_premature_split`,
    `test_physical_key_gap_is_not_widened_by_compensation`), verified
    to actually fail against the pre-fix logic before confirming they
    pass against the fix.

### Changed
- **Keyer Mode moved from `CW Keyer` to `Settings → Keyer`** — it was the
  one keying-behavior setting living outside the group its siblings
  (WPM, Paddle Reverse, Iambic Mode, Weighting) already occupied. New
  path: **Menu → Settings → Keyer → Keyer Mode**. `PARAM_MODE` is
  dispatched by enum value everywhere it's read/written, not by menu
  position, so this is a pure menu-tree relocation with no change to
  keying behavior, persistence, or the settings schema.

---

## [v2.3.0] — LED Fixes & Diagnostics

**Status:** Hardware Verified
**Release:** Approved

### Fixed
- **Training/Farnsworth/Memory playback is now visible, not just audible** — `core_led_trainerFlashOn()`/`Off()` existed since the LED subsystem was introduced but had no caller anywhere; the status LED never flashed during training. It is now wired into the shared Morse playback engine (`core_morseplayer.cpp`), so every dit/dah played during a Training drill, Farnsworth practice, or Memory-keyer message now flashes the LED in sync with the tone.
- **BLE pairing-status LED no longer gets stuck blinking after the pairing window closes** — the LED inferred "pairing" from `bleEnabled + not connected` alone, never checking whether the radio was actually still advertising. Once the 60-second pairing window auto-expired without a connection, the radio went quiet but the LED kept blinking indefinitely. The LED now tracks real advertising state and goes dark exactly when the device stops being discoverable.

### Added
- **Diagnostics → LED Test** — instant LED ON / LED OFF / BLINK TEST actions for verifying the status LED on the bench, independent of BLE or training state. Blink test is a bounded, self-terminating pattern.
- `docs/FUNCTIONALITY_STATUS.md` — code-grounded audit of implemented vs. stubbed vs. implemented-but-unreachable functionality.
- `docs/USER_MANUAL.md` — full operator manual for the current feature set.

### Housekeeping
- `FIRMWARE_VERSION` bumped to match this release (was stale at `2.1.0` since v2.2.0).
- Removed the unused `LED_BLINK_SLOW_MS` constant.
- `transport.h`'s bond-reset comment no longer references a `PIN_BOND_RESET` that was never defined; documents the actual (menu-only) trigger.
- Added **Statistics → Reset Lifetime** — `core_stats_resetLifetime()` existed with no menu entry; a user could not previously clear lifetime statistics. Now reachable with a confirm dialog, same pattern as Factory Reset.
- README.md refreshed to match the current feature set (previously described the v1.1.0-era firmware).

### Compatibility
- No settings schema changes (`SETTINGS_VERSION` unchanged).
- No BLE protocol or JSON payload changes.

---

## [v2.2.0] — BLE Controls, Status LED & Date/Time

**Status:** Hardware Verified
**Release:** Approved

### Added
- Enhanced BLE controls and status handling, with status LED integration for BLE connection state.
- Callsign configuration, RTC (software clock) configuration, display configuration options, advanced keyer settings.
- **Outdoor** and **Silent** operator profiles, with per-profile display-contrast persistence.
- Date/time formatting support (3 date formats, 2 time formats) and improved RTC integration.
- Various UI and configuration-workflow improvements.

### Compatibility
- Users upgrading from v2.1.0 can update normally without additional migration steps.

---

## [v2.1.0] — Training, Statistics, Games & Profiles

**Status:** Hardware Verified
**Release:** Approved

### Added
- **Training subsystem**: Koch Method, Farnsworth, Character drills, Word drills, Callsign drills, Adaptive learning, Exam mode, progress tracking.
- **Statistics system**: session statistics, lifetime statistics, training accuracy, exam history, WPM history, persistent high scores.
- **CW Games**: Copy Challenge, Memory Challenge, Speed Challenge — with pause/resume, high-score persistence, and integrated statistics.
- **Operator Profiles**: four initial profile slots (Default, Portable, Contest, Practice), independently persisted, preserved during Factory Reset.
- **Audio improvements**: adjustable sidetone volume with live preview, sidetone enable/disable, persistent audio settings.
- New UI screens: volume adjustment, profile management, statistics, game interfaces.
- New module: `core_profiles`. Settings schema bumped to version 5.

---

## [v2.0.0] — OLED UI & Firmware Architecture Overhaul

**Status:** Hardware Verified
**Release:** Approved

### Added
- Fully redesigned 128×64 monochrome, Nokia-inspired OLED UI with rotary-encoder navigation, icon-based main menu, three-row submenus, and a full-screen parameter editor.
- New modular UI framework: `ui_backend`, `ui_renderer`, `ui_state`, `ui_menu`, `ui_screens`, `ui_input`, `ui_config`, `ui_layout`, `ui_fonts`, `ui_icons_anim`, `ui_splash_logo`, plus `core_memory`.
- Integrated CW Keyer menu (straight key/paddle configuration, Memory Messages, Live Monitor, Decoder toggle, Tune mode).
- Comprehensive Diagnostics module (system info, hardware diagnostics, runtime status, engineering tools).

### Breaking Changes
- Removed legacy WPM potentiometer support.
- OLED navigation moved to the rotary encoder module; straight key/DIT/DAH are no longer used for UI navigation and are dedicated exclusively to CW operation.

### Resource Usage
- ~697 KB flash (53%), ~40 KB RAM (12%) at release.

---

## [v1.2.2] — Legacy WPM Potentiometer Cleanup

**Status:** Hardware Verified
**Release:** Approved

### Removed
- Legacy analog WPM potentiometer support, its ADC initialization, and smoothing logic — freed GPIO34 for the (then) upcoming navigation keypad.

No functional regressions; internal architectural cleanup ahead of the v2.0.0 UI work.

---

## [v1.2.1] — BLE Bond Management & Stability

**Status:** Hardware Verified
**Release:** Approved

### Added
- Secure BLE bond reset functionality, first-time pairing workflow with passkey authentication, trusted-device management.
- Physical bond-reset button support (later superseded — see v2.3.0, no hardware pin was ultimately wired).
- Optional serial debug bond-reset command.
- Enhanced OLED pairing-status indicators.

### Fixed
- BLE pairing reliability and internal stability issues found during hardware validation.

---

## [v1.1.0] — Operator Settings Persistence

**Status:** Hardware Verified
**Release:** Approved

### Added
- **Operator settings persistence** — WPM, sidetone frequency, and paddle-reverse now survive a reboot, stored in NVS under a dedicated `morpheus_set` namespace (key `keyerSettings`), fully isolated from BLE bonding data.
- **Versioned settings storage** — a `SETTINGS_VERSION` field guards the stored format. A missing or version-mismatched blob falls back to defaults and is immediately rewritten in the current format, instead of silently reverting every boot.
- **Debounced NVS writes** — settings are written to flash only when something actually changed, and at most once per `SETTINGS_SAVE_DEBOUNCE_MS` (default 5000ms), to minimize flash wear.
- **Paddle reverse** — DIT/DAH can be swapped in iambic paddle mode; the setting persists across reboot. Straight key mode is unaffected (no second contact to swap).
- **Runtime sidetone frequency** — previously a compile-time constant, now adjustable at runtime and persisted across reboot.
- **Settings migration support** — `services_loadSettings()` detects outdated/invalid stored data via the version field and recovers cleanly rather than reading garbage.
- `services_factoryResetSettings()` — restores WPM/sidetone/paddle-reverse to factory defaults. No hardware trigger is wired to it yet; planned for v1.1 Feature 2 (Bond Reset).
- Temporary `FEATURE_DEBUG_SERIAL_COMMANDS` flag (off by default), with a serial command parser (`SET WPM`, `SET TONE`, `SET PADDLE_REV`, `RESET SETTINGS`) used to validate this feature on real hardware ahead of v1.2's real configuration menu. Requires `FEATURE_SERIAL=1`.

### Fixed
- **Sidetone range validation** — sidetone frequency is now clamped to `SIDETONE_FREQ_MIN_HZ`–`SIDETONE_FREQ_MAX_HZ` (200–2000 Hz) instead of accepting any value.
- **Version compatibility handling** — a version mismatch in stored settings is now explicitly detected and handled via automatic fallback-and-rewrite, rather than left unguarded.
- **Separate operator settings namespace** — settings storage uses its own NVS namespace (`morpheus_set`), distinct from BLE bonding's `cwkeyer` namespace, preventing any cross-contamination between settings reset and bond reset.
- **Improved settings recovery** — any NVS read failure now produces a clean, immediate fallback to defaults rather than undefined behavior.

### Compatibility
- No BLE protocol or JSON payload changes.
- No architectural changes — split-core structure (`core_keyer` / `core_decoder` / `display` / `transport` / `services`) unchanged.
- First-boot/unconfigured-board behavior is unchanged: falls back to existing `config.h` defaults (15 WPM, 600 Hz, paddle not reversed).
- BLE bonding/pairing state is unaffected by any part of this feature.
- All current feature flags (`FEATURE_OLED`, `FEATURE_BLE`, `FEATURE_SIDETONE`, `FEATURE_SERIAL`, `FEATURE_DEBUG_SERIAL_COMMANDS`) continue to work in their supported combinations.

### Files changed
`config.h`, `core_keyer.h`, `core_keyer.cpp`, `services.h`, `services.cpp`, `MORPHEUS.ino`

### Validated on real hardware
NVS settings persistence, debounced settings save, WPM persistence, sidetone persistence, paddle reverse persistence, settings migration logic, factory defaults, settings reset, BLE bonding unaffected, version upgrade handling, feature flag behavior, normal boot behavior, debug command validation.
