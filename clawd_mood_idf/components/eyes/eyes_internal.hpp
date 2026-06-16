#pragma once
#include "eyes_pose.hpp"
#include "esp_random.h"

namespace eyes {

// ── 屏幕/表情区布局 ──
constexpr int16_t  DISP_W      = 240;
constexpr int16_t  DISP_H      = 240;
constexpr int16_t  EXPR_ZONE_Y = 28;
constexpr int16_t  EXPR_ZONE_H = 185;
constexpr uint16_t C_BLACK     = 0x0000;

// ── rig 调参 ──（RIG_TICK_MS 在公开头 eyes_pose.hpp，供调用方循环节拍）
constexpr int32_t  RIG_DAMP     = 196;   // 速度阻尼(/256)
constexpr uint8_t  BLINK_FRAMES = 7;

extern const uint8_t BLINK_H_PCT[BLINK_FRAMES];
extern const uint8_t BLINK_W_PCT[BLINK_FRAMES];
extern const int8_t  BREATH_TAB[16];

struct EyeRect { int16_t x, y, w, h; bool valid; };
struct Spring  { int32_t cur, vel; };   // 8.8 定点

struct EyeRig {
    EyePose  pose;            // 目标姿态
    uint8_t  flags;
    EyeStyle drawnStyle;      // 当前屏上样式
    Spring   ox, oy, w, h, lid;
    uint32_t nextBlinkMs, nextSaccadeMs;
    uint8_t  blinkFrame;      // 0=未眨眼,1..BLINK_FRAMES=进行中
    bool     blinkAgain;
    int16_t  sacX;
    uint16_t breathPhase;     // 8.8 相位
    EyeRect  prevL, prevR;
    bool     prevValid;
    bool     zoneDirty;       // 需整区清屏(样式切换等)
    uint8_t  trans;           // 0=无 1=闭眼中 2=睁眼中
    EyePose  transNext;
    uint8_t  transFlagsNext;
};

// 模块状态（定义在 eyes_rig.cpp）
extern EyeRig   rig;
extern uint16_t s_bgColor;    // 默认 0xD880(品牌橙)
extern bool     s_winkRight;  // 默认 false
extern uint8_t  s_speed;      // 默认 2

// ── 坐标助手 ──
inline int16_t eyeLX(int16_t ox) { return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox; }
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }
inline int16_t rigLCX(int16_t ox){ return eyeLX(ox) + EYE_W / 2; }
inline int16_t rigRCX(int16_t ox){ return eyeRX(ox) + EYE_W / 2; }

// ── 硬件 RNG：random(n)→[0,n)；random(a,b)→[a,b) ──
inline int32_t rrand(int32_t n)            { return n > 0 ? (int32_t)(esp_random() % (uint32_t)n) : 0; }
inline int32_t rrand(int32_t a, int32_t b) { return a + rrand(b - a); }

// ── 跨文件内部函数 ──
int16_t rigK();                                   // eyes_rig.cpp
void    springTo(Spring& s, int32_t target);      // eyes_rig.cpp
void    springSnap(Spring& s, int32_t target);    // eyes_rig.cpp
int16_t rigBreathOffset();                         // eyes_rig.cpp
void    drawRig();                                 // eyes_render.cpp
void    drawRigEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t lid,
                   bool rightFacing, uint16_t col, EyeRect& out);  // eyes_render.cpp

} // namespace eyes
