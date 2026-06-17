#include "monitor.hpp"
#include "eyes.hpp"
#include "eyes_pose.hpp"
#include "mood.hpp"
#include "display.hpp"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

// thinking/working 无新事件多久自动回 idle（容长思考/长回答的纯生成阶段）
#define STATUS_TIMEOUT_MS 180000

// edit 打字机书写带几何（移植自 .ino）
#define EDIT_CELL_W  12
#define EDIT_LINE_X  18
#define EDIT_LINE_Y  196
#define EDIT_LINE_H  10
#define EDIT_TRACE_Y 184
#define EDIT_CARET_H 14
#define EDIT_BAND_H  ((EDIT_LINE_Y + EDIT_CARET_H) - EDIT_TRACE_Y)

// working 跑马灯几何
#define TICKER_Y    213
#define TICKER_H    (240 - TICKER_Y)
#define TICKER_COLS 19

namespace {
using namespace eyes;

// ── monitor 状态 ──
uint8_t  s_state    = MON_IDLE;
uint8_t  s_idleExpr = IDLE_NORMAL;
uint8_t  workAct    = ACT_WORK;
bool     s_winkRight = false;

// ── /status 状态机 ──
uint32_t lastStatusMs   = 0;       // 上次状态推送时刻（0=从未；超时/alert 紧急度用）
bool     statusTimedOut = false;
// HTTP task → render task 的待应用状态：单写者=HTTP handler、单消费=render task tick，
// 避免两个 task 同时改 rig/eyes（见计划「线程安全」）。
volatile bool s_pending = false;
char s_pendS[16]    = "";
char s_pendAct[16]  = "";
char s_pendInfo[24] = "";
// done 庆祝（非阻塞，render task 内逐帧跑）
bool     s_celebrate   = false;
uint32_t s_celebrateMs = 0;
// working 跑马灯
char     tickerText[32]  = "";
char     tickerDrawn[32] = "";
uint8_t  tickerScroll    = 0;
uint32_t tickerScrollMs  = 0;
bool     tickerVisible   = false;

// ── 手动状态牌 presence ──
enum { PRES_NONE=0, PRES_MEETING, PRES_TOILET, PRES_SOLDER, PRES_REST };
uint8_t s_presence = PRES_NONE;
volatile bool s_presPending = false;   // HTTP task 写、render task 消费
char    s_presPendS[12]     = "";
int8_t  s_presDrawn         = -1;      // 场景上一帧已画的 presence(变化→静态道具重画)

uint8_t parsePresence(const char* s) {
    if (!strcmp(s, "meeting")) return PRES_MEETING;
    if (!strcmp(s, "toilet"))  return PRES_TOILET;
    if (!strcmp(s, "solder"))  return PRES_SOLDER;
    if (!strcmp(s, "rest"))    return PRES_REST;
    return PRES_NONE;
}

// ── 状态助手（移植自 .ino parseAct/actVerb/setTickerText）──
uint8_t parseAct(const char* a) {
    if (!strcmp(a, "read"))  return ACT_READ;
    if (!strcmp(a, "edit"))  return ACT_EDIT;
    if (!strcmp(a, "run"))   return ACT_RUN;
    if (!strcmp(a, "net"))   return ACT_NET;
    if (!strcmp(a, "agent")) return ACT_AGENT;
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
void setTickerText(uint8_t act, const char* info) {
    char clean[22];
    uint8_t n = 0;
    for (size_t i = 0; info[i] && n < 21; i++) {
        const char c = info[i];
        if (c >= 0x20 && c <= 0x7E) clean[n++] = c;   // 仅可打印 ASCII
    }
    clean[n] = 0;
    if (n == 0) snprintf(tickerText, sizeof(tickerText), "> %s", actVerb(act));
    else        snprintf(tickerText, sizeof(tickerText), "> %s %s", actVerb(act), clean);
    tickerScroll = 0;
    tickerDrawn[0] = 0;
}

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
    } else if (s_state == MON_THINKING) {
        p = POSE_THINK; f = RIG_BREATH;
    } else if (s_state == MON_WORKING) {
        switch (workAct) {
            case ACT_READ: p = POSE_READ; f = RIG_BLINK; break;
            case ACT_EDIT: p = POSE_EDIT; f = RIG_BLINK; break;
            case ACT_RUN:  p = POSE_RUN;  f = 0;         break;
            case ACT_NET:  p = POSE_NET;  f = RIG_BLINK; break;
            default:       p = POSE_SCAN; f = 0;         break;
        }
    } else if (s_state == MON_ALERT) {
        p = POSE_SURPRISED; f = RIG_BLINK | RIG_BREATH;   // 警觉眼 + 上方「!」角标
    }
    // MON_OFFLINE 落到 POSE_NORMAL（静态普通眼）
    // 表情身份去重：同一表情重复不打断行为脚本
    uint8_t exprId = s_state;
    if (s_state == MON_IDLE)    exprId |= (uint8_t)(s_idleExpr << 4);
    if (s_state == MON_WORKING) exprId |= (uint8_t)(workAct << 4);
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

// ── 消费 HTTP task 投递的待应用状态（移植自 .ino applyMonitorState，由 render task 调）──
static void applyPendingState(uint32_t now) {
    if (!s_pending) return;
    char s[16], act[16], info[24];
    strncpy(s,    s_pendS,    sizeof(s));    s[sizeof(s)-1]       = 0;
    strncpy(act,  s_pendAct,  sizeof(act));  act[sizeof(act)-1]   = 0;
    strncpy(info, s_pendInfo, sizeof(info)); info[sizeof(info)-1] = 0;
    s_pending = false;   // 单消费者：清旗标即"已取"

    if (!strcmp(s, "done")) {                 // 完成：唤醒 + 攒喜悦 + 非阻塞庆祝
        lastStatusMs = now; statusTimedOut = false;
        triggerWake(now);
        mood::onDone();
        s_state = MON_IDLE; s_idleExpr = IDLE_HAPPY;
        idleShowingOther = true; idlePhaseMs = now;   // 当作一次"其他表情"，循环完自动回普通
        s_celebrate = true; s_celebrateMs = now;
        applyExpression(false);               // → 开心弧眼（happyStars + tickCelebrate 加料）
        return;
    }

    uint8_t ns;
    if      (!strcmp(s, "idle"))     ns = MON_IDLE;
    else if (!strcmp(s, "thinking")) ns = MON_THINKING;
    else if (!strcmp(s, "working"))  ns = MON_WORKING;
    else if (!strcmp(s, "alert"))    ns = MON_ALERT;
    else if (!strcmp(s, "offline"))  ns = MON_OFFLINE;
    else return;                              // 未知状态：忽略
    s_state = ns;

    if (s_state == MON_WORKING) {
        if (act[0]) { workAct = parseAct(act); setTickerText(workAct, info); }
        else        { workAct = ACT_WORK; tickerText[0] = 0; }   // 老 hook/缺参：经典扫视无跑马灯
    }

    if (s_state == MON_THINKING || s_state == MON_WORKING || s_state == MON_ALERT)
        triggerWake(now);                     // 活动事件唤醒（睡眠中也唤醒）
    // idle/offline 不唤醒、不清困意（idle 继续累积；offline 暂停）
    lastStatusMs = now; statusTimedOut = false;

    switch (s_state) {
        case MON_IDLE:
            display::backlight(true);
            if (sleepScriptMs == 0) {         // 睡眠中收到 idle：不打断，继续睡
                idlePhaseMs = 0; idleShowingOther = false; s_idleExpr = IDLE_NORMAL;
                applyExpression(false);
            }
            break;
        case MON_THINKING:
        case MON_WORKING:
        case MON_ALERT:
            display::backlight(true);
            applyExpression(false);
            break;
        case MON_OFFLINE:                     // 离线：普通眼 + 灭背光 + 复位入睡
            sleepScriptMs = 0; sleepStage = SLEEP_AWAKE; sleepClosed = false; sleepInited = false;
            applyExpression(false);
            display::backlight(false);
            break;
    }
}

// ── thinking/working 30s/3min 无新事件自动回 idle（移植自 .ino checkStatusTimeout）──
static void checkStatusTimeout(uint32_t now) {
    if (lastStatusMs == 0) return;
    if (s_state == MON_IDLE || s_state == MON_OFFLINE) return;
    if (s_state == MON_ALERT) return;         // alert 不超时：逐级加急直到新事件
    if (now - lastStatusMs < STATUS_TIMEOUT_MS) return;
    if (statusTimedOut) return;
    statusTimedOut = true;
    s_state = MON_IDLE;
    display::backlight(true);
    idlePhaseMs = 0; idleShowingOther = false; s_idleExpr = IDLE_NORMAL;
    applyExpression(false);
}

// ═══════════════════ 覆盖层（叠加在 drawRig 之上） ═══════════════════
// ── 覆盖层共用色/坐标常量 ─────────────────────────────────────────
constexpr uint16_t OV_BG    = 0xD880;   // 品牌橙脸底色(原 animBgColor)
constexpr uint16_t OV_WHITE = 0xFFFF;
constexpr uint16_t OV_BLACK = 0x0000;
constexpr uint16_t OV_BLUSH = 0xFAEF;
constexpr int16_t  DISP_W = 240, LCX = 45, RCX = 195, EYECY = 80;  // = rigLCX(0)/rigRCX(0)/eyeCY()
constexpr int16_t  DISP_H = 240;
// 跑马灯/edit 配色（移植自 .ino color565）
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t C_GREEN  = rgb565(80, 220, 130);
constexpr uint16_t C_DARKBG = rgb565(10,  12,  16);
constexpr uint16_t C_MUTED  = rgb565(90,  88,  86);
constexpr uint16_t C_DIMGRN = rgb565(18,  92,  40);

// 矩形结构(供 eraseRectOutside 局部使用)
struct OvRect { int16_t x, y, w, h; bool valid; };

// 消费 /presence pending：切换时整屏清一次、复位场景缓存；切回 auto 恢复正常 Monitor
static void applyPresencePending(uint32_t now) {
    if (!s_presPending) return;
    char s[12]; strncpy(s, s_presPendS, sizeof(s)); s[sizeof(s)-1] = 0;
    s_presPending = false;
    uint8_t np = parsePresence(s);
    if (np == s_presence) return;
    s_presence = np;
    display::gfx().fillScreen(OV_BG);
    s_presDrawn = -1;
    if (np == PRES_NONE) {
        idlePhaseMs = 0; idleShowingOther = false; s_idleExpr = IDLE_NORMAL;
        sleepScriptMs = 0; sleepStage = SLEEP_AWAKE; sleepClosed = false; sleepInited = false;
        s_state = MON_IDLE;
        applyExpression(true);
    } else {
        // 进场:标脏区,确保 fillScreen 抹掉眼睛后这帧会重画(随后 presenceTickEyes 覆盖为状态牌姿态)
        eyes::setPose(POSE_NORMAL, 0, true);
    }
    (void)now;
}

// 状态牌期间每帧设眼睛姿态（scriptPose 逐帧直驱）
static void presenceTickEyes(uint32_t now) {
    EyePose p = POSE_NORMAL;
    switch (s_presence) {
        case PRES_MEETING: p = {STYLE_RECT, (int16_t)(sinf(now/1500.0f)*4), 14, EYE_W, 36, 60}; break;
        case PRES_TOILET:  p = {STYLE_ARC,  (int16_t)(sinf(now/900.0f)*5),   6, 30, 30,  0}; break;
        case PRES_SOLDER:  p = {STYLE_RECT, (int16_t)(sinf(now/220.0f)*4 + sinf(now/90.0f)*1.5f), 18, EYE_W, 24, 80}; break;
        case PRES_REST:    p = {STYLE_RECT, (int16_t)(sinf(now/960.0f)*3), (int16_t)(sinf(now/480.0f)*4), EYE_W, EYE_H, 0}; break;
        default: break;
    }
    eyes::scriptPose(p, 0);
}

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
// 防闪:不整块刷背景,而是"只擦旧嘴露出的边条(eraseRectOutside)+ 单次推新嘴精灵",
//      重叠区每像素只写一次最终色,杜绝"整框先变橙再画黑"的闪烁。
void tickYawnMouth(unsigned long now) {
  auto& g = display::gfx();
  static bool   shown = false;
  static OvRect pm    = {0, 0, 0, 0, false};   // 上一帧嘴包围盒
  static int16_t pmw  = -1;
  const float m = (s_state == MON_IDLE
                   && sleepScriptMs != 0 && !eyes::inTransition())
                  ? sleepYawnPhase(now - sleepScriptMs) : 0.f;   // 多次哈欠窗口
  if (m <= 0.02f) {
    if (shown) { if (pm.valid) g.fillRect(pm.x, pm.y, pm.w, pm.h, OV_BG); shown = false; pmw = -1; pm.valid = false; }
    return;
  }
  const int16_t mw = 10 + (int16_t)(30 * m), mh = 8 + (int16_t)(34 * m);
  if (mw == pmw && !eyes::zoneClearedThisFrame()) return;
  pmw = mw;
  if (eyes::zoneClearedThisFrame()) pm.valid = false;   // 表情区被整清过:旧嘴已没,别再擦
  const int16_t mx = 120 - mw / 2, my = 150 - mh / 2;
  if (pm.valid) eraseRectOutside(pm, mx, my, mw, mh);   // 只擦旧嘴露出的部分(实心,不闪)
  lgfx::LGFX_Sprite cv(&g);
  cv.setColorDepth(16); cv.createSprite(mw, mh);
  cv.fillScreen(OV_BG);
  cv.fillRoundRect(0, 0, mw, mh, (mw < mh ? mw : mh) / 2, OV_BLACK);
  cv.pushSprite(mx, my); cv.deleteSprite();
  pm = {mx, my, mw, mh, true};
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

// ── ALERT 告急描边（仅第 3 档）：四周白边随脉冲；on=false 擦掉固定 10px 边框区 ──
void alertBorder(bool on, unsigned long now) {
  auto& g = display::gfx();
  const int16_t MAX = 10;
  g.fillRect(0, 0, DISP_W, MAX, OV_BG);
  g.fillRect(0, DISP_H - MAX, DISP_W, MAX, OV_BG);
  g.fillRect(0, 0, MAX, DISP_H, OV_BG);
  g.fillRect(DISP_W - MAX, 0, MAX, DISP_H, OV_BG);
  if (!on) return;
  const double p = (sin((double)now / 300.0) + 1.0) / 2.0;   // 0..1 ~2s 呼吸
  const int16_t th = (int16_t)(p * MAX);
  if (th <= 0) return;
  g.fillRect(0, 0, DISP_W, th, OV_WHITE);
  g.fillRect(0, DISP_H - th, DISP_W, th, OV_WHITE);
  g.fillRect(0, 0, th, DISP_H, OV_WHITE);
  g.fillRect(DISP_W - th, 0, th, DISP_H, OV_WHITE);
}

// ── ALERT 角标：眼上一个上下跳的「!」，紧急度按无人处理时长分三档 ──
void alertBadgeOverlay(unsigned long now) {
  auto& g = display::gfx();
  static uint32_t lastSig = 0xFFFFFFFF;
  static bool shown = false;
  const int16_t BX = DISP_W / 2 - 22, BY = 12, BW = 44, BH = 60;
  const bool active = (s_state == MON_ALERT && !eyes::inTransition());
  if (!active) {
    if (shown) { g.fillRect(BX, BY, BW, BH, OV_BG); alertBorder(false, now); shown = false; lastSig = 0xFFFFFFFF; }
    return;
  }
  const unsigned long el = now - lastStatusMs;
  const uint8_t  stage  = (el >= 20000) ? 2 : (el >= 8000 ? 1 : 0);
  const uint16_t period = (stage == 2) ? 420 : (stage == 1) ? 650 : 1400;
  const int16_t  amp    = (stage == 2) ? 5   : (stage == 1) ? 4   : 3;
  const int16_t  bw     = (stage == 2) ? 9   : (stage == 1) ? 8   : 6;
  const int16_t  bh     = (stage == 2) ? 30  : (stage == 1) ? 27  : 22;
  const int16_t  dh     = (stage == 2) ? 9   : (stage == 1) ? 8   : 6;
  const int16_t  yo     = (int16_t)(sin((double)(now % period) / period * 2 * M_PI) * amp);
  uint32_t sig = (uint32_t)stage * 100000 + (uint32_t)(yo + 20) * 1000;
  if (stage == 2) sig += (now / 70) % 1000;                  // 告急档边框持续脉冲
  if (sig == lastSig && shown && !eyes::zoneClearedThisFrame()) return;
  lastSig = sig;
  alertBorder(stage == 2, now);
  g.fillRect(BX, BY, BW, BH, OV_BG);
  const int16_t topY = 18 + yo;
  g.fillRect(DISP_W / 2 - bw / 2, topY,          bw, bh, OV_WHITE);   // 「!」竖条
  g.fillRect(DISP_W / 2 - bw / 2, topY + bh + 5, bw, dh, OV_WHITE);   // 「!」点
  shown = true;
}

// ── 思考省略号：眼上三个黑点依次起跳「···」──
void thinkingDotsOverlay(unsigned long now) {
  auto& g = display::gfx();
  static bool shown = false;
  static uint32_t lastSig = 0xFFFFFFFF;
  const int16_t BX = DISP_W / 2 - 38, BY = 48, BW = 76, BH = 26;
  const bool active = (s_state == MON_THINKING && !eyes::inTransition());
  if (!active) {
    if (shown) { g.fillRect(BX, BY, BW, BH, OV_BG); shown = false; lastSig = 0xFFFFFFFF; }
    return;
  }
  const uint32_t sig = (uint32_t)(now / 50);
  if (sig == lastSig && shown && !eyes::zoneClearedThisFrame()) return;
  lastSig = sig;
  g.fillRect(BX, BY, BW, BH, OV_BG);
  const int16_t cx = DISP_W / 2, baseY = 64;
  for (uint8_t i = 0; i < 3; i++) {
    const double ph = fmod((double)now / 260.0 - i + 30.0, 3.0);
    const int16_t yo = (ph < 1.0) ? (int16_t)(sin(ph * M_PI) * 7) : 0;
    g.fillCircle(cx - 26 + i * 26, baseY - yo, 5, OV_BLACK);
  }
  shown = true;
}

// ── working/edit 打字机：底部代码块 + 闪烁光标 + 上行暗痕（移植自 .ino rigOverlayTick edit 块）──
void editCaretOverlay(unsigned long now) {
  auto& g = display::gfx();
  static bool caretOn = false;
  static unsigned long caretMs = 0;
  static uint8_t  lastEditCol = 255;   // 255=未在书写
  static int16_t  lastCaretX  = -1;
  static uint16_t editLine    = 0;
  if (s_state == MON_WORKING && workAct == ACT_EDIT) {
    const uint8_t col = (behStep > EDIT_COLS) ? EDIT_COLS : (uint8_t)behStep;
    const int16_t caretX = EDIT_LINE_X + (int16_t)col * EDIT_CELL_W;
    const bool wrapped = (lastEditCol != 255 && col < lastEditCol);
    const bool jumped  = (lastEditCol != 255 && col > lastEditCol + 1);
    const bool zc = eyes::zoneClearedThisFrame();
    const bool rebuild = zc || lastEditCol == 255 || wrapped || jumped;
    if (caretX != lastCaretX && lastCaretX >= 0) g.fillRect(lastCaretX, EDIT_LINE_Y, 8, EDIT_CARET_H, OV_BG);
    if (rebuild) {
      if (lastEditCol == 255) editLine = 0;
      if (wrapped) editLine++;
      g.fillRect(0, EDIT_TRACE_Y, DISP_W, EDIT_BAND_H, OV_BG);
      if (editLine > 0) {
        const int16_t tw = (editLine & 1) ? 150 : 110;
        for (int16_t x = EDIT_LINE_X; x < EDIT_LINE_X + tw; x += EDIT_CELL_W) g.fillRect(x, EDIT_TRACE_Y, 8, 6, C_DIMGRN);
      }
      for (uint8_t i = 0; i < col; i++) {
        if ((i * 7 + editLine * 3) % 5 == 0) continue;
        g.fillRect(EDIT_LINE_X + i * EDIT_CELL_W, EDIT_LINE_Y, 8, EDIT_LINE_H, C_GREEN);
      }
    } else if (col == lastEditCol + 1) {
      const uint8_t i = col - 1;
      if ((i * 7 + editLine * 3) % 5 != 0) g.fillRect(EDIT_LINE_X + i * EDIT_CELL_W, EDIT_LINE_Y, 8, EDIT_LINE_H, C_GREEN);
    }
    lastEditCol = col;
    if (caretX != lastCaretX) {
      lastCaretX = caretX; caretOn = true; caretMs = now;
      g.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, C_GREEN);
    } else if (rebuild) {
      if (caretOn) g.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, C_GREEN);
    } else if (now - caretMs >= 400) {
      caretOn = !caretOn; caretMs = now;
      g.fillRect(caretX, EDIT_LINE_Y, 8, EDIT_CARET_H, caretOn ? C_GREEN : OV_BG);
    }
  } else if (lastEditCol != 255) {
    g.fillRect(0, EDIT_TRACE_Y, DISP_W, EDIT_BAND_H, OV_BG);
    lastEditCol = 255; lastCaretX = -1; editLine = 0;
  }
}

