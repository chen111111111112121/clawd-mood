# 新用户上手 & 开机信息屏重做 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让新用户"开机 → 手机填 WiFi → PC 一键脚本打通 AI agent"全程零摩擦，并把游离在状态机外的开机信息屏重做成正规视图。

**Architecture:** 两块独立可交付的改动。**Part A（固件）**：把信息屏做成 `VIEW_BOOTINFO`，用统一入口 `enterBootInfoView(autoLeaveMs)` 取代散落的 `bootScreenDeadline` 计时；未配置常驻、已连上 3s 确认后进表情，并修掉"配置后卡死在信息屏"的 bug，新增 `/cmd?k=i` 召回。**Part B（上手链路）**：安装脚本接受主机名 + 自动发现 `clawd.local`，hook 默认目标改为 `clawd.local`（mDNS+缓存已实现），门户页保存 WiFi 后把安装命令递给用户。

**Tech Stack:** ESP32-C3 Arduino 单文件固件（`clawd_mochi.ino`，无构建/测试框架，靠烧录 + HTTP 观察验证）；纯 Node hook（`node:test`）；PowerShell 安装脚本（手动 dry-run 验证）。

**验证现实说明：** 固件无单元测试框架——所有固件任务用"烧录后用 PowerShell `Invoke-WebRequest` 打 HTTP + 肉眼看屏"验证。hook 逻辑有 `node:test`，写真测试。PowerShell 脚本不跑改全局配置的真安装，用 `-WhatIf` 思路的只读片段或 `-SkipCursor -SkipClaude` 验证。

**Part A 与 Part B 相互独立，可分别交付。** 建议先做 A（纯固件、风险低、立即可验），再做 B。

---

## 文件结构

| 文件 | 职责 | 本计划改动 |
|---|---|---|
| `clawd-mochi/clawd_mochi/clawd_mochi.ino` | 固件全部逻辑 | Part A 全部；Part B 仅门户页 `INDEX_HTML` |
| `clawd-mochi/hook/clawd-hook.js` | 事件→状态推送，IP 解析 | Part B：默认目标改 `clawd.local` |
| `clawd-mochi/hook/install-global.ps1` | 一键安装到 Cursor + Claude Code | Part B：接受主机名 + 自动发现 |
| `clawd-mochi/hook/device.json.example` | 配置样例 | Part B：默认值改 `clawd.local` |
| `clawd-mochi/hook/test/semantics.test.js` | hook 单测 | Part B：补 `isIPv4('clawd.local')` 断言 |
| `clawd-mochi/README.md`、`配置指南.md` | 中文文档 | Part B：上手流程 + 视图/API 表 |

---

# PART A — 开机信息屏重做（固件）

### Task A1：新增 VIEW_BOOTINFO 与确认时长常量

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:69-73`（VIEW 定义区）
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:119`（BOOT_INFO_MS 附近）

- [ ] **Step 1: 加视图常量**

在 `:73` 的 `#define VIEW_MONITOR 4` 之后新增一行：

```cpp
#define VIEW_BOOTINFO    5
```

- [ ] **Step 2: 加确认时长常量**

在 `:119` 的 `#define BOOT_INFO_MS 10000` 之后新增：

```cpp
#define BOOT_CONFIRM_MS 3000   // 已连上家庭 WiFi:显示 IP 确认 3s 后进表情
```

- [ ] **Step 3: 编译验证**

Arduino IDE 选 **ESP32C3 Dev Module** / **USB CDC On Boot: Enabled**，点 Verify（只编译不烧录）。
Expected: 编译通过，无 `VIEW_BOOTINFO` / `BOOT_CONFIRM_MS` 未定义错误。

