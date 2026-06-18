"""可复用 UI 组件：mood 字标、hero 形象(电视机小猫)、卡片、可选列表行、单选点、活动 pill。"""
import os

from PySide6.QtCore import Qt, QRectF, Signal
from PySide6.QtGui import QPainter, QColor, QFont, QPen, QFontMetrics
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import (
    QWidget, QFrame, QLabel, QHBoxLayout, QVBoxLayout,
)

from clawd_mochi.ui import icons, theme

_ASSETS = os.path.join(os.path.dirname(__file__), "assets")


def _asset(name: str) -> str:
    return os.path.join(_ASSETS, name)


class MoodWordmark(QWidget):
    """品牌字标 mood：m + 两张猫脸(logo) + d，两个 o = 两只 mood。"""

    def __init__(self, height: int = 42):
        super().__init__()
        self._svg = QSvgRenderer(_asset("mood-logo.svg"))
        self._font = QFont("Segoe UI")
        self._font.setPixelSize(round(height * 0.9))
        self._font.setWeight(QFont.ExtraBold)
        fm = QFontMetrics(self._font)
        self._mw = fm.horizontalAdvance("m")
        self._dw = fm.horizontalAdvance("d")
        self._face = round(height * 0.58)
        gap = round(height * 0.03)
        pad = round(height * 0.06)
        self._baseline = round(height * 0.8)
        self._x_m = pad
        self._x_f1 = self._x_m + self._mw + gap
        self._x_f2 = self._x_f1 + self._face + gap
        self._x_d = self._x_f2 + self._face + gap
        self.setFixedSize(self._x_d + self._dw + pad, height)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        p.setRenderHint(QPainter.TextAntialiasing, True)
        p.setFont(self._font)
        p.setPen(QColor(theme.INK))
        p.drawText(self._x_m, self._baseline, "m")
        p.drawText(self._x_d, self._baseline, "d")
        top = self._baseline - self._face
        self._svg.render(p, QRectF(self._x_f1, top, self._face, self._face))
        self._svg.render(p, QRectF(self._x_f2, top, self._face, self._face))
        p.end()


class MoodFace(QWidget):
    """hero 形象：电视机小猫 mood（矢量），替代旧笑脸。"""

    def __init__(self, size: int = 78):
        super().__init__()
        self.setFixedSize(size, size)
        self._svg = QSvgRenderer(_asset("mood-mascot.svg"))

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        self._svg.render(p, QRectF(0, 0, self.width(), self.height()))
        p.end()


class RadioDot(QWidget):
    """自绘单选点：环 + 选中时内点。"""

    def __init__(self):
        super().__init__()
        self.setFixedSize(20, 20)
        self._on = False

    def set_on(self, on: bool):
        self._on = on
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        c = QColor(theme.CORAL) if self._on else QColor("#D8C6B0")
        p.setPen(QPen(c, 2))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QRectF(2, 2, 16, 16))
        if self._on:
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(theme.CORAL))
            p.drawEllipse(QRectF(6, 6, 8, 8))
        p.end()


def make_card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    theme.card_shadow(f)
    return f


def make_pill(text: str, live: bool) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("Pill")
    lab.setProperty("live", "true" if live else "false")
    return lab


class SelectableRow(QFrame):
    """图标 + 名称(+meta) + 尾部组件(单选点/pill/按钮)的卡片行。"""

    clicked = Signal()

    def __init__(self, icon_name: str, title: str, meta: str | None = None, *, selectable: bool = True):
        super().__init__()
        self.setObjectName("Row")
        self._selectable = selectable
        self.setProperty("selected", False)
        self.setProperty("hoverable", "true" if selectable else "false")
        if selectable:
            self.setCursor(Qt.PointingHandCursor)
        theme.card_shadow(self)

        h = QHBoxLayout(self)
        h.setContentsMargins(15, 11, 15, 11)
        h.setSpacing(13)

        box = QLabel()
        box.setObjectName("IconBox")
        box.setFixedSize(38, 38)
        box.setAlignment(Qt.AlignCenter)
        box.setPixmap(icons.pixmap(icon_name, 21))
        h.addWidget(box)

        col = QVBoxLayout()
        col.setSpacing(1)
        name = QLabel(title)
        name.setObjectName("RowName")
        col.addWidget(name)
        if meta:
            m = QLabel(meta)
            m.setObjectName("RowMeta")
            col.addWidget(m)
        h.addLayout(col)
        h.addStretch(1)
        self._h = h
        self._radio = None

    def add_radio(self) -> RadioDot:
        self._radio = RadioDot()
        self._h.addWidget(self._radio)
        return self._radio

    def add_trailing(self, w: QWidget):
        self._h.addWidget(w)

    def set_selected(self, on: bool):
        self.setProperty("selected", "true" if on else "false")
        theme.repolish(self)
        if self._radio:
            self._radio.set_on(on)

    def mouseReleaseEvent(self, e):
        if self._selectable and self.rect().contains(e.position().toPoint()):
            self.clicked.emit()
        super().mouseReleaseEvent(e)
