"""System / integration tests of REAL firmware over BLE (bleak) - no UI, no bridge.

    python3 tests/hardware/ble_system_test.py AA:BB:CC:DD:EE:FF [--json out.json]

Talks to a bonded MORPHEUS through its GATT service and checks the whole command
vocabulary documented in firmware/MORPHEUS/ble_control.cpp, its error paths,
robustness (oversize/malformed input, bursts, reconnect cycles) and latency.

Safety: nothing is flashed. The only persistent setting touched is the keyer WPM,
changed by +1 and restored; training/game sessions are started and stopped; the
virtual key is pulsed briefly. A cleanup step stops sessions, releases the key and
verifies the original WPM. Needs exclusive use of the device (one BLE connection).
"""
import asyncio
import json
import re
import statistics
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ble_client"))
import protocol as proto  # noqa: E402

FW_VERSION = re.search(r'FIRMWARE_VERSION\[\]\s*=\s*"([^"]+)"', (ROOT / "firmware/MORPHEUS/config.h").read_text()).group(1)
MODES = ["KOCH", "CHARACTERS", "WORDS", "CALLSIGNS", "ADAPTIVE", "EXAM", "LISTENING", "COMBINED"]
GAMES = ["COPY", "MEMORY", "SPEED"]


class Dev:
    def __init__(self, address):
        self.address, self.client, self.q, self.words = address, None, asyncio.Queue(), asyncio.Queue()
        self.dropped = 0

    async def connect(self):
        self.stage = "scan"
        dev = await BleakScanner.find_device_by_address(self.address, timeout=15)
        if dev is None:
            raise RuntimeError("device not found (is it advertising and not connected elsewhere?)")
        self.client = BleakClient(dev, timeout=30)
        self.stage = "connect"
        await self.client.connect()
        self.stage = "subscribe control events (CCCD write)"
        await self.client.start_notify(proto.CONTROL_EVT_UUID, lambda _c, d: self.q.put_nowait(json.loads(bytes(d))))
        self.stage = "subscribe word characteristic (CCCD write)"
        await self.client.start_notify(proto.WORD_CHAR_UUID, lambda _c, d: self.words.put_nowait(json.loads(bytes(d))))
        self.stage = "first request/read"

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    async def write(self, payload):
        raw = payload if isinstance(payload, (bytes, bytearray)) else json.dumps(payload, separators=(",", ":")).encode()
        await self.client.write_gatt_char(proto.CONTROL_CMD_UUID, raw, response=True)

    async def read_evt(self):
        return json.loads(bytes(await self.client.read_gatt_char(proto.CONTROL_EVT_UUID)))

    async def wait(self, pred, timeout=3.0, poll=0.4):
        """First event matching pred, from notifications; falls back to reading the
        characteristic (notify is best-effort, the value is not)."""
        end, next_poll = time.monotonic() + timeout, time.monotonic() + poll
        while time.monotonic() < end:
            try:
                evt = await asyncio.wait_for(self.q.get(), 0.1)
                if pred(evt):
                    return evt
            except asyncio.TimeoutError:
                pass
            if time.monotonic() >= next_poll:
                next_poll = time.monotonic() + poll
                evt = await self.read_evt()
                if pred(evt):
                    return evt
        return None

    async def cmd(self, payload, pred, timeout=3.0):
        self.drain()
        await self.write(payload)
        return await self.wait(pred, timeout)

    async def info(self):
        return await self.cmd({"cmd": "get_device_info"}, lambda e: e.get("evt") == "device_info")


ack = lambda name, ok=True: (lambda e: e.get("evt") == "ack" and e.get("cmd") == name and e.get("ok") is ok)  # noqa: E731
err = lambda text: (lambda e: e.get("evt") == "error" and text in e.get("message", ""))  # noqa: E731
state = lambda evt, **kv: (lambda e: e.get("evt") == evt and all(e.get(k) == v for k, v in kv.items()))  # noqa: E731

