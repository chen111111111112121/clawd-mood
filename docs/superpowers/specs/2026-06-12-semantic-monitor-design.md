# 设计:语义监测 + 表情灵动化(方向 A 第一期)

> 日期:2026-06-12
> 状态:已获用户批准的设计,待实施
> 风格基准:已通过动态 mockup 验证 —— 缓动+回弹扫视、挤压拉伸眨眼、呼吸起伏、随机双连眨

## 背景与目标

当前 Monitor 模式只有 6 个粗粒度状态,任何工具调用都显示同一个扫视眼;hook 把 stdin JSON 里的 `tool_name` / `tool_input` 直接丢弃。表情动画为逐表情手写的 `tick*` 函数,位置跳变、瞬时眨眼、节奏固定。

本期目标:

1. **工具语义表情** —— 设备能区分 Claude 正在读文件 / 写代码 / 跑命令 / 搜索 / 派子代理
2. **活动 Ticker** —— 屏幕底部显示当前动作与文件名
3. **表情灵动化** —— 全部 Monitor 表情迁移到统一的"眼睛骨架"引擎,获得缓动、回弹、呼吸、随机眨眼

**明确不做(留给后续)**:多会话网格(第二期)、情绪引擎(第三期)、手动模式阻塞动画的改造、alert/offline 状态的表现变更。

## 范围内改动的文件

| 文件 | 改动 |
|---|---|
| `hook/clawd-hook.js` | 语义提取、参数清洗、`CLAWD_DRY` 调试模式 |
| `clawd_mochi/clawd_mochi.ino` | rig 引擎、working 子状态、ticker、`/status` 参数解析 |
| `hook/状态测试指令.md` | 新增 act/info 测试命令 |
| `README.md`、`hook/README.md` | 状态映射与 API 表更新 |

## 1. 协议(hook → 固件)

扩展现有端点,完全向后兼容:

```
GET /status?s=working&act=edit&info=clawd_mochi.ino
```

- `act` ∈ `read | edit | run | net | agent | work`,仅在 `s=working` 时有意义
- `info`:已清洗的短文本(见 §2)
- 兼容矩阵:老固件忽略新参数;新固件缺参数时行为与现状完全一致;未知 `act` 按 `work` 处理

## 2. Hook 端(clawd-hook.js)

### 2.1 语义映射

`PreToolUse` / `PostToolUse` 解析 stdin JSON 的 `tool_name` + `tool_input`:

| tool_name | act | info 来源 |
|---|---|---|
| Read / Glob / Grep / NotebookRead | `read` | `file_path` 的 basename,Glob/Grep 用 `pattern` |
| Edit / Write / MultiEdit / NotebookEdit | `edit` | `file_path` 的 basename |
| Bash | `run` | `command` 的首个词 |
| WebFetch / WebSearch | `net` | URL 域名 / `query` 开头 |
| Task | `agent` | `description` |
| 其他 / 缺失 | `work` | tool_name 或留空 |

Cursor 的 `preToolUse` / `postToolUse` 同样处理(payload 字段名一致时复用同一逻辑,取不到则降级为 `work`)。

### 2.2 info 清洗(hook 侧负责语义,固件只做防御)

1. 仅保留可打印 ASCII(0x20–0x7E),其余字符剔除(屏幕字体不支持中文)
2. 清洗后为空(如纯中文文件名)→ 退化为扩展名(`.md`)或 tool_name
3. 截断至 22 字符
4. URL 编码后拼接

### 2.3 调试模式

环境变量 `CLAWD_DRY=1` 时打印将要请求的完整 URL 到 stdout 并退出,不发 HTTP。其余行为(静默失败、2.5s 超时、`beforeSubmitPrompt` 的 `{"continue":true}`)不变。

## 3. 固件:眼睛骨架(rig)引擎

### 3.1 数据结构

```cpp
enum EyeStyle : uint8_t { STYLE_RECT, STYLE_CHEVRON, STYLE_ARC, STYLE_HEART };

struct EyePose {           // 一个表情 = 一个目标姿态
  EyeStyle style;
  int16_t  ox, oy;         // 眼对中心偏移
  int16_t  w, h;           // 眼宽高(CHEVRON 映射为 reach/arm)
  uint8_t  lid;            // 眼睑闭合度 0(全开)–255(全闭)
};

struct EyeRig {            // 运行时状态
  EyePose  target;
  // 各参数的当前值与速度(整数弹簧)
  // 前帧包围盒(脏区擦除)
  // 噪声层状态:下次眨眼时刻、眨眼阶段、下次扫视时刻、呼吸相位
};
```

### 3.2 动画机制

