"""In-app pairing dialog - the branded "Enter code 123456" screen that
replaces the terminal/OS-Settings detour."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)

from ble_pairing import PairingWorker


class PairingDialog(QDialog):
    def __init__(self, target_name: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"Pair {target_name}")
        self.setMinimumWidth(420)
        self.setModal(True)

        self._target_name = target_name
        self.worker = PairingWorker()
        self.worker.status_changed.connect(self._on_status)
        self.worker.passkey_entry_needed.connect(self._on_passkey_entry_needed)
        self.worker.confirmation_needed.connect(self._on_confirmation_needed)
        self.worker.passkey_display.connect(self._on_passkey_display)
        self.worker.pairing_finished.connect(self._on_finished)

        self.success = False

        root = QVBoxLayout(self)

        self.status_label = QLabel(f"Ready to pair with {target_name}.")
        self.status_label.setWordWrap(True)
        root.addWidget(self.status_label)

        self.entry_row = QHBoxLayout()
        self.passkey_edit = QLineEdit()
        self.passkey_edit.setPlaceholderText("Code shown on MORPHEUS display")
        self.passkey_edit.setMaxLength(6)
        self.passkey_edit.returnPressed.connect(self._submit_passkey)
        self.entry_row.addWidget(self.passkey_edit)
        self.submit_btn = QPushButton("Submit")
        self.submit_btn.clicked.connect(self._submit_passkey)
        self.entry_row.addWidget(self.submit_btn)
        self._set_entry_visible(False)
        root.addLayout(self.entry_row)

        self.confirm_row = QHBoxLayout()
        self.yes_btn = QPushButton("Yes, it matches")
        self.yes_btn.clicked.connect(lambda: self._submit_confirmation(True))
        self.no_btn = QPushButton("No")
        self.no_btn.setObjectName("dangerButton")
        self.no_btn.clicked.connect(lambda: self._submit_confirmation(False))
        self.confirm_row.addWidget(self.yes_btn)
        self.confirm_row.addWidget(self.no_btn)
        self._set_confirm_visible(False)
        root.addLayout(self.confirm_row)

        self.start_btn = QPushButton("Start Pairing")
        self.start_btn.clicked.connect(self._start)
        root.addWidget(self.start_btn)

        self.close_btn = QPushButton("Close")
        self.close_btn.clicked.connect(self.reject)
        root.addWidget(self.close_btn)

    def _set_entry_visible(self, visible: bool):
        self.passkey_edit.setVisible(visible)
        self.submit_btn.setVisible(visible)
        if visible:
            self.passkey_edit.setFocus()

    def _set_confirm_visible(self, visible: bool):
        self.yes_btn.setVisible(visible)
        self.no_btn.setVisible(visible)

    def _start(self):
        self.start_btn.setEnabled(False)
        self.status_label.setText(f"Looking for {self._target_name}...")
        self.worker.start_pairing(self._target_name)

    def _submit_passkey(self):
        text = self.passkey_edit.text().strip()
        if not text.isdigit():
            self.status_label.setText("Enter the 6-digit number exactly as shown on the device.")
            return
        self._set_entry_visible(False)
        self.status_label.setText("Verifying...")
        self.worker.submit_passkey(int(text))

    def _submit_confirmation(self, accepted: bool):
        self._set_confirm_visible(False)
        self.status_label.setText("Verifying..." if accepted else "Rejected.")
        self.worker.submit_confirmation(accepted)

    def _on_status(self, text: str):
        self.status_label.setText(text)

    def _on_passkey_entry_needed(self, _device_path: str):
        self.status_label.setText(
            "Enter the code shown on your MORPHEUS display:"
        )
        self._set_entry_visible(True)

    def _on_confirmation_needed(self, _device_path: str, passkey: int):
        self.status_label.setText(f"Does your MORPHEUS display show: {passkey:06d} ?")
        self._set_confirm_visible(True)

    def _on_passkey_display(self, _device_path: str, passkey: int):
        self.status_label.setText(f"MORPHEUS should show: {passkey:06d}")

    def _on_finished(self, ok: bool, message: str):
        self._set_entry_visible(False)
        self._set_confirm_visible(False)
        self.success = ok
        if ok:
            self.status_label.setText("Paired successfully.")
            self.close_btn.setText("Done")
        else:
            self.status_label.setText(f"Pairing failed: {message}")
            self.start_btn.setEnabled(True)
