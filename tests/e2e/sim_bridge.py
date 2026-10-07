"""Real desktop bridge (ws_server.py + backend.py, unmodified) on a simulated radio.

Only `BleakClient` / `BleakScanner` are replaced, by a device that follows the
firmware's ble_control.cpp command vocabulary (acks, errors, state pushes,
"keyer busy" refusal, range checks, 96-byte command cap). No Bluetooth hardware
is touched and no firmware is flashed. Used by morpheus_ui/test/e2e/.

    python3 sim_bridge.py <port> [--firmware 2.8.4] [--fail-start]

Prints READY once listening. Stdin lines inject device-side input:
    word <TEXT>        completed keyed word (word characteristic)
    drop               link loss (device vanishes)
"""
import asyncio
import json
import sys
import threading
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2] / "tools" / "ble_client"
sys.path.insert(0, str(ROOT))

import backend  # noqa: E402
import protocol as proto  # noqa: E402
import ws_server  # noqa: E402

CMD_CAP = 96
KOCH_MIN, KOCH_MAX = 2, 40
RANGES = {"wpm": (5, 40), "tone": (200, 2000), "volume": (0, 100), "reversed": (0, 1),
          "mode": (0, 1), "iambic": (0, 1), "weight": (30, 70), "sidetone": (0, 1)}
MODES = {"KOCH", "CHARACTERS", "WORDS", "CALLSIGNS", "ADAPTIVE", "EXAM", "LISTENING", "COMBINED"}
GAMES = {"COPY", "MEMORY", "SPEED"}


class SimDevice:
    def __init__(self, firmware: str):
        self.firmware = firmware
        self.cfg = dict(wpm=20, tone=600, volume=80, reversed=0, mode=1, iambic=0, weight=50, sidetone=1)
        self.train = dict(active=False)
        self.game = dict(active=False)
        self.key_down = False
        self.commands = []
        self.fail_start = False
        self.last_evt = {"evt": "device_info", **self._info()}
        self.notify = None  # set by SimClient

    def _info(self):
        c = self.cfg
        return dict(firmwareVersion=self.firmware, wpm=c["wpm"], sidetoneHz=c["tone"],
                    sidetoneEnabled=bool(c["sidetone"]), volume=c["volume"],
                    paddleReversed=bool(c["reversed"]), mode="PADDLE" if c["mode"] else "STRAIGHT",
                    iambicMode="IAMBIC_B" if c["iambic"] else "IAMBIC_A", weightPercent=c["weight"])

    def emit(self, evt):
        self.last_evt = evt
        if self.notify:
            self.notify(bytearray(json.dumps(evt).encode()))

    def handle(self, raw: bytes):
        if len(raw) >= CMD_CAP:
            return  # firmware drops oversized writes
        try:
            cmd = json.loads(raw)
        except ValueError:
            return self.emit({"evt": "error", "message": "missing cmd"})
        name = cmd.get("cmd")
        self.commands.append(cmd)
        if name is None:
            return self.emit({"evt": "error", "message": "missing cmd"})
        ack = lambda ok=True: self.emit({"evt": "ack", "cmd": name, "ok": ok})  # noqa: E731
        if name == "key_down":
            self.key_down = True
        elif name == "key_up":
            self.key_down = False
        elif name == "train_start":
            if cmd.get("mode") not in MODES:
                return self.emit({"evt": "error", "message": "bad or missing mode"})
            level = cmd.get("kochLevel")
            if level is not None and not (isinstance(level, int) and KOCH_MIN <= level <= KOCH_MAX):
                return self.emit({"evt": "error", "message": "invalid Koch pool level"})
            if self.fail_start:
                return self.emit({"evt": "error", "message": "keyer busy"})
            if self.game["active"]:
                self.game = dict(active=False)
                self.emit({"evt": "game_state", **self.game})
            self.train = dict(active=True, mode=cmd["mode"], phase="LISTENING", correct=0, attempts=0,
                              kochLevel=level or 2, wpm=self.cfg["wpm"])
            ack()
            self.emit({"evt": "train_state", **self.train})
        elif name == "train_stop":
            self.train = dict(active=False)
            ack()
            self.emit({"evt": "train_state", **self.train})
        elif name in ("train_confirm", "train_answer"):
            ack()
            self.emit({"evt": "train_state", **self.train})
        elif name == "game_start":
            if cmd.get("game") not in GAMES:
                return self.emit({"evt": "error", "message": "bad or missing game"})
            ok = not self.train["active"]  # silently refused during a trainer session
            if ok:
                self.game = dict(active=True, game=cmd["game"], paused=False, phase="PLAY", highScore=0)
            ack(ok)
            self.emit({"evt": "game_state", **self.game})
        elif name in ("game_stop", "game_pause", "game_confirm", "game_restart"):
            if name == "game_stop":
                self.game = dict(active=False)
            elif name == "game_pause" and self.game["active"]:
                self.game["paused"] = not self.game.get("paused", False)
            elif name == "game_restart" and self.game["active"]:
                self.game.update(phase="PLAY", paused=False)
            ack()
            self.emit({"evt": "game_state", **self.game})
        elif name == "set_keyer":
            field, value = cmd.get("field"), cmd.get("value")
            lo_hi = RANGES.get(field)
            if lo_hi is None or not isinstance(value, int) or not lo_hi[0] <= value <= lo_hi[1]:
                return self.emit({"evt": "error", "message": "invalid keyer setting"})
            if self.key_down or self.train["active"] or self.game["active"]:
                return self.emit({"evt": "error", "message": "keyer busy"})
            self.cfg[field] = value
            ack()
            self.emit({"evt": "device_info", **self._info()})
        elif name == "get_device_info":
            self.emit({"evt": "device_info", **self._info()})
        elif name in ("probe_keyer", "reset_keyer_metrics"):
            self.emit({"evt": "keyer_metrics", "id": cmd.get("id"), "ts": 1000, "seq": 1,
                       "ditMs": 60, "dahMs": 180, "gapMs": 180, "virtual": self.key_down})
        else:
            self.emit({"evt": "error", "message": "unknown cmd"})


