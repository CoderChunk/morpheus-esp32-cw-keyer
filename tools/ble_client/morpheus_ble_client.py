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

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QGraphicsDropShadowEffect,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QScrollArea,
    QStackedWidget,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from ble_client_core import BleWorker
from pages import GamesPage, KeyerPage, PlaceholderPage, TrainingPage
import protocol as proto
from widgets import (
    ACCENT,
    BarsIcon,
    BluetoothIcon,
    GearIcon,
    HomeIcon,
    LetterBadge,
    LinkIcon,
    LogoMark,
    MUTED,
    PieIcon,
    WHITE,
)

try:
    from pairing_dialog import PairingDialog
    PAIRING_AVAILABLE = True
except ImportError:
    # dbus-next (or PySide6/bleak themselves) missing, or non-Linux -
    # in-app pairing is Linux-only. Degrade to a disabled button rather
    # than crashing the whole app over an optional feature.
    PairingDialog = None
    PAIRING_AVAILABLE = False

# Modern flat-dark palette: three background tiers (app / panel / card),
# one accent, semantic red/green for disconnected/connected. Fonts and
# paddings are sized for a maximized window, not a small fixed dialog.
DARK_STYLESHEET = """
QMainWindow, QWidget { background-color: #0f1117; color: #e8e9ee; font-size: 11pt; }
QLabel { background-color: transparent; }

QWidget#connectionBar {
    background-color: #161825; border-bottom: 1px solid #262939;
}
QWidget#sidebarPanel { background-color: #12141d; border-right: 1px solid #232636; }
QLabel#brandTitle { font-size: 17pt; font-weight: 800; color: #ffffff; letter-spacing: 1px; }
QLabel#brandSubtitle { font-size: 8.5pt; color: #6c7086; font-weight: 600; }

QListWidget#sidebar {
    background-color: transparent; border: none; padding: 6px 10px; outline: none;
}
QListWidget#sidebar::item {
    padding: 0px; margin: 2px 10px; border-radius: 10px; color: #9096ab;
    font-size: 11pt; font-weight: 500;
}
QListWidget#sidebar::item:selected {
    background-color: #5b7cfa; color: #ffffff; font-weight: 700;
}
QListWidget#sidebar::item:hover:!selected { background-color: #1c1f2e; color: #e8e9ee; }
QListWidget#sidebar::item:focus { border: none; outline: none; }

QLineEdit {
    background-color: #1c1f2e; border: 1px solid #2c2f42; border-radius: 8px;
    padding: 9px 12px; color: #e8e9ee; selection-background-color: #5b7cfa;
}
QLineEdit:focus { border: 1px solid #5b7cfa; }
QComboBox {
    background-color: #1c1f2e; border: 1px solid #2c2f42; border-radius: 8px;
    padding: 9px 12px; color: #e8e9ee; min-width: 140px;
}
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
    background-color: #1c1f2e; color: #e8e9ee; border: 1px solid #2c2f42;
    selection-background-color: #5b7cfa; outline: none;
}

QPushButton {
    background-color: #5b7cfa; color: white; border: none; border-radius: 8px;
    padding: 10px 20px; font-weight: 700;
}
QPushButton:hover { background-color: #6f8dfb; }
QPushButton:pressed { background-color: #4a68d9; }
QPushButton:disabled { background-color: #1c1f2e; color: #4a4e5e; }
QPushButton#dangerButton { background-color: #2c1f26; color: #ff8080; }
QPushButton#dangerButton:hover { background-color: #3a2530; }
QPushButton#dangerButton:disabled { background-color: #1c1f2e; color: #4a4e5e; }
QPushButton#virtualKey {
    font-size: 14pt; padding: 34px; background-color: #1c1f2e; color: #c7cbdb;
    border: 2px solid #2c2f42; border-radius: 14px;
}
QPushButton#virtualKey:hover { border-color: #5b7cfa; color: #ffffff; }
QPushButton#virtualKey:pressed { background-color: #5b7cfa; color: #ffffff; border-color: #5b7cfa; }
QPushButton#compactButton { padding: 10px 6px; }
QPushButton#roundIconButton {
    padding: 0px; border-radius: 22px; font-size: 16pt; font-weight: 800;
    background-color: #1c1f2e; color: #c7cbdb; border: 1px solid #2c2f42;
}
QPushButton#roundIconButton:hover:!disabled { background-color: #262a3d; }
QPushButton#roundIconButton:disabled { background-color: #171a26; color: #3a3e52; border-color: #232636; }

QGroupBox {
    background-color: #161825; border: 1px solid #232636; border-radius: 14px;
    margin-top: 18px; padding: 18px; font-weight: 700; color: #9096ab; font-size: 10pt;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 16px; top: 2px; padding: 0 8px;
    color: #c7cbdb; letter-spacing: 0.5px;
}

QTableWidget {
    background-color: #12141d; alternate-background-color: #161825;
    gridline-color: #232636; border: 1px solid #232636; border-radius: 10px;
    padding: 2px;
}
QTableWidget::item { padding: 6px; }
QHeaderView::section {
    background-color: #1c1f2e; color: #9096ab; padding: 10px;
    border: none; border-bottom: 1px solid #232636; font-weight: 700;
}
QTextEdit {
    background-color: #12141d; border: 1px solid #232636; border-radius: 10px;
    padding: 14px; font-family: 'Courier New', monospace; font-size: 13pt;
    color: #7ee787;
}
QProgressBar {
    background-color: #12141d; border: 1px solid #232636; border-radius: 8px;
    height: 18px; text-align: center; color: #e8e9ee; font-weight: 600;
}
QProgressBar::chunk { background-color: #5b7cfa; border-radius: 7px; }

QLabel#statusDot { font-size: 18pt; }
QLabel#connStatusText { font-size: 11.5pt; font-weight: 700; }
QLabel#sectionLabel { color: #9096ab; font-weight: 700; font-size: 9.5pt; }
QLabel#bigTarget {
    font-size: 64pt; font-weight: 800; color: #ffffff; padding: 28px;
    background-color: #12141d; border-radius: 16px; border: 1px solid #232636;
}
QLabel#placeholderIcon { font-size: 40pt; }
QLabel#placeholderTitle { font-size: 24pt; font-weight: 800; color: #ffffff; }
QLabel#placeholderNote { color: #8a8fa3; font-size: 11pt; margin-top: 8px; }
QStatusBar { background-color: #12141d; color: #6c7086; border-top: 1px solid #232636; }

QLabel#brandWordmark { font-size: 15pt; font-weight: 800; color: #ffffff; letter-spacing: 1px; }
QLabel#brandTagline { font-size: 8pt; color: #6c7086; font-weight: 600; letter-spacing: 0.5px; }

QWidget#devicePill {
    background-color: #1c1f2e; border: 1px solid #2c2f42; border-radius: 22px;
}
QLabel#deviceNameSmall { font-weight: 700; font-size: 10.5pt; color: #e8e9ee; }
QLabel#deviceStatusSmall { font-size: 8.5pt; font-weight: 600; }

QPushButton#gearButton {
    background-color: #1c1f2e; border: 1px solid #2c2f42; border-radius: 20px;
    padding: 0px; min-width: 40px; max-width: 40px; min-height: 40px; max-height: 40px;
}
QPushButton#gearButton:hover { background-color: #262a3d; }

QWidget#sidebarRow { background: transparent; }
QLabel#sidebarLabel { font-size: 11pt; font-weight: 600; color: #9096ab; }
QLabel#sidebarLabelActive { font-size: 11pt; font-weight: 700; color: #ffffff; }
QLabel#sidebarFooterBrand { font-size: 9.5pt; font-weight: 700; color: #c7cbdb; }
QLabel#sidebarFooterVersion { font-size: 8pt; color: #6c7086; }

QWidget#statPill { background-color: #161825; border: 1px solid #232636; border-radius: 14px; }
QWidget#pillBadge { background-color: #1c1f2e; border-radius: 10px; }
QLabel#pillValue { font-size: 14pt; font-weight: 800; color: #ffffff; }

QWidget#plainPanel { background-color: #161825; border: 1px solid #232636; border-radius: 16px; }
QLabel#panelTitle { font-size: 10pt; font-weight: 700; color: #c7cbdb; letter-spacing: 0.5px; }

QLabel#heroTitle { font-size: 26pt; font-weight: 800; color: #ffffff; letter-spacing: 2px; }
QLabel#heroSubtitle { font-size: 10pt; font-weight: 700; color: #cdd3ea; letter-spacing: 1px; }
QLabel#heroTagline { font-size: 9.5pt; color: #9aa1bd; }

QLabel#deviceName { font-size: 12.5pt; font-weight: 700; color: #ffffff; }
QLabel#keyStatus { font-size: 13pt; font-weight: 800; color: #ffffff; margin-top: 6px; }

QWidget#heroCard { background: transparent; }
QScrollArea#pageScroll { background: transparent; border: none; }
QScrollArea#pageScroll > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #0f1117; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #2c2f42; border-radius: 6px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #3a3e55; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
"""

