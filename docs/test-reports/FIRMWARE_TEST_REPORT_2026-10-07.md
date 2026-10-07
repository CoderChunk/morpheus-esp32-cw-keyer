# MORPHEUS Firmware 2.8.4 — Test Report

| | |
|---|---|
| **Document** | Test report: functional, non-functional, regression, integration and system testing |
| **Date** | 7 October 2026 (IST) |
| **Software under test** | Firmware **2.8.4** (`config.h`), desktop bridge 1.2.0, `morpheus_ui` native BLE client |
| **Status** | **COMPLETE except ST-17 (fresh pairing, interactive)** — one reliability finding open (FND-01) |

## 1. Executive summary

| Test type | Tests | Pass | Fail | Pending |
|---|---|---|---|---|
| Functional (host, firmware modules) | 10 | 10 | 0 | 0 |
| Non-functional | 13 | 12 | 1 | 0 |
| Regression | 8 | 8 | 0 | 0 |
| Integration | 22 | 22 | 0 | 0 |
| System (real device, over BLE) | 18 | 16 | 1 | 1 |
| **Total** | **71** | **68** | **2** | **1** |

**Verdict:** the firmware is functionally correct and robust against malformed input. Every documented command, error path and state transition behaved as specified on a real device, the device was left exactly as found after every run, and a 30-minute soak showed no drops. **One genuine reliability defect remains open:** about 1 in 8–10 reconnects to the bonded device fails shortly after connecting (FND-01), reproduced by two independent BLE clients. A second finding is a design limit on virtual keying speed (FND-02). Neither blocks use, but FND-01 should be investigated before relying on the device for unattended sessions.

Details of findings are in §9. Limits of this test campaign are in §10.

## 2. Test environment

| Item | Value |
|---|---|
| Device under test | MORPHEUS-CW, ESP32-D0WD-V3, address 38:18:2B:8A:70:3E, firmware 2.8.4 (flashed 6 Oct 2026; **not flashed by this campaign**) |
| Device state | Bonded and trusted with the host before testing |
| Host | Fedora Linux 43, kernel 7.2.9-100.fc43.x86_64, BlueZ 5.87 (`bluetoothctl`), adapter 2C:3B:70:6B:8F:D4 |
| Toolchain | `arduino-cli` (esp32:esp32:esp32), g++ 15 (host tests), Python 3 with `bleak` (device tests), Flutter 3.47.5 |
| Baseline for regression | Tag v2.7.1 (last tagged release), compiled in an isolated git worktree |
| Authorization | Connecting to the device, virtual keying, sessions, WPM change-and-restore, and repeated reconnects were authorized by the owner. No flashing and no settings other than WPM (restored) were changed. |

## 3. Methods and conventions

- **Host tests** compile the **real** firmware sources (`core_decoder.cpp`, `core_trainer.cpp`, `core_keyer.cpp`, `core_games.cpp`, protocol headers) with g++ and test doubles for hardware; no reimplementation.
- **Device tests** (`tests/hardware/ble_system_test.py`) use an independent Python/`bleak` client: no UI and no bridge in the path. The control characteristic holds only the *latest* event, so an `ack` is routinely overwritten by the state push that follows; confirmations are therefore taken from **state events**, with a read of the characteristic as fallback (documented firmware behaviour).
- **Integration tests** run the real bridge (`ws_server.py`/`backend.py`) against the real device, and cross-check the source of every layer for contract drift.
- **Safety:** each device run ends with a cleanup step (key released, training/game stopped, WPM restored) and verifies it (ST-15).
- Commands: `python -m unittest discover -s tests`; `tests/native/run.sh`; `tests/native/run_sanitized.sh`; `python3 tests/hardware/ble_system_test.py <address>`; `arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all firmware/MORPHEUS`.

