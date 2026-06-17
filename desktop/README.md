# Clawd Mochi 桌面上位机（Qt-Python）

跨平台桌面控制台，取代旧版 Node 控制台（`../agent/`）与 PowerShell 安装脚本。
本阶段实现：今日陪伴统计、工具绑定、presence 状态牌、设备连通性、**Hook 一键安装**。

## 运行（开发）

    cd desktop
    python -m pip install -e ".[dev]"
    python -m clawd_mochi

## 测试

    cd desktop && python -m pytest -v

## 数据约定

沿用 `~/.clawd-mood/`（可用环境变量 `CLAWD_CONFIG_DIR` 覆盖）：
- `agent.json`：本应用独占写 `{ activeTool, tools, presence }`，Hook 只读。
- `agent-state.json`：Hook 独占写 `{ lastSeen }`，本应用只读。
- `events-YYYY-MM-DD.jsonl`：Hook 按天 append，本应用聚合成「今日陪伴」。
- `hook/device.json`、`hook/device-cache.json`：设备地址解析。

## 路线（后续计划）

- 串口刷固件（esptool）、zeroconf 设备发现、PyInstaller 打包与正式托盘图标。
