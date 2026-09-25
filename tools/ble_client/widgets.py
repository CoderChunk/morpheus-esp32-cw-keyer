"""
Custom-painted icon and control widgets.

Emoji/dingbat glyphs were tried for the sidebar earlier in this project
and verified (via real screenshots) to render as broken tofu boxes on
this system's fonts. Everything here is drawn directly with QPainter
instead, so it renders identically everywhere regardless of what font
packs happen to be installed.
"""

import math

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QPushButton, QSizePolicy, QWidget

MUTED = QColor("#8a90a8")
WHITE = QColor("#ffffff")
ACCENT = QColor("#5b7cfa")
GREEN = QColor("#3ddc84")
RED = QColor("#ff5c7a")


# ---------------------------------------------------------------------------
class IconWidget(QWidget):
    """Base class for small fixed-size line-icon widgets."""

    def __init__(self, size=20, color=None, parent=None):
        super().__init__(parent)
        self._size = size
        self._color = color or MUTED
        self.setFixedSize(size, size)

    def set_color(self, color: QColor):
        self._color = color
        self.update()

    def _pen(self, painter: QPainter, width_frac=0.11):
        pen = QPen(self._color, max(1.5, self._size * width_frac))
        pen.setJoinStyle(Qt.RoundJoin)
        pen.setCapStyle(Qt.RoundCap)
        painter.setPen(pen)
        return pen


class BluetoothIcon(IconWidget):
    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.1)
        w, h = self.width(), self.height()
        cx = w / 2
        top, bottom, mid = h * 0.14, h * 0.86, h / 2
        path = QPainterPath()
        path.moveTo(cx, top)
        path.lineTo(cx + w * 0.26, h * 0.32)
        path.lineTo(cx - w * 0.24, h * 0.68)
        path.lineTo(cx, bottom)
        path.moveTo(cx, top)
        path.lineTo(cx - w * 0.24, h * 0.32)
        path.lineTo(cx + w * 0.26, h * 0.68)
        path.lineTo(cx, bottom)
        p.drawPath(path)
        p.drawLine(QPointF(cx, top), QPointF(cx, bottom))


class GearIcon(IconWidget):
    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.1)
        w, h = self.width(), self.height()
        cx, cy = w / 2, h / 2
        r_outer = min(w, h) * 0.42
        teeth = 8
        for i in range(teeth):
            angle = (2 * math.pi / teeth) * i
            x1 = cx + r_outer * 0.76 * math.cos(angle)
            y1 = cy + r_outer * 0.76 * math.sin(angle)
            x2 = cx + r_outer * 1.05 * math.cos(angle)
            y2 = cy + r_outer * 1.05 * math.sin(angle)
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QPointF(cx, cy), r_outer * 0.72, r_outer * 0.72)
        p.drawEllipse(QPointF(cx, cy), r_outer * 0.26, r_outer * 0.26)


class HomeIcon(IconWidget):
    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.11)
        p.setBrush(Qt.NoBrush)
        w, h = self.width(), self.height()
        path = QPainterPath()
        path.moveTo(w * 0.5, h * 0.06)
        path.lineTo(w * 0.92, h * 0.42)
        path.lineTo(w * 0.78, h * 0.42)
        path.lineTo(w * 0.78, h * 0.92)
        path.lineTo(w * 0.22, h * 0.92)
        path.lineTo(w * 0.22, h * 0.42)
        path.lineTo(w * 0.08, h * 0.42)
        path.closeSubpath()
        p.drawPath(path)
        p.drawRect(QRectF(w * 0.42, h * 0.58, w * 0.16, h * 0.34))


