# Clawd Agent · 控制台

本地控制台现为两页面板：「今日陪伴」（当天陪伴时长 / 会话 / 提问 / 最长连续专注 / 午睡叙事）和「工具绑定」（绑定 Mochi 当前响应哪一款 AI 工具）。被绑定工具的 hook 事件才驱动设备，其他工具静默忽略。

## 运行

```bash
node clawd-agent.js          # 默认端口 6624,可用 CLAWD_AGENT_PORT 覆盖
```

浏览器打开 http://127.0.0.1:6624 ，在「今日陪伴」查看当天陪伴概览，在「工具绑定」单选当前工具。

## HTTP 接口

- `GET /today`（可加 `?date=YYYY-MM-DD`，默认当天）：用 20 分钟空闲阈值聚合当天事件日志，返回「今日陪伴」所需数据。
- `POST /device/presence`(body `{state}`,state∈auto/meeting/toilet/solder/rest):校验后代理到设备 `GET /presence?s=`,并把 `presence` 写入 `agent.json`(面板「状态」页高亮读此)。

## 给某款工具的 hook 声明来源

安装该工具的 hook 时设环境变量 `CLAWD_SOURCE=<工具id>`（如 `cursor`/`lingma`），或加 `--source=<id>` argv。
内置 Claude Code(`cc`)/Cursor(`cursor`) 可由事件名自动推断，无需显式设置。

## 配置文件（`~/.clawd-mood/`，或 `CLAWD_CONFIG_DIR` 覆盖）

- `agent.json`：`activeTool` + 工具注册表（控制台写、hook 读）。`activeTool` 为空 ⇒ 不门控（所有工具放行，兼容旧版）。
- `agent-state.json`：`lastSeen`（hook 写、控制台读，用于活动指示）。
- `events-YYYY-MM-DD.jsonl`：按天事件日志（hook 写、控制台读），供「今日陪伴」聚合。

## 扩展一款新 AI 工具

1. 在 `agent.json.tools` 加一条 `{id, name}`。
2. 给该工具按其 hook 机制安装 `clawd-hook.js` 并设 `CLAWD_SOURCE=<id>`。
3. 完成——它的事件即纳入门控，可在控制台被选为当前工具。

## 测试

```bash
node --test test/agent.test.js
```

## 后续

"跟随聚焦窗口"自动切换、把一键配置环境（`hook/install-global.ps1`）并入控制台"设置"页 —— 均留待后续；当前手动运行。
