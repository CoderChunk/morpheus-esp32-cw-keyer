"""
Qt-signal adapter over backend.MorpheusBackend's pairing operations.

All the BlueZ Agent1 D-Bus logic now lives in pairing_backend.py,
imported lazily by backend.py (see backend.py's module docstring).
PairingWorker's only job is translating the backend's plain-callback
pairing events into Qt signals for pairing_dialog.py, unchanged from
before this split - the security model and pairing UX are identical to
the original single-file implementation.
"""

from PySide6.QtCore import QObject, Signal

import backend


class PairingWorker(QObject):
    status_changed = Signal(str)
    passkey_entry_needed = Signal(str)          # device object path
    confirmation_needed = Signal(str, int)       # device object path, passkey
    passkey_display = Signal(str, int)           # informational only
    pairing_finished = Signal(bool, str)         # ok, message (device path on success)

    def __init__(self):
        super().__init__()
        self._backend = backend.MorpheusBackend()
        self._backend.on_pairing_state(self._on_pairing_event)

    # ------------------------------------------------------------------
    def start_pairing(self, target_name: str):
        self._backend.start_pairing(target_name)

    def submit_passkey(self, value: int):
        self._backend.submit_passkey(str(value))

    def submit_confirmation(self, accepted: bool):
        self._backend.confirm_pairing(accepted)

    # ------------------------------------------------------------------
    def _on_pairing_event(self, evt: "backend.PairingEvent"):
        PairingEventType = backend.PairingEventType
        if evt.type == PairingEventType.PAIRING_STARTED:
            self.status_changed.emit("Connecting to system D-Bus...")
        elif evt.type == PairingEventType.PAIRING_WAITING_FOR_PASSKEY:
            if evt.passkey is not None:
                self.passkey_display.emit(evt.devicePath or "", evt.passkey)
            else:
                self.passkey_entry_needed.emit(evt.devicePath or "")
        elif evt.type == PairingEventType.PAIRING_WAITING_FOR_CONFIRMATION:
            self.confirmation_needed.emit(evt.devicePath or "", evt.passkey or 0)
        elif evt.type == PairingEventType.PAIRING_SUCCEEDED:
            self.pairing_finished.emit(True, evt.devicePath or "")
        elif evt.type in (PairingEventType.PAIRING_FAILED, PairingEventType.PAIRING_UNAVAILABLE):
            self.pairing_finished.emit(False, evt.errorMessage or "Pairing failed")