- **tick 频率**:固定 ~33ms(`loop()` 内,沿用现有非阻塞模式);`animSpeed` 不再改 tick 间隔,而是作为运动速度倍率作用于弹簧刚度与噪声频率
- **弹簧插值**:每参数 `vel += (target - cur) * K; vel = vel * DAMP / 256; cur += vel`(整数运算)。K/DAMP 调出轻微过冲回弹的手感,作为顶部可调常量
- **生命噪声层**(叠加在插值结果上,按表情的行为标志启用):
  - 眨眼:间隔 2500–6000ms 随机,12% 概率双连眨;过程为 预备(+6% 高)→ 挤压闭合(scaleY≈0.08、scaleX≈1.18)→ 回弹睁开,整程 ~180ms
  - 微扫视:±3px,间隔 700–2000ms 随机
  - 呼吸:正弦 ±2px,周期 3.4s(查表实现)
- **表情切换过渡**:lid 合上(150ms)→ 切换 style 与姿态 → lid 睁开(150ms),替代现有 `invalidateExpressionCanvas()` 后整屏重绘的硬切
- **渲染**:`drawRig()` 按 style 分发到现有绘制原语(矩形 / `drawChevron` / 弧线 / 心形),脏区机制沿用"擦前帧包围盒 + 画新帧";`lastTickOx`、`lastTickSquishState` 等散装状态变量由 rig 的前帧包围盒统一取代

### 3.3 既有表情迁移

| 表情 | rig 定义 |
|---|---|
| idle-普通 | RECT 居中;眨眼+扫视+呼吸全开 |
| idle-困倦 | RECT 高 lid(~70%)+ 呼吸幅度加倍;"Z"字保留现有绘制 |
| idle-爱心 | HEART;缓动心跳脉冲(w/h 周期 ±8%) |
| idle-开心 | ARC;缓动左右摇摆 + 偶发星星 |
| thinking | CHEVRON;开合由 lid 驱动,叠加呼吸 |
| done | ARC 回弹入场 + 现有 sparkle 序列,结束回 idle(逻辑不变) |
| alert / offline | 不迁移,保持 Logo 动画 / 关背光 |

idle 轮播(15–45s 随机)与 30s 超时逻辑不变,但切换走 lid 过渡。

## 4. working 子状态与 Ticker

### 4.1 子状态姿态(`MON_WORKING` + `workAct`)

| act | 姿态与行为 |
|---|---|
| `read` | 眼位下移、压扁,缓慢向下扫读循环 |
| `edit` | 微眯 + 表情区右下绿色光标块按 ~530ms 闪烁 |
| `run` | 窄高紧绷眼,快速小幅扫视(幅度 ±8px、间隔短) |
| `net` | 标准眼大幅角到角扫视(幅度 ±24px) |
| `agent` / `work` | 现有 Scan 扫视行为(迁到 rig 上) |

### 4.2 Ticker

- 区域:y = 216–240(现有表情区 28–213,无需改布局),背景深色条 + 1px 顶部分隔线
- 内容:`> <act> <info>`,等宽字体 size 2(20 列)
- 超 19 字符跑马灯滚动,步进 150ms;文本未变时不重绘
- 仅 `MON_WORKING` 显示;状态切换、30s 超时、进入手动视图时清除

### 4.3 状态接入

- `routeStatus()` 读取可选 `act` / `info`,传入 `applyMonitorState(s, act, info)`
- 新增全局:`workAct`(枚举)、`tickerText`(定长 char 缓冲)
- 固件防御性过滤 info:仅可打印 ASCII、截断 21 字符

## 5. 错误处理

| 情形 | 行为 |
|---|---|
| 未知 act | 按 `work` 处理 |
| 缺 act/info | 与现状完全一致(通用 working) |
| info 含非法字符 / 超长 | 固件二次过滤截断 |
| 事件爆发(连续 PreToolUse) | ticker 仅文本变化时重绘;rig 设目标 O(1);WebServer 串行处理 |
| hook 任何失败 | 静默退出,不影响宿主(现状保持) |

## 6. 测试计划

**Hook 单测**(无需设备):
```bash
CLAWD_DRY=1 node clawd-hook.js PreToolUse < payload-edit.json   # 期望 URL 含 act=edit&info=...
```
覆盖:六类 act、中文文件名退化、超长截断、缺字段降级、Cursor camelCase 事件。

**设备联测**:`状态测试指令.md` 新增六类 act 的 PowerShell 命令、超长 info、未知 act。

**手感验收**(对照已确认的 mockup):回弹扫视、挤压拉伸眨眼、呼吸、随机双连眨、表情 lid 过渡、六种 working 姿态肉眼可辨。

**回归**:手动四视图按钮、终端输入、画布绘制、WiFi 配置、背光、`/state` 字段、老 hook 打新固件、新 hook 打老固件。

## 7. 验收标准

1. Claude Code 实际会话中,设备能随工具类型切换姿态,ticker 显示当前文件/命令
2. 所有 Monitor 表情具备灵动特征,无表情间闪屏硬切
3. 手动模式与既有 HTTP API 行为零回归
4. 任一端单独升级不破坏另一端
