from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton,
)

from clawd_mochi.core import device, hookinstall
from clawd_mochi.ui import theme
from clawd_mochi.ui.widgets import make_card, SelectableRow

_ICON = {"cc": "cc", "cursor": "cursor"}


class SettingsPage(QWidget):
    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(30, 24, 30, 28)
        root.setSpacing(12)

        h1 = QLabel("设置 · Hook 安装")
        h1.setObjectName("H1")
        root.addWidget(h1)

        card = make_card()
        cl = QVBoxLayout(card)
        cl.setContentsMargins(20, 18, 20, 18)
        cl.setSpacing(12)

        # 设备地址
        field = QHBoxLayout()
        field.setSpacing(12)
        lab = QLabel("设备地址")
        lab.setObjectName("FieldLabel")
        self._ip = QLineEdit(device.resolve_device_target())
        self._ip.setObjectName("Field")
        field.addWidget(lab)
        field.addWidget(self._ip, 1)
        cl.addLayout(field)

        # 每工具一行
        self._rows_box = QVBoxLayout()
        self._rows_box.setSpacing(10)
        cl.addLayout(self._rows_box)

        self._msg = QLabel("")
        self._msg.setObjectName("Toast")
        self._msg.setProperty("kind", "ok")
        self._msg.setWordWrap(True)
        self._msg.setVisible(False)
        cl.addWidget(self._msg)

        root.addWidget(card)
        root.addStretch(1)
        self.refresh()

    def _clear(self):
        while self._rows_box.count():
            item = self._rows_box.takeAt(0)
            w = item.widget()
            if w:
                w.setParent(None)
                w.deleteLater()

    def refresh(self):
        self._clear()
        for tid in hookinstall.list_tools():
            spec = hookinstall.TOOLS[tid]
            present = hookinstall.tool_present(tid)
            installed = hookinstall.hook_installed(tid)
            present_txt = "本机已检测到" if present else "未检测到（本机未安装）"
            inst_txt = "hook 已配置" if installed else "未配置"
            row = SelectableRow(_ICON.get(tid, "settings"), spec["name"],
                                f"{present_txt} · {inst_txt}", selectable=False)
            btn = QPushButton("安装 / 更新")
            btn.setObjectName("Primary")
            btn.setEnabled(present)
            if present:
                btn.setCursor(Qt.PointingHandCursor)
            btn.clicked.connect(lambda _=False, x=tid: self._install(x))
            row.add_trailing(btn)
            self._rows_box.addWidget(row)

    def _toast(self, text: str, kind: str):
        self._msg.setText(text)
        self._msg.setProperty("kind", kind)
        theme.repolish(self._msg)
        self._msg.setVisible(True)

    def _install(self, tool_id: str):
        ip = self._ip.text().strip() or "clawd.local"
        try:
            r = hookinstall.install_hook_for(tool_id, ip)
        except hookinstall.NodeNotFound:
            self._toast("❌ 未找到 Node.js，请先安装 Node 或在 PATH 中可用。", "bad")
            return
        except Exception as e:  # 定位 hook 源失败等
            self._toast(f"❌ 安装失败：{e}", "bad")
            return
        name = hookinstall.TOOLS[tool_id]["name"]
        if not r["ok"]:
            self._toast(f"⚠️ 未检测到 {name}，已跳过。", "bad")
            return
        self._toast(f"✅ {name} 已配置（设备 {r['device_ip']}）。重启该工具后开新会话生效。", "ok")
        self.refresh()
