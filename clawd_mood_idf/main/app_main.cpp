#include "esp_log.h"
#include "esp_timer.h"
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
    monitor::bootGreeting();   // 开机问候（阻塞 ~2.3s），随后 eyes::init() 清屏接管
    eyes::init();
    mood::init();
    monitor::init();
    ESP_LOGI(TAG, "boot mood: energy=%.0f joy=%.0f", mood::energy(), mood::joy());

    netsvc::wifi_init();   // AP 常开 + 有 NVS 凭据则连 STA
    netsvc::http_start();  // /status 路由（Hook 推状态切表情）

    // 开机/配网信息屏:显示 AP/控制器地址 + 家庭 WiFi 状态,引导配网。
    // STA 连接是异步的,此刻多半未连上 → 常驻;tickBootInfo 在后台连上后给 3s 确认再进表情。
    const uint32_t bootNow = (uint32_t)(esp_timer_get_time() / 1000);
    monitor::enterBootInfo(bootNow, netsvc::sta_connected(), netsvc::sta_ip());

    render_task_start();
}
