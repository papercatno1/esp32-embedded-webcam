# 网页图传（MVP）

板子：微雪 **Waveshare ESP32-S3-LCD-2**（ESP32-S3R8 / 8 MB PSRAM / 16 MB Flash / OV5640）

浏览器打开就能看到实时画面，支持单帧下载、拍照存 TF 卡、状态查看。

## 功能

| 路径 | 作用 |
| --- | --- |
| `/` | 网页界面（深色、手机自适应） |
| `/stream` | MJPEG 实时流（`multipart/x-mixed-replace`） |
| `/capture` | 下载当前一帧 JPG |
| `/shoot` | 拍照存到 TF 卡 `/photo/<毫秒>.jpg` |
| `/status` | 状态 JSON（模式/IP/RSSI/分辨率/帧率/堆/运行时长/是否有卡） |
| `/restart` | 重启板子 |

- 摄像头：OV5640，VGA 640×480，JPEG，质量 12，双缓冲放 PSRAM
- 无线：填了 `WIFI_SSID` 就连路由，15 秒连不上**自动退回热点** `ESP32-LCD2-CAM / 12345678`，地址 `http://192.168.4.1`
- TF 卡：走 SPI（39/38/40，CS 41），没插卡不影响图传

## 目录

```
firmware/webcam/
├─ platformio.ini        板型 / PSRAM / 分区表 / 依赖
├─ partitions_16MB.csv   16MB 布局（与出厂固件一致）
└─ src/
   ├─ main.cpp           摄像头 + Wi-Fi + HTTP + SD
   ├─ camera_pins.h      OV5640 的 15 个引脚（来自官方原理图）
   ├─ wifi_config.h      Wi-Fi 账号 / 热点名密码
   └─ www.h              网页（存在 Flash，不占 RAM）
```

## 编译 / 烧录 / 看日志

```powershell
cd E:\ESP32开发\firmware\webcam
..\..\.venv\Scripts\python.exe -m platformio run -t upload
..\..\.venv\Scripts\python.exe -m platformio device monitor -p COM16 -b 115200
```

或一条命令：`..\..\.venv\Scripts\python.exe -m platformio run -t upload -t monitor`

## 使用

1. 上电，串口会打印 IP（STA 或 AP）。热点模式下是 `http://192.168.4.1/`
2. 手机/电脑连上同一个网络，浏览器打开该地址
3. 画面卡顿：先看顶部 RSSI；低于 -70 dBm 建议靠近或改 5G 路由

## 排查

| 现象 | 原因 |
| --- | --- |
| 串口 `[CAM] 初始化失败: 0x105` | FPC 没插到底或金手指朝向反了 |
| 串口 `0x20004 / 0x20005` | 检测不到 / 不支持的传感器 |
| 网页一直转圈 | 路由器隔离了客户端（AP isolation），换热点模式试 |
| `/shoot` 提示没有 TF 卡 | 卡没插好，或卡不是 FAT32 |

## 后续可加

- 屏幕状态页（FPS / 电量 / 卡容量）——ST7789T3，引脚见 `docs/引脚定义.md`
- 分辨率切换接口（VGA / SVGA / UXGA）
- 录像（连拍 JPEG 打包）
- 手机端自动刷新缩略图墙
