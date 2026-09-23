"""Page widgets for the MORPHEUS BLE client's sidebar navigation."""

from datetime import datetime

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import (
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

import protocol as proto


def section_label(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("sectionLabel")
    return lbl


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
    def __init__(self, title: str, note: str):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignCenter)
        t = QLabel(title)
        t.setObjectName("placeholderTitle")
        t.setAlignment(Qt.AlignCenter)
        n = QLabel(note)
        n.setObjectName("placeholderNote")
        n.setAlignment(Qt.AlignCenter)
        n.setWordWrap(True)
        n.setMaximumWidth(480)
        layout.addWidget(t)
        layout.addWidget(n)


# ----------------------------------------------------------------------------
class KeyerPage(QWidget):
    """Word telemetry - the original always-on BLE_WORD_CHAR_UUID feed."""

    def __init__(self):
        super().__init__()
        self._word_count = 0
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(12)

        stats_row = QHBoxLayout()
        self.wpm_label = QLabel("WPM: --")
        self.mode_label = QLabel("Mode: --")
        self.count_label = QLabel("Words received: 0")
        for lbl in (self.wpm_label, self.mode_label, self.count_label):
            lbl.setObjectName("sectionLabel")
            stats_row.addWidget(lbl)
        stats_row.addStretch(1)
        root.addLayout(stats_row)

        root.addWidget(section_label("Word Log"))
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["Time", "Word", "WPM", "Mode"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.table.setAlternatingRowColors(True)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.verticalHeader().setVisible(False)
        root.addWidget(self.table, 2)

        root.addWidget(section_label("Live Transcript"))
        self.transcript = QTextEdit()
        self.transcript.setReadOnly(True)
        root.addWidget(self.transcript, 1)

    def on_word_received(self, payload: dict):
        word = str(payload.get("word", ""))
        wpm = payload.get("wpm", "?")
        mode = str(payload.get("mode", "?"))

        self._word_count += 1
        self.count_label.setText(f"Words received: {self._word_count}")
        self.wpm_label.setText(f"WPM: {wpm}")
        self.mode_label.setText(f"Mode: {mode}")

        row = self.table.rowCount()
        self.table.insertRow(row)
        self.table.setItem(row, 0, QTableWidgetItem(datetime.now().strftime("%H:%M:%S")))
        word_item = QTableWidgetItem(word)
        word_item.setFont(QFont("Courier New", 11, QFont.Bold))
        self.table.setItem(row, 1, word_item)
        self.table.setItem(row, 2, QTableWidgetItem(str(wpm)))
        self.table.setItem(row, 3, QTableWidgetItem(mode))
        self.table.scrollToBottom()

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
        controls = QGroupBox("Session")
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
        live = QGroupBox("Live Drill")
        lgrid = QVBoxLayout(live)
        self.target_label = big_label("--")
        lgrid.addWidget(self.target_label)

        info_row = QHBoxLayout()
        self.phase_label = QLabel("Phase: --")
        self.correct_label = QLabel("Correct: 0 / 0")
        self.koch_label = QLabel("Koch Level: --")
        self.adaptive_label = QLabel("Adaptive WPM: --")
        for lbl in (self.phase_label, self.correct_label, self.koch_label, self.adaptive_label):
            lbl.setObjectName("sectionLabel")
            info_row.addWidget(lbl)
        info_row.addStretch(1)
        lgrid.addLayout(info_row)
        root.addWidget(live)

        # --- exam result ---------------------------------------------------
        self.exam_box = QGroupBox("Exam Result")
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
        key_box = QGroupBox("Answer (virtual straight key)")
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

        mode = state.get("mode", "?")
        phase = state.get("phase", "?")
        target = state.get("target", "")
        self.target_label.setText(target if target else "–")
        self.phase_label.setText(f"Mode: {mode}   Phase: {phase}")
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

        controls = QGroupBox("Session")
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

        live = QGroupBox("Live Game")
        lgrid = QVBoxLayout(live)
        self.target_label = big_label("--")
        lgrid.addWidget(self.target_label)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setTextVisible(False)
        lgrid.addWidget(self.progress)

        info_row = QHBoxLayout()
        self.phase_label = QLabel("Phase: --")
        self.score_label = QLabel("Score: --")
        self.lives_label = QLabel("Lives: --")
        self.high_label = QLabel("High Score: --")
        for lbl in (self.phase_label, self.score_label, self.lives_label, self.high_label):
            lbl.setObjectName("sectionLabel")
            info_row.addWidget(lbl)
        info_row.addStretch(1)
        lgrid.addLayout(info_row)
        root.addWidget(live)

        key_box = QGroupBox("Answer (virtual straight key)")
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
        self.phase_label.setText(f"Game: {game}   Phase: {phase_text}")
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
