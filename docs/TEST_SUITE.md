# Firmware test suite

One entry point: **`tests/run_all.sh`**. Every level, host and real device, with a results folder
(`test-results/<time>/summary.md` + one log per step). The UI repo has its own suite and a
combined runner (`morpheus_ui/tool/run_full_system_tests.sh`, see its `docs/TEST_SUITE.md`).

```sh
tests/run_all.sh --host                                  # no hardware needed
tests/run_all.sh --host --build --baseline v2.7.1        # + ESP32 build, warnings, size budget and size vs a tag
tests/run_all.sh --device AA:BB:CC:DD:EE:FF              # system, negative and integration tests on the REAL device
tests/run_all.sh --device ADDR --stress 20 --soak 30     # + reconnect reliability and a 30 min soak
tests/run_all.sh --all --device ADDR --stress 20 --soak 30
```
Device steps need exclusive use of the device (one BLE connection), a bonded host, and **never flash
it**. The only persistent setting touched is WPM (+1, always restored; verified by ST-14).
Options: `--reconnect-min <0..1>` required reconnect success rate (default 1.0 = strict).

## Requirements
Python 3 with `bleak` and `websockets` (device tests), g++ (host tests), `arduino-cli` with the ESP32
core (build). For full AddressSanitizer: `sudo dnf install libasan libubsan` (otherwise
`run_sanitized.sh` falls back to a hardened build and says so).

## Levels and test IDs

| Level | Step | What | Test IDs | Files |
|---|---|---|---|---|
| Functional | H1, H2 | Real firmware modules on the host (decoder, trainer, keyer, games, protocol parsers, metrics); bridge logic | FT-01 … FT-10 | `tests/native/test_core_*.cpp`, `test_game_morse_protocol.cpp`, `test_keyer_*.cpp`, `tests/test_*.py` |
| Non-functional | H3 | Parser fuzz (734,520 cases), hardened/sanitized build, decoder throughput | NFT-01 … NFT-03 | `test_protocol_fuzz.cpp`, `bench_decoder.cpp`, `run_sanitized.sh` |
| Non-functional | B1, B2 | ESP32 build, `--warnings all`, flash/RAM budget (≤ 90% / ≤ 50%), growth vs a tag (≤ 5%) | NFT-04 … NFT-06 | `run_all.sh` |
| Non-functional | D1, D3, D4 | Latency, burst, malformed input, reconnect reliability, soak | NFT-07 … NFT-11, NFT-13 | `tests/hardware/ble_system_test.py` |
| Regression | H1, H2, B2, D1 | Original suites unchanged; v2.7.1 command set on the new firmware; size growth | RT-01 … RT-08 | (same files) |
| Integration | H1 | Contract drift across firmware, bridge, simulator and Flutter app (UUIDs, command vocabulary, size cap, ranges, versions) | IT-11 … IT-21 | `tests/test_protocol_consistency.py` |
| Integration | D2 | Real `ws_server.py` ↔ real device | IT-01 … IT-10 | `tests/hardware/bridge_integration_test.py` |
| System | D1 | Whole firmware over BLE: info, GATT surface, 8 training modes, Koch pool, games and exclusivity, settings, metrics, key release, decode, latency | ST-01 … ST-16 | `ble_system_test.py` |
| **Negative** | H2/H3 | Decoder: overflow, disabled, clock wraparound, time reversal, bad lookups, 1M random ops | NEG-F01 … NEG-F10 | `tests/native/test_decoder_negative.cpp` |
| **Negative** | H1 | Bridge parser/queue: malformed/non-UTF-8/unknown events, stale metrics, re-read errors | NEG-B01 … NEG-B10 | `tests/test_bridge_negative.py` |
| **Negative** | D1 | Real device: wrong-state commands, duplicate/conflicting starts, bad types and shapes, field/enum exactness, every range limit, size cap (95 B ok / 96 B ignored), degenerate writes, hostile strings, probe-id edges, 80 rapid contradictory commands, stray key traffic, **link loss with the key held** | NEG-D01 … NEG-D14 | `ble_system_test.py` |

`python3 tests/hardware/ble_system_test.py ADDR` can also be run directly; `--reconnect-stress ADDR N SECS`
and `--soak ADDR MINUTES` are the stand-alone reliability modes.

## Known failing tests (open defects, kept strict on purpose)
FND-03 (stuck key after link loss) was fixed in firmware 2.8.5 and verified on hardware; NEG-D14 / FS-N07 now pass on 2.8.5 and still fail on 2.8.4.

A red run is the truth until these are fixed. Details: `docs/test-reports/FIRMWARE_TEST_REPORT_*.md`.

| Test | Defect |
|---|---|
| `D3` / ST-13 / NFT-11 | **FND-01** about 10–12% of reconnects fail right after connecting (reproduced with two clients) |

## Adding tests
Host tests compile the real sources; add a `.cpp` to `tests/native/` and a `run` line in `run.sh` and
`run_sanitized.sh`. Device tests record results with `record("ID", "name", ok, detail)`; confirm outcomes by
**state events**, not `ack` (the control characteristic holds only the latest event, so an ack is routinely
overwritten by the state push that follows it).