// ── Activity ticker（仅 MON_WORKING，底部一行打字机式状态文本）──
void clearTicker() {
  if (!tickerVisible) return;
  display::gfx().fillRect(0, TICKER_Y, DISP_W, TICKER_H, OV_BG);
  tickerVisible = false; tickerDrawn[0] = 0; tickerScroll = 0;
}
void drawTickerFrame(const char* txt) {
  auto& g = display::gfx();
  g.fillRect(0, TICKER_Y, DISP_W, TICKER_H, C_DARKBG);
  g.drawFastHLine(0, TICKER_Y, DISP_W, C_MUTED);
  g.setTextColor(C_GREEN); g.setTextSize(2); g.setCursor(4, TICKER_Y + 5);
  char buf[TICKER_COLS + 1];
  strncpy(buf, txt, TICKER_COLS); buf[TICKER_COLS] = 0;
  g.print(buf); g.setTextSize(1);
  tickerVisible = true;
}
void tickTicker(unsigned long now) {
  if (s_state != MON_WORKING || tickerText[0] == 0) { clearTicker(); return; }
  const size_t len = strlen(tickerText);
  if (len <= TICKER_COLS) {                       // 静态：仅变化时重绘
    if (strcmp(tickerText, tickerDrawn) != 0) { drawTickerFrame(tickerText); strcpy(tickerDrawn, tickerText); }
    return;
  }
  if (now - tickerScrollMs < 150) return;         // 跑马灯
  tickerScrollMs = now;
  const size_t vlen = len + 3;
  char win[TICKER_COLS + 1];
  for (uint8_t i = 0; i < TICKER_COLS; i++) { const size_t idx = (tickerScroll + i) % vlen; win[i] = (idx < len) ? tickerText[idx] : ' '; }
  win[TICKER_COLS] = 0;
  drawTickerFrame(win);
  tickerScroll = (uint8_t)((tickerScroll + 1) % vlen);
}

