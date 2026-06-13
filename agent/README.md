# Clawd Agent · 工具绑定控制台

绑定 Mochi 当前响应哪一款 AI 工具。被绑定工具的 hook 事件才驱动设备，其他工具静默忽略。

## 运行

```bash
node clawd-agent.js          # 默认端口 6624,可用 CLAWD_AGENT_PORT 覆盖
```

浏览器打开 http://127.0.0.1:6624 ，单选当前工具。

## 给某款工具的 hook 声明来源

安装该工具的 hook 时设环境变量 `CLAWD_SOURCE=<工具id>`（如 `cursor`/`lingma`），或加 `--source=<id>` argv。
内置 Claude Code(`cc`)/Cursor(`cursor`) 可由事件名自动推断，无需显式设置。

## 配置文件（`~/.clawd-mood/`，或 `CLAWD_CONFIG_DIR` 覆盖）

- `agent.json`：`activeTool` + 工具注册表（控制台写、hook 读）。`activeTool` 为空 ⇒ 不门控（所有工具放行，兼容旧版）。
- `agent-state.json`：`lastSeen`（hook 写、控制台读，用于活动指示）。

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
