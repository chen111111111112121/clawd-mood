from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QVBoxLayout, QPushButton, QLabel, QStackedWidget,
)

from clawd_mochi.core import device
from clawd_mochi.ui.today_page import TodayPage
from clawd_mochi.ui.bind_page import BindPage
from clawd_mochi.ui.presence_page import PresencePage


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Clawd · 桌面陪伴控制台")
        self.resize(900, 600)

        root = QHBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # —— 侧栏 ——
        side = QVBoxLayout()
        side.setContentsMargins(14, 18, 14, 14)
        brand = QLabel("Clawd\n桌面陪伴控制台")
        side.addWidget(brand)

        self._pages = QStackedWidget()
        self.today = TodayPage()
        self.presence = PresencePage()
        self.bind = BindPage()
        for w in (self.today, self.presence, self.bind):
            self._pages.addWidget(w)

        self._nav_btns = []
        for i, (text, page) in enumerate([
            ("今日陪伴", self.today), ("状态", self.presence), ("工具绑定", self.bind),
        ]):
            b = QPushButton(text)
            b.setCheckable(True)
            b.clicked.connect(lambda _=False, idx=i: self._switch(idx))
            side.addWidget(b)
            self._nav_btns.append(b)
        side.addStretch(1)

        # —— 设备状态脚 ——
        self._dev_status = QLabel("设备：检测中…")
        self._dev_ip = QLabel("…")
        self._dev_ip.setWordWrap(True)
        test_btn = QPushButton("测试 / 重连")
        test_btn.clicked.connect(self.test_device)
        side.addWidget(self._dev_status)
        side.addWidget(self._dev_ip)
        side.addWidget(test_btn)

        side_box = QWidget()
        side_box.setLayout(side)
        side_box.setFixedWidth(212)
        root.addWidget(side_box)
        root.addWidget(self._pages, 1)

        self._switch(0)
        self.test_device()

        # 刷新定时器：绑定每 5s，今日每 30s
        self._t_bind = QTimer(self)
        self._t_bind.timeout.connect(self.bind.refresh)
        self._t_bind.timeout.connect(self.presence.refresh)
        self._t_bind.start(5000)
        self._t_today = QTimer(self)
        self._t_today.timeout.connect(self.today.refresh)
        self._t_today.start(30000)

    def _switch(self, idx: int):
        self._pages.setCurrentIndex(idx)
        for i, b in enumerate(self._nav_btns):
            b.setChecked(i == idx)
        self._pages.currentWidget().refresh()

    def test_device(self):
        r = device.device_test()
        self._dev_ip.setText(str(r["target"]))
        self._dev_status.setText("设备：已连接" if r["ok"] else "设备：不可达")
