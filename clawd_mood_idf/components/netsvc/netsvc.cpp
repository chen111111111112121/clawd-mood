#include "netsvc.hpp"
#include "monitor.hpp"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "mdns.h"
#include "nvs.h"

// 固件版本=单一可信来源:工程根 version.txt → IDF 烤进 app 镜像(esp_app_desc),
// /info 读 esp_app_get_description()->version。改版本只动 version.txt 再重编,全程一致不漂移。

// 配网页（移植自 .ino INDEX_HTML）：手机连 AP 后浏览器填家庭 WiFi。
static const char INDEX_HTML[] = R"rawhtml(<!doctype html><html lang="zh"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clawd Mochi · 配网</title>
<style>
  :root{--bg:#0b0c10;--card:#15171b;--fg:#f2efe9;--muted:#9aa6b2;--accent:#da1100}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,system-ui,"PingFang SC","Microsoft YaHei",sans-serif;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
  .card{width:100%;max-width:360px;background:var(--card);border-radius:16px;padding:26px 22px;box-shadow:0 10px 30px rgba(0,0,0,.4)}
  h1{font-size:19px;margin:0 0 4px}
  .sub{color:var(--muted);font-size:13px;margin:0 0 20px;line-height:1.5}
  label{display:block;font-size:13px;color:var(--muted);margin:14px 0 6px}
  input{width:100%;padding:11px 12px;border-radius:9px;border:1px solid #2a2d33;background:#0f1115;color:var(--fg);font-size:15px}
  button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:10px;background:var(--accent);color:#fff;font-size:15px;font-weight:600;cursor:pointer}
  button:disabled{opacity:.6}
  #msg{margin-top:16px;font-size:13px;min-height:18px;line-height:1.5}
  .ok{color:#50dc82}.err{color:#ff7a6a}
</style></head><body>
  <div class="card">
    <h1>Clawd Mochi 配网</h1>
    <p class="sub">连接家庭 WiFi 后，PC 的 Claude Code / Cursor 监测才能推送到设备。</p>
    <label for="ssid">WiFi 名称 (SSID)</label>
    <input id="ssid" autocomplete="off" placeholder="2.4G WiFi 名称">
    <label for="pass">WiFi 密码</label>
    <input id="pass" type="password" autocomplete="off" placeholder="密码">
    <button id="save" onclick="save()">保存并连接</button>
    <div id="msg"></div>
  </div>
<script>
function save(){
  var ssid=document.getElementById('ssid').value.trim();
  var pass=document.getElementById('pass').value;
  var msg=document.getElementById('msg'), btn=document.getElementById('save');
  if(!ssid){msg.className='err';msg.textContent='请填写 WiFi 名称';return;}
  btn.disabled=true;msg.className='';msg.textContent='连接中…';
  fetch('/wifi/save?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass))
    .then(function(r){return r.json()})
    .then(function(j){
      if(j.sta){msg.className='ok';msg.textContent='已连接 ✓ 设备 IP：'+j.sta_ip;}
      else{msg.className='err';msg.textContent='连接失败，请检查名称/密码后重试';}
    })
    .catch(function(){msg.className='err';msg.textContent='请求失败，请重试';})
    .finally(function(){btn.disabled=false;});
}
</script>
</body></html>)rawhtml";

static const char* TAG = "netsvc";
static const char* AP_SSID = "ClaWD-Mood";
static const char* AP_PASS = "clawd1234";
static constexpr int STA_MAX_RETRY = 5;   // 连不上重试上限,超了停(单射频:别搅扰 AP)

namespace {
bool s_staConnected = false;
char s_staIp[16] = "";
int  s_retry = 0;
bool s_haveCreds = false;
bool s_mdnsUp = false;

// 起 mDNS：clawd.local + _http._tcp:80。仅首次连上 STA 时起一次。
void start_mdns() {
    if (s_mdnsUp) return;
    if (mdns_init() != ESP_OK) { ESP_LOGW(TAG, "mdns_init failed"); return; }
    mdns_hostname_set("clawd");
    mdns_instance_name_set("Clawd Mochi");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    s_mdnsUp = true;
    ESP_LOGI(TAG, "mDNS up: http://clawd.local");
}

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
        start_mdns();   // 连上后起 clawd.local
    }
}

// URL 解码（%XX + '+'→空格），就地写入 dst。hook 发的 info 一般是 ASCII。
void url_decode(const char* src, char* dst, size_t dsz) {
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < dsz; i++) {
        char c = src[i];
        if (c == '+') { dst[o++] = ' '; }
        else if (c == '%' && src[i+1] && src[i+2]) {
            auto hex = [](char h)->int { if (h>='0'&&h<='9') return h-'0'; if (h>='a'&&h<='f') return h-'a'+10; if (h>='A'&&h<='F') return h-'A'+10; return 0; };
            dst[o++] = (char)((hex(src[i+1]) << 4) | hex(src[i+2])); i += 2;
        } else { dst[o++] = c; }
    }
    dst[o] = 0;
}

esp_err_t h_status(httpd_req_t* req) {
    char q[192] = {0}, s[16] = {0}, act[16] = {0}, rawInfo[64] = {0}, info[40] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "s",    s,       sizeof(s));
        httpd_query_key_value(q, "act",  act,     sizeof(act));
        httpd_query_key_value(q, "info", rawInfo, sizeof(rawInfo));
        url_decode(rawInfo, info, sizeof(info));
    }
    if (!s[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"e\":1}");
        return ESP_OK;
    }
    monitor::setState(s, act, info);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

