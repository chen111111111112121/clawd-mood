#include "render_task.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eyes.hpp"

static const char *TAG = "render";

// M3 临时姿态驱动：每 4s 轮播,验证稳帧下的样式过渡。M4 用 mood/monitor 替换本函数。
static void pickDemoPose(uint32_t now)
{
    static const eyes::EyePose cycle[] = {
        eyes::POSE_NORMAL, eyes::POSE_HAPPY, eyes::POSE_HEART, eyes::POSE_CURIOUS,
    };
    static const uint8_t flags[] = {
        RIG_BLINK | RIG_SACCADE | RIG_BREATH,
        RIG_BREATH,
        0,
        RIG_BLINK,
    };
    constexpr int N = sizeof(cycle) / sizeof(cycle[0]);
    static uint32_t lastSwitch = 0;
    static int idx = 0;
    if (now - lastSwitch >= 4000) {
        idx = (idx + 1) % N;
        eyes::setPose(cycle[idx], flags[idx], false);
        lastSwitch = now;
        ESP_LOGI(TAG, "pose -> %d", idx);
    }
}

static void renderTask(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(RIG_TICK_MS);
    TickType_t lastWake = xTaskGetTickCount();

    uint32_t frames = 0;
    uint32_t fpsWindowStart = (uint32_t)(esp_timer_get_time() / 1000);

    for (;;) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);  // 单调毫秒

        pickDemoPose(now);
        eyes::tick(now);
        eyes::draw();

        // 实测 FPS：每 5s 打一行,核对稳帧
        frames++;
        if (now - fpsWindowStart >= 5000) {
            ESP_LOGI(TAG, "fps=%u", (unsigned)(frames * 1000 / (now - fpsWindowStart)));
            frames = 0;
            fpsWindowStart = now;
        }

        vTaskDelayUntil(&lastWake, period);   // 绝对周期,无累积漂移
    }
}

void render_task_start()
{
    // 栈 8KB：LovyanGFX 绘制 + LGFX_Sprite 调用链较深,留足余量(sprite 像素在堆上,不占栈)。
    // 优先级 5：高于 idle、低于后续网络 task 的常见取值,单核 C3 不绑核。
    xTaskCreate(renderTask, "render", 8192, nullptr, 5, nullptr);
}
