#include "monitor.hpp"
#include "eyes.hpp"
#include "eyes_pose.hpp"
#include "mood.hpp"
#include "display.hpp"
#include "esp_random.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Working 子动作（M6 的 /status?act= 驱动；M4a 恒为 ACT_WORK）
#define ACT_WORK  0
#define ACT_READ  1
#define ACT_EDIT  2
#define ACT_RUN   3
#define ACT_NET   4
#define ACT_AGENT 5
#define EDIT_COLS 17   // 打字机 edit 表情每行格数

namespace {
using namespace eyes;

// ── monitor 状态 ──
uint8_t  s_state    = MON_IDLE;
uint8_t  s_idleExpr = IDLE_NORMAL;
uint8_t  workAct    = ACT_WORK;
bool     s_winkRight = false;

// 行为脚本桥接：monitor 持有 behPose 副本，逐帧推给 eyes（见计划「关键设计」）
EyePose  behPose  = POSE_NORMAL;
uint8_t  behFlags = RIG_BLINK | RIG_SACCADE | RIG_BREATH;

// idle 轮播状态
bool     idleShowingOther = false;
uint32_t idlePhaseMs = 0;

// 行为脚本计时
uint32_t behNextMs = 0;
uint8_t  behStep   = 0;

inline int32_t rrand(int32_t n)            { return n > 0 ? (int32_t)(esp_random() % (uint32_t)n) : 0; }
inline int32_t rrand(int32_t a, int32_t b) { return a + rrand(b - a); }

// ── 睡眠阶段 ──
enum { SLEEP_AWAKE=0, SLEEP_DROWSY=1, SLEEP_ASLEEP=2, SLEEP_DEEP=3 };
constexpr float    SLEEP_DROWSY_AT = 25.0f;     // 困意≥ 触发入睡
constexpr uint32_t SLP_YAWN_DUR = 1800;
constexpr uint32_t SLP_CLOSE_AT = 16500;
constexpr uint32_t SLP_DEEP_AT  = 18000;

uint8_t  sleepStage    = SLEEP_AWAKE;
uint32_t sleepScriptMs = 0;     // 0=清醒 >0=入睡/睡眠中(锚点)
bool     sleepInited   = false;
bool     sleepClosed   = false;
uint8_t  wakeFlourish  = 0;     // 唤醒花絮:0=无 1=温柔 2=惊醒(深睡)
uint32_t wakeFlourishMs = 0;

// 入睡曲线缓动
inline float sl_clamp(float v,float a,float b){return v<a?a:(v>b?b:v);}
inline float sl_seg(float t,float a,float b){return sl_clamp((t-a)/(b-a),0.f,1.f);}
inline float sl_eio(float t){return t<0.5f?2.f*t*t:1.f-powf(-2.f*t+2.f,2.f)/2.f;}
inline float sl_eo(float t){return 1.f-powf(1.f-t,3.f);}
inline float sl_lerp(float a,float b,float t){return a+(b-a)*t;}
const uint16_t SLEEP_YAWNS[3] = {0,4200,8400};
inline float sleepYawnPhase(uint32_t se){
    for(uint8_t i=0;i<3;i++) if(se>=SLEEP_YAWNS[i] && se<SLEEP_YAWNS[i]+SLP_YAWN_DUR)
        return sinf((float)(se-SLEEP_YAWNS[i])/(float)SLP_YAWN_DUR*(float)M_PI);
    return 0.f;
}

uint32_t otherCycleMs(uint8_t expr) {
    switch (expr) {
        case IDLE_HEART: case IDLE_LOVE:    return 900;
        case IDLE_SPARKLE: case IDLE_HAPPY: return 1200;
        case IDLE_WINK: case IDLE_GIGGLE:   return 1400;
        case IDLE_MUSIC: case IDLE_DIZZY:   return 1600;
        case IDLE_YAWN: case IDLE_SLEEPY:   return 1800;
        default:                            return 1300;
    }
}

// ── 状态 → 姿态 + 行为标志（移植自 .ino rigApplyExpression，idle 分支为主）──
void applyExpression(bool snap) {
    EyePose p = POSE_NORMAL;
    uint8_t f = RIG_BLINK | RIG_SACCADE | RIG_BREATH;
    if (s_state == MON_IDLE) {
        switch (s_idleExpr) {
            case IDLE_SLEEPY:    p = POSE_SLEEPY;    f = RIG_BREATH2; break;
            case IDLE_HEART:     p = POSE_HEART;     f = 0;           break;
            case IDLE_HAPPY:     p = POSE_HAPPY;     f = RIG_BREATH;  break;
            case IDLE_CURIOUS:   p = POSE_CURIOUS;   f = RIG_BLINK;   break;
            case IDLE_WINK:      p = POSE_WINK;      f = RIG_BREATH;  break;
            case IDLE_SPARKLE:   p = POSE_SPARKLE;   f = RIG_BREATH;  break;
            case IDLE_SURPRISED: p = POSE_SURPRISED; f = RIG_BLINK;   break;
            case IDLE_SHY:       p = POSE_SHY;       f = RIG_BLINK;   break;
            case IDLE_DIZZY:     p = POSE_DIZZY;     f = 0;           break;
            case IDLE_MUSIC:     p = POSE_MUSIC;     f = RIG_BREATH;  break;
            case IDLE_YAWN:      p = POSE_YAWN;      f = RIG_BREATH2; break;
            case IDLE_LOVE:      p = POSE_LOVE;      f = 0;           break;
            case IDLE_GIGGLE:    p = POSE_GIGGLE;    f = 0;           break;
            default: break;
        }
    }
    // 表情身份去重：同一表情重复不打断行为脚本
    uint8_t exprId = s_state;
    if (s_state == MON_IDLE) exprId |= (uint8_t)(s_idleExpr << 4);
    static uint8_t lastExprId = 255;
    if (!snap && exprId == lastExprId) return;
    lastExprId = exprId;

    behPose = p; behFlags = f;
    behNextMs = 0; behStep = 0;
    s_winkRight = false; eyes::setWinkRight(false);
    eyes::setPose(behPose, behFlags, snap);
}

// ── 行为脚本：周期性挪 behPose 目标，弹簧负责平滑（移植自 .ino:980-1338）──
static void behaviorTick(uint32_t now) {
  if (eyes::inTransition()) return;

  if (s_state == MON_IDLE) {
    switch (s_idleExpr) {
      case IDLE_HEART:                    // 心跳脉冲
        if (now >= behNextMs) {
          behPose.w = (behPose.w == 6) ? 7 : 6;
          behPose.h = behPose.w;
          behNextMs = now + 600;
          eyes::setPose(behPose, behFlags, false);
        }
        break;
      case IDLE_HAPPY: {                  // 缓动摇摆
        static const int8_t sway[8] = {-6, -3, 0, 3, 6, 3, 0, -3};
        if (now >= behNextMs) { behPose.ox = sway[behStep & 7]; behStep++; behNextMs = now + 260; eyes::setPose(behPose, behFlags, false); }
        break;
      }
      case IDLE_CURIOUS: {                // 东张西望 + 偶尔上抬
        static const int8_t darts[6] = {-22, 22, -22, 0, 22, 0};
        if (now >= behNextMs) {
          behPose.ox = darts[behStep % 6];
          behPose.oy = (behStep % 3 == 0) ? -7 : 0;
          behStep++;
          behNextMs = now + ((behStep & 1) ? 520 : 300);
          eyes::setPose(behPose, behFlags, false);
        }
        break;
      }
      case IDLE_WINK:                     // 右眼周期性眨(drawRig 据此画横条)
        s_winkRight = (now % 2400) < 1100; eyes::setWinkRight(s_winkRight);
        break;
      case IDLE_SPARKLE:                  // 星星眼缩放闪烁
        if (now >= behNextMs) { behPose.w = (behPose.w <= 6) ? 8 : 6; behNextMs = now + 360; eyes::setPose(behPose, behFlags, false); }
        break;
      case IDLE_SURPRISED: {              // 猛地瞪大(弹簧回弹),再缩回
        const bool pop = (now % 2600) < 900;
        behPose.w  = pop ? 42 : EYE_W;
        behPose.h  = pop ? 64 : EYE_H;
        behPose.oy = pop ? -2 : 0;
        eyes::setPose(behPose, behFlags, false);
        break;
      }
      case IDLE_SHY:                      // 不时撇视
        if (now >= behNextMs) { behPose.ox = (behStep & 1) ? 9 : -9; behStep++; behNextMs = now + 900 + rrand(600); eyes::setPose(behPose, behFlags, false); }
        break;
      case IDLE_DIZZY: {                  // 双眼画圈
        const double a = now / 360.0;
        behPose.ox = (int16_t)(cos(a) * 10);
        behPose.oy = (int16_t)(sin(a) * 6);
        eyes::setPose(behPose, behFlags, false);
        break;
      }
      case IDLE_MUSIC: {                  // 随拍摇摆
        static const int8_t sway2[8] = {-7, -4, 0, 4, 7, 4, 0, -4};
        if (now >= behNextMs) { behPose.ox = sway2[behStep & 7]; behStep++; behNextMs = now + 220; eyes::setPose(behPose, behFlags, false); }
        break;
      }
      case IDLE_YAWN: {                   // 眯眼打哈欠(配合 O 嘴覆盖层)
        const unsigned long c = now % 3400;
        if (c < 1700) {
          const double m = sin((double)c / 1700.0 * M_PI);
          behPose.lid = (int16_t)(40 + 200 * m);
          behPose.h   = (int16_t)(56 - 18 * m);
        } else { behPose.lid = 0; behPose.h = 56; }
        eyes::setPose(behPose, behFlags, false);
        break;
      }
      case IDLE_LOVE:                     // 爱心跳动
        if (now >= behNextMs) { behPose.w = (behPose.w == 6) ? 7 : 6; behPose.h = behPose.w; behNextMs = now + 500; eyes::setPose(behPose, behFlags, false); }
        break;
      case IDLE_GIGGLE: {                 // 憋笑上下抖
        const unsigned long c = now % 2200;
        behPose.oy = (c < 1100) ? (8 + (((now / 90) & 1) ? -3 : 3)) : 8;
        eyes::setPose(behPose, behFlags, false);
        break;
      }
      default: break;                     // 普通/困倦:噪声层足够
    }
    return;
  }

  if (s_state == MON_THINKING) {     // 平眼保持平静(动效交给上方省略号 thinkingDotsOverlay)
    return;
  }

  if (s_state == MON_WORKING) {
    switch (workAct) {
      case ACT_READ: {                    // 低头扫读
        static const int8_t readoy[6] = {22, 26, 30, 34, 30, 26};
        if (now >= behNextMs) { behPose.oy = readoy[behStep % 6]; behStep++; behNextMs = now + 420; eyes::setPose(behPose, behFlags, false); }
        break;
      }
      case ACT_RUN:                       // 紧绷快速小扫视
        if (now >= behNextMs) { behPose.ox = (int16_t)rrand(-8, 9); behNextMs = now + 260 + rrand(160); eyes::setPose(behPose, behFlags, false); }
        break;
      case ACT_NET:                       // 大幅东张西望
        if (now >= behNextMs) { behPose.ox = rrand(2) ? 24 : -24; behNextMs = now + 500 + rrand(400); eyes::setPose(behPose, behFlags, false); }
        break;
      case ACT_EDIT:                      // 打字机:眼随写入点移动,行尾回车归位
        if (now >= behNextMs) {
          behStep++;
          if (behStep > EDIT_COLS) behStep = 0;
          behPose.ox = -13 + ((int16_t)behStep * 26) / EDIT_COLS;
          behNextMs = now + 170;
          eyes::setPose(behPose, behFlags, false);
        }
        break;
      default: {                          // ACT_WORK / ACT_AGENT:经典扫视
        static const int8_t scan[10] = {-28, -18, -8, 2, 12, 22, 28, 16, 2, -14};
        if (now >= behNextMs) { behPose.ox = scan[behStep % 10]; behStep++; behNextMs = now + 180; eyes::setPose(behPose, behFlags, false); }
        break;
      }
    }
  }
}

// ── 清醒 idle 轮播（移植自 .ino checkIdleRotation，去掉睡眠分支留 M4b）──
void checkIdleRotation(uint32_t now) {
    if (s_state != MON_IDLE) return;
    if (idlePhaseMs == 0) { idlePhaseMs = now; s_idleExpr = IDLE_NORMAL; applyExpression(true); return; }
    if (!idleShowingOther) {                       // 普通 hub
        if (now - idlePhaseMs < 10000UL /*NORMAL_HOLD_MS*/) return;
        s_idleExpr = mood::pickAwakeExpr();
        idleShowingOther = true;
        idlePhaseMs = now;
        applyExpression(false);
    } else {                                        // 穿插其他表情:循环 3 次回普通
        if (now - idlePhaseMs < otherCycleMs(s_idleExpr) * 3UL /*OTHER_LOOP_COUNT*/) return;
        s_idleExpr = IDLE_NORMAL;
        idleShowingOther = false;
        idlePhaseMs = now;
        applyExpression(false);
    }
}

// ── 入睡脚本：困意累出后从清醒平滑滑入熟睡（逐帧驱动，移植自 .ino:1705-1742）──
static void tickSleep(uint32_t now) {
    if (sleepScriptMs == 0) return;
    const uint32_t se = now - sleepScriptMs;

    if (se < SLP_CLOSE_AT) {                 // 入睡曲线：RECT，多次哈欠 + 渐沉 + 慢眨 + 点头
        if (!sleepInited) { eyes::setPose(POSE_NORMAL, 0, true); sleepInited = true; }
        const float t = (float)se;
        float open;
        if      (t < 10000) open = sl_lerp(1.00f, 0.42f, sl_eio(sl_seg(t, 0, 10000)));
        else if (t < 13000) open = 0.42f;
        else if (t < 15000) open = sl_lerp(0.42f, 0.12f, sl_eio(sl_seg(t, 13000, 15000)));
        else                open = sl_lerp(0.12f, 0.00f, sl_eio(sl_seg(t, 15000, 16500)));
        float oy = sinf(t / 2600.0f * 2.0f * (float)M_PI) * 1.6f;
        const float ym = sleepYawnPhase(se);
        if (ym > 0) { open = fminf(open, 1.0f - 0.72f * ym); oy += -3.0f * ym; }
        const int16_t bw[3][2] = {{3000,500},{7000,560},{14200,760}};
        for (uint8_t i = 0; i < 3; i++)
            if (t >= bw[i][0] && t < bw[i][0] + bw[i][1])
                open *= 1.0f - sinf(sl_seg(t, bw[i][0], bw[i][0] + bw[i][1]) * (float)M_PI);
        if (t >= 10000 && t < 13000) {
            if (t < 11300)      { oy += sl_lerp(0,16, sl_eio(sl_seg(t,10200,11300))); open = fminf(open, sl_lerp(0.40f,0.22f, sl_seg(t,10200,11300))); }
            else if (t < 11700) { float p = sl_seg(t,11300,11700); oy += sl_lerp(16,-2, sl_eo(p)); open = fmaxf(open, sl_lerp(0.22f,0.78f, sl_eo(p))); }
            else                { float p = sl_seg(t,11700,13000); oy += sl_lerp(-2,10, p);        open = fminf(open, sl_lerp(0.78f,0.40f, sl_eo(p))); }
        }
        const int16_t lid = (int16_t)(240.0f * (1.0f - sl_clamp(open, 0.f, 1.f)));
        EyePose p = { STYLE_RECT, 0, (int16_t)oy, EYE_W, EYE_H, (uint8_t)lid };
        eyes::scriptPose(p, 0);              // 逐帧直驱（不整区清屏）
        sleepClosed = false;
    } else {                                  // 已合眼：睡着→熟睡（闭眼弧 + 呼吸）
        if (!sleepClosed) { sleepClosed = true; sleepStage = SLEEP_ASLEEP; eyes::setPose(POSE_DOZE, RIG_BREATH2, true); }
        if (se >= SLP_DEEP_AT && sleepStage != SLEEP_DEEP) { sleepStage = SLEEP_DEEP; eyes::setPose(POSE_DEEPSLEEP, RIG_BREATH2, false); }
    }
}

// ── 唤醒：复位困意/脚本，回普通轮播（唤醒花絮覆盖层 = M4b-2）──
static void triggerWake(uint32_t now) {
    if (sleepScriptMs != 0) {
        wakeFlourish = (sleepStage == SLEEP_DEEP) ? 2 : 1;   // 熟睡=惊醒花絮，其余=温柔
        wakeFlourishMs = now;
        eyes::setPose(POSE_SHUT, RIG_BREATH, true);   // 闭眼矩形起手，平滑睁开
    }
    mood::resetSleepiness();
    sleepStage = SLEEP_AWAKE;
    sleepScriptMs = 0;
    sleepInited = false;
    sleepClosed = false;
    idlePhaseMs = 0; idleShowingOther = false; s_idleExpr = IDLE_NORMAL;
}

// ═══════════════════ 覆盖层（叠加在 drawRig 之上） ═══════════════════
// ── 覆盖层共用色/坐标常量 ─────────────────────────────────────────
constexpr uint16_t OV_BG    = 0xD880;   // 品牌橙脸底色(原 animBgColor)
constexpr uint16_t OV_WHITE = 0xFFFF;
constexpr uint16_t OV_BLACK = 0x0000;
constexpr uint16_t OV_BLUSH = 0xFAEF;
constexpr int16_t  DISP_W = 240, LCX = 45, RCX = 195, EYECY = 80;  // = rigLCX(0)/rigRCX(0)/eyeCY()

// 矩形结构(供 eraseRectOutside 局部使用)
struct OvRect { int16_t x, y, w, h; bool valid; };

// ── 位图与小图形(本地副本,均画在 g 上) ───────────────────────────
const uint8_t HEART5[5] = { 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 };
const uint8_t STAR7[7]  = { 0b0001000, 0b0011100, 0b1111111, 0b0111110, 0b0011100, 0b0010100, 0b0100010 };

void drawHeartAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
  auto& g = display::gfx();
  for (int8_t row = 0; row < 5; row++)
    for (int8_t c = 0; c < 5; c++)
      if (HEART5[row] & (1 << (4 - c)))
        g.fillRect(cx + (c - 2) * scale, cy + (row - 2) * scale, scale, scale, col);
}

void drawStarAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
  auto& g = display::gfx();
  for (int8_t row = 0; row < 7; row++)
    for (int8_t c = 0; c < 7; c++)
      if (STAR7[row] & (1 << (6 - c)))
        g.fillRect(cx + (c - 3) * scale, cy + (row - 3) * scale, scale, scale, col);
}

void drawHappyArc(int16_t cx, int16_t cy, int16_t w, uint16_t col) {
  auto& g = display::gfx();
  const int16_t hw = w / 2;
  for (uint8_t t = 0; t < 4; t++) {
    g.drawLine(cx - hw, cy + t,         cx,       cy - hw / 2 + t, col);
    g.drawLine(cx,      cy - hw / 2 + t, cx + hw,  cy + t,         col);
  }
}

// 擦除旧矩形中未被新矩形覆盖的部分(最多 4 条边条),黑色主体直接覆盖,避免闪烁
void eraseRectOutside(const OvRect &p, int16_t nx, int16_t ny, int16_t nw, int16_t nh) {
  if (!p.valid) return;
  auto& g = display::gfx();
  const int16_t px2 = p.x + p.w, py2 = p.y + p.h;
  const int16_t nx2 = nx + nw, ny2 = ny + nh;
  if (nx >= px2 || nx2 <= p.x || ny >= py2 || ny2 <= p.y) {
    g.fillRect(p.x, p.y, p.w, p.h, OV_BG);   // 无重叠:整块擦
    return;
  }
  if (ny > p.y)  g.fillRect(p.x, p.y, p.w, ny - p.y, OV_BG);
  if (py2 > ny2) g.fillRect(p.x, ny2, p.w, py2 - ny2, OV_BG);
  const int16_t iy = (p.y > ny) ? p.y : ny;
  const int16_t ih = ((py2 < ny2) ? py2 : ny2) - iy;
  if (ih > 0) {
    if (nx > p.x)  g.fillRect(p.x, iy, nx - p.x, ih, OV_BG);
    if (px2 > nx2) g.fillRect(nx2, iy, px2 - nx2, ih, OV_BG);
  }
}

