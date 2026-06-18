"""串口固件升级的纯逻辑层（可单测，模块顶层不 import esptool/pyserial）。

职责:串口设备识别、随包固件 manifest 解析、esptool 参数构造、进度行解析、语义版本比较。
实际跑 esptool 在 flash_runner.py;这里只产出"会动逻辑"的可测纯函数。
"""
import os
import re
import sys

# Espressif USB VID(原生 USB-Serial/JTAG)
_ESP_VID = 0x303A
# ESP 强特征:原生 USB JTAG、或具体 USB-UART 桥芯片家族(CP210x/CH34x/FTDI)。
# 故意不匹配泛化的 "USB Serial"/"UART"——那对任何串口都成立,会误标。
_BRIDGE_RE = re.compile(r"JTAG|CP210|CH34\d|CH910|FT232|FTDI|Silicon Labs", re.I)
# 进度百分比:兼容 esptool v4「(62 %)」与 v5「62%」两种形式
_PCT_RE = re.compile(r"(\d+)\s*%")


def classify_port(vid, pid, description: str) -> dict:
    desc = description or ""
    likely = (vid == _ESP_VID) or bool(_BRIDGE_RE.search(desc))
    return {"label": desc, "likely_esp": likely}


def list_serial_ports(comports=None) -> list[dict]:
    """枚举串口;likely_esp 排前。comports 可注入(默认惰性用 pyserial)。"""
    if comports is None:
        from serial.tools import list_ports
        comports = list_ports.comports
    out = []
    for p in comports():
        desc = getattr(p, "description", "") or ""
        c = classify_port(getattr(p, "vid", None), getattr(p, "pid", None), desc)
        label = f"{p.device} — {desc}" if desc and desc != "n/a" else p.device
        out.append({"device": p.device, "label": label, "likely_esp": c["likely_esp"]})
    out.sort(key=lambda x: not x["likely_esp"])   # likely 在前(稳定排序)
    return out


def _repo_build_dir() -> str:
    # desktop/clawd_mochi/core/firmware.py → 仓库根(clawd-mochi)/clawd_mood_idf/build
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "clawd_mood_idf", "build"))


def _packaged_dir() -> str:
    if getattr(sys, "frozen", False):   # PyInstaller 冻结包:固件随 datas 收进 _MEIPASS
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        return os.path.join(base, "clawd_mochi", "firmware")
    # desktop/clawd_mochi/core/firmware.py → clawd_mochi/firmware
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "firmware"))


# 单文件固件 = app 镜像,刷在 factory app 分区起始,**不碰 0x9000 的 NVS**(保留 WiFi/情绪)。
# 见 clawd_mood_idf/partitions.csv:nvs@0x9000 / factory app@0x10000。
APP_OFFSET = "0x10000"
_VER_IN_NAME_RE = re.compile(r"v?(\d+\.\d+\.\d+)")


def resolve_firmware_dir() -> str:
    """固件目录解析:CLAWD_FIRMWARE_DIR > 打包目录(有 .bin) > 仓库 build(dev 兜底) > 打包目录。"""
    env = os.environ.get("CLAWD_FIRMWARE_DIR")
    if env:
        return env.strip()
    pkg = _packaged_dir()
    if bundled_firmware(pkg):
        return pkg
    build = _repo_build_dir()
    if os.path.exists(os.path.join(build, "clawd_mood_idf.bin")):
        return build
    return pkg


def bundled_firmware(firmware_dir: str | None = None) -> str | None:
    """返回随包/构建出的 app 镜像路径;找不到返回 None。"""
    import glob
    firmware_dir = firmware_dir or resolve_firmware_dir()
    for pat in ("clawd-mochi-app-*.bin", "clawd_mood_idf.bin"):
        hits = sorted(glob.glob(os.path.join(firmware_dir, pat)))
        if hits:
            return hits[-1]
    return None


def version_from_filename(path: str) -> str:
    """从文件名取版本(如 clawd-mochi-app-v1.2.3.bin → 1.2.3);取不到返回 'unknown'。"""
    m = _VER_IN_NAME_RE.search(os.path.basename(path or ""))
    return m.group(1) if m else "unknown"


def read_app_version(path: str) -> str:
    """从 app 镜像烤进的 esp_app_desc 读权威版本(不信文件名)。
    布局:esp_image_header(0x18)+segment_header(0x08) 后是 esp_app_desc,
    magic 0xABCD5432 @文件 0x20,version[32] @文件 0x30。读不出返回 'unknown'。"""
    import struct
    try:
        with open(path, "rb") as f:
            head = f.read(0x60)
    except OSError:
        return "unknown"
    if len(head) < 0x50 or struct.unpack_from("<I", head, 0x20)[0] != 0xABCD5432:
        return "unknown"
    ver = head[0x30:0x50].split(b"\x00", 1)[0].decode("ascii", "replace").strip()
    return ver or "unknown"


def firmware_display_version(path: str) -> str:
    """展示用版本:优先二进制烤进的权威版本,回退文件名(别人改名也尽量读对)。"""
    v = read_app_version(path)
    return v if v != "unknown" else version_from_filename(path)


def build_flash_args(port: str, file_path: str, baud: int = 460800, offset: str = APP_OFFSET) -> list[str]:
    """单文件刷写参数:app 镜像写到 offset(默认 0x10000)。
    用 --flash_*=keep:app 镜像非 bootloader,不需要(也不应)改写 flash 头设置。
    下划线参数 esptool v4/v5 都接受。"""
    return ["--chip", "esp32c3", "-p", port, "-b", str(baud),
            "--before", "default_reset", "--after", "hard_reset",
            "write_flash",
            "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
            offset, os.path.normpath(file_path)]


def parse_progress(line: str):
    m = _PCT_RE.search(line or "")
    return int(m.group(1)) if m else None


def parse_version(s: str) -> tuple:
    s = (s or "").strip().lstrip("vV")
    s = re.split(r"[-+ ]", s, maxsplit=1)[0]   # 去掉 -rc1 / +build 等
    nums = []
    for part in s.split(".")[:3]:
        try:
            nums.append(int(part))
        except ValueError:
            nums.append(0)
    while len(nums) < 3:
        nums.append(0)
    return tuple(nums)


def cmp_version(a: str, b: str) -> int:
    va, vb = parse_version(a), parse_version(b)
    return (va > vb) - (va < vb)
