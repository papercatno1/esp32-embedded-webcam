// 网络配置
// 1) 想让板子连你家路由：填 WIFI_SSID / WIFI_PASS，15 秒连不上会自动退回热点模式
// 2) 想直接用板子的热点：留空即可，手机连 "ESP32-LCD2-CAM" 密码 "12345678"，访问 http://192.168.4.1
#pragma once

#define WIFI_SSID  ""          // 例如 "TP-LINK_5G"
#define WIFI_PASS  ""          // 例如 "12345678"

#define AP_SSID    "ESP32-LCD2-CAM"
#define AP_PASS    "12345678"  // 至少 8 位
