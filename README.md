<p align="center">
  <img src="pics/banner-mood.svg" alt="Clawd Mood" width="100%">
</p>

<h1 align="center">Clawd Mood</h1>



<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--C3-orange" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/display-ST7789%20240x240-orange" alt="ST7789">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT">
</p>

<p align="center">
  <img src="pics/expressions-grid.svg" alt="屏幕表情九宫格" width="62%">
</p>
<p align="center"><sub>屏幕实际表情一览（普通 / 开心 / 爱心 / 星星眼 / 好奇 / 困倦 / 思考 / 惊讶 / 花痴）</sub></p>

> 独立粉丝项目，与 Anthropic 无官方关联。「Claude」「Clawd」为 Anthropic 商标。
> 
> 本项目基于开源项目 **[yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)** 二次开发，在其硬件/外壳基础上重写并扩展了固件（眼睛骨架引擎、情绪引擎、语义监测）与 PC 侧 Hook / 控制台。感谢原作者 Yousuf Amanuel。

3D 外壳模型见 [MakerWorld - Clawd Mochi](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot)（本仓库 `models/` 也附带 STL / 3MF）。

---

## 功能概览

设备以 **Monitor 监测**为核心：PC 上 Cursor 或 Claude Code 通过 Hook 推送状态，屏幕自动切换动画。PC **无需**连设备热点，只要与设备在同一局域网即可。

AP 热点（`ClaWD-Mood` / `clawd1234`）+ `http://192.168.4.1` 仅用于**配置家庭 WiFi**（极简配网页），不提供手动表情 / 终端 / 绘图控制。

### Monitor 模式（Hook 自动推送）

- **Monitor 状态**：`idle` / `thinking` / `working` / `done` / `alert` / `offline`
- **idle**：由**情绪引擎**驱动——按当前心情（专注 / 疲惫 / 雀跃 / 惬意）在对应表情子集里轮播，轮播速度也随心情变化。心情由 energy（工作降/空闲升）+ joy（done 加分/衰减）累积判定，并轻量持久化（重启大致恢复）。
- **thinking**：微眯眼 + 上方省略号 `···` 依次起跳
- **working**：按工具显示六种语义姿态（读/写/跑/搜/子代理/通用），底部 ticker 显示当前文件或命令
- **done**：闪光庆祝后自动回到 idle
- **WiFi 双模**：AP 热点常开 + STA 连家里 WiFi（凭据写入 Flash）

### 情绪引擎（idle 表情的"心情"）

设备内部维护 energy（工作时降、空闲时升）与 joy（每次 `done` 加分、随时间衰减）两个标量，据此判定当前心情，**只影响 idle**（其他状态各自的专属视觉不变）。判定优先级：joy≥50→雀跃；否则 energy≤30→疲惫；否则最近 3 分钟有活动→专注；否则→惬意。

| 心情  | 何时出现               | idle 轮播的表情子集            | 轮播节奏 |
| --- | ------------------ | ----------------------- | ---- |
| 专注  | 正常工作节奏（最近有活动）      | 普通 / 好奇 / 眨眼            | 中    |
| 疲惫  | 长时间连续干活（energy≤30） | 困倦 / 哈欠 / 转圈            | 慢    |
| 雀跃  | 刚连续完成几件事（joy≥50）   | 开心 / 星星眼 / 偷笑 / 听歌 / 惊讶 | 快    |
| 惬意  | 放松的默认态（长时间空闲）      | 爱心 / 花痴 / 偷笑 / 听歌 / 害羞  | 中    |

energy/joy 轻量持久化（最多每 5 分钟 + 会话结束时写 Flash），重启大致恢复"今天累不累"。速率/阈值见 `clawd_mood.ino` 顶部 `ENERGY_*` / `JOY_*` / `MOOD_*` 宏。14 种 idle 表情已全部编入心情子集（惊讶在雀跃、害羞在惬意）。

---

## 快速开始

### 1. 硬件接线

> VCC 必须接 **3.3V**，禁止 5V。

| 屏幕         | ESP32-C3 GPIO |
| ---------- | ------------- |
| VCC        | 3V3           |
| GND        | GND           |
| SDA (MOSI) | **GPIO 9**    |
| SCL (SCK)  | **GPIO 8**    |
| RES        | GPIO 2        |
| DC         | GPIO 1        |
| CS         | GPIO 4        |
| BL         | GPIO 3        |

### 2. 烧录固件