// ── done 庆祝：开心弧眼之上，顶/底安全条 ~1.6s 闪烁星点（非阻塞）──
uint32_t celebSig = 0xFFFFFFFF;
void tickCelebrate(unsigned long now) {
  if (!s_celebrate) return;
  auto& g = display::gfx();
  const int16_t TY = 4, TH = 30, BY = 196, BH = 30;
  const unsigned long t = now - s_celebrateMs;
  if (t > 1600) {
    g.fillRect(0, TY, DISP_W, TH, OV_BG);
    g.fillRect(0, BY, DISP_W, BH, OV_BG);
    s_celebrate = false; celebSig = 0xFFFFFFFF;
    return;
  }
  const uint32_t sig = now / 90;
  if (sig == celebSig && !eyes::zoneClearedThisFrame()) return;
  celebSig = sig;
  g.fillRect(0, TY, DISP_W, TH, OV_BG);
  g.fillRect(0, BY, DISP_W, BH, OV_BG);
  static const int16_t sx[5] = {28, 76, 120, 164, 212};
  for (uint8_t i = 0; i < 5; i++) {
    const uint8_t sc = 1 + (uint8_t)((now / 120 + i) % 3);
    drawStarAt(sx[i],     TY + 15, sc, OV_WHITE);
    drawStarAt(sx[4 - i], BY + 15, sc, OV_WHITE);
  }
}

