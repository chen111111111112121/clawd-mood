# 语义监测 + 表情灵动化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 设备按 Claude 正在用的工具切换表情、底部 ticker 显示当前文件/命令,且全部 Monitor 表情迁移到带弹簧缓动、随机眨眼、呼吸起伏的眼睛骨架(rig)引擎。

**Architecture:** hook 端从 stdin JSON 提取 `tool_name`/`tool_input` 归类为 act/info 追加到 `/status`;固件新增 rig 引擎(目标姿态 + 整数弹簧插值 + 生命噪声层)替代逐表情 `tick*` 函数;ticker 占屏幕 216–240px 独立绘制。协议向后兼容。

**Tech Stack:** Node.js(node:test 内置测试,Node ≥18,本机 v24)、Arduino C++(ESP32-C3 + Adafruit GFX/ST7789,单文件 `.ino`)。

**规格:** `docs/superpowers/specs/2026-06-12-semantic-monitor-design.md`

**验证环境说明:**
- Hook 任务可全自动验证(`node --test`)。
- 固件任务的编译/烧录/目测步骤标记为 **[用户检查点]**:本机无 arduino-cli,需在 Arduino IDE 点 Verify/Upload(板型 ESP32C3 Dev Module,USB CDC On Boot: Enabled)。代码编辑与提交可由执行者完成,检查点等用户确认后再继续。
- 设备联测命令默认设备 IP 写作 `$IP`,实际值见 `hook/device.json`。

---

## Task 1: Hook 语义提取函数(TDD)

**Files:**
- Modify: `hook/clawd-hook.js`
- Create: `hook/test/semantics.test.js`

- [ ] **Step 1: 写失败测试**

创建 `hook/test/semantics.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert');
const {
  classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics,
} = require('../clawd-hook.js');

// ── classifyTool ──────────────────────────────────────────────
test('Edit -> act=edit, info=basename (Windows path)', () => {
  assert.deepStrictEqual(
    classifyTool('Edit', { file_path: 'D:\\proj\\src\\main.cpp' }),
    { act: 'edit', info: 'main.cpp' });
});

test('Write/NotebookEdit 也归 edit', () => {
  assert.strictEqual(classifyTool('Write', { file_path: 'a/b.md' }).act, 'edit');
  assert.strictEqual(classifyTool('NotebookEdit', { notebook_path: 'n.ipynb' }).act, 'edit');
});

test('Read -> act=read, info=basename', () => {
  assert.deepStrictEqual(
    classifyTool('Read', { file_path: '/home/u/x/notes.md' }),
    { act: 'read', info: 'notes.md' });
});

test('Grep 无 file_path 时用 pattern', () => {
  assert.deepStrictEqual(
    classifyTool('Grep', { pattern: 'TODO' }),
    { act: 'read', info: 'TODO' });
});

test('Bash -> act=run, info=命令首词', () => {
  assert.deepStrictEqual(
    classifyTool('Bash', { command: '  git log --oneline -5' }),
    { act: 'run', info: 'git' });
});

test('WebFetch -> act=net, info=域名', () => {
  assert.deepStrictEqual(
    classifyTool('WebFetch', { url: 'https://docs.arduino.cc/x/y' }),
    { act: 'net', info: 'docs.arduino.cc' });
});

test('WebSearch -> act=net, info=query', () => {
  assert.deepStrictEqual(
    classifyTool('WebSearch', { query: 'esp32 spring animation' }),
    { act: 'net', info: 'esp32 spring animation' });
});

test('Task -> act=agent, info=description', () => {
  assert.deepStrictEqual(
    classifyTool('Task', { description: 'explore codebase' }),
    { act: 'agent', info: 'explore codebase' });
});

test('未知工具 -> act=work, info=工具名', () => {
  assert.deepStrictEqual(
    classifyTool('SomeNewTool', {}),
    { act: 'work', info: 'SomeNewTool' });
});

test('缺 tool_name/tool_input 不抛异常', () => {
  assert.strictEqual(classifyTool(undefined, undefined).act, 'work');
});

// ── sanitizeInfo ──────────────────────────────────────────────
test('sanitize: 中文剔除只剩扩展名', () => {
  assert.strictEqual(sanitizeInfo('工程分析.md'), '.md');
});

test('sanitize: 超长截断 22 字符', () => {
  assert.strictEqual(sanitizeInfo('a'.repeat(40)).length, 22);
});

test('sanitize: 控制字符剔除、首尾空白去除', () => {
  assert.strictEqual(sanitizeInfo('  a\tb\nc  '), 'abc');
});

test('sanitize: 非字符串返回空串', () => {
  assert.strictEqual(sanitizeInfo(null), '');
});

// ── resolveSemantics ──────────────────────────────────────────
test('resolveSemantics: 纯中文 info 退化为 tool_name', () => {
  assert.deepStrictEqual(
    resolveSemantics({ tool_name: 'Read', tool_input: { file_path: '工程分析' } }),
    { act: 'read', info: 'Read' });
});

test('resolveSemantics: payload 为 null 返回 null', () => {
  assert.strictEqual(resolveSemantics(null), null);
});

// ── buildStatusUrl ────────────────────────────────────────────
test('working + 语义 -> 带 act/info 且 URL 编码', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'working', { act: 'edit', info: 'a b.cpp' }),
    'http://1.2.3.4/status?s=working&act=edit&info=a%20b.cpp');
});

test('非 working 状态忽略语义', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'thinking', { act: 'edit', info: 'x' }),
    'http://1.2.3.4/status?s=thinking');
});

test('info 为空时省略 info 参数', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'working', { act: 'run', info: '' }),
    'http://1.2.3.4/status?s=working&act=run');
});
```

- [ ] **Step 2: 运行确认失败**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi" && node --test hook/test/
```
期望:**FAIL**,报错形如 `classifyTool is not a function`(clawd-hook.js 目前没有导出)。

> 跑单个测试:`node --test --test-name-pattern="Bash" hook/test/`

- [ ] **Step 3: 实现**

在 `hook/clawd-hook.js` 的 `STATE_MAP` 定义之后、`getDeviceIP()` 之前插入:

```js
// ── Tool semantics (state=working 时随 /status 附带) ─────────
const READ_TOOLS = new Set(['Read', 'Glob', 'Grep', 'NotebookRead']);
const EDIT_TOOLS = new Set(['Edit', 'Write', 'MultiEdit', 'NotebookEdit']);

function basename(p) {
  if (typeof p !== 'string' || !p) return '';
  const parts = p.replace(/\\/g, '/').split('/');
  return parts[parts.length - 1] || '';
}

