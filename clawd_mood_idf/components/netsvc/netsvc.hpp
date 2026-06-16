#pragma once
#include <stdint.h>

namespace netsvc {

// 起 WiFi：AP 常开 + 有 NVS 凭据则连 STA。须先 nvs_flash_init()。开机调一次。
void wifi_init();

// 起 HTTP 服务：注册 /status（Hook 推状态）等路由。须在 wifi_init() 后调一次。
void http_start();

bool sta_connected();
const char* sta_ip();        // "" 表示未连

} // namespace netsvc
