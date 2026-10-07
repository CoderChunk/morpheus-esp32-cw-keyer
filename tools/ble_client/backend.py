"""
MORPHEUS transport-agnostic backend API.

Implements the backend contract (see BACKEND_API.md): BLE
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
import time
import re
import uuid
from datetime import datetime, timezone
from dataclasses import dataclass
from enum import Enum
from typing import Callable, List, Optional

from bleak import BleakClient, BleakScanner

import protocol as proto

APPLICATION_VERSION = "1.2.0"


# ---------------------------------------------------------------------------
# Enumerations (string-valued so they serialize identically to the values in
# BACKEND_API.md, whether or not a given consumer ever
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
    LISTENING = "LISTENING"
    COMBINED = "COMBINED"


class GameId(str, Enum):
    COPY = "COPY"
    MEMORY = "MEMORY"
    SPEED = "SPEED"


class PairingEventType(str, Enum):
    PAIRING_STARTED = "PAIRING_STARTED"
    PAIRING_WAITING_FOR_PASSKEY = "PAIRING_WAITING_FOR_PASSKEY"
    PAIRING_WAITING_FOR_CONFIRMATION = "PAIRING_WAITING_FOR_CONFIRMATION"
    PAIRING_SUCCEEDED = "PAIRING_SUCCEEDED"
    PAIRING_FAILED = "PAIRING_FAILED"
    PAIRING_UNAVAILABLE = "PAIRING_UNAVAILABLE"
    PAIRING_CANCELLED = "PAIRING_CANCELLED"
    PAIRING_SYSTEM_PROMPT = "PAIRING_SYSTEM_PROMPT"


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
class LivePatternEvent:
    """The in-progress dit/dah pattern for the character currently
    being keyed (BLE JSON key "pat"), e.g. ".-" - empty once the
    character finalizes. Fires once per keyed element, the most
    frequent of the three keyer telemetry events."""
    pattern: str
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
    # The base keyer WPM (core_keyer_getWpm()) - what every mode except
    # ADAPTIVE actually plays targets at. Lets a client synthesize
    # listening audio that matches the device's real speed instead of
    # guessing a fixed one.
    wpm: Optional[int] = None
    examScorePercent: Optional[int] = None
    examPassed: Optional[bool] = None
    examCorrectCount: Optional[int] = None
    examTotalCount: Optional[int] = None


@dataclass
class GameState:
    active: bool
    game: Optional[str] = None
    paused: bool = False
    phase: Optional[str] = None
    highScore: int = 0
    # COPY
    target: Optional[str] = None
    score: Optional[int] = None
    lives: Optional[int] = None
    fallProgressPct: Optional[int] = None
    # MEMORY
    chainLength: Optional[int] = None
    inputProgress: Optional[int] = None
    chain: Optional[str] = None
    # SPEED
    combo: Optional[int] = None
    beatRemainingMs: Optional[int] = None
    lastChar: Optional[str] = None
    wasLastCorrect: Optional[bool] = None
    # Same as TrainingState.wpm - the base keyer WPM these games' target
    # characters are actually played at.
    wpm: Optional[int] = None


@dataclass
class PairingEvent:
    type: PairingEventType
    deviceAddress: Optional[str] = None
    attemptId: Optional[str] = None
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
        self._game_state = GameState(active=False)
        # Populated only after a "get_device_info" round trip (see
        # request_device_info()/ble_control.cpp's sendDeviceInfo()) -
        # firmwareVersion/wpm/sidetoneHz/sidetoneEnabled/volume/
        # paddleReversed/mode/iambicMode/weightPercent. Empty until then.
        self._device_info: dict = {}
        self._metric_probes: dict = {}
        # Errors are events: one refusal can reach us as a notification, again as the
        # post-command re-read and again as BlueZ's echo of that read. Report it once
        # per command (reset each time a command is written).
        self._last_error_msg = None
        # BlueZ reports our own GATT read as a notification of the same value; drop that
        # echo (value, expiry) so it is not mistaken for a new event.
        self._echo_guard = None
        self._metrics_listeners: list = []

        self._connection_listeners: List[Callable[[ConnectionInfo], None]] = []
        self._keyer_listeners: List[Callable[[KeyerWordEvent], None]] = []
        self._keyer_live_listeners: List[Callable[[LiveWordEvent], None]] = []
        self._keyer_pattern_listeners: List[Callable[[LivePatternEvent], None]] = []
        self._training_listeners: List[Callable[[TrainingState], None]] = []
        self._game_morse_listeners: List[Callable[[dict], None]] = []
        self._game_listeners: List[Callable[[GameState], None]] = []
        self._pairing_listeners: List[Callable[[PairingEvent], None]] = []
        self._error_listeners: List[Callable[[BackendError], None]] = []
        self._device_info_listeners: List[Callable[[dict], None]] = []

        # GATT session thread/loop
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None
        self._client: Optional[BleakClient] = None
        self._stop_requested = False

        # Outgoing command queue (see _command_writer) - created fresh on
        # each connection, on self._loop.
        self._cmd_queue: Optional[asyncio.PriorityQueue] = None
        self._cmd_seq = 0
        self._key_backlog = 0

        # Strong references for the fire-and-forget _reconcileControlState
        # tasks _command_writer spawns - asyncio only weakly tracks a task
        # created via ensure_future/create_task with no reference kept, so
        # without this set the task can be garbage-collected mid-flight
        # (a well-known asyncio footgun), silently dropping the exact
        # correction train_stop's reliability depends on.
        self._background_tasks: set = set()

        # Pairing thread/loop (independent of the GATT session, same as
        # the original ble_pairing.py design)
        self._pairing_loop: Optional[asyncio.AbstractEventLoop] = None
        self._pairing_thread: Optional[threading.Thread] = None
        self._pairing_driver = None  # pairing_backend.PairingDriver, set once running
        self._pairing_cancelled = False
        self._pairing_attempt = None
        self._notifications_ready = False
        self._last_seen = None
        self._discovery_running = False
        self._advertisements = {}
        self._system_pairing = None
        self._connect_task = None

    # ------------------------------------------------------------------
    # Subscription API (§13 Event Summary)
    # ------------------------------------------------------------------
    def on_connection_changed(self, callback: Callable[[ConnectionInfo], None]) -> None:
        self._connection_listeners.append(callback)

    def on_keyer_word(self, callback: Callable[[KeyerWordEvent], None]) -> None:
        self._keyer_listeners.append(callback)

    def on_keyer_live_word(self, callback: Callable[[LiveWordEvent], None]) -> None:
        self._keyer_live_listeners.append(callback)

    def on_keyer_live_pattern(self, callback: Callable[[LivePatternEvent], None]) -> None:
        self._keyer_pattern_listeners.append(callback)

    def on_training_state(self, callback: Callable[[TrainingState], None]) -> None:
        self._training_listeners.append(callback)

    def on_game_morse(self, callback: Callable[[dict], None]) -> None:
        self._game_morse_listeners.append(callback)

    def _on_game_morse_notify(self, _characteristic, data: bytearray) -> None:
        try:
            evt = json.loads(bytes(data).decode("utf-8"))
            if (evt.get("evt") != "game_morse" or evt.get("game") not in proto.GAMES
                    or not isinstance(evt.get("char"), str) or len(evt["char"]) != 1
                    or type(evt.get("run")) is not int or not 0 <= evt["run"] <= 0xffffffff
                    or type(evt.get("seq")) is not int or not 1 <= evt["seq"] <= 0xffffffff):
                return
            for cb in list(self._game_morse_listeners):
                cb(evt)
        except (ValueError, UnicodeError, TypeError, AttributeError):
            return

    def on_game_state(self, callback: Callable[[GameState], None]) -> None:
        self._game_listeners.append(callback)

    def on_pairing_state(self, callback: Callable[[PairingEvent], None]) -> None:
        self._pairing_listeners.append(callback)

    def on_error(self, callback: Callable[[BackendError], None]) -> None:
        self._error_listeners.append(callback)

    def on_device_info_changed(self, callback: Callable[[dict], None]) -> None:
        self._device_info_listeners.append(callback)

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

    def _emit_keyer_pattern(self, evt: LivePatternEvent) -> None:
        for cb in list(self._keyer_pattern_listeners):
            cb(evt)

    def _emit_training(self, state: TrainingState) -> None:
        self._training_state = state
        for cb in list(self._training_listeners):
            cb(state)

    def _emit_game(self, state: GameState) -> None:
        self._game_state = state
        for cb in list(self._game_listeners):
            cb(state)

    def _emit_pairing(self, evt: PairingEvent) -> None:
        for cb in list(self._pairing_listeners):
            cb(evt)

    def _emit_error(self, error: BackendError) -> None:
        for cb in list(self._error_listeners):
            cb(error)

    def _emit_device_info(self, info: dict) -> None:
        self._device_info = info
        for cb in list(self._device_info_listeners):
            cb(info)

    # ------------------------------------------------------------------
    # Capabilities / metadata (§10, §12)
    # ------------------------------------------------------------------
    def get_capabilities(self) -> Capabilities:
        return Capabilities(pairing=self._try_import_pairing_backend() is not None,
                            connectivityManagement=True)

    def get_metadata(self) -> dict:
        return {
            "applicationVersion": APPLICATION_VERSION,
            "deviceName": self._connection.deviceName,
            # None until a "get_device_info" round trip completes (see
            # request_device_info()) - the firmware doesn't push this
            # unprompted, so a fresh connection starts without it.
            "deviceFirmwareVersion": self._device_info.get("firmwareVersion"),
        }

    def get_device_info(self) -> dict:
        """Last "device_info" snapshot received (see
        on_device_info_changed/request_device_info) - empty until the
        first round trip completes."""
        return dict(self._device_info)

    @property
    def connection(self) -> ConnectionInfo:
        return self._connection

    @property
    def training(self) -> TrainingState:
        return self._training_state

    @property
    def game(self) -> GameState:
        return self._game_state

    def get_snapshot(self) -> dict:
        """Everything a fresh consumer (e.g. a newly-connected IPC
        client - see ws_server.py) needs to reconstruct current state
        without having raced any events emitted before it subscribed."""
        return {
            "connection": self._connection,
            "training": self._training_state,
            "game": self._game_state,
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

    @staticmethod
    def _valid_identifier(value):
        if not isinstance(value, str):
            return False
        if sys.platform == "darwin":
            try:
                uuid.UUID(value)
                return True
            except ValueError:
                return False
        return re.fullmatch(r"[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}", value) is not None

    async def discover_devices(self):
        if self._discovery_running:
            raise ValueError("A device search is already running")
        self._discovery_running = True
        try:
            found = await BleakScanner.discover(timeout=5.0, return_adv=True)
            devices = []
            for device, adv in found.values():
                name = adv.local_name or device.name or ""
                if not (name.upper().startswith("MORPHEUS") or
                        proto.SERVICE_UUID in [u.lower() for u in adv.service_uuids]):
                    continue
                seen = datetime.now(timezone.utc).isoformat()
                row = dict(identifier=device.address, name=name or proto.DEVICE_NAME,
                           rssi=adv.rssi, lastSeen=seen, paired=None)
                devices.append(row)
                self._advertisements[device.address.upper()] = (time.monotonic(), row)
            return devices
        finally:
            self._discovery_running = False

    async def list_paired_devices(self):
        module = self._try_import_pairing_backend()
        if module is None:
            return []  # OS-owned pairing on non-Linux; never claim a fake host bond.
        return await module.paired_devices()

    async def remove_pairing(self, address):
        if not self._valid_identifier(address):
            raise ValueError("A valid Bluetooth device address is required")
        if self._connection.state == ConnectionState.CONNECTED and (self._connection.deviceAddress or "").upper() == address.upper():
            raise ValueError("Disconnect this device before removing its pairing")
        module = self._try_import_pairing_backend()
        if module is not None:
            await module.remove_pairing(address)
        elif sys.platform == "win32":
            await BleakClient(address).unpair()
        else:
            raise ValueError("Remove this bond in the operating system Bluetooth settings")
        self._advertisements.pop(address.upper(), None)

    def get_device_runtime(self):
        address = self._connection.deviceAddress
        observation = self._advertisements.get((address or "").upper())
        rssi = observation[1]["rssi"] if observation and time.monotonic() - observation[0] < 30 else None
        return dict(deviceManagement=True, bridgeRunning=True,
                    usesSystemPairing=self._try_import_pairing_backend() is None,
                    canListPairings=self._try_import_pairing_backend() is not None,
                    canRemovePairing=self._try_import_pairing_backend() is not None or sys.platform == "win32",
                    scanActive=self._discovery_running or self._connection.state == ConnectionState.SCANNING,
                    notificationsReady=self._notifications_ready,
                    lastSeen=self._last_seen, rssi=rssi, signalPercent=None)

    async def prepare_connect(self):
        # DISCONNECTED is emitted before the old worker exits. Wait for cleanup,
        # rather than dropping the next connect request because that thread lives.
        if self._thread and self._thread.is_alive() and self._stop_requested:
            await asyncio.to_thread(self._thread.join, 12)
            if self._thread.is_alive():
                raise ValueError("The previous device is still disconnecting")

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
    def start_training(self, mode, koch_level=None) -> None:
        mode_value = mode.value if isinstance(mode, TrainingMode) else str(mode)
        if mode_value not in TrainingMode.__members__:
            self._emit_error(BackendError(
                code=ErrorCode.INVALID_PARAMETER.value,
                message=f"Unknown training mode: {mode_value}",
                operation="startTraining",
            ))
            return
        command = {"cmd": "train_start", "mode": mode_value}
        if koch_level is not None:
            if isinstance(koch_level, bool) or not isinstance(koch_level, int) or not 2 <= koch_level <= 40:
                raise ValueError("Invalid Koch pool level")
            command["kochLevel"] = koch_level
        self._send_command(command, operation="startTraining")

    def stop_training(self) -> None:
        self._send_command({"cmd": "train_stop"}, operation="stopTraining")

    def confirm_training(self) -> None:
        self._send_command({"cmd": "train_confirm"}, operation="confirmTraining")

    def answer_training(self, text: str) -> None:
        """Identification answer for LISTENING/COMBINED - see
        BACKEND_API.md. A no-op on the
        device unless phase == "AWAIT_ANSWER"."""
        self._send_command({"cmd": "train_answer", "text": text}, operation="answerTraining")

    # ------------------------------------------------------------------
    # Device games (§8.6) - COPY/MEMORY/SPEED, device-authoritative,
    # mutually exclusive with Training (same single-decoder-consumer
    # rule enforced firmware-side).
    # ------------------------------------------------------------------
    def start_game(self, game) -> None:
        game_value = game.value if isinstance(game, GameId) else str(game)
        if game_value not in GameId.__members__:
            self._emit_error(BackendError(
                code=ErrorCode.INVALID_PARAMETER.value,
                message=f"Unknown game: {game_value}",
                operation="startGame",
            ))
            return
        self._send_command({"cmd": "game_start", "game": game_value}, operation="startGame")

    def stop_game(self) -> None:
        self._send_command({"cmd": "game_stop"}, operation="stopGame")

    def pause_game(self) -> None:
        self._send_command({"cmd": "game_pause"}, operation="pauseGame")

    def confirm_game(self) -> None:
        self._send_command({"cmd": "game_confirm"}, operation="confirmGame")

    def restart_game(self) -> None:
        self._send_command({"cmd": "game_restart"}, operation="restartGame")

    def set_keyer_setting(self, field: str, value: int) -> None:
        bounds = {"wpm": (5, 40), "tone": (200, 2000), "volume": (0, 100),
                  "reversed": (0, 1), "mode": (0, 1), "iambic": (0, 1),
                  "weight": (30, 70), "sidetone": (0, 1)}
        if field not in bounds or isinstance(value, bool) or not isinstance(value, int):
            raise ValueError("Invalid keyer setting")
        low, high = bounds[field]
        if not low <= value <= high:
            raise ValueError("Keyer setting outside firmware range")
        self._send_command({"cmd": "set_keyer", "field": field, "value": value}, operation="setKeyerSetting")

    def on_keyer_metrics(self, callback) -> None:
        self._metrics_listeners.append(callback)

    def _emit_metrics(self, payload: dict) -> None:
        for callback in list(self._metrics_listeners):
            callback(payload)

    def request_keyer_metrics(self, probe_id: str, reset: bool = False) -> None:
        if (not isinstance(probe_id, str) or not 1 <= len(probe_id) <= 24
                or not probe_id.isascii() or not probe_id.isalnum() or type(reset) is not bool):
            raise ValueError("Invalid keyer probe")
        if self._loop is None or self._client is None:
            self._emit_metrics({"id": probe_id, "unavailable": True})
            return
        # Lower priority than remote key edges and session-control commands.
        command = {"cmd": "reset_keyer_metrics" if reset else "probe_keyer", "id": probe_id}
        asyncio.run_coroutine_threadsafe(self._enqueue_command(2, command, "probeKeyerMetrics"), self._loop)

    def _expire_metrics(self, probe_id: str) -> None:
        pending = self._metric_probes.pop(probe_id, None)
        if pending is not None:
            pending[1].cancel()
            self._emit_metrics({"id": probe_id, "unavailable": True})

    def request_device_info(self) -> None:
        """Fire-and-forget, same as every other command here - the
        result arrives via on_device_info_changed (ble_control.cpp's
        "device_info" event), not a return value."""
        self._send_command({"cmd": "get_device_info"}, operation="requestDeviceInfo")

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

    def start_pairing(self, target_device_name: str, device_address=None, attempt_id=None) -> None:
        if device_address is not None and not self._valid_identifier(device_address):
            raise ValueError("A valid Bluetooth device address is required")
        if attempt_id is not None and (not isinstance(attempt_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", attempt_id)):
            raise ValueError("Invalid pairing attempt identifier")
        pairing_backend = self._try_import_pairing_backend()
        if pairing_backend is None:
            if not device_address:
                self._emit_pairing(PairingEvent(PairingEventType.PAIRING_UNAVAILABLE,
                    errorCode=ErrorCode.PAIRING_UNAVAILABLE.value,
                    errorMessage="Select a device for system pairing"))
                return
            if self._thread and self._thread.is_alive():
                raise ValueError("Disconnect the active device before system pairing")
            self._pairing_cancelled = False
            self._pairing_attempt = attempt_id
            self._system_pairing = (device_address, attempt_id)
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_SYSTEM_PROMPT,
                deviceAddress=device_address, attemptId=attempt_id))
            self.connect(device_address)
            return
        if self._pairing_thread and self._pairing_thread.is_alive():
            raise ValueError("Another pairing attempt is still running")
        self._pairing_cancelled = False
        self._pairing_attempt = attempt_id
        self._pairing_thread = threading.Thread(
            target=self._run_pairing, args=(pairing_backend, target_device_name, device_address, attempt_id), daemon=True
        )
        self._pairing_thread.start()

    def submit_passkey(self, passkey: str, attempt_id=None) -> None:
        if not isinstance(passkey, str) or not re.fullmatch(r"[0-9]{6}", passkey):
            raise ValueError("PIN must contain exactly six digits")
        if attempt_id is not None and attempt_id != self._pairing_attempt:
            raise ValueError("This pairing attempt is no longer active")
        if self._pairing_driver is None:
            raise ValueError("No pairing attempt is waiting for a PIN")
        self._pairing_driver.submit_passkey(int(passkey))

    def confirm_pairing(self, accepted: bool, attempt_id=None) -> None:
        if not isinstance(accepted, bool):
            raise ValueError("Pairing confirmation must be boolean")
        if attempt_id is not None and attempt_id != self._pairing_attempt:
            raise ValueError("This pairing attempt is no longer active")
        if self._pairing_driver is None:
            raise ValueError("No pairing attempt is waiting for confirmation")
        self._pairing_driver.submit_confirmation(accepted)

    def cancel_pairing(self, attempt_id=None):
        if attempt_id is not None and attempt_id != self._pairing_attempt:
            raise ValueError("This pairing attempt is no longer active")
        self._pairing_cancelled = True
        if self._system_pairing is not None:
            address, token = self._system_pairing
            self._system_pairing = None
            self.disconnect()
            if self._loop is not None and self._connect_task is not None:
                self._loop.call_soon_threadsafe(self._connect_task.cancel)
            self._emit_pairing(PairingEvent(PairingEventType.PAIRING_CANCELLED,
                deviceAddress=address, attemptId=token))
        if self._pairing_driver is not None:
            self._pairing_driver.cancel()

    async def wait_pairing_idle(self):
        if self._pairing_thread and self._pairing_thread.is_alive():
            await asyncio.to_thread(self._pairing_thread.join, 8)
            if self._pairing_thread.is_alive():
                raise ValueError("The previous pairing attempt is still closing")

    def _run_pairing(self, pairing_backend, target_name: str, device_address=None, attempt_id=None) -> None:
        self._pairing_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._pairing_loop)
        def emit(kind, **kwargs):
            if self._pairing_cancelled and kind != PairingEventType.PAIRING_CANCELLED:
                return
            self._emit_pairing(PairingEvent(kind, deviceAddress=device_address,
                                           attemptId=attempt_id, **kwargs))
        emit(PairingEventType.PAIRING_STARTED)

        def on_waiting_passkey(device_path):
            emit(PairingEventType.PAIRING_WAITING_FOR_PASSKEY, devicePath=device_path)

        def on_waiting_confirmation(device_path, passkey):
            emit(PairingEventType.PAIRING_WAITING_FOR_CONFIRMATION,
                                             devicePath=device_path, passkey=passkey)

        def on_display_passkey(device_path, passkey):
            emit(PairingEventType.PAIRING_WAITING_FOR_PASSKEY,
                                             devicePath=device_path, passkey=passkey)

        def on_succeeded(device_path):
            emit(PairingEventType.PAIRING_SUCCEEDED, devicePath=device_path)

        def on_failed(error_code, error_message):
            emit(PairingEventType.PAIRING_FAILED,
                                             errorCode=error_code, errorMessage=error_message)

        self._pairing_driver = pairing_backend.PairingDriver(
            self._pairing_loop, on_waiting_passkey, on_waiting_confirmation,
            on_display_passkey, on_succeeded, on_failed,
        )
        try:
            if self._pairing_cancelled:
                emit(PairingEventType.PAIRING_CANCELLED)
                return
            self._pairing_loop.run_until_complete(self._pairing_driver.run(target_name, device_address))
        except asyncio.CancelledError:
            emit(PairingEventType.PAIRING_CANCELLED)
        except Exception as exc:  # noqa: BLE001
            emit(PairingEventType.PAIRING_FAILED,
                                             errorCode=ErrorCode.PAIRING_FAILED.value,
                                             errorMessage=str(exc))
        finally:
            self._pairing_driver = None
            self._pairing_loop.close()
            self._pairing_loop = None

    # ------------------------------------------------------------------
    # Internal: GATT session
    # ------------------------------------------------------------------
    # Every control-channel write (key_down/key_up/train_*) shares ONE
    # GATT characteristic and each write uses response=True, so bleak's
    # underlying BLE stack only ever has one such write in flight at a
    # time - concurrent calls queue up implicitly at that layer. Without
    # an explicit queue here, that implicit ordering is whatever order
    # the asyncio scheduler happens to start the tasks in, which is FIFO
    # by submission - fine on its own, EXCEPT a virtual key held down
    # during WORDS/CALLSIGNS training (or a fast game) keeps submitting
    # key_down/key_up every keyed element, so a train_stop/game_stop
    # submitted mid-hold lands at the BACK of that backlog and doesn't
    # reach the device until the user releases the key. _cmd_queue makes
    # that ordering explicit and priority-aware: session-control commands
    # (PRIORITY_CONTROL) always jump ahead of any already-queued
    # key_down/key_up (PRIORITY_KEY), so Stop is never starved by keying.
    PRIORITY_CONTROL = 0
    PRIORITY_KEY = 1

    # Bounds how many key_down/key_up commands may sit in the queue
    # ahead of the one currently being written. Priority alone already
    # lets a control command (e.g. train_stop) cut the line, but if a
    # fast/sustained virtual-key session submits commands faster than
    # the device's single write-with-response round trip can drain them,
    # an unbounded backlog would still delay Stop by however long it
    # takes the CURRENTLY in-flight write plus this whole backlog to
    # clear, one at a time, before Stop's own turn comes up. Capping the
    # backlog and dropping the newest excess key command bounds that
    # worst case to MAX_KEY_BACKLOG round trips - stale queued key
    # state is low-value anyway once a backlog has built up that deep.
    MAX_KEY_BACKLOG = 4

    def _send_command(self, command: dict, operation: str) -> None:
        if self._loop is None or self._client is None:
            self._emit_error(BackendError(
                code=ErrorCode.DEVICE_BUSY.value,
                message="Not connected",
                operation=operation,
                recoverable=True,
            ))
            return
        priority = self.PRIORITY_KEY if command.get("cmd", "").startswith("key_") else self.PRIORITY_CONTROL
        asyncio.run_coroutine_threadsafe(self._enqueue_command(priority, command, operation), self._loop)

    async def _enqueue_command(self, priority: int, command: dict, operation: str) -> None:
        if self._cmd_queue is None:
            self._emit_error(BackendError(code=ErrorCode.DEVICE_BUSY.value, message="Not connected", operation=operation))
            return
        if priority == self.PRIORITY_KEY and self._key_backlog >= self.MAX_KEY_BACKLOG:
            return  # drop: queue already deep enough that this one is stale by the time it'd be sent
        self._cmd_seq += 1
        if priority == self.PRIORITY_KEY:
            self._key_backlog += 1
        # (priority, seq, ...): seq is a tiebreaker so PriorityQueue never
        # has to compare two dicts (which aren't orderable) for equal
        # priorities - it just falls through to comparing ints, preserving
        # FIFO order within the same priority band.
        await self._cmd_queue.put((priority, self._cmd_seq, command, operation))

    async def _command_writer(self) -> None:
        """Single consumer draining self._cmd_queue in priority order -
        the only coroutine that ever calls write_gatt_char, so command
        ordering on the wire exactly matches queue order instead of
        asyncio's incidental task-scheduling order."""
        assert self._cmd_queue is not None
        while True:
            priority, _seq, command, operation = await self._cmd_queue.get()
            if priority == self.PRIORITY_KEY:
                self._key_backlog -= 1
            if self._client is None or not self._client.is_connected:
                self._emit_error(BackendError(code=ErrorCode.DEVICE_BUSY.value, message="Not connected", operation=operation))
                continue
            try:
                # Firmware's jsonGetString() (ble_control.cpp) matches the
                # literal pattern "key":" with no space after the colon -
                # json.dumps()'s default ": " separator would silently fail
                # to match, so every command's "cmd" field would come back
                # as "missing cmd". Compact separators avoid that.
                payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
                self._last_error_msg = None  # a refusal after this write is new
                self._echo_guard = None      # ...and so is any event after it (the read's echo is long past)
                # A write-with-response that never gets acked (e.g. the
                # peripheral's BLE stack wedged after a burst of traffic)
                # would otherwise hang this coroutine forever - since this
                # is the ONLY place that calls write_gatt_char, that stalls
                # every command behind it in the queue permanently,
                # including any future train_stop. A bounded timeout turns
                # that into a reported error instead of a silent freeze.
                if command.get("cmd") in ("probe_keyer", "reset_keyer_metrics"):
                    probe_id = command["id"]
                    previous = self._metric_probes.pop(probe_id, None)
                    if previous: previous[1].cancel()
                    timer = asyncio.get_running_loop().call_later(5.0, self._expire_metrics, probe_id)
                    self._metric_probes[probe_id] = (time.perf_counter(), timer)
                await asyncio.wait_for(
                    self._client.write_gatt_char(proto.CONTROL_CMD_UUID, payload, response=True),
                    timeout=3.0,
                )
                cmd_name = command.get("cmd", "")
                if cmd_name.startswith("train_") or cmd_name.startswith("game_") or cmd_name in ("set_keyer", "get_device_info"):
                    task = asyncio.ensure_future(self._reconcileControlState())
                    self._background_tasks.add(task)
                    task.add_done_callback(self._background_tasks.discard)
            except Exception as exc:  # noqa: BLE001
                if command.get("cmd") in ("probe_keyer", "reset_keyer_metrics"):
                    self._expire_metrics(command["id"])
                    continue
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
        except asyncio.CancelledError:
            pass
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value, message=str(exc), operation="connect"))
        finally:
            self._emit_connection(ConnectionInfo(state=ConnectionState.DISCONNECTED))
            self._loop.close()
            self._loop = None
            self._connect_task = None
            if self._system_pairing is not None:
                target, token = self._system_pairing
                self._system_pairing = None
                self._emit_pairing(PairingEvent(PairingEventType.PAIRING_FAILED,
                    deviceAddress=target, attemptId=token,
                    errorMessage="System pairing did not complete. Check the PIN on your device and retry."))

    def _on_word_notify(self, _characteristic, data: bytearray) -> None:
        self._last_seen = datetime.now(timezone.utc).isoformat()
        # Same characteristic carries two payload shapes, told apart by
        # JSON key: "live" is the whole in-progress word so far (fires
        # per character, never itself authoritative), "word" is the
        # word-gap-finalized event (fires once per completed word).
        #
        # This characteristic also carries "pat" (live dit/dah pattern),
        # so with three event kinds sharing one NOTIFY-only (no delivery
        # guarantee) characteristic, an occasional empty/truncated/
        # garbled payload is expected transport noise, not a real fault
        # - same reasoning _reconcileControlState()'s docstring gives for
        # BLE_CONTROL_EVT_UUID silently losing notifies. Surfacing every
        # one of these as a user-facing INTERNAL_ERROR would alarm the
        # user over something that isn't actionable and doesn't affect
        # correctness (the next notify on this channel is seconds away
        # at most). Drop it quietly instead.
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
        except Exception:  # noqa: BLE001
            return
        try:
            if "pat" in payload:
                self._emit_keyer_pattern(LivePatternEvent(
                    pattern=str(payload.get("pat", "")),
                    timestamp=int(payload.get("timestamp", 0)),
                ))
                return
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
            # A payload that *parsed* as JSON but doesn't match the
            # expected shape is more likely a real bug than transport
            # noise, so this one still surfaces.
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value,
                                           message=f"Malformed keyer payload: {exc}"))

    def _on_control_notify(self, _characteristic, data: bytearray) -> None:
        guard = self._echo_guard
        if guard is not None and bytes(data) == guard[0] and time.monotonic() < guard[1]:
            return  # echo of our own read, not a new event
        self._last_seen = datetime.now(timezone.utc).isoformat()
        try:
            payload = json.loads(bytes(data).decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.INTERNAL_ERROR.value,
                                           message=f"Malformed control payload: {exc}"))
            return
        self._handle_control_payload(payload)

    def _handle_control_payload(self, payload: dict) -> None:
        evt = payload.get("evt")
        if evt == "keyer_metrics":
            probe_id = payload.get("id")
            if not isinstance(probe_id, str): return
            pending = self._metric_probes.pop(probe_id, None)
            if pending is None:
                return  # Unsolicited/stale readback cannot masquerade as a measured RTT.
            started, timer = pending
            timer.cancel()
            self._emit_metrics({**payload, "bleRoundTripMs": (time.perf_counter() - started) * 1000})
        elif evt == "train_state":
            self._emit_training(TrainingState(
                active=bool(payload.get("active")),
                mode=payload.get("mode"),
                phase=payload.get("phase"),
                target=payload.get("target"),
                correct=int(payload.get("correct", 0)),
                attempts=int(payload.get("attempts", 0)),
                kochLevel=payload.get("kochLevel"),
                adaptiveWpm=payload.get("adaptiveWpm"),
                wpm=payload.get("wpm"),
                examScorePercent=payload.get("examScorePercent"),
                examPassed=payload.get("examPassed"),
                examCorrectCount=payload.get("examCorrect"),
                examTotalCount=payload.get("examTotal"),
            ))
        elif evt == "game_state":
            self._emit_game(GameState(
                active=bool(payload.get("active")),
                game=payload.get("game"),
                paused=bool(payload.get("paused", False)),
                phase=payload.get("phase"),
                highScore=int(payload.get("highScore", 0)),
                target=payload.get("target"),
                score=payload.get("score"),
                lives=payload.get("lives"),
                fallProgressPct=payload.get("fallProgressPct"),
                chainLength=payload.get("chainLength"),
                inputProgress=payload.get("inputProgress"),
                chain=payload.get("chain"),
                combo=payload.get("combo"),
                beatRemainingMs=payload.get("beatRemainingMs"),
                lastChar=payload.get("lastChar"),
                wasLastCorrect=payload.get("wasLastCorrect"),
                wpm=payload.get("wpm"),
            ))
        elif evt == "device_info":
            self._emit_device_info({
                "firmwareVersion": payload.get("firmwareVersion"),
                "wpm": payload.get("wpm"),
                "sidetoneHz": payload.get("sidetoneHz"),
                "sidetoneEnabled": payload.get("sidetoneEnabled"),
                "volume": payload.get("volume"),
                "paddleReversed": payload.get("paddleReversed"),
                "mode": payload.get("mode"),
                "iambicMode": payload.get("iambicMode"),
                "weightPercent": payload.get("weightPercent"),
            })
        elif evt == "error":
            message = payload.get("message", "unknown error")
            if message == self._last_error_msg:
                return  # duplicate or echo of the refusal already reported for this command
            self._last_error_msg = message
            self._emit_error(BackendError(code=ErrorCode.TRAINING_START_FAILED.value, message=message))
        # "ack" is intentionally not surfaced here - fire-and-forget
        # bookkeeping the contract doesn't require a listener for.

    async def _reconcileControlState(self) -> None:
        """Guaranteed correction for train_state/game_state after a
        command we just sent - see transport.cpp's BLE_CONTROL_EVT_UUID
        comment for why this exists: BLE_CONTROL_EVT_UUID is a plain
        NOTIFY (no delivery guarantee - confirmed via live capture that
        it drops the one-shot active:false confirmation roughly a
        quarter to half the time), so a train_stop/train_start/
        train_confirm can silently leave the cached TrainingState stale
        with nothing to self-correct it. The characteristic's underlying
        VALUE is set on every firmware-side push regardless of whether
        the notify itself is delivered, so a plain GATT READ - issued
        shortly after the write, once the firmware's own control-state
        push (rate-limited to at most every 150ms) has had time to run -
        is unaffected by that loss and always reflects the truth.
        """
        if self._client is None or not self._client.is_connected:
            return
        await asyncio.sleep(0.3)
        if self._client is None or not self._client.is_connected:
            return
        try:
            raw = await self._client.read_gatt_char(proto.CONTROL_EVT_UUID)
            payload = json.loads(bytes(raw).decode("utf-8"))
        except Exception:  # noqa: BLE001
            return
        self._echo_guard = (bytes(raw), time.monotonic() + 0.8)
        # An error is an event, not state: the notification already delivered it, and
        # this re-read returns the same event again. Re-emitting it made one refusal
        # ("keyer busy") fail a later, unrelated command in the app.
        if payload.get("evt") == "error":
            return
        self._handle_control_payload(payload)

    async def _async_connect_main(self, address: Optional[str]) -> None:
        self._connect_task = asyncio.current_task()
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
        writer_task: Optional[asyncio.Task] = None
        try:
            async with BleakClient(device, pair=self._system_pairing is not None,
                                   timeout=60 if self._system_pairing is not None else 30) as client:
                self._client = client
                self._cmd_queue = asyncio.PriorityQueue()
                self._key_backlog = 0
                writer_task = asyncio.create_task(self._command_writer())
                await client.start_notify(proto.WORD_CHAR_UUID, self._on_word_notify)
                await client.start_notify(proto.CONTROL_EVT_UUID, self._on_control_notify)
                # Older firmware has no game-input characteristic; state/commands remain usable.
                if client.services.get_characteristic(proto.GAME_MORSE_UUID) is not None:
                    await client.start_notify(proto.GAME_MORSE_UUID, self._on_game_morse_notify)
                # An authenticated read proves that the secured transport is usable.
                raw = await client.read_gatt_char(proto.CONTROL_EVT_UUID)
                self._last_seen = datetime.now(timezone.utc).isoformat()
                try:
                    self._handle_control_payload(json.loads(bytes(raw).decode("utf-8")))
                except (ValueError, TypeError):
                    pass
                if self._stop_requested:
                    return
                self._notifications_ready = True
                self._emit_connection(ConnectionInfo(
                    state=ConnectionState.CONNECTED,
                    deviceName=device.name or proto.DEVICE_NAME,
                    deviceAddress=device.address,
                ))
                if self._system_pairing is not None:
                    target, token = self._system_pairing
                    self._system_pairing = None
                    self._emit_pairing(PairingEvent(PairingEventType.PAIRING_SUCCEEDED,
                        deviceAddress=target, attemptId=token))
                while client.is_connected and not self._stop_requested:
                    await asyncio.sleep(0.5)
        except Exception as exc:  # noqa: BLE001
            self._emit_error(BackendError(code=ErrorCode.CONNECTION_FAILED.value, message=str(exc), operation="connect"))
        finally:
            self._notifications_ready = False
            self._last_seen = None
            self._device_info = {}
            if writer_task is not None:
                writer_task.cancel()
            for probe_id in list(self._metric_probes):
                pending = self._metric_probes[probe_id]
                pending[1].cancel()
                self._expire_metrics(probe_id)
            self._cmd_queue = None
            self._client = None
