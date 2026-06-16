#include "display.hpp"

// ── ST7789 240×240 面板配置（引脚来自旧 Arduino 版）──────────────
// SCK=8 MOSI=9 MISO=-1 DC=1 CS=4 RST=2 BL=3，SPI2_HOST。
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX()
    {
        {   // SPI 总线
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;   // 40MHz，首次点屏稳妥；通了可提到 80MHz
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = 8;
            cfg.pin_mosi   = 9;
            cfg.pin_miso   = -1;
            cfg.pin_dc     = 1;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // 面板
            auto cfg = _panel.config();
            cfg.pin_cs        = 4;
            cfg.pin_rst       = 2;
            cfg.pin_busy      = -1;
            cfg.panel_width   = 240;
            cfg.panel_height  = 240;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.readable      = false;   // 无 MISO
            cfg.invert        = true;    // 多数 240×240 ST7789 IPS 需反色
            cfg.rgb_order     = false;
            _panel.config(cfg);
        }
        {   // 背光（PWM）
            auto cfg = _light.config();
            cfg.pin_bl      = 3;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static LGFX s_lcd;   // 唯一实例

namespace display {

void init()
{
    s_lcd.init();
    s_lcd.setRotation(3);   // 真机实测：rot0=顶部内容跑到右侧, rot1=跑到下方, rot3=正确朝向(顶部在顶部)。LovyanGFX 的 rotation 编号与 Adafruit 不同(旧 Arduino 版是 1)
    s_lcd.setBrightness(255);
    s_lcd.fillScreen(0x0000);   // 上电先清黑
}

lgfx::LGFX_Device &gfx() { return s_lcd; }

void backlight(bool on) { s_lcd.setBrightness(on ? 255 : 0); }

} // namespace display
