#include "eyes.hpp"
#include "eyes_internal.hpp"
#include "display.hpp"

namespace eyes {

// 本帧 drawRig 是否清过表情区（供覆盖层 zoneClearedThisFrame 查询）
static bool s_zoneCleared = false;

// ── 眨眼挤压表（drawRig 用）──
const uint8_t BLINK_H_PCT[BLINK_FRAMES] = {106, 60, 8, 8, 70, 104, 100};
const uint8_t BLINK_W_PCT[BLINK_FRAMES] = { 97, 105, 118, 118, 103, 98, 100};

// ── 位图字模 ──
static const uint8_t HEART5[5] = { 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 };
static const uint8_t STAR7[7]  = { 0b0001000, 0b0011100, 0b1111111, 0b0111110, 0b0011100, 0b0010100, 0b0100010 };

// ── 线条/像素眼型绘制器 ──
static void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                        uint8_t thk, bool rightFacing, uint16_t col) {
    auto& g = display::gfx();
    for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
        if (rightFacing) {
            g.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
            g.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
        } else {
            g.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
            g.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
        }
    }
}

static void drawHeartAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
    auto& g = display::gfx();
    for (int8_t row = 0; row < 5; row++)
        for (int8_t c = 0; c < 5; c++)
            if (HEART5[row] & (1 << (4 - c)))
                g.fillRect(cx + (c - 2) * scale, cy + (row - 2) * scale, scale, scale, col);
}

static void drawStarAt(int16_t cx, int16_t cy, uint8_t scale, uint16_t col) {
    auto& g = display::gfx();
    for (int8_t row = 0; row < 7; row++)
        for (int8_t c = 0; c < 7; c++)
            if (STAR7[row] & (1 << (6 - c)))
                g.fillRect(cx + (c - 3) * scale, cy + (row - 3) * scale, scale, scale, col);
}

static void drawHappyArc(int16_t cx, int16_t cy, int16_t w, uint16_t col) {
    auto& g = display::gfx();
    const int16_t hw = w / 2;
    for (uint8_t t = 0; t < 4; t++) {
        g.drawLine(cx - hw, cy + t,         cx,      cy - hw / 2 + t, col);
        g.drawLine(cx,      cy - hw / 2 + t, cx + hw, cy + t,         col);
    }
}

// 位图眼：内存里拼好"底色+图形"一次性推屏，每像素只写一次终色，消除脉动闪烁。
// 旧版用 GFXcanvas16 + drawRGBBitmap；此处换 LovyanGFX 的 LGFX_Sprite + pushSprite。
static void blitBitmapEye(const uint8_t* bmp, uint8_t n, int16_t cx, int16_t cy, uint8_t scale,
                          uint16_t fg, uint16_t bg, EyeRect& out) {
    auto& g = display::gfx();
    const int16_t W = (int16_t)n * scale, H = (int16_t)n * scale;
    const int16_t x0 = cx - (n / 2) * scale, y0 = cy - (n / 2) * scale;
    lgfx::LGFX_Sprite cv(&g);
    cv.setColorDepth(16);
    cv.createSprite(W, H);
    cv.fillScreen(bg);
    for (uint8_t row = 0; row < n; row++)
        for (uint8_t c = 0; c < n; c++)
            if (bmp[row] & (1 << (n - 1 - c)))
                cv.fillRect(c * scale, row * scale, scale, scale, fg);
    cv.pushSprite(x0, y0);
    cv.deleteSprite();
    out = {x0, y0, W, H, true};
}

