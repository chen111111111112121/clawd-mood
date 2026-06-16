#pragma once
#include <stdint.h>

namespace mood {

// 心情（只在 idle 体现，决定轮播哪组表情）
enum Mood : uint8_t { FOCUSED = 0, TIRED = 1, CHEERFUL = 2, COZY = 3 };

// 从 NVS 读回精力/心情（须先 nvs_flash_init()）。开机调一次。
void init();

// 每 ~2s 推进两轴并判定心情。active=thinking/working（耗精力）；sleeping=熟睡（快回血，M4b 用，M4a 传 false）。
// 返回 true 表示 currentMood 本次发生变化（调用方可据此让 idle 立刻换组）。
bool update(uint32_t nowMs, bool active, bool sleeping);

Mood current();

// done 事件加 joy（M6 monitor 收到 done 时调；M4a 暂不触发）。
void onDone();

// 由心情挑一个"非普通且有精神"的 idle 表情（返回 IDLE_* 值，见 monitor）。
uint8_t pickAwakeExpr();

// 当前精力/心情（0..100），供日志/调试。
float energy();
float joy();

// 困意（0..100）：idle 累积，精力越低涨越快；活动/睡眠期不累积。
float sleepiness();
void  resetSleepiness();   // 唤醒/有活动时清零

} // namespace mood
