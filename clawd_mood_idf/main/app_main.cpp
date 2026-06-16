#include "esp_log.h"
#include "display.hpp"
#include "eyes.hpp"
#include "render_task.hpp"

static const char *TAG = "clawd";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Clawd Mood IDF — M3 render task");
    display::init();
    eyes::init();
    render_task_start();   // 起渲染 task 后 app_main 返回,task 继续跑
}