// ── 新空闲表情覆盖层(飘字/精灵):签名门控 + 记录上一帧矩形按需擦除 ──
#define OV_MAX 6
int16_t  ovRx[OV_MAX], ovRy[OV_MAX], ovRw[OV_MAX], ovRh[OV_MAX];
uint8_t  ovRn   = 0;            // 上一帧绘制的精灵矩形数
uint32_t ovSig  = 0xFFFFFFFF;   // 上一帧签名(不变则跳过,避免静态闪烁)
uint8_t  ovExpr = 255;          // 上一帧的 idle 表情

void ovEraseAll() {
  auto& g = display::gfx();
  for (uint8_t i = 0; i < ovRn; i++) g.fillRect(ovRx[i], ovRy[i], ovRw[i], ovRh[i], OV_BG);
  ovRn = 0;
}
void ovMark(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (ovRn >= OV_MAX) return;
  ovRx[ovRn] = x; ovRy[ovRn] = y; ovRw[ovRn] = w; ovRh[ovRn] = h; ovRn++;
}
void ovText(const char* s, int16_t cx, int16_t topY, uint8_t size, uint16_t col) {
  auto& g = display::gfx();
  const int16_t w = (int16_t)strlen(s) * 6 * size, h = 8 * size, x = cx - w / 2;
  g.setTextSize(size); g.setTextColor(col); g.setCursor(x, topY); g.print(s);
  g.setTextSize(1);
  ovMark(x - 2, topY - 2, w + 4, h + 4);
}
void ovNote(int16_t x, int16_t y, uint16_t col) {   // 简易八分音符:符头 + 符干
  auto& g = display::gfx();
  g.fillRect(x, y, 6, 4, col);
  g.fillRect(x + 5, y - 12, 2, 14, col);
  ovMark(x - 1, y - 13, 10, 20);
}