RESULTS = []


def record(tid, name, ok, detail):
    RESULTS.append({"id": tid, "name": name, "pass": bool(ok), "detail": detail})
    print(f"{'PASS' if ok else 'FAIL'}  {tid}  {name}: {detail}", flush=True)


async def main(address, out):
    d = Dev(address)
    await d.connect()
    original_wpm = None
    try:
        # SYS-01 device info schema, ranges and version
        i = await d.info()
        original_wpm = i and i.get("wpm")
        ok = bool(i) and i.get("firmwareVersion") == FW_VERSION and 5 <= i["wpm"] <= 40 and 200 <= i["sidetoneHz"] <= 2000 \
            and 0 <= i["volume"] <= 100 and i["mode"] in ("PADDLE", "STRAIGHT") and i["iambicMode"] in ("IAMBIC_A", "IAMBIC_B") \
            and 30 <= i["weightPercent"] <= 70 and isinstance(i["sidetoneEnabled"], bool) and isinstance(i["paddleReversed"], bool)
        record("SYS-01", "device_info schema, ranges, version == config.h", ok, json.dumps(i))

        # SYS-02 GATT surface
        svc = d.client.services.get_service(proto.SERVICE_UUID)
        props = {c.uuid: set(c.properties) for c in svc.characteristics} if svc else {}
        want = {proto.WORD_CHAR_UUID: {"notify"}, proto.CONTROL_CMD_UUID: {"write"}, proto.CONTROL_EVT_UUID: {"notify", "read"}}
        ok = svc is not None and all(w <= props.get(u, set()) for u, w in want.items())
        record("SYS-02", "GATT service + characteristic properties", ok, str({k[4:8]: sorted(v) for k, v in props.items()}))

        # SYS-03 training lifecycle, every mode
        bad = []
        for m in MODES:
            a = await d.cmd({"cmd": "train_start", "mode": m}, state("train_state", active=True, mode=m))
            b = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
            if not (a and b):
                bad.append(m)
        record("SYS-03", "train_start/train_stop for all 8 modes", not bad, "failed: " + ",".join(bad) if bad else "8/8 modes start and stop")

        # SYS-04 Koch pool bounds
        res = []
        for level in (2, 12, 40):
            # The control characteristic holds only the latest event, so an ack is
            # routinely overwritten by the state push that follows it: confirm by state.
            res.append(bool(await d.cmd({"cmd": "train_start", "mode": "KOCH", "kochLevel": level},
                                        state("train_state", active=True, mode="KOCH", kochLevel=level))))
            await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
        for level in ("1", "41", '"x"', "-3", "2.5"):
            raw = b'{"cmd":"train_start","mode":"KOCH","kochLevel":' + level.encode() + b'}'
            d.drain(); await d.write(raw)
            res.append(bool(await d.wait(err("invalid Koch pool level"))))
        record("SYS-04", "Koch pool 2/12/40 accepted; 1/41/\"x\"/-3/2.5 rejected", all(res), str(res))

        # SYS-05 negative commands
        cases = [({"x": 1}, "missing cmd"), ({"cmd": "bogus"}, "unknown cmd"), ({"cmd": "train_start", "mode": "NOPE"}, "bad or missing mode"),
                 ({"cmd": "train_start"}, "bad or missing mode"), ({"cmd": "game_start", "game": "NOPE"}, "bad or missing game"),
                 ({"cmd": "train_answer"}, "missing text"), ({"cmd": "set_keyer", "field": "wpm", "value": 41}, "invalid keyer setting"),
                 ({"cmd": "set_keyer", "field": "nope", "value": 1}, "invalid keyer setting"),
                 ({"cmd": "set_keyer", "field": "wpm", "value": -1}, "invalid keyer setting"),
                 ({"cmd": "probe_keyer", "id": "bad id!"}, "invalid probe id"), ({"cmd": "probe_keyer"}, "invalid probe id")]
        fails = []
        for payload, text in cases:
            if not await d.cmd(payload, err(text)):
                fails.append(f"{payload} -> expected '{text}'")
        d.drain(); await d.write(b"{bad")
        if not await d.wait(err("missing cmd")):
            fails.append("malformed JSON")
        record("SYS-05", f"{len(cases) + 1} invalid commands return the documented error", not fails, "; ".join(fails) or "all rejected with the documented message")

        # SYS-06 oversize command
        alive = False
        try:
            await d.write(b'{"cmd":"get_device_info","pad":"' + b"x" * 120 + b'"}')
            detail = "oversize write accepted by link and ignored"
        except Exception as e:  # noqa: BLE001
            detail = f"oversize write refused: {type(e).__name__}"
        alive = bool(await d.info())
        record("SYS-06", "command >= 96 bytes does not break the device", alive, detail + "; device still answers")

        # SYS-07 games + exclusivity
        probs = []
        for g in GAMES:
            if not await d.cmd({"cmd": "game_start", "game": g}, state("game_state", active=True, game=g)):
                probs.append(f"start {g}")
            p1 = await d.cmd({"cmd": "game_pause"}, lambda e: e.get("evt") == "game_state" and e.get("paused") is True)
            p2 = await d.cmd({"cmd": "game_pause"}, lambda e: e.get("evt") == "game_state" and e.get("paused") is False)
            if not (p1 and p2):
                probs.append(f"pause {g}")
            if not await d.cmd({"cmd": "game_stop"}, state("game_state", active=False)):
                probs.append(f"stop {g}")
        await d.cmd({"cmd": "game_start", "game": "COPY"}, ack("game_start"))
        if not await d.cmd({"cmd": "train_start", "mode": "WORDS"}, state("train_state", active=True)):
            probs.append("trainer did not start over game")
        g = await d.wait(lambda e: e.get("evt") == "game_state", 1.0)
        await d.cmd({"cmd": "train_stop"}, ack("train_stop"))
        await d.cmd({"cmd": "game_stop"}, ack("game_stop"))
        await d.cmd({"cmd": "train_start", "mode": "WORDS"}, state("train_state", active=True))
        started = await d.cmd({"cmd": "game_start", "game": "COPY"}, state("game_state", active=True), 1.5)
        trainer = await d.cmd({"cmd": "get_device_info"}, lambda e: e.get("evt") == "train_state", 0.8)
        if started:
            probs.append("game_start during training was NOT refused")
        await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
        await d.cmd({"cmd": "game_stop"}, state("game_state", active=False), 1.0)
        record("SYS-07", "games start/pause/stop; trainer<->game exclusivity", not probs, "; ".join(probs) or "COPY/MEMORY/SPEED ok; exclusivity enforced both ways")

        # SYS-08 set_keyer: change, confirm, busy refusal, restore
        probs = []
        new = original_wpm + 1 if original_wpm < 40 else original_wpm - 1
        r = await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": new}, lambda e: e.get("evt") == "device_info" and e.get("wpm") == new)
        if not r:
            probs.append("wpm change not confirmed")
        await d.cmd({"cmd": "train_start", "mode": "WORDS"}, ack("train_start"))
        if not await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, err("keyer busy")):
            probs.append("no 'keyer busy' during training")
        await d.cmd({"cmd": "train_stop"}, ack("train_stop"))
        r = await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, lambda e: e.get("evt") == "device_info" and e.get("wpm") == original_wpm)
        if not r:
            probs.append("wpm not restored")
        record("SYS-08", f"set_keyer wpm {original_wpm}->{new}->{original_wpm}; refused while training", not probs, "; ".join(probs) or "confirmed, refused when busy, restored")

        # SYS-09 metrics correlation
        ids = [f"id{n}x{int(time.time()) % 1000}" for n in range(20)]
        got = 0
        for pid in ids:
            e = await d.cmd({"cmd": "probe_keyer", "id": pid}, lambda e, pid=pid: e.get("evt") == "keyer_metrics" and e.get("id") == pid, 2.0)
            got += bool(e)
        record("SYS-09", "20 probe_keyer ids each echoed back (correlation)", got == 20, f"{got}/20")

        # SYS-10 key_down/key_up, stray double down, no stuck key
        await d.write({"cmd": "key_down"}); await d.write({"cmd": "key_down"}); await asyncio.sleep(0.12)
        await d.write({"cmd": "key_up"}); await asyncio.sleep(0.4)
        r = await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, lambda e: e.get("evt") in ("device_info", "error"))
        record("SYS-10", "double key_down then key_up leaves the key released", bool(r) and r.get("evt") == "device_info", "set_keyer not refused as busy" if r and r.get("evt") == "device_info" else str(r))

        # SYS-15 a keyed element is decoded and reported as a word. A dah (240 ms) is used:
        # every write is acknowledged before the next, so BLE cannot space a key-down and
        # key-up closer than one round trip (see SYS-16).
        while not d.words.empty():
            d.words.get_nowait()
        await d.write({"cmd": "key_down"}); await asyncio.sleep(0.24); await d.write({"cmd": "key_up"})
        word = None
        end = time.monotonic() + 6
        while time.monotonic() < end and not word:
            try:
                w = await asyncio.wait_for(d.words.get(), 0.5)
                if w.get("word"):
                    word = w
            except asyncio.TimeoutError:
                pass
        record("SYS-15", "one virtual dah is decoded and notified as word 'T'", bool(word) and word.get("word") == "T", json.dumps(word))

        # SYS-16 shortest element the BLE virtual key can produce (measurement)
        meas = []
        for want in (30, 70, 150, 300):
            await d.cmd({"cmd": "reset_keyer_metrics", "id": f"r{want}"}, lambda e: e.get("evt") == "keyer_metrics", 2.0)
            await d.write({"cmd": "key_down"}); await asyncio.sleep(want / 1000); await d.write({"cmd": "key_up"})
            await asyncio.sleep(0.8)
            m = await d.cmd({"cmd": "probe_keyer", "id": f"p{want}"}, lambda e: e.get("evt") == "keyer_metrics" and e.get("id") == f"p{want}", 2.0)
            got = (m or {}).get("ditMs") or (m or {}).get("dahMs")
            meas.append((want, got))
        ok = all(g is not None and w <= g <= w + 250 for w, g in meas)
        record("SYS-16", "requested hold vs element the device measured (ms)", ok,
               ", ".join(f"{w}->{g}" for w, g in meas) + "  (floor ~ one ATT round trip)")

        # SYS-11 burst of 100 back-to-back requests
        d.drain()
        t0 = time.monotonic()
        for _ in range(100):
            await d.write({"cmd": "get_device_info"})
        dt = time.monotonic() - t0
        await asyncio.sleep(1.5)
        seen = d.q.qsize()
        alive = bool(await d.info())
        record("SYS-11", "100 requests back-to-back; device stays alive", alive, f"sent in {dt:.1f}s ({100 / dt:.0f}/s); {seen} notifications delivered (notify is best-effort)")

        # SYS-12 request/notify round-trip latency
        lat, lost = [], 0
        for _ in range(100):
            t = time.monotonic()
            e = await d.cmd({"cmd": "get_device_info"}, lambda e: e.get("evt") == "device_info", 2.0)
            if e:
                lat.append((time.monotonic() - t) * 1000)
            else:
                lost += 1
        lat.sort()
        pct = lambda p: lat[min(len(lat) - 1, int(len(lat) * p))] if lat else float("nan")  # noqa: E731
        record("SYS-12", "get_device_info round trip x100", lost == 0 and pct(0.95) < 1000,
               f"p50 {pct(.5):.0f} ms, p95 {pct(.95):.0f} ms, max {lat[-1] if lat else 0:.0f} ms, lost {lost}")

        # ------------------------------------------------------------------
        # NEGATIVE tests (NEG-D01..D13): wrong state, bad types, boundaries, hostile
        # strings. After each group the device must still answer and keep its state.
        # ------------------------------------------------------------------
        async def healthy():
            i = await d.info()
            return bool(i) and i.get("wpm") == original_wpm

        async def silent(payload, secs=1.2):
            """True if the device sends NO event for this write."""
            d.drain()
            await d.write(payload)
            return await d.wait(lambda e: True, secs, poll=10) is None

        # NEG-D01 commands that make no sense in the current state
        await d.write({"cmd": "train_stop"}); await d.write({"cmd": "game_stop"}); await d.write({"cmd": "game_pause"})
        await d.write({"cmd": "game_confirm"}); await d.write({"cmd": "train_confirm"}); await d.write({"cmd": "train_answer", "text": "A"})
        await d.write({"cmd": "key_up"}); await d.write({"cmd": "game_restart"}); await asyncio.sleep(0.6)
        tr = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
        gm = await d.cmd({"cmd": "game_stop"}, state("game_state", active=False))
        record("NEG-D01", "stop/pause/confirm/answer/restart/key_up with nothing active are harmless", bool(tr) and bool(gm) and await healthy(), "device alive, nothing started")

        # NEG-D02 duplicate and conflicting starts
        await d.cmd({"cmd": "train_start", "mode": "WORDS"}, state("train_state", active=True, mode="WORDS"))
        again = await d.cmd({"cmd": "train_start", "mode": "WORDS"}, state("train_state", active=True, mode="WORDS"))
        sw = await d.cmd({"cmd": "train_start", "mode": "CHARACTERS"}, state("train_state", active=True))
        stopped = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
        record("NEG-D02", "start twice / start another mode while active leaves one consistent session that stops cleanly", bool(again) and bool(sw) and bool(stopped) and await healthy(),
               f"second start -> {again and again.get('mode')}, other mode -> {sw and sw.get('mode')}")

        # NEG-D03 wrong value types and shapes for set_keyer
        bad_values = [b'"20"', b"20.5", b"true", b"null", b"99999999999", b"-5", b"[20]", b"{}", b"1e2", b"0x14", b"20abc"]
        leaked = []
        for v in bad_values:
            d.drain(); await d.write(b'{"cmd":"set_keyer","field":"wpm","value":' + v + b"}")
            if not await d.wait(err("invalid keyer setting"), 1.5):
                leaked.append(v.decode())
        d.drain(); await d.write(b'{"cmd":"set_keyer","field":"wpm"}')
        if not await d.wait(err("invalid keyer setting"), 1.5): leaked.append("<no value>")
        d.drain(); await d.write(b'{"cmd":"set_keyer","value":20}')
        if not await d.wait(err("invalid keyer setting"), 1.5): leaked.append("<no field>")
        record("NEG-D03", f"{len(bad_values) + 2} malformed set_keyer values/shapes rejected; WPM unchanged", not leaked and await healthy(), "not rejected: " + ",".join(leaked) if leaked else "all rejected")

        # NEG-D04 field names are exact
        leaked = []
        for f in ("WPM", " wpm", "wpm ", "Wpm", "wp", "wpmm", "", "tone\u0000"):
            d.drain(); await d.write({"cmd": "set_keyer", "field": f, "value": 20})
            if not await d.wait(err("invalid keyer setting"), 1.5): leaked.append(repr(f))
        record("NEG-D04", "set_keyer field names must match exactly (case, spaces, prefixes, empty)", not leaked and await healthy(), "accepted: " + ",".join(leaked) if leaked else "all rejected")

        # NEG-D05 every range limit, just outside (rejections only: no persistent change)
        outside = [("wpm", 4), ("wpm", 41), ("tone", 199), ("tone", 2001), ("volume", -1), ("volume", 101), ("weight", 29), ("weight", 71),
                   ("mode", 2), ("iambic", 2), ("reversed", 2), ("sidetone", 2)]
        leaked = []
        for f, v in outside:
            d.drain(); await d.write({"cmd": "set_keyer", "field": f, "value": v})
            if not await d.wait(err("invalid keyer setting"), 1.5): leaked.append(f"{f}={v}")
        record("NEG-D05", f"{len(outside)} out-of-range settings (one past each limit) rejected", not leaked and await healthy(), "accepted: " + ",".join(leaked) if leaked else "all rejected")

        # NEG-D06 command size boundary: 95 bytes accepted, 96 and 97 ignored
        def sized(n):
            base = b'{"cmd":"get_device_info","p":""}'
            return base[:-2] + b"x" * (n - len(base)) + b'"}'
        ok95 = bool(await d.cmd(sized(95), lambda e: e.get("evt") == "device_info", 2.0))
        ig96, ig97 = await silent(sized(96)), await silent(sized(97))
        record("NEG-D06", "command size cap: 95 B answered, 96 B and 97 B ignored", ok95 and ig96 and ig97 and await healthy(), f"95B answered={ok95}, 96B ignored={ig96}, 97B ignored={ig97}")

        # NEG-D07 degenerate writes
        outcomes = []
        for raw in (b"{", b"}", b"\x00", b"\xff\xfe", b"null", b"[]", b'""', b"0"):
            try:
                d.drain(); await d.write(raw); outcomes.append("ok")
            except Exception as e:  # noqa: BLE001
                outcomes.append(type(e).__name__)
        try:
            await d.write(b""); outcomes.append("empty ok")
        except Exception as e:  # noqa: BLE001
            outcomes.append("empty refused by link")
        record("NEG-D07", "degenerate writes ({ } NUL 0xFF null [] \"\" 0, empty) do not disturb the device", await healthy(), ", ".join(sorted(set(outcomes))))

        # NEG-D08 injection-style strings are inert
        hostile = [{"cmd": "train_start", "mode": 'WORDS"}'}, {"cmd": "train_start", "mode": "WORDS\u0000"}, {"cmd": "game_start", "game": "COPY\n"},
                   {"cmd": 'get_device_info","x":"'}, {"cmd": "train_start", "mode": "../../etc/passwd"}, {"cmd": "train_start", "mode": "%s%s%s%n"},
                   {"cmd": "\u202ekey_down"}, {"cmd": "train_start", "mode": "WORDS", "cmd2": "game_start"}]
        for h in hostile:
            d.drain(); await d.write(h); await asyncio.sleep(0.15)
        await asyncio.sleep(0.5)
        tr = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False)); gm = await d.cmd({"cmd": "game_stop"}, state("game_state", active=False))
        record("NEG-D08", f"{len(hostile)} hostile strings (quote/NUL/format/unicode-override/path) are inert", bool(tr) and bool(gm) and await healthy(), "no session left running")

        # NEG-D09 probe id edges
        cases = [("a" * 24, True), ("A1b2C3", True), ("a" * 25, False), ("", False), ("id-1", False), ("id 1", False), ("\u00e9", False), ("x" * 100, False)]
        bad = []
        for pid, valid in cases:
            if valid:
                got = await d.cmd({"cmd": "probe_keyer", "id": pid}, lambda e, pid=pid: e.get("evt") == "keyer_metrics" and e.get("id") == pid, 2.0)
                if not got: bad.append(f"valid id '{pid[:6]}' not answered")
            else:
                d.drain(); await d.write({"cmd": "probe_keyer", "id": pid})
                if not await d.wait(err("invalid probe id"), 1.5): bad.append(f"invalid id {pid[:6]!r} accepted")
        record("NEG-D09", "probe id: 24-char/alphanumeric accepted; 25+ chars, empty, punctuation, space, non-ASCII rejected", not bad and await healthy(), "; ".join(bad) or "all as specified")

        # NEG-D10 oversized/odd answer text with no session
        await d.write({"cmd": "train_answer", "text": "A" * 40}); await d.write({"cmd": "train_answer", "text": ""}); await d.write({"cmd": "train_answer", "text": "\u00e9"})
        await asyncio.sleep(0.5)
        record("NEG-D10", "train_answer with long, empty or non-ASCII text and no session is harmless", await healthy(), "device alive")

        # NEG-D11 rapid contradictory commands, no waiting between them
        for _ in range(20):
            await d.write({"cmd": "train_start", "mode": "WORDS"}); await d.write({"cmd": "train_stop"})
            await d.write({"cmd": "game_start", "game": "COPY"}); await d.write({"cmd": "game_stop"})
        await asyncio.sleep(1.0)
        tr = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False)); gm = await d.cmd({"cmd": "game_stop"}, state("game_state", active=False))
        record("NEG-D11", "80 rapid start/stop commands in a row settle to a clean idle state", bool(tr) and bool(gm) and await healthy(), "device alive, idle")

        # NEG-D12 enum values are exact
        bad = []
        for payload, text in (({"cmd": "game_start", "game": "copy"}, "bad or missing game"), ({"cmd": "game_start", "game": "Copy"}, "bad or missing game"),
                              ({"cmd": "game_start", "game": ""}, "bad or missing game"), ({"cmd": "train_start", "mode": "words"}, "bad or missing mode"),
                              ({"cmd": "train_start", "mode": ""}, "bad or missing mode"), ({"cmd": "train_start", "mode": "WORD"}, "bad or missing mode")):
            if not await d.cmd(payload, err(text), 1.5): bad.append(str(payload))
        record("NEG-D12", "mode and game names are case- and length-exact", not bad and await healthy(), "; ".join(bad) or "all rejected")

        # NEG-D13 stray key traffic cannot leave the key stuck
        for _ in range(10):
            await d.write({"cmd": "key_up"})
        for _ in range(10):
            await d.write({"cmd": "key_down"})
        await d.write({"cmd": "key_up"}); await asyncio.sleep(0.6)
        r = await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, lambda e: e.get("evt") in ("device_info", "error"))
        record("NEG-D13", "10 stray key_up, 10 key_down then one key_up leaves the key released", bool(r) and r.get("evt") == "device_info", "settings not refused as busy" if r and r.get("evt") == "device_info" else str(r))

        # NEG-D14 link loss while the virtual key is held must not leave it stuck
        await d.write({"cmd": "key_down"}); await asyncio.sleep(0.3)
        await d.client.disconnect()                      # the key-up can never be sent
        await asyncio.sleep(2.0)
        for attempt in range(5):                         # reconnect (FND-01 can need a retry)
            try:
                await d.connect(); break
            except Exception:  # noqa: BLE001
                await asyncio.sleep(1.5)
        r = await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, lambda e: e.get("evt") in ("device_info", "error"))
        released = bool(r) and r.get("evt") == "device_info"
        await d.write({"cmd": "key_up"}); await asyncio.sleep(0.3)   # always clean up
        record("NEG-D14", "link loss with the key held leaves the key released", released,
               "released" if released else f"KEY STUCK after link loss: device answers '{(r or {}).get('message')}' until key_up (FND-03)")

        # SYS-13 disconnect/reconnect cycles
        times, bad = [], 0
        for _ in range(10):
            await d.disconnect(); await asyncio.sleep(1.0)
            t = time.monotonic()
            try:
                await d.connect()
                ok = bool(await d.info())
                times.append(time.monotonic() - t)
                bad += not ok
            except Exception as e:  # noqa: BLE001
                bad += 1
                print("   reconnect error:", e)
        record("SYS-13", "10 disconnect/reconnect cycles on the bonded device", bad == 0 and len(times) == 10,
               f"{10 - bad}/10 ok; connect+info {min(times):.1f}-{max(times):.1f} s" if times else "no successful cycle")
    finally:
        # SYS-14 cleanup: leave the device as found
        try:
            if not (d.client and d.client.is_connected):
                await d.connect()
            await d.write({"cmd": "key_up"})
            await d.cmd({"cmd": "train_stop"}, ack("train_stop"))
            await d.cmd({"cmd": "game_stop"}, ack("game_stop"))
            if original_wpm is not None:
                await d.cmd({"cmd": "set_keyer", "field": "wpm", "value": original_wpm}, lambda e: e.get("evt") == "device_info")
            i = await d.info()
            tr = await d.cmd({"cmd": "train_stop"}, state("train_state", active=False))
            record("SYS-14", "device left as found (no session, key up, WPM restored)", bool(i) and i.get("wpm") == original_wpm and bool(tr),
                   f"wpm {i.get('wpm') if i else None} (original {original_wpm}), training inactive={bool(tr)}")
        finally:
            await d.disconnect()
    n_ok = sum(r["pass"] for r in RESULTS)
    print(f"\n{n_ok}/{len(RESULTS)} passed")
    if out:
        Path(out).write_text(json.dumps(RESULTS, indent=2))
    return 0 if n_ok == len(RESULTS) else 1


