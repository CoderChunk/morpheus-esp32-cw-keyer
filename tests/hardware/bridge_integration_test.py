"""Integration test: the real desktop bridge (ws_server.py) against the REAL device.

    python3 tests/hardware/bridge_integration_test.py AA:BB:CC:DD:EE:FF [--json out.json]

Starts ws_server.py itself on a free port, drives it over WebSocket exactly as the
Flutter client does, and stops it afterwards. Needs exclusive use of the device.
Test IDs IT-01 .. IT-10 (see docs/TEST_SUITE.md).
"""
import asyncio
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parents[2]
RESULTS = []


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def rec(tid, name, ok, detail=""):
    RESULTS.append({"id": tid, "name": name, "pass": bool(ok), "detail": detail})
    print(("PASS" if ok else "FAIL"), tid, name, detail, flush=True)


async def run(address):
    port = free_port()
    server = subprocess.Popen([sys.executable, str(ROOT / "tools/ble_client/ws_server.py"), "--port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT / "tools/ble_client")
    try:
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                break
            except OSError:
                await asyncio.sleep(0.1)
        async with websockets.connect(f"ws://127.0.0.1:{port}") as ws:
            events = []

            async def rpc(i, method, params=None, wait=8):
                await ws.send(json.dumps({"id": str(i), "method": method, **({"params": params} if params else {})}))
                end = time.time() + wait
                while time.time() < end:
                    try:
                        m = json.loads(await asyncio.wait_for(ws.recv(), 0.5))
                    except asyncio.TimeoutError:
                        continue
                    if m.get("type") == "response" and m.get("id") == str(i):
                        return m
                    events.append(m)

            async def until(pred, wait=10):
                end = time.time() + wait
                while time.time() < end:
                    for e in events:
                        if pred(e):
                            return e
                    try:
                        events.append(json.loads(await asyncio.wait_for(ws.recv(), 0.5)))
                    except asyncio.TimeoutError:
                        pass

            ev = lambda name, **kv: (lambda e: e.get("event") == name and all(e["data"].get(k) == v for k, v in kv.items()))  # noqa: E731

            await rpc(1, "connect", {"deviceAddress": address}, 10)
            c = await until(ev("connectionChanged", state="CONNECTED"), 25)
            rec("IT-01", "bridge connects to the real device", c, (c or {}).get("data", {}).get("deviceName", ""))
            await rpc(2, "requestDeviceInfo")
            d = await until(lambda e: e.get("event") == "deviceInfoChanged", 8)
            rec("IT-02", "deviceInfoChanged carries the firmware version", d and d["data"].get("firmwareVersion"), json.dumps((d or {}).get("data")))
            await rpc(3, "startTraining", {"mode": "WORDS"})
            rec("IT-03", "startTraining -> trainingStateChanged(active, WORDS)", await until(ev("trainingStateChanged", active=True, mode="WORDS"), 8))
            events.clear(); await rpc(4, "stopTraining")
            rec("IT-04", "stopTraining -> trainingStateChanged(inactive)", await until(ev("trainingStateChanged", active=False), 8))
            await rpc(5, "startGame", {"game": "COPY"})
            rec("IT-05", "startGame COPY -> gameStateChanged(active)", await until(ev("gameStateChanged", active=True, game="COPY"), 8))
            events.clear(); await rpc(6, "stopGame")
            rec("IT-06", "stopGame -> gameStateChanged(inactive)", await until(ev("gameStateChanged", active=False), 8))
            events.clear(); await rpc(7, "probeKeyerMetrics", {"id": "intp1", "reset": False})
            m = await until(lambda e: e.get("event") == "keyerMetricsReceived" and e["data"].get("id") == "intp1", 8)
            rec("IT-07", "probeKeyerMetrics returns a correlated sample with bridge RTT", m and "bleRoundTripMs" in m["data"], json.dumps((m or {}).get("data")))
            r = await rpc(8, "setKeyerSetting", {"field": "wpm", "value": 99})
            rec("IT-08", "out-of-range setKeyerSetting is rejected", r and not r.get("ok"), json.dumps((r or {}).get("error")))
            r = await rpc(9, "getSnapshot")
            rec("IT-09", "getSnapshot reports CONNECTED", r and r.get("ok") and r["result"]["connection"]["state"] == "CONNECTED")
            await rpc(10, "disconnect")
            rec("IT-10", "disconnect -> connectionChanged(DISCONNECTED)", await until(ev("connectionChanged", state="DISCONNECTED"), 15))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    out = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    try:
        asyncio.run(run(sys.argv[1]))
    except Exception as e:  # noqa: BLE001
        rec("IT-00", "bridge integration run completed", False, f"{type(e).__name__}: {e}")
    n = sum(r["pass"] for r in RESULTS)
    print(f"\n{n}/{len(RESULTS)} passed")
    if out:
        Path(out).write_text(json.dumps(RESULTS, indent=2))
    sys.exit(0 if n == len(RESULTS) and n >= 10 else 1)
