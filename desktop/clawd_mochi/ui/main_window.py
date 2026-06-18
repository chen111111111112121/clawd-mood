from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QVBoxLayout, QPushButton, QLabel, QStackedWidget, QFrame,
)
from PySide6.QtGui import QIcon

from clawd_mochi.core import device
from clawd_mochi.ui import icons, theme
from clawd_mochi.ui.widgets import MoodWordmark
from clawd_mochi.ui.today_page import TodayPage
from clawd_mochi.ui.bind_page import BindPage
from clawd_mochi.ui.presence_page import PresencePage
from clawd_mochi.ui.firmware_page import FirmwarePage
from clawd_mochi.ui.settings_page import SettingsPage
from clawd_mochi.ui.titlebar import TitleBar
from clawd_mochi.ui.frameless_win import install_frameless

# (导航文本, 页属性名, 图标名)
_NAV = [
    ("今日陪伴", "today", "today"),
    ("状态", "presence", "presence"),
    ("工具绑定", "bind", "bind"),
    ("固件升级", "firmware", "firmware"),
    ("设置", "settings", "settings"),
]


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setObjectName("Root")
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setWindowTitle("mood")
        self.resize(940, 600)
        self.setMinimumSize(900, 560)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self._title_bar = TitleBar(self)
        outer.addWidget(self._title_bar)

        body = QHBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(0)
        body.addWidget(self._build_side())
        self._pages = QStackedWidget()
        self.today = TodayPage()
        self.presence = PresencePage()
        self.bind = BindPage()
        self.firmware = FirmwarePage()
        self.settings = SettingsPage()
        for w in (self.today, self.presence, self.bind, self.firmware, self.settings):
            self._pages.addWidget(w)
        body.addWidget(self._pages, 1)
        outer.addLayout(body, 1)

        install_frameless(self, self._title_bar)

        self._switch(0)
        self.test_device()

        # 刷新定时器：绑定/状态每 5s，今日每 30s
        self._t_fast = QTimer(self)
        self._t_fast.timeout.connect(self.bind.refresh)
        self._t_fast.timeout.connect(self.presence.refresh)
        self._t_fast.start(5000)
        self._t_today = QTimer(self)
        self._t_today.timeout.connect(self.today.refresh)
        self._t_today.start(30000)

    # —— 侧栏 ——
    def _build_side(self) -> QFrame:
        side = QFrame()
        side.setObjectName("Side")
        side.setFixedWidth(208)
        col = QVBoxLayout(side)
        col.setContentsMargins(14, 16, 14, 14)
        col.setSpacing(2)

        brand_wrap = QHBoxLayout()
        brand_wrap.setContentsMargins(4, 2, 0, 12)
        brand_wrap.addWidget(MoodWordmark())
        brand_wrap.addStretch(1)
        col.addLayout(brand_wrap)

        nav_label = QLabel("导航")
        nav_label.setObjectName("NavLabel")
        nav_label.setContentsMargins(8, 4, 0, 6)
        col.addWidget(nav_label)

        self._nav_btns = []
        for i, (text, _attr, icon) in enumerate(_NAV):
            b = QPushButton(text)
            b.setObjectName("Nav")
            b.setCheckable(True)
            b.setCursor(Qt.PointingHandCursor)
            b.setIcon(QIcon(icons.nav_pixmap(icon, False)))
            b.clicked.connect(lambda _=False, idx=i: self._switch(idx))
            col.addWidget(b)
            self._nav_btns.append((b, icon))

        col.addStretch(1)
        col.addWidget(self._build_foot())
        return side

    def _build_foot(self) -> QFrame:
        foot = QFrame()
        foot.setObjectName("DeviceFoot")
        theme.card_shadow(foot)
        fl = QVBoxLayout(foot)
        fl.setContentsMargins(12, 10, 12, 11)
        fl.setSpacing(5)

        row = QHBoxLayout()
        row.setSpacing(8)
        self._dot = QLabel()
        self._dot.setObjectName("Dot")
        self._dot.setFixedSize(10, 10)
        self._dev_status = QLabel("设备：检测中…")
        self._dev_status.setObjectName("DeviceStatus")
        row.addWidget(self._dot)
        row.addWidget(self._dev_status)
        row.addStretch(1)
        fl.addLayout(row)

        self._dev_ip = QLabel("…")
        self._dev_ip.setObjectName("DeviceIp")
        self._dev_ip.setWordWrap(True)
        fl.addWidget(self._dev_ip)

        btn = QPushButton("测试 / 重连")
        btn.setObjectName("FootBtn")
        btn.setCursor(Qt.PointingHandCursor)
        btn.clicked.connect(self.test_device)
        fl.addWidget(btn)
        return foot

    def _switch(self, idx: int):
        self._pages.setCurrentIndex(idx)
        for i, (b, icon) in enumerate(self._nav_btns):
            on = i == idx
            b.setChecked(on)
            b.setIcon(QIcon(icons.nav_pixmap(icon, on)))
        self._pages.currentWidget().refresh()

    def test_device(self):
        r = device.device_test()
        self._dev_ip.setText(str(r["target"]))
        self._dev_status.setText("设备：已连接" if r["ok"] else "设备：不可达")
        self._dot.setProperty("s", "ok" if r["ok"] else "bad")
        theme.repolish(self._dot)
