#pragma once
// 熄屏、敲击唤醒、甩动翻页
//
// 规则：一段时间没有交互（BLE 指令 / 翻页 / 上电）就关背光；
// 熄屏期间用 QMI8658 的加速度变化判定"敲一下"，命中即点亮；
// 无论亮灭，陀螺仪转得够快（约 50 度以上）判定为一次"甩动"，亮屏时用来翻页。
// 没有 IMU 时不允许自动熄屏（否则点不亮，只能复位）。
#include <Arduino.h>

void power_begin(uint32_t idle_ms = 60000);

// 0 = 常亮
void power_set_timeout(uint32_t ms);
uint32_t power_timeout();

// 有交互：点亮并重置计时
void power_activity();

// 手动开关（S 指令用）
void power_blank();
void power_wake();

bool power_is_blank();

// 距上次交互的毫秒数
uint32_t power_idle_ms();

// loop 里调用：超时熄屏 / 敲击唤醒 / 甩动检测
void power_tick();

// 取走一次待处理的甩动：-1 上一条，+1 下一条，0 没有
int power_take_swipe();
uint32_t power_flip_count();   // 翻牌动作累计次数（排查用）

// 调试信息
float    power_last_impulse();     // 最近一次敲击的加速度变化量（g）
float    power_last_swipe_deg();   // 最近一次甩动累计转角（度）
char     power_last_swipe_axis();  // 'x' / 'y' / 'z'
uint32_t power_swipe_count();      // 甩动次数
void power_set_swipe_sign(int sign);   // 甩动翻页方向：1 / -1
int  power_get_swipe_sign();

// 陀螺仪实时读数，用来判断"传感器到底看不看得见你在甩"
float    power_last_mag();         // 最近一次采样的合成角速度（dps）
float    power_peak_mag();         // 上电以来的角速度峰值（dps）
void     power_reset_peak();       // 清零峰值（发 `Q` 前用）