function classifyTool(toolName, toolInput) {
  const name = typeof toolName === 'string' ? toolName : '';
  const input = (toolInput && typeof toolInput === 'object') ? toolInput : {};
  if (READ_TOOLS.has(name)) {
    return { act: 'read', info: basename(input.file_path) || (typeof input.pattern === 'string' ? input.pattern : '') };
  }
  if (EDIT_TOOLS.has(name)) {
    return { act: 'edit', info: basename(input.file_path) || basename(input.notebook_path) };
  }
  if (name === 'Bash') {
    const cmd = typeof input.command === 'string' ? input.command.trim() : '';
    return { act: 'run', info: cmd.split(/\s+/)[0] || '' };
  }
  if (name === 'WebFetch' || name === 'WebSearch') {
    let info = '';
    if (typeof input.url === 'string') {
      try { info = new URL(input.url).hostname; } catch (_) { info = input.url; }
    } else if (typeof input.query === 'string') {
      info = input.query;
    }
    return { act: 'net', info };
  }
  if (name === 'Task' || name === 'Agent') {
    return { act: 'agent', info: typeof input.description === 'string' ? input.description : '' };
  }
  return { act: 'work', info: name };
}

function sanitizeInfo(s) {
  if (typeof s !== 'string') return '';
  let out = '';
  for (const ch of s) {
    const code = ch.codePointAt(0);
    if (code >= 0x20 && code <= 0x7e) out += ch;
  }
  return out.trim().slice(0, 22);
}

function resolveSemantics(payload) {
  if (!payload || typeof payload !== 'object') return null;
  const { act, info } = classifyTool(payload.tool_name, payload.tool_input);
  const clean = sanitizeInfo(info) || sanitizeInfo(payload.tool_name || '');
  return { act, info: clean };
}

function buildStatusUrl(ip, state, semantics) {
  let url = `http://${ip}/status?s=${encodeURIComponent(state)}`;
  if (state === 'working' && semantics) {
    url += `&act=${encodeURIComponent(semantics.act)}`;
    if (semantics.info) url += `&info=${encodeURIComponent(semantics.info)}`;
  }
  return url;
}
```

在文件末尾,把现有的:

```js
main().catch(() => process.exit(0));
```

替换为:

```js
if (require.main === module) {
  main().catch(() => process.exit(0));
} else {
  module.exports = { classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics, STATE_MAP };
}
```

- [ ] **Step 4: 运行确认通过**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi" && node --test hook/test/
```
期望:**全部 PASS**(19 tests)。

- [ ] **Step 5: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add hook/clawd-hook.js hook/test/semantics.test.js
git commit -m "✨ feat(hook): 工具语义提取 — classify/sanitize/buildStatusUrl + 单测"
```

---

## Task 2: Hook 集成 — 单次解析 payload、URL 组装、CLAWD_DRY

**Files:**
- Modify: `hook/clawd-hook.js`(`readStdin` 之后的 `resolveEvent`、`pushState`、`main`)

- [ ] **Step 1: 重构 main 流程**

删除现有 `resolveEvent` 函数,把现有 `main` 替换为:

```js
async function main() {
  const stdinText = await readStdin();
  let payload = null;
  const text = (stdinText || '').trim();
  if (text) {
    try { payload = JSON.parse(text); } catch (_) {}
  }
  const event = (payload && payload.hook_event_name) || process.argv[2];
  writeCursorStdout(event);
  const state = STATE_MAP[event];
  if (!state) process.exit(0);
  const semantics = state === 'working' ? resolveSemantics(payload) : null;
  await pushState(state, semantics);
  process.exit(0);
}
```

- [ ] **Step 2: pushState 接语义 + DRY 模式**

把现有 `pushState` 替换为:

```js
function pushState(state, semantics) {
  const url = buildStatusUrl(getDeviceIP(), state, semantics);
  if (process.env.CLAWD_DRY === '1') {
    process.stdout.write(url + '\n');
    return Promise.resolve(true);
  }
  return new Promise((resolve) => {
    const req = http.get(url, (res) => {
      res.resume();
      resolve(true);
    });
    req.on('error', () => resolve(false));
    req.setTimeout(TIMEOUT_MS, () => {
      req.destroy();
      resolve(false);
    });
  });
}
```

- [ ] **Step 3: 回归单测**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi" && node --test hook/test/
```
期望:全部 PASS。

- [ ] **Step 4: CLI 级验证(DRY,不打设备)**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi/hook"
echo '{"hook_event_name":"PreToolUse","tool_name":"Edit","tool_input":{"file_path":"a/b/main.cpp"}}' | CLAWD_DEVICE_IP=1.2.3.4 CLAWD_DRY=1 node clawd-hook.js
```
期望输出:`http://1.2.3.4/status?s=working&act=edit&info=main.cpp`

```bash
echo '{"hook_event_name":"UserPromptSubmit"}' | CLAWD_DEVICE_IP=1.2.3.4 CLAWD_DRY=1 node clawd-hook.js
```
期望输出:`http://1.2.3.4/status?s=thinking`(无 act/info)

```bash
echo '{"hook_event_name":"preToolUse","tool_name":"Bash","tool_input":{"command":"npm run build"}}' | CLAWD_DEVICE_IP=1.2.3.4 CLAWD_DRY=1 node clawd-hook.js
```
期望输出(Cursor camelCase 同样工作):`http://1.2.3.4/status?s=working&act=run&info=npm`

```bash
node clawd-hook.js SessionStart < /dev/null | head -1
```
期望:无 DRY 时静默(有设备则设备切 idle;无设备 2.5s 超时静默退出,退出码 0)。

- [ ] **Step 5: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add hook/clawd-hook.js
git commit -m "✨ feat(hook): /status 附带 act/info + CLAWD_DRY 调试模式"
```

---

## Task 3: Hook 文档

**Files:**
- Modify: `hook/README.md`(「状态映射」表之后)
- Modify: `hook/状态测试指令.md`(文件末尾)

- [ ] **Step 1: hook/README.md 增加语义映射节**

在「状态映射」表格之后插入:

```markdown
## 工具语义(act/info)

`PreToolUse` / `PostToolUse` 事件会额外附带工具语义:

```
GET /status?s=working&act=<act>&info=<短文本>
```

| 工具 | act | info |
| ---- | --- | ---- |
| Read / Glob / Grep | `read` | 文件名或搜索词 |
| Edit / Write / NotebookEdit | `edit` | 文件名 |
| Bash | `run` | 命令首词 |
| WebFetch / WebSearch | `net` | 域名 / 查询词 |
| Task(子代理) | `agent` | 任务描述 |
| 其他 | `work` | 工具名 |

