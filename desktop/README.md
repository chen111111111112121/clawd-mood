# Clawd Mochi 桌面上位机（Qt-Python）

跨平台桌面控制台，取代旧版 Node 控制台（`../agent/`）与 PowerShell 安装脚本。
本阶段实现：今日陪伴统计、工具绑定、presence 状态牌、设备连通性、**Hook 一键安装**、**串口刷固件**。

## 运行（开发）

    cd desktop
    python -m pip install -e ".[dev]"
    python -m clawd_mochi

## 固件升级（串口刷写 · 单文件）

「固件升级」页：读设备 `/info` 显示当前版本 → 升级到版本 → 选串口 →（可选「选择文件…」用下载的固件）→ 一键刷写。

**单文件 = app 镜像**（`clawd-mochi-app-v<版本>.bin`，每次编译就一个文件）。刷写写到 app 分区 `0x10000`，
**不碰 `0x9000` 的 NVS → 升级保留 WiFi / 情绪配置**。把这个 `.bin` 发给别人，对方在本页「选择文件…」选它即可升级。

> 前提：设备已有本固件的 bootloader+分区表（出厂/刷过的设备都满足）。**全新空白芯片**需先用 IDF 工具链
> （`_build_idf.bat -p COMx flash`）做一次完整烧录，之后即可用单文件升级。

**版本号单一来源** = `clawd_mood_idf/version.txt`（如 `1.0.0`）。IDF 把它烤进 app 镜像；设备 `/info`、
固件文件名、PC 显示全部由它决定，零漂移。改版本只动 version.txt 再重编。PC 端展示时**直接从 .bin 读**
烤进的版本（不信文件名，别人改了名也读得出真版本）。

**发版前生成固件包**：

    # 1) 改 clawd_mood_idf/version.txt（如 1.1.0）→ 2) 重编固件 → 3) 生成单文件
    python ../tools/bundle_firmware.py     # 读 build 版本 → clawd_mochi/firmware/clawd-mochi-app-v<版本>.bin

**打包成「拿到即烧」单包**（目标机零环境）：

    python -m pip install pyinstaller
    pyinstaller clawd-mochi.spec           # 产物 dist/ClawdMochi/

打出的 exe 自带 Python + esptool + 固件镜像。全新电脑**无需** Python/Node/esptool/驱动即可烧录
（设备走 ESP32-C3 原生 USB，Win10/11 自带 CDC 驱动免装；若用 CH340/CP210x 串口板需装厂商驱动）。
**Node 仅用于 Hook 实时监测**，与烧录无关。

## 测试

    cd desktop && python -m pytest -v

## 数据约定

沿用 `~/.clawd-mood/`（可用环境变量 `CLAWD_CONFIG_DIR` 覆盖）：
- `agent.json`：本应用独占写 `{ activeTool, tools, presence }`，Hook 只读。
- `agent-state.json`：Hook 独占写 `{ lastSeen }`，本应用只读。
- `events-YYYY-MM-DD.jsonl`：Hook 按天 append，本应用聚合成「今日陪伴」。
- `hook/device.json`、`hook/device-cache.json`：设备地址解析。

## 路线（后续计划）

- 固件升级进阶：从 GitHub Releases 拉新版 + 语义版本比对「需升级」判定。
- zeroconf 设备发现、串口配网、正式托盘/应用图标。