// 仅服务 idle 新表情(>=IDLE_CURIOUS)。眼睛由 drawRig 先画,这里画其上的飘字/精灵,位置一律避开眼睛矩形。
void idleNewOverlay(unsigned long now) {
  auto& g = display::gfx();
  const bool active = (s_state == MON_IDLE
                       && s_idleExpr >= IDLE_CURIOUS && !eyes::inTransition());
  if (!active) {
    if (ovExpr != 255) { ovEraseAll(); ovExpr = 255; ovSig = 0xFFFFFFFF; }
    return;
  }
  if (s_idleExpr != ovExpr) { ovEraseAll(); ovExpr = s_idleExpr; ovSig = 0xFFFFFFFF; }

  switch (s_idleExpr) {
    case IDLE_CURIOUS: {                    // 一跳一跳的问号
      const bool show = (now % 2600) < 1500;
      const uint32_t sig = show ? (uint32_t)(100 + (now / 60) % 90) : 0;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      if (show) {
        const int16_t yo = (int16_t)(sin((double)(now % 1500) / 1500.0 * M_PI) * 9);
        ovText("?", DISP_W / 2, 30 - yo, 3, OV_WHITE);
      }
      ovSig = sig; break;
    }
    case IDLE_WINK: {                       // 上扬小嘴(静态)
      const uint32_t sig = 1;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      drawHappyArc(DISP_W / 2, 150, 40, OV_BLACK);
      ovMark(DISP_W / 2 - 22, 138, 44, 22);
      ovSig = sig; break;
    }
    case IDLE_SPARKLE: {                    // 上方闪光点
      const bool on = ((now / 300) % 2) == 0;
      const uint32_t sig = on ? 1 : 2;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      if (on) {
        static const int16_t pts[3][2] = {{96, 34}, {144, 38}, {DISP_W / 2, 22}};
        for (uint8_t i = 0; i < 3; i++) {
          g.fillRect(pts[i][0] - 4, pts[i][1], 8, 2, OV_WHITE);
          g.fillRect(pts[i][0] - 1, pts[i][1] - 4, 2, 8, OV_WHITE);
          ovMark(pts[i][0] - 5, pts[i][1] - 5, 10, 10);
        }
      }
      ovSig = sig; break;
    }
    case IDLE_SURPRISED: {                  // 感叹号
      const unsigned long c = now % 2600;
      const bool show = c < 900;
      const uint32_t sig = show ? (uint32_t)(50 + c / 120) : 0;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      if (show) ovText("!", DISP_W / 2, 24, 3, OV_WHITE);
      ovSig = sig; break;
    }
    case IDLE_SHY: {                        // 双颊腮红(缓慢脉动)
      const uint8_t p = (now / 350) % 4;
      const int16_t extra = (p < 2) ? p : (4 - p);   // 0,1,2,1
      const uint32_t sig = 10 + extra;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      const int16_t bw = 26 + extra * 3, bh = 12, by = 116;
      g.fillRoundRect(45 - bw / 2,  by, bw, bh, 5, OV_BLUSH); ovMark(45 - bw / 2 - 1,  by - 1, bw + 2, bh + 2);
      g.fillRoundRect(195 - bw / 2, by, bw, bh, 5, OV_BLUSH); ovMark(195 - bw / 2 - 1, by - 1, bw + 2, bh + 2);
      ovSig = sig; break;
    }
    case IDLE_DIZZY: {                      // 头顶绕圈小星
      const uint32_t sig = now / 45;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 3; i++) {
        const double a = now / 420.0 + i * 2.094;
        const int16_t x = DISP_W / 2 + (int16_t)(cos(a) * 22);
        const int16_t y = 26 + (int16_t)(sin(a) * 12);
        drawStarAt(x, y, 2, OV_WHITE);
        ovMark(x - 8, y - 8, 16, 16);
      }
      ovSig = sig; break;
    }
    case IDLE_MUSIC: {                      // 两侧升起的音符
      const uint32_t sig = now / 45;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 2; i++) {
        const unsigned long base = now + i * 900;
        const double t = (double)(base % 1800) / 1800.0;
        const int16_t x = (i ? 222 : 14) + (int16_t)(sin(t * 6) * 4);
        const int16_t y = 150 - (int16_t)(t * 70);
        ovNote(x, y, OV_WHITE);
      }
      ovSig = sig; break;
    }
    case IDLE_YAWN: {                       // O 形嘴(单次整块写入,避免脉动闪烁)
      const unsigned long c = now % 3400;
      const bool open = c < 1700;
      const double m = open ? sin((double)c / 1700.0 * M_PI) : 0;
      const uint32_t sig = open ? (uint32_t)(20 + (int)(m * 18)) : 0;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      if (!open) {                          // 合嘴:整块清掉上一帧的嘴(它本就要消失)
        if (ovRn > 0) g.fillRect(ovRx[0], ovRy[0], ovRw[0], ovRh[0], OV_BG);
        ovRn = 0;
      } else {
        const int16_t mw = 10 + (int16_t)(30 * m), mh = 8 + (int16_t)(34 * m);
        const int16_t mx = DISP_W / 2 - mw / 2, my = 150 - mh / 2;
        if (ovRn > 0) {                     // 只擦旧嘴露出的部分(实心,不闪),不整块刷
          OvRect pm = {ovRx[0], ovRy[0], ovRw[0], ovRh[0], true};
          eraseRectOutside(pm, mx, my, mw, mh);
        }
        ovRn = 0;
        lgfx::LGFX_Sprite cv(&g);           // 内存拼好整块再一次性推屏(每像素只写一次)
        cv.setColorDepth(16); cv.createSprite(mw, mh);
        cv.fillScreen(OV_BG);
        cv.fillRoundRect(0, 0, mw, mh, (mw < mh ? mw : mh) / 2, OV_BLACK);
        cv.pushSprite(mx, my); cv.deleteSprite();
        ovMark(mx, my, mw, mh);
      }
      ovSig = sig; break;
    }
    case IDLE_LOVE: {                       // 升起的小爱心(白)
      const uint32_t sig = now / 45;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      for (uint8_t i = 0; i < 3; i++) {
        const unsigned long base = now + i * 700;
        const double t = (double)(base % 2100) / 2100.0;
        const int16_t x = DISP_W / 2 + (i - 1) * 36 + (int16_t)(sin(t * 5) * 5);
        const int16_t y = 60 - (int16_t)(t * 48);
        drawHeartAt(x, y, 3, OV_WHITE);
        ovMark(x - 10, y - 10, 20, 20);
      }
      ovSig = sig; break;
    }
    case IDLE_GIGGLE: {                     // 两侧笑纹(笑时显示)
      const bool on = (now % 2200) < 1100;
      const uint32_t sig = on ? 1 : 0;
      if (sig == ovSig && !eyes::zoneClearedThisFrame()) break;
      ovEraseAll();
      if (on) {
        for (uint8_t i = 0; i < 2; i++) {
          const int16_t y = 62 + i * 7;
          g.drawLine(20,  y, 12,  y - 4, OV_WHITE); ovMark(11,  y - 5, 11, 7);
          g.drawLine(220, y, 228, y - 4, OV_WHITE); ovMark(219, y - 5, 11, 7);
        }
      }
      ovSig = sig; break;
    }
  }
}