- [ ] **Step 4: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): add VIEW_BOOTINFO and BOOT_CONFIRM_MS constants"
```

---

### Task A2：drawWifiScreen footer 随场景变化 + 统一入口 enterBootInfoView

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:418-445`（drawWifiScreen 签名与 footer）
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:1594` 上方（enterMonitorView 之前插入新函数）

- [ ] **Step 1: 改 drawWifiScreen 签名与 footer**

把 `:418` 的 `void drawWifiScreen() {` 改为带参版本，并把 `:444` 的固定 footer（`"auto start in 10s ..."`）替换为三态 footer。完整替换 `:443-445`：

```cpp
  tft.setTextColor(C_MUTED); tft.setTextSize(1);
  tft.setCursor(12, 210);
  if (autoLeaveMs) {
    tft.print("auto start in "); tft.print(autoLeaveMs / 1000); tft.print("s ...");
  } else if (!staConnected) {
    tft.print("waiting for WiFi setup ...");
  } else {
    tft.print("press any view to start");
  }
}
```

并把函数签名行 `:418` 改为：

```cpp
void drawWifiScreen(uint32_t autoLeaveMs) {
```

- [ ] **Step 2: 新增统一入口 enterBootInfoView**

在 `:1594` 的 `void enterMonitorView() {` 之前插入：

```cpp
// 信息屏作为正规视图;autoLeaveMs=0 表示常驻,直到状态推送/手动切视图
void enterBootInfoView(uint32_t autoLeaveMs) {
  currentView = VIEW_BOOTINFO;
  termMode = false;
  bootScreenDeadline = autoLeaveMs ? (millis() + autoLeaveMs) : 0;
  drawWifiScreen(autoLeaveMs);
}
```

- [ ] **Step 3: 加前置声明（若需要）**

Arduino 的 .ino 通常自动生成原型；若 Step 5 编译报 `enterBootInfoView` 未声明，在文件顶部声明区（`bootScreenDeadline` 定义行 `:120` 之后）加：

```cpp
void enterBootInfoView(uint32_t autoLeaveMs);
void drawWifiScreen(uint32_t autoLeaveMs);
```

- [ ] **Step 4: 暂时让旧调用点编译通过**

此时 `:2336`、`:2418` 仍调用旧的 `drawWifiScreen()` 无参版本，会编译失败——下一任务统一替换。本任务先只验证函数体本身语法：把 `:2418` 临时改为 `drawWifiScreen(BOOT_INFO_MS);`、`:2336` 临时改为 `drawWifiScreen(BOOT_INFO_MS);`（Task A3 会再正式改写）。

- [ ] **Step 5: 编译验证**

Arduino IDE Verify。
Expected: 编译通过。

- [ ] **Step 6: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): drawWifiScreen scene-aware footer + enterBootInfoView entry"
```

---

### Task A3：setup() 三分支启动

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2418-2419`

- [ ] **Step 1: 替换开机信息屏调用**

把 `:2418-2419` 这两行：

```cpp
  drawWifiScreen();
  bootScreenDeadline = millis() + BOOT_INFO_MS;
```

替换为：

```cpp
  if (savedSSID.length() == 0) {
    enterBootInfoView(0);                  // 未配置:常驻直到配置完成
  } else if (staConnected) {
    enterBootInfoView(BOOT_CONFIRM_MS);    // 已连上:3s 确认 IP 后进表情
  } else {
    enterBootInfoView(BOOT_INFO_MS);       // 配了但还没连上:10s 兜底
  }
```

- [ ] **Step 2: 编译验证**

Arduino IDE Verify。
Expected: 通过。

- [ ] **Step 3: 烧录 + 验证未配置常驻**

擦除已存凭据后烧录（或首次烧录的新设备）。
Expected: 上电后信息屏显示 `not connected` / `waiting for WiFi setup ...`，**永久停留，不进表情**。

- [ ] **Step 4: 验证已配置 3s 确认**

设备已配过家庭 WiFi 时重启。
Expected: 信息屏显示绿色 IP + `auto start in 3s ...`，约 3 秒后切到表情屏。

- [ ] **Step 5: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): three-branch boot info screen by config/connection state"
```

---

### Task A4：loop() 离开守卫 + 连上后改确认计时

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2444-2447`（离开守卫）
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2458-2460`（STA 新连上分支）

- [ ] **Step 1: 守卫加 VIEW_BOOTINFO 条件**

把 `:2444-2447`：

```cpp
  if (bootScreenDeadline && millis() >= bootScreenDeadline) {
    bootScreenDeadline = 0;
    enterMonitorView();
  }
```

替换为：

```cpp
  if (currentView == VIEW_BOOTINFO && bootScreenDeadline && millis() >= bootScreenDeadline) {
    enterMonitorView();   // enterMonitorView 内部已置 bootScreenDeadline = 0
  }
```

- [ ] **Step 2: STA 后台连上时,若仍在信息屏则转 3s 确认**

在 `:2458-2460` 的 `if (!wasConnected) startMDNS();` 之后补一行（仍在该 `if (staConnected)` 分支内）：

```cpp
        if (!wasConnected) startMDNS();
        if (!wasConnected && currentView == VIEW_BOOTINFO) {
          enterBootInfoView(BOOT_CONFIRM_MS);   // 后台连上:刷新成绿色 IP + 3s 确认
        }
```

- [ ] **Step 3: 编译验证**

Arduino IDE Verify。
Expected: 通过。

- [ ] **Step 4: 烧录 + 验证兜底离开**

设备配了一个连不上的 SSID（或弱信号）重启，落入 10s 兜底分支。
Expected: 约 10 秒后信息屏自动切到表情屏（不再永久停留）。

- [ ] **Step 5: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): loop guards boot-info exit by view; rearm to confirm on late STA connect"
```

---

### Task A5：routeWifiSave 用 enterBootInfoView，修卡死 bug

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2336`

- [ ] **Step 1: 替换保存后重绘逻辑**

把 `:2336` 的 `drawWifiScreen();` 替换为：

```cpp
  if (staConnected) {
    enterBootInfoView(BOOT_CONFIRM_MS);   // 连上:显示 IP 3s 后自动进表情(修复永久卡死)
  } else {
    enterBootInfoView(0);                  // 没连上:常驻,让用户重试
  }
```

- [ ] **Step 2: 编译验证**

Arduino IDE Verify。
Expected: 通过。

- [ ] **Step 3: 烧录 + 验证保存后不卡死**

设备开机超过 10s（信息屏已自动离开、进了表情）后，手机连 AP 打开 `192.168.4.1`，填正确家庭 WiFi 点 Save。
Expected: 设备屏短暂显示绿色 IP，**约 3 秒后自动回到表情屏**（旧行为是永久卡在信息屏）。

- [ ] **Step 4: 验证保存失败留在原地**

填错误密码点 Save。
Expected: 信息屏显示 `not connected` 并常驻，便于重试。

- [ ] **Step 5: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "fix(fw): wifi save no longer stuck on info screen; auto-return after connect"
```

---

### Task A6：/cmd?k=i 召回信息屏

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2216-2219`（routeCmd switch 内）

- [ ] **Step 1: 加 'i' 分支**

在 `:2219` 的 `case 'a':` 块之后、`}` 之前插入：

```cpp
    case 'i':
      enterBootInfoView(0);   // 召回信息屏,常驻直到下次切视图
      break;
```

注意：`:2206` 的 `bootScreenDeadline = 0;` 在 switch 之前执行，对 `'i'` 无害——`enterBootInfoView(0)` 内部会重置该值。其余 case 把 `currentView` 切走，loop 守卫的 `currentView == VIEW_BOOTINFO` 条件不再成立，旧的散落 `bootScreenDeadline = 0` 现在是冗余但无害，保留不动以缩小改动面。

- [ ] **Step 2: 编译验证**

Arduino IDE Verify。
Expected: 通过。

- [ ] **Step 3: 烧录 + HTTP 验证召回**

设备处于表情屏时，PC 上执行（替换 `<IP>`）：

```powershell
Invoke-WebRequest -Uri "http://<IP>/cmd?k=i" -UseBasicParsing
```

Expected: 设备屏切回信息屏并常驻；再发 `/cmd?k=m` 可切回 Monitor 表情。

- [ ] **Step 4: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): /cmd?k=i recalls boot info screen on demand"
```

---

### Task A7：Web 控制器加 "ℹ device info" 按钮

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:1766-1768`（controls 区 HTML）
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:1929` 上方（views JS 区，加 showInfo 函数）

- [ ] **Step 1: 加按钮**

把 `:1766-1768` 的 controls 区：

```html
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
</div>
```

替换为：

```html
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
  <button class="cbtn" id="infoBtn" onclick="showInfo()">&#8505; device info</button>
