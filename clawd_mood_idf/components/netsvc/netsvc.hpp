#pragma once
#include <stdint.h>

namespace netsvc {

// 起 WiFi：AP 常开 + 有 NVS 凭据则连 STA。须先 nvs_flash_init()。开机调一次。
void wifi_init();

bool sta_connected();
const char* sta_ip();        // "" 表示未连

} // namespace netsvc
