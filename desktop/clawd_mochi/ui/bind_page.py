import time

from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel

from clawd_mochi.core import config
from clawd_mochi.ui.widgets import SelectableRow, make_pill

ACTIVE_FRESH_MS = 60000
_ICON = {"cc": "cc", "cursor": "cursor"}


class BindPage(QWidget):
    def __init__(self):
        super().__init__()
        self._root = QVBoxLayout(self)
        self._root.setContentsMargins(30, 24, 30, 28)
        self._root.setSpacing(10)

        h1 = QLabel("工具绑定")
        h1.setObjectName("H1")
        self._root.addWidget(h1)
        self._root.addSpacing(4)

        self._rows_box = QVBoxLayout()
        self._rows_box.setSpacing(10)
        self._root.addLayout(self._rows_box)
        self._root.addStretch(1)
        self._rows: dict[str, SelectableRow] = {}
        self.refresh()

    def _clear(self):
        while self._rows_box.count():
            item = self._rows_box.takeAt(0)
            w = item.widget()
            if w:
                w.setParent(None)
                w.deleteLater()
        self._rows = {}

    def refresh(self):
        cfg = config.read_config()
        last_seen = config.read_state()["lastSeen"]
        active = cfg["activeTool"]
        now = time.time() * 1000
        self._clear()
        for t in cfg["tools"]:
            tid = t["id"]
            installed = "已安装 hook" if t.get("installed") else "未安装 hook"
            row = SelectableRow(_ICON.get(tid, "bind"), t["name"], f"{installed} · {tid}")
            seen = last_seen.get(tid)
            fresh = bool(seen) and (now - seen < ACTIVE_FRESH_MS)
            row.add_trailing(make_pill("最近有事件" if fresh else "空闲", fresh))
            row.set_selected(active == tid)
            row.clicked.connect(lambda x=tid: self._set_active(x))
            self._rows_box.addWidget(row)
            self._rows[tid] = row

    def _set_active(self, tool_id: str):
        config.write_active_tool(tool_id)
        self.refresh()