</div>
```

- [ ] **Step 2: 加 showInfo JS**

在 `:1929` 的 `// ── Views` 注释之前插入：

```javascript
// ── Device info ─────────────────────────────────────────────────
async function showInfo() {
  if (await req('/cmd?k=i')) toast('device info on screen');
}
```

- [ ] **Step 3: 烧录 + 验证**

手机连 AP 开 `192.168.4.1`，点 `ℹ device info`。
Expected: toast 提示 `device info on screen`，设备屏切到信息屏。

- [ ] **Step 4: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): web controller device-info recall button"
```

---

# PART B — 一键上手链路

### Task B1：hook 默认目标改 clawd.local

**Files:**
- Modify: `clawd-mochi/hook/clawd-hook.js:124`
- Modify: `clawd-mochi/hook/device.json.example`
- Test: `clawd-mochi/hook/test/semantics.test.js`

- [ ] **Step 1: 写断言（确认 clawd.local 走 mDNS 路径）**

在 `clawd-mochi/hook/test/semantics.test.js` 末尾追加：

```javascript
// ── 默认主机名走 mDNS 路径 ─────────────────────────────────────
test('clawd.local 不是 IPv4,会进入 mDNS 解析分支', () => {
  assert.strictEqual(isIPv4('clawd.local'), false);
  assert.strictEqual(isIPv4('192.168.1.5'), true);
});
```

- [ ] **Step 2: 运行测试看通过**

Run（本机 `node --test` 需显式文件路径）:

```bash
node --test clawd-mochi/hook/test/semantics.test.js
```

Expected: 全部通过（`isIPv4` 早已导出，此断言应直接 PASS，作为防回归锚点）。

- [ ] **Step 3: 改默认目标**

把 `clawd-hook.js:124`：

```javascript
  return '192.168.150.21';
