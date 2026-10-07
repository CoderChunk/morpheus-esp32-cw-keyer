"""
Local WebSocket/JSON IPC server exposing backend.MorpheusBackend to any
language-neutral client - built for a Flutter/Dart frontend, but usable
by anything that can open a WebSocket and speak JSON.

Python remains the sole owner of all BLE/device logic (per explicit
requirement: BLE/device logic must not move into Flutter). This module
only translates MorpheusBackend's Python calls/callbacks to and from
JSON messages over a local socket - see WS_PROTOCOL.md for the
complete wire protocol this implements.

Run standalone:
    python3 ws_server.py [--host 127.0.0.1] [--port 8765]

A Flutter app is expected to either spawn this as a subprocess bundled
with the app, or connect to an already-running instance - both are
valid; this module doesn't care which.
"""

import argparse
import asyncio
import json
import logging
from dataclasses import asdict, is_dataclass
from enum import Enum
from typing import Any, Dict, Optional, Set

import websockets

import backend

logger = logging.getLogger("ws_server")

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765

EVENT_CHANNELS = (
    "gameMorseReceived",
    "connectionChanged",
    "keyerWordReceived",
    "keyerLiveWordReceived",
    "keyerLivePatternReceived",
    "trainingStateChanged",
    "gameStateChanged",
    "pairingStateChanged",
    "backendError",
    "deviceInfoChanged",
    "keyerMetricsReceived",
)


