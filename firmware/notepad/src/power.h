#pragma once
// 熄屏与敲击唤醒
//
// 规则：一段时间没有交互（BLE 指令 / 翻页 / 上电）就关背光；
// 熄屏期间用 QMI8658 的加速度变化判定"敲一下"，命中即点亮。
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

// loop 里调用：超时熄屏 / 熄屏时检测敲击
void power_tick();

// 调试：最近一次敲击事件的加速度变化量
float power_last_impulse();
