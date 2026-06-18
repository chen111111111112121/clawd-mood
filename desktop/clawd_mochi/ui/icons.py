"""SVG 图标集 + 渲染助手。图标用 stroke=currentColor 占位，渲染时替换为指定颜色。"""
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtCore import QByteArray, Qt
from PySide6.QtGui import QPixmap, QPainter

from clawd_mochi.ui.theme import CORAL_DEEP, FAINT

# viewBox 统一 24×24；线条图标，stroke=__C__ 占位
_STROKE = {
    "today": '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    "presence": '<circle cx="12" cy="8" r="3.4"/><path d="M5.5 19c0-3.6 2.9-6 6.5-6s6.5 2.4 6.5 6"/>',
    "bind": '<path d="M7 8 3 12l4 4M17 8l4 4-4 4M14 4l-4 16"/>',
    "settings": '<circle cx="12" cy="12" r="3"/><path d="M12 3v2m0 14v2M3 12h2m14 0h2M5.6 5.6l1.4 1.4m10 10 1.4 1.4m0-13.2-1.4 1.4m-10 10-1.4 1.4"/>',
    "firmware": '<path d="M12 15V4M8 8l4-4 4 4"/><rect x="4" y="16" width="16" height="5" rx="1.5"/>',
    # presence 状态
    "auto": '<path d="M13 2 4.5 13.5H11l-1 8.5L19.5 10H13z"/>',
    "meeting": '<rect x="3" y="5" width="18" height="12" rx="2"/><path d="M2 21h20"/>',
    "toilet": '<path d="M6 3h12v18M6 3v18M3 21h18M9 8h0"/>',
    "solder": '<path d="M3 21 14 10M14 10l3-3 4 4-3 3zM14 10l-3 3"/>',
    "rest": '<path d="M4 8h13a4 4 0 0 1 0 8h-1M4 8v9a1 1 0 0 0 1 1h6M8 2v2M11 2v2"/>',
    # 工具
    "cc": '<path d="M12 3v18M3 12h18M6 6l12 12M18 6 6 18"/>',
    "cursor": '<path d="M5 3l14 8-6 1.6L9.6 19z"/>',
}


def _svg(name: str, color: str) -> str:
    body = _STROKE[name]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
            f'stroke="{color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
            f'{body}</svg>')


def pixmap(name: str, size: int = 22, color: str = CORAL_DEEP, scale: int = 2) -> QPixmap:
    """渲染图标为 QPixmap（按设备像素比放大，保证高分屏清晰）。"""
    renderer = QSvgRenderer(QByteArray(_svg(name, color).encode("utf-8")))
    pm = QPixmap(size * scale, size * scale)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.Antialiasing, True)
    renderer.render(p)
    p.end()
    pm.setDevicePixelRatio(scale)
    return pm


def nav_pixmap(name: str, on: bool) -> QPixmap:
    return pixmap(name, 18, CORAL_DEEP if on else "#8A7866")
