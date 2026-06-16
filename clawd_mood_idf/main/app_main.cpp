#include "esp_log.h"
#include "nvs_flash.h"
#include "display.hpp"
#include "eyes.hpp"
#include "mood.hpp"
#include "monitor.hpp"
#include "netsvc.hpp"
#include "render_task.hpp"

static const char *TAG = "clawd";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Clawd Mood IDF — M4a mood + idle");

    // NVS 全局初始化一次（mood 持久化 + 后续 M5 WiFi 凭据共用）
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    display::init();
    eyes::init();
    mood::init();
    monitor::init();
    ESP_LOGI(TAG, "boot mood: energy=%.0f joy=%.0f", mood::energy(), mood::joy());

    netsvc::wifi_init();   // AP 常开 + 有 NVS 凭据则连 STA

    render_task_start();
}
