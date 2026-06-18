"""固件升级页:展示设备当前版本 → 随附版本,选串口一键刷写。视觉真值 web/mockups/desktop-firmware.html。

线程:取 /info(VersionWorker)与刷写(FlashWorker)都在 QThread 里跑,信号回主线程更新 UI,
不阻塞界面。esptool 进程内调用(flash_runner),打包后 exe 自带,目标机零环境可刷。
"""
import os

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QPushButton,
    QProgressBar, QTextEdit, QFrame, QFileDialog,
)

from clawd_mochi.core import device, firmware, flash_runner
from clawd_mochi.ui import theme


class VersionWorker(QThread):
    got = Signal(object)   # dict | None

    def run(self):
        self.got.emit(device.get_info())


class FlashWorker(QThread):
    line = Signal(str)
    done = Signal(bool, object)   # ok, err|None

    def __init__(self, argv):
        super().__init__()
        self._argv = argv

    def run(self):
        try:
            flash_runner.run_flash(
                self._argv,
                on_output=self.line.emit,
                on_done=lambda ok, err: self.done.emit(ok, err),
            )
        except flash_runner.EsptoolMissing:
            self.done.emit(False, "未找到 esptool(打包异常);正常安装包不应出现")
        except Exception as e:  # noqa: BLE001
            self.done.emit(False, str(e))


def _vtile(key: str, accent: bool):
    box = QFrame()
    box.setObjectName("VTile")
    theme.card_shadow(box)
    lay = QVBoxLayout(box)
    lay.setContentsMargins(14, 12, 14, 12)
    lay.setSpacing(2)
    k = QLabel(key)
    k.setObjectName("VKey")
    val = QLabel("…")
    val.setObjectName("VVal")
    if accent:
        val.setProperty("accent", "true")
    meta = QLabel("")
    meta.setObjectName("VMeta")
    lay.addWidget(k)
    lay.addWidget(val)
    lay.addWidget(meta)
    return box, val, meta


