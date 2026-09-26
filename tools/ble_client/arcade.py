"""
Client-side arcade Morse-code mini-games for the Games tab.

These are intentionally NOT BLE-backed. The firmware's BLE control
protocol (ble_control.cpp) only implements three games (COPY/MEMORY/
SPEED via game_start/game_state) - there is no device command for the
six arcade concepts the reference UI template shows. Per an explicit
user decision, all six are implemented here as real, fully playable
local games (typing practice checked against protocol.MORSE_TABLE, the
same exact table the firmware's decoder uses) rather than faking a
device feature that doesn't exist. Best scores persist locally via
QSettings - real per-player history, not a fabricated number.
"""

import random

from PySide6.QtCore import QPointF, QRectF, Qt, QSettings, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QKeyEvent, QLinearGradient, QPainter, QPainterPath
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QGraphicsDropShadowEffect,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

import protocol as proto
from widgets import BeaconIcon, FruitIcon, InvaderIcon, MeteorIcon, SpaceshipIcon, WordBubbleIcon

LETTER_POOL = [c for c in proto.MORSE_TABLE if c.isalpha()]
DIGIT_POOL = [c for c in proto.MORSE_TABLE if c.isdigit()]
WORD_POOL = ["CQ", "DE", "TEST", "MORPHEUS", "SOS", "QTH", "RST", "ES", "PSE", "GM"]

DIFFICULTIES = ["Easy", "Medium", "Hard"]
SPEEDS_WPM = [10, 15, 20, 25, 30]
LIVES_OPTIONS = [1, 3, 5]
DURATION_OPTIONS_S = [60, 120, 180]

# mode:
#   "falling"     - a letter/digit falls; type its Morse pattern before it lands
#   "discriminate" - falling chars include a "wanted" one; only solve that one,
#                    letting others land is fine, solving a wrong one costs a life
#   "decode"      - a Morse pattern falls; press the letter key it decodes to
#   "word"        - a whole word falls; type its full Morse pattern (letters
#                    separated by a space) before it lands
GAME_DEFS = [
    {
        "id": "space_war", "title": "Space War", "color": "#5b7cfa",
        "mode": "falling", "icon": SpaceshipIcon,
        "desc": "Destroy incoming enemies by typing their Morse code correctly.",
        "tags": ["Speed", "Accuracy", "Reaction"],
    },
    {
        "id": "fruit_ninja", "title": "Fruit Ninja (Morse)", "color": "#ff8a3d",
        "mode": "falling", "icon": FruitIcon,
        "desc": "Slice the correct fruit by typing its Morse code before it falls.",
        "tags": ["Speed", "Recognition", "Fun"],
    },
    {
        "id": "meteor_catch", "title": "Meteor Catch", "color": "#f5b942",
        "mode": "discriminate", "icon": MeteorIcon,
        "desc": "Catch the correct character and avoid the wrong ones.",
        "tags": ["Comprehension", "Speed", "Endurance"],
    },
    {
        "id": "signal_rescue", "title": "Signal Rescue", "color": "#3ddc84",
        "mode": "decode", "icon": BeaconIcon,
        "desc": "Decode incoming Morse messages to rescue stranded beacons.",
        "tags": ["Comprehension", "Accuracy", "Strategy"],
    },
    {
        "id": "morse_invaders", "title": "Morse Invaders", "color": "#8a5cf6",
        "mode": "falling", "icon": InvaderIcon,
        "desc": "Classic arcade action with a Morse twist. Type to shoot.",
        "tags": ["Speed", "Accuracy", "Arcade"],
    },
    {
        "id": "word_rush", "title": "Word Rush", "color": "#22c3c3",
        "mode": "word", "icon": WordBubbleIcon,
        "desc": "Type complete words in Morse before time runs out.",
        "tags": ["Vocabulary", "Speed", "Streak"],
    },
]


def _fall_speed_px_per_tick(wpm: int, difficulty: str) -> float:
    base = 0.35 + (wpm - 10) * 0.045
    mult = {"Easy": 0.7, "Medium": 1.0, "Hard": 1.4}.get(difficulty, 1.0)
    return base * mult


def _spawn_interval_ms(difficulty: str) -> int:
    return {"Easy": 1600, "Medium": 1150, "Hard": 800}.get(difficulty, 1150)


class Target:
    __slots__ = ("text", "pattern", "input", "x", "y", "wanted")

    def __init__(self, text, pattern, x, wanted=True):
        self.text = text
        self.pattern = pattern
        self.input = ""
        self.x = x
        self.y = 0.0
        self.wanted = wanted