// ── 手动状态牌场景（P4–P7 填充道具，本期为空桩）──
// 开会中:认真盯笔记本(静态) + 视频会议宫格 + 发言者绿框轮换(动态)。眼睛由 presenceTickEyes 处理。
static void drawMeetingScene(uint32_t now) {
    auto& g = display::gfx();
    const int16_t sx=64, sy=150, sw=112, sh=46;
    const uint16_t C_BORD=rgb565(17,22,31), C_SCR=rgb565(11,27,51),
                   C_TILE=rgb565(38,69,99), C_MAN=rgb565(159,182,207),
                   C_SPK=rgb565(70,210,122), C_KB=rgb565(42,49,64), C_PAD=rgb565(58,65,80);
    const int cols=3, rows=2, gp=4;
    const int tw=(sw-gp*(cols+1))/cols, th=(sh-gp*(rows+1))/rows;
    const int active = (int)((now/900) % (uint32_t)(cols*rows));
    static int lastActive = -1;
    auto tilePos = [&](int idx, int& tx, int& ty){ tx = sx+gp+(idx%cols)*(tw+gp); ty = sy+gp+(idx/cols)*(th+gp); };

    if (s_presDrawn != (int8_t)PRES_MEETING) {        // 进场:静态笔记本 + 全部 tile + 键盘
        g.fillRoundRect(sx-3, sy-3, sw+6, sh+6, 6, C_BORD);
        g.fillRoundRect(sx, sy, sw, sh, 4, C_SCR);
        for (int i=0;i<cols*rows;i++){ int tx,ty; tilePos(i,tx,ty);
            g.fillRoundRect(tx,ty,tw,th,2,C_TILE);
            g.fillCircle(tx+tw/2, ty+(int)(th*0.40f), (int)(th*0.19f), C_MAN);
            g.fillRoundRect(tx+tw/2-(int)(tw*0.27f), ty+(int)(th*0.60f), (int)(tw*0.54f), (int)(th*0.36f), 2, C_MAN);
        }
        g.fillTriangle(sx-2,sy+sh+3, sx+sw+2,sy+sh+3, sx+sw+15,sy+sh+17, C_KB);
        g.fillTriangle(sx-2,sy+sh+3, sx+sw+15,sy+sh+17, sx-15,sy+sh+17, C_KB);
        g.fillRect(120-15, sy+sh+8, 30, 3, C_PAD);
        lastActive = -1;
    }
    if (active != lastActive) {                        // 发言绿框:重画上一 tile(连小人)抹掉绿框,避免圆角残留;描新
        if (lastActive >= 0) {
            int tx,ty; tilePos(lastActive,tx,ty);
            g.fillRoundRect(tx,ty,tw,th,2,C_TILE);
            g.fillCircle(tx+tw/2, ty+(int)(th*0.40f), (int)(th*0.19f), C_MAN);
            g.fillRoundRect(tx+tw/2-(int)(tw*0.27f), ty+(int)(th*0.60f), (int)(tw*0.54f), (int)(th*0.36f), 2, C_MAN);
        }
        int tx,ty; tilePos(active,tx,ty); g.drawRect(tx+1,ty+1,tw-2,th-2,C_SPK);
        lastActive = active;
    }
}
// 上厕所中:WC 门牌轻浮 + 悠闲口哨音符升起(各自脏区重画)。眼睛由 presenceTickEyes 处理(笑眯眼)。
static void drawToiletScene(uint32_t now) {
    auto& g = display::gfx();
    // WC 门牌(随 sin 上下浮 ~3px)
    static int16_t lastSy = -999;
    const int16_t sw=44, sh=28, sx=120-22;
    const int16_t syf = 22 + (int16_t)(sinf(now/1100.0f)*3);
    if (syf != lastSy) {
        if (lastSy != -999) g.fillRect(sx-1, lastSy-1, sw+2, sh+2, OV_BG);   // 擦旧
        g.fillRoundRect(sx, syf, sw, sh, 7, OV_WHITE);
        g.setTextColor(OV_BG); g.setTextSize(2); g.setCursor(sx+6, syf+7); g.print("WC");
        g.setTextSize(1);
        lastSy = syf;
    }
    // 口哨音符(右侧升起循环;每帧擦旧画新)
    static int16_t pnx=-999, pny=-999;
    const float t = (now % 1900) / 1900.0f;
    const int16_t nx = 196 + (int16_t)(sinf(t*6)*4), ny = 150 - (int16_t)(t*42);
    if (pnx != -999) g.fillRect(pnx-1, pny-13, 10, 20, OV_BG);   // 擦旧音符包围盒
    g.fillRect(nx, ny, 6, 4, OV_WHITE);            // 符头
    g.fillRect(nx+5, ny-12, 2, 14, OV_WHITE);      // 符干
    pnx = nx; pny = ny;
}
// 实验室中:PCB 绿板 + 烙铁尖朝下(静态) + 焊点火花(偶发) + 升起烟丝(动态)。眼睛由 presenceTickEyes 处理。
static void drawSolderScene(uint32_t now) {
    auto& g = display::gfx();
    const int16_t jx=118, jy=204;                         // 焊点
    const uint16_t C_PTHK=rgb565(14,42,26), C_PFACE=rgb565(31,138,76),
                   C_TRACE=rgb565(207,155,58), C_PAD=rgb565(230,194,90),
                   C_GRIP=rgb565(43,64,94), C_GHI=rgb565(77,93,132), C_GROOVE=rgb565(35,43,62),
                   C_FERR=rgb565(202,162,58), C_BAR=rgb565(207,207,207), C_BARHI=rgb565(242,242,242),
                   C_TIP=rgb565(143,143,143), C_HOT=rgb565(255,140,50), C_SMOKE=rgb565(150,150,150);

    if (s_presDrawn != (int8_t)PRES_SOLDER) {             // 进场:静态 PCB + 烙铁
        const int16_t pbX=22, pbY=204, pbW=196, pbH=32;
        g.fillRect(pbX, pbY+4, pbW, pbH, C_PTHK);         // 板厚
        g.fillRect(pbX, pbY, pbW, pbH-4, C_PFACE);        // 板面
        g.drawLine(pbX+18,pbY+20, 74,pbY+20, C_TRACE); g.drawLine(74,pbY+20, 96,pbY+11, C_TRACE); g.drawLine(96,pbY+11, jx,pbY+9, C_TRACE);
        g.drawLine(jx,pbY+9, 150,pbY+9, C_TRACE);         g.drawLine(150,pbY+9, 170,pbY+22, C_TRACE);
        const int16_t pads[4][2]={{48,pbY+20},{74,pbY+20},{170,pbY+22},{196,pbY+15}};
        for (auto& pd : pads) g.fillCircle(pd[0], pd[1], 3, C_PAD);
        // 烙铁:沿轴 T(jx,jy-2)→H(210,150);粗线沿法线铺多条 1px 线
        const float Tx=jx, Ty=jy-2, Hx=210, Hy=150;
        float ux=Hx-Tx, uy=Hy-Ty; const float L=sqrtf(ux*ux+uy*uy); ux/=L; uy/=L;
        const float px=-uy, py=ux;
        auto P=[&](float d, float& X, float& Y){ X=Tx+ux*d; Y=Ty+uy*d; };
        auto thick=[&](float d0,float d1,int w,uint16_t c){ float ax,ay,bx,by; P(d0,ax,ay); P(d1,bx,by);
            for(int o=-w/2;o<=w/2;o++) g.drawLine((int)(ax+px*o),(int)(ay+py*o),(int)(bx+px*o),(int)(by+py*o),c); };
        thick(38,104,14,C_GRIP); thick(44,98,4,C_GHI);
        for (float d=48; d<=92; d+=9){ float cx,cy; P(d,cx,cy); g.drawLine((int)(cx+px*7),(int)(cy+py*7),(int)(cx-px*7),(int)(cy-py*7), C_GROOVE); }
        thick(30,38,11,C_FERR); thick(14,30,7,C_BAR); thick(15,29,3,C_BARHI);
        float bX,bY,eX,eY,hX,hY; P(14,bX,bY); P(0,eX,eY); P(5,hX,hY);
        g.fillTriangle((int)(bX+px*3.6f),(int)(bY+py*3.6f),(int)(bX-px*3.6f),(int)(bY-py*3.6f),(int)eX,(int)eY, C_TIP);
        g.fillTriangle((int)(hX+px*2.6f),(int)(hY+py*2.6f),(int)(hX-px*2.6f),(int)(hY-py*2.6f),(int)eX,(int)eY, C_HOT);
    }
    // 火花(偶发迸一下):画/擦一个小包围盒
    static bool sparkOn=false;
    const bool spark = (now % 1400) < 160;
    if (spark != sparkOn) {
        g.fillRect(jx-12, jy-12, 24, 24, OV_BG);
        if (spark) for (int i=0;i<6;i++){ float a=i/6.0f*6.2832f;
            g.drawLine((int)(jx+cosf(a)*4),(int)(jy+sinf(a)*4),(int)(jx+cosf(a)*11),(int)(jy+sinf(a)*11), OV_WHITE); }
        sparkOn = spark;
    }
    // 烟丝(每帧波浪上升):擦上一帧烟带 + 重画
    const int16_t smTop=jy-6-18*3, smBoxW=40;
    g.fillRect(jx-smBoxW/2, smTop-2, smBoxW, (jy-6)-smTop+4, OV_BG);  // 擦烟带
    int16_t lastX=0,lastY=0;
    for (int k=0;k<=18;k++){ int16_t yy=jy-6-k*3; int16_t xx=jx+(int16_t)(sinf(now/300.0f + k*0.5f)*(4+k*0.35f));
        if (k>0) g.drawLine(lastX,lastY,xx,yy,C_SMOKE); lastX=xx; lastY=yy; }
}
static void drawRestScene   (uint32_t now){ (void)now; }
static void drawPresenceScene(uint32_t now) {
    switch (s_presence) {
        case PRES_MEETING: drawMeetingScene(now); break;
        case PRES_TOILET:  drawToiletScene(now);  break;
        case PRES_SOLDER:  drawSolderScene(now);  break;
        case PRES_REST:    drawRestScene(now);    break;
        default: break;
    }
    s_presDrawn = (int8_t)s_presence;
}
} // namespace

