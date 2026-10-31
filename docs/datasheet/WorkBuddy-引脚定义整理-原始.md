# Waveshare ESP32-S3-LCD-2 引脚定义整理

> 整理时间：2026-10-31
> 芯片：ESP32-S3R8（Xtensa LX7 双核 240MHz，8MB PSRAM + 16MB Flash，2.4G Wi-Fi + BLE5）
> 屏：2.0" IPS 240×320，驱动 ST7789（SPI）
> 其他板载器件：QMI8658 六轴 IMU、TF 卡槽、24Pin 摄像头座（OV2640/OV5640）、锂电池接口（ETA6098 充电）

## 一、结论先行：官方 Arduino 文档页有没有引脚信息？

**没有。**

`https://docs.waveshare.com/ESP32-S3-LCD-2/Arduino` 只是「如何在 Arduino IDE 里跑例程」的教程，
全文仅 8 处出现 `pin`，且都是代码里的宏名（如 `EXAMPLE_PIN_NUM_LCD_DC`），页面原文明确写着：

> This tutorial uses the ESP32-S3-Zero as a reference example, and all hardware code is based on its pinout.
> Before you start, we recommend checking the pinout of your development board…

即：教程以 ESP32-S3-Zero 为参考，要求读者自行核对本板引脚。主页 `.../ESP32-S3-LCD-2/`
的 “Interface Introduction” 图也只有 **28Pin 排针引出脚**，不含板内器件走线。

## 二、引脚信息的真正出处

| 来源 | 位置 | 内容 |
| --- | --- | --- |
| V2 原理图 PDF | `https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2_V2.pdf` | **第 1 页右下角有 `GPIO / Camera / LCD / SD_Card / Other` 对照矩阵表**，最权威 |
| V1 原理图 PDF | `https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf` | 同上，V1 无 TE 信号 |
| 官方示例包 | `https://files.waveshare.com/wiki/ESP32-S3-LCD-2/ESP32-S3-LCD-2-Demo.zip`（约 71MB） | 宏定义写死在各 bsp 头文件里 |
| 文档导航 | `.../ESP32-S3-LCD-2/Resources-And-Documents` | 原理图、芯片手册、2D/3D 图纸入口 |

> 注：ESP32-S3-LCD-2 与 ESP32-S3-Touch-LCD-2 **共用同一块 PCB**，因此共用原理图；
> 原理图中的触摸相关电路（TP_INT / TP_SDA / TP_SCL）仅适用于 Touch 版本。

示例代码中的关键宏定义文件：

```
Arduino/examples/01_factory/bsp_lv_port.h      → LCD 引脚
Arduino/examples/01_factory/bsp_i2c.h          → I2C 引脚
Arduino/examples/03_sd_card_test/*.ino         → TF 卡 SPI + CS
Arduino/examples/06_lvgl_battery/*.ino         → 电池 ADC
Arduino/examples/09_lvgl_camera/*.ino          → 摄像头 DVP 全部脚位
ESP-IDF/01_sd_card_test/main/sd_card_example_main.c
ESP-IDF/02_lvgl_qmi8658/main/bsp_qmi8658.c
```

## 三、板内器件 → GPIO 映射（核心表）

### 3.1 LCD（ST7789，SPI 总线）

| 信号 | GPIO | 备注 |
| --- | --- | --- |
| SCLK | **39** | 与 TF 卡共用 |
| MOSI(SDA) | **38** | 与 TF 卡共用 |
| MISO | **40** | 与 TF 卡共用 |
| DC | **42** | |
| CS | **45** | |
| RST | **-1** | 代码里不做软件复位，走板级复位 |
| BL（背光） | **1** | LEDC PWM，5000 Hz / 10 bit |
| 像素时钟 | 80 MHz | 240×320，`EXAMPLE_LCD_ROTATION` 示例内为 0 或 1 |

### 3.2 TF 卡（SPI 模式，与 LCD 共用总线）

| 信号 | GPIO |
| --- | --- |
| CLK | 39 |
| MOSI | 38 |
| MISO | 40 |
| **CS** | **41** |

### 3.3 I2C（QMI8658 六轴 IMU / Touch 版触摸屏）

| 信号 | GPIO | 备注 |
| --- | --- | --- |
| SDA | **48** | 与触摸共用同一 I2C 总线 |
| SCL | **47** | |

### 3.4 电池电压检测

| 信号 | GPIO | 备注 |
| --- | --- | --- |
| BAT_ADC | **5** | 200K/100K 分压（R19/R20），`analogRead()` 读取 |

### 3.5 摄像头 DVP（24Pin 座，OV2640 / OV5640）

| 信号 | GPIO | 信号 | GPIO |
| --- | --- | --- | --- |
| XCLK | **8** | D0 | **12** |
| PCLK | **9** | D1 | **13** |
| VSYNC | **6** | D2 | **15** |
| HREF | **4** | D3 | **11** |
| SIOD(SDA) | **21** | D4 | **14** |
| SIOC(SCL) | **16** | D5 | **10** |
| PWDN | **17** | D6 | **7** |
| RESET | **-1** | D7 | **2** |

### 3.6 板载按键与串口

