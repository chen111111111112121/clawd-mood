#pragma once
#include <LovyanGFX.hpp>

// 显示子系统对外接口。内部的 ST7789/SPI 配置对调用方完全隐藏，
// 调用方只通过 gfx() 拿到绘制设备（LovyanGFX 基类，含所有绘图方法）。
namespace display {

// 初始化 SPI + 面板，并点亮背光。app 启动时调用一次。
void init();

// 取绘制设备。eyes 等组件用它画图：display::gfx().fillScreen(...) 等。
lgfx::LGFX_Device &gfx();

// 背光开关（true=亮，false=灭）。
void backlight(bool on);

} // namespace display
