"""
BLE worker: runs bleak (asyncio) on a background thread, talks to the
GUI thread only through Qt signals. Qt auto-queues signal delivery
across threads as long as this QObject stays on the main thread (it
does; only the async work runs elsewhere), so no locking is needed on
the GUI side.
"""

import asyncio
import json
import threading

from PySide6.QtCore import QObject, Signal

from bleak import BleakClient, BleakScanner

import protocol as proto


class BleWorker(QObject):
    status_changed = Signal(str)
    connected_changed = Signal(bool)
    device_info = Signal(str, str)
    word_received = Signal(dict)
    train_state_received = Signal(dict)
    game_state_received = Signal(dict)
    command_ack = Signal(str, bool)
    command_error = Signal(str)
    error = Signal(str)

    def __init__(self):
        super().__init__()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._client: BleakClient | None = None
        self._stop_requested = False

    # ------------------------------------------------------------------
    def start(self, address: str = ""):
        if self._thread and self._thread.is_alive():
            return
        self._stop_requested = False
        self._thread = threading.Thread(
            target=self._run, args=(address.strip() or None,), daemon=True
        )
        self._thread.start()

    def stop(self):
        self._stop_requested = True
        if self._loop is not None:
            asyncio.run_coroutine_threadsafe(self._async_stop(), self._loop)

    def send_command(self, command: dict):
        """Fire-and-forget: writes JSON to the control-cmd characteristic.
        Safe to call from the GUI thread; the actual write happens on the
        worker's own event loop.
        """
        if self._loop is None or self._client is None:
            self.error.emit("Not connected")
            return
        asyncio.run_coroutine_threadsafe(self._async_send_command(command), self._loop)

    # ------------------------------------------------------------------
    async def _async_stop(self):
        if self._client is not None and self._client.is_connected:
            await self._client.disconnect()

    async def _async_send_command(self, command: dict):
        if self._client is None or not self._client.is_connected:
            self.error.emit("Not connected")
            return
        try:
            # Firmware's jsonGetString() (ble_control.cpp) matches the
            # literal pattern "key":" with no space after the colon -
            # json.dumps()'s default ": " separator would silently fail
            # to match, so every command's "cmd" field would come back
            # as "missing cmd". Compact separators avoid that.
            payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
            await self._client.write_gatt_char(proto.CONTROL_CMD_UUID, payload, response=True)
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Command send failed: {exc}")

    def _run(self, address: str | None):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._async_main(address))
        except Exception as exc:  # noqa: BLE001
            self.error.emit(str(exc))
        finally:
            self.connected_changed.emit(False)
            self._loop.close()
            self._loop = None

    def _on_word_notify(self, _characteristic, data: bytearray):
        self._dispatch_json(data, self.word_received, expect_evt=None)

    def _on_control_notify(self, _characteristic, data: bytearray):
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Malformed control payload: {exc}")
            return
        evt = payload.get("evt")
        if evt == "train_state":
            self.train_state_received.emit(payload)
        elif evt == "game_state":
            self.game_state_received.emit(payload)
        elif evt == "ack":
            self.command_ack.emit(payload.get("cmd", ""), bool(payload.get("ok")))
        elif evt == "error":
            self.command_error.emit(payload.get("message", "unknown error"))

    def _dispatch_json(self, data: bytearray, signal, expect_evt):
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            self.error.emit(f"Malformed payload: {exc}")
            return
        if expect_evt is None or payload.get("evt") == expect_evt:
            signal.emit(payload)

    async def _async_main(self, address: str | None):
        self.status_changed.emit("Scanning...")
        device = None
        if address:
            device = await BleakScanner.find_device_by_address(address, timeout=proto.SCAN_TIMEOUT_S)
        else:
            device = await BleakScanner.find_device_by_filter(
                lambda d, _adv: d.name == proto.DEVICE_NAME, timeout=proto.SCAN_TIMEOUT_S
            )

        if self._stop_requested:
            return
        if device is None:
            self.error.emit(
                f"{proto.DEVICE_NAME} not found in {proto.SCAN_TIMEOUT_S:.0f}s. "
                "Is BLE enabled and advertising on the device "
                "(Connectivity > Bluetooth > BLE toggle)?"
            )
            self.status_changed.emit("Not found")
            return

        self.status_changed.emit(f"Connecting to {device.address}...")
        try:
            async with BleakClient(device) as client:
                self._client = client
                self.connected_changed.emit(True)
                self.device_info.emit(device.name or proto.DEVICE_NAME, device.address)
                self.status_changed.emit(f"Connected: {device.name} ({device.address})")
                await client.start_notify(proto.WORD_CHAR_UUID, self._on_word_notify)
                await client.start_notify(proto.CONTROL_EVT_UUID, self._on_control_notify)
                while client.is_connected and not self._stop_requested:
                    await asyncio.sleep(0.5)
        except Exception as exc:  # noqa: BLE001
            self.error.emit(str(exc))
        finally:
            self._client = None
            self.status_changed.emit("Disconnected")
