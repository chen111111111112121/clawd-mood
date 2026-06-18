import json
from clawd_mochi.core import device


def test_resolve_env_takes_priority(tmp_path, monkeypatch):
    monkeypatch.setenv("CLAWD_DEVICE_IP", "10.0.0.9")
    assert device.resolve_device_target(str(tmp_path)) == "10.0.0.9"


def test_resolve_from_device_json(tmp_path, monkeypatch):
    monkeypatch.delenv("CLAWD_DEVICE_IP", raising=False)
    hook_dir = tmp_path / "hook"
    hook_dir.mkdir()
    (hook_dir / "device.json").write_text(json.dumps({"device_ip": "192.168.1.50"}), encoding="utf-8")
    assert device.resolve_device_target(str(tmp_path)) == "192.168.1.50"


def test_resolve_local_swaps_to_cached_ip(tmp_path, monkeypatch):
    monkeypatch.delenv("CLAWD_DEVICE_IP", raising=False)
    hook_dir = tmp_path / "hook"
    hook_dir.mkdir()
    (hook_dir / "device.json").write_text(json.dumps({"device_ip": "clawd.local"}), encoding="utf-8")
    (hook_dir / "device-cache.json").write_text(json.dumps({"ip": "192.168.1.77"}), encoding="utf-8")
    assert device.resolve_device_target(str(tmp_path)) == "192.168.1.77"


def test_resolve_default_local(tmp_path, monkeypatch):
    monkeypatch.delenv("CLAWD_DEVICE_IP", raising=False)
    assert device.resolve_device_target(str(tmp_path)) == "clawd.local"


def test_device_test_ok(monkeypatch):
    monkeypatch.setattr(device, "_http_get", lambda url, timeout=1.5: "{}")
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    r = device.device_test()
    assert r == {"ok": True, "target": "1.2.3.4"}


def test_device_test_probes_root_not_state(monkeypatch):
    # 探活必须打根路径 /(设备无 /state,打 /state 会 404 → 永远判不可达)
    seen = {}
    monkeypatch.setattr(device, "_http_get", lambda url, timeout=1.5: seen.setdefault("url", url) or "")
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    device.device_test()
    assert seen["url"] == "http://1.2.3.4/"


def test_device_test_fail(monkeypatch):
    def boom(url, timeout=1.5):
        raise OSError("unreachable")
    monkeypatch.setattr(device, "_http_get", boom)
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    assert device.device_test() == {"ok": False, "target": "1.2.3.4"}


def test_send_presence_builds_url(monkeypatch):
    seen = {}
    monkeypatch.setattr(device, "_http_get", lambda url, timeout=1.5: seen.setdefault("url", url) or "{}")
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    r = device.send_presence("meeting")
    assert seen["url"] == "http://1.2.3.4/presence?s=meeting"
    assert r == {"ok": True, "target": "1.2.3.4", "presence": "meeting"}


def test_send_presence_rejects_bad_state():
    r = device.send_presence("bogus")
    assert r["ok"] is False
    assert "bad" in r.get("error", "").lower()


def test_get_info_returns_none_when_absent(monkeypatch):
    def boom(url, timeout=1.5):
        raise OSError("404 / no such endpoint")
    monkeypatch.setattr(device, "_http_get", boom)
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    assert device.get_info() is None


def test_get_info_parses(monkeypatch):
    monkeypatch.setattr(device, "_http_get", lambda url, timeout=1.5: '{"version":"1.2.3"}')
    monkeypatch.setattr(device, "resolve_device_target", lambda *a, **k: "1.2.3.4")
    assert device.get_info() == {"version": "1.2.3"}
