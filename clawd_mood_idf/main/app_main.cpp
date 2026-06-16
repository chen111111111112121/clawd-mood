#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.hpp"

static const char *TAG = "clawd";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Clawd Mood IDF — M1 primitives self-test");
    display::init();

    auto &g = display::gfx();
    g.fillScreen(g.color565(20, 22, 28));   // 深底

    // 1) 画线：白色对角十字
    g.drawLine(0, 0, 239, 239, g.color565(255, 255, 255));
    g.drawLine(239, 0, 0, 239, g.color565(255, 255, 255));

    // 2) 圆角矩形（填充）：橙红，居中
    g.fillRoundRect(70, 70, 100, 100, 18, g.color565(220, 17, 0));

    // 3) 文字
    g.setTextColor(g.color565(255, 255, 255));
    g.setTextSize(2);
    g.setCursor(60, 12);
    g.print("M1 OK");

    ESP_LOGI(TAG, "self-test drawn");

    // 4) 背光开关自检：灭 1.5s → 亮，循环
    bool on = true;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        on = !on;
        display::backlight(on);
        ESP_LOGI(TAG, "backlight %s", on ? "ON" : "OFF");
    }
}
