#include "monitor.hpp"
#include "eyes.hpp"
#include "eyes_pose.hpp"
#include "mood.hpp"
#include "esp_random.h"
#include <math.h>

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
        eyes::setPose(POSE_SHUT, RIG_BREATH, true);   // 闭眼矩形起手，平滑睁开
    }
    mood::resetSleepiness();
    sleepStage = SLEEP_AWAKE;
    sleepScriptMs = 0;
    sleepInited = false;
    sleepClosed = false;
    idlePhaseMs = 0; idleShowingOther = false; s_idleExpr = IDLE_NORMAL;
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

} // namespace monitor