| 功能 | 引脚 |
| --- | --- |
| BOOT 键 | **GPIO0** |
| RST 键 | CHIP_PU（EN） |
| UART0 TXD | **GPIO43** |
| UART0 RXD | **GPIO44** |
| 原生 USB D- / D+ | **GPIO19 / GPIO20**（同时引到排针，见注意事项） |

## 四、28Pin 排针引出脚（用于外接扩展）

| 脚号 | 引脚 | 脚号 | 引脚 |
| --- | --- | --- | --- |
| 1 | 3V3 | 28 | GPIO2 |
| 2 | GND | 27 | GPIO4 |
| 3 | TXD | 26 | GPIO6 |
| 4 | RXD | 25 | GPIO16 |
| 5 | GPIO47 | 24 | GPIO17 |
| 6 | GPIO48 | 23 | GPIO18 |
| 7 | GPIO15 | 22 | GPIO21 |
| 8 | GPIO13 | 21 | GPIO8 |
| 9 | GPIO11 | 20 | GPIO7 |
| 10 | GPIO12 | 19 | GPIO10 |
| 11 | GPIO14 | 18 | GPIO20 |
| 12 | GPIO9 | 17 | GPIO19 |
| 13 | GND | 16 | GND |
| 14 | VBAT | 15 | 5V |

## 五、用之前必须知道的 4 个坑

1. **摄像头占掉的引脚全部引到了排针**：2、4、6、7、8、9、10、11、12、13、14、15、16、17、21。
   插上摄像头后这些脚不要当普通 IO 用；反之要用摄像头，就不能占用它们。
2. **LCD 与 TF 卡共用一组 SPI（39/38/40）**，仅 CS 不同（LCD=45、TF=41）。
   二者可同挂总线，但不建议再挂第三个 SPI 从机。
3. **GPIO19 / GPIO20**：板上没有 USB 转串口芯片，Type-C 直连 S3 原生 USB。
   这两脚就是 USB 的 D-/D+，占用后会影响 USB 下载与串口功能。
4. **V2 硬件新增 LCD TE（防撕裂）信号**，官方例程未使用。
   若要用来做刷屏同步，需自行在 V2 原理图第 1 页确认其具体 GPIO。

## 六、可直接粘进代码的引脚定义（Arduino）

```cpp
/* ---------- LCD (ST7789, SPI) ---------- */
#define EXAMPLE_PIN_NUM_LCD_SCLK   39
#define EXAMPLE_PIN_NUM_LCD_MOSI   38
#define EXAMPLE_PIN_NUM_LCD_MISO   40
#define EXAMPLE_PIN_NUM_LCD_DC     42
#define EXAMPLE_PIN_NUM_LCD_RST   (-1)
#define EXAMPLE_PIN_NUM_LCD_CS     45
#define EXAMPLE_PIN_NUM_LCD_BL      1      // PWM 背光
#define EXAMPLE_LCD_H_RES         240
#define EXAMPLE_LCD_V_RES         320
#define EXAMPLE_LCD_ROTATION        1

/* ---------- I2C (QMI8658 / 触摸) ---------- */
#define EXAMPLE_PIN_NUM_I2C_SDA    48
#define EXAMPLE_PIN_NUM_I2C_SCL    47

/* ---------- TF 卡 (共用 LCD 的 SPI，仅 CS 不同) ---------- */
#define PIN_NUM_MOSI   38
#define PIN_NUM_MISO   40
#define PIN_NUM_CLK    39
#define PIN_NUM_CS     41

/* ---------- 电池电压 ADC ---------- */
#define EXAMPLE_PIN_NUM_BAT         5

/* ---------- 摄像头 DVP ---------- */
#define PWDN_GPIO_NUM  17
#define RESET_GPIO_NUM (-1)
#define XCLK_GPIO_NUM   8
#define SIOD_GPIO_NUM  21
#define SIOC_GPIO_NUM  16
#define Y2_GPIO_NUM    12   // bit0
#define Y3_GPIO_NUM    13   // bit1
#define Y4_GPIO_NUM    15   // bit2
#define Y5_GPIO_NUM    11   // bit3
#define Y6_GPIO_NUM    14   // bit4
#define Y7_GPIO_NUM    10   // bit5
#define Y8_GPIO_NUM     7   // bit6
#define Y9_GPIO_NUM     2   // bit7
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    4
#define PCLK_GPIO_NUM    9

/* ---------- 背光 PWM ---------- */
#define LEDC_FREQ         5000
#define LEDC_TIMER_10_BIT   10
```

## 七、本地文件

- 原理图 PDF（V2，3 页，内容在第 1 页）：
  `C:\Users\nuc\WorkBuddy\2026-10-31-14-21-59\sch_v2.pdf`

## 八、待确认 / 未覆盖项

- **LCD_RST 实际连接的网络**（原理图中有 `LCD_RST` 网络，R16 为 NC/0R、R17 为 NC/10K）：
  官方例程统一填 `-1`，需手工读原理图第 1 页确认是否挂在 CHIP_PU 上。
- **LCD TE 信号脚位**（V2 新增）：例程未使用，脚位待查。
- **原理图中 `TP_INT / TP_SDA / TP_SCL`** 属 Touch 版本专用，本板不适用。
- 板载 LED（蓝 LED1、红 LED2）为电源 / 充电状态指示，非用户可控 GPIO。