info 仅保留可打印 ASCII、截断 22 字符;老固件忽略这些参数。

### 调试

`CLAWD_DRY=1` 时只打印将要请求的 URL,不发 HTTP:

```bash
echo '{"hook_event_name":"PreToolUse","tool_name":"Edit","tool_input":{"file_path":"x/main.cpp"}}' | CLAWD_DRY=1 node clawd-hook.js
```

运行单测:`node --test hook/test/`
```

- [ ] **Step 2: 状态测试指令.md 增加 hook 语义测试节**

文件末尾追加:

```markdown
## 工具语义测试(CLAWD_DRY,不需设备)

```bash
cd hook
echo '{"hook_event_name":"PreToolUse","tool_name":"Read","tool_input":{"file_path":"a/b.md"}}'   | CLAWD_DRY=1 node clawd-hook.js   # act=read&info=b.md
echo '{"hook_event_name":"PreToolUse","tool_name":"Bash","tool_input":{"command":"git st"}}'      | CLAWD_DRY=1 node clawd-hook.js   # act=run&info=git
echo '{"hook_event_name":"PreToolUse","tool_name":"WebSearch","tool_input":{"query":"esp32"}}'    | CLAWD_DRY=1 node clawd-hook.js   # act=net&info=esp32
echo '{"hook_event_name":"PreToolUse","tool_name":"Task","tool_input":{"description":"explore"}}' | CLAWD_DRY=1 node clawd-hook.js   # act=agent&info=explore
```
```

- [ ] **Step 3: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add hook/README.md "hook/状态测试指令.md"
git commit -m "📝 docs(hook): act/info 语义映射与 CLAWD_DRY 测试说明"
```

---

## Task 4: 固件 — rig 引擎(纯新增,先不接线)

**Files:**
- Modify: `clawd_mochi/clawd_mochi.ino`

本任务只**新增**代码,不改任何现有函数 —— 编译通过即可提交,行为零变化。

- [ ] **Step 1: 状态常量与全局(workAct / ticker 文本)**

在 `#define IDLE_POOL_SIZE 4`(约 90 行)之后插入:

```cpp
// ── Working sub-acts (semantic working states) ───────────────
#define ACT_WORK  0
#define ACT_READ  1
#define ACT_EDIT  2
#define ACT_RUN   3
#define ACT_NET   4
#define ACT_AGENT 5

uint8_t workAct = ACT_WORK;
char    tickerText[32] = "";
```

- [ ] **Step 2: rig 数据结构与常量**

在 `uint16_t drawBgColor = 0;`(约 122 行)之后、Terminal 段之前插入:

```cpp
// ── Eye rig: lively expression engine ────────────────────────
enum EyeStyle : uint8_t { STYLE_RECT, STYLE_CHEVRON, STYLE_ARC, STYLE_HEART };

struct EyePose {
  EyeStyle style;
  int16_t  ox, oy;   // 眼对偏移
  int16_t  w, h;     // RECT:眼宽高 | CHEVRON:w=reach,h=2*arm | ARC:w=弧宽 | HEART:w=像素 scale
  uint8_t  lid;      // 眼睑 0=全开 .. 240=全闭
};

#define RIG_BLINK    0x01
#define RIG_SACCADE  0x02
#define RIG_BREATH   0x04
#define RIG_BREATH2  0x08   // 双倍呼吸幅度(困倦)

struct Spring { int32_t cur, vel; };   // 8.8 定点

struct EyeRig {
  EyePose  pose;            // 目标姿态
  uint8_t  flags;
  EyeStyle drawnStyle;      // 当前屏上样式
  Spring   ox, oy, w, h, lid;
  unsigned long nextBlinkMs, nextSaccadeMs;
  uint8_t  blinkFrame;      // 0=未眨眼,1..BLINK_FRAMES=进行中
  bool     blinkAgain;      // 双连眨待续
  int16_t  sacX;
  uint16_t breathPhase;     // 8.8 相位
  EyeRect  prevL, prevR;
  bool     prevValid;
  bool     zoneDirty;       // 需整区清屏(样式切换等)
  uint8_t  trans;           // 样式过渡 0=无 1=闭眼中 2=睁眼中
  EyePose  transNext;
  uint8_t  transFlagsNext;
};

EyeRig rig;
bool rigZoneCleared = false;   // 本帧是否清过表情区(供覆盖层重绘判断)

#define RIG_TICK_MS  33
#define RIG_DAMP     196       // 速度阻尼(/256)
#define BLINK_FRAMES 7
static const uint8_t BLINK_H_PCT[BLINK_FRAMES] = {106, 60, 8, 8, 70, 104, 100};
static const uint8_t BLINK_W_PCT[BLINK_FRAMES] = { 97, 105, 118, 118, 103, 98, 100};
static const int8_t  BREATH_TAB[16] = {0,1,1,2,2,2,1,1,0,-1,-1,-2,-2,-2,-1,-1};

// 姿态预设
const EyePose POSE_NORMAL = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
const EyePose POSE_SLEEPY = {STYLE_RECT,    0,  8, EYE_W, EYE_H, 170};
const EyePose POSE_HEART  = {STYLE_HEART,   0,  0, 6, 6, 0};
const EyePose POSE_HAPPY  = {STYLE_ARC,     0,  8, 30, 30, 0};
const EyePose POSE_THINK  = {STYLE_CHEVRON, 0,  0, EYE_W / 2, EYE_H, 0};
const EyePose POSE_SCAN   = {STYLE_RECT,    0,  0, SCAN_EYE_W, EYE_H, 0};
const EyePose POSE_READ   = {STYLE_RECT,    0, 26, 26, 16, 0};
const EyePose POSE_EDIT   = {STYLE_RECT,    0,  0, 22, 38, 50};
const EyePose POSE_RUN    = {STYLE_RECT,    0,  0, 14, 44, 0};
const EyePose POSE_NET    = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
```

- [ ] **Step 3: rig 运算与渲染函数**

在 `drawSparkles` 函数(约 612–624 行)之后插入:

