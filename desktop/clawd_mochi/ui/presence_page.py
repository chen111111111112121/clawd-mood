from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel

from clawd_mochi.core import config, device
from clawd_mochi.ui import theme
from clawd_mochi.ui.widgets import SelectableRow

# (id, 名称, 图标名)
PRESENCE = [
    ("auto", "工作中", "auto"),
    ("meeting", "开会中", "meeting"),
    ("toilet", "上厕所中", "toilet"),
    ("solder", "实验室中", "solder"),
    ("rest", "休息中", "rest"),
]


class PresencePage(QWidget):
    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(30, 24, 30, 28)
        root.setSpacing(10)

        h1 = QLabel("状态")
        h1.setObjectName("H1")
        root.addWidget(h1)
        root.addSpacing(4)

        self._rows: dict[str, SelectableRow] = {}
        for pid, name, icon in PRESENCE:
            row = SelectableRow(icon, name)
            row.add_radio()
            row.clicked.connect(lambda x=pid: self._set(x))
            root.addWidget(row)
            self._rows[pid] = row

        self._toast = QLabel("")
        self._toast.setObjectName("Toast")
        self._toast.setProperty("kind", "bad")
        self._toast.setWordWrap(True)
        self._toast.setVisible(False)
        root.addSpacing(4)
        root.addWidget(self._toast)
        root.addStretch(1)
        self.refresh()

    def refresh(self):
        cur = config.read_config()["presence"]
        for pid, row in self._rows.items():
            row.set_selected(pid == cur)

    def _set(self, pid: str):
        config.write_presence(pid)
        self.refresh()
        r = device.send_presence(pid)
        if r["ok"]:
            self._toast.setVisible(False)
        else:
            self._toast.setText("设备不可达，已记录选择（设备恢复后重选生效）")
            theme.repolish(self._toast)
            self._toast.setVisible(True)
