#!/usr/bin/env python3
"""Live OLED screenshot over BLE - like screencap.py, but never reboots
the board: connecting over BLE never touches the reset pins the way
opening the USB-serial port does (see the old screencap.py's module
docstring, in git history, for that whole story).

Requires the firmware built with FEATURE_DEBUG_SERIAL_COMMANDS=1
(config.h) and FEATURE_BLE=1 (on by default) - the "dump_screen_start"/
"dump_screen_chunk" BLE commands are gated by the same debug flag as
the removed serial path; see ble_control.cpp's file header comment for
the wire format and why it's request-per-chunk rather than a push.

Every connect makes the firmware show its "BLE PAIRED" confirmation
overlay for ~2.5s (UI_BLE_OVERLAY_MS) - onAuthenticationComplete() fires
on any connection reaching a secure/bonded state, not just a brand-new
pairing, so a plain reconnect triggers it too. Single-shot mode below
waits out that overlay before capturing, on every run. For more than
one screenshot in a sitting, --session pays that wait once and reuses
the same connection - see its docstring below for why that's the
better default over "just add a delay and reconnect every time".

Usage:
    python3 screencap_ble.py -o screenshot.png
    python3 screencap_ble.py --session                  # interactive: Enter to capture, q to quit
    python3 screencap_ble.py --session --interval 5      # auto-capture every 5s until Ctrl-C
"""
import argparse
import asyncio
import re
import sys
import time

from bleak import BleakClient, BleakScanner
from PIL import Image

# Must match firmware/MORPHEUS/config.h exactly (also mirrored in
# tools/ble_client/protocol.py, the production BLE bridge this script
# deliberately does NOT depend on - this is a standalone bench tool).
SERVICE_UUID = "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b"
CONTROL_CMD_UUID = "7a48a2b0-0003-4ad4-9f1a-1c2d3e4f5a6b"
CONTROL_EVT_UUID = "7a48a2b0-0004-4ad4-9f1a-1c2d3e4f5a6b"
DEVICE_NAME = "MORPHEUS-CW"

# firmware/MORPHEUS/ui_config.h's UI_BLE_OVERLAY_MS, plus headroom.
BLE_OVERLAY_SETTLE_S = 3.0

HEADER_RE = re.compile(r'"evt":"screen_dump_start","w":(\d+),"h":(\d+),"bytes":(\d+),"n":(\d+)')
CHUNK_RE = re.compile(r'"evt":"screen_dump_chunk","i":(\d+),"hex":"([0-9A-Fa-f]*)"')


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def make_requester(client: BleakClient, timeout: float, verbose: bool):
    """One request per chunk, not a push burst: BlueZ's D-Bus notification
    delivery coalesces same-characteristic updates sent faster than the
    client processes them (confirmed by hand - a server-push design
    reliably lost every notification but the last, even though the
    firmware's own transport_sendControlEvent() returned true every
    time). Waiting on a per-request asyncio.Event before asking for the
    next chunk guarantees at most one notification is ever in flight -
    this closure is that request/wait pairing, reusable across many
    dumps on one connection."""
    got: dict = {}
    reply = asyncio.Event()

    def on_notify(_handle, data: bytearray):
        text = data.decode("utf-8", errors="replace")
        if verbose: log(f"  < {text}")
        got["text"] = text
        reply.set()

    async def request(cmd: str) -> str:
        reply.clear()
        await client.write_gatt_char(CONTROL_CMD_UUID, cmd.encode())
        await asyncio.wait_for(reply.wait(), timeout=timeout)
        return got["text"]

    return on_notify, request