```cpp
// ═════════════════════════════════════════════════════════════
//  EYE RIG ENGINE
// ═════════════════════════════════════════════════════════════

int16_t rigK() {               // 弹簧刚度(/256),随 animSpeed
  if (animSpeed == 3) return 72;
  if (animSpeed == 1) return 30;
  return 48;
}

void springTo(Spring &s, int32_t target) {   // target 为 8.8
  s.vel += ((target - s.cur) * rigK()) >> 8;
  s.vel  = (s.vel * RIG_DAMP) >> 8;
  s.cur += s.vel;
}

void springSnap(Spring &s, int32_t target) { s.cur = target; s.vel = 0; }

inline int16_t rigLCX(int16_t ox) { return eyeLX(ox) + EYE_W / 2; }
inline int16_t rigRCX(int16_t ox) { return eyeRX(ox) + EYE_W / 2; }

void rigInvalidate() {
  rig.prevValid = false;
  rig.zoneDirty = true;
}

void rigSnapPose(const EyePose &p, uint8_t flags) {
  rig.pose = p; rig.flags = flags; rig.trans = 0;
  springSnap(rig.ox,  (int32_t)p.ox  << 8);
  springSnap(rig.oy,  (int32_t)p.oy  << 8);
  springSnap(rig.w,   (int32_t)p.w   << 8);
  springSnap(rig.h,   (int32_t)p.h   << 8);
  springSnap(rig.lid, (int32_t)p.lid << 8);
  rig.drawnStyle = p.style;
  rig.blinkFrame = 0;
  rigInvalidate();
}

void rigSetPose(const EyePose &p, uint8_t flags) {
  if (p.style != rig.drawnStyle) {
    rig.trans = 1;                 // 闭眼→换样式→睁眼(过渡中再调用则更新目标)
    rig.transNext = p;
    rig.transFlagsNext = flags;
    return;
  }
  if (rig.trans == 1) rig.trans = 0;   // 过渡中改回同样式:取消闭眼
  rig.pose = p;
  rig.flags = flags;
}

int16_t rigBreathOffset() {
  if (!(rig.flags & (RIG_BREATH | RIG_BREATH2))) return 0;
  int16_t v = BREATH_TAB[(rig.breathPhase >> 8) & 15];
  if (rig.flags & RIG_BREATH2) v *= 2;
  return v;
}

void rigTick(unsigned long now) {
  // 样式过渡
  if (rig.trans == 1) {
    springTo(rig.lid, (int32_t)240 << 8);
    if ((rig.lid.cur >> 8) >= 225) {
      rig.pose  = rig.transNext;
      rig.flags = rig.transFlagsNext;
      springSnap(rig.ox, (int32_t)rig.pose.ox << 8);
      springSnap(rig.oy, (int32_t)rig.pose.oy << 8);
      springSnap(rig.w,  (int32_t)rig.pose.w  << 8);
      springSnap(rig.h,  (int32_t)rig.pose.h  << 8);
      rig.drawnStyle = rig.pose.style;
      rig.zoneDirty = true;
      rig.trans = 2;
    }
  } else {
    if (rig.trans == 2 && (rig.lid.cur >> 8) <= rig.pose.lid + 12) rig.trans = 0;
    springTo(rig.lid, (int32_t)rig.pose.lid << 8);
  }

  // 微扫视
  if ((rig.flags & RIG_SACCADE) && rig.trans == 0) {
    if (now >= rig.nextSaccadeMs) {
      rig.sacX = (int16_t)random(-3, 4);
      rig.nextSaccadeMs = now + 700 + random(1300);
    }
  } else {
    rig.sacX = 0;
  }

  springTo(rig.ox, ((int32_t)(rig.pose.ox + rig.sacX)) << 8);
  springTo(rig.oy, (int32_t)rig.pose.oy << 8);
  springTo(rig.w,  (int32_t)rig.pose.w  << 8);
  springTo(rig.h,  (int32_t)rig.pose.h  << 8);

  // 眨眼
  if (rig.blinkFrame > 0) {
    rig.blinkFrame++;
    if (rig.blinkFrame > BLINK_FRAMES) {
      if (rig.blinkAgain) { rig.blinkFrame = 1; rig.blinkAgain = false; }
      else rig.blinkFrame = 0;
    }
  } else if ((rig.flags & RIG_BLINK) && rig.trans == 0 && now >= rig.nextBlinkMs) {
    rig.blinkFrame = 1;
    rig.blinkAgain = (random(100) < 12);
    rig.nextBlinkMs = now + 2500 + random(3500);
  }

  // 呼吸
  if (rig.flags & (RIG_BREATH | RIG_BREATH2)) {
    rig.breathPhase += (animSpeed == 1) ? 26 : ((animSpeed == 3) ? 60 : 40);
  }
}

void drawRigEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t lid,
                bool rightFacing, EyeRect &out) {
  switch (rig.drawnStyle) {
    case STYLE_CHEVRON: {
      const int16_t arm = ((h / 2) * (240 - lid)) / 240;
      if (arm <= 3) {
        tft.fillRect(cx - w / 2, cy - 4, w, 8, C_BLACK);
        out = {(int16_t)(cx - w / 2), (int16_t)(cy - 4), w, 8, true};
      } else {
        drawChevron(cx, cy, arm, w, 10, rightFacing, C_BLACK);
        out = {(int16_t)(cx - w / 2 - 2), (int16_t)(cy - arm - 12),
               (int16_t)(w + 4), (int16_t)(arm * 2 + 24), true};
      }
      break;
    }
    case STYLE_ARC:
      drawHappyArc(cx, cy, w, C_BLACK);
      out = {(int16_t)(cx - w / 2), (int16_t)(cy - w / 4 - 2),
             (int16_t)(w + 2), (int16_t)(w / 4 + 8), true};
      break;
    case STYLE_HEART: {
      uint8_t scale = (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w));
      drawHeartAt(cx, cy, scale, C_BLACK);
      out = {(int16_t)(cx - scale * 3), (int16_t)(cy - scale * 3),
             (int16_t)(scale * 6), (int16_t)(scale * 6), true};
      break;
    }
    default: {  // STYLE_RECT — 眼睑自上而下
      int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
      if (vis < 5) vis = 5;
      const int16_t top = cy - h / 2 + (h - vis);
      tft.fillRect(cx - w / 2, top, w, vis, C_BLACK);
      out = {(int16_t)(cx - w / 2), top, w, vis, true};
      break;
    }
  }
}

void drawRig() {
  int16_t ox  = rig.ox.cur >> 8;
  int16_t oy  = (rig.oy.cur >> 8) + rigBreathOffset();
  int16_t w   = rig.w.cur >> 8;   if (w < 4) w = 4;
  int16_t h   = rig.h.cur >> 8;   if (h < 4) h = 4;
  int16_t lid = rig.lid.cur >> 8;
  if (lid < 0) lid = 0;
  if (lid > 240) lid = 240;

  if (rig.blinkFrame > 0) {       // 挤压拉伸眨眼,叠加在眼睑之上
    h = (int16_t)((int32_t)h * BLINK_H_PCT[rig.blinkFrame - 1] / 100);
    w = (int16_t)((int32_t)w * BLINK_W_PCT[rig.blinkFrame - 1] / 100);
    if (h < 4) h = 4;
  }

  rigZoneCleared = false;
  if (rig.zoneDirty) {
    tft.fillRect(0, EXPR_ZONE_Y, DISP_W, EXPR_ZONE_H, animBgColor);
    rig.prevValid = false;
    rig.zoneDirty = false;
    rigZoneCleared = true;
  }
  if (rig.prevValid) {
    tft.fillRect(rig.prevL.x - 2, rig.prevL.y - 2, rig.prevL.w + 4, rig.prevL.h + 4, animBgColor);
    tft.fillRect(rig.prevR.x - 2, rig.prevR.y - 2, rig.prevR.w + 4, rig.prevR.h + 4, animBgColor);
  }

  const int16_t cy = eyeCY() + oy;
  drawRigEye(rigLCX(ox), cy, w, h, lid, true,  rig.prevL);
  drawRigEye(rigRCX(ox), cy, w, h, lid, false, rig.prevR);
  rig.prevValid = true;
}

// ── 表情选择:状态 → 姿态 + 行为标志 ──────────────────────────
void rigApplyExpression(bool snap) {
  EyePose p = POSE_NORMAL;
  uint8_t f = RIG_BLINK | RIG_SACCADE | RIG_BREATH;

  if (monitorState == MON_IDLE) {
    switch (currentIdleExpr) {
      case IDLE_SLEEPY: p = POSE_SLEEPY; f = RIG_BREATH2;  break;
      case IDLE_HEART:  p = POSE_HEART;  f = 0;            break;
      case IDLE_HAPPY:  p = POSE_HAPPY;  f = RIG_BREATH;   break;
      default: break;
    }
  } else if (monitorState == MON_THINKING) {
    p = POSE_THINK; f = RIG_BREATH;
  } else if (monitorState == MON_WORKING) {
    switch (workAct) {
      case ACT_READ: p = POSE_READ; f = RIG_BLINK; break;
      case ACT_EDIT: p = POSE_EDIT; f = RIG_BLINK; break;
      case ACT_RUN:  p = POSE_RUN;  f = 0;         break;
      case ACT_NET:  p = POSE_NET;  f = RIG_BLINK; break;
      default:       p = POSE_SCAN; f = 0;         break;
    }
  }

  if (snap) rigSnapPose(p, f);
  else      rigSetPose(p, f);
}

// ── 脚本化行为:周期性挪动目标姿态,弹簧负责平滑 ─────────────
void rigBehaviorTick(unsigned long now) {
  static unsigned long nextMoveMs = 0;
  static uint8_t step = 0;
  if (rig.trans != 0) return;

  if (monitorState == MON_IDLE) {
    switch (currentIdleExpr) {
      case IDLE_HEART:                    // 心跳脉冲
        if (now >= nextMoveMs) {
          rig.pose.w = (rig.pose.w == 6) ? 7 : 6;
          rig.pose.h = rig.pose.w;
          nextMoveMs = now + 600;
        }
        break;
      case IDLE_HAPPY: {                  // 缓动摇摆
        static const int8_t sway[8] = {-6, -3, 0, 3, 6, 3, 0, -3};
        if (now >= nextMoveMs) { rig.pose.ox = sway[step & 7]; step++; nextMoveMs = now + 260; }
        break;
      }
      default: break;                     // 普通/困倦:噪声层足够
    }
    return;
  }

  if (monitorState == MON_THINKING) {     // chevron 开合
    if (now >= nextMoveMs) {
      rig.pose.lid = (rig.pose.lid == 0) ? 240 : 0;
      nextMoveMs = now + 900;
    }
    return;
  }

  if (monitorState == MON_WORKING) {
    switch (workAct) {
      case ACT_READ: {                    // 低头扫读
        static const int8_t readoy[6] = {22, 26, 30, 34, 30, 26};
        if (now >= nextMoveMs) { rig.pose.oy = readoy[step % 6]; step++; nextMoveMs = now + 420; }
        break;
      }
      case ACT_RUN:                       // 紧绷快速小扫视
        if (now >= nextMoveMs) { rig.pose.ox = (int16_t)random(-8, 9); nextMoveMs = now + 260 + random(160); }
        break;
      case ACT_NET:                       // 大幅东张西望
        if (now >= nextMoveMs) { rig.pose.ox = random(2) ? 24 : -24; nextMoveMs = now + 500 + random(400); }
        break;
      case ACT_EDIT:                      // 眼睛稳住,光标覆盖层闪烁
        break;
      default: {                          // ACT_WORK / ACT_AGENT:经典扫视
        static const int8_t scan[10] = {-28, -18, -8, 2, 12, 22, 28, 16, 2, -14};
        if (now >= nextMoveMs) { rig.pose.ox = scan[step % 10]; step++; nextMoveMs = now + 180; }
        break;
      }
    }
  }
}

// ── 覆盖层:困倦 Z 字、edit 光标 ─────────────────────────────
void rigOverlayTick() {
  static uint8_t lastZ = 255;
  static bool caretOn = false;
  static unsigned long caretMs = 0;

  if (monitorState == MON_IDLE && currentIdleExpr == IDLE_SLEEPY && rig.trans == 0) {
    const uint8_t z = 1 + (uint8_t)((millis() / 1400) % 3);
    if (z != lastZ || rigZoneCleared) {
      const int16_t zy = eyeY() + EYE_H + 14;
      tft.fillRect(DISP_W / 2 - 36, zy - 2, 72, 20, animBgColor);
      tft.setTextColor(C_WHITE);
      tft.setTextSize(2);
      tft.setCursor(DISP_W / 2 - 24, zy);
      tft.print("Z");
      if (z > 1) tft.print(" z");
      if (z > 2) tft.print(" z");
      lastZ = z;
    }
  } else {
    lastZ = 255;
  }

  if (monitorState == MON_WORKING && workAct == ACT_EDIT) {
    const unsigned long now = millis();
    if (now - caretMs >= 530 || rigZoneCleared) {
      if (now - caretMs >= 530) { caretOn = !caretOn; caretMs = now; }
      tft.fillRect(206, 196, 10, 16, caretOn ? C_GREEN : animBgColor);
    }
  }

  // 开心眼:偶发星星(位置避开弧线眼的脏区包围盒)
  static unsigned long starMs = 0;
  static bool starsOn = false;
  if (monitorState == MON_IDLE && currentIdleExpr == IDLE_HAPPY && rig.trans == 0) {
    const unsigned long snow = millis();
    if (!starsOn && snow - starMs > 3600) {
      starsOn = true; starMs = snow;
      tft.fillRect(rigLCX(0) - 18, eyeCY() - 24, 4, 4, C_WHITE);
      tft.fillRect(rigRCX(0) + 14, eyeCY() - 22, 3, 3, C_WHITE);
    } else if (starsOn && snow - starMs > 450) {
      starsOn = false; starMs = snow;
      tft.fillRect(rigLCX(0) - 18, eyeCY() - 24, 4, 4, animBgColor);
      tft.fillRect(rigRCX(0) + 14, eyeCY() - 22, 3, 3, animBgColor);
    }
  } else {
    starsOn = false;   // 离开 happy 后区域由 zone 清理负责
  }
}
```

