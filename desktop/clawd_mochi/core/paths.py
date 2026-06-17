"""配置目录与日期工具。沿用 Node 控制台约定：CLAWD_CONFIG_DIR || ~/.clawd-mood。"""
import os
import datetime as dt


def config_dir() -> str:
    return os.environ.get("CLAWD_CONFIG_DIR") or os.path.join(
        os.path.expanduser("~"), ".clawd-mood"
    )


def today_str(now_ms: int | None = None) -> str:
    d = dt.datetime.now() if now_ms is None else dt.datetime.fromtimestamp(now_ms / 1000)
    return f"{d.year:04d}-{d.month:02d}-{d.day:02d}"
