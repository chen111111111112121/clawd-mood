#include "netsvc.hpp"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"

static const char* TAG = "netsvc";
static const char* AP_SSID = "ClaWD-Mood";
static const char* AP_PASS = "clawd1234";
static constexpr int STA_MAX_RETRY = 5;   // 连不上重试上限,超了停(单射频:别搅扰 AP)

namespace {
bool s_staConnected = false;
char s_staIp[16] = "";
int  s_retry = 0;
bool s_haveCreds = false;

void on_wifi(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_haveCreds) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_staConnected = false; s_staIp[0] = 0;
        if (s_haveCreds && s_retry < STA_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "STA disconnected, retry %d/%d", s_retry, STA_MAX_RETRY);
            esp_wifi_connect();
        } else if (s_haveCreds) {
            ESP_LOGW(TAG, "STA give up after %d tries — keep AP clean", STA_MAX_RETRY);
            // 不再重连：单射频下持续重试会搅乱 softAP。M6 配网保存后可重启或再触发。
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
        snprintf(s_staIp, sizeof(s_staIp), IPSTR, IP2STR(&e->ip_info.ip));
        s_staConnected = true; s_retry = 0;
        ESP_LOGI(TAG, "STA got IP: %s", s_staIp);
    }
}

// 从 NVS 读 ssid/pass(旧 Arduino 版同 namespace "clawd" 同键)。返回是否有 ssid。
bool load_creds(char* ssid, size_t ssz, char* pass, size_t psz) {
    nvs_handle_t h;
    ssid[0] = 0; pass[0] = 0;
    if (nvs_open("clawd", NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssz, pl = psz;
    nvs_get_str(h, "ssid", ssid, &sl);
    nvs_get_str(h, "pass", pass, &pl);
    nvs_close(h);
    return ssid[0] != 0;
}
} // namespace

namespace netsvc {

void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // AP 配置
    wifi_config_t ap = {};
    strncpy((char*)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(AP_SSID);
    strncpy((char*)ap.ap.password, AP_PASS, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    // STA 配置(若 NVS 有凭据)
    char ssid[33], pass[65];
    s_haveCreds = load_creds(ssid, sizeof(ssid), pass, sizeof(pass));
    if (s_haveCreds) {
        wifi_config_t sta = {};
        strncpy((char*)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
        strncpy((char*)sta.sta.password, pass, sizeof(sta.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_LOGI(TAG, "STA creds found, will connect to '%s'", ssid);
    } else {
        ESP_LOGI(TAG, "no STA creds in NVS — AP only");
    }

    ESP_ERROR_CHECK(esp_wifi_start());   // 触发 WIFI_EVENT_STA_START → on_wifi 里 connect
    ESP_LOGI(TAG, "WiFi up: AP '%s' (pass %s)%s", AP_SSID, AP_PASS, s_haveCreds ? " + STA" : "");
}

bool sta_connected() { return s_staConnected; }
const char* sta_ip() { return s_staIp; }

} // namespace netsvc