// ── 单眼绘制：按 drawnStyle 分派 ──（col 传 s_bgColor 即"按字形精确擦除"）
void drawRigEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t lid,
                bool rightFacing, uint16_t col, EyeRect& out) {
    auto& g = display::gfx();
    switch (rig.drawnStyle) {
        case STYLE_SLEEP: {
            const int16_t hw = w / 2, dip = 7;
            for (uint8_t t = 0; t < 5; t++) {
                g.drawLine(cx - hw, cy - dip / 2 + t, cx,      cy + dip + t,     col);
                g.drawLine(cx,      cy + dip + t,     cx + hw, cy - dip / 2 + t, col);
            }
            out = {(int16_t)(cx - hw - 2), (int16_t)(cy - dip / 2 - 2),
                   (int16_t)(w + 4), (int16_t)(dip + 12), true};
            break;
        }
        case STYLE_CHEVRON: {
            const int16_t arm = ((h / 2) * (240 - lid)) / 240;
            if (arm <= 3) {
                g.fillRect(cx - w / 2, cy - 4, w, 8, col);
                out = {(int16_t)(cx - w / 2), (int16_t)(cy - 4), w, 8, true};
            } else {
                drawChevron(cx, cy, arm, w, 10, rightFacing, col);
                out = {(int16_t)(cx - w / 2 - 2), (int16_t)(cy - arm - 12),
                       (int16_t)(w + 4), (int16_t)(arm * 2 + 24), true};
            }
            break;
        }
        case STYLE_ARC:
            drawHappyArc(cx, cy, w, col);
            out = {(int16_t)(cx - w / 2), (int16_t)(cy - w / 4 - 2),
                   (int16_t)(w + 2), (int16_t)(w / 4 + 8), true};
            break;
        case STYLE_HEART: {
            uint8_t scale = (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w));
            drawHeartAt(cx, cy, scale, col);
            out = {(int16_t)(cx - scale * 3), (int16_t)(cy - scale * 3),
                   (int16_t)(scale * 6), (int16_t)(scale * 6), true};
            break;
        }
        case STYLE_STAR: {
            uint8_t scale = (uint8_t)((w < 2) ? 2 : ((w > 7) ? 7 : w));
            drawStarAt(cx, cy, scale, col);
            out = {(int16_t)(cx - scale * 4), (int16_t)(cy - scale * 4),
                   (int16_t)(scale * 8), (int16_t)(scale * 8), true};
            break;
        }
        default: {  // STYLE_RECT — 眼睑自上而下
            int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
            if (vis < 5) vis = 5;
            const int16_t top = cy - h / 2 + (h - vis);
            g.fillRect(cx - w / 2, top, w, vis, col);
            out = {(int16_t)(cx - w / 2), top, w, vis, true};
            break;
        }
    }
}

// 擦除旧矩形中未被新矩形覆盖的边条（黑色主体直接覆盖，避免闪烁）。
static void eraseRectOutside(const EyeRect& p, int16_t nx, int16_t ny, int16_t nw, int16_t nh) {
    auto& g = display::gfx();
    if (!p.valid) return;
    const int16_t px2 = p.x + p.w, py2 = p.y + p.h;
    const int16_t nx2 = nx + nw, ny2 = ny + nh;
    if (nx >= px2 || nx2 <= p.x || ny >= py2 || ny2 <= p.y) {
        g.fillRect(p.x, p.y, p.w, p.h, s_bgColor);
        return;
    }
    if (ny > p.y)  g.fillRect(p.x, p.y, p.w, ny - p.y, s_bgColor);
    if (py2 > ny2) g.fillRect(p.x, ny2, p.w, py2 - ny2, s_bgColor);
    const int16_t iy = (p.y > ny) ? p.y : ny;
    const int16_t ih = ((py2 < ny2) ? py2 : ny2) - iy;
    if (ih > 0) {
        if (nx > p.x)  g.fillRect(p.x, iy, nx - p.x, ih, s_bgColor);
        if (px2 > nx2) g.fillRect(nx2, iy, px2 - nx2, ih, s_bgColor);
    }
}

