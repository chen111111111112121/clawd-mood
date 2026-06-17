"""可复用 UI 组件：MOOD 字标、hero 猫脸、卡片、可选列表行、单选点、活动 pill。"""
from PySide6.QtCore import Qt, QTimer, QVariantAnimation, QPointF, QRectF, Signal
from PySide6.QtGui import QPainter, QColor, QFont, QPen, QBrush, QLinearGradient, QRadialGradient
from PySide6.QtWidgets import (
    QWidget, QFrame, QLabel, QHBoxLayout, QVBoxLayout, QSizePolicy,
)

from clawd_mochi.ui import icons, theme


class _Blinker:
    """给自绘控件加偶发眨眼：_open 在 1↔0.1 之间。"""

    def _init_blink(self, period_ms: int = 5200):
        self._open = 1.0
        self._anim = None
        self._t = QTimer(self)
        self._t.timeout.connect(self._do_blink)
        self._t.start(period_ms)

    def _do_blink(self):
        a = QVariantAnimation(self)
        a.setDuration(170)
        a.setKeyValueAt(0.0, 1.0)
        a.setKeyValueAt(0.5, 0.1)
        a.setKeyValueAt(1.0, 1.0)
        a.valueChanged.connect(self._on_blink)
        a.start()
        self._anim = a  # 持引用防 GC

    def _on_blink(self, v):
        self._open = float(v)
        self.update()


class MoodWordmark(QWidget, _Blinker):
    """品牌字标：M + 两只会眨的眼睛(O) + 字母 D + 腮红点。"""

    def __init__(self):
        super().__init__()
        self.setFixedSize(146, 48)
        self._init_blink()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        coral = QColor(theme.CORAL_DEEP)

        f = QFont("Segoe UI", 26)
        f.setWeight(QFont.Black)
        p.setFont(f)
        p.setPen(coral)
        from PySide6.QtGui import QFontMetricsF
        fm = QFontMetricsF(f)
        cap = fm.capHeight()
        baseline = self.height() / 2 + cap / 2
        eye_cy = baseline - cap / 2
        d = cap * 0.86          # 眼睛直径
        gap = 3
        x = 2.0

        # M
        p.drawText(QPointF(x, baseline), "M")
        x += fm.horizontalAdvance("M") + gap + 2

        # 两只眼睛
        for _i in range(2):
            self._eye(p, x + d / 2, eye_cy, d / 2, coral)
            x += d + gap

        x += 1
        # D（眼睛绘制后 pen 被设为 NoPen，需恢复，否则文字不显示）
        p.setPen(coral)
        p.setFont(f)
        p.drawText(QPointF(x, baseline), "D")
        dw = fm.horizontalAdvance("D")
        # 腮红点（D 右下）
        p.setPen(Qt.NoPen)
        p.setBrush(QColor("#F3B8A0"))
        p.drawEllipse(QPointF(x + dw + 4, baseline - 2), 2.6, 2.6)
        p.end()

    def _eye(self, p, cx, cy, r, coral):
        pen = QPen(coral, max(2.0, r * 0.42))
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        ry = r * self._open
        if ry < r * 0.18:        # 几乎闭合 → 画一条横线
            p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy))
            return
        p.drawEllipse(QPointF(cx, cy), r, ry)
        # 瞳孔
        if self._open > 0.6:
            p.setPen(Qt.NoPen)
            p.setBrush(coral)
            p.drawEllipse(QPointF(cx, cy + r * 0.18), r * 0.34, r * 0.34 * self._open)


class MoodFace(QWidget, _Blinker):
    """hero 圆脸：渐变底 + 两只会眨的眼睛 + 微笑 + 腮红。"""

    def __init__(self, size: int = 78):
        super().__init__()
        self.setFixedSize(size, size)
        self._init_blink(4800)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        w = self.width()
        g = QRadialGradient(w * 0.38, w * 0.32, w * 0.75)
        g.setColorAt(0, QColor("#FBE7D2"))
        g.setColorAt(1, QColor("#F1CBA8"))
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(g))
        p.drawEllipse(0, 0, w, w)

        coral = QColor(theme.CORAL_DEEP)
        eye_y = w * 0.44
        ex = w * 0.30
        r = w * 0.085
        for cx in (w / 2 - ex, w / 2 + ex):
            ry = r * 2.2 * self._open
            p.setPen(Qt.NoPen)
            p.setBrush(coral)
            if ry < r * 0.5:
                pen = QPen(coral, r * 0.7)
                pen.setCapStyle(Qt.RoundCap)
                p.setPen(pen)
                p.drawLine(QPointF(cx - r, eye_y), QPointF(cx + r, eye_y))
            else:
                p.drawEllipse(QPointF(cx, eye_y), r, ry)
        # 微笑
        pen = QPen(coral, w * 0.05)
        pen.setCapStyle(Qt.RoundCap)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        smile = QRectF(w * 0.36, w * 0.5, w * 0.28, w * 0.22)
        p.drawArc(smile, 200 * 16, 140 * 16)
        # 腮红
        p.setPen(Qt.NoPen)
        p.setBrush(QColor("#F3B8A0"))
        p.drawEllipse(QPointF(w * 0.24, w * 0.58), w * 0.055, w * 0.055)
        p.drawEllipse(QPointF(w * 0.76, w * 0.58), w * 0.055, w * 0.055)
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