```

替换为：

```javascript
  return 'clawd.local';
```

- [ ] **Step 4: 改样例文件**

把 `clawd-mochi/hook/device.json.example` 内容设为：

```json
{ "device_ip": "clawd.local" }
```

- [ ] **Step 5: dry-run 验证默认目标生效**

临时移走本机配置后干跑（`CLAWD_DRY=1` 只打印 URL 不发请求）：

```bash
CLAWD_DEVICE_IP=clawd.local CLAWD_DRY=1 node clawd-mochi/hook/clawd-hook.js SessionStart
```

Expected: 打印形如 `http://<解析到的IP或clawd.local>/status?s=idle`（mDNS 能解析则为 IP，否则原样 `clawd.local`）。

- [ ] **Step 6: Commit**

```bash
git add clawd-mochi/hook/clawd-hook.js clawd-mochi/hook/device.json.example clawd-mochi/hook/test/semantics.test.js
git commit -m "feat(hook): default device target to clawd.local (mDNS + cache already in place)"
```

---

### Task B2：安装脚本接受主机名 + 自动发现 clawd.local

**Files:**
- Modify: `clawd-mochi/hook/install-global.ps1:150-165`

- [ ] **Step 1: 无参时自动发现 clawd.local**

把 `:150-159` 的 `if (-not $DeviceIP) { ... }` 整块替换为：

```powershell
if (-not $DeviceIP) {
    $existing = Read-ExistingDeviceIP
    if ($existing) {
        $prompt = "Mochi device IP/host [detected: $existing]"
        $DeviceIP = Read-Host $prompt
        if (-not $DeviceIP) { $DeviceIP = $existing }
    } else {
        # 自动发现:试 clawd.local /state
        try {
            $probe = Invoke-WebRequest -Uri "http://clawd.local/state" -UseBasicParsing -TimeoutSec 3
            if ($probe.StatusCode -eq 200) {
                Write-Host "Auto-discovered device at clawd.local" -ForegroundColor Green
                $DeviceIP = "clawd.local"
            }
        } catch { }
        if (-not $DeviceIP) {
            $DeviceIP = Read-Host "Mochi device IP/host (e.g. 192.168.150.21 or clawd.local; see device screen)"
        }
    }
}
```

