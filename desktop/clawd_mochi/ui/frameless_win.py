"""Windows 无边框窗口:保留原生缩放/Aero Snap/投影/Win11 圆角,标题栏可拖动。
非 Windows 平台直接退回系统边框(仅防崩)。最大化已禁用,故无需处理"最大化盖任务栏"。
"""
import sys
import ctypes
from ctypes import wintypes

from PySide6.QtCore import Qt, QAbstractNativeEventFilter
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QApplication

# —— Win32 常量 ——
WM_NCCALCSIZE = 0x0083
WM_NCHITTEST = 0x0084
GWL_STYLE = -16
WS_THICKFRAME = 0x00040000
WS_CAPTION = 0x00C00000
WS_MINIMIZEBOX = 0x00020000
WS_MAXIMIZEBOX = 0x00010000
HTCLIENT, HTCAPTION = 1, 2
HTLEFT, HTRIGHT, HTTOP, HTBOTTOM = 10, 11, 12, 15
HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT = 13, 14, 16, 17
DWMWA_WINDOW_CORNER_PREFERENCE = 33
DWMWCP_ROUND = 2
_BORDER = 6  # 逻辑像素:缩放边判定容差


class _MARGINS(ctypes.Structure):
    _fields_ = [("l", ctypes.c_int), ("r", ctypes.c_int),
                ("t", ctypes.c_int), ("b", ctypes.c_int)]


class _NativeFilter(QAbstractNativeEventFilter):
    def __init__(self, window, title_bar):
        super().__init__()
        self.win = window
        self.tb = title_bar
        self.hwnd = int(window.winId())

    def nativeEventFilter(self, event_type, message):
        if event_type != b"windows_generic_MSG":
            return False, 0
        msg = wintypes.MSG.from_address(int(message))
        if msg.hwnd != self.hwnd:
            return False, 0
        if msg.message == WM_NCCALCSIZE:
            # 客户区占满整窗 = 去掉系统非客户区边框
            return True, 0
        if msg.message == WM_NCHITTEST:
            return True, self._hit()
        return False, 0

    def _hit(self):
        # 用 Qt 逻辑全局坐标,天然 DPI 安全(避免解析 lParam 物理像素)
        pos = self.win.mapFromGlobal(QCursor.pos())
        x, y, w, h, b = pos.x(), pos.y(), self.win.width(), self.win.height(), _BORDER
        left, right, top, bottom = x < b, x > w - b, y < b, y > h - b
        if top and left:
            return HTTOPLEFT
        if top and right:
            return HTTOPRIGHT
        if bottom and left:
            return HTBOTTOMLEFT
        if bottom and right:
            return HTBOTTOMRIGHT
        if left:
            return HTLEFT
        if right:
            return HTRIGHT
        if top:
            return HTTOP
        if bottom:
            return HTBOTTOM
        # 标题栏拖动:落在 title bar 内且不在按钮上
        tb_local = self.tb.mapFrom(self.win, pos)
        if self.tb.rect().contains(tb_local):
            from clawd_mochi.ui.titlebar import WindowButton
            if not isinstance(self.tb.childAt(tb_local), WindowButton):
                return HTCAPTION
        return HTCLIENT


def install_frameless(window, title_bar):
    """让 window 成为无边框但保留原生行为。须在 window 拥有 HWND 前后均可(内部用 winId 创建)。"""
    window.setWindowFlag(Qt.FramelessWindowHint, True)
    if sys.platform != "win32":
        return  # 非 Windows:退回系统边框
    if QApplication.platformName() in ("offscreen", "minimal"):
        return  # 测试/无窗口系统:无真实 HWND,跳过原生处理
    hwnd = int(window.winId())
    user32 = ctypes.windll.user32
    dwmapi = ctypes.windll.dwmapi

    # 保留缩放(THICKFRAME)+投影/Snap(CAPTION)+最小化;去掉最大化
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    style = (style | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX) & ~WS_MAXIMIZEBOX
    user32.SetWindowLongW(hwnd, GWL_STYLE, style)

    # 原生投影(若真机无投影,试 _MARGINS(0,0,0,1))
    m = _MARGINS(1, 1, 1, 1)
    dwmapi.DwmExtendFrameIntoClientArea(hwnd, ctypes.byref(m))

    # Win11 圆角(老系统忽略)
    pref = ctypes.c_int(DWMWCP_ROUND)
    try:
        dwmapi.DwmSetWindowAttribute(
            hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ctypes.byref(pref), ctypes.sizeof(pref))
    except Exception:
        pass

    flt = _NativeFilter(window, title_bar)
    QApplication.instance().installNativeEventFilter(flt)
    window._frameless_filter = flt  # 持引用防 GC