1. 安装 [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. 添加 ESP32 开发板支持（Espressif `esp32`）
3. 安装库：`Adafruit GFX Library`、`Adafruit ST7735 and ST7789 Library`
4. 开发板：**ESP32C3 Dev Module**，**USB CDC On Boot: Enabled**
5. 打开 `clawd_mood/clawd_mood.ino` 并上传

### 3. 配置 WiFi

1. 手机连热点 **`ClaWD-Mood`** / 密码 **`clawd1234`**
2. 浏览器打开 **`http://192.168.4.1`**
3. 在 **wifi setup** 填入家里 WiFi，保存
4. 连接成功后：门户页自动显示 PC 安装命令；设备屏幕显示局域网 IP，约 3 秒后进入表情界面

> **开机说明**：**连上**家里 WiFi 后，设备屏显示 IP 约 3 秒再进表情；**未连上**（没配过 / 旧凭据失效 / 换了网络环境）则信息屏（`VIEW_BOOTINFO`）常驻，并保持 AP 热点稳定，供你随时重新配网。换网络环境时无需任何额外操作——连不上会自动回到信息屏等你重配。

### 4. 配置 PC Hook（Cursor + Claude Code，一键安装）

在 `hook` 目录运行：

```powershell
cd hook
.\install-global.ps1
```

脚本会自动发现 `clawd.local`；也可手动指定 IP 或主机名：

```powershell
.\install-global.ps1 -DeviceIP 192.168.x.x
```

脚本会：

- 安装 Hook 到 `%USERPROFILE%\.clawd-mood\hook\`
- 写入 `device.json`（设备 IP / 主机名）
- 写入 Cursor 全局 `%USERPROFILE%\.cursor\hooks.json`
- **合并** Claude Code `%USERPROFILE%\.claude\settings.json` 中的 `hooks`（保留其他配置）

仅装 Cursor：`.\install-global.ps1 -SkipClaude`  
仅装 Claude Code：`.\install-global.ps1 -SkipCursor`

**重启 Cursor / Claude Code** 后生效。Cursor 需在 **Agent 模式**（非 Ask）发消息。

> IP 变更时：编辑 `%USERPROFILE%\.clawd-mood\hook\device.json`，或重跑 `install-global.ps1 -DeviceIP <新IP或clawd.local>`

手动配置说明见 [hook/README.md](hook/README.md) 或 [配置指南.md](配置指南.md)。

---

## Monitor 状态

| 状态         | 屏幕表现                                          | 典型触发                                                |
| ---------- | --------------------------------------------- | --------------------------------------------------- |
| `idle`     | 情绪引擎按心情（专注/疲惫/雀跃/惬意）轮播对应表情子集                  | 会话开始                                                |
| `thinking` | 微眯眼 + 省略号 `···` 依次起跳                          | 发送 Agent 消息                                         |
| `working`  | 按工具显示六种姿态（读/写/跑/搜/子代理/通用），底部 ticker 显示当前文件或命令 | 调用工具                                                |
| `done`     | 闪光庆祝 → 自动 idle                                | Agent 完成（`stop`）                                    |
| `alert`    | 警觉眼 + 上方 "!" 角标，无人处理逐级加急（温和→催促→告急描边）          | 需要确认/输入的通知（`Notification`/`Elicitation`）；普通工具失败不再触发 |
| `offline`  | 关背光                                           | 会话结束                                                |

`thinking` / `working` 若 30 秒无新事件，自动回到 `idle`。

表情由眼睛骨架引擎驱动：弹簧缓动与回弹、随机眨眼（12% 概率双连眨）、微扫视、呼吸起伏；表情切换时走眼睑过渡而非硬跳。working 的工具语义由 Hook 自动附带，URL 形如：

```
GET /status?s=working&act=read|edit|run|net|agent|work&info=<短文本>
```

---

## 多 AI 工具绑定（PC 控制台）

同时开多款 AI 编程工具（Cursor、Claude Code…）时，设备一次只响应你**绑定**的那一款，避免互相抢屏。由一个本地 Node 控制台管理：

```bash
node agent/clawd-agent.js          # 默认端口 6624,可用 CLAWD_AGENT_PORT 覆盖
```

浏览器打开 **`http://127.0.0.1:6624`**，单选当前要响应的工具即可。原理：

- hook 用 `CLAWD_SOURCE=<id>`（或 `--source=<id>`）声明来源；内置 Cursor / Claude Code 按事件名自动识别。
- 绑定写入 `~/.clawd-mood/agent.json`，hook 据此自我门控——非当前工具的事件静默忽略，**设备固件无需改动**。
- 未绑定（`activeTool` 为空）时不门控，行为与单工具时一致（向后兼容）。

新增一款工具：在控制台工具表加一条 + 给其 hook 设 `CLAWD_SOURCE`。详见 [agent/README.md](agent/README.md)。

---

## HTTP API

```
GET http://<设备IP>/status?s=idle|thinking|working|done|alert|offline   # Monitor 状态推送（hook 使用）
GET http://<设备IP>/status?s=working&act=read|edit|run|net|agent|work&info=<短文本>
GET http://192.168.4.1/wifi/save?ssid=名称&pass=密码                     # AP 模式下配网（配网页使用）
GET http://192.168.4.1/                                                  # 极简配网页
```

设备同时注册 `http://clawd.local`（mDNS），IP 变化后无需重新配置。

测试：

```powershell
Invoke-WebRequest -Uri "http://192.168.x.x/status?s=thinking" -UseBasicParsing
```

更多测试命令见 [hook/状态测试指令.md](hook/状态测试指令.md)。

---

## 项目结构

```
clawd-mood/
├── clawd_mood/
│   └── clawd_mood.ino          # 固件（单文件）
├── hook/
│   ├── clawd-hook.js            # Cursor / Claude Code → 设备状态
│   ├── install-global.ps1       # 一键安装全局 Hook
│   ├── device.json.example      # 设备 IP 模板（复制为 device.json,本地用）
│   ├── README.md                # Hook 详细说明
│   └── 状态测试指令.md          # 全状态测试命令
├── agent/
│   ├── clawd-agent.js           # PC 控制台：绑定当前响应哪款 AI 工具
│   ├── panel.html               # 控制台网页
│   └── README.md                # 控制台说明
├── web/
│   ├── idle-gallery.html        # 空闲表情画廊（rig 实时预览全部 idle 表情）
│   └── expression-editor/
│       └── index.html           # 浏览器表情编辑器
├── models/                      # 3D 外壳 STL / 3MF
├── pics/                        # 图片素材
├── 配置指南.md                  # 完整配置流程（中文）
└── README.md                    # 本文件
```

---

## 表情编辑器（Web）

在浏览器中自定义与固件一致的表情参数，实时预览 240×240 画面，导出 JSON / C++ 常量：

```
web/expression-editor/index.html
```

双击打开即可，无需服务器。

### 空闲表情画廊（Web）

`web/idle-gallery.html` 用与固件一致的 rig 引擎实时预览全部 14 个 idle 表情（弹簧缓动、眨眼、呼吸），可调速度，用于挑选与微调表情。双击打开即可。

---

## 文档

| 文档                               | 内容                                |
| -------------------------------- | --------------------------------- |
| [配置指南.md](配置指南.md)               | 从硬件到 Hook 的完整步骤                   |
| [hook/README.md](hook/README.md) | Hook 安装、Cursor / Claude Code 事件映射 |
| [hook/状态测试指令.md](hook/状态测试指令.md) | HTTP / Hook 全状态测试                 |

---

## 故障排查

| 现象                           | 处理                                                                 |
| ---------------------------- | ------------------------------------------------------------------ |
| Hook 无反应                     | 确认在 **Agent 模式**；Settings → Hooks 已启用；重启 Cursor                    |
| 设备不更新                        | 检查 `device.json` IP；PC 与设备同网段                                      |
| IP 变了                        | 改 `%USERPROFILE%\.clawd-mood\hook\device.json`                     |
| 30 秒后回 idle                  | 正常超时保护                                                             |
| `clawd.local` 解析到 198.18.x.x | 系统代理 fake-ip 劫持；hook 不受影响（内置 mDNS 直查），浏览器需在代理加 `*.local` 放行        |
| 开机停在信息屏                      | **未连上即正常常驻**（等你配网）；只有成功连上才会约 3 秒后进表情。停在信息屏说明没连上——检查 WiFi 是否可达或重新配网 |
| 换了网络后手机搜不到 AP                | 开机后等约 10 秒（STA 尝试结束）AP 即稳定；设备会自动停在信息屏，连 AP 重新配网即可                  |

---

## 外观与 3D 外壳

`models/` 提供两种外壳（普通眼 / 眯眼）的 STL 与 3MF（含 AMS 多色版），可直接打印；也可在 [MakerWorld](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot) 获取。

<p align="center">
  <img src="pics/clawd_3D_4_3.png" alt="3D 外壳" width="30%">
  <img src="pics/clawd_3D_squished_eyes_4_3.png" alt="眯眼版外壳" width="30%">
  <img src="pics/clawd_mochi_start.jpeg" alt="开机信息屏" width="30%">
</p>

---

## 致谢

- **[yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)** — 本项目的上游开源项目（硬件设计、3D 外壳、初版固件思路）。
- Clawd 形象与 Claude Code 来自 **Anthropic**。

## License

MIT License — 见 [LICENSE](LICENSE)。沿用上游 MIT 许可，保留原作者版权声明。