- [ ] **Step 4: 编译检查 [用户检查点]**

Arduino IDE 打开 `clawd_mochi/clawd_mochi.ino` → Verify(✓)。
期望:编译成功(新代码尚无人调用,仅可能有 unused 警告)。

- [ ] **Step 5: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add clawd_mochi/clawd_mochi.ino
git commit -m "✨ feat(firmware): 眼睛骨架引擎 — 弹簧插值/眨眼/扫视/呼吸/样式过渡(未接线)"
```

---

## Task 5: 固件 — Monitor 切换到 rig,删除旧 tick 路径

**Files:**
- Modify: `clawd_mochi/clawd_mochi.ino`(`tickMonitorAnimation`、`applyMonitorState`、`enterMonitorView`、`checkIdleRotation`、`checkStatusTimeout`、`animDoneSparkle`、`routeRedraw`;删除 `tickIdleAnimation`/`tickThinkingAnimation`/`tickWorkingAnimation`/`resetMonitorAnim`/`drawIdleStatic`)

- [ ] **Step 1: 重写 tickMonitorAnimation**

整体替换现有 `tickMonitorAnimation`:

```cpp
void tickMonitorAnimation() {
  if (currentView != VIEW_MONITOR) return;
  if (busy) return;
  if (monitorState == MON_OFFLINE || monitorState == MON_ALERT) return;

  const unsigned long now = millis();
  if (lastAnimTick != 0 && now - lastAnimTick < RIG_TICK_MS) return;
  lastAnimTick = now;

  rigBehaviorTick(now);
  rigTick(now);
  drawRig();
  rigOverlayTick();
}
```

同时删除函数 `tickIdleAnimation`、`tickThinkingAnimation`、`tickWorkingAnimation`、`resetMonitorAnim`(后者的调用点在下面各步一并替换)。

- [ ] **Step 2: applyMonitorState 接 rig**

把 `applyMonitorState` 中 `done` 分支的 `animDoneSparkle();` 保持不变,把状态 switch 替换为:

```cpp
  switch (monitorState) {
    case MON_IDLE:
      if (!backlightOn) setBacklight(true);
      resetIdleRotation();
      rigApplyExpression(false);
      break;
    case MON_THINKING:
    case MON_WORKING:
      if (!backlightOn) setBacklight(true);
      rigApplyExpression(false);
      break;
    case MON_ALERT:
      if (!backlightOn) setBacklight(true);
      animLogoReveal();
      rigInvalidate();
      break;
    case MON_OFFLINE:
      drawNormalEyes();
      setBacklight(false);
      rigInvalidate();
      break;
  }