class BarsIcon(IconWidget):
    """Ascending bar chart - used for Training."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        p.setBrush(self._color)
        w, h = self.width(), self.height()
        n, bw, gap = 3, w * 0.2, w * 0.12
        heights = (0.4, 0.7, 1.0)
        x = w * 0.08
        for i in range(n):
            bh = h * 0.85 * heights[i]
            p.drawRoundedRect(QRectF(x, h * 0.92 - bh, bw, bh), 1.5, 1.5)
            x += bw + gap


class PieIcon(IconWidget):
    """Pie/donut chart - used for Statistics."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        w, h = self.width(), self.height()
        rect = QRectF(w * 0.08, h * 0.08, w * 0.84, h * 0.84)
        p.setBrush(QColor("#333649"))
        p.drawEllipse(rect)
        p.setBrush(self._color)
        path = QPainterPath()
        path.moveTo(rect.center())
        path.arcTo(rect, 90, -150)
        path.closeSubpath()
        p.drawPath(path)


class LinkIcon(IconWidget):
    """Two interlocking rings - used for Connectivity."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.14)
        p.setBrush(Qt.NoBrush)
        w, h = self.width(), self.height()
        p.drawRoundedRect(QRectF(w * 0.04, h * 0.3, w * 0.55, h * 0.4), h * 0.2, h * 0.2)
        p.drawRoundedRect(QRectF(w * 0.41, h * 0.3, w * 0.55, h * 0.4), h * 0.2, h * 0.2)


class TextLinesIcon(IconWidget):
    """Three short horizontal strokes - used for a "last word" stat."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.13)
        w, h = self.width(), self.height()
        for i, frac in enumerate((0.3, 0.52, 0.74)):
            y = h * frac
            x2 = w * 0.82 if i < 2 else w * 0.58
            p.drawLine(QPointF(w * 0.16, y), QPointF(x2, y))


class PulseIcon(IconWidget):
    """Zigzag waveform line - used for the Character Practice header."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.11)
        p.setBrush(Qt.NoBrush)
        w, h = self.width(), self.height()
        pts = [(0.0, 0.5), (0.2, 0.5), (0.34, 0.14), (0.48, 0.86), (0.62, 0.5), (1.0, 0.5)]
        path = QPainterPath()
        path.moveTo(w * pts[0][0], h * pts[0][1])
        for fx, fy in pts[1:]:
            path.lineTo(w * fx, h * fy)
        p.drawPath(path)


class ChevronIcon(IconWidget):
    """Small right-pointing chevron, used as a row affordance."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.16)
        w, h = self.width(), self.height()
        path = QPainterPath()
        path.moveTo(w * 0.35, h * 0.2)
        path.lineTo(w * 0.68, h * 0.5)
        path.lineTo(w * 0.35, h * 0.8)
        p.drawPath(path)


class BatteryIcon(IconWidget):
    """Battery outline, drawn empty/neutral since the firmware has no BLE
    battery service - there is no real level to fill it with, so this
    is a static glyph for visual match rather than a fake reading."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.1)
        p.setBrush(Qt.NoBrush)
        w, h = self.width(), self.height()
        body = QRectF(w * 0.04, h * 0.22, w * 0.78, h * 0.56)
        p.drawRoundedRect(body, h * 0.1, h * 0.1)
        cap = QRectF(w * 0.86, h * 0.36, w * 0.1, h * 0.28)
        p.setPen(Qt.NoPen)
        p.setBrush(self._color)
        p.drawRoundedRect(cap, w * 0.02, w * 0.02)


class SignalBarsIcon(IconWidget):
    """Ascending signal bars, drawn as an outline (not filled) since
    there is no real RSSI/link-quality data behind it - see BatteryIcon."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        p.setBrush(self._color)
        w, h = self.width(), self.height()
        n, bw, gap = 4, w * 0.16, w * 0.06
        x = w * 0.02
        for i in range(n):
            bh = h * (0.32 + 0.68 * (i + 1) / n)
            p.drawRoundedRect(QRectF(x, h - bh, bw, bh), 1, 1)
            x += bw + gap


class DocumentIcon(IconWidget):
    """Simple page-with-lines glyph, used for the Live Transcript header."""

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        self._pen(p, 0.1)
        p.setBrush(Qt.NoBrush)
        w, h = self.width(), self.height()
        p.drawRoundedRect(QRectF(w * 0.16, h * 0.08, w * 0.68, h * 0.84), 2, 2)
        for frac in (0.36, 0.54, 0.72):
            p.drawLine(QPointF(w * 0.3, h * frac), QPointF(w * 0.7, h * frac))