async def reconnect_stress(address, cycles, delay):
    ok, errs, times = 0, {}, []
    d = Dev(address)
    for n in range(cycles):
        t = time.monotonic()
        try:
            await d.connect()
            if await d.info():
                ok += 1
                times.append(time.monotonic() - t)
            else:
                errs["no device_info"] = errs.get("no device_info", 0) + 1
        except Exception as e:  # noqa: BLE001
            k = f"at '{d.stage}': {type(e).__name__}: {str(e)[:60]}"
            errs[k] = errs.get(k, 0) + 1
        try:
            await d.disconnect()
        except Exception:  # noqa: BLE001
            pass
        await asyncio.sleep(delay)
    times.sort()
    print(f"reconnect x{cycles}, {delay}s apart: {ok} ok, {cycles - ok} failed; "
          f"connect+info {times[0]:.1f}-{times[-1]:.1f}s p50 {times[len(times) // 2]:.1f}s" if times else "none ok")
    for k, v in errs.items():
        print(f"   {v} x {k}")
    return 0 if ok == cycles else 1


async def soak(address, minutes):
    """Hold one connection for `minutes`, asking for device info every 30 s."""
    d = Dev(address)
    await d.connect()
    replies = failures = 0
    lat = []
    end = time.monotonic() + minutes * 60
    try:
        while time.monotonic() < end:
            t = time.monotonic()
            if not d.client.is_connected:
                failures += 1
                print("connection dropped", flush=True)
                break
            if await d.info():
                replies += 1
                lat.append((time.monotonic() - t) * 1000)
            else:
                failures += 1
            await asyncio.sleep(30)
    finally:
        await d.disconnect()
    lat.sort()
    print(f"SOAK RESULT: {minutes} min, replies {replies}, failures {failures}, "
          f"reply ms p50 {lat[len(lat) // 2] if lat else '-'} max {lat[-1] if lat else '-'}")
    return 0 if failures == 0 and replies >= max(1, minutes * 2 - 1) else 1


if __name__ == "__main__":
    if sys.argv[1] == "--soak":
        sys.exit(asyncio.run(soak(sys.argv[2], float(sys.argv[3]))))
    if sys.argv[1] == "--reconnect-stress":
        sys.exit(asyncio.run(reconnect_stress(sys.argv[2], int(sys.argv[3]), float(sys.argv[4]))))
    addr = sys.argv[1]
    out = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    sys.exit(asyncio.run(main(addr, out)))