// 开心眼:偶发星星(位置避开弧线眼的脏区包围盒)。原 rigOverlayTick 的 HAPPY 块。
void happyStars(unsigned long now) {
  auto& g = display::gfx();
  static unsigned long starMs = 0;
  static bool starsOn = false;
  if (s_state == MON_IDLE && s_idleExpr == IDLE_HAPPY && !eyes::inTransition()) {
    const unsigned long snow = now;
    if (!starsOn && snow - starMs > 3600) {
      starsOn = true; starMs = snow;
      g.fillRect(LCX - 18, EYECY - 24, 4, 4, OV_WHITE);
      g.fillRect(RCX + 14, EYECY - 22, 3, 3, OV_WHITE);
    } else if (starsOn && snow - starMs > 450) {
      starsOn = false; starMs = snow;
      g.fillRect(LCX - 18, EYECY - 24, 4, 4, OV_BG);
      g.fillRect(RCX + 14, EYECY - 22, 3, 3, OV_BG);
    }
  } else {
    starsOn = false;   // 离开 happy 后区域由 zone 清理负责
  }
}

// 鼻涕泡:白圈 + 高光(熟睡)
void drawSleepBubble(int16_t cx, int16_t cy, int16_t r) {
  auto& g = display::gfx();
  g.drawCircle(cx, cy, r, OV_WHITE);
  g.fillCircle(cx - r / 3, cy - r / 3, 2, OV_WHITE);
}