// ── 整窝增量重绘 ──（WINK 已解耦为 s_winkRight）
void drawRig() {
    auto& g = display::gfx();
    s_zoneCleared = false;
    int16_t ox  = rig.ox.cur >> 8;
    int16_t oy  = (rig.oy.cur >> 8) + rigBreathOffset();
    int16_t w   = rig.w.cur >> 8;   if (w < 4) w = 4;
    int16_t h   = rig.h.cur >> 8;   if (h < 4) h = 4;
    int16_t lid = rig.lid.cur >> 8; if (lid < 0) lid = 0; if (lid > 240) lid = 240;

    if (rig.blinkFrame > 0) {       // 挤压拉伸眨眼，叠加在眼睑之上
        h = (int16_t)((int32_t)h * BLINK_H_PCT[rig.blinkFrame - 1] / 100);
        w = (int16_t)((int32_t)w * BLINK_W_PCT[rig.blinkFrame - 1] / 100);
        if (h < 4) h = 4;
    }

    static int16_t lastOx = -32768, lastOy = 0, lastW = 0, lastH = 0, lastLid = 0;
    static uint8_t lastStyle = 255;
    static bool    lastWink  = false;

    const bool winkNow = s_winkRight;
    const bool unchanged = !rig.zoneDirty && rig.prevValid &&
        ox == lastOx && oy == lastOy && w == lastW && h == lastH &&
        lid == lastLid && (uint8_t)rig.drawnStyle == lastStyle && winkNow == lastWink;
    if (unchanged) return;

    if (rig.zoneDirty) {
        g.fillRect(0, EXPR_ZONE_Y, DISP_W, EXPR_ZONE_H, s_bgColor);
        rig.prevValid = false;
        rig.zoneDirty = false;
        s_zoneCleared = true;
    }

    const int16_t cy = eyeCY() + oy;

    if (rig.drawnStyle == STYLE_RECT) {
        int16_t vis = (int16_t)((int32_t)h * (240 - lid) / 240);
        if (vis < 5) vis = 5;
        int16_t top = cy - h / 2 + (h - vis);
        int16_t visL = vis, topL = top, visR = vis, topR = top;
        if (winkNow) { visR = 10; topR = cy - 5; }   // 右眼眯成横条
        const int16_t lx = rigLCX(ox) - w / 2;
        const int16_t rx = rigRCX(ox) - w / 2;
        if (rig.prevValid) {
            eraseRectOutside(rig.prevL, lx, topL, w, visL);
            eraseRectOutside(rig.prevR, rx, topR, w, visR);
        }
        g.fillRect(lx, topL, w, visL, C_BLACK);
        g.fillRect(rx, topR, w, visR, C_BLACK);
        rig.prevL = {lx, topL, w, visL, true};
        rig.prevR = {rx, topR, w, visR, true};
    } else if (rig.drawnStyle == STYLE_HEART || rig.drawnStyle == STYLE_STAR) {
        const uint8_t  n   = (rig.drawnStyle == STYLE_HEART) ? 5 : 7;
        const uint8_t* bmp = (rig.drawnStyle == STYLE_HEART) ? HEART5 : STAR7;
        const uint8_t  scale = (rig.drawnStyle == STYLE_HEART)
                                 ? (uint8_t)((w < 3) ? 3 : ((w > 9) ? 9 : w))
                                 : (uint8_t)((w < 2) ? 2 : ((w > 7) ? 7 : w));
        const int16_t W = (int16_t)n * scale;
        const int16_t lx0 = rigLCX(ox) - (n / 2) * scale, rx0 = rigRCX(ox) - (n / 2) * scale;
        const int16_t y0  = cy - (n / 2) * scale;
        if (rig.prevValid) {
            eraseRectOutside(rig.prevL, lx0, y0, W, W);
            eraseRectOutside(rig.prevR, rx0, y0, W, W);
        }
        blitBitmapEye(bmp, n, rigLCX(ox), cy, scale, C_BLACK, s_bgColor, rig.prevL);
        blitBitmapEye(bmp, n, rigRCX(ox), cy, scale, C_BLACK, s_bgColor, rig.prevR);
    } else {
        EyeRect dump;
        if (rig.prevValid && lastStyle == (uint8_t)rig.drawnStyle && lastOx != -32768) {
            const int16_t pcy = eyeCY() + lastOy;
            drawRigEye(rigLCX(lastOx), pcy, lastW, lastH, lastLid, true,  s_bgColor, dump);
            drawRigEye(rigRCX(lastOx), pcy, lastW, lastH, lastLid, false, s_bgColor, dump);
        } else if (rig.prevValid) {
            g.fillRect(rig.prevL.x - 2, rig.prevL.y - 2, rig.prevL.w + 4, rig.prevL.h + 4, s_bgColor);
            g.fillRect(rig.prevR.x - 2, rig.prevR.y - 2, rig.prevR.w + 4, rig.prevR.h + 4, s_bgColor);
        }
        drawRigEye(rigLCX(ox), cy, w, h, lid, true,  C_BLACK, rig.prevL);
        drawRigEye(rigRCX(ox), cy, w, h, lid, false, C_BLACK, rig.prevR);
    }
    rig.prevValid = true;

    lastOx = ox; lastOy = oy; lastW = w; lastH = h; lastLid = lid;
    lastStyle = (uint8_t)rig.drawnStyle;
    lastWink = winkNow;
}

// ── 公开 API ──
void draw() { drawRig(); }

bool zoneClearedThisFrame() { return s_zoneCleared; }

} // namespace eyes
