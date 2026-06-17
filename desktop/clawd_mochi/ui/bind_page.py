import time

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QRadioButton, QLabel, QButtonGroup,
)

from clawd_mochi.core import config

ACTIVE_FRESH_MS = 60000


class BindPage(QWidget):
    def __init__(self):
        super().__init__()
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(28, 24, 28, 24)
        self._layout.addWidget(QLabel("<h1>工具绑定</h1>"))
        self._layout.addWidget(QLabel("绑定 Mochi 当前响应哪一款 AI 工具（单选）"))
        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        self._rows_box = QVBoxLayout()
        self._layout.addLayout(self._rows_box)
        self._layout.addStretch(1)
        self._buttons: dict[str, QRadioButton] = {}
        self.refresh()

    def _clear_rows(self):
        for btn in list(self._buttons.values()):
            self._group.removeButton(btn)
        while self._rows_box.count():
            item = self._rows_box.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()
        self._buttons = {}

    def refresh(self):
        cfg = config.read_config()
        state = config.read_state()
        last_seen = state["lastSeen"]
        active = cfg["activeTool"]
        self._clear_rows()
        now = time.time() * 1000
        for t in cfg["tools"]:
            seen = last_seen.get(t["id"])
            fresh = bool(seen) and (now - seen < ACTIVE_FRESH_MS)
            installed = "已安装 hook" if t.get("installed") else "未安装 hook"
            live = "最近有事件" if fresh else "空闲"
            rb = QRadioButton(f"{t['name']}    （{installed} · {t['id']} · {live}）")
            rb.setChecked(active == t["id"])
            rb.clicked.connect(lambda _=False, tid=t["id"]: self._set_active(tid))
            self._group.addButton(rb)
            self._rows_box.addWidget(rb)
            self._buttons[t["id"]] = rb

    def _set_active(self, tool_id: str):
        config.write_active_tool(tool_id)
        self.refresh()
