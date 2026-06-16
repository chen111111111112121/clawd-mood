#pragma once
#include <stdint.h>

// ── 眼睛几何常量（来自旧 Arduino 版，姿态预设据此参数化）──
#define EYE_W       30
#define EYE_H       60
#define EYE_GAP     120
#define EYE_OX      0
#define EYE_OY      40
#define SCAN_EYE_W  20

// ── 行为标志（setPose 的 flags 参数，按位或）──
#define RIG_BLINK   0x01
#define RIG_SACCADE 0x02
#define RIG_BREATH  0x04
#define RIG_BREATH2 0x08   // 双倍呼吸幅度（困倦）

// 推荐 tick/重绘间隔（ms）≈30fps。调用方循环节拍用它。
#define RIG_TICK_MS 33

namespace eyes {

enum EyeStyle : uint8_t { STYLE_RECT, STYLE_CHEVRON, STYLE_ARC, STYLE_HEART, STYLE_STAR, STYLE_SLEEP };

struct EyePose {
    EyeStyle style;
    int16_t  ox, oy;   // 眼对偏移
    int16_t  w, h;     // RECT:眼宽高 | CHEVRON:w=reach,h=2*arm | ARC:w=弧宽 | HEART/STAR:w=scale
    uint8_t  lid;      // 眼睑 0=全开 .. 240=全闭
};

// ── 姿态预设（M4 monitor/mood 据状态选择；M2 演示用其中几个）──
constexpr EyePose POSE_NORMAL    = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
constexpr EyePose POSE_SLEEPY    = {STYLE_RECT,    0,  8, EYE_W, EYE_H, 170};
constexpr EyePose POSE_HEART     = {STYLE_HEART,   0,  0, 6, 6, 0};
constexpr EyePose POSE_HAPPY     = {STYLE_ARC,     0,  8, 30, 30, 0};
constexpr EyePose POSE_THINK     = {STYLE_RECT,    0, 10, EYE_W, 30, 0};
constexpr EyePose POSE_SCAN      = {STYLE_RECT,    0,  0, SCAN_EYE_W, EYE_H, 0};
constexpr EyePose POSE_READ      = {STYLE_RECT,    0, 26, 26, 16, 0};
constexpr EyePose POSE_EDIT      = {STYLE_RECT,  -13, 12, 20, 34, 40};
constexpr EyePose POSE_RUN       = {STYLE_RECT,    0,  0, 14, 44, 0};
constexpr EyePose POSE_NET       = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
constexpr EyePose POSE_CURIOUS   = {STYLE_RECT,    0,  0, 28, 54, 0};
constexpr EyePose POSE_WINK      = {STYLE_RECT,    0,  0, EYE_W, 56, 0};
constexpr EyePose POSE_SPARKLE   = {STYLE_STAR,    0,  0, 7, 7, 0};
constexpr EyePose POSE_SURPRISED = {STYLE_RECT,    0,  0, EYE_W, EYE_H, 0};
constexpr EyePose POSE_SHY       = {STYLE_RECT,    0,  6, 24, 42, 30};
constexpr EyePose POSE_DIZZY     = {STYLE_RECT,    0,  0, 26, 40, 60};
constexpr EyePose POSE_MUSIC     = {STYLE_ARC,     0,  8, 30, 30, 0};
constexpr EyePose POSE_YAWN      = {STYLE_RECT,    0,  0, EYE_W, 56, 0};
constexpr EyePose POSE_DOZE      = {STYLE_SLEEP,   0, 10, EYE_W, 10, 0};
constexpr EyePose POSE_DEEPSLEEP = {STYLE_SLEEP,   0, 16, EYE_W, 10, 0};
constexpr EyePose POSE_SHUT      = {STYLE_RECT,    0,  6, EYE_W, EYE_H, 235};
constexpr EyePose POSE_LOVE      = {STYLE_HEART,   0,  0, 6, 6, 0};
constexpr EyePose POSE_GIGGLE    = {STYLE_ARC,     0,  8, 30, 30, 0};

} // namespace eyes