esp_err_t h_presence(httpd_req_t* req) {
    char q[64] = {0}, s[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK)
        httpd_query_key_value(q, "s", s, sizeof(s));
    if (!s[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"e\":1}");
        return ESP_OK;
    }
    monitor::setPresence(s);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
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

// 写 STA 配置（不重连）。
void apply_sta(const char* ssid, const char* pass) {
    wifi_config_t sta = {};
    strncpy((char*)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char*)sta.sta.password, pass, sizeof(sta.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &sta);
}

// 存凭据到 NVS + 重连 STA，阻塞等待至多 ~8s。返回是否连上。（在 HTTP task 调，可阻塞。）
bool save_creds_and_reconnect(const char* ssid, const char* pass) {
    nvs_handle_t h;
    if (nvs_open("clawd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
    }
    s_haveCreds = true; s_retry = 0;
    apply_sta(ssid, pass);
    esp_wifi_disconnect();
    esp_wifi_connect();
    for (int i = 0; i < 80 && !s_staConnected; i++) vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "wifi/save '%s' -> %s", ssid, s_staConnected ? s_staIp : "(fail)");
    return s_staConnected;
}

esp_err_t h_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 设备信息:供上位机展示「当前版本」并(后续)做版本比对。id=STA MAC 十六进制。
esp_err_t h_info(httpd_req_t* req) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const esp_app_desc_t* desc = esp_app_get_description();   // 版本来自烤进二进制的 app 描述
    char body[220];
    snprintf(body, sizeof(body),
             "{\"name\":\"clawd-mochi\",\"version\":\"%s\",\"chip\":\"esp32c3\","
             "\"id\":\"%02x%02x%02x%02x%02x%02x\","
             "\"caps\":[\"monitor\",\"mood\",\"sleep\",\"presence\",\"bootinfo\"]}",
             desc->version, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

esp_err_t h_wifi_save(httpd_req_t* req) {
    char q[256] = {0}, rs[48] = {0}, rp[96] = {0}, ssid[33] = {0}, pass[65] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "ssid", rs, sizeof(rs));
        httpd_query_key_value(q, "pass", rp, sizeof(rp));
        url_decode(rs, ssid, sizeof(ssid));
        url_decode(rp, pass, sizeof(pass));
    }
    if (!ssid[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"e\":1}");
        return ESP_OK;
    }
    const bool ok = save_creds_and_reconnect(ssid, pass);
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":1,\"sta\":%s,\"sta_ip\":\"%s\"}", ok ? "true" : "false", s_staIp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
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

void http_start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t u_status = { .uri = "/status", .method = HTTP_GET, .handler = h_status, .user_ctx = nullptr };
    httpd_register_uri_handler(srv, &u_status);
    httpd_uri_t u_presence = { .uri = "/presence", .method = HTTP_GET, .handler = h_presence, .user_ctx = nullptr };
    httpd_register_uri_handler(srv, &u_presence);
    httpd_uri_t u_root = { .uri = "/", .method = HTTP_GET, .handler = h_root, .user_ctx = nullptr };
    httpd_register_uri_handler(srv, &u_root);
    httpd_uri_t u_save = { .uri = "/wifi/save", .method = HTTP_GET, .handler = h_wifi_save, .user_ctx = nullptr };
    httpd_register_uri_handler(srv, &u_save);
    httpd_uri_t u_info = { .uri = "/info", .method = HTTP_GET, .handler = h_info, .user_ctx = nullptr };
    httpd_register_uri_handler(srv, &u_info);
    ESP_LOGI(TAG, "HTTP up: / /info /status /presence /wifi/save");
}

bool sta_connected() { return s_staConnected; }
const char* sta_ip() { return s_staIp; }

} // namespace netsvc
