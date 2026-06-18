"""设备 HTTP 客户端（局域网）。

设备目标解析与 Hook 对齐：CLAWD_DEVICE_IP > <cfg>/hook/device.json > 缓存 IP > clawd.local。
取到 clawd.local 时优先换缓存真实 IP（urllib 解析不了 mDNS 主机名）。
"""
import json
import os
import urllib.request

from clawd_mochi.core.config import PRESENCE_VALUES
from clawd_mochi.core.paths import config_dir


def _read_json(path: str) -> dict | None:
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _resolve_cached_ip(directory: str) -> str | None:
    c = _read_json(os.path.join(directory, "hook", "device-cache.json"))
    if c and c.get("ip"):
        return str(c["ip"])
    return None


def resolve_device_target(directory: str | None = None) -> str:
    directory = directory or config_dir()
    env = os.environ.get("CLAWD_DEVICE_IP")
    if env:
        return env.strip()
    cfg = _read_json(os.path.join(directory, "hook", "device.json"))
    if cfg and cfg.get("device_ip"):
        t = str(cfg["device_ip"]).strip()
        return (_resolve_cached_ip(directory) or t) if t == "clawd.local" else t
    return _resolve_cached_ip(directory) or "clawd.local"


# 直连局域网设备:显式禁用代理。否则系统代理(如 Clash)会劫持对设备 LAN IP 的请求,
# 表现为「无法连接设备」。hook 走原生 http 不过代理,这里 urllib 须同样绕开。
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def _http_get(url: str, timeout: float = 1.5) -> str:
    with _OPENER.open(url, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "replace")


def device_test() -> dict:
    target = resolve_device_target()
    try:
        # 探活打根路径(配网页):两版固件都返回 200。设备无 /state(随 Web 手动控制一并移除),
        # 旧探针打 /state 会 404 → urllib 抛 HTTPError → 永远判为不可达。
        _http_get(f"http://{target}/")
        return {"ok": True, "target": target}
    except Exception:
        return {"ok": False, "target": target}


def send_presence(state: str) -> dict:
    if state not in PRESENCE_VALUES:
        return {"ok": False, "error": "bad state", "presence": state}
    target = resolve_device_target()
    try:
        _http_get(f"http://{target}/presence?s={state}")
        return {"ok": True, "target": target, "presence": state}
    except Exception:
        return {"ok": False, "target": target, "presence": state}


def get_info() -> dict | None:
    """读取设备 /info（当前固件未实现 → 返回 None，UI 优雅降级为'版本未知'）。"""
    target = resolve_device_target()
    try:
        return json.loads(_http_get(f"http://{target}/info"))
    except Exception:
        return None
