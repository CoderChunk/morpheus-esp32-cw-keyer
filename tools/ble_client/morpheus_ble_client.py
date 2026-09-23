#!/usr/bin/env python3
"""
MORPHEUS BLE Test Client
========================

A desktop GUI (PySide6) that connects to a MORPHEUS keyer over Bluetooth
Low Energy: live word telemetry, plus full remote control of Training
and Games via the BLE_CONTROL_CMD/EVT protocol (ble_control.cpp in the
firmware) - including a virtual straight key so drills and games can be
played entirely from this app.

The word and control-event characteristics require an encrypted +
authenticated link, so the device must be BONDED first. On Linux, the
"Pair New Device" button handles this in-app (registers as a BlueZ
pairing agent - see ble_pairing.py) with no terminal or OS Settings
detour. On other platforms, or if dbus-next isn't installed, that
button is disabled and pairing must be done once via the OS's own
Bluetooth settings before connecting here.

Usage:
    python3 morpheus_ble_client.py

Requires: PySide6, bleak, and (Linux only) dbus-next - see requirements.txt
"""

import sys

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSizePolicy,
    QStackedWidget,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from ble_client_core import BleWorker
from pages import GamesPage, KeyerPage, PlaceholderPage, TrainingPage
import protocol as proto

try:
    from pairing_dialog import PairingDialog
    PAIRING_AVAILABLE = True
except ImportError:
    # dbus-next (or PySide6/bleak themselves) missing, or non-Linux -
    # in-app pairing is Linux-only. Degrade to a disabled button rather
    # than crashing the whole app over an optional feature.
    PairingDialog = None
    PAIRING_AVAILABLE = False

DARK_STYLESHEET = """
QMainWindow, QWidget { background-color: #1a1b22; color: #e6e6e6; font-size: 10.5pt; }
QListWidget#sidebar {
    background-color: #14151b; border: none; border-right: 1px solid #2c2e3a;
    padding: 8px 0px; outline: none;
}
QListWidget#sidebar::item {
    padding: 12px 20px; border-left: 3px solid transparent; color: #9aa0b0;
}
QListWidget#sidebar::item:selected {
    background-color: #232532; border-left: 3px solid #3a6ff0; color: #ffffff;
}
QListWidget#sidebar::item:hover:!selected { background-color: #1f2129; }
QLineEdit {
    background-color: #262835; border: 1px solid #3d3f4d; border-radius: 6px;
    padding: 6px 8px; color: #e6e6e6;
}
QComboBox {
    background-color: #262835; border: 1px solid #3d3f4d; border-radius: 6px;
    padding: 6px 8px; color: #e6e6e6; min-width: 120px;
}
QPushButton {
    background-color: #3a6ff0; color: white; border: none; border-radius: 6px;
    padding: 8px 16px; font-weight: 600;
}
QPushButton:hover { background-color: #4d7dff; }
QPushButton:disabled { background-color: #2c2e3a; color: #6a6d7a; }
QPushButton#dangerButton { background-color: #4d3f3f; }
QPushButton#dangerButton:hover { background-color: #6b4a4a; }
QPushButton#virtualKey { font-size: 13pt; padding: 20px; background-color: #2c2e3a; }
QPushButton#virtualKey:hover { background-color: #35384a; }
QPushButton#virtualKey:pressed { background-color: #3a6ff0; }
QGroupBox {
    border: 1px solid #2c2e3a; border-radius: 8px; margin-top: 12px;
    padding-top: 8px; font-weight: 600; color: #9aa0b0;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
QTableWidget {
    background-color: #1f2129; alternate-background-color: #232532;
    gridline-color: #2c2e3a; border: 1px solid #2c2e3a; border-radius: 6px;
}
QHeaderView::section {
    background-color: #262835; color: #9aa0b0; padding: 6px;
    border: none; border-bottom: 1px solid #2c2e3a; font-weight: 600;
}
QTextEdit {
    background-color: #1f2129; border: 1px solid #2c2e3a; border-radius: 6px;
    padding: 8px; font-family: 'Courier New', monospace; font-size: 13pt;
    color: #7ee787;
}
QProgressBar {
    background-color: #1f2129; border: 1px solid #2c2e3a; border-radius: 6px;
    height: 14px; text-align: center;
}
QProgressBar::chunk { background-color: #3a6ff0; border-radius: 5px; }
QLabel#statusDot { font-size: 16pt; }
QLabel#sectionLabel { color: #9aa0b0; font-weight: 600; font-size: 9pt; }
QLabel#bigTarget {
    font-size: 48pt; font-weight: 700; color: #ffffff; padding: 16px;
    background-color: #1f2129; border-radius: 10px;
}
QLabel#placeholderTitle { font-size: 20pt; font-weight: 700; color: #ffffff; }
QLabel#placeholderNote { color: #8a8d99; font-size: 10.5pt; margin-top: 8px; }
QStatusBar { background-color: #14151b; color: #9aa0b0; }
"""

SIDEBAR_SECTIONS = [
    "CW Keyer", "Training", "Statistics", "Connectivity", "Profiles",
    "Settings", "Diagnostics", "Tools", "Games", "Help",
]

