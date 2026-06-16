#pragma once
#include <stdint.h>

// Monitor 状态（M4a 恒为 IDLE；thinking/working/done/alert 由 M6 的 /status 驱动）
#define MON_IDLE     0
#define MON_THINKING 1
#define MON_WORKING  2
#define MON_DONE     3
#define MON_ALERT    4
#define MON_OFFLINE  5

// idle 表情（数值需与 mood::pickAwakeExpr 返回值一致）
#define IDLE_NORMAL    0
#define IDLE_SLEEPY    1
#define IDLE_HEART     2
#define IDLE_HAPPY     3
#define IDLE_CURIOUS   4
#define IDLE_WINK      5
#define IDLE_SPARKLE   6
#define IDLE_SURPRISED 7
#define IDLE_SHY       8
#define IDLE_DIZZY     9
#define IDLE_MUSIC     10
#define IDLE_YAWN      11
#define IDLE_LOVE      12
#define IDLE_GIGGLE    13

namespace monitor {

// 初始化：snap 到普通表情、起 idle 轮播。须在 eyes::init() 与 mood::init() 之后调。
void init();

// 每帧推进：更新 mood、idle 轮播、行为脚本，并把姿态推给 eyes。由渲染 task 调。
void tick(uint32_t nowMs);

// 在 eyes::draw() 之后调：画 idle 表情装饰 + 睡眠 Zzz/鼻涕泡/O 嘴/唤醒花絮等覆盖层。
void drawOverlays(uint32_t nowMs);

// /status 状态推送：thinking/working/done/alert/idle/offline。线程安全（仅写待应用缓冲，
// 由 render task 的 tick() 消费），故可在 HTTP task 线程直接调。act/info 可为 nullptr/""。
void setState(const char* s, const char* act, const char* info);

} // namespace monitor
