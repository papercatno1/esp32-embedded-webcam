#pragma once
// QMI8658A 六轴 IMU（板载，I2C：SDA=GPIO48，SCL=GPIO47）
//
// 只用到加速度计：用来做"敲一下唤醒屏幕"。
#include <Arduino.h>

// 初始化（探测 0x6A/0x6B，校验 WHO_AM_I）。true = 可用
bool qmi_begin();

bool qmi_ok();

// 读加速度，单位 g。true = 读到合理数据
bool qmi_read_accel(float &ax, float &ay, float &az);

uint8_t qmi_addr();
