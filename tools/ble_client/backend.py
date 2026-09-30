"""
MORPHEUS transport-agnostic backend API.

Implements the contract in MORPHEUS_BACKEND_API_REQUIREMENTS.md: BLE
connection/discovery, keyer telemetry, virtual key commands, training
control, Linux in-process pairing, capability flags, and a structured
error model - independent of any particular UI framework.

This module has no PySide6/Qt import anywhere in it. `MorpheusBackend`
is a plain Python class; consumers subscribe to events with plain
callables via the `on_*` methods below. The existing desktop app
(ble_client_core.py / ble_pairing.py) is a thin Qt-signal adapter on
top of this class - see BACKEND_API.md for how to wire up a different
frontend against the same backend.

Threading model: BLE GATT operations (connect/keyer/training) run on
one background thread with its own asyncio loop; Linux pairing runs on
a second, independent background thread with its own loop (pairing and
the post-pairing GATT session are deliberately separate, exactly as in
the original ble_client_core.py/ble_pairing.py this was extracted
from). Registered callbacks are invoked directly from whichever
background thread produced the event - callers that need to touch a
UI must marshal to their own UI thread themselves (the Qt adapter does
this for free via Qt's queued signal delivery).
"""

import asyncio
import json
import sys
import threading
from dataclasses import dataclass
from enum import Enum
from typing import Callable, List, Optional

from bleak import BleakClient, BleakScanner

import protocol as proto

APPLICATION_VERSION = "1.1.0"


# ---------------------------------------------------------------------------
# Enumerations (string-valued so they serialize identically to the values in
# MORPHEUS_BACKEND_API_REQUIREMENTS.md, whether or not a given consumer ever
# actually serializes them - e.g. the Qt adapter passes them through as-is).
# ---------------------------------------------------------------------------
class ConnectionState(str, Enum):
    SCANNING = "SCANNING"
    CONNECTING = "CONNECTING"
    CONNECTED = "CONNECTED"
    DISCONNECTED = "DISCONNECTED"
    NOT_FOUND = "NOT_FOUND"
    ERROR = "ERROR"


class KeyingMode(str, Enum):
    STRAIGHT = "STRAIGHT"
    PADDLE = "PADDLE"


class TrainingMode(str, Enum):
    KOCH = "KOCH"
    CHARACTERS = "CHARACTERS"
    WORDS = "WORDS"
    CALLSIGNS = "CALLSIGNS"
    ADAPTIVE = "ADAPTIVE"
    EXAM = "EXAM"


class PairingEventType(str, Enum):
    PAIRING_STARTED = "PAIRING_STARTED"
    PAIRING_WAITING_FOR_PASSKEY = "PAIRING_WAITING_FOR_PASSKEY"
    PAIRING_WAITING_FOR_CONFIRMATION = "PAIRING_WAITING_FOR_CONFIRMATION"
    PAIRING_SUCCEEDED = "PAIRING_SUCCEEDED"
    PAIRING_FAILED = "PAIRING_FAILED"
    PAIRING_UNAVAILABLE = "PAIRING_UNAVAILABLE"


class Severity(str, Enum):
    ERROR = "ERROR"
    WARNING = "WARNING"
    INFO = "INFO"


class ErrorCode(str, Enum):
    DEVICE_NOT_FOUND = "DEVICE_NOT_FOUND"
    CONNECTION_FAILED = "CONNECTION_FAILED"
    CONNECTION_LOST = "CONNECTION_LOST"
    CONNECTION_TIMEOUT = "CONNECTION_TIMEOUT"
    PAIRING_UNAVAILABLE = "PAIRING_UNAVAILABLE"
    PAIRING_FAILED = "PAIRING_FAILED"
    INVALID_COMMAND = "INVALID_COMMAND"
    INVALID_PARAMETER = "INVALID_PARAMETER"
    UNSUPPORTED_OPERATION = "UNSUPPORTED_OPERATION"
    DEVICE_BUSY = "DEVICE_BUSY"
    TRAINING_NOT_ACTIVE = "TRAINING_NOT_ACTIVE"
    TRAINING_START_FAILED = "TRAINING_START_FAILED"
    KEYER_COMMAND_FAILED = "KEYER_COMMAND_FAILED"
    BACKEND_UNAVAILABLE = "BACKEND_UNAVAILABLE"
    INTERNAL_ERROR = "INTERNAL_ERROR"