def _centered(widget: QWidget, max_width: int) -> QWidget:
    """Wraps a page so it stays a readable width and centers on a wide
    or maximized window, instead of stretching thin forms edge-to-edge."""
    container = QWidget()
    layout = QHBoxLayout(container)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.addStretch(1)
    widget.setMaximumWidth(max_width)
    layout.addWidget(widget)
    layout.addStretch(1)
    return container


SIDEBAR_SECTIONS = [
    "CW Keyer", "Training", "Statistics", "Connectivity", "Profiles",
    "Settings", "Diagnostics", "Tools", "Games", "Help",
]

# Emoji-style icon glyphs were tried first and verified (via real
# screenshots) to render as broken/missing tofu boxes on this system's
# fonts. widgets.py draws real vector icons instead for the five
# sections the reference template shows explicitly; everything else
# falls back to a plain-letter badge (ASCII always renders safely).
SIDEBAR_ICON_FACTORY = {
    "CW Keyer": lambda: HomeIcon(19, MUTED),
    "Training": lambda: BarsIcon(19, MUTED),
    "Statistics": lambda: PieIcon(19, MUTED),
    "Connectivity": lambda: LinkIcon(19, MUTED),
    "Settings": lambda: GearIcon(19, MUTED),
}


def _make_sidebar_icon(name: str):
    factory = SIDEBAR_ICON_FACTORY.get(name)
    if factory:
        return factory()
    return LetterBadge(name[0], 19)

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
        self.resize(1440, 900)
        self.setMinimumSize(1000, 640)

        self.worker = BleWorker()
        self.worker.status_changed.connect(self._on_status_changed)
        self.worker.connected_changed.connect(self._on_connected_changed)
        self.worker.device_info.connect(self._on_device_info)
        self.worker.error.connect(self._on_error)

        self._build_ui()
        self.setStyleSheet(DARK_STYLESHEET)

        # Wire pages to the worker after construction so page signal
        # handlers exist before any event can arrive.
        self.worker.word_received.connect(self.keyer_page.on_word_received)
        self.worker.train_state_received.connect(self.training_page.on_train_state)
        self.worker.game_state_received.connect(self.games_page.on_game_state)
        self.worker.command_error.connect(lambda msg: self.statusBar().showMessage(f"Device error: {msg}", 6000))
        self.keyer_page.command_requested.connect(self.worker.send_command)
        self.training_page.command_requested.connect(self.worker.send_command)
        self.games_page.command_requested.connect(self.worker.send_command)

    # ------------------------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        outer.addWidget(self._build_connection_bar())

        body = QHBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(0)
        body.addWidget(self._build_sidebar())

        self.stack = QStackedWidget()
        stack_container = QWidget()
        stack_layout = QVBoxLayout(stack_container)
        stack_layout.setContentsMargins(32, 28, 32, 28)
        stack_layout.addWidget(self.stack)

        # A page's natural content height can exceed the visible window
        # (e.g. on a smaller/tiled screen, or if a compositor hands us a
        # smaller-than-expected maximized size) - without this, the
        # bottom of the page silently clips with no way to reach it.
        scroll = QScrollArea()
        scroll.setObjectName("pageScroll")
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setFrameShape(QScrollArea.NoFrame)
        scroll.setWidget(stack_container)
        body.addWidget(scroll, 1)

        self.keyer_page = KeyerPage()
        self.training_page = TrainingPage()
        self.games_page = GamesPage()

        # Full-bleed pages (benefit from all available width, e.g. a
        # table) go in as-is; everything else is centered with a max
        # width so a maximized/ultrawide window doesn't stretch small
        # forms into an unreadable single thin row of controls.
        pages = {
            "CW Keyer": self.keyer_page,
            "Training": _centered(self.training_page, 900),
            "Games": _centered(self.games_page, 900),
        }
        for name in SIDEBAR_SECTIONS:
            if name in pages:
                page = pages[name]
            else:
                placeholder = PlaceholderPage(
                    name, PLACEHOLDER_NOTES.get(name, "Not yet implemented."), ""
                )
                page = _centered(placeholder, 620)
            self.stack.addWidget(page)

        outer.addLayout(body, 1)
        self.sidebar.setCurrentRow(0)

        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage(f"Service {proto.SERVICE_UUID}")

    def _build_sidebar(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("sidebarPanel")
        panel.setFixedWidth(240)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self.sidebar = QListWidget()
        self.sidebar.setObjectName("sidebar")
        self.sidebar.setFocusPolicy(Qt.NoFocus)
        self._sidebar_rows = []
        for name in SIDEBAR_SECTIONS:
            item = QListWidgetItem()
            row_widget, icon, label = self._make_sidebar_row(name)
            item.setSizeHint(row_widget.sizeHint())
            self.sidebar.addItem(item)
            self.sidebar.setItemWidget(item, row_widget)
            self._sidebar_rows.append((icon, label))
        self.sidebar.currentRowChanged.connect(self._on_sidebar_row_changed)
        layout.addWidget(self.sidebar, 1)

        footer = QWidget()
        footer_layout = QHBoxLayout(footer)
        footer_layout.setContentsMargins(22, 14, 22, 18)
        footer_layout.setSpacing(10)
        text_col = QVBoxLayout()
        text_col.setSpacing(1)
        brand_lbl = QLabel("MORPHEUS")
        brand_lbl.setObjectName("sidebarFooterBrand")
        version_lbl = QLabel("v1.1.0")
        version_lbl.setObjectName("sidebarFooterVersion")
        text_col.addWidget(brand_lbl)
        text_col.addWidget(version_lbl)
        footer_layout.addLayout(text_col)
        footer_layout.addStretch(1)
        self.footer_status_dot = QLabel("●")
        self.footer_status_dot.setStyleSheet("color: #ff5c7a; font-size: 12pt;")
        footer_layout.addWidget(self.footer_status_dot)
        layout.addWidget(footer)

        return panel

    def _make_sidebar_row(self, name: str):
        row = QWidget()
        row.setObjectName("sidebarRow")
        layout = QHBoxLayout(row)
        layout.setContentsMargins(14, 11, 14, 11)
        layout.setSpacing(12)
        icon = _make_sidebar_icon(name)
        layout.addWidget(icon)
        label = QLabel(name)
        label.setObjectName("sidebarLabel")
        layout.addWidget(label, 1)
        return row, icon, label

    def _on_sidebar_row_changed(self, index: int):
        self.stack.setCurrentIndex(index)
        for i, (icon, label) in enumerate(self._sidebar_rows):
            active = i == index
            icon.set_color(WHITE if active else MUTED)
            label.setObjectName("sidebarLabelActive" if active else "sidebarLabel")
            label.setStyleSheet("")
            label.style().unpolish(label)
            label.style().polish(label)

    def _build_connection_bar(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("connectionBar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(24, 14, 24, 14)
        row.setSpacing(16)

        brand = QWidget()
        brand_row = QHBoxLayout(brand)
        brand_row.setContentsMargins(0, 0, 0, 0)
        brand_row.setSpacing(10)
        brand_row.addWidget(LogoMark(36))
        brand_text = QVBoxLayout()
        brand_text.setSpacing(0)
        wordmark = QLabel("MORPHEUS")
        wordmark.setObjectName("brandWordmark")
        tagline = QLabel("CW KEYER · BLE CLIENT")
        tagline.setObjectName("brandTagline")
        brand_text.addWidget(wordmark)
        brand_text.addWidget(tagline)
        brand_row.addLayout(brand_text)
        row.addWidget(brand)

        row.addStretch(1)

        device_pill = QWidget()
        device_pill.setObjectName("devicePill")
        pill_row = QHBoxLayout(device_pill)
        pill_row.setContentsMargins(14, 8, 18, 8)
        pill_row.setSpacing(10)
        self.pill_bt_icon = BluetoothIcon(18, ACCENT)
        pill_row.addWidget(self.pill_bt_icon)
        pill_text = QVBoxLayout()
        pill_text.setSpacing(0)
        self.device_pill_name = QLabel(proto.DEVICE_NAME)
        self.device_pill_name.setObjectName("deviceNameSmall")
        status_row = QHBoxLayout()
        status_row.setSpacing(5)
        status_row.setContentsMargins(0, 0, 0, 0)
        self.status_dot = QLabel("●")
        self.status_dot.setStyleSheet("color: #ff5c7a; font-size: 8pt;")
        self.status_label = QLabel("Disconnected")
        self.status_label.setObjectName("deviceStatusSmall")
        self.status_label.setStyleSheet("color: #ff5c7a;")
        status_row.addWidget(self.status_dot)
        status_row.addWidget(self.status_label)
        status_row.addStretch(1)
        pill_text.addWidget(self.device_pill_name)
        pill_text.addLayout(status_row)
        pill_row.addLayout(pill_text)
        row.addWidget(device_pill)

        self.pair_btn = QPushButton("Pair New Device")
        self.pair_btn.clicked.connect(self._on_pair_clicked)
        if not PAIRING_AVAILABLE:
            self.pair_btn.setEnabled(False)
            self.pair_btn.setToolTip(
                "In-app pairing needs dbus-next and is Linux-only. "
                "Pair once via your OS's Bluetooth settings instead."
            )
        row.addWidget(self.pair_btn)

        row.addWidget(QLabel("Address:"))
        self.address_edit = QLineEdit()
        self.address_edit.setPlaceholderText(f"blank = scan for \"{proto.DEVICE_NAME}\"")
        self.address_edit.setFixedWidth(200)
        row.addWidget(self.address_edit)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect_clicked)
        row.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.setObjectName("dangerButton")
        self.disconnect_btn.setEnabled(False)
        self.disconnect_btn.clicked.connect(self._on_disconnect_clicked)
        row.addWidget(self.disconnect_btn)

        gear_btn = QPushButton()
        gear_btn.setObjectName("gearButton")
        gear_layout = QHBoxLayout(gear_btn)
        gear_layout.setContentsMargins(0, 0, 0, 0)
        gear_layout.addWidget(GearIcon(18, MUTED), 0, Qt.AlignCenter)
        gear_btn.setToolTip("Settings")
        gear_btn.clicked.connect(lambda: self.sidebar.setCurrentRow(SIDEBAR_SECTIONS.index("Settings")))
        row.addWidget(gear_btn)

        shadow = QGraphicsDropShadowEffect(bar)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 4)
        shadow.setColor(QColor(0, 0, 0, 120))
        bar.setGraphicsEffect(shadow)

        return bar

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
        # The device pill is small by design (matches the reference
        # template's compact status pill), so it gets a short word while
        # the full detail (e.g. "Connected: MORPHEUS-CW (AA:BB:...)")
        # goes to the status bar where there's room for it.
        short = text.split(":", 1)[0] if ":" in text else text
        self.status_label.setText(short)
        self.statusBar().showMessage(text, 6000)

    def _on_connected_changed(self, connected: bool):
        color = "#3ddc84" if connected else "#ff5c7a"
        self.status_dot.setStyleSheet(f"color: {color}; font-size: 8pt;")
        self.status_label.setStyleSheet(f"color: {color};")
        self.footer_status_dot.setStyleSheet(f"color: {color}; font-size: 12pt;")
        self.pill_bt_icon.set_color(ACCENT if connected else MUTED)
        self.keyer_page.set_connected(connected)
        if connected:
            self.disconnect_btn.setEnabled(True)
        else:
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)
            self.address_edit.setEnabled(True)
            self.device_pill_name.setText(proto.DEVICE_NAME)

    def _on_device_info(self, name: str, address: str):
        self.device_pill_name.setText(name)
        self.keyer_page.set_device_info(name, address)

    def _on_error(self, message: str):
        self.statusBar().showMessage(f"Error: {message}", 8000)

    def closeEvent(self, event):
        self.worker.stop()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    win = MainWindow()
    # Requesting the maximized window state before the window has ever
    # been shown races the xdg-shell handshake on some Wayland
    # compositors, which then reports a bogus 0x0 configure event
    # ("qt.qpa.wayland: Configure event ... invalid width/height: 0")
    # and can leave the window laid out at that bogus size - showing
    # the window at its normal geometry first, then requesting maximize
    # on the next event-loop iteration, avoids the race entirely.
    win.show()
    QTimer.singleShot(0, win.showMaximized)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