DEVICE: SimDevice
BACKEND_REF = {}


class SimClient:
    def __init__(self, _device, **_kw):
        self.is_connected = True
        self.services = SimpleNamespace(get_characteristic=lambda uuid: object())

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        self.is_connected = False

    async def start_notify(self, uuid, callback):
        if str(uuid).lower() == proto.CONTROL_EVT_UUID.lower():
            DEVICE.notify = lambda data: callback(None, data)

    async def read_gatt_char(self, _uuid):
        return bytearray(json.dumps(DEVICE.last_evt).encode())

    async def write_gatt_char(self, _uuid, data, response=True):
        await asyncio.sleep(0.005)  # one link round trip
        DEVICE.handle(bytes(data))

    async def disconnect(self):
        self.is_connected = False


async def find_device(*_a, **_k):
    return SimpleNamespace(name=proto.DEVICE_NAME, address="AA:BB:CC:DD:EE:01")


async def no_prepare(self):
    return None


def stdin_pump(loop, server):
    for line in sys.stdin:
        verb, _, rest = line.strip().partition(" ")
        b = server.backend
        if verb == "word":
            loop_ = b._loop
            if loop_:
                loop_.call_soon_threadsafe(b._on_word_notify, None, bytearray(json.dumps({"word": rest, "wpm": 18}).encode()))
        elif verb == "drop" and b._client is not None:
            b._client.is_connected = False


async def main():
    global DEVICE
    port = int(sys.argv[1])
    firmware = sys.argv[sys.argv.index("--firmware") + 1] if "--firmware" in sys.argv else "2.8.4"
    DEVICE = SimDevice(firmware)
    DEVICE.fail_start = "--fail-start" in sys.argv
    backend.BleakClient = SimClient
    backend.BleakScanner = SimpleNamespace(find_device_by_filter=find_device, find_device_by_address=find_device)
    backend.MorpheusBackend.prepare_connect = no_prepare
    server = ws_server.MorpheusWebSocketServer("127.0.0.1", port)
    threading.Thread(target=stdin_pump, args=(asyncio.get_running_loop(), server), daemon=True).start()
    task = asyncio.create_task(server.serve_forever())
    await asyncio.sleep(0.3)
    print("READY", flush=True)
    await task


if __name__ == "__main__":
    asyncio.run(main())