class ArcadeCanvas(QWidget):
    """The actual playable surface - shared by all six games, branching
    on `mode` for spawn/check rules. Keyboard-driven: '.' / '-' build a
    Morse guess (falling/discriminate/word modes), letter keys answer
    directly (decode mode)."""

    finished = Signal(int, bool)  # (final_score, cleared_by_time)

    def __init__(self, game_def: dict, wpm: int, lives: int, duration_s: int, difficulty: str, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.StrongFocus)
        self.game_def = game_def
        self.mode = game_def["mode"]
        self.color = QColor(game_def["color"])
        self.wpm = wpm
        self.lives = lives
        self.max_lives = lives
        self.duration_s = duration_s
        self.time_left_ms = duration_s * 1000
        self.difficulty = difficulty
        self.score = 0
        self.combo = 0
        self._running = False
        self._targets: list[Target] = []
        self._word_target = None
        self._wanted_char = None
        self._flash = ""
        self._flash_good = True

        self.setMinimumSize(420, 320)

        self._tick_timer = QTimer(self)
        self._tick_timer.timeout.connect(self._tick)
        self._spawn_timer = QTimer(self)
        self._spawn_timer.timeout.connect(self._spawn)

    def start(self):
        self._running = True
        self.score = 0
        self.combo = 0
        self.lives = self.max_lives
        self.time_left_ms = self.duration_s * 1000
        self._targets.clear()
        self._word_target = None
        if self.mode == "discriminate":
            self._wanted_char = random.choice(LETTER_POOL)
        self._tick_timer.start(30)
        self._spawn_timer.start(_spawn_interval_ms(self.difficulty))
        self._spawn()
        self.setFocus()
        self.update()

    def stop(self):
        self._running = False
        self._tick_timer.stop()
        self._spawn_timer.stop()

    # ------------------------------------------------------------------
    def _spawn(self):
        if not self._running:
            return
        w = max(self.width(), 200)
        if self.mode == "word":
            if self._word_target is not None:
                return
            word = random.choice(WORD_POOL)
            pattern = " ".join(proto.MORSE_TABLE[c] for c in word)
            self._word_target = Target(word, pattern, w * random.uniform(0.2, 0.8))
            return

        pool = LETTER_POOL if self.mode != "discriminate" else LETTER_POOL
        if len(self._targets) >= (5 if self.mode == "discriminate" else 3):
            return
        if self.mode == "discriminate" and random.random() < 0.4:
            char = self._wanted_char
        else:
            char = random.choice(pool)
        pattern = proto.MORSE_TABLE[char]
        wanted = (char == self._wanted_char) if self.mode == "discriminate" else True
        x = random.uniform(0.08, 0.92) * w
        self._targets.append(Target(char, pattern, x, wanted))

    def _tick(self):
        if not self._running:
            return
        self.time_left_ms -= 30
        if self.time_left_ms <= 0:
            self._end(cleared=True)
            return

        speed = _fall_speed_px_per_tick(self.wpm, self.difficulty)
        h = self.height()
        landed = []
        for t in ([self._word_target] if self._word_target else self._targets):
            t.y += speed * 4
            if t.y >= h - 30:
                landed.append(t)

        for t in landed:
            if t is self._word_target:
                self._word_target = None
                self._lose_life()
            else:
                self._targets.remove(t)
                if self.mode != "discriminate" or t.wanted:
                    self._lose_life()
        self.update()

    def _lose_life(self):
        self.combo = 0
        self.lives -= 1
        self._flash_good = False
        self._flash = "MISS"
        if self.lives <= 0:
            self._end(cleared=False)

    def _end(self, cleared: bool):
        self.stop()
        self.finished.emit(self.score, cleared)
        self.update()

    # ------------------------------------------------------------------
    def keyPressEvent(self, event: QKeyEvent):
        if not self._running:
            return
        text = event.text()
        if self.mode == "decode":
            if text.isalpha():
                self._check_decode(text.upper())
            return
        if self.mode in ("falling", "discriminate", "word"):
            if event.key() == Qt.Key_Backspace:
                self._active_target_input(-1)
                self.update()
                return
            if text in (".", "-"):
                self._active_target_input(text)
                self.update()
                return

    def _active_target(self):
        if self.mode == "word":
            return self._word_target
        if not self._targets:
            return None
        # nearest to landing = most urgent
        return max(self._targets, key=lambda t: t.y)

    def _active_target_input(self, key):
        t = self._active_target()
        if t is None:
            return
        if key == -1:
            t.input = t.input[:-1]
            return
        t.input += key
        if len(t.input) >= len(t.pattern):
            if t.input == t.pattern:
                self._solve(t)
            else:
                t.input = ""
                self._lose_life()

    def _solve(self, t: Target):
        if t is self._word_target:
            self._word_target = None
        elif self.mode == "discriminate":
            self._targets.remove(t)
            if not t.wanted:
                self._lose_life()
                return
            self._wanted_char = random.choice(LETTER_POOL)
        else:
            self._targets.remove(t)
        self.combo += 1
        gained = 10 + min(self.combo, 10) * 2
        self.score += gained
        self._flash_good = True
        self._flash = f"+{gained}"

    def _check_decode(self, letter: str):
        if not self._targets:
            return
        t = min(self._targets, key=lambda x: x.y)
        if letter == t.text:
            self._targets.remove(t)
            self._solve_decode()
        else:
            self._lose_life()

    def _solve_decode(self):
        self.combo += 1
        gained = 10 + min(self.combo, 10) * 2
        self.score += gained
        self._flash_good = True
        self._flash = f"+{gained}"

    # ------------------------------------------------------------------
    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        grad = QLinearGradient(0, 0, 0, h)
        grad.setColorAt(0, QColor("#0c0e16"))
        grad.setColorAt(1, QColor("#12141d"))
        p.fillRect(self.rect(), grad)

        # HUD
        p.setPen(QColor("#e8e9ee"))
        hud_font = QFont()
        hud_font.setBold(True)
        hud_font.setPointSize(11)
        p.setFont(hud_font)
        p.drawText(QRectF(14, 10, 200, 26), Qt.AlignLeft, f"Score: {self.score}")
        p.drawText(QRectF(w - 214, 10, 200, 26), Qt.AlignRight, "♥ " * self.lives)
        secs = max(0, self.time_left_ms) // 1000
        p.drawText(QRectF(0, 10, w, 26), Qt.AlignCenter, f"{secs // 60:02d}:{secs % 60:02d}")

        if self.mode == "discriminate" and self._wanted_char:
            p.setPen(QColor("#c7cbdb"))
            small = QFont()
            small.setPointSize(9)
            small.setBold(True)
            p.setFont(small)
            p.drawText(QRectF(0, 36, w, 20), Qt.AlignCenter, f"Catch:  {self._wanted_char}   ({self._wanted_char}'s Morse only)")

        if not self._running:
            p.setPen(QColor("#8a8fa3"))
            msg_font = QFont()
            msg_font.setPointSize(13)
            msg_font.setBold(True)
            p.setFont(msg_font)
            p.drawText(self.rect(), Qt.AlignCenter, "Press Play to start")
            return

        targets = [self._word_target] if self._word_target else self._targets
        for t in targets:
            self._draw_target(p, t)

    def _draw_target(self, p: QPainter, t: Target):
        is_word = t is self._word_target
        rw = 92 if is_word else 46
        rh = 40 if is_word else 40
        rect = QRectF(t.x - rw / 2, t.y - rh / 2, rw, rh)
        p.setPen(Qt.NoPen)
        color = self.color if (self.mode != "discriminate" or t.wanted) else QColor("#3a3e55")
        p.setBrush(color)
        p.drawRoundedRect(rect, 10, 10)

        p.setPen(QColor("#ffffff"))
        label_font = QFont()
        label_font.setBold(True)
        label_font.setPointSize(9 if is_word else 13)
        p.setFont(label_font)
        label = t.pattern if self.mode == "decode" else t.text
        p.drawText(rect.adjusted(0, -10, 0, -18), Qt.AlignCenter, label)

        input_font = QFont("Courier New")
        input_font.setBold(True)
        input_font.setPointSize(10)
        p.setFont(input_font)
        p.drawText(rect.adjusted(0, 14, 0, 12), Qt.AlignCenter, t.input)


