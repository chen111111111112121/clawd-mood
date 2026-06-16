#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.hpp"
#include "eyes.hpp"

static const char *TAG = "clawd";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Clawd Mood IDF — M2 eyes demo");
    display::init();
    eyes::init();   // 清屏品牌橙 + snap 到 POSE_NORMAL(眨眼/扫视/呼吸)

    // 轮播姿态：验证 RECT/ARC/HEART/CURIOUS 渲染器 + 样式过渡 + 弹簧平滑
    static const eyes::EyePose cycle[] = {
        eyes::POSE_NORMAL, eyes::POSE_HAPPY, eyes::POSE_HEART, eyes::POSE_CURIOUS,
    };
    static const uint8_t flags[] = {
        RIG_BLINK | RIG_SACCADE | RIG_BREATH,   // NORMAL
        RIG_BREATH,                              // HAPPY(arc)
        0,                                       // HEART
        RIG_BLINK,                               // CURIOUS
    };
    const int N = sizeof(cycle) / sizeof(cycle[0]);

    uint32_t lastSwitch = 0;
    int idx = 0;
    while (true) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);  // 单调毫秒

        if (now - lastSwitch >= 4000) {        // 每 4s 换一个姿态(经弹簧平滑过渡)
            idx = (idx + 1) % N;
            eyes::setPose(cycle[idx], flags[idx], false);
            lastSwitch = now;
            ESP_LOGI(TAG, "pose -> %d", idx);
        }

        eyes::tick(now);
        eyes::draw();
        vTaskDelay(pdMS_TO_TICKS(RIG_TICK_MS));  // ~30fps
    }
}