```

- [ ] **Step 3: enterMonitorView / checkIdleRotation / checkStatusTimeout / animDoneSparkle**

`enterMonitorView` 替换为:

```cpp
void enterMonitorView() {
  currentView = VIEW_MONITOR;
  termMode = false;
  statusTimedOut = false;
  lastAnimTick = 0;
  if (monitorState == MON_IDLE) resetIdleRotation();
  rigApplyExpression(true);   // snap:进视图立即就位
}
```

`checkIdleRotation` 中:

```cpp
  currentIdleIndex = (currentIdleIndex + 1) % IDLE_POOL_SIZE;
  currentIdleExpr = IDLE_POOL[currentIdleIndex];
  animPhase = 0;
  invalidateExpressionCanvas();
```
替换为:
```cpp
  currentIdleIndex = (currentIdleIndex + 1) % IDLE_POOL_SIZE;
  currentIdleExpr = IDLE_POOL[currentIdleIndex];
  rigApplyExpression(false);   // 眼睑过渡切表情
```

`checkStatusTimeout` 末尾的:
```cpp
  resetIdleRotation();
  resetMonitorAnim();
```
替换为:
```cpp
  resetIdleRotation();
  rigApplyExpression(false);
```

`animDoneSparkle` 开头 `drawHappyEyes(0, false);` 之前插入回弹入场:

```cpp
  static const int16_t grow[5] = {8, 20, 36, 26, 30};
  for (uint8_t i = 0; i < 5; i++) {
    tft.fillScreen(animBgColor);
    drawHappyArc(eyeLX(0) + EYE_W / 2, eyeCY() + 8, grow[i], C_BLACK);
    drawHappyArc(eyeRX(0) + EYE_W / 2, eyeCY() + 8, grow[i], C_BLACK);
    delay(speedMs(60));
    server.handleClient();
  }
```

`animDoneSparkle` 末尾的:
```cpp
  resetIdleRotation();
  resetMonitorAnim();
```
替换为:
```cpp
  resetIdleRotation();
  rigApplyExpression(true);
```

- [ ] **Step 4: routeRedraw 的 MONITOR 分支**

把 `routeRedraw` 中:
```cpp
    case VIEW_MONITOR:
      switch (monitorState) {
        case MON_THINKING: drawSquishEyes(false); break;
        case MON_WORKING:  drawScanEyes(0); break;
        default:           drawIdleStatic(IDLE_POOL[currentIdleIndex]); break;
      }
      break;
```
替换为:
```cpp
    case VIEW_MONITOR:
      rigApplyExpression(true);   // 换背景色后整区重绘
      break;
