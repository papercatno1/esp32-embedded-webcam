#pragma once
// 320x240 横屏 UI：标题条(24px) + 正文(24px 点阵，自动折行) + 页脚(16px)
#include <Arduino.h>

void ui_begin();
bool ui_ready();

// 显示第 idx 条；-1 表示按 notes_cursor()
void ui_show_note(int idx);
void ui_show_empty();
void ui_message(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr);
void ui_progress(int pct, const char *title);   // OTA/联网进度

void ui_backlight(int pct);   // 5-100
int  ui_backlight_get();

// 背光开关（熄屏用，不改动设定亮度）
void ui_screen_on(bool on);
bool ui_screen_is_on();
