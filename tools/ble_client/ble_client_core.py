"""
Qt-signal adapter over backend.MorpheusBackend.

All the actual BLE/asyncio logic now lives in backend.py, which has no
Qt dependency (see BACKEND_API.md - it's meant to be usable by any frontend, not just this
one). BleWorker's only job is translating the backend's plain-callback
events into Qt signals so the rest of this app doesn't have to change:
Qt auto-queues signal delivery across threads as long as this QObject
stays on the main thread (it does; only the backend's own background
threads run the async work), so no locking is needed on the GUI side.
"""

from PySide6.QtCore import QObject, Signal

import backend


class BleWorker(QObject):
    status_changed = Signal(str)
    connected_changed = Signal(bool)
    device_info = Signal(str, str)
    word_received = Signal(dict)
    train_state_received = Signal(dict)
    command_error = Signal(str)
    error = Signal(str)

    def __init__(self):
        super().__init__()
        self._backend = backend.MorpheusBackend()
        self._backend.on_connection_changed(self._on_connection_changed)
        self._backend.on_keyer_word(self._on_keyer_word)
        self._backend.on_training_state(self._on_training_state)
        self._backend.on_error(self._on_backend_error)

    # ------------------------------------------------------------------
    def start(self, address: str = ""):
        self._backend.connect(address.strip() or None)

    def stop(self):
        self._backend.disconnect()

    def send_command(self, command: dict):
        """Fire-and-forget: dispatches to the matching backend command.
        Safe to call from the GUI thread; the backend marshals the
        actual write onto its own background loop.
        """
        cmd = command.get("cmd")
        if cmd == "key_down":
            self._backend.key_down()
        elif cmd == "key_up":
            self._backend.key_up()
        elif cmd == "train_start":
            self._backend.start_training(command.get("mode", ""))
        elif cmd == "train_stop":
            self._backend.stop_training()
        elif cmd == "train_confirm":
            self._backend.confirm_training()
        else:
            self.error.emit(f"Unknown command: {cmd}")

    # ------------------------------------------------------------------
    # backend.MorpheusBackend callbacks (invoked from a backend
    # background thread) - Qt signal emission below is what marshals
    # each event onto the GUI thread.
    # ------------------------------------------------------------------
    def _on_connection_changed(self, info: "backend.ConnectionInfo"):
        state = info.state.value
        if state == backend.ConnectionState.CONNECTED.value:
            self.connected_changed.emit(True)
            if info.deviceName and info.deviceAddress:
                self.device_info.emit(info.deviceName, info.deviceAddress)
            self.status_changed.emit(f"Connected: {info.deviceName} ({info.deviceAddress})")
        elif state == backend.ConnectionState.SCANNING.value:
            self.status_changed.emit("Scanning...")
        elif state == backend.ConnectionState.CONNECTING.value:
            self.status_changed.emit("Connecting...")
        elif state == backend.ConnectionState.NOT_FOUND.value:
            self.status_changed.emit("Not found")
        elif state == backend.ConnectionState.DISCONNECTED.value:
            self.connected_changed.emit(False)
            self.status_changed.emit(info.statusMessage or "Disconnected")

    def _on_keyer_word(self, evt: "backend.KeyerWordEvent"):
        self.word_received.emit({
            "word": evt.word, "wpm": evt.wpm, "mode": evt.mode, "timestamp": evt.timestamp,
        })

    def _on_training_state(self, state: "backend.TrainingState"):
        payload = {
            "active": state.active, "mode": state.mode, "phase": state.phase,
            "target": state.target, "correct": state.correct, "attempts": state.attempts,
            "kochLevel": state.kochLevel, "adaptiveWpm": state.adaptiveWpm,
            "examScorePercent": state.examScorePercent, "examPassed": state.examPassed,
            "examCorrect": state.examCorrectCount, "examTotal": state.examTotalCount,
        }
        self.train_state_received.emit(payload)

    def _on_backend_error(self, err: "backend.BackendError"):
        if err.operation in ("startTraining", "stopTraining", "confirmTraining"):
            self.command_error.emit(err.message)
        else:
            self.error.emit(err.message)