PLACEHOLDER_NOTES = {
    "Statistics": "Session/lifetime statistics are tracked on-device but not yet "
                  "exposed over the BLE control protocol. Would reuse the same "
                  "evt-push pattern as train_state/game_state.",
    "Connectivity": "Connection is managed from the bar above on every page. A "
                     "dedicated page here would show paired-device management "
                     "(MORPHEUS remembers up to 3) once a 'list/forget device' "
                     "command is added to the protocol.",
    "Profiles": "Profile load/save (6 named presets) needs new commands added "
                "to ble_control.cpp - not yet implemented.",
    "Settings": "Keyer/audio/display settings read+write needs new commands "
                "added to ble_control.cpp - not yet implemented.",
    "Diagnostics": "Live diagnostics (GPIO, memory, NVS) needs new commands "
                    "added to ble_control.cpp - not yet implemented.",
    "Tools": "Nothing to show yet - mirrors the OLED's own placeholder Tools menu.",
    "Help": "MORPHEUS BLE Test Client\n\n"
            "Word telemetry, Training and Games are fully live over BLE. "
            "Other sections are placeholders for future protocol work - "
            "see docs/USER_MANUAL.md and docs/FUNCTIONALITY_STATUS.md in "
            "the firmware repo for what's implemented on-device.",
}


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MORPHEUS BLE Test Client")
        self.resize(1080, 720)

        self.worker = BleWorker()
        self.worker.status_changed.connect(self._on_status_changed)
        self.worker.connected_changed.connect(self._on_connected_changed)
        self.worker.error.connect(self._on_error)

        self._build_ui()
        self.setStyleSheet(DARK_STYLESHEET)

        # Wire pages to the worker after construction so page signal
        # handlers exist before any event can arrive.
        self.worker.word_received.connect(self.keyer_page.on_word_received)
        self.worker.train_state_received.connect(self.training_page.on_train_state)
        self.worker.game_state_received.connect(self.games_page.on_game_state)
        self.worker.command_error.connect(lambda msg: self.statusBar().showMessage(f"Device error: {msg}", 6000))
        self.training_page.command_requested.connect(self.worker.send_command)
        self.games_page.command_requested.connect(self.worker.send_command)

    # ------------------------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        outer.addLayout(self._build_connection_bar())

        body = QHBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(0)

        self.sidebar = QListWidget()
        self.sidebar.setObjectName("sidebar")
        self.sidebar.setFixedWidth(200)
        for name in SIDEBAR_SECTIONS:
            QListWidgetItem(name, self.sidebar)
        self.sidebar.currentRowChanged.connect(lambda i: self.stack.setCurrentIndex(i))
        body.addWidget(self.sidebar)

        self.stack = QStackedWidget()
        stack_container = QWidget()
        stack_layout = QVBoxLayout(stack_container)
        stack_layout.setContentsMargins(20, 20, 20, 20)
        stack_layout.addWidget(self.stack)
        body.addWidget(stack_container, 1)

        self.keyer_page = KeyerPage()
        self.training_page = TrainingPage()
        self.games_page = GamesPage()

        pages = {
            "CW Keyer": self.keyer_page,
            "Training": self.training_page,
            "Games": self.games_page,
        }
        for name in SIDEBAR_SECTIONS:
            page = pages.get(name) or PlaceholderPage(name, PLACEHOLDER_NOTES.get(name, "Not yet implemented."))
            self.stack.addWidget(page)

        outer.addLayout(body, 1)
        self.sidebar.setCurrentRow(0)

        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage(f"Service {proto.SERVICE_UUID}")

    def _build_connection_bar(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setContentsMargins(16, 12, 16, 12)

        self.status_dot = QLabel("●")
        self.status_dot.setObjectName("statusDot")
        self.status_dot.setStyleSheet("color: #e05252;")
        row.addWidget(self.status_dot)

        self.status_label = QLabel("Disconnected")
        self.status_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        row.addWidget(self.status_label, 1)

        self.pair_btn = QPushButton("Pair New Device")
        self.pair_btn.clicked.connect(self._on_pair_clicked)
        if not PAIRING_AVAILABLE:
            self.pair_btn.setEnabled(False)
            self.pair_btn.setToolTip(
                "In-app pairing needs dbus-next and is Linux-only. "
                "Pair once via your OS's Bluetooth settings instead."
            )
        row.addWidget(self.pair_btn)

        row.addWidget(QLabel("Address (optional):"))
        self.address_edit = QLineEdit()
        self.address_edit.setPlaceholderText(f"blank = scan for \"{proto.DEVICE_NAME}\"")
        self.address_edit.setFixedWidth(220)
        row.addWidget(self.address_edit)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect_clicked)
        row.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.setObjectName("dangerButton")
        self.disconnect_btn.setEnabled(False)
        self.disconnect_btn.clicked.connect(self._on_disconnect_clicked)
        row.addWidget(self.disconnect_btn)

        return row

    # ------------------------------------------------------------------
    def _on_connect_clicked(self):
        self.connect_btn.setEnabled(False)
        self.address_edit.setEnabled(False)
        self.worker.start(self.address_edit.text())

    def _on_disconnect_clicked(self):
        self.disconnect_btn.setEnabled(False)
        self.worker.stop()

    def _on_pair_clicked(self):
        dialog = PairingDialog(proto.DEVICE_NAME, self)
        dialog.exec()
        if dialog.success:
            self.statusBar().showMessage("Paired - click Connect to link up.", 6000)

    def _on_status_changed(self, text: str):
        self.status_label.setText(text)

    def _on_connected_changed(self, connected: bool):
        if connected:
            self.status_dot.setStyleSheet("color: #3fd67d;")
            self.disconnect_btn.setEnabled(True)
        else:
            self.status_dot.setStyleSheet("color: #e05252;")
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)
            self.address_edit.setEnabled(True)

    def _on_error(self, message: str):
        self.statusBar().showMessage(f"Error: {message}", 8000)

    def closeEvent(self, event):
        self.worker.stop()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