// 入睡打哈欠的 O 形嘴(黑色圆角矩形随 sin 张合;复用 IDLE_YAWN 画法,在 drawRig 之后画)
void tickYawnMouth(unsigned long now) {
  auto& g = display::gfx();
  static bool    shown = false;
  static int16_t pmw   = -1;
  const int16_t EX = 98, EY = 126, EW = 44, EH = 48;            // 擦除区(容纳最大嘴)
  const float m = (s_state == MON_IDLE
                   && sleepScriptMs != 0 && !eyes::inTransition())
                  ? sleepYawnPhase(now - sleepScriptMs) : 0.f;   // 多次哈欠窗口
  if (m <= 0.02f) {
    if (shown) { g.fillRect(EX, EY, EW, EH, OV_BG); shown = false; pmw = -1; }
    return;
  }
  const int16_t mw = 10 + (int16_t)(30 * m), mh = 8 + (int16_t)(34 * m);
  if (mw == pmw && !eyes::zoneClearedThisFrame()) return;
  pmw = mw;
  g.fillRect(EX, EY, EW, EH, OV_BG);                            // 擦上一帧整块,防残留
  lgfx::LGFX_Sprite cv(&g);
  cv.setColorDepth(16); cv.createSprite(mw, mh);
  cv.fillScreen(OV_BG);
  cv.fillRoundRect(0, 0, mw, mh, (mw < mh ? mw : mh) / 2, OV_BLACK);
  cv.pushSprite(120 - mw / 2, 150 - mh / 2); cv.deleteSprite();
  shown = true;
}

