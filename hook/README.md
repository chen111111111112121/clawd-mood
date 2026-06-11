# Clawd Mochi × Claude Code Hook

将 Claude Code 桌面端的会话事件推送到 Clawd Mochi 设备，屏幕会根据状态自动切换表情。

## 状态映射

| Claude Code 事件 | 设备状态 | 屏幕表现 |
| ---------------- | -------- | -------- |
| `SessionStart` | `idle` | Normal Eyes |
| `UserPromptSubmit` | `thinking` | Squish Eyes |
| `PreToolUse` | `working` | Claude Code 界面 |
| `PostToolUse` | `working` | Claude Code 界面 |
| `Stop` | `idle` | Normal Eyes |
| `Notification` | `alert` | Logo 动画 |
| `SessionEnd` | `offline` | Normal Eyes + 关闭背光 |

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

Hook 脚本**静默失败**，不会影响 Claude Code 正常使用。

## API 参考

设备端接收：

```
GET http://<device-ip>/status?s=idle|thinking|working|alert|offline
```

响应：`{"ok":1}`
