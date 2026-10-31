// 摄像头 OV5640 引脚定义（来源：官方原理图 V2 引脚矩阵，见 docs/引脚定义.md）
// 板子：Waveshare ESP32-S3-LCD-2
#pragma once

#define CAM_PIN_PWDN   17
#define CAM_PIN_RESET  -1     // 原理图上未接
#define CAM_PIN_XCLK    8

#define CAM_PIN_SIOD   21     // TWI_SDA
#define CAM_PIN_SIOC   16     // TWI_CLK

#define CAM_PIN_D7      2
#define CAM_PIN_D6      7
#define CAM_PIN_D5     10
#define CAM_PIN_D4     14
#define CAM_PIN_D3     11
#define CAM_PIN_D2     15
#define CAM_PIN_D1     13
#define CAM_PIN_D0     12

#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    4
#define CAM_PIN_PCLK    9