class GameCard(QWidget):
    clicked = Signal(str)

    def __init__(self, game_def: dict, parent=None):
        super().__init__(parent)
        self.game_id = game_def["id"]
        self.setObjectName("gameCard")
        self.setCursor(Qt.PointingHandCursor)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        art = QWidget()
        art.setFixedHeight(150)
        art.setObjectName("gameCardArt")
        art.setStyleSheet(f"background-color: {game_def['color']}22; border-top-left-radius: 14px; border-top-right-radius: 14px;")
        art_layout = QVBoxLayout(art)
        icon_cls = game_def["icon"]
        icon = icon_cls(56, QColor(game_def["color"]))
        art_layout.addWidget(icon, 0, Qt.AlignCenter)
        layout.addWidget(art)

        body = QWidget()
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(16, 12, 16, 14)
        body_layout.setSpacing(6)
        title = QLabel(game_def["title"])
        title.setObjectName("panelTitle")
        desc = QLabel(game_def["desc"])
        desc.setObjectName("sectionLabel")
        desc.setWordWrap(True)
        body_layout.addWidget(title)
        body_layout.addWidget(desc)

        tag_row = QHBoxLayout()
        tag_row.setSpacing(6)
        for tag in game_def["tags"]:
            pill = QLabel(tag)
            pill.setObjectName("gameTag")
            tag_row.addWidget(pill)
        tag_row.addStretch(1)
        body_layout.addLayout(tag_row)
        layout.addWidget(body)

        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 5)
        shadow.setColor(QColor(0, 0, 0, 90))
        self.setGraphicsEffect(shadow)
        self.set_selected(False)

    def set_selected(self, selected: bool):
        self.setProperty("selected", "true" if selected else "false")
        self.style().unpolish(self)
        self.style().polish(self)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit(self.game_id)
        super().mousePressEvent(event)