class LetterBadge(IconWidget):
    """Fallback icon: a plain letter in a ring, for sections that don't
    have a bespoke vector icon yet - plain ASCII always renders safely,
    unlike emoji/dingbats."""

    def __init__(self, letter: str, size=20, parent=None):
        super().__init__(size, MUTED, parent)
        self.letter = letter

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        p.setPen(self._pen(p, 0.09))
        p.setBrush(Qt.NoBrush)
        rect = QRectF(w * 0.08, h * 0.08, w * 0.84, h * 0.84)
        p.drawEllipse(rect)
        font = QFont()
        font.setBold(True)
        font.setPointSizeF(max(7.0, self._size * 0.42))
        p.setFont(font)
        p.drawText(rect, Qt.AlignCenter, self.letter)


class ClickablePanel(QWidget):
    """A plain QWidget that emits `clicked` on left-click, so a styled
    container (like the top-bar device pill) can act as a button
    without fighting QPushButton's own content layout."""

    clicked = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setCursor(Qt.PointingHandCursor)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit()
        super().mousePressEvent(event)


class LogoMark(QWidget):
    """Abstract blue mountain-peak "M" mark for the brand header."""

    def __init__(self, size=40, parent=None):
        super().__init__(parent)
        self.setFixedSize(size, size)

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        grad = QLinearGradient(0, h, w, 0)
        grad.setColorAt(0, QColor("#3a5cff"))
        grad.setColorAt(1, QColor("#8fb2ff"))
        p.setPen(Qt.NoPen)
        p.setBrush(grad)
        path = QPainterPath()
        path.moveTo(w * 0.5, h * 0.08)
        path.lineTo(w * 0.86, h * 0.86)
        path.lineTo(w * 0.6, h * 0.86)
        path.lineTo(w * 0.5, h * 0.52)
        path.lineTo(w * 0.4, h * 0.86)
        path.lineTo(w * 0.14, h * 0.86)
        path.closeSubpath()
        p.drawPath(path)