```
并删除函数 `drawIdleStatic`(已无调用方)。

- [ ] **Step 5: 编译检查 [用户检查点]**

Arduino IDE Verify。期望:编译成功。若报 `resetMonitorAnim`/`drawIdleStatic` 未定义,说明有调用点漏替换 —— 全文搜索这两个名字应为 0 处引用。

- [ ] **Step 6: 烧录 + 目测验收 [用户检查点]**

Upload 后用 PowerShell 逐项(`$IP` 换成设备 IP):

```powershell
Invoke-WebRequest -Uri "http://$IP/status?s=idle" -UseBasicParsing       # 普通眼:呼吸+随机眨眼+微扫视,偶尔双连眨
Invoke-WebRequest -Uri "http://$IP/status?s=thinking" -UseBasicParsing   # 眯眼开合,带缓动
Invoke-WebRequest -Uri "http://$IP/status?s=working" -UseBasicParsing    # 窄眼扫视,弹簧回弹
Invoke-WebRequest -Uri "http://$IP/status?s=done" -UseBasicParsing       # 弧线眼回弹入场+闪光,2s 后回 idle
Invoke-WebRequest -Uri "http://$IP/status?s=alert" -UseBasicParsing      # Logo 动画(不变)
Invoke-WebRequest -Uri "http://$IP/status?s=offline" -UseBasicParsing    # 关背光(不变)
```

逐项核对:idle 等 15–45s 自动轮换表情且为**眼睑过渡**不闪屏;web 控制器四个手动视图、终端、画布无回归。

- [ ] **Step 7: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add clawd_mochi/clawd_mochi.ino
git commit -m "♻️ refactor(firmware): Monitor 全表情迁移至 rig 引擎,删除旧 tick 路径"
```

---

## Task 6: 固件 — /status 解析 act/info,working 子状态生效

**Files:**
- Modify: `clawd_mochi/clawd_mochi.ino`(`routeStatus`、`applyMonitorState` 签名;新增 `parseAct`、`setTickerText`)

- [ ] **Step 1: parseAct + setTickerText**

在 `applyMonitorState` 之前插入:

```cpp
uint8_t parseAct(const String& a) {
  if (a == "read")  return ACT_READ;
  if (a == "edit")  return ACT_EDIT;
  if (a == "run")   return ACT_RUN;
  if (a == "net")   return ACT_NET;
  if (a == "agent") return ACT_AGENT;
  return ACT_WORK;
}

const char* actVerb(uint8_t act) {
  switch (act) {
    case ACT_READ:  return "read";
    case ACT_EDIT:  return "edit";
    case ACT_RUN:   return "run";
    case ACT_NET:   return "net";
    case ACT_AGENT: return "agent";
    default:        return "work";
  }
}

void setTickerText(uint8_t act, const String& info) {
  char clean[22];
  uint8_t n = 0;
  for (size_t i = 0; i < info.length() && n < 21; i++) {
    const char c = info[i];
    if (c >= 0x20 && c <= 0x7E) clean[n++] = c;   // 防御:仅可打印 ASCII
  }
  clean[n] = 0;
  if (n == 0) snprintf(tickerText, sizeof(tickerText), "> %s", actVerb(act));
  else        snprintf(tickerText, sizeof(tickerText), "> %s %s", actVerb(act), clean);
}
```

- [ ] **Step 2: applyMonitorState 改三参签名**

签名改为 `void applyMonitorState(const String& s, const String& act, const String& info)`,并在 `else if (s == "working") monitorState = MON_WORKING;` 行后的公共段(`lastStatusMs = millis();` 之前)插入:

```cpp
  if (monitorState == MON_WORKING && s == "working") {
    if (act.length() > 0) {
      workAct = parseAct(act);
      setTickerText(workAct, info);
    } else {
      workAct = ACT_WORK;        // 老 hook / 缺参:经典扫视、无 ticker,与现状一致
      tickerText[0] = 0;
    }
  }
```

注意:`MON_WORKING` 分支已经调用 `rigApplyExpression(false)`(Task 5),act 变化会自动换姿态,无需额外代码。

- [ ] **Step 3: routeStatus 传参**

替换 `routeStatus`:

```cpp
void routeStatus() {
  if (!server.hasArg("s") || server.arg("s").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}");
    return;
  }
  const String s = server.arg("s");
  const String act  = server.hasArg("act")  ? server.arg("act")  : "";
  const String info = server.hasArg("info") ? server.arg("info") : "";
  server.send(200, "application/json", "{\"ok\":1}");
  applyMonitorState(s, act, info);
}
```

- [ ] **Step 4: 编译检查 [用户检查点]**

Arduino IDE Verify。期望:编译成功(`applyMonitorState` 唯一调用点就是 `routeStatus`,签名同步修改)。

- [ ] **Step 5: 烧录 + 设备联测 [用户检查点]**

```powershell
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=read&info=notes.md"  -UseBasicParsing  # 低头扫读
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=edit&info=main.cpp"  -UseBasicParsing  # 微眯+绿光标闪
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=run&info=git"        -UseBasicParsing  # 紧绷快速小扫视
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=net&info=github.com" -UseBasicParsing  # 大幅东张西望
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=agent"               -UseBasicParsing  # 经典扫视
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=qwerty"              -UseBasicParsing  # 未知 act → 经典扫视
Invoke-WebRequest -Uri "http://$IP/status?s=working"                         -UseBasicParsing  # 缺参 → 与旧行为一致
```
五种姿态肉眼可辨;act 之间切换平滑(弹簧过渡)。

- [ ] **Step 6: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add clawd_mochi/clawd_mochi.ino
git commit -m "✨ feat(firmware): /status 解析 act/info,working 六种语义姿态"
```

---

## Task 7: 固件 — 活动 Ticker

**Files:**
- Modify: `clawd_mochi/clawd_mochi.ino`(新增 ticker 函数组;`tickMonitorAnimation` 加一行;`setTickerText` 加两行;`enterMonitorView` 加一行)

- [ ] **Step 1: ticker 全局与函数**

在 Task 4 新增的 `char tickerText[32] = "";` 之后补充:

```cpp
char    tickerDrawn[32] = "";
uint8_t tickerScroll = 0;
unsigned long tickerScrollMs = 0;
bool    tickerVisible = false;
#define TICKER_Y    216
#define TICKER_H    (DISP_H - TICKER_Y)
#define TICKER_COLS 19
```

在 `rigOverlayTick` 之后插入:

```cpp
// ── Activity ticker(仅 MON_WORKING) ─────────────────────────
void clearTicker() {
  if (!tickerVisible) return;
  tft.fillRect(0, TICKER_Y, DISP_W, TICKER_H, animBgColor);
  tickerVisible = false;
  tickerDrawn[0] = 0;
  tickerScroll = 0;
}

