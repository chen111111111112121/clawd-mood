import os

import pytest

from clawd_mochi.core import firmware


# ── classify_port ─────────────────────────────────────────────
def test_classify_port_espressif_vid():
    assert firmware.classify_port(0x303A, 0x1001, "USB JTAG/serial debug unit")["likely_esp"] is True


def test_classify_port_bridge_by_description():
    assert firmware.classify_port(0x1A86, 0x7523, "USB-SERIAL CH340")["likely_esp"] is True
    assert firmware.classify_port(0x10C4, 0xEA60, "Silicon Labs CP210x UART Bridge")["likely_esp"] is True


def test_classify_port_unknown_not_likely():
    assert firmware.classify_port(0x1234, 0x5678, "Some Random Device")["likely_esp"] is False


def test_classify_port_label_is_description():
    assert firmware.classify_port(0x303A, 1, "USB JTAG")["label"] == "USB JTAG"


# ── list_serial_ports (inject fake comports) ──────────────────
class _FakePort:
    def __init__(self, device, description, vid=None, pid=None):
        self.device, self.description, self.vid, self.pid = device, description, vid, pid


def test_list_serial_ports_orders_likely_first():
    fakes = [
        _FakePort("COM3", "USB Serial Device", vid=0x1234, pid=1),
        _FakePort("COM7", "USB JTAG/serial debug unit", vid=0x303A, pid=0x1001),
    ]
    ports = firmware.list_serial_ports(comports=lambda: fakes)
    assert ports[0]["device"] == "COM7"
    assert ports[0]["likely_esp"] is True
    assert ports[0]["label"] == "COM7 — USB JTAG/serial debug unit"


def test_list_serial_ports_empty():
    assert firmware.list_serial_ports(comports=lambda: []) == []


# ── resolve_firmware_dir ──────────────────────────────────────
def test_resolve_firmware_dir_env_takes_priority(tmp_path, monkeypatch):
    monkeypatch.setenv("CLAWD_FIRMWARE_DIR", str(tmp_path))
    assert firmware.resolve_firmware_dir() == str(tmp_path)


# ── bundled_firmware ──────────────────────────────────────────
def test_bundled_firmware_finds_app_image(tmp_path):
    (tmp_path / "clawd-mochi-app-v1.0.0.bin").write_bytes(b"\x00")
    assert firmware.bundled_firmware(str(tmp_path)).endswith("clawd-mochi-app-v1.0.0.bin")


def test_bundled_firmware_falls_back_to_idf_name(tmp_path):
    (tmp_path / "clawd_mood_idf.bin").write_bytes(b"\x00")
    assert firmware.bundled_firmware(str(tmp_path)).endswith("clawd_mood_idf.bin")


def test_bundled_firmware_none_when_absent(tmp_path):
    assert firmware.bundled_firmware(str(tmp_path)) is None


# ── version_from_filename ─────────────────────────────────────
def test_version_from_filename():
    assert firmware.version_from_filename("clawd-mochi-app-v1.2.3.bin") == "1.2.3"
    assert firmware.version_from_filename("/x/y/fw-2.0.0.bin") == "2.0.0"
    assert firmware.version_from_filename("clawd_mood_idf.bin") == "unknown"


# ── read_app_version (从 .bin 烤进的 esp_app_desc 读权威版本) ──
import struct


def _fake_app_bin(version: str) -> bytes:
    buf = bytearray(0x60)
    struct.pack_into("<I", buf, 0x20, 0xABCD5432)        # esp_app_desc magic
    vb = version.encode("ascii")
    buf[0x30:0x30 + len(vb)] = vb                          # version[32] @ desc+0x10
    return bytes(buf)


def test_read_app_version_from_binary(tmp_path):
    p = tmp_path / "app.bin"
    p.write_bytes(_fake_app_bin("1.4.2"))
    assert firmware.read_app_version(str(p)) == "1.4.2"


def test_read_app_version_non_app_returns_unknown(tmp_path):
    p = tmp_path / "notapp.bin"
    p.write_bytes(b"\x00" * 0x60)                          # 无 magic
    assert firmware.read_app_version(str(p)) == "unknown"


def test_read_app_version_missing_file(tmp_path):
    assert firmware.read_app_version(str(tmp_path / "nope.bin")) == "unknown"


# ── firmware_display_version: 优先二进制,回退文件名 ──
def test_firmware_display_version_prefers_binary(tmp_path):
    p = tmp_path / "renamed-by-user.bin"
    p.write_bytes(_fake_app_bin("3.1.0"))
    assert firmware.firmware_display_version(str(p)) == "3.1.0"   # 文件名没版本,仍读出真版本


def test_firmware_display_version_falls_back_to_name(tmp_path):
    p = tmp_path / "clawd-mochi-app-v2.2.2.bin"
    p.write_bytes(b"\x00" * 0x60)                          # 非法/读不出 → 用文件名
    assert firmware.firmware_display_version(str(p)) == "2.2.2"


# ── build_flash_args (单文件 app 镜像 @0x10000) ────────────────
def test_build_flash_args_shape(tmp_path):
    app = tmp_path / "clawd-mochi-app-v1.0.0.bin"
    app.write_bytes(b"\x00")
    args = firmware.build_flash_args("COM7", str(app), baud=460800)
    assert args[:4] == ["--chip", "esp32c3", "-p", "COM7"]
    assert "-b" in args and "460800" in args
    assert "write_flash" in args
    assert args.index("--before") < args.index("write_flash")
    # app 镜像写在 0x10000(保留 NVS)
    wf = args.index("write_flash")
    assert "0x10000" in args[wf:]
    # 用 keep 不改写 flash 设置(app 镜像非 bootloader)
    assert "--flash_mode" in args and "keep" in args
    # 绝对路径且存在
    assert os.path.normpath(str(app)) in [os.path.normpath(a) for a in args]


def test_build_flash_args_custom_offset(tmp_path):
    f = tmp_path / "full.bin"
    f.write_bytes(b"\x00")
    args = firmware.build_flash_args("COM3", str(f), offset="0x0")
    wf = args.index("write_flash")
    assert "0x0" in args[wf:]


# ── parse_progress ────────────────────────────────────────────
def test_parse_progress_extracts_percent():
    assert firmware.parse_progress("Writing at 0x00010000... (62 %)") == 62  # esptool v4 形式
    assert firmware.parse_progress("Writing at 0x0... (100 %)") == 100
    assert firmware.parse_progress("Writing at 0x10000... 62%") == 62        # esptool v5 形式
    assert firmware.parse_progress("Writing at 0x10000... 7 %") == 7


def test_parse_progress_none_when_no_percent():
    assert firmware.parse_progress("Connecting....") is None
    assert firmware.parse_progress("Hash of data verified.") is None


# ── version helpers ───────────────────────────────────────────
def test_parse_version_tolerant():
    assert firmware.parse_version("v1.2.3") == (1, 2, 3)
    assert firmware.parse_version("1.0") == (1, 0, 0)
    assert firmware.parse_version("unknown") == (0, 0, 0)
    assert firmware.parse_version("2.5.1-rc1") == (2, 5, 1)


def test_cmp_version():
    assert firmware.cmp_version("1.0.0", "1.0.1") == -1
    assert firmware.cmp_version("v2.0.0", "1.9.9") == 1
    assert firmware.cmp_version("1.2.3", "1.2.3") == 0
