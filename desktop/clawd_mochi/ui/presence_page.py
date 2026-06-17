from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QRadioButton, QLabel, QButtonGroup,
)

from clawd_mochi.core import config, device

PRESENCE = [
    ("auto", "工作中", "交还自动 · 听 AI 活动"),
    ("meeting", "开会中", "盯笔记本视频会议"),
    ("toilet", "上厕所中", "稍等片刻"),
    ("solder", "实验室中", "焊板子 · 专注中"),
    ("rest", "休息中", "听歌喝咖啡"),
]


class PresencePage(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(28, 24, 28, 24)
        layout.addWidget(QLabel("<h1>状态</h1>"))
        layout.addWidget(QLabel(
            "手动状态牌：设备常驻显示，不被 AI 事件打断。「工作中」= 交还自动（听 Claude Code）。"))
        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        self._buttons: dict[str, QRadioButton] = {}
        for pid, name, sub in PRESENCE:
            rb = QRadioButton(f"{name}    （{sub}）")
            rb.clicked.connect(lambda _=False, x=pid: self._set(x))
            self._group.addButton(rb)
            layout.addWidget(rb)
            self._buttons[pid] = rb
        self._tip = QLabel("")
        self._tip.setStyleSheet("color:#C4632F")
        layout.addWidget(self._tip)
        layout.addStretch(1)
        self.refresh()

    def refresh(self):
        cur = config.read_config()["presence"]
        if cur in self._buttons:
            self._buttons[cur].setChecked(True)

    def _set(self, pid: str):
        config.write_presence(pid)
        r = device.send_presence(pid)
        self._tip.setText("" if r["ok"] else "设备不可达，已记录选择（设备恢复后重选生效）")