- [ ] **Step 2: 校验改为接受 IP 或主机名**

把 `:161-165`：

```powershell
$DeviceIP = $DeviceIP.Trim()
$ipPattern = '^\d{1,3}(\.\d{1,3}){3}$'
if ($DeviceIP -notmatch $ipPattern) {
    throw "Invalid IP: $DeviceIP"
}
```

替换为：

```powershell
$DeviceIP = $DeviceIP.Trim()
$ipPattern   = '^\d{1,3}(\.\d{1,3}){3}$'
$hostPattern = '^[A-Za-z0-9][A-Za-z0-9.\-]*$'
if ($DeviceIP -notmatch $ipPattern -and $DeviceIP -notmatch $hostPattern) {
    throw "Invalid IP/host: $DeviceIP"
}
```

- [ ] **Step 3: 验证接受 clawd.local（不改全局配置）**

跳过两边写入，只验证不被校验拒绝：

```powershell
powershell -File clawd-mochi/hook/install-global.ps1 -DeviceIP clawd.local -SkipCursor -SkipClaude
```

Expected: 不抛 `Invalid IP/host`；末尾 `Device IP : clawd.local`（设备连通测试通过与否取决于当前网络/mDNS，不影响校验本身）。

- [ ] **Step 4: 验证 IPv4 仍可用**

```powershell
powershell -File clawd-mochi/hook/install-global.ps1 -DeviceIP 192.168.1.50 -SkipCursor -SkipClaude
```

Expected: 同样通过校验。

- [ ] **Step 5: Commit**

```bash
git add clawd-mochi/hook/install-global.ps1
git commit -m "feat(installer): accept hostname + auto-discover clawd.local when no IP given"
```

---

### Task B3：门户页保存 WiFi 后递出安装命令

**Files:**
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:1807-1814`（wifi 区 HTML）
- Modify: `clawd-mochi/clawd_mochi/clawd_mochi.ino:2019-2021`（saveWifi 成功分支 JS）

- [ ] **Step 1: 加安装提示块 HTML**

把 `:1813` 的 `<div class="wstat" id="wifiStat">AP always on: ClaWD-Mochi / clawd1234</div>` 之后、`</div>`（`:1814`）之前插入：

```html
  <div class="winstall" id="winstall" style="display:none;flex-direction:column;gap:6px;margin-top:8px">
    <span class="wlbl">ON YOUR PC — run in the repo's hook/ folder</span>
    <code id="wcmd" style="display:block;background:#1c1b1f;border:1px solid #38343a;border-radius:6px;padding:8px;font-size:11px;color:#d8d4cc;word-break:break-all"></code>
    <button class="wgo" onclick="copyCmd()">Copy command</button>
    <span class="wstat">Tip: <code>.\install-global.ps1</code> with no IP also auto-finds the device.</span>
  </div>