class PlayDialog(QDialog):
    def __init__(self, game_def: dict, wpm: int, lives: int, duration_s: int, difficulty: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(game_def["title"])
        self.setModal(True)
        self.result_score = 0
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self.canvas = ArcadeCanvas(game_def, wpm, lives, duration_s, difficulty)
        self.canvas.setMinimumSize(680, 460)
        self.canvas.finished.connect(self._on_finished)
        layout.addWidget(self.canvas)

        footer = QHBoxLayout()
        footer.setContentsMargins(16, 10, 16, 14)
        self.status_label = QLabel("Good luck!")
        self.status_label.setObjectName("sectionLabel")
        footer.addWidget(self.status_label)
        footer.addStretch(1)
        close_btn = QPushButton("Close")
        close_btn.setObjectName("dangerButton")
        close_btn.clicked.connect(self.reject)
        footer.addWidget(close_btn)
        layout.addLayout(footer)

        self.canvas.start()

    def _on_finished(self, score, cleared):
        self.result_score = score
        self.status_label.setText(
            f"Time's up! Final score: {score}" if cleared else f"Out of lives - final score: {score}"
        )


class GamesPage(QWidget):
    """The Games tab: a card gallery matching the reference template,
    backed entirely by real local gameplay (see module docstring)."""

    def __init__(self):
        super().__init__()
        self._settings = QSettings("MORPHEUS", "BLEClient")
        self._selected_id = GAME_DEFS[0]["id"]
        self._cards = {}

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(18)
        root.addWidget(self._build_header())

        body = QHBoxLayout()
        body.setSpacing(18)
        body.addWidget(self._build_grid(), 2)
        body.addWidget(self._build_detail_panel(), 1)
        root.addLayout(body)

        self._select(self._selected_id)

    # ------------------------------------------------------------------
    def _build_header(self) -> QWidget:
        row = QHBoxLayout()
        row.setSpacing(14)
        icon = WordBubbleIcon(30, QColor("#5b7cfa"))
        row.addWidget(icon)
        title_col = QVBoxLayout()
        title_col.setSpacing(2)
        title = QLabel("Games")
        title.setObjectName("pageTitle")
        subtitle = QLabel("Play, practice and improve your Morse skills with interactive games.")
        subtitle.setObjectName("pageSubtitle")
        title_col.addWidget(title)
        title_col.addWidget(subtitle)
        row.addLayout(title_col)
        row.addStretch(1)

        # Real controls: they configure the local game engine directly
        # (spawn rate, fall speed) - unlike the Keyer/Training pages,
        # there's no BLE command they'd need to exist, since these
        # games don't talk to the device at all.
        row.addWidget(QLabel("Difficulty"))
        self.difficulty_combo = QComboBox()
        self.difficulty_combo.addItems(DIFFICULTIES)
        self.difficulty_combo.setCurrentText("Medium")
        row.addWidget(self.difficulty_combo)

        row.addWidget(QLabel("Game Speed"))
        self.speed_combo = QComboBox()
        self.speed_combo.addItems([f"{w} WPM" for w in SPEEDS_WPM])
        self.speed_combo.setCurrentIndex(1)
        row.addWidget(self.speed_combo)

        wrap = QWidget()
        wrap.setLayout(row)
        return wrap

    def _build_grid(self) -> QWidget:
        grid_widget = QWidget()
        grid = QGridLayout(grid_widget)
        grid.setSpacing(18)
        for i, game_def in enumerate(GAME_DEFS):
            card = GameCard(game_def)
            card.clicked.connect(self._select)
            self._cards[game_def["id"]] = card
            grid.addWidget(card, i // 3, i % 3)
        return grid_widget

    def _build_detail_panel(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("plainPanel")
        shadow = QGraphicsDropShadowEffect(panel)
        shadow.setBlurRadius(28)
        shadow.setOffset(0, 6)
        shadow.setColor(QColor(0, 0, 0, 90))
        panel.setGraphicsEffect(shadow)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(20, 18, 20, 20)
        layout.setSpacing(14)

        head = QHBoxLayout()
        self.detail_title = QLabel("--")
        self.detail_title.setObjectName("pageTitle")
        head.addWidget(self.detail_title)
        head.addStretch(1)
        self.best_score_label = QLabel("Best Score: --")
        self.best_score_label.setObjectName("bestScoreBadge")
        head.addWidget(self.best_score_label)
        layout.addLayout(head)

        self.detail_art = QWidget()
        self.detail_art.setObjectName("gameCardArt")
        self.detail_art.setMinimumHeight(180)
        self._detail_art_layout = QVBoxLayout(self.detail_art)
        layout.addWidget(self.detail_art)

        self.detail_desc = QLabel("--")
        self.detail_desc.setObjectName("sectionLabel")
        self.detail_desc.setWordWrap(True)
        layout.addWidget(self.detail_desc)

        controls = QHBoxLayout()
        controls.setSpacing(10)

        def stat_control(label, items, tooltip=None):
            col = QVBoxLayout()
            col.setSpacing(4)
            lbl = QLabel(label)
            lbl.setObjectName("sectionLabel")
            col.addWidget(lbl)
            combo = QComboBox()
            combo.addItems([str(i) for i in items])
            if tooltip:
                combo.setToolTip(tooltip)
            col.addWidget(combo)
            controls.addLayout(col)
            return combo

        self.lives_combo = stat_control("Lives", LIVES_OPTIONS)
        self.lives_combo.setCurrentText("3")
        self.duration_combo = stat_control("Duration", [f"{d // 60} min" for d in DURATION_OPTIONS_S])
        self.duration_combo.setCurrentIndex(1)
        layout.addLayout(controls)

        self.play_btn = QPushButton("▶  Play Game")
        self.play_btn.setObjectName("ctaButton")
        self.play_btn.clicked.connect(self._on_play)
        layout.addWidget(self.play_btn)
        layout.addStretch(1)

        return panel

    # ------------------------------------------------------------------
    def _game_def(self, game_id: str) -> dict:
        return next(g for g in GAME_DEFS if g["id"] == game_id)

    def _best_score_key(self, game_id: str) -> str:
        return f"best_score/{game_id}"

    def _select(self, game_id: str):
        self._selected_id = game_id
        for gid, card in self._cards.items():
            card.set_selected(gid == game_id)

        game_def = self._game_def(game_id)
        self.detail_title.setText(game_def["title"])
        self.detail_desc.setText(game_def["desc"])
        self.detail_art.setStyleSheet(
            f"background-color: {game_def['color']}22; border-radius: 14px;"
        )
        while self._detail_art_layout.count():
            item = self._detail_art_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        icon = game_def["icon"](72, QColor(game_def["color"]))
        self._detail_art_layout.addWidget(icon, 0, Qt.AlignCenter)

        best = self._settings.value(self._best_score_key(game_id), 0, type=int)
        self.best_score_label.setText(f"Best Score: {best:,}" if best else "Best Score: --")

    def _on_play(self):
        game_def = self._game_def(self._selected_id)
        wpm = SPEEDS_WPM[self.speed_combo.currentIndex()]
        lives = int(self.lives_combo.currentText())
        duration_s = DURATION_OPTIONS_S[self.duration_combo.currentIndex()]
        difficulty = self.difficulty_combo.currentText()

        dialog = PlayDialog(game_def, wpm, lives, duration_s, difficulty, self)
        dialog.exec()

        best_key = self._best_score_key(self._selected_id)
        best = self._settings.value(best_key, 0, type=int)
        if dialog.result_score > best:
            self._settings.setValue(best_key, dialog.result_score)
        self._select(self._selected_id)