# ---------------------------------------------------------------------------
# Data contracts
# ---------------------------------------------------------------------------
@dataclass
class ConnectionInfo:
    state: ConnectionState
    deviceName: Optional[str] = None
    deviceAddress: Optional[str] = None
    statusMessage: Optional[str] = None


@dataclass
class KeyerWordEvent:
    word: str
    wpm: int
    mode: str
    timestamp: int


@dataclass
class LiveWordEvent:
    """The whole in-progress word so far, sent once per decoded
    character (BLE JSON key "live") - not yet finalized by a word-gap.
    Same shape as KeyerWordEvent so client-side code can reuse one
    parser for both; kept as a distinct type/event because it fires far
    more often and is never itself the authoritative completed word."""
    word: str
    wpm: int
    mode: str
    timestamp: int


@dataclass
class TrainingState:
    active: bool
    mode: Optional[str] = None
    phase: Optional[str] = None
    target: Optional[str] = None
    correct: int = 0
    attempts: int = 0
    kochLevel: Optional[int] = None
    adaptiveWpm: Optional[int] = None
    examScorePercent: Optional[int] = None
    examPassed: Optional[bool] = None
    examCorrectCount: Optional[int] = None
    examTotalCount: Optional[int] = None


@dataclass
class PairingEvent:
    type: PairingEventType
    devicePath: Optional[str] = None
    passkey: Optional[int] = None
    errorCode: Optional[str] = None
    errorMessage: Optional[str] = None


@dataclass
class BackendError:
    code: str
    message: str
    severity: str = Severity.ERROR.value
    recoverable: bool = True
    operation: Optional[str] = None


@dataclass
class Capabilities:
    connection: bool = True
    pairing: bool = False
    keyer: bool = True
    training: bool = True
    statistics: bool = False
    connectivityManagement: bool = False
    profiles: bool = False
    settings: bool = False
    diagnostics: bool = False
    tools: bool = False