```

- [ ] **Step 2: 成功分支填充并显示命令**

把 `:2019-2021` 的成功分支：

```javascript
    if (j.ok && j.sta) {
      document.getElementById('wifiStat').textContent = 'Connected: ' + j.sta_ip;
      toast('WiFi connected');
```

替换为：

```javascript
    if (j.ok && j.sta) {
      document.getElementById('wifiStat').textContent = 'Connected: ' + j.sta_ip;
      document.getElementById('wcmd').textContent =
        '.\\install-global.ps1 -DeviceIP ' + j.sta_ip;
      document.getElementById('winstall').style.display = 'flex';
      toast('WiFi connected');
```

- [ ] **Step 3: 加 copyCmd（非安全上下文兜底）**

在 `:2009` 的 `// ── WiFi config` 注释之后、`saveWifi` 之前插入：

```javascript
function copyCmd() {
  const txt = document.getElementById('wcmd').textContent;
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(txt).then(() => toast('copied'), () => toast('copy failed', false));
    return;
  }
  // http 局域网非安全上下文:execCommand 兜底
  const ta = document.createElement('textarea');
  ta.value = txt; ta.style.position = 'fixed'; ta.style.opacity = '0';
  document.body.appendChild(ta); ta.focus(); ta.select();
  try { document.execCommand('copy'); toast('copied'); }
  catch (e) { toast('long-press to copy', false); }
  document.body.removeChild(ta);
}
```

- [ ] **Step 4: 烧录 + 验证**

手机连 AP 开 `192.168.4.1`，填正确家庭 WiFi 点 Save。
Expected: `Connected: 192.168.x.x` 下方出现命令块 `.\install-global.ps1 -DeviceIP 192.168.x.x`，点 Copy command 提示 `copied`（或长按可选中）。

- [ ] **Step 5: Commit**

```bash
git add clawd-mochi/clawd_mochi/clawd_mochi.ino
git commit -m "feat(fw): portal hands the install command after wifi save"
```

---

### Task B4：文档同步

**Files:**
- Modify: `clawd-mochi/README.md`
- Modify: `clawd-mochi/配置指南.md`

- [ ] **Step 1: README 上手流程**

在 README 的快速开始/上手章节，加入端到端流程（按实际章节标题就近插入）：

```markdown
## 快速上手

1. 烧录固件（Arduino IDE：ESP32C3 Dev Module / USB CDC On Boot: Enabled）
2. 上电：屏幕显示 AP 名 `ClaWD-Mochi` 与 `192.168.4.1`，未配置时常驻
3. 手机连 AP（密码 `clawd1234`）→ 浏览器开 `192.168.4.1` → 填家庭 WiFi → Save
4. 门户页给出 PC 安装命令；屏幕也会显示设备 IP，3 秒后进表情
5. PC 上在 `hook/` 目录运行 `.\install-global.ps1`（不带参会自动发现 `clawd.local`，
   或用门户页给的 `-DeviceIP <ip>`）→ 自动装好 Claude Code + Cursor 的 hook
6. 新开会话即联动；想再看 IP 点 Web 控制器的 `ℹ device info` 或访问 `/cmd?k=i`
```

- [ ] **Step 2: 同步视图表与 API 表**

在 README / 配置指南中相应表格补两行：

```markdown
| 视图 | VIEW_BOOTINFO | 开机信息屏（WiFi/IP），未配置常驻、已配置确认后进表情 |
| API  | `/cmd?k=i`    | 召回开机信息屏 |
```

- [ ] **Step 3: Commit**

```bash
git add clawd-mochi/README.md clawd-mochi/配置指南.md
git commit -m "docs: onboarding flow, VIEW_BOOTINFO and /cmd?k=i in tables"
```

---

## Self-Review 结论

- **Spec 覆盖**：开机三态分流（A3）、未配置常驻（A3 Step3）、已连上 3s 确认（A3/A4/A5）、修保存卡死 bug（A5）、信息屏召回（A6/A7）、安装脚本主机名+自动发现（B2）、hook 默认 clawd.local（B1）、门户递命令（B3）、文档（B4）——逐条有任务对应。
- **类型一致**：`enterBootInfoView(uint32_t)`、`drawWifiScreen(uint32_t)` 在 A2 定义，A3/A5/A6 调用签名一致；`VIEW_BOOTINFO`/`BOOT_CONFIRM_MS` 在 A1 定义后使用。
- **无占位符**：每个代码步骤均给出完整可粘贴代码与精确行号/命令。
- **已知约束**：门户在手机查看而命令在 PC 运行——B3 的"复制"在手机上价值有限，真正的"一键"是 B2 的 PC 端 `clawd.local` 自动发现；门户命令块 + 设备屏显示 IP 是兜底，已在文档（B4 Step1 第 5 点）说明两条路径。
