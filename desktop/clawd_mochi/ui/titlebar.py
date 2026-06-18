"""无边框窗口的极简暖色标题栏 + 两个圆点窗口按钮。"""
from PySide6.QtCore import Qt, QPointF
from PySide6.QtGui import QPainter, QColor, QPen, QFont
from PySide6.QtWidgets import QWidget, QHBoxLayout, QToolButton


class WindowButton(QToolButton):
    """暖色圆点：平时纯色,hover 浮现 –／× 字形。"""

    _DOT = {"min": "#E7CDB2", "close": "#E0875F"}
    _GLYPH = {"min": "–", "close": "×"}  # – ×

    def __init__(self, kind: str):
        super().__init__()
        self.kind = kind
        self._hover = False
        self.setObjectName("WinBtn")
        self.setCursor(Qt.PointingHandCursor)
        self.setFixedSize(30, 30)

    def enterEvent(self, e):
        self._hover = True
        self.update()
        super().enterEvent(e)

    def leaveEvent(self, e):
        self._hover = False
        self.update()
        super().leaveEvent(e)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        c = QPointF(self.width() / 2, self.height() / 2)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(self._DOT[self.kind]))
        p.drawEllipse(c, 6.5, 6.5)
        if self._hover:
            p.setPen(QPen(QColor("#5A4636")))
            f = QFont("Segoe UI")
            f.setPixelSize(13)
            f.setBold(True)
            p.setFont(f)
            p.drawText(self.rect(), Qt.AlignCenter, self._GLYPH[self.kind])
        p.end()


class TitleBar(QWidget):
    """~40px 暖色条:右侧两个窗口按钮,其余区域作拖动手柄(由 frameless 层报 caption)。"""

    HEIGHT = 40

    def __init__(self, window: QWidget):
        super().__init__()
        self.setObjectName("TitleBar")
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setFixedHeight(self.HEIGHT)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 8, 0)
        lay.setSpacing(4)
        lay.addStretch(1)
        self.btn_min = WindowButton("min")
        self.btn_close = WindowButton("close")
        self.btn_min.clicked.connect(lambda: window.showMinimized())
        self.btn_close.clicked.connect(lambda: window.hide())
        lay.addWidget(self.btn_min)
        lay.addWidget(self.btn_close)