## 4. Functional testing (host, real firmware modules)

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| FT-01 | Decoder: timing boundaries, character and word decode | `tests/native/test_core_decoder.cpp` against real `core_decoder.cpp`, injectable clock | Dits/dahs, character and word gaps classified exactly at their boundaries | All decoder tests passed | PASS | Replaces the Python model as the authority; model still run (FT-08) |
| FT-02 | Trainer: modes, Koch pool, scoring, level-up | `test_core_trainer.cpp` | Documented behaviour for all modes and Koch progression | All trainer tests passed | PASS | |
| FT-03 | Keyer: WPM, weight, iambic, setters | `test_core_keyer.cpp` | Setters clamp and apply per spec | All keyer tests passed | PASS | |
| FT-04 | Games: input observer, scoring, trainer/game exclusivity | `test_core_games_input.cpp` | Scoring and exclusivity rules hold | Passed | PASS | |
| FT-05 | Game-morse wire encoder | `test_game_morse_protocol.cpp` | Valid frames encode; invalid input refused | Passed | PASS | Also stressed in NFT-01 |
| FT-06 | Keyer-setting parser and ranges | `test_keyer_settings_protocol.cpp` | Bare integers accepted; junk, signs, decimals, overflow rejected; ranges per field | Passed | PASS | |
| FT-07 | Keyer metrics: real element durations, expiry, reset, rollover, packet budget | `test_keyer_metrics.cpp` against real decoder | Only actual completed elements reported; expired samples null | Passed | PASS | |
| FT-08 | Python decoder model | `tests/test_decoder_logic.py` | Model agrees with spec | Passed | PASS | |
| FT-09 | BLE JSON size budget | `tests/test_ble_json_budget.py` | Overhead and escaped-word buffer cover the worst case | Passed | PASS | |
| FT-10 | Bridge and device-management logic | `tests/test_device_management.py`, `test_keyer_metrics_bridge.py`, `test_game_morse_bridge.py`, `test_ui_commands.py` | Queue priority, parsing, correlation, command shaping correct | All passed | PASS | |

