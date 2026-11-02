#pragma once
// 320x240 横屏 UI：标题条(24px) + 正文(24px 点阵，自动折行) + 页脚(16px)
#include <Arduino.h>

void ui_begin();
uint32_t ui_last_img_ink();   // 上一张图画了多少个黑点（0=没画出来）
void ui_bus_recover();   // SD 探测后恢复 LCD 总线
bool ui_ready();
void ui_set_invert(int on);

// 显示第 idx 条；-1 表示按 notes_cursor()
void ui_show_note(int idx);
void ui_show_empty();
void ui_show_splash(const char *ver);
void ui_message(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr);
void ui_progress(int pct, const char *title);   // OTA/联网进度

void ui_backlight(int pct);   // 5-100
int  ui_backlight_get();

// 背光开关（熄屏用，不改动设定亮度）
void ui_screen_on(bool on);
bool ui_screen_is_on();

// 页脚状态：蓝牙是否已连、多少秒熄屏（0 = 常亮）、当前第几条/共几条
void ui_set_link(bool connected);
void ui_set_timeout_s(uint32_t sec);
void ui_set_page(int idx, int total);
void ui_footer_refresh();     // 只重画页脚（状态变了就调一下）

// ---------------- 系统信息页（模式：原生系统） ----------------
struct SysInfo {
  const char *ver;
  const char *mode;
  const char *wifi;       // "已连 192.168.88.32" / "未连"
  const char *ble;        // "已连" / "广播中"
  const char *sdcard;     // "29818MB" / "未挂载"
  int psram_mb;
  int flash_mb;
  int batt_mv;
  int temp_c;
  uint32_t heap_kb;
  uint32_t up_s;
  int notes;
};
void ui_show_sys(const SysInfo &si);