# ---------------------------------------------------------------------------
class MorpheusBackend:
    """The transport-agnostic backend. One instance per running app."""

    def __init__(self):
        self._connection = ConnectionInfo(state=ConnectionState.DISCONNECTED)
        self._training_state = TrainingState(active=False)

        self._connection_listeners: List[Callable[[ConnectionInfo], None]] = []
        self._keyer_listeners: List[Callable[[KeyerWordEvent], None]] = []
        self._keyer_live_listeners: List[Callable[[LiveWordEvent], None]] = []
        self._training_listeners: List[Callable[[TrainingState], None]] = []
        self._pairing_listeners: List[Callable[[PairingEvent], None]] = []
        self._error_listeners: List[Callable[[BackendError], None]] = []

        # GATT session thread/loop
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None
        self._client: Optional[BleakClient] = None
        self._stop_requested = False

        # Pairing thread/loop (independent of the GATT session, same as
        # the original ble_pairing.py design)
        self._pairing_loop: Optional[asyncio.AbstractEventLoop] = None
        self._pairing_thread: Optional[threading.Thread] = None
        self._pairing_driver = None  # pairing_backend.PairingDriver, set once running

    # ------------------------------------------------------------------
    # Subscription API (§13 Event Summary)
    # ------------------------------------------------------------------
    def on_connection_changed(self, callback: Callable[[ConnectionInfo], None]) -> None:
        self._connection_listeners.append(callback)

    def on_keyer_word(self, callback: Callable[[KeyerWordEvent], None]) -> None:
        self._keyer_listeners.append(callback)

    def on_keyer_live_word(self, callback: Callable[[LiveWordEvent], None]) -> None:
        self._keyer_live_listeners.append(callback)

    def on_training_state(self, callback: Callable[[TrainingState], None]) -> None:
        self._training_listeners.append(callback)

    def on_pairing_state(self, callback: Callable[[PairingEvent], None]) -> None:
        self._pairing_listeners.append(callback)

    def on_error(self, callback: Callable[[BackendError], None]) -> None:
        self._error_listeners.append(callback)

    def _emit_connection(self, info: ConnectionInfo) -> None:
        self._connection = info
        for cb in list(self._connection_listeners):
            cb(info)

    def _emit_keyer(self, evt: KeyerWordEvent) -> None:
        for cb in list(self._keyer_listeners):
            cb(evt)

    def _emit_keyer_live(self, evt: LiveWordEvent) -> None:
        for cb in list(self._keyer_live_listeners):
            cb(evt)

    def _emit_training(self, state: TrainingState) -> None:
        self._training_state = state
        for cb in list(self._training_listeners):
            cb(state)

    def _emit_pairing(self, evt: PairingEvent) -> None:
        for cb in list(self._pairing_listeners):
            cb(evt)

    def _emit_error(self, error: BackendError) -> None:
        for cb in list(self._error_listeners):
            cb(error)

    # ------------------------------------------------------------------
    # Capabilities / metadata (§10, §12)
    # ------------------------------------------------------------------
    def get_capabilities(self) -> Capabilities:
        return Capabilities(pairing=self._try_import_pairing_backend() is not None)

    def get_metadata(self) -> dict:
        return {
            "applicationVersion": APPLICATION_VERSION,
            "deviceName": self._connection.deviceName,
            "deviceFirmwareVersion": None,  # not exposed over BLE yet
        }

    @property
    def connection(self) -> ConnectionInfo:
        return self._connection

    @property
    def training(self) -> TrainingState:
        return self._training_state

    def get_snapshot(self) -> dict:
        """Everything a fresh consumer (e.g. a newly-connected IPC
        client - see ws_server.py) needs to reconstruct current state
        without having raced any events emitted before it subscribed."""
        return {
            "connection": self._connection,
            "training": self._training_state,
            "capabilities": self.get_capabilities(),
            "metadata": self.get_metadata(),
        }

    # ------------------------------------------------------------------
    # Reference data (§9)
    # ------------------------------------------------------------------
    @staticmethod
    def get_koch_sequence() -> str:
        return proto.KOCH_ORDER

    @staticmethod
    def get_morse_table() -> dict:
        return dict(proto.MORSE_TABLE)

    @staticmethod
    def get_reverse_morse_table() -> dict:
        """Morse pattern -> character, the other lookup direction a
        client needs to build a "decode" style game/UI."""
        return {pattern: char for char, pattern in proto.MORSE_TABLE.items()}

    # ------------------------------------------------------------------
    # Connection operations (§4.2)
    # ------------------------------------------------------------------
    def scan(self) -> None:
        """Discovery only - does not open a connection. Fire-and-forget;
        result observed via on_connection_changed (NOT_FOUND if nothing
        matched within the timeout, DISCONNECTED if a device was found
        and is ready to be connect()-ed)."""
        if self._thread and self._thread.is_alive():
            return
        self._stop_requested = False
        self._thread = threading.Thread(target=self._run_scan_only, daemon=True)
        self._thread.start()

    def connect(self, device_address: Optional[str] = None) -> None:
        """Scan-then-connect sequence (or direct connect if an address
        is supplied and the platform permits it)."""
        if self._thread and self._thread.is_alive():
            return
        self._stop_requested = False
        self._thread = threading.Thread(
            target=self._run_connect, args=(device_address,), daemon=True
        )
        self._thread.start()

    def disconnect(self) -> None:
        self._stop_requested = True
        if self._loop is not None:
            asyncio.run_coroutine_threadsafe(self._async_disconnect(), self._loop)

    # ------------------------------------------------------------------
    # Virtual straight key (§7) - shared by Training (§8.5)
    # ------------------------------------------------------------------
    def key_down(self) -> None:
        self._send_command({"cmd": "key_down"}, operation="keyDown")

    def key_up(self) -> None:
        self._send_command({"cmd": "key_up"}, operation="keyUp")

    # ------------------------------------------------------------------
    # Training operations (§8.2)
    # ------------------------------------------------------------------
    def start_training(self, mode) -> None:
        mode_value = mode.value if isinstance(mode, TrainingMode) else str(mode)
        if mode_value not in TrainingMode.__members__:
            self._emit_error(BackendError(
                code=ErrorCode.INVALID_PARAMETER.value,
                message=f"Unknown training mode: {mode_value}",
                operation="startTraining",
            ))
            return
        self._send_command({"cmd": "train_start", "mode": mode_value}, operation="startTraining")

    def stop_training(self) -> None:
        self._send_command({"cmd": "train_stop"}, operation="stopTraining")

    def confirm_training(self) -> None:
        self._send_command({"cmd": "train_confirm"}, operation="confirmTraining")

    # ------------------------------------------------------------------
    # Pairing operations (§5) - Linux/BlueZ only
    # ------------------------------------------------------------------
    @staticmethod
    def _try_import_pairing_backend():
        """Lazy, isolated import: only pairing depends on dbus-next, so
        a machine without it (or a non-Linux platform) loses only this
        one capability, never the rest of the backend."""
        if not sys.platform.startswith("linux"):
            return None
        try:
            import pairing_backend
            return pairing_backend
        except ImportError:
            return None

    def start_pairing(self, target_device_name: str) -> None:
        pairing_backend = self._try_import_pairing_backend()
        if pairing_backend is None:
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_UNAVAILABLE,
                                             errorCode=ErrorCode.PAIRING_UNAVAILABLE.value,
                                             errorMessage="Pairing needs dbus-next and is Linux-only"))
            return
        if self._pairing_thread and self._pairing_thread.is_alive():
            return
        self._pairing_thread = threading.Thread(
            target=self._run_pairing, args=(pairing_backend, target_device_name), daemon=True
        )
        self._pairing_thread.start()

    def submit_passkey(self, passkey: str) -> None:
        if self._pairing_driver is not None:
            self._pairing_driver.submit_passkey(int(passkey))

    def confirm_pairing(self, accepted: bool) -> None:
        if self._pairing_driver is not None:
            self._pairing_driver.submit_confirmation(accepted)

    def _run_pairing(self, pairing_backend, target_name: str) -> None:
        self._pairing_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._pairing_loop)
        self._emit_pairing(PairingEvent(PairingEventType.PAIRING_STARTED))

        def on_waiting_passkey(device_path):
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_WAITING_FOR_PASSKEY, devicePath=device_path))

        def on_waiting_confirmation(device_path, passkey):
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_WAITING_FOR_CONFIRMATION,
                                             devicePath=device_path, passkey=passkey))

        def on_display_passkey(device_path, passkey):
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_WAITING_FOR_PASSKEY,
                                             devicePath=device_path, passkey=passkey))

        def on_succeeded(device_path):
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_SUCCEEDED, devicePath=device_path))

        def on_failed(error_code, error_message):
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_FAILED,
                                             errorCode=error_code, errorMessage=error_message))

        self._pairing_driver = pairing_backend.PairingDriver(
            self._pairing_loop, on_waiting_passkey, on_waiting_confirmation,
            on_display_passkey, on_succeeded, on_failed,
        )
        try:
            self._pairing_loop.run_until_complete(self._pairing_driver.run(target_name))
        except Exception as exc:  # noqa: BLE001
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_FAILED,
                                             errorCode=ErrorCode.PAIRING_FAILED.value,
                                             errorMessage=str(exc)))
        finally:
            self._pairing_driver = None
            self._pairing_loop.close()
            self._pairing_loop = None

    # ------------------------------------------------------------------
    # Internal: GATT session
    # ------------------------------------------------------------------
    def _send_command(self, command: dict, operation: str) -> None:
        if self._loop is None or self._client is None:
            self._emit_error(BackendError(
                code=ErrorCode.DEVICE_BUSY.value,
                message="Not connected",
                operation=operation,
                recoverable=True,
            ))
            return
        asyncio.run_coroutine_threadsafe(self._async_send_command(command, operation), self._loop)

    async def _async_send_command(self, command: dict, operation: str) -> None:
        if self._client is None or not self._client.is_connected:
            self._emit_error(BackendError(code=ErrorCode.DEVICE_BUSY.value, message="Not connected", operation=operation))
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
            code = (ErrorCode.KEYER_COMMAND_FAILED if command.get("cmd", "").startswith("key_")
                    else ErrorCode.TRAINING_START_FAILED)
            self._emit_error(BackendError(code=code.value, message=str(exc), operation=operation))

    async def _async_disconnect(self) -> None:
        if self._client is not None and self._client.is_connected:
            await self._client.disconnect()

    def _run_scan_only(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self._async_scan_only())
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value, message=str(exc), operation="scan"))
        finally:
            loop.close()

    async def _async_scan_only(self) -> None:
        self._emit_connection(ConnectionInfo(state=ConnectionState.SCANNING))
        device = await BleakScanner.find_device_by_filter(
            lambda d, _adv: d.name == proto.DEVICE_NAME, timeout=proto.SCAN_TIMEOUT_S
        )
        if device is None:
            self._emit_connection(ConnectionInfo(
                state=ConnectionState.NOT_FOUND,
                statusMessage=f"{proto.DEVICE_NAME} not found in {proto.SCAN_TIMEOUT_S:.0f}s",
            ))
        else:
            self._emit_connection(ConnectionInfo(
                state=ConnectionState.DISCONNECTED,
                deviceName=device.name, deviceAddress=device.address,
                statusMessage="Found - ready to connect",
            ))

    def _run_connect(self, address: Optional[str]) -> None:
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._async_connect_main(address))
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value, message=str(exc), operation="connect"))
        finally:
            self._emit_connection(ConnectionInfo(state=ConnectionState.DISCONNECTED))
            self._loop.close()
            self._loop = None

    def _on_word_notify(self, _characteristic, data: bytearray) -> None:
        # Same characteristic carries two payload shapes, told apart by
        # JSON key: "live" is the whole in-progress word so far (fires
        # per character, never itself authoritative), "word" is the
        # word-gap-finalized event (fires once per completed word).
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
            if "live" in payload:
                self._emit_keyer_live(LiveWordEvent(
                    word=str(payload.get("live", "")),
                    wpm=int(payload.get("wpm", 0)),
                    mode=str(payload.get("mode", "")),
                    timestamp=int(payload.get("timestamp", 0)),
                ))
                return
            self._emit_keyer(KeyerWordEvent(
                word=str(payload.get("word", "")),
                wpm=int(payload.get("wpm", 0)),
                mode=str(payload.get("mode", "")),
                timestamp=int(payload.get("timestamp", 0)),
            ))
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value,
                                           message=f"Malformed keyer payload: {exc}"))

    def _on_control_notify(self, _characteristic, data: bytearray) -> None:
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value,
                                           message=f"Malformed control payload: {exc}"))
            return
        evt = payload.get("evt")
        if evt == "train_state":
            self._emit_training(TrainingState(
                active=bool(payload.get("active")),
                mode=payload.get("mode"),
                phase=payload.get("phase"),
                target=payload.get("target"),
                correct=int(payload.get("correct", 0)),
                attempts=int(payload.get("attempts", 0)),
                kochLevel=payload.get("kochLevel"),
                adaptiveWpm=payload.get("adaptiveWpm"),
                examScorePercent=payload.get("examScorePercent"),
                examPassed=payload.get("examPassed"),
                examCorrectCount=payload.get("examCorrect"),
                examTotalCount=payload.get("examTotal"),
            ))
        elif evt == "error":
            self._emit_error(BackendError(code=ErrorCode.TRAINING_START_FAILED.value,
                                           message=payload.get("message", "unknown error")))
        # "ack" and "game_state" events are intentionally not surfaced here:
        # ack is fire-and-forget bookkeeping the contract doesn't require,
        # and game_state belongs to the firmware's own 3-game protocol,
        # which this application's Games feature does not use (see
        # UI_SPECIFICATION.md §4).

    async def _async_connect_main(self, address: Optional[str]) -> None:
        self._emit_connection(ConnectionInfo(state=ConnectionState.SCANNING))
        if address:
            device = await BleakScanner.find_device_by_address(address, timeout=proto.SCAN_TIMEOUT_S)
        else:
            device = await BleakScanner.find_device_by_filter(
                lambda d, _adv: d.name == proto.DEVICE_NAME, timeout=proto.SCAN_TIMEOUT_S
            )

        if self._stop_requested:
            return
        if device is None:
            self._emit_error(BackendError(
                code=ErrorCode.DEVICE_NOT_FOUND.value,
                message=f"{proto.DEVICE_NAME} not found in {proto.SCAN_TIMEOUT_S:.0f}s. "
                        "Is BLE enabled and advertising on the device?",
                operation="connect",
            ))
            self._emit_connection(ConnectionInfo(state=ConnectionState.NOT_FOUND))
            return

        self._emit_connection(ConnectionInfo(state=ConnectionState.CONNECTING))
        try:
            async with BleakClient(device) as client:
                self._client = client
                self._emit_connection(ConnectionInfo(
                    state=ConnectionState.CONNECTED,
                    deviceName=device.name or proto.DEVICE_NAME,
                    deviceAddress=device.address,
                ))
                await client.start_notify(proto.WORD_CHAR_UUID, self._on_word_notify)
                await client.start_notify(proto.CONTROL_EVT_UUID, self._on_control_notify)
                while client.is_connected and not self._stop_requested:
                    await asyncio.sleep(0.5)
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.CONNECTION_FAILED.value, message=str(exc), operation="connect"))
        finally:
            self._client = None
