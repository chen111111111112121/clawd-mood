#!/usr/bin/env python3
"""把 ESP-IDF 编译出的 **app 镜像** 打成上位机随包固件(单文件)。

单文件 = app 镜像(clawd_mood_idf.bin),刷写时写到 factory app 分区 0x10000,
**不碰 0x9000 的 NVS** → 升级保留 WiFi/情绪。版本写进文件名供 PC 端展示。
发布前跑一次:  python tools/bundle_firmware.py
"""
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
BUILD = os.path.join(REPO, "clawd_mood_idf", "build")
APP_BIN = os.path.join(BUILD, "clawd_mood_idf.bin")
OUT = os.path.join(REPO, "desktop", "clawd_mochi", "firmware")


def read_build_version() -> str:
    """版本来自 IDF 构建产物(源头=工程根 version.txt,已烤进二进制),非手写。"""
    with open(os.path.join(BUILD, "project_description.json"), encoding="utf-8") as f:
        v = json.load(f).get("project_version", "unknown")
    return re.sub(r"[^0-9A-Za-z._-]", "_", str(v)) or "unknown"   # 文件名安全


def main() -> int:
    if not os.path.exists(APP_BIN):
        print(f"找不到 {APP_BIN};先编译固件(_build_idf.bat build)", file=sys.stderr)
        return 1
    version = read_build_version()
    os.makedirs(OUT, exist_ok=True)
    # 清掉旧产物(可能是早期的多文件方案残留)
    for f in os.listdir(OUT):
        if f.endswith(".bin") or f == "manifest.json":
            os.remove(os.path.join(OUT, f))
    dst = os.path.join(OUT, f"clawd-mochi-app-v{version}.bin")
    shutil.copyfile(APP_BIN, dst)
    print(f"固件包已生成 → {dst}  ({os.path.getsize(dst)} B, version {version})")
    print("分发:把这个 .bin 发给用户,PC 软件「固件升级」选它即可(写 0x10000,保留配置)。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
