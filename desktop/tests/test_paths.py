import os
from clawd_mochi.core import paths


def test_config_dir_uses_env(monkeypatch):
    monkeypatch.setenv("CLAWD_CONFIG_DIR", "/tmp/custom-clawd")
    assert paths.config_dir() == "/tmp/custom-clawd"


def test_config_dir_default(monkeypatch):
    monkeypatch.delenv("CLAWD_CONFIG_DIR", raising=False)
    expected = os.path.join(os.path.expanduser("~"), ".clawd-mood")
    assert paths.config_dir() == expected


def test_today_str_formats_zero_padded():
    # 2026-06-07 09:30 本地时间 → 毫秒时间戳
    import datetime as dt
    ts = int(dt.datetime(2026, 6, 7, 9, 30).timestamp() * 1000)
    assert paths.today_str(ts) == "2026-06-07"
