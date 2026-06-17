from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton,
)

from clawd_mochi.core import device, hookinstall


class SettingsPage(QWidget):
    def __init__(self):
        super().__init__()
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(28, 24, 28, 24)
        self._layout.addWidget(QLabel("<h1>设置 · Hook 安装</h1>"))
        self._layout.addWidget(QLabel(
            "为每款 AI 工具单独安装 Clawd hook。安装前会检查本机是否装有该工具；"
            "原配置自动备份。"))

        row = QHBoxLayout()
        row.addWidget(QLabel("设备地址："))
        self._ip = QLineEdit(device.resolve_device_target())
        row.addWidget(self._ip, 1)
        self._layout.addLayout(row)

        self._rows_box = QVBoxLayout()
        self._layout.addLayout(self._rows_box)

        self._msg = QLabel("")
        self._msg.setWordWrap(True)
        self._layout.addWidget(self._msg)
        self._layout.addStretch(1)
        self.refresh()

    def _clear_rows(self):
        while self._rows_box.count():
            w = self._rows_box.takeAt(0).widget()
            if w:
                w.deleteLater()

    def refresh(self):
        self._clear_rows()
        for tid in hookinstall.list_tools():
            spec = hookinstall.TOOLS[tid]
            present = hookinstall.tool_present(tid)
            installed = hookinstall.hook_installed(tid)
            row = QWidget()
            rl = QHBoxLayout(row)
            rl.setContentsMargins(0, 0, 0, 0)
            present_txt = "本机已检测到" if present else "未检测到"
            inst_txt = "hook 已配置" if installed else "未配置"
            rl.addWidget(QLabel(f"<b>{spec['name']}</b>　{present_txt} · {inst_txt}"), 1)
            btn = QPushButton("安装 / 更新")
            btn.setEnabled(present)               # 未检测到该工具 → 置灰
            btn.clicked.connect(lambda _=False, x=tid: self._install(x))
            rl.addWidget(btn)
            self._rows_box.addWidget(row)

    def _install(self, tool_id: str):
        ip = self._ip.text().strip() or "clawd.local"
        try:
            r = hookinstall.install_hook_for(tool_id, ip)
        except hookinstall.NodeNotFound:
            self._msg.setText("❌ 未找到 Node.js，请先安装 Node 或在 PATH 中可用。")
            return
        except Exception as e:  # 定位 hook 源失败等
            self._msg.setText(f"❌ 安装失败：{e}")
            return
        name = hookinstall.TOOLS[tool_id]["name"]
        if not r["ok"]:
            self._msg.setText(f"⚠️ 未检测到 {name}，已跳过。")
            return
        self._msg.setText(f"✅ {name} 已配置（设备 {r['device_ip']}）。重启该工具后开新会话生效。")
        self.refresh()