class RequestError(Exception):
    """Raised by a method handler to produce a structured error response
    (§ Error schema in WS_PROTOCOL.md) instead of a raw traceback."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def _jsonify(value: Any) -> Any:
    """Recursively converts dataclasses/Enums (backend.py's data
    contracts) into plain JSON-safe values. Everything the backend
    hands back - ConnectionInfo, TrainingState, PairingEvent,
    BackendError, Capabilities, and dicts/lists/None/primitives - is
    representable this way with no custom per-type serializer needed."""
    if is_dataclass(value) and not isinstance(value, type):
        return {k: _jsonify(v) for k, v in asdict(value).items()}
    if isinstance(value, Enum):
        return value.value
    if isinstance(value, dict):
        return {k: _jsonify(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonify(v) for v in value]
    return value


class ClientSession:
    """Per-connection subscription state - each WebSocket client has
    its own independent subscribe/unsubscribe set (§ Subscription
    lifecycle)."""

    def __init__(self, ws):
        self.ws = ws
        self.subscribed: Set[str] = set(EVENT_CHANNELS)  # default: subscribed to everything


class MorpheusWebSocketServer:
    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT):
        self.host = host
        self.port = port
        self.backend = backend.MorpheusBackend()
        self._clients: Dict[Any, ClientSession] = {}
        self._loop: Optional[asyncio.AbstractEventLoop] = None

        self.backend.on_connection_changed(lambda info: self._broadcast("connectionChanged", info))
        self.backend.on_keyer_word(lambda evt: self._broadcast("keyerWordReceived", evt))
        self.backend.on_keyer_live_word(lambda evt: self._broadcast("keyerLiveWordReceived", evt))
        self.backend.on_keyer_live_pattern(lambda evt: self._broadcast("keyerLivePatternReceived", evt))
        self.backend.on_training_state(lambda st: self._broadcast("trainingStateChanged", st))
        self.backend.on_game_morse(lambda evt: self._broadcast("gameMorseReceived", evt))
        self.backend.on_game_state(lambda st: self._broadcast("gameStateChanged", st))
        self.backend.on_pairing_state(lambda evt: self._broadcast("pairingStateChanged", evt))
        self.backend.on_error(lambda err: self._broadcast("backendError", err))
        self.backend.on_keyer_metrics(lambda metrics: self._broadcast("keyerMetricsReceived", metrics))
        self.backend.on_device_info_changed(lambda info: self._broadcast("deviceInfoChanged", info))

    # ------------------------------------------------------------------
    # Outbound: backend callbacks -> event frames
    # ------------------------------------------------------------------
    def _broadcast(self, channel: str, payload: Any) -> None:
        # Called from one of MorpheusBackend's own background threads
        # (its GATT loop or its pairing loop) - never the server's own
        # asyncio loop - so this hops onto that loop thread-safely
        # rather than touching any WebSocket directly from here.
        if self._loop is None:
            return
        asyncio.run_coroutine_threadsafe(self._async_broadcast(channel, payload), self._loop)

    async def _async_broadcast(self, channel: str, payload: Any) -> None:
        message = json.dumps({"type": "event", "event": channel, "data": _jsonify(payload)})
        dead = []
        for ws, session in list(self._clients.items()):
            if channel not in session.subscribed:
                continue
            try:
                await ws.send(message)
            except websockets.exceptions.ConnectionClosed:
                dead.append(ws)
        for ws in dead:
            self._clients.pop(ws, None)

    # ------------------------------------------------------------------
    # Inbound: request frames -> backend calls
    # ------------------------------------------------------------------
    async def _dispatch(self, session: ClientSession, method: str, params: dict) -> Any:
        b = self.backend

        if method == "scan":
            b.scan()
            return None
        if method == "connect":
            await b.prepare_connect()
            b.connect(params.get("deviceAddress"))
            return None
        if method == "disconnect":
            b.disconnect()
            return None
        if method == "keyDown":
            b.key_down()
            return None
        if method == "keyUp":
            b.key_up()
            return None
        if method == "startTraining":
            mode = params.get("mode")
            if not mode:
                raise RequestError("INVALID_PARAMETER", "params.mode is required")
            b.start_training(mode, params.get("kochLevel"))
            return None
        if method == "stopTraining":
            b.stop_training()
            return None
        if method == "confirmTraining":
            b.confirm_training()
            return None
        if method == "answerTraining":
            text = params.get("text")
            if not text:
                raise RequestError("INVALID_PARAMETER", "params.text is required")
            b.answer_training(str(text))
            return None
        if method == "startGame":
            game = params.get("game")
            if not game:
                raise RequestError("INVALID_PARAMETER", "params.game is required")
            b.start_game(game)
            return None
        if method == "stopGame":
            b.stop_game()
            return None
        if method == "pauseGame":
            b.pause_game()
            return None
        if method == "confirmGame":
            b.confirm_game()
            return None
        if method == "restartGame":
            b.restart_game()
            return None
        if method == "discoverDevices":
            return {"devices": await b.discover_devices()}
        if method == "listPairedDevices":
            return {"devices": await b.list_paired_devices()}
        if method == "getDeviceRuntime":
            return b.get_device_runtime()
        if method == "removePairing":
            await b.remove_pairing(params.get("deviceAddress"))
            return None
        if method == "cancelPairing":
            b.cancel_pairing(params.get("attemptId"))
            await b.wait_pairing_idle()
            await b.prepare_connect()
            return None
        if method == "startPairing":
            import protocol as proto
            await b.wait_pairing_idle()
            await b.prepare_connect()
            b.start_pairing(params.get("targetDeviceName", proto.DEVICE_NAME),
                            params.get("deviceAddress"), params.get("attemptId"))
            return None
        if method == "submitPasskey":
            passkey = params.get("passkey")
            if passkey is None:
                raise RequestError("INVALID_PARAMETER", "params.passkey is required")
            b.submit_passkey(passkey, params.get("attemptId"))
            return None
        if method == "confirmPairing":
            b.confirm_pairing(params.get("accepted"), params.get("attemptId"))
            return None
        if method == "setKeyerSetting":
            b.set_keyer_setting(params.get("field"), params.get("value"))
            return None
        if method == "probeKeyerMetrics":
            b.request_keyer_metrics(params.get("id"), params.get("reset", False))
            return None  # Reply with dispatch only; real device response is a correlated event.
        if method == "requestDeviceInfo":
            b.request_device_info()
            return None
        if method == "getSnapshot":
            return b.get_snapshot()
        if method == "getMorseTable":
            return {"table": b.get_morse_table(), "reverse": b.get_reverse_morse_table()}
        if method == "getKochSequence":
            return {"sequence": b.get_koch_sequence()}
        if method == "subscribe":
            events = params.get("events") or list(EVENT_CHANNELS)
            session.subscribed |= (set(events) & set(EVENT_CHANNELS))
            return {"subscribed": sorted(session.subscribed)}
        if method == "unsubscribe":
            events = params.get("events") or list(EVENT_CHANNELS)
            session.subscribed -= set(events)
            return {"subscribed": sorted(session.subscribed)}

        raise RequestError("UNSUPPORTED_OPERATION", f"Unknown method: {method!r}")

    async def _handle_message(self, session: ClientSession, raw: str) -> None:
        ws = session.ws
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            await ws.send(json.dumps({
                "type": "response", "id": None, "ok": False,
                "error": {"code": "INVALID_COMMAND", "message": "Malformed JSON"},
            }))
            return

        msg_id = msg.get("id")
        method = msg.get("method")
        params = msg.get("params") or {}

        try:
            if not method:
                raise RequestError("INVALID_COMMAND", "params.method is required")
            result = await self._dispatch(session, method, params)
            await ws.send(json.dumps({"type": "response", "id": msg_id, "ok": True, "result": _jsonify(result)}))
        except RequestError as exc:
            await ws.send(json.dumps({
                "type": "response", "id": msg_id, "ok": False,
                "error": {"code": exc.code, "message": exc.message},
            }))
        except Exception as exc:  # noqa: BLE001
            logger.exception("Unhandled error dispatching %r", method)
            await ws.send(json.dumps({
                "type": "response", "id": msg_id, "ok": False,
                "error": {"code": "INTERNAL_ERROR", "message": str(exc)},
            }))

    async def _handle_client(self, ws) -> None:
        session = ClientSession(ws)
        self._clients[ws] = session
        logger.info("Client connected (%d total)", len(self._clients))
        try:
            try:
                async for raw in ws:
                    await self._handle_message(session, raw)
            except websockets.exceptions.ConnectionClosed:
                # `websockets`'s own async-for loop already absorbs a
                # clean close (ConnectionClosedOK) as normal iteration
                # end - verified empirically, not just per the docs -
                # so in practice this only ever fires for the abrupt
                # case (e.g. the OS killing the client process, no
                # close frame exchanged at all). That's still an
                # ordinary end-of-session event for a local IPC socket,
                # not a server error. Without this handler, the
                # exception propagates out of this coroutine and
                # `websockets` logs it as "connection handler failed"
                # with a full traceback.
                logger.info(
                    "Client connection closed (code=%s, reason=%r)",
                    ws.close_code, ws.close_reason,
                )
        finally:
            self._clients.pop(ws, None)
            logger.info("Client disconnected (%d total)", len(self._clients))

    async def serve_forever(self) -> None:
        self._loop = asyncio.get_running_loop()
        async with websockets.serve(self._handle_client, self.host, self.port):
            logger.info("MORPHEUS backend WebSocket server listening on ws://%s:%d", self.host, self.port)
            await asyncio.Future()  # run until cancelled


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                         format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    server = MorpheusWebSocketServer(args.host, args.port)
    asyncio.run(server.serve_forever())


if __name__ == "__main__":
    main()
