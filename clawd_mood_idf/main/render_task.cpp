#include "render_task.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eyes.hpp"
#include "monitor.hpp"
#include "netsvc.hpp"

static const char *TAG = "render";

static void renderTask(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(RIG_TICK_MS);
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t frames = 0;
    uint32_t fpsWindowStart = (uint32_t)(esp_timer_get_time() / 1000);

    for (;;) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        // 开机/配网信息屏:仍在屏时占用整屏,跳过 eyes 渲染(状态变化由 tickBootInfo 自刷新)
        monitor::tickBootInfo(now, netsvc::sta_connected(), netsvc::sta_ip());
        if (monitor::bootInfoActive()) {
            frames++;
            vTaskDelayUntil(&lastWake, period);
            continue;
        }

        monitor::tick(now);   // 推进 mood/idle 轮播/行为脚本/睡眠 → eyes
        eyes::tick(now);
        eyes::draw();
        monitor::drawOverlays(now);   // 眼睛之上叠加装饰/Zzz/鼻涕泡/花絮

        frames++;
        if (now - fpsWindowStart >= 5000) {
            ESP_LOGI(TAG, "fps=%u", (unsigned)(frames * 1000 / (now - fpsWindowStart)));
            frames = 0; fpsWindowStart = now;
        }
        vTaskDelayUntil(&lastWake, period);
    }
}

void render_task_start()
{
    xTaskCreate(renderTask, "render", 8192, nullptr, 5, nullptr);
}
