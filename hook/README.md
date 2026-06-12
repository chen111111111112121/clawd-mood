# Clawd Mochi Hook（Claude Code + Cursor）

将 Claude Code / Cursor Agent 的会话事件推送到 Clawd Mochi 设备，屏幕会根据状态自动切换表情。

**同一脚本** `clawd-hook.js` 同时支持 Claude Code 与 Cursor。

## 状态映射

| Claude Code 事件 | 设备状态 | 屏幕表现 |
| ---------------- | -------- | -------- |
| `SessionStart` | `idle` | Normal Eyes |
| `UserPromptSubmit` | `thinking` | Squish Eyes |
| `PreToolUse` | `working` | 收窄双眼扫视（Scan 眼） |
| `PostToolUse` | `working` | 收窄双眼扫视（Scan 眼） |
| `Stop` | `done` | 开心笑眼 + 闪光 → 自动 idle |
| `Notification` | `alert` | Logo 动画 |
| `SessionEnd` | `offline` | Normal Eyes + 关闭背光 |

## 工具语义（act/info）

`PreToolUse` / `PostToolUse` 事件会额外附带工具语义：

```
GET /status?s=working&act=<act>&info=<短文本>
```

| 工具 | act | info |
| ---- | --- | ---- |
| Read / Glob / Grep | `read` | 文件名或搜索词 |
| Edit / Write / NotebookEdit | `edit` | 文件名 |
| Bash | `run` | 命令首词 |
| WebFetch / WebSearch | `net` | 域名 / 查询词 |
| Task（子代理） | `agent` | 任务描述 |
| 其他 | `work` | 工具名 |

info 仅保留可打印 ASCII、截断 22 字符；老固件忽略这些参数。

### 调试

`CLAWD_DRY=1` 时只打印将要请求的 URL，不发 HTTP：

```bash
echo '{"hook_event_name":"PreToolUse","tool_name":"Edit","tool_input":{"file_path":"x/main.cpp"}}' | CLAWD_DRY=1 node clawd-hook.js
```

运行单测：`node --test hook/test/semantics.test.js`

## 前置条件

1. 已烧录支持 Monitor 模式的固件（含 `/status` 端点）
2. 设备已通过 Web 控制器配置并连接家里 WiFi
3. 记下设备在局域网中的 IP（如 `192.168.1.100`）
4. PC 与设备在同一局域网（PC 可正常上网）

## 安装步骤

### 1. 配置设备 IP

复制示例配置并填入设备 IP：

```bash
cp device.json.example device.json
```

编辑 `device.json`：

```json
{
  "device_ip": "192.168.1.100"
}
```

或使用环境变量（优先级更高）：

```bash
set CLAWD_DEVICE_IP=192.168.1.100
```

### 2. 测试 Hook

```bash
node clawd-hook.js SessionStart
node clawd-hook.js UserPromptSubmit
node clawd-hook.js Stop
```

设备应依次切换为普通眼、眯眼、普通眼。

### 3. 配置 Claude Code Hooks

编辑 `%USERPROFILE%\.claude\settings.json`（Windows）或 `~/.claude/settings.json`（macOS/Linux）。

将 `HOOK_PATH` 替换为本目录下 `clawd-hook.js` 的**绝对路径**（Windows 建议使用正斜杠）：

```json
{
  "hooks": {
    "SessionStart": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js SessionStart" }
    ],
    "UserPromptSubmit": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js UserPromptSubmit" }
    ],
    "PreToolUse": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js PreToolUse" }
    ],
    "PostToolUse": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js PostToolUse" }
    ],
    "Stop": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js Stop" }
    ],
    "Notification": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js Notification" }
    ],
    "SessionEnd": [
      { "type": "command", "command": "node HOOK_PATH/clawd-hook.js SessionEnd" }
    ]
  }
}
```

**Windows 示例：**

```json
"command": "node D:/Desktop/AI/clawd-micho/clawd-mochi/hook/clawd-hook.js SessionStart"
```

### 4. 重启 Claude Code

保存 `settings.json` 后重启 Claude Code，启动新会话即可看到设备进入 `idle` 状态。

---

## Cursor 配置

Cursor 使用 `hooks.json`（不是 Claude 的 `settings.json`）。

### 方式 A：一键全局安装（Cursor + Claude Code，推荐）

