#pragma once
// QMI8658A：加速度计（敲击唤醒）+ 陀螺仪（甩动翻页）
#include <Arduino.h>

bool qmi_begin();
bool qmi_ok();
uint8_t qmi_addr();

// 单位：g
bool qmi_read_accel(float &ax, float &ay, float &az);
// 单位：dps（度/秒）
bool qmi_read_gyro(float &gx, float &gy, float &gz);
// 一次读完 6 轴（加速度在 0x35，陀螺仪紧跟其后 0x3B）
bool qmi_read_motion(float &ax, float &ay, float &az,
                     float &gx, float &gy, float &gz);
