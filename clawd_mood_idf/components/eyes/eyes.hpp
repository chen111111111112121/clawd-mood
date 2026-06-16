#pragma once
#include "eyes_pose.hpp"

namespace eyes {

// 初始化：清屏为底色 + snap 到 POSE_NORMAL（含眨眼/扫视/呼吸）。启动时调一次。
void init();

// 设定目标姿态。snap=true 立刻到位（开机/强制）；false 经弹簧平滑（含换样式的闭眼→睁眼过渡）。
void setPose(const EyePose& p, uint8_t flags, bool snap = false);

// 推进一帧状态（弹簧/眨眼/扫视/呼吸/样式过渡）。nowMs 为单调毫秒时基。每 ~RIG_TICK_MS 调一次。
void tick(uint32_t nowMs);

// 把当前状态增量重绘到屏（无变化帧自动整帧跳过）。
void draw();

// WINK 表情：右眼眯成横条（M4 在 WINK idle 表情时按行为脚本开关）。默认 false。
void setWinkRight(bool on);

// 表情区底色（=Clawd 脸色）。默认品牌橙 0xD880。
void setBgColor(uint16_t color565);

// 动画速度档：1=慢 2=正常(默认) 3=快。
void setSpeed(uint8_t s);

// 是否正处于样式切换的闭眼→睁眼过渡中（行为脚本据此暂停推姿态）。
bool inTransition();

// 逐帧直驱：把目标设为 p 并立刻 snap 全部弹簧到位（不触发整区清屏、不清眨眼）。
// 给睡眠等"精确逐帧曲线"用——每帧调用，画面即等于 p；与 setPose(snap=true) 的区别是不置 zoneDirty。
void scriptPose(const EyePose& p, uint8_t flags);

// 本帧 draw() 是否做了整区清屏（样式切换等）。覆盖层据此强制重画自己被抹掉的装饰。
bool zoneClearedThisFrame();

} // namespace eyes