# ---------------------------------------------------------------------------
class WpmDial(QWidget):
    """Circular arc readout for the last-seen WPM from live word telemetry.

    Read-only: the firmware's BLE control protocol has no command to set
    keying speed remotely for plain telemetry mode (only Training's
    adaptive WPM is reported, not settable here), so this shows -/+
    buttons disabled with a tooltip rather than pretending they work.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self._value = 0
        self._max = 40
        self.setMinimumSize(220, 220)
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)

    def set_value(self, wpm):
        try:
            self._value = int(wpm)
        except (TypeError, ValueError):
            self._value = 0
        self.update()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height()) - 16
        rect = QRectF((self.width() - side) / 2, (self.height() - side) / 2, side, side)

        track = QPen(QColor("#232636"), side * 0.045)
        track.setCapStyle(Qt.RoundCap)
        p.setPen(track)
        p.drawArc(rect, 90 * 16, -300 * 16)

        frac = max(0.0, min(1.0, self._value / self._max))
        if frac > 0:
            arc_pen = QPen(ACCENT, side * 0.045)
            arc_pen.setCapStyle(Qt.RoundCap)
            p.setPen(arc_pen)
            p.drawArc(rect, 90 * 16, int(-300 * frac * 16))

        knob_angle = math.radians(90 - 300 * frac)
        kr = side / 2 - side * 0.045
        kx = rect.center().x() + kr * math.cos(knob_angle)
        ky = rect.center().y() - kr * math.sin(knob_angle)
        p.setPen(Qt.NoPen)
        p.setBrush(WHITE)
        p.drawEllipse(QPointF(kx, ky), side * 0.03, side * 0.03)

        p.setPen(WHITE)
        value_font = QFont()
        value_font.setPointSizeF(side * 0.16)
        value_font.setBold(True)
        p.setFont(value_font)
        value_rect = QRectF(rect.x(), rect.center().y() - side * 0.14, rect.width(), side * 0.2)
        p.drawText(value_rect, Qt.AlignCenter, str(self._value) if self._value else "--")

        p.setPen(MUTED)
        label_font = QFont()
        label_font.setPointSizeF(side * 0.055)
        label_font.setBold(True)
        p.setFont(label_font)
        label_rect = QRectF(rect.x(), rect.center().y() + side * 0.06, rect.width(), side * 0.1)
        p.drawText(label_rect, Qt.AlignCenter, "WPM")


class RingGauge(QWidget):
    """Generic circular percentage gauge - e.g. Training's live Accuracy
    (correct/attempts, computed from real train_state fields)."""

    def __init__(self, label: str, color=None, parent=None):
        super().__init__(parent)
        self._value = 0
        self._label = label
        self._color = color or ACCENT
        self.setMinimumSize(170, 170)
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)

    def set_value(self, percent):
        try:
            self._value = max(0, min(100, int(percent)))
        except (TypeError, ValueError):
            self._value = 0
        self.update()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height()) - 12
        rect = QRectF((self.width() - side) / 2, (self.height() - side) / 2, side, side)

        track = QPen(QColor("#232636"), side * 0.09)
        track.setCapStyle(Qt.RoundCap)
        p.setPen(track)
        p.drawArc(rect, 90 * 16, -360 * 16)

        if self._value > 0:
            arc_pen = QPen(self._color, side * 0.09)
            arc_pen.setCapStyle(Qt.RoundCap)
            p.setPen(arc_pen)
            p.drawArc(rect, 90 * 16, int(-360 * (self._value / 100.0) * 16))

        p.setPen(WHITE)
        value_font = QFont()
        value_font.setPointSizeF(side * 0.15)
        value_font.setBold(True)
        p.setFont(value_font)
        value_rect = QRectF(rect.x(), rect.center().y() - side * 0.16, rect.width(), side * 0.2)
        p.drawText(value_rect, Qt.AlignCenter, f"{self._value}%")

        p.setPen(MUTED)
        label_font = QFont()
        label_font.setPointSizeF(side * 0.07)
        label_font.setBold(True)
        p.setFont(label_font)
        label_rect = QRectF(rect.x(), rect.center().y() + side * 0.05, rect.width(), side * 0.12)
        p.drawText(label_rect, Qt.AlignCenter, self._label)


class MorseGlyph(QWidget):
    """Renders a Morse pattern (e.g. ".-.") as dot/dash bars - the real
    pattern for the live training target character, from protocol.MORSE_TABLE
    (an exact copy of the firmware's own morseTable)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._pattern = ""
        self.setMinimumHeight(30)

    def set_pattern(self, pattern: str):
        self._pattern = pattern or ""
        self.update()

    def paintEvent(self, _event):
        if not self._pattern:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        p.setBrush(WHITE)
        h = min(self.height(), 26)
        dot_w = h * 0.5
        dash_w = h * 1.7
        gap = h * 0.55
        widths = [dot_w if c == "." else dash_w for c in self._pattern]
        total = sum(widths) + gap * (len(widths) - 1)
        x = (self.width() - total) / 2
        y = (self.height() - h * 0.42) / 2
        for w in widths:
            p.drawRoundedRect(QRectF(x, y, w, h * 0.42), h * 0.2, h * 0.2)
            x += w + gap


class RoundIconButton(QPushButton):
    """Small circular +/- style button used flanking the WPM dial."""

    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setObjectName("roundIconButton")
        self.setFixedSize(44, 44)
        self.setCursor(Qt.PointingHandCursor)


# ---------------------------------------------------------------------------
class CircularKeyButton(QPushButton):
    """Big circular press-and-hold virtual straight key.

    Sends real key_down/key_up over the BLE control protocol (the same
    command Training/Games already use) - held via mouse or SPACE while
    focused. Paints a play-style triangle when idle and a solid "keying"
    glow while held, rather than reusing an unrelated play/pause
    metaphor that wouldn't reflect what the button actually does.
    """

    key_down = Signal()
    key_up = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(160, 160)
        self.setCursor(Qt.PointingHandCursor)
        self.setFocusPolicy(Qt.StrongFocus)
        self._pressed = False

    def is_keying(self) -> bool:
        return self._pressed

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
            self.update()
            self.key_down.emit()

    def _release(self):
        if self._pressed:
            self._pressed = False
            self.update()
            self.key_up.emit()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height())
        rect = QRectF((self.width() - side) / 2, (self.height() - side) / 2, side, side)

        grad = QLinearGradient(rect.topLeft(), rect.bottomRight())
        if self._pressed:
            grad.setColorAt(0, QColor("#ff7a59"))
            grad.setColorAt(1, QColor("#e6493f"))
        else:
            grad.setColorAt(0, QColor("#6f8dfb"))
            grad.setColorAt(1, QColor("#4a68d9"))
        p.setPen(Qt.NoPen)
        p.setBrush(grad)
        p.drawEllipse(rect)

        p.setBrush(WHITE)
        if self._pressed:
            bar_w, bar_h = side * 0.09, side * 0.3
            gap = side * 0.06
            cx, cy = rect.center().x(), rect.center().y()
            p.drawRoundedRect(QRectF(cx - gap - bar_w, cy - bar_h / 2, bar_w, bar_h), 3, 3)
            p.drawRoundedRect(QRectF(cx + gap, cy - bar_h / 2, bar_w, bar_h), 3, 3)
        else:
            tri = QPainterPath()
            cx, cy = rect.center().x(), rect.center().y()
            s = side * 0.16
            tri.moveTo(cx - s * 0.6, cy - s)
            tri.lineTo(cx - s * 0.6, cy + s)
            tri.lineTo(cx + s * 0.9, cy)
            tri.closeSubpath()
            p.drawPath(tri)


