#include "eyes.hpp"
#include "eyes_internal.hpp"
#include "display.hpp"

namespace eyes {

// ── 模块状态定义 ──
EyeRig   rig         = {};
uint16_t s_bgColor   = 0xD880;   // 品牌橙 color565(218,17,0)
bool     s_winkRight = false;
uint8_t  s_speed     = 2;

const int8_t BREATH_TAB[16] = {0,1,1,2,2,2,1,1,0,-1,-1,-2,-2,-2,-1,-1};

// ── 弹簧物理（8.8 定点）──
int16_t rigK() {
    if (s_speed == 3) return 72;
    if (s_speed == 1) return 30;
    return 48;
}

void springTo(Spring& s, int32_t target) {
    s.vel += ((target - s.cur) * rigK()) >> 8;
    s.vel  = (s.vel * RIG_DAMP) >> 8;
    s.cur += s.vel;
}

void springSnap(Spring& s, int32_t target) { s.cur = target; s.vel = 0; }

int16_t rigBreathOffset() {
    if (!(rig.flags & (RIG_BREATH | RIG_BREATH2))) return 0;
    int16_t v = BREATH_TAB[(rig.breathPhase >> 8) & 15];
    if (rig.flags & RIG_BREATH2) v *= 2;
    return v;
}

// ── 姿态 API（内部）──
static void rigInvalidate() {
    rig.prevValid = false;
    rig.zoneDirty = true;
}

static void rigSnapPose(const EyePose& p, uint8_t flags) {
    rig.pose = p; rig.flags = flags; rig.trans = 0;
    springSnap(rig.ox,  (int32_t)p.ox  << 8);
    springSnap(rig.oy,  (int32_t)p.oy  << 8);
    springSnap(rig.w,   (int32_t)p.w   << 8);
    springSnap(rig.h,   (int32_t)p.h   << 8);
    springSnap(rig.lid, (int32_t)p.lid << 8);
    rig.drawnStyle = p.style;
    rig.blinkFrame = 0;
    rigInvalidate();
}

static void rigSetPose(const EyePose& p, uint8_t flags) {
    if (p.style != rig.drawnStyle) {
        rig.trans = 1;                 // 闭眼→换样式→睁眼
        rig.transNext = p;
        rig.transFlagsNext = flags;
        return;
    }
    if (rig.trans == 1) rig.trans = 0; // 过渡中改回同样式:取消闭眼
    rig.pose = p;
    rig.flags = flags;
}

// ── tick：推进一帧 ──
void tick(uint32_t now) {
    // 样式过渡
    if (rig.trans == 1) {
        springTo(rig.lid, (int32_t)240 << 8);
        if ((rig.lid.cur >> 8) >= 225) {
            rig.pose  = rig.transNext;
            rig.flags = rig.transFlagsNext;
            springSnap(rig.ox, (int32_t)rig.pose.ox << 8);
            springSnap(rig.oy, (int32_t)rig.pose.oy << 8);
            springSnap(rig.w,  (int32_t)rig.pose.w  << 8);
            springSnap(rig.h,  (int32_t)rig.pose.h  << 8);
            rig.drawnStyle = rig.pose.style;
            rig.zoneDirty = true;
            rig.trans = 2;
        }
    } else {
        if (rig.trans == 2 && (rig.lid.cur >> 8) <= rig.pose.lid + 12) rig.trans = 0;
        springTo(rig.lid, (int32_t)rig.pose.lid << 8);
    }

    // 微扫视
    if ((rig.flags & RIG_SACCADE) && rig.trans == 0) {
        if (now >= rig.nextSaccadeMs) {
            rig.sacX = (int16_t)rrand(-3, 4);
            rig.nextSaccadeMs = now + 700 + rrand(1300);
        }
    } else {
        rig.sacX = 0;
    }

    springTo(rig.ox, ((int32_t)(rig.pose.ox + rig.sacX)) << 8);
    springTo(rig.oy, (int32_t)rig.pose.oy << 8);
    springTo(rig.w,  (int32_t)rig.pose.w  << 8);
    springTo(rig.h,  (int32_t)rig.pose.h  << 8);

    // 眨眼
    if (rig.blinkFrame > 0) {
        rig.blinkFrame++;
        if (rig.blinkFrame > BLINK_FRAMES) {
            if (rig.blinkAgain) { rig.blinkFrame = 1; rig.blinkAgain = false; }
            else rig.blinkFrame = 0;
        }
    } else if ((rig.flags & RIG_BLINK) && rig.trans == 0 && now >= rig.nextBlinkMs) {
        rig.blinkFrame = 1;
        rig.blinkAgain = (rrand(100) < 12);
        rig.nextBlinkMs = now + 2500 + rrand(3500);
    }

    // 呼吸
    if (rig.flags & (RIG_BREATH | RIG_BREATH2)) {
        rig.breathPhase += (s_speed == 1) ? 26 : ((s_speed == 3) ? 60 : 40);
    }
}

// ── 公开 API ──
void init() {
    display::gfx().fillScreen(s_bgColor);
    rigSnapPose(POSE_NORMAL, RIG_BLINK | RIG_SACCADE | RIG_BREATH);
}

void setPose(const EyePose& p, uint8_t flags, bool snap) {
    if (snap) rigSnapPose(p, flags);
    else      rigSetPose(p, flags);
}

void setWinkRight(bool on)      { s_winkRight = on; }
void setBgColor(uint16_t color) { s_bgColor = color; }
void setSpeed(uint8_t s)        { s_speed = s; }
bool inTransition()             { return rig.trans != 0; }

void scriptPose(const EyePose& p, uint8_t flags) {
    rig.pose = p; rig.flags = flags; rig.drawnStyle = p.style; rig.trans = 0;
    springSnap(rig.ox,  (int32_t)p.ox  << 8);
    springSnap(rig.oy,  (int32_t)p.oy  << 8);
    springSnap(rig.w,   (int32_t)p.w   << 8);
    springSnap(rig.h,   (int32_t)p.h   << 8);
    springSnap(rig.lid, (int32_t)p.lid << 8);
}

} // namespace eyes
