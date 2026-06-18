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

// 开机问候动画（阻塞 ~2.3s，纯 display 直绘）。须在 display::init() 之后、eyes::init() 之前调。
void bootGreeting();

// ── 开机/配网信息屏（移植自 .ino VIEW_BOOTINFO / drawWifiScreen）──
// 在屏上显示 AP 名/密码/控制器地址(192.168.4.1) + 家庭 WiFi 连接状态，引导用户配网。
// WiFi 状态由调用方(main, 读 netsvc)传入，故 monitor 不依赖 netsvc(避免与 netsvc→monitor 成环)。
//
// 进入信息屏:已连上家庭 WiFi→显示绿色 IP，3s 后自动进表情；未连上→常驻等配网。
// now 为单调毫秒(= render task 的时基)。http_start() 后调一次。
void enterBootInfo(uint32_t now, bool staConnected, const char* staIp);

// 信息屏期间每帧推进(由 render task 在 eyes 之前调)：
//   - 收到 /status 或 /presence 推送 → 立即离屏进 Monitor；
//   - 后台 STA 连上/掉线 → 刷新信息屏(连上则给 3s 确认倒计时)；
//   - 确认倒计时到 → 自动离屏进 Monitor。
// 仍在信息屏时调用方应跳过 eyes 绘制(见 bootInfoActive)。
void tickBootInfo(uint32_t now, bool staConnected, const char* staIp);

// 当前是否处于信息屏(true 时 render task 跳过 eyes::tick/draw/drawOverlays)。
bool bootInfoActive();

// 初始化：snap 到普通表情、起 idle 轮播。须在 eyes::init() 与 mood::init() 之后调。
void init();

// 每帧推进：更新 mood、idle 轮播、行为脚本，并把姿态推给 eyes。由渲染 task 调。
void tick(uint32_t nowMs);

// 在 eyes::draw() 之后调：画 idle 表情装饰 + 睡眠 Zzz/鼻涕泡/O 嘴/唤醒花絮等覆盖层。
void drawOverlays(uint32_t nowMs);

// /status 状态推送：thinking/working/done/alert/idle/offline。线程安全（仅写待应用缓冲，
// 由 render task 的 tick() 消费），故可在 HTTP task 线程直接调。act/info 可为 nullptr/""。
void setState(const char* s, const char* act, const char* info);

// 手动状态牌:s ∈ {auto,meeting,toilet,solder,rest}。线程安全(仅写 pending,render task 消费)。
void setPresence(const char* s);
bool presenceActive();   // 当前是否在状态牌(≠auto)

} // namespace monitor
