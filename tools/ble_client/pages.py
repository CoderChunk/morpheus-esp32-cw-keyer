"""Page widgets for the MORPHEUS BLE client's sidebar navigation."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QFont, QTextCursor
from PySide6.QtWidgets import (
    QComboBox,
    QGraphicsDropShadowEffect,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

import protocol as proto
from widgets import (
    ACCENT,
    BarsIcon,
    BluetoothIcon,
    ChevronIcon,
    CircularKeyButton,
    DocumentIcon,
    HeroPanel,
    PieIcon,
    RoundIconButton,
    TextLinesIcon,
    WpmDial,
)


def section_label(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("sectionLabel")
    return lbl


def card(title: str) -> QGroupBox:
    """A QGroupBox with a subtle drop shadow, used as the card container
    for every section on the Training/Games pages."""
    box = QGroupBox(title)
    shadow = QGraphicsDropShadowEffect(box)
    shadow.setBlurRadius(28)
    shadow.setOffset(0, 6)
    shadow.setColor(QColor(0, 0, 0, 90))
    box.setGraphicsEffect(shadow)
    return box


def stat_pill(icon_widget: QWidget, label: str) -> tuple[QWidget, QLabel]:
    """Icon + caption/value + trailing chevron row, styled like the
    Tone/Mode/Spacing pills in the reference template - used for the row
    of live-stat pills under the Keyer hero panel."""
    box = QWidget()
    box.setObjectName("statPill")
    shadow = QGraphicsDropShadowEffect(box)
    shadow.setBlurRadius(20)
    shadow.setOffset(0, 5)
    shadow.setColor(QColor(0, 0, 0, 90))
    box.setGraphicsEffect(shadow)

    layout = QHBoxLayout(box)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(14)

    badge = QWidget()
    badge.setObjectName("pillBadge")
    badge.setFixedSize(40, 40)
    badge_layout = QHBoxLayout(badge)
    badge_layout.setContentsMargins(0, 0, 0, 0)
    badge_layout.addWidget(icon_widget, 0, Qt.AlignCenter)
    layout.addWidget(badge)

    text_col = QVBoxLayout()
    text_col.setSpacing(2)
    caption = QLabel(label)
    caption.setObjectName("sectionLabel")
    value = QLabel("--")
    value.setObjectName("pillValue")
    text_col.addWidget(caption)
    text_col.addWidget(value)
    layout.addLayout(text_col, 1)

    chevron = ChevronIcon(14, QColor("#4a4e5e"))
    layout.addWidget(chevron, 0, Qt.AlignVCenter)
    return box, value


def big_label(text: str = "--") -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("bigTarget")
    lbl.setAlignment(Qt.AlignCenter)
    return lbl


class VirtualKeyButton(QPushButton):
    """Press-and-hold (mouse) or hold SPACE while focused - sends a
    virtual straight-key down/up pair over BLE, classified DIT/DAH by
    the firmware from hold duration exactly like a real straight key.
    """

    key_down = Signal()
    key_up = Signal()

    def __init__(self):
        super().__init__("HOLD TO KEY  (or hold SPACE while focused)")
        self.setObjectName("virtualKey")
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumHeight(64)
        self._pressed = False

    def mousePressEvent(self, event):
        self._press()
        super().mousePressEvent(event)

    def mouseReleaseEvent(self, event):
        self._release()
        super().mouseReleaseEvent(event)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Space and not event.isAutoRepeat():
            self._press()
            return
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key_Space and not event.isAutoRepeat():
            self._release()
            return
        super().keyReleaseEvent(event)

    def _press(self):
        if not self._pressed:
            self._pressed = True
            self.key_down.emit()

    def _release(self):
        if self._pressed:
            self._pressed = False
            self.key_up.emit()


# ----------------------------------------------------------------------------
class PlaceholderPage(QWidget):
    def __init__(self, title: str, note: str, icon: str = ""):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignCenter)
        layout.setSpacing(10)
        if icon:
            i = QLabel(icon)
            i.setObjectName("placeholderIcon")
            i.setAlignment(Qt.AlignCenter)
            layout.addWidget(i)
        t = QLabel(title)
        t.setObjectName("placeholderTitle")
        t.setAlignment(Qt.AlignCenter)
        n = QLabel(note)
        n.setObjectName("placeholderNote")
        n.setAlignment(Qt.AlignCenter)
        n.setWordWrap(True)
        n.setMaximumWidth(520)
        layout.addWidget(t)
        layout.addWidget(n)


# ----------------------------------------------------------------------------
class KeyerPage(QWidget):
    """Word telemetry - the original always-on BLE_WORD_CHAR_UUID feed -
    plus a real virtual straight key (key_down/key_up, the same command
    Training/Games use). The WPM dial is a live readout, not a remote
    speed control: the firmware's BLE protocol has no command to set
    keying speed outside Training's adaptive mode, so there is nothing
    honest for +/- buttons here to do.
    """

    command_requested = Signal(dict)

    _TRANSCRIPT_SIZES = (12, 15, 19)

    def __init__(self):
        super().__init__()
        self._word_count = 0
        self._transcript_size_idx = 1
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(18)

        top_row = QHBoxLayout()
        top_row.setSpacing(18)
        top_row.addWidget(self._build_hero(), 2)
        top_row.addWidget(self._build_side_column(), 1)
        root.addLayout(top_row)

        pills_row = QHBoxLayout()
        pills_row.setSpacing(14)
        mode_pill, self.mode_value = stat_pill(BarsIcon(18, ACCENT), "MODE")
        words_pill, self.count_value = stat_pill(PieIcon(18, ACCENT), "WORDS RECEIVED")
        last_pill, self.last_word_value = stat_pill(TextLinesIcon(18, ACCENT), "LAST WORD")
        self.count_value.setText("0")
        for pill in (mode_pill, words_pill, last_pill):
            pills_row.addWidget(pill)
        root.addLayout(pills_row)

        transcript_card = QWidget()
        transcript_card.setObjectName("plainPanel")
        tshadow = QGraphicsDropShadowEffect(transcript_card)
        tshadow.setBlurRadius(28)
        tshadow.setOffset(0, 6)
        tshadow.setColor(QColor(0, 0, 0, 90))
        transcript_card.setGraphicsEffect(tshadow)
        tlayout = QVBoxLayout(transcript_card)
        tlayout.setContentsMargins(20, 16, 20, 20)
        tlayout.setSpacing(12)

        header = QHBoxLayout()
        header.setSpacing(10)
        header.addWidget(DocumentIcon(16, QColor("#9096ab")))
        title = QLabel("Live Transcript")
        title.setObjectName("panelTitle")
        header.addWidget(title)
        header.addStretch(1)
        self.font_btn = QPushButton("Aa")
        self.font_btn.setObjectName("compactButton")
        self.font_btn.setFixedWidth(56)
        self.font_btn.setToolTip("Cycle transcript text size")
        self.font_btn.clicked.connect(self._on_cycle_font_size)
        header.addWidget(self.font_btn)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("dangerButton")
        clear_btn.clicked.connect(self._on_clear)
        header.addWidget(clear_btn)
        tlayout.addLayout(header)

        self.transcript = QTextEdit()
        self.transcript.setReadOnly(True)
        self.transcript.setMinimumHeight(140)
        tlayout.addWidget(self.transcript)
        root.addWidget(transcript_card)

        self._apply_transcript_font()

    # ------------------------------------------------------------------
    def _build_hero(self) -> QWidget:
        hero = HeroPanel()
        layout = QVBoxLayout(hero)
        layout.setContentsMargins(36, 30, 36, 30)
        layout.setSpacing(4)

        title = QLabel("MORPHEUS")
        title.setObjectName("heroTitle")
        subtitle = QLabel("CW KEYER  ·  BLE TELEMETRY")
        subtitle.setObjectName("heroSubtitle")
        tagline = QLabel("Hear  ·  Practice  ·  Connect")
        tagline.setObjectName("heroTagline")
        layout.addWidget(title)
        layout.addWidget(subtitle)
        layout.addWidget(tagline)
        layout.addSpacing(8)

        dial_row = QHBoxLayout()
        dial_row.addStretch(1)
        minus_btn = RoundIconButton("−")
        minus_btn.setEnabled(False)
        minus_btn.setToolTip(
            "Keying speed isn't remotely settable outside Training - this "
            "dial shows the live WPM reported with each received word."
        )
        dial_row.addWidget(minus_btn, 0, Qt.AlignVCenter)
        self.wpm_dial = WpmDial()
        dial_row.addWidget(self.wpm_dial)
        plus_btn = RoundIconButton("+")
        plus_btn.setEnabled(False)
        plus_btn.setToolTip(minus_btn.toolTip())
        dial_row.addWidget(plus_btn, 0, Qt.AlignVCenter)
        dial_row.addStretch(1)
        layout.addLayout(dial_row)
        layout.addStretch(1)
        return hero

    def _build_side_column(self) -> QWidget:
        col = QWidget()
        layout = QVBoxLayout(col)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(14)

        device_card = QWidget()
        device_card.setObjectName("plainPanel")
        dshadow = QGraphicsDropShadowEffect(device_card)
        dshadow.setBlurRadius(28)
        dshadow.setOffset(0, 6)
        dshadow.setColor(QColor(0, 0, 0, 90))
        device_card.setGraphicsEffect(dshadow)
        dlayout = QVBoxLayout(device_card)
        dlayout.setContentsMargins(18, 16, 18, 18)
        dlayout.setSpacing(14)

        dheader = QHBoxLayout()
        dtitle = QLabel("Device")
        dtitle.setObjectName("panelTitle")
        dheader.addWidget(dtitle)
        dheader.addStretch(1)
        self.device_status_dot = QLabel("●")
        self.device_status_dot.setStyleSheet("color: #ff5c7a; font-size: 8pt;")
        self.device_status_text = QLabel("Disconnected")
        self.device_status_text.setObjectName("deviceStatusSmall")
        self.device_status_text.setStyleSheet("color: #ff5c7a;")
        dheader.addWidget(self.device_status_dot)
        dheader.addWidget(self.device_status_text)
        dlayout.addLayout(dheader)

        drow = QHBoxLayout()
        drow.setSpacing(12)
        dbadge = QWidget()
        dbadge.setObjectName("pillBadge")
        dbadge.setFixedSize(40, 40)
        dbadge_layout = QHBoxLayout(dbadge)
        dbadge_layout.setContentsMargins(0, 0, 0, 0)
        dbadge_layout.addWidget(BluetoothIcon(18, ACCENT), 0, Qt.AlignCenter)
        drow.addWidget(dbadge)

        dtext = QVBoxLayout()
        dtext.setSpacing(2)
        self.device_name_label = QLabel("Not connected")
        self.device_name_label.setObjectName("deviceName")
        self.device_addr_label = QLabel("--")
        self.device_addr_label.setObjectName("sectionLabel")
        dtext.addWidget(self.device_name_label)
        dtext.addWidget(self.device_addr_label)
        drow.addLayout(dtext, 1)
        drow.addWidget(ChevronIcon(14, QColor("#4a4e5e")), 0, Qt.AlignVCenter)
        dlayout.addLayout(drow)
        layout.addWidget(device_card)

        key_card = QWidget()
        key_card.setObjectName("plainPanel")
        kshadow = QGraphicsDropShadowEffect(key_card)
        kshadow.setBlurRadius(28)
        kshadow.setOffset(0, 6)
        kshadow.setColor(QColor(0, 0, 0, 90))
        key_card.setGraphicsEffect(kshadow)
        klayout = QVBoxLayout(key_card)
        klayout.setAlignment(Qt.AlignHCenter)
        klayout.setContentsMargins(18, 30, 18, 30)
        self.key_button = CircularKeyButton()
        self.key_button.key_down.connect(self._on_key_down)
        self.key_button.key_up.connect(self._on_key_up)
        klayout.addWidget(self.key_button, 0, Qt.AlignHCenter)
        klayout.addSpacing(14)
        self.key_status_label = QLabel("Ready")
        self.key_status_label.setObjectName("keyStatus")
        self.key_status_label.setAlignment(Qt.AlignCenter)
        self.key_hint_label = QLabel("Hold the button or SPACE to key")
        self.key_hint_label.setObjectName("sectionLabel")
        self.key_hint_label.setAlignment(Qt.AlignCenter)
        klayout.addWidget(self.key_status_label)
        klayout.addWidget(self.key_hint_label)
        layout.addWidget(key_card, 1)
        return col

    # ------------------------------------------------------------------
    def set_device_info(self, name: str, address: str):
        self.device_name_label.setText(name)
        self.device_addr_label.setText(address)

    def set_connected(self, connected: bool):
        color = "#3ddc84" if connected else "#ff5c7a"
        self.device_status_dot.setStyleSheet(f"color: {color}; font-size: 8pt;")
        self.device_status_text.setStyleSheet(f"color: {color};")
        self.device_status_text.setText("Connected" if connected else "Disconnected")
        if not connected:
            self.device_name_label.setText("Not connected")
            self.device_addr_label.setText("--")

    def _on_key_down(self):
        self.key_status_label.setText("Keying...")
        self.command_requested.emit({"cmd": "key_down"})

    def _on_key_up(self):
        self.key_status_label.setText("Ready")
        self.command_requested.emit({"cmd": "key_up"})

    def _on_cycle_font_size(self):
        self._transcript_size_idx = (self._transcript_size_idx + 1) % len(self._TRANSCRIPT_SIZES)
        self._apply_transcript_font()

    def _apply_transcript_font(self):
        size = self._TRANSCRIPT_SIZES[self._transcript_size_idx]
        font = QFont("Courier New", size)
        self.transcript.setFont(font)

    def _on_clear(self):
        self.transcript.clear()
        self._word_count = 0
        self.count_value.setText("0")
        self.last_word_value.setText("--")

    def on_word_received(self, payload: dict):
        word = str(payload.get("word", ""))
        wpm = payload.get("wpm", "?")
        mode = str(payload.get("mode", "?"))

        self._word_count += 1
        self.count_value.setText(str(self._word_count))
        self.mode_value.setText(mode)
        self.last_word_value.setText(word or "--")
        self.wpm_dial.set_value(wpm)

        self.transcript.moveCursor(QTextCursor.MoveOperation.End)
        self.transcript.insertPlainText(word + " ")
        self.transcript.moveCursor(QTextCursor.MoveOperation.End)


# ----------------------------------------------------------------------------
class TrainingPage(QWidget):
    command_requested = Signal(dict)

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(14)

        # --- controls ---------------------------------------------------
        controls = card("Session")
        crow = QHBoxLayout(controls)
        crow.addWidget(QLabel("Mode:"))
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(proto.TRAIN_MODES)
        crow.addWidget(self.mode_combo)
        self.start_btn = QPushButton("Start")
        self.start_btn.clicked.connect(self._on_start)
        crow.addWidget(self.start_btn)
        self.stop_btn = QPushButton("Stop")
        self.stop_btn.setObjectName("dangerButton")
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "train_stop"}))
        crow.addWidget(self.stop_btn)
        crow.addStretch(1)
        root.addWidget(controls)

        # --- live drill ---------------------------------------------------
        live = card("Live Drill")
        lgrid = QVBoxLayout(live)
        self.target_label = big_label("--")
        lgrid.addWidget(self.target_label)

        # Grid, not a single row: four stat labels in one QHBoxLayout
        # overflowed the card width and got silently clipped by Qt
        # rather than wrapping - a fixed 2x2 grid can't do that.
        info_grid = QGridLayout()
        info_grid.setHorizontalSpacing(28)
        info_grid.setVerticalSpacing(6)
        self.phase_label = QLabel("Phase: --")
        self.correct_label = QLabel("Correct: 0 / 0")
        self.koch_label = QLabel("Koch Level: --")
        self.adaptive_label = QLabel("Adaptive WPM: --")
        for lbl in (self.phase_label, self.correct_label, self.koch_label, self.adaptive_label):
            lbl.setObjectName("sectionLabel")
        info_grid.addWidget(self.phase_label, 0, 0)
        info_grid.addWidget(self.correct_label, 0, 1)
        info_grid.addWidget(self.koch_label, 1, 0)
        info_grid.addWidget(self.adaptive_label, 1, 1)
        lgrid.addLayout(info_grid)
        root.addWidget(live)

        # --- exam result ---------------------------------------------------
        self.exam_box = card("Exam Result")
        egrid = QGridLayout(self.exam_box)
        self.exam_score_label = QLabel("--")
        self.exam_pass_label = QLabel("--")
        egrid.addWidget(QLabel("Score:"), 0, 0)
        egrid.addWidget(self.exam_score_label, 0, 1)
        egrid.addWidget(QLabel("Result:"), 1, 0)
        egrid.addWidget(self.exam_pass_label, 1, 1)
        self.exam_box.setVisible(False)
        root.addWidget(self.exam_box)

        # --- virtual key ---------------------------------------------------
        key_box = card("Answer (virtual straight key)")
        kbox = QVBoxLayout(key_box)
        self.key_button = VirtualKeyButton()
        self.key_button.key_down.connect(lambda: self.command_requested.emit({"cmd": "key_down"}))
        self.key_button.key_up.connect(lambda: self.command_requested.emit({"cmd": "key_up"}))
        kbox.addWidget(self.key_button)
        root.addWidget(key_box)

        root.addStretch(1)
        self._active = False

    def _on_start(self):
        mode = self.mode_combo.currentText()
        self.command_requested.emit({"cmd": "train_start", "mode": mode})

    def on_train_state(self, state: dict):
        active = bool(state.get("active"))
        self._active = active
        self.start_btn.setEnabled(not active)
        self.stop_btn.setEnabled(active)
        self.mode_combo.setEnabled(not active)

        if not active:
            self.target_label.setText("--")
            self.phase_label.setText("Phase: --")
            self.correct_label.setText("Correct: 0 / 0")
            self.koch_label.setText("Koch Level: --")
            self.adaptive_label.setText("Adaptive WPM: --")
            self.exam_box.setVisible(False)
            return

        phase = state.get("phase", "?")
        target = state.get("target", "")
        self.target_label.setText(target if target else "–")
        self.phase_label.setText(f"Phase: {phase}")
        self.correct_label.setText(f"Correct: {state.get('correct', 0)} / {state.get('attempts', 0)}")
        self.koch_label.setText(f"Koch Level: {state.get('kochLevel', '--')}")
        self.adaptive_label.setText(f"Adaptive WPM: {state.get('adaptiveWpm', '--')}")

        if phase == "EXAM_DONE":
            self.exam_box.setVisible(True)
            self.exam_score_label.setText(f"{state.get('examScorePercent', 0)}% "
                                           f"({state.get('examCorrect', 0)}/{state.get('examTotal', 0)})")
            passed = state.get("examPassed", False)
            self.exam_pass_label.setText("PASSED" if passed else "FAILED")
        else:
            self.exam_box.setVisible(False)


# ----------------------------------------------------------------------------
class GamesPage(QWidget):
    command_requested = Signal(dict)

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(14)

        controls = card("Session")
        crow = QHBoxLayout(controls)
        crow.addWidget(QLabel("Game:"))
        self.game_combo = QComboBox()
        self.game_combo.addItems(proto.GAMES)
        crow.addWidget(self.game_combo)
        self.start_btn = QPushButton("Start")
        self.start_btn.clicked.connect(self._on_start)
        crow.addWidget(self.start_btn)
        self.pause_btn = QPushButton("Pause / Resume")
        self.pause_btn.setEnabled(False)
        self.pause_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "game_pause"}))
        crow.addWidget(self.pause_btn)
        self.restart_btn = QPushButton("Restart")
        self.restart_btn.setEnabled(False)
        self.restart_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "game_restart"}))
        crow.addWidget(self.restart_btn)
        self.stop_btn = QPushButton("Stop")
        self.stop_btn.setObjectName("dangerButton")
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "game_stop"}))
        crow.addWidget(self.stop_btn)
        crow.addStretch(1)
        root.addWidget(controls)

        live = card("Live Game")
        lgrid = QVBoxLayout(live)
        self.target_label = big_label("--")
        lgrid.addWidget(self.target_label)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setTextVisible(False)
        lgrid.addWidget(self.progress)

        info_grid = QGridLayout()
        info_grid.setHorizontalSpacing(28)
        info_grid.setVerticalSpacing(6)
        self.phase_label = QLabel("Phase: --")
        self.score_label = QLabel("Score: --")
        self.lives_label = QLabel("Lives: --")
        self.high_label = QLabel("High Score: --")
        for lbl in (self.phase_label, self.score_label, self.lives_label, self.high_label):
            lbl.setObjectName("sectionLabel")
        info_grid.addWidget(self.phase_label, 0, 0)
        info_grid.addWidget(self.score_label, 0, 1)
        info_grid.addWidget(self.lives_label, 1, 0)
        info_grid.addWidget(self.high_label, 1, 1)
        lgrid.addLayout(info_grid)
        root.addWidget(live)

        key_box = card("Answer (virtual straight key)")
        kbox = QVBoxLayout(key_box)
        self.key_button = VirtualKeyButton()
        self.key_button.key_down.connect(lambda: self.command_requested.emit({"cmd": "key_down"}))
        self.key_button.key_up.connect(lambda: self.command_requested.emit({"cmd": "key_up"}))
        kbox.addWidget(self.key_button)
        self.confirm_btn = QPushButton("Confirm (restart from Game Over)")
        self.confirm_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "game_confirm"}))
        kbox.addWidget(self.confirm_btn)
        root.addWidget(key_box)

        root.addStretch(1)

    def _on_start(self):
        game = self.game_combo.currentText()
        self.command_requested.emit({"cmd": "game_start", "game": game})

    def on_game_state(self, state: dict):
        active = bool(state.get("active"))
        self.start_btn.setEnabled(not active)
        self.pause_btn.setEnabled(active)
        self.restart_btn.setEnabled(active)
        self.stop_btn.setEnabled(active)
        self.game_combo.setEnabled(not active)

        if not active:
            self.target_label.setText("--")
            self.phase_label.setText("Phase: --")
            self.score_label.setText("Score: --")
            self.lives_label.setText("Lives: --")
            self.high_label.setText("High Score: --")
            self.progress.setValue(0)
            return

        game = state.get("game", "?")
        phase = state.get("phase", "?")
        paused = state.get("paused", False)
        phase_text = f"{phase} (PAUSED)" if paused else phase
        self.phase_label.setText(f"Phase: {phase_text}")
        self.high_label.setText(f"High Score: {state.get('highScore', '--')}")

        if game == "COPY":
            self.target_label.setText(state.get("target", "") or "–")
            self.score_label.setText(f"Score: {state.get('score', 0)}")
            self.lives_label.setText(f"Lives: {state.get('lives', 0)}")
            self.progress.setValue(int(state.get("fallProgressPct", 0)))
        elif game == "MEMORY":
            self.target_label.setText(str(state.get("chainLength", 0)))
            self.score_label.setText(f"Input progress: {state.get('inputProgress', 0)}")
            self.lives_label.setText("")
            self.progress.setValue(0)
        elif game == "SPEED":
            last = state.get("lastChar", "") or "–"
            correct = state.get("wasLastCorrect", False)
            self.target_label.setText(f"{last} {'✓' if correct else ''}")
            self.score_label.setText(f"Combo: {state.get('combo', 0)}")
            self.lives_label.setText(f"Lives: {state.get('lives', 0)}")
            self.progress.setValue(0)