在 `hook` 目录运行（将 IP 换成设备屏幕上的地址）：

```powershell
cd hook
.\install-global.ps1 -DeviceIP 192.168.150.21
```

脚本会：

1. 复制 `clawd-hook.js` 到 `%USERPROFILE%\.clawd-mochi\hook\`
2. 写入 `%USERPROFILE%\.clawd-mochi\hook\device.json`
3. 写入 Cursor 全局 `%USERPROFILE%\.cursor\hooks.json`
4. **合并** Claude Code `%USERPROFILE%\.claude\settings.json` 的 `hooks`（不覆盖其他设置）

仅装 Cursor：加 `-SkipClaude`  
仅装 Claude Code：加 `-SkipCursor`

IP 变更时重跑：`.\install-global.ps1 -DeviceIP <新IP>`  
或只编辑 `%USERPROFILE%\.clawd-mochi\hook\device.json`

重启 Cursor（Agent 模式）或 Claude Code（新会话）后生效。

### 方式 B：已手动配置的全局 hooks

文件：`%USERPROFILE%\.cursor\hooks.json`

### 方式 C：仅当前项目

仓库内已有：`.cursor/hooks.json`（打开本仓库时生效）

### 方式 D：手动安装

```powershell
copy hook\cursor-hooks.json.example %USERPROFILE%\.cursor\hooks.json
# 编辑其中的 node.exe 路径（若不在 D:\nodejs）
```

### Cursor 事件映射

| Cursor 事件 | 设备状态 | 屏幕表现 |
| ----------- | -------- | -------- |
| `sessionStart` | `idle` | 空闲动画 |
| `beforeSubmitPrompt` | `thinking` | 眯眼 |
| `preToolUse` / `postToolUse` | `working` | 快速扫视 |
| `subagentStart` / `subagentStop` | `working` | 快速扫视 |
| `stop` | `done` | 闪光庆祝 → idle |
| `sessionEnd` | `offline` | 关背光 |
| `postToolUseFailure` | `alert` | Logo 动画 |
| `preCompact` | `thinking` | 眯眼 |

> 未映射的事件（如 `afterAgentThought`）会静默跳过，避免刷屏。

### 启用与调试

1. **重启 Cursor** 或保存 `hooks.json` 后等待自动重载
2. 打开 **Settings → Hooks** 查看是否加载
3. 查看 **Hooks** 输出通道排查错误
4. 测试：在 Agent 里发一条消息，设备应切到 `thinking`

```powershell
# 模拟 Cursor beforeSubmitPrompt
echo '{"hook_event_name":"beforeSubmitPrompt"}' | & "D:\nodejs\node.exe" "D:/Desktop/AI/clawd-micho/clawd-mochi/hook/clawd-hook.js"
```

---

## 设备端 WiFi 配置

1. 手机连接设备热点 `ClaWD-Mochi` / `clawd1234`
2. 浏览器打开 `http://192.168.4.1`
3. 在 **wifi setup** 区域输入家里 WiFi 的 SSID 和密码
4. 点击 **Save & Connect**
5. 连接成功后页面和屏幕会显示设备局域网 IP
6. 将该 IP 写入 `device.json`

设备同时保留 AP 热点，可随时用手机控制；PC Hook 通过 STA 局域网 IP 通信。

## 故障排查

| 现象 | 可能原因 |
| ---- | -------- |
| Hook 无反应 | IP 错误、设备未连上家里 WiFi、防火墙拦截 |
| 状态不更新 | 未进入 Monitor 模式（Hook 会自动切换，也可手动点 Monitor） |
| 30 秒后回到 idle | 正常超时保护（thinking/working 无新事件时自动 idle） |
| Claude Code 报错 | 检查 `node` 是否在 PATH 中、路径是否正确 |
| Cursor Hook 无反应 | 检查 `~/.cursor/hooks.json`、Settings → Hooks、重启 Cursor |
| Clawd on Desk 覆盖配置 | 确认 hooks 指向本仓库的 `clawd-hook.js`，非旧版 Desktop 路径 |

Hook 脚本**静默失败**，不会影响 Claude Code / Cursor 正常使用。

## API 参考

设备端接收：

```
GET http://<device-ip>/status?s=idle|thinking|working|done|alert|offline
```

响应：`{"ok":1}`
