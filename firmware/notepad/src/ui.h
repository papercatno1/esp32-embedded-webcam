#pragma once
// 320x240 横屏 UI：标题条 + 正文（16px 点阵，自动折行，最多 10 行）
#include <Arduino.h>

void ui_begin();
bool ui_ready();

// 显示第 idx 条；-1 表示按 notes_cursor()
void ui_show_note(int idx);
void ui_show_empty();
void ui_message(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr);
void ui_progress(int pct, const char *title);   // OTA/联网进度

void ui_backlight(int pct);   // 0-100
int  ui_backlight_get();