async def dump_via(request, verbose: bool) -> Image.Image:
    text = await request('{"cmd":"dump_screen_start"}')
    m = HEADER_RE.search(text)
    if not m:
        raise RuntimeError(f"unexpected response to dump_screen_start: {text!r}")
    width, height, expected, n = map(int, m.groups())
    if verbose: log(f"header: {width}x{height}, {expected} bytes in {n} chunks")

    chunks: dict[int, str] = {}
    for i in range(n):
        text = await request(f'{{"cmd":"dump_screen_chunk","i":{i}}}')
        m = CHUNK_RE.search(text)
        if not m or int(m.group(1)) != i:
            raise RuntimeError(f"unexpected response to chunk {i}: {text!r}")
        chunks[i] = m.group(2)

    hex_all = "".join(chunks[i] for i in range(n))
    raw = bytes.fromhex(hex_all)
    if len(raw) != expected:
        raise RuntimeError(f"got {len(raw)} bytes, expected {expected}")

    # Same u8g2_ll_hvline_vertical_top_lsb layout as tools/oled_render.
    img = Image.new("1", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            byte = raw[(y >> 3) * width + x]
            px[x, y] = 255 if (byte >> (y & 7)) & 1 else 0
    return img


async def find(device_name: str, timeout: float, verbose: bool):
    if verbose: log(f"scanning for {device_name!r}...")
    device = await BleakScanner.find_device_by_name(device_name, timeout=timeout)
    if device is None:
        raise RuntimeError(f"no BLE device named {device_name!r} found within {timeout}s")
    return device


async def capture_once(device_name: str, timeout: float, verbose: bool) -> Image.Image:
    device = await find(device_name, timeout, verbose)
    async with BleakClient(device) as client:
        if verbose: log(f"connected to {device.address}")
        on_notify, request = make_requester(client, timeout, verbose)
        await client.start_notify(CONTROL_EVT_UUID, on_notify)
        # Single-shot mode: pay the "BLE PAIRED" overlay's own display
        # time here so the capture lands after it's cleared, since this
        # connection only lives for one screenshot anyway.
        await asyncio.sleep(BLE_OVERLAY_SETTLE_S)
        return await dump_via(request, verbose)


async def session(device_name: str, timeout: float, verbose: bool, out_prefix: str, interval: float | None) -> None:
    """Connects once and reuses that connection for many screenshots.
    Reconnecting per-shot (the naive fix for the "BLE PAIRED" overlay)
    means paying BLE_OVERLAY_SETTLE_S *and* a fresh handshake on every
    single screenshot, and still flashes that overlay on the physical
    device every time. Connecting once pays the settle delay a single
    time; every capture after that is near-instant and never re-triggers
    the overlay, because the connection - and therefore its "just
    authenticated" transition - never happens again."""
    device = await find(device_name, timeout, verbose)
    async with BleakClient(device) as client:
        log(f"connected to {device.address}")
        on_notify, request = make_requester(client, timeout, verbose)
        await client.start_notify(CONTROL_EVT_UUID, on_notify)
        log(f"waiting {BLE_OVERLAY_SETTLE_S}s for the BLE PAIRED overlay to clear...")
        await asyncio.sleep(BLE_OVERLAY_SETTLE_S)

        i = 0
        if interval:
            log(f"auto-capturing every {interval}s - Ctrl-C to stop")
            try:
                while True:
                    img = await dump_via(request, verbose)
                    path = f"{out_prefix}_{i:03d}.png"
                    img.save(path)
                    log(f"saved {path} ({img.width}x{img.height})")
                    i += 1
                    await asyncio.sleep(interval)
            except KeyboardInterrupt:
                pass
        else:
            log("press Enter to capture, 'q' + Enter to quit")
            while True:
                line = await asyncio.to_thread(input, "> ")
                if line.strip().lower() == "q":
                    break
                t0 = time.monotonic()
                img = await dump_via(request, verbose)
                path = f"{out_prefix}_{i:03d}.png"
                img.save(path)
                log(f"saved {path} ({img.width}x{img.height}) in {time.monotonic() - t0:.2f}s")
                i += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--name", default=DEVICE_NAME)
    ap.add_argument("-t", "--timeout", type=float, default=15.0, help="per-BLE-operation timeout, seconds")
    ap.add_argument("-o", "--out", default="screenshot", help="output file (single-shot) or filename prefix (--session)")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--session", action="store_true", help="connect once, capture repeatedly without reconnecting")
    ap.add_argument("--interval", type=float, default=None, help="with --session: auto-capture every N seconds instead of waiting for Enter")
    args = ap.parse_args()

    try:
        if args.session:
            out_prefix = args.out[:-4] if args.out.lower().endswith(".png") else args.out
            asyncio.run(session(args.name, args.timeout, args.verbose, out_prefix, args.interval))
            return
        out = args.out if args.out.lower().endswith(".png") else args.out + ".png"
        img = asyncio.run(capture_once(args.name, args.timeout, args.verbose))
    except (RuntimeError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    img.save(out)
    print(f"saved {out} ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