namespace monitor {

// 开机问候动画（阻塞，~2.3s）：闭眼→缓缓睁开→腮红→上扬嘴+小星+右眼眨一下→定格。
// 纯 display::gfx() 直绘，不碰 eyes/mood；须在 eyes::init() 之前调（init 会 fillScreen 接管）。
void bootGreeting() {
    auto& g = display::gfx();
    const int16_t lx = LCX - EYE_W / 2, rx = RCX - EYE_W / 2;   // 眼左上 x = 30 / 180
    const int16_t ey = EYECY - EYE_H / 2, ecy = EYECY;          // 眼顶 y = 50 / 眼中心 = 80
    const int16_t bw = 26, bh = 12, by = 116;                   // 腮红块

    g.fillScreen(OV_BG);
    g.fillRect(lx, ecy - 3, EYE_W, 6, OV_BLACK);               // 1) 闭眼细线
    g.fillRect(rx, ecy - 3, EYE_W, 6, OV_BLACK);
    vTaskDelay(pdMS_TO_TICKS(200));

    const uint8_t OPEN_STEPS = 18;                              // 2) ease-out 睁开（只增不减覆盖，无闪）
    for (uint8_t s = 1; s <= OPEN_STEPS; s++) {
        const float t = (float)s / OPEN_STEPS;
        const float e = 1.0f - (1.0f - t) * (1.0f - t);
        int16_t vis = 6 + (int16_t)((EYE_H - 6) * e);
        if (vis > EYE_H) vis = EYE_H;
        const int16_t top = ecy - vis / 2;
        g.fillRect(lx, top, EYE_W, vis, OV_BLACK);
        g.fillRect(rx, top, EYE_W, vis, OV_BLACK);
        vTaskDelay(pdMS_TO_TICKS(55));
    }

    for (uint8_t k = 1; k <= 3; k++) {                          // 3) 腮红分级放大浮现
        const int16_t w = bw * k / 3, h = bh * k / 3;
        g.fillRoundRect(LCX - w / 2, by + (bh - h) / 2, w, h, 4, OV_BLUSH);
        g.fillRoundRect(RCX - w / 2, by + (bh - h) / 2, w, h, 4, OV_BLUSH);
        vTaskDelay(pdMS_TO_TICKS(90));
    }

    drawHappyArc(DISP_W / 2, 150, 40, OV_BLACK);                // 4) 上扬小嘴 + 右上小星 + 右眼眨
    drawStarAt(206, 34, 3, OV_WHITE);
    vTaskDelay(pdMS_TO_TICKS(120));
    g.fillRect(rx, ey, EYE_W, (ecy - 3) - ey, OV_BG);          // 右眼闭：只擦上下两条细缝
    g.fillRect(rx, ecy + 3, EYE_W, (ey + EYE_H) - (ecy + 3), OV_BG);
    vTaskDelay(pdMS_TO_TICKS(170));
    g.fillRect(rx, ey, EYE_W, EYE_H, OV_BLACK);                // 右眼睁回：黑块覆盖
    vTaskDelay(pdMS_TO_TICKS(500));                            // 5) 定格
}

void init() {
    s_state = MON_IDLE;
    s_idleExpr = IDLE_NORMAL;
    idlePhaseMs = 0;
    applyExpression(true);
}

void tick(uint32_t now) {
    applyPresencePending(now);
    if (s_presence != PRES_NONE) {     // 状态牌:冻结自动、丢弃 /status、按牌设眼
        s_pending = false;
        presenceTickEyes(now);
        return;
    }
    applyPendingState(now);     // 先消费 HTTP task 投递的 /status 状态（单消费者）
    checkStatusTimeout(now);    // thinking/working 久无新事件自动回 idle

    const bool active = (s_state == MON_THINKING || s_state == MON_WORKING);
    const bool sleeping = (s_state == MON_IDLE && sleepStage >= SLEEP_ASLEEP);
    bool moodChanged = mood::update(now, active, sleeping);

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
    if (s_presence != PRES_NONE) { drawPresenceScene(now); return; }
    idleNewOverlay(now);       // idle 表情飘字/精灵(自带 active 守卫)
    happyStars(now);           // 开心眼偶发星
    tickYawnMouth(now);        // 入睡 O 嘴
    tickSleepOverlay(now);     // 睡着 Zzz + 熟睡鼻涕泡
    tickWakeFlourish(now);     // 唤醒花絮
    thinkingDotsOverlay(now);  // thinking 省略号
    alertBadgeOverlay(now);    // alert「!」角标(+告急描边)
    editCaretOverlay(now);     // working/edit 打字机书写带
    tickTicker(now);           // working 底部跑马灯
    tickCelebrate(now);        // done 庆祝星点
}

// /status 推送：仅写"待应用"缓冲，由 render task 的 tick() 消费（线程安全，见计划）。
// 在 HTTP task 线程被调，故不直接触碰 rig/eyes。
void setState(const char* s, const char* act, const char* info) {
    strncpy(s_pendS,    s    ? s    : "", sizeof(s_pendS));    s_pendS[sizeof(s_pendS)-1]       = 0;
    strncpy(s_pendAct,  act  ? act  : "", sizeof(s_pendAct));  s_pendAct[sizeof(s_pendAct)-1]   = 0;
    strncpy(s_pendInfo, info ? info : "", sizeof(s_pendInfo)); s_pendInfo[sizeof(s_pendInfo)-1] = 0;
    s_pending = true;   // 最后置旗标：消费者读到 true 时三个缓冲已就绪
}

// /presence 推送：仅写待应用缓冲，由 render task 的 tick() 消费（线程安全）。
void setPresence(const char* s) {
    strncpy(s_presPendS, s ? s : "", sizeof(s_presPendS)); s_presPendS[sizeof(s_presPendS)-1] = 0;
    s_presPending = true;
}
bool presenceActive() { return s_presence != PRES_NONE; }

} // namespace monitor
