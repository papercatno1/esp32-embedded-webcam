// 网络配置（票据屏）
//
// BLE 是主通道；Wi-Fi 只用于 OTA 升级与状态页，连不上不影响使用。
// 凭据来自 wifi_secrets.h —— 该文件被 .gitignore 排除。
#pragma once

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#define MDNS_NAME "esp32note"