## 5. Non-functional testing

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| NFT-01 | Robustness: parser/encoder fuzzing | `tests/native/test_protocol_fuzz.cpp`: 734,520 deterministic cases (junk JSON, hostile ids/patterns/games, every output capacity 0–220 bytes, extreme counter values) with exact-size heap buffers | No overrun, no write on failure, every accepted output is well-formed and within capacity | **734,520 checks, 0 failures** | PASS | New test added by this campaign |
| NFT-02 | Robustness: hardened build of every native test | `tests/native/run_sanitized.sh`: `-O2 -Werror`, UBSan (trap mode), `_FORTIFY_SOURCE=3`, glibc heap checks, stack protector | All suites run clean | All clean | PASS | **AddressSanitizer was not available** (runtime library not installed); a hardened fallback ran instead. Install `libasan`/`libubsan` for full ASan |
| NFT-03 | Performance: decoder throughput | `bench_decoder.cpp`: 200,000 characters (600,000 elements) through the real decoder | Every character decoded; < 20 µs/element on host | 74 ns/element; 200,000 characters and 40,000 words decoded exactly | PASS | Host figure is a regression indicator only; an ESP32 is roughly 10–30× slower, still far below keying rates |
| NFT-04 | Resource: flash and RAM | `arduino-cli compile` | Within device limits | 741,918 B flash (56% of 1,310,720); 42,180 B RAM (12% of 327,680) | PASS | |
| NFT-05 | Resource regression vs v2.7.1 | Compile tag v2.7.1 in a clean worktree and compare | Growth proportionate to new features | Flash +3,468 B (+0.47%); RAM +64 B | PASS | v2.7.1: 738,450 B / 42,116 B |
| NFT-06 | Code quality: compiler warnings | ESP32 build with `--warnings all` after a clean rebuild; host build `-Wall -Wextra -Werror` | No new warnings | **Correction:** an earlier statement here ("none reported") was wrong — it came from a cached build that prints no warnings. A clean rebuild shows **10 pre-existing warnings in 8 distinct forms** in `ui_backend.cpp`, `transport.cpp`, `ui_state.cpp`, `ui_screens.cpp` (format/truncation, unused variables, one deprecated NimBLE call). None are in code changed by this campaign. The build gate now fails only on warnings *not* in `tests/firmware_warnings.baseline`. Host build: 1 benign `strncpy` warning (OBS-01). | PASS (baseline) | Pre-existing warnings are recorded, not fixed |
| NFT-07 | Performance: command round-trip latency | ST-12: 100 × `get_device_info`, write to notify | No loss; p95 < 1 s | p50 98 ms, p95 99 ms, max 194 ms, 0 lost | PASS | Latency is bounded by the BLE connection interval (see FND-02) |
| NFT-08 | Throughput and burst handling | ST-11: 100 requests back to back | Device stays alive; responses delivered | Sent in 9.9 s (about 10/s, limited by one ATT round trip each); 100/100 notifications; device alive | PASS | |
| NFT-09 | Robustness: malformed and oversize input on the real device | ST-05, ST-06 | Documented errors; no crash; device still answers | 12 invalid commands rejected with the exact documented message; a ≥ 96-byte command ignored; device responsive | PASS | |
| NFT-10 | Reliability: 30-minute connection soak | Hold connection 30 min, one device-info request per 30 s (via the app's native client) | 0 drops, 0 failures | 60/60 replies, 0 failures, 0 drops; reply p50 224 ms, max 241 ms | PASS | |
| NFT-11 | Reliability: reconnect cycles | Disconnect/reconnect the bonded device repeatedly and send a first command (§8 table) | 100% | **Python/bleak: 72/80 (90%). Dart/universal_ble: 88/100 (88%).** Failures: link dropped or first command refused right after connecting; the next attempt succeeds | **FAIL** | **FND-01.** Reproduced by two independent clients, not dependent on the delay between cycles (1 s, 2 s, 4 s) |
| NFT-12 | Security: link protection | Source review of `transport.cpp`: bonding + MITM + secure connections, display-only IO capability (random 6-digit passkey), control/word/game characteristics require encryption and authentication | Sensitive characteristics unreadable/unwritable without a bond | Flags present as specified; every device test above ran over the bonded, encrypted link | PASS | **Not tested:** rejection of an *unbonded* client (would require removing the bond; see ST-17) |
| NFT-13 | Timing: virtual key accuracy | Hold requested 30/70/150/300 ms; read the device's measured element | Measured element ≈ requested | 30→97, 70→146, 150→244, 300→389 ms (key-up sent after the key-down acknowledgement). With concurrent scheduling (app test): 80→97, 240→243 | **PASS with limit** | **FND-02:** shortest producible element ≈ one ATT round trip (~97 ms) |

## 6. Regression testing

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| RT-01 | Original Python tests unchanged | `python -m unittest discover -s tests` (the original 23 tests plus 11 new) | All pass | 34/34 | PASS | |
| RT-02 | Original native suites unchanged | `tests/native/run.sh` (decoder, trainer, keyer, games, game-morse, settings, metrics) | All pass | All passed | PASS | |
| RT-03 | Build still succeeds; size within bounds | `arduino-cli compile` | Builds; growth small | Builds; +0.47% flash vs v2.7.1 | PASS | = NFT-05 |
| RT-04 | v2.7.1 command set still works on 2.8.4 | ST-03, ST-07, ST-10 on the real device: `train_start/stop` (8 modes), `game_*`, `key_down/up` | Same behaviour as v2.7.1 | All behaved as before | PASS | |
| RT-05 | Mode-only `train_start` keeps default behaviour | ST-03 sends `train_start` without `kochLevel` for every mode | Starts with default pool | All 8 started | PASS | Backward-compatibility requirement of 2.8.3 |
| RT-06 | Decoded word telemetry unchanged | ST-15: a keyed dah appears on the word characteristic as `T` | Word notification delivered | `{"word":"T","wpm":18,"mode":"STRAIGHT",...}` | PASS | |
| RT-07 | Bridge legacy flows against the real device | INT-03 to INT-06 | Training and game state events as before | Passed | PASS | |
| RT-08 | UI regression suite | `flutter test` in `morpheus_ui` | All pass | 169 passed, 6 hardware tests skipped (opt-in) | PASS | |

## 7. Integration testing

### 7.1 Bridge ↔ real firmware (real device, real `ws_server.py`)

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| IT-01 | Bridge connects to the device | `connect` with the device address | `connectionChanged(CONNECTED)` | Connected as MORPHEUS-CW | PASS | |
| IT-02 | Device info | `requestDeviceInfo` | `deviceInfoChanged` with firmware 2.8.4 | firmware 2.8.4, 18 WPM, STRAIGHT | PASS | |
| IT-03 | Start training | `startTraining WORDS` | `trainingStateChanged(active, WORDS)` | As expected | PASS | |
| IT-04 | Stop training | `stopTraining` | `trainingStateChanged(inactive)` | As expected | PASS | |
| IT-05 | Start game | `startGame COPY` | `gameStateChanged(active, COPY)` | As expected | PASS | |
| IT-06 | Stop game | `stopGame` | `gameStateChanged(inactive)` | As expected | PASS | |
| IT-07 | Metrics probe | `probeKeyerMetrics` | Correlated `keyerMetricsReceived` with bridge RTT | Returned, RTT 194 ms | PASS | |
| IT-08 | Out-of-range setting | `setKeyerSetting wpm 99` | Rejected | Rejected | PASS | See OBS-02 (error code) |
| IT-09 | Snapshot | `getSnapshot` | state CONNECTED | CONNECTED | PASS | |
| IT-10 | Disconnect | `disconnect` | `connectionChanged(DISCONNECTED)` | As expected | PASS | |

### 7.2 Cross-layer consistency (source of firmware, bridge, simulator and app) — `tests/test_protocol_consistency.py`

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| IT-11 | GATT UUIDs agree, firmware ↔ bridge | Compare `config.h` with `protocol.py` for all 5 UUIDs | Identical | Identical | PASS | |
| IT-12 | UUID scheme agrees with the Dart client | Compare with `native_ble_morpheus_client.dart` | Same scheme | Same | PASS | |
| IT-13 | Firmware command vocabulary is the documented set | Extract commands from `ble_control.cpp` | The 15 documented commands (+2 debug-only) | Match | PASS | |
| IT-14 | Bridge only sends commands the firmware knows | Extract commands sent by `backend.py` | Subset of firmware set | Subset | PASS | |
| IT-15 | Simulator covers the whole firmware vocabulary | Compare `sim_bridge.py` | No missing command | Complete | PASS | Guards simulated tests against drift |
| IT-16 | Mode and game names agree | Compare enums in firmware, simulator | Same | Same | PASS | |
| IT-17 | Command size cap agrees | `BLE_CONTROL_CMD_CAP` vs simulator | 96 | 96 | PASS | |
| IT-18 | App only sends commands the firmware knows | Extract from the Dart client | Subset | Subset | PASS | |
| IT-19 | Setting ranges agree, firmware ↔ app | Compare `keyer_settings_protocol.h` with the app's bounds | Identical for wpm/tone/volume/weight | Identical | PASS | |
| IT-20 | CHANGELOG documents the running version | `FIRMWARE_VERSION` vs CHANGELOG | Entry exists | Entry exists | PASS | |
| IT-21 | App minimum firmware ≤ this firmware | Compare versions | 2.8.3 ≤ 2.8.4 | OK | PASS | |
| IT-22 | UI ↔ bridge ↔ simulated radio | 7 end-to-end tests (`morpheus_ui/test/e2e/bridge_e2e_test.dart`) | All pass | 7/7 | PASS | Simulator only; real-device UI results are in the native-BLE report |

## 8. System testing (real device, end to end over BLE) — `tests/hardware/ble_system_test.py`

| Test ID | Test scenario | Test procedure | Expected result | Actual result | Result | Remarks |
|---|---|---|---|---|---|---|
| ST-01 | Device info schema, ranges and version | `get_device_info`; check all fields | All present, in range; version equals `config.h` | firmware 2.8.4, 18 WPM, 600 Hz, vol 80, STRAIGHT, IAMBIC_B, weight 50 | PASS | |
| ST-02 | GATT service and characteristic properties | Enumerate service | Notify/read/write as designed | Present as specified | PASS | |
| ST-03 | Training: every mode starts and stops | `train_start`/`train_stop` for KOCH, CHARACTERS, WORDS, CALLSIGNS, ADAPTIVE, EXAM, LISTENING, COMBINED | State event active with that mode, then inactive | 8/8 | PASS | |
| ST-04 | Koch pool bounds | Start with pool 2, 12, 40; reject 1, 41, "x", −3, 2.5 | Valid accepted (pool echoed); invalid → `invalid Koch pool level` | 8/8 as expected | PASS | |
| ST-05 | Invalid commands return the documented error | 12 cases: no cmd, unknown cmd, bad mode/game, missing text, bad field/value (×3), bad probe id (×2), malformed JSON | Exact documented message each time | 12/12 | PASS | |
| ST-06 | Oversize command (≥ 96 B) | Send a 150-byte command, then `get_device_info` | Ignored; device still answers | Ignored; device answered | PASS | |
| ST-07 | Games and exclusivity | Start/pause/resume/stop COPY, MEMORY, SPEED; start training over a game; start a game during training | Each state follows; trainer stops a game; a game is refused during training | As expected | PASS | |
| ST-08 | `set_keyer` | WPM 18→19→18; attempt during training | Change confirmed by device info; `keyer busy` during training; restored | As expected | PASS | |
| ST-09 | Metrics correlation | 20 distinct `probe_keyer` ids | Each echoed exactly | 20/20 | PASS | |
| ST-10 | Key release safety | `key_down` ×2 then `key_up`; then a setting change | Not refused as busy (key released) | Not refused | PASS | |
| ST-11 | Request burst | 100 back-to-back requests | Device alive, all answered | As NFT-08 | PASS | |
| ST-12 | Round-trip latency | 100 sequential requests | No loss | As NFT-07 | PASS | |
| ST-13 | Disconnect/reconnect cycles on the bonded device | 10 cycles, then stress runs of 15 (1 s and 4 s spacing) and 30 (2 s spacing) | All reconnect and answer | Final run 7/10; stress 14/15, 14/15, 28/30. Earlier run 9/10 | **FAIL** | **FND-01** (= NFT-11) |
| ST-14 | Device left as found | After all tests: stop sessions, release key, restore WPM, verify | WPM 18, no session | WPM 18, training inactive | PASS | Run after every system run |
| ST-15 | Decoder end to end | Virtual dah (240 ms) | Reported as word `T` | `T` | PASS | A first attempt with a dit failed — see §9, TM-02 |
| ST-16 | Virtual-key element accuracy | Four requested holds; read device measurement | Within 250 ms above request | 30→97, 70→146, 150→244, 300→389 | PASS | Documents FND-02 |
| ST-17 | Fresh pairing: bond removed and re-created with the PIN from the display | Remove bond via the app; reconnect; operator enters the 6-digit PIN | New bond, connected | **PENDING** — interactive, needs the operator at the machine | PENDING | Would also allow testing rejection of an unbonded client (NFT-12) |
| ST-18 | Soak (system level) | As NFT-10 | 0 drops | 60/60, 0 drops | PASS | |

### Reconnect-cycle data behind ST-13 / NFT-11

| Client | Spacing | Cycles | Failed | Failure observed |
|---|---|---|---|---|
| bleak (Python) | 1 s | 10 | 1 | first command: GATT "Unlikely Error" (0x0E) |
| bleak (Python) | 1 s | 10 | 3 | same |
| bleak (Python) | 1 s | 15 | 1 | same |
| bleak (Python) | 4 s | 15 | 1 | same |
| bleak (Python) | 2 s | 30 | 2 | same, all at the first request after connecting |
| universal_ble (Dart) | 2 s | 20 | 1 | "Operation failed" |
| universal_ble (Dart) | 2 s | 40 | 6 | write failed, "Not connected" |
| universal_ble (Dart) | 2 s | 40 | 5 | write failed, "Not connected" (after an unsuccessful client retry experiment, which was reverted) |

## 9. Findings

| ID | Severity | Type | Description | Evidence | Recommended action |
|---|---|---|---|---|---|
| **FND-01** | **Medium** | Reliability (open) | About 10–12% of reconnects to the bonded device fail within about a second of connecting: the link is dropped, or the first GATT operation is refused ("Unlikely Error"). The next attempt succeeds. No dependence on the delay between cycles. | 20 failures in 180 cycles across two independent clients (bleak 8/80, universal_ble 12/100) | **Unconfirmed hypothesis:** on connect the firmware calls `startSecurity()` (`transport.cpp:170`) while BlueZ also initiates encryption for a bonded peer; a collision could tear down or stall the link. To confirm: capture the HCI trace (`btmon`, needs root) or enable `FEATURE_SERIAL` and log `EVT BLE_DISCONNECT reason=`. A possible fix is to call `startSecurity()` only if the link is not yet encrypted after a short delay. Meanwhile the app's auto-reconnect recovers within about 3 s. |
| **FND-02** | Low (design limit) | Performance | The firmware sets no preferred connection parameters (no `updateConnParams` in the source), so the BLE connection interval is chosen by the host. Every write is acknowledged before the next, so a key-down and key-up cannot arrive closer than about one round trip (≈ 97 ms measured). A dit shorter than that is stretched, which limits reliable virtual keying to roughly 12 WPM. Request/response round trips are ≈ 98 ms for the same reason. | ST-12, ST-16; app key-timing: dit 80 → 97 ms | Consider requesting a short connection interval (for example 7.5–15 ms) from the firmware after connecting, then re-measure. Physical-key keying is unaffected. |
| OBS-01 | Info | Code | `core_trainer_getKochCharset()` computes `outSize - 1`, which underflows when `outSize == 0`. The compiler's truncation warning at `core_trainer.cpp:376` is a false positive for current callers (all pass a real buffer and the code NUL-terminates). | Host build with `-Werror` | Add a zero-size guard if the function is ever exposed more widely. No change made. |
| OBS-02 | Low | Bridge | Out-of-range `setKeyerSetting` is rejected correctly but with code `INTERNAL_ERROR` (a plain `ValueError`); `INVALID_PARAMETER` would match the spec's convention. | IT-08 | Map `ValueError` in `ws_server.py` to `INVALID_PARAMETER`. |
| TM-01 | Test method | — | Pulse timing in the first key-timing run included the write acknowledgement (≈ 65 ms), so 80 ms dits were classified as dahs. | App key-timing run | Fixed: schedule key-up from the key-down start. |
| TM-02 | Test method | — | The first system run failed ST-04, ST-07 and ST-15 because tests (a) expected an `ack` that the firmware's single-value characteristic had already overwritten with the following state, and (b) again awaited the write acknowledgement before timing a 70 ms dit. | First system run: 11/15 | Fixed: confirm by state events; key a 240 ms dah. Re-run: 15/16 with only FND-01 failing. |

## 10. Coverage gaps and limitations

- **ST-17 (fresh pairing) is pending** and requires the operator to type the PIN shown on the device display. The rejection of unbonded clients is untested.
- **AddressSanitizer did not run** (runtime library not installed). The hardened fallback is weaker. To close the gap: `sudo dnf install libasan libubsan`, then re-run `tests/native/run_sanitized.sh`.
- **The Linux/BlueZ stack only.** Windows, macOS, Android and iOS centrals were not exercised, and FND-01 could be specific to BlueZ.
- **One device, one adapter, one host.** No second MORPHEUS unit or board revision.
- **Not tested:** the OLED UI and menus, physical paddle/straight-key input, LED and audio output, NVS persistence across power cycles, battery/power behaviour, multi-device trust-list behaviour (`BLE_TRUSTED_DEVICE_CAP`), advertising/pairing-window timing, and OTA/flash procedures.
- **Host timing figures** (NFT-03) are not ESP32 timings.
- **Root cause of FND-01 is not established** (see the hypothesis above).

## 10a. Round-1 fixes (7 Oct, evening)

| Item | Change | Status |
|---|---|---|
| **FND-03** stuck key after link loss | Firmware 2.8.5: `ble_control_service()` releases an orphaned virtual key (`virtual_key_guard.h`, host test NEG-F11). **Flashed and verified on the device** (NEG-D14 and FS-N07 pass on 2.8.5). | Closed |
| Bridge error code | `setKeyerSetting` with a bad value now returns `INVALID_PARAMETER` (was `INTERNAL_ERROR`); test added | Done |
| Build gate | `tests/firmware_warnings.baseline` + a forced clean rebuild, so warnings can no longer be hidden by the build cache | Done |

## 11. Conclusion

The firmware passes 68 of the 71 planned tests. Two fail, and both are the same defect (FND-01, seen as NFT-11 and ST-13); one is pending (ST-17, interactive). All documented commands and error paths behave as specified on a real device, the parsers withstood more than 734,000 hostile inputs, resource growth since v2.7.1 is negligible, and a 30-minute soak ran without a drop. The open reliability defect **FND-01 (intermittent failure after reconnecting)** should be diagnosed with an HCI trace or serial log before the firmware is treated as release-quality for unattended use. FND-02 is a documented design limit with a likely firmware-side improvement.

## Addendum — firmware 2.8.6 (2026-10-07)

| Finding | Status |
|---|---|
| **FND-03** stuck key after link loss | **Closed** in 2.8.5; verified on hardware. |
| **FND-02** virtual key floor | **Mitigated** in 2.8.6: the firmware requests a 7.5–15 ms connection interval once the link is secure. On the same Linux/BlueZ host ST-16 now measures requested 30/70/150/300 ms holds as 57/90/168/326 ms (previously 97/146/244/389). Hosts may ignore or adjust the request. |
| **FND-01** reconnect failures | **Open**, unchanged (SYS-13 9/10 and the reconnect-cycle step 9/10 after the 2.8.6 flash). Root cause still unconfirmed. |

The 2.8.6 smoke test ran on a board flashed with the 2.8.5 build plus the connection-interval change; the released 2.8.6 differs only in the version string.