# ---------------------------------------------------------------------------
class HeroPanel(QWidget):
    """Gradient hero banner with a decorative radio-tower silhouette,
    standing in for a photographic background image (no asset ships
    with this tool) while keeping the same visual mood as the template:
    dusk gradient, concentric transmission rings, tower on the horizon.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(260)

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        grad = QLinearGradient(0, h, w, 0)
        grad.setColorAt(0.0, QColor("#0c0e16"))
        grad.setColorAt(0.55, QColor("#1c2440"))
        grad.setColorAt(1.0, QColor("#7a4a3a"))
        p.setPen(Qt.NoPen)
        p.setBrush(grad)
        p.drawRoundedRect(QRectF(0, 0, w, h), 18, 18)

        tower_x = w * 0.82
        base_y = h * 0.92
        top_y = h * 0.28

        ring_pen = QPen(QColor(255, 255, 255, 40), 1.4)
        p.setPen(ring_pen)
        p.setBrush(Qt.NoBrush)
        for i, r in enumerate((28, 48, 70)):
            p.drawEllipse(QPointF(tower_x, top_y), r, r)

        p.setPen(QPen(QColor(10, 11, 18, 210), 3))
        p.drawLine(QPointF(tower_x, top_y), QPointF(tower_x - w * 0.05, base_y))
        p.drawLine(QPointF(tower_x, top_y), QPointF(tower_x + w * 0.05, base_y))
        for frac in (0.4, 0.6, 0.8):
            y = top_y + (base_y - top_y) * frac
            spread = w * 0.05 * frac
            p.drawLine(QPointF(tower_x - spread, y), QPointF(tower_x + spread, y))

        p.setBrush(QColor(10, 11, 18, 220))
        p.drawEllipse(QPointF(tower_x, top_y - 2), 5, 5)

        silhouette = QPainterPath()
        silhouette.moveTo(0, h)
        silhouette.lineTo(0, h * 0.82)
        for i in range(0, 11):
            x = w * i / 10
            y = h * (0.72 + 0.06 * math.sin(i * 1.3))
            silhouette.lineTo(x, y)
        silhouette.lineTo(w, h)
        silhouette.closeSubpath()
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(6, 7, 12, 200))
        p.drawPath(silhouette)