void drawTickerFrame(const char* txt) {
  tft.fillRect(0, TICKER_Y, DISP_W, TICKER_H, C_DARKBG);
  tft.drawFastHLine(0, TICKER_Y, DISP_W, C_MUTED);
  tft.setTextColor(C_GREEN);
  tft.setTextSize(2);
  tft.setCursor(4, TICKER_Y + 5);
  char buf[TICKER_COLS + 1];
  strncpy(buf, txt, TICKER_COLS);
  buf[TICKER_COLS] = 0;
  tft.print(buf);
  tickerVisible = true;
}

void tickTicker(unsigned long now) {
  if (monitorState != MON_WORKING || tickerText[0] == 0) {
    clearTicker();
    return;
  }
  const size_t len = strlen(tickerText);
  if (len <= TICKER_COLS) {                       // 静态:仅变化时重绘
    if (strcmp(tickerText, tickerDrawn) != 0) {
      drawTickerFrame(tickerText);
      strcpy(tickerDrawn, tickerText);
    }
    return;
  }
  if (now - tickerScrollMs < 150) return;         // 跑马灯
  tickerScrollMs = now;
  const size_t vlen = len + 3;                    // 3 格空隙
  char win[TICKER_COLS + 1];
  for (uint8_t i = 0; i < TICKER_COLS; i++) {
    const size_t idx = (tickerScroll + i) % vlen;
    win[i] = (idx < len) ? tickerText[idx] : ' ';
  }
  win[TICKER_COLS] = 0;
  drawTickerFrame(win);
  tickerScroll = (uint8_t)((tickerScroll + 1) % vlen);
}
```

- [ ] **Step 2: 接线三处**

`tickMonitorAnimation` 中 `rigOverlayTick();` 之后加:

```cpp
  tickTicker(now);
```

`setTickerText` 末尾(`snprintf` 之后)加:

```cpp
  tickerScroll = 0;
  tickerDrawn[0] = 0;
```

`enterMonitorView` 中 `rigApplyExpression(true);` 之前加:

```cpp
  tickerDrawn[0] = 0;   // 重进视图强制重绘 ticker
  tickerVisible = false;
```

- [ ] **Step 3: 编译检查 [用户检查点]**

Arduino IDE Verify。期望:编译成功。

- [ ] **Step 4: 烧录 + 设备联测 [用户检查点]**

```powershell
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=edit&info=main.cpp" -UseBasicParsing
# 底部出现深色条:"> edit main.cpp"(绿色等宽)
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=read&info=very-long-file-name-here.md" -UseBasicParsing
# 超 19 字符 → 跑马灯滚动
Invoke-WebRequest -Uri "http://$IP/status?s=thinking" -UseBasicParsing
# ticker 消失
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=run&info=npm" -UseBasicParsing
# ticker 回来;等 30s 超时回 idle → ticker 消失
```
另验证:working 中按 web 控制器切到画布再点 Monitor 回来,ticker 能恢复显示。

- [ ] **Step 5: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add clawd_mochi/clawd_mochi.ino
git commit -m "✨ feat(firmware): 底部活动 ticker — 静态/跑马灯,仅 working 显示"
```

---

## Task 8: 文档更新(README + 设备测试指令)

**Files:**
- Modify: `README.md`(「Monitor 状态」表、「HTTP API」节)
- Modify: `hook/状态测试指令.md`(设备部分)

- [ ] **Step 1: README.md**

「Monitor 状态」表中 `working` 一行替换为:

```markdown
| `working` | 按工具显示六种姿态(读/写/跑/搜/子代理/通用),底部 ticker 显示当前文件或命令 | 调用工具 |
```

表格之后追加:

```markdown
所有表情由眼睛骨架引擎驱动:弹簧缓动 + 回弹、随机眨眼(偶尔双连眨)、呼吸起伏,表情切换走眼睑过渡。

Monitor 模式 `working` 支持工具语义(由 Hook 自动附带):

```
GET http://<设备IP>/status?s=working&act=read|edit|run|net|agent&info=<短文本>
```
```

「HTTP API」代码块中 `/status` 一行替换为:

```
GET http://<设备IP>/status?s=idle|thinking|working|done|alert|offline[&act=read|edit|run|net|agent][&info=文本]
```

- [ ] **Step 2: 状态测试指令.md 设备部分**

追加:

```markdown
## working 子状态测试(需设备)

```powershell
$IP = "192.168.x.x"
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=read&info=notes.md"   -UseBasicParsing
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=edit&info=main.cpp"   -UseBasicParsing
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=run&info=git"         -UseBasicParsing
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=net&info=github.com"  -UseBasicParsing
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=agent&info=explore"   -UseBasicParsing
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=bogus&info=x"         -UseBasicParsing  # 未知 act → 通用扫视
Invoke-WebRequest -Uri "http://$IP/status?s=working&act=read&info=very-long-file-name-here.md" -UseBasicParsing  # 跑马灯
```
```

- [ ] **Step 3: 提交**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add README.md "hook/状态测试指令.md"
git commit -m "📝 docs: working 子状态、ticker 与灵动引擎说明"
```

---

## Task 9: 端到端验收与回归

**Files:** 无新改动(发现问题则按问题修复)

- [ ] **Step 1: hook 全量单测**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi" && node --test hook/test/
```
期望:全部 PASS。

- [ ] **Step 2: 真实会话端到端 [用户检查点]**

确认全局 hook 副本已更新(hook 改动需重新安装):

```powershell
cd "D:\Desktop\AI\clawd-micho\clawd-mochi\hook"
.\install-global.ps1
```
(脚本会沿用已有 device.json 的 IP。)

重启 Claude Code,发起一个会让它读文件、改文件、跑命令的任务,观察设备:
- 提问后 → thinking 眯眼
- 读文件 → 低头扫读 + ticker 显示文件名
- 编辑 → 微眯 + 光标闪 + ticker
- 跑命令 → 紧绷小扫视 + ticker 显示命令词
- 完成 → 回弹庆祝 → idle 轮播

- [ ] **Step 3: 回归清单 [用户检查点]**

逐项:手动模式 w/s/d 三个按钮动画、终端输入回显、画布绘制、背景色修改(`/redraw`,Monitor 视图下应整区重绘新背景)、背光开关、`/state` 返回字段不变、AP 热点 + STA 共存、老版 hook(如有其他机器)推 `/status?s=working` 不带参数仍正常。

- [ ] **Step 4: 收尾提交(如有修复)**

```bash
cd "D:\Desktop\AI\clawd-micho\clawd-mochi"
git add -A clawd_mochi hook
git commit -m "🐛 fix: 端到端验收发现的问题修复"
```
