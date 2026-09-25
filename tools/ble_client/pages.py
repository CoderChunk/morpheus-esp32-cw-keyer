"""Page widgets for the MORPHEUS BLE client's sidebar navigation."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QFont, QTextCursor
from PySide6.QtWidgets import (
    QButtonGroup,
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
    MorseGlyph,
    PieIcon,
    PulseIcon,
    RingGauge,
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
    """Styled to match the reference template's Training tab, but every
    number and control here is backed by the real BLE protocol. The
    firmware has no commands to set speed, pick a "lesson", or switch
    a listen/type input mode remotely (ble_control.cpp only accepts
    train_start/stop/confirm and key_down/up) - so unlike the template's
    mockup, this doesn't show fake WPM/lesson/mode pickers. The Koch
    progress grid uses protocol.KOCH_ORDER (an exact copy of the
    firmware's own character sequence) driven by the real live
    kochLevel field, and the Morse glyph uses protocol.MORSE_TABLE (an
    exact copy of the firmware's decoder table) for the real target
    character - not fabricated data, just real data rendered visually.
    """

    command_requested = Signal(dict)

    def __init__(self):
        super().__init__()
        self._active = False
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(18)

        root.addWidget(self._build_header())
        root.addWidget(self._build_toolbar())

        body = QHBoxLayout()
        body.setSpacing(18)
        body.addWidget(self._build_practice_card(), 2)

        side = QVBoxLayout()
        side.setSpacing(18)
        side.addWidget(self._build_progress_card())
        side.addWidget(self._build_stats_card())
        side_widget = QWidget()
        side_widget.setLayout(side)
        body.addWidget(side_widget, 1)
        root.addLayout(body)
        root.addStretch(1)

        self._on_mode_changed(self.mode_group.checkedButton())

    # ------------------------------------------------------------------
    def _build_header(self) -> QWidget:
        header = QHBoxLayout()
        header.setSpacing(18)

        title_col = QVBoxLayout()
        title_col.setSpacing(2)
        title = QLabel("Training")
        title.setObjectName("pageTitle")
        subtitle = QLabel("Build your CW skills with live device drills")
        subtitle.setObjectName("pageSubtitle")
        title_col.addWidget(title)
        title_col.addWidget(subtitle)
        header.addLayout(title_col)
        header.addStretch(1)

        seg = QWidget()
        seg.setObjectName("segmentGroup")
        seg_row = QHBoxLayout(seg)
        seg_row.setContentsMargins(4, 4, 4, 4)
        seg_row.setSpacing(2)
        self.mode_group = QButtonGroup(self)
        self.mode_group.setExclusive(True)
        for mode in proto.TRAIN_MODES:
            btn = QPushButton(proto.TRAIN_MODE_LABELS.get(mode, mode))
            btn.setObjectName("segmentButton")
            btn.setCheckable(True)
            btn.setProperty("trainMode", mode)
            seg_row.addWidget(btn)
            self.mode_group.addButton(btn)
        list(self.mode_group.buttons())[0].setChecked(True)
        self.mode_group.buttonClicked.connect(self._on_mode_changed)
        header.addWidget(seg)

        note = QHBoxLayout()
        note.setSpacing(8)
        info_icon = QLabel("ⓘ")
        info_icon.setObjectName("infoGlyph")
        note.addWidget(info_icon)
        self.mode_desc_label = QLabel()
        self.mode_desc_label.setObjectName("sectionLabel")
        self.mode_desc_label.setWordWrap(True)
        self.mode_desc_label.setMaximumWidth(220)
        note.addWidget(self.mode_desc_label)
        header.addLayout(note)

        wrap = QWidget()
        wrap.setLayout(header)
        return wrap

    def _build_toolbar(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("plainPanel")
        shadow = QGraphicsDropShadowEffect(bar)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 5)
        shadow.setColor(QColor(0, 0, 0, 90))
        bar.setGraphicsEffect(shadow)
        row = QHBoxLayout(bar)
        row.setContentsMargins(18, 14, 18, 14)
        row.setSpacing(12)

        hint = QLabel("Speed and character set are configured on the device itself "
                       "(Settings menu) - not remotely adjustable over BLE yet.")
        hint.setObjectName("sectionLabel")
        hint.setWordWrap(True)
        row.addWidget(hint, 1)

        self.confirm_btn = QPushButton("Confirm")
        self.confirm_btn.setToolTip("Advance past an exam result / confirmation prompt")
        self.confirm_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "train_confirm"}))
        row.addWidget(self.confirm_btn)

        self.stop_btn = QPushButton("Stop")
        self.stop_btn.setObjectName("dangerButton")
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(lambda: self.command_requested.emit({"cmd": "train_stop"}))
        row.addWidget(self.stop_btn)

        self.start_btn = QPushButton("▶  Start Training")
        self.start_btn.setObjectName("ctaButton")
        self.start_btn.clicked.connect(self._on_start)
        row.addWidget(self.start_btn)

        return bar

    def _build_practice_card(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("plainPanel")
        shadow = QGraphicsDropShadowEffect(panel)
        shadow.setBlurRadius(28)
        shadow.setOffset(0, 6)
        shadow.setColor(QColor(0, 0, 0, 90))
        panel.setGraphicsEffect(shadow)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(20, 18, 20, 24)
        layout.setSpacing(14)

        head = QHBoxLayout()
        head.setSpacing(10)
        head.addWidget(PulseIcon(18, ACCENT))
        title_col = QVBoxLayout()
        title_col.setSpacing(0)
        title = QLabel("Character Practice")
        title.setObjectName("panelTitle")
        subtitle = QLabel("Copy what you hear with the virtual key below")
        subtitle.setObjectName("sectionLabel")
        title_col.addWidget(title)
        title_col.addWidget(subtitle)
        head.addLayout(title_col)
        head.addStretch(1)
        self.phase_label = QLabel("--")
        self.phase_label.setObjectName("sectionLabel")
        head.addWidget(self.phase_label)
        layout.addLayout(head)

        stage = QWidget()
        stage.setObjectName("practiceStage")
        stage_layout = QVBoxLayout(stage)
        stage_layout.setAlignment(Qt.AlignCenter)
        stage_layout.setSpacing(10)
        stage.setMinimumHeight(220)
        self.target_label = big_label("--")
        stage_layout.addWidget(self.target_label)
        self.morse_glyph = MorseGlyph()
        stage_layout.addWidget(self.morse_glyph)
        layout.addWidget(stage, 1)

        self.exam_box = QWidget()
        self.exam_box.setObjectName("examBanner")
        ebox = QHBoxLayout(self.exam_box)
        ebox.setContentsMargins(16, 12, 16, 12)
        self.exam_result_label = QLabel("--")
        self.exam_result_label.setObjectName("panelTitle")
        ebox.addWidget(self.exam_result_label)
        ebox.addStretch(1)
        self.exam_score_label = QLabel("--")
        self.exam_score_label.setObjectName("sectionLabel")
        ebox.addWidget(self.exam_score_label)
        self.exam_box.setVisible(False)
        layout.addWidget(self.exam_box)

        self.key_button = CircularKeyButton()
        self.key_button.key_down.connect(lambda: self.command_requested.emit({"cmd": "key_down"}))
        self.key_button.key_up.connect(lambda: self.command_requested.emit({"cmd": "key_up"}))
        layout.addWidget(self.key_button, 0, Qt.AlignHCenter)
        hint = QLabel("Hold the button or SPACE to key your answer")
        hint.setObjectName("sectionLabel")
        hint.setAlignment(Qt.AlignCenter)
        layout.addWidget(hint)

        return panel

    def _build_progress_card(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("plainPanel")
        shadow = QGraphicsDropShadowEffect(panel)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 5)
        shadow.setColor(QColor(0, 0, 0, 90))
        panel.setGraphicsEffect(shadow)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(18, 16, 18, 18)
        layout.setSpacing(12)

        head = QHBoxLayout()
        head.addWidget(PieIcon(16, ACCENT))
        title = QLabel("Koch Progress")
        title.setObjectName("panelTitle")
        head.addWidget(title)
        head.addStretch(1)
        self.koch_level_label = QLabel("--")
        self.koch_level_label.setObjectName("sectionLabel")
        head.addWidget(self.koch_level_label)
        layout.addLayout(head)

        self.koch_row = QHBoxLayout()
        self.koch_row.setSpacing(8)
        self._koch_char_labels = []
        for _ in range(6):
            lbl = QLabel("-")
            lbl.setObjectName("kochChar")
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setFixedSize(38, 38)
            self.koch_row.addWidget(lbl)
            self._koch_char_labels.append(lbl)
        layout.addLayout(self.koch_row)

        self.progress_fallback_label = QLabel("Correct: 0 / 0")
        self.progress_fallback_label.setObjectName("sectionLabel")
        self.progress_fallback_label.setVisible(False)
        layout.addWidget(self.progress_fallback_label)

        return panel

    def _build_stats_card(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("plainPanel")
        shadow = QGraphicsDropShadowEffect(panel)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 5)
        shadow.setColor(QColor(0, 0, 0, 90))
        panel.setGraphicsEffect(shadow)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(18, 16, 18, 18)
        layout.setSpacing(14)

        head = QHBoxLayout()
        head.addWidget(BarsIcon(16, ACCENT))
        title = QLabel("Training Statistics")
        title.setObjectName("panelTitle")
        head.addWidget(title)
        head.addStretch(1)
        reset_btn = QPushButton("Reset")
        reset_btn.setObjectName("compactButton")
        reset_btn.setEnabled(False)
        reset_btn.setToolTip("Not supported yet: these counters are tracked on-device "
                              "and there's no reset command in the BLE control protocol.")
        head.addWidget(reset_btn)
        layout.addLayout(head)

        body = QHBoxLayout()
        body.setSpacing(16)
        self.accuracy_gauge = RingGauge("Accuracy", ACCENT)
        body.addWidget(self.accuracy_gauge)

        rows = QVBoxLayout()
        rows.setSpacing(10)
        self.correct_stat = self._stat_row(rows, "#3ddc84", "Correct")
        self.incorrect_stat = self._stat_row(rows, "#ff5c7a", "Incorrect")
        self.avg_response_stat = self._stat_row(rows, "#6c7086", "Avg Response")
        self.cpm_stat = self._stat_row(rows, "#6c7086", "Characters / min")
        body.addLayout(rows, 1)
        layout.addLayout(body)

        return panel

    def _stat_row(self, rows: QVBoxLayout, dot_color: str, label: str) -> QLabel:
        row = QHBoxLayout()
        row.setSpacing(8)
        dot = QLabel("●")
        dot.setStyleSheet(f"color: {dot_color}; font-size: 9pt;")
        row.addWidget(dot)
        caption = QLabel(label)
        caption.setObjectName("sectionLabel")
        row.addWidget(caption)
        row.addStretch(1)
        value = QLabel("--")
        value.setObjectName("pillValue")
        row.addWidget(value)
        rows.addLayout(row)
        return value

    # ------------------------------------------------------------------
    def _on_mode_changed(self, button):
        mode = button.property("trainMode")
        self.mode_desc_label.setText(proto.TRAIN_MODE_DESCRIPTIONS.get(mode, ""))
        is_koch = mode == "KOCH"
        for lbl in self._koch_char_labels:
            lbl.setVisible(is_koch)
        self.koch_level_label.setVisible(is_koch)
        self.progress_fallback_label.setVisible(not is_koch)

    def _on_start(self):
        mode = self.mode_group.checkedButton().property("trainMode")
        self.command_requested.emit({"cmd": "train_start", "mode": mode})

    def _selected_mode(self) -> str:
        return self.mode_group.checkedButton().property("trainMode")

    def on_train_state(self, state: dict):
        active = bool(state.get("active"))
        self._active = active
        self.start_btn.setEnabled(not active)
        self.stop_btn.setEnabled(active)
        for btn in self.mode_group.buttons():
            btn.setEnabled(not active)

        if not active:
            self.target_label.setText("--")
            self.morse_glyph.set_pattern("")
            self.phase_label.setText("--")
            self.exam_box.setVisible(False)
            self.correct_stat.setText("--")
            self.incorrect_stat.setText("--")
            self.avg_response_stat.setText("--")
            self.cpm_stat.setText("--")
            self.accuracy_gauge.set_value(0)
            self.koch_level_label.setText("--")
            self.progress_fallback_label.setText("Correct: 0 / 0")
            for lbl in self._koch_char_labels:
                lbl.setText("-")
                lbl.setProperty("state", "")
                lbl.style().unpolish(lbl)
                lbl.style().polish(lbl)
            return

        phase = state.get("phase", "?")
        target = str(state.get("target", "") or "")
        self.target_label.setText(target if target else "–")
        self.morse_glyph.set_pattern(proto.MORSE_TABLE.get(target.upper(), ""))
        self.phase_label.setText(phase)

        correct = state.get("correct", 0)
        attempts = state.get("attempts", 0)
        incorrect = max(0, attempts - correct)
        accuracy = int(round(correct * 100 / attempts)) if attempts else 0
        self.correct_stat.setText(str(correct))
        self.incorrect_stat.setText(str(incorrect))
        self.accuracy_gauge.set_value(accuracy)
        # Not reported by train_state - honestly shown as unavailable
        # rather than a fabricated number.
        self.avg_response_stat.setText("--")
        self.cpm_stat.setText("--")

        koch_level = state.get("kochLevel")
        if self._selected_mode() == "KOCH" and koch_level is not None:
            self.koch_level_label.setText(f"Level {koch_level} / {len(proto.KOCH_ORDER)}")
            start = max(0, min(koch_level, len(proto.KOCH_ORDER)) - 3)
            window = proto.KOCH_ORDER[start:start + 6]
            for i, lbl in enumerate(self._koch_char_labels):
                if i < len(window):
                    idx = start + i
                    ch = window[i]
                    lbl.setText(ch)
                    if ch == target:
                        lbl.setProperty("state", "current")
                    elif idx < koch_level:
                        lbl.setProperty("state", "unlocked")
                    else:
                        lbl.setProperty("state", "locked")
                else:
                    lbl.setText("")
                    lbl.setProperty("state", "")
                lbl.style().unpolish(lbl)
                lbl.style().polish(lbl)
        else:
            self.progress_fallback_label.setText(f"Correct: {correct} / {attempts}")

        if phase == "EXAM_DONE":
            self.exam_box.setVisible(True)
            passed = state.get("examPassed", False)
            self.exam_result_label.setText("PASSED" if passed else "FAILED")
            self.exam_score_label.setText(
                f"{state.get('examScorePercent', 0)}% "
                f"({state.get('examCorrect', 0)}/{state.get('examTotal', 0)})"
            )
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
