// 网络配置
//
// 凭据来自 网络信息/链接.txt（Wi-Fi 二维码字符串 WIFI:T:WPA;S:...;P:...;;），
// 解析后写进 wifi_secrets.h —— 该文件被 .gitignore 排除，不会上传。
//
// 行为：
//   1) 有凭据 → 先连家里路由（STA），成功后手机在同一 Wi-Fi 下即可访问
//   2) 15 秒连不上 → 自动退回自带热点（AP），保证永远能连上
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

// 兜底热点
#define AP_SSID    "ESP32-LCD2-CAM"
#define AP_PASS    "12345678"      // 至少 8 位

// mDNS 名字：连上路由后可用 http://esp32cam.local/ 访问，不用记 IP
#define MDNS_NAME  "esp32cam"