// 唤醒花絮:叠加在已切好的目标态之上,~600ms 非阻塞。眼睛睁开由 rig 过渡自然完成。
void tickWakeFlourish(unsigned long now) {
  if (wakeFlourish == 0) return;
  auto& g = display::gfx();
  const int16_t EXX = 90, EXY = 6, EXW = 60, EXH = 38;     // "!" 区
  const int16_t BX = 80, BY = 110, BW = 80, BH = 80;       // 泡破区(须容纳半径~36 的飞溅)
  const unsigned long t = now - wakeFlourishMs;
  if (t > 600) {
    g.fillRect(EXX, EXY, EXW, EXH, OV_BG);
    g.fillRect(BX, BY, BW, BH, OV_BG);
    wakeFlourish = 0;
    return;
  }
  if (wakeFlourish == 2) {                                 // 惊醒:泡破 + "!"
    g.fillRect(BX, BY, BW, BH, OV_BG);
    if (t < 220) {
      const float pt = t / 220.0f;
      for (uint8_t i = 0; i < 6; i++) {
        const float a = i / 6.0f * 6.2832f;
        const int16_t r0 = 6 + (int16_t)(pt * 16), r1 = 12 + (int16_t)(pt * 24);
        g.drawLine(120 + cos(a) * r0, 150 + sin(a) * r0,
                   120 + cos(a) * r1, 150 + sin(a) * r1, OV_WHITE);
      }
    }
    g.fillRect(EXX, EXY, EXW, EXH, OV_BG);                 // "!"
    g.setTextColor(OV_WHITE); g.setTextSize(4);
    g.setCursor(113, 8); g.print('!'); g.setTextSize(1);
  }
  // wakeFlourish==1(温柔):不画额外元素,睁眼/伸展由 rig 切到目标态完成
}