class FirmwarePage(QWidget):
    def __init__(self):
        super().__init__()
        self._vworker = None
        self._fworker = None
        self._flashing = False

        root = QVBoxLayout(self)
        root.setContentsMargins(30, 24, 30, 28)
        root.setSpacing(12)

        h1 = QLabel("固件升级")
        h1.setObjectName("H1")
        root.addWidget(h1)

        card = QFrame()
        card.setObjectName("Card")
        theme.card_shadow(card)
        cl = QVBoxLayout(card)
        cl.setContentsMargins(20, 18, 20, 20)
        cl.setSpacing(14)
        root.addWidget(card)
        root.addStretch(1)

        # 设备当前版本
        vrow = QHBoxLayout()
        vrow.setSpacing(14)
        cur_box, self._cur_val, self._cur_meta = _vtile("设备当前版本", False)
        vrow.addWidget(cur_box, 1)
        vrow.addStretch(1)
        cl.addLayout(vrow)

        # 串口选择
        prow = QHBoxLayout()
        prow.setSpacing(10)
        plabel = QLabel("串口设备")
        plabel.setObjectName("PortLabel")
        self._port = QComboBox()
        self._port.setObjectName("Port")
        refresh_btn = QPushButton("↻ 刷新")
        refresh_btn.setObjectName("Mini")
        refresh_btn.setCursor(Qt.PointingHandCursor)
        refresh_btn.clicked.connect(self._refresh_ports)
        prow.addWidget(plabel)
        prow.addWidget(self._port, 1)
        prow.addWidget(refresh_btn)
        cl.addLayout(prow)

        # 升级固件文件(默认随包;可选下载的 .bin)
        frow = QHBoxLayout()
        frow.setSpacing(10)
        flabel = QLabel("升级固件")
        flabel.setObjectName("PortLabel")
        self._file_label = QLabel("…")
        self._file_label.setObjectName("Hint")
        pick_btn = QPushButton("选择文件…")
        pick_btn.setObjectName("Mini")
        pick_btn.setCursor(Qt.PointingHandCursor)
        pick_btn.clicked.connect(self._pick_file)
        frow.addWidget(flabel)
        frow.addWidget(self._file_label, 1)
        frow.addWidget(pick_btn)
        cl.addLayout(frow)
        self._firmware_path = None
        self._user_picked = False

        # 操作
        arow = QHBoxLayout()
        arow.setSpacing(12)
        self._flash_btn = QPushButton("刷写固件")
        self._flash_btn.setObjectName("Primary")
        self._flash_btn.setCursor(Qt.PointingHandCursor)
        self._flash_btn.clicked.connect(self._on_flash)
        arow.addWidget(self._flash_btn)
        arow.addStretch(1)
        cl.addLayout(arow)

        # 进度
        self._prog = QProgressBar()
        self._prog.setObjectName("Prog")
        self._prog.setRange(0, 100)
        self._prog.setValue(0)
        self._prog.setTextVisible(False)
        self._prog.hide()
        cl.addWidget(self._prog)
        self._prog_meta = QLabel("")
        self._prog_meta.setObjectName("ProgMeta")
        self._prog_meta.hide()
        cl.addWidget(self._prog_meta)

        # 状态横幅
        self._banner = QLabel("")
        self._banner.setObjectName("Banner")
        self._banner.setWordWrap(True)
        self._banner.hide()
        cl.addWidget(self._banner)

        # 日志
        self._log = QTextEdit()
        self._log.setObjectName("Log")
        self._log.setReadOnly(True)
        self._log.setFixedHeight(132)
        self._log.hide()
        cl.addWidget(self._log)

    # —— 数据 ——
    def refresh(self):
        if not self._flashing:
            if not self._user_picked:                       # 默认用随包固件
                self._firmware_path = firmware.bundled_firmware()
            self._update_firmware_label()
            self._refresh_ports()
            self._fetch_version()

    def _update_firmware_label(self):
        path = self._firmware_path
        if not path:
            self._file_label.setText("未找到固件文件")
            return
        ver = firmware.firmware_display_version(path)   # 优先读二进制烤进的版本,回退文件名
        tag = ("v" + ver) if ver != "unknown" else "版本未知"
        self._file_label.setText(f"{os.path.basename(path)}  ({tag})")

    def _pick_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择固件文件", "", "固件镜像 (*.bin)")
        if path:
            self._firmware_path = path
            self._user_picked = True
            self._hide_banner()
            self._update_firmware_label()

    def _fetch_version(self):
        self._cur_val.setText("…")
        self._cur_meta.setText("读取中…")
        self._vworker = VersionWorker()
        self._vworker.got.connect(self._on_version)
        self._vworker.start()

    def _on_version(self, info):
        if info and info.get("version"):
            self._cur_val.setText("v" + str(info["version"]))
            self._cur_meta.setText(f"{info.get('chip', 'esp32c3')} · 读自 /info")
        else:
            self._cur_val.setText("未知")
            self._cur_meta.setText("设备未上线;刷写不受影响")

    def _refresh_ports(self):
        prev = self._port.currentData()
        self._port.clear()
        ports = firmware.list_serial_ports()
        for p in ports:
            tag = "  ★" if p["likely_esp"] else ""
            self._port.addItem(p["label"] + tag, p["device"])
        if not ports:
            self._flash_btn.setEnabled(False)
            self._set_banner("warn", "未检测到串口设备。请用 USB 数据线（非充电线）连接 Mochi，再点「刷新」。"
                             "若用的是 CH340/CP210x 串口板，请先安装对应驱动。")
        else:
            self._flash_btn.setEnabled(not self._flashing)
            if self._banner.property("kind") == "warn":
                self._hide_banner()
            if prev:                                  # 尽量保持上次选择
                i = self._port.findData(prev)
                if i >= 0:
                    self._port.setCurrentIndex(i)

    # —— 刷写 ——
    def _on_flash(self):
        if self._flashing:
            return
        port = self._port.currentData()
        if not port:
            self._set_banner("warn", "请先选择串口设备。")
            return
        if not self._firmware_path or not os.path.exists(self._firmware_path):
            self._set_banner("bad", "未找到固件文件，请用「选择文件…」指定下载的 .bin。")
            return
        argv = firmware.build_flash_args(port, self._firmware_path)

        self._flashing = True
        self._flash_btn.setEnabled(False)
        self._flash_btn.setText("刷写中…")
        self._port.setEnabled(False)
        self._hide_banner()
        self._prog.setValue(0)
        self._prog.show()
        self._prog_meta.setText("连接设备…请勿拔线")
        self._prog_meta.show()
        self._log.clear()
        self._log.show()

        self._fworker = FlashWorker(argv)
        self._fworker.line.connect(self._on_line)
        self._fworker.done.connect(self._on_done)
        self._fworker.start()

    def _on_line(self, line: str):
        self._log.append(line)
        self._log.verticalScrollBar().setValue(self._log.verticalScrollBar().maximum())
        pct = firmware.parse_progress(line)
        if pct is not None:
            self._prog.setValue(pct)
            self._prog_meta.setText(f"写入中… {pct}%")

    def _on_done(self, ok: bool, err):
        self._flashing = False
        self._flash_btn.setEnabled(True)
        self._flash_btn.setText("刷写固件")
        self._port.setEnabled(True)
        if ok:
            self._prog.setValue(100)
            self._prog_meta.setText("完成")
            self._set_banner("ok", "✅ 刷写完成，设备已重启。可拔线，等待开机问候。")
        else:
            self._set_banner("bad", f"❌ 刷写失败：{err or '未知错误'}。请检查串口是否被占用后重试。")

    # —— 横幅 ——
    def _set_banner(self, kind: str, text: str):
        self._banner.setText(text)
        self._banner.setProperty("kind", kind)
        theme.repolish(self._banner)
        self._banner.show()

    def _hide_banner(self):
        self._banner.hide()
        self._banner.setProperty("kind", "")