// 睡眠覆盖层:睡着/熟睡的 Zzz;熟睡额外鼻涕泡随呼吸吹大→破。脏区按签名重画,避免闪烁。
void tickSleepOverlay(unsigned long now) {
  auto& g = display::gfx();
  static int8_t   lastStage = -1;
  static uint32_t lastSig   = 0xFFFFFFFF;
  const int16_t ZX = 80, ZY = 2, ZW = 92, ZH = 42;       // Zzz 区
  const int16_t BX = 94, BY = 124, BW = 52, BH = 52;     // 鼻涕泡区(眼下方)
  const bool active = (s_state == MON_IDLE
                       && sleepStage >= SLEEP_ASLEEP && !eyes::inTransition());
  if (!active) {
    if (lastStage != -1) {
      g.fillRect(ZX, ZY, ZW, ZH, OV_BG);
      g.fillRect(BX, BY, BW, BH, OV_BG);
      lastStage = -1; lastSig = 0xFFFFFFFF;
    }
    return;
  }
  if ((int8_t)sleepStage != lastStage || eyes::zoneClearedThisFrame()) {
    g.fillRect(ZX, ZY, ZW, ZH, OV_BG);
    g.fillRect(BX, BY, BW, BH, OV_BG);
    lastStage = (int8_t)sleepStage; lastSig = 0xFFFFFFFF;
  }
  const uint8_t z = 1 + (uint8_t)((now / 1500) % 3);
  int16_t br = -1;
  if (sleepStage == SLEEP_DEEP) {
    const uint16_t bp = (uint16_t)(now % 4200);
    br = (bp < 3600) ? (int16_t)(4 + 18UL * bp / 3600) : -1;   // <3.6s 吹大,之后破
  }
  const uint32_t sig = (uint32_t)z * 64 + (uint32_t)(br < 0 ? 63 : br);
  if (sig == lastSig && !eyes::zoneClearedThisFrame()) return;
  lastSig = sig;

  g.fillRect(ZX, ZY, ZW, ZH, OV_BG);                  // Zzz:由小到大向右上
  g.setTextColor(OV_WHITE);
  for (uint8_t i = 0; i < z; i++) {
    g.setTextSize(i + 1);
    g.setCursor(92 + i * 18, 34 - i * 12);
    g.print('z');
  }
  g.setTextSize(1);

  if (sleepStage == SLEEP_DEEP) {                     // 鼻涕泡
    g.fillRect(BX, BY, BW, BH, OV_BG);
    if (br > 0) {
      drawSleepBubble(120, 150, br);
    } else {                                          // 破裂飞溅
      for (uint8_t i = 0; i < 6; i++) {
        const float a = i / 6.0f * 6.2832f;
        g.drawLine(120 + cos(a) * 8, 150 + sin(a) * 8,
                   120 + cos(a) * 20, 150 + sin(a) * 20, OV_WHITE);
      }
    }
  }
}
} // namespace

namespace monitor {

void init() {
    s_state = MON_IDLE;
    s_idleExpr = IDLE_NORMAL;
    idlePhaseMs = 0;
    applyExpression(true);
}

void tick(uint32_t now) {
    const bool active = (s_state == MON_THINKING || s_state == MON_WORKING);
    const bool sleeping = (s_state == MON_IDLE && sleepStage >= SLEEP_ASLEEP);
    bool moodChanged = mood::update(now, active, sleeping);

    // —— 临时调试自动唤醒：熟睡满 8s 自醒，便于真机看完整 睡→醒→再睡 循环。M6 接 /status 后删除。——
    if (s_state == MON_IDLE && sleepStage == SLEEP_DEEP && sleepScriptMs != 0
        && (now - sleepScriptMs) > (SLP_DEEP_AT + 8000)) {
        triggerWake(now);
    }

    // 困意到阈值 → 起入睡脚本
    if (s_state == MON_IDLE && sleepScriptMs == 0 && mood::sleepiness() >= SLEEP_DROWSY_AT) {
        sleepScriptMs = now; sleepStage = SLEEP_DROWSY; sleepInited = false; sleepClosed = false;
        s_idleExpr = IDLE_NORMAL;
    }

    if (s_state == MON_IDLE && sleepScriptMs != 0) {
        tickSleep(now);                 // 入睡/睡眠中：脚本接管眼睛
    } else {
        if (moodChanged && s_state == MON_IDLE && !idleShowingOther)
            idlePhaseMs = (now > 10000UL) ? (now - 10000UL) : 0;
        checkIdleRotation(now);         // 清醒：M4a 轮播
        behaviorTick(now);
    }
}

void drawOverlays(uint32_t now) {
    idleNewOverlay(now);     // idle 表情飘字/精灵(自带 active 守卫)
    happyStars(now);         // 开心眼偶发星
    tickYawnMouth(now);      // 入睡 O 嘴
    tickSleepOverlay(now);   // 睡着 Zzz + 熟睡鼻涕泡
    tickWakeFlourish(now);   // 唤醒花絮
}

} // namespace monitor
