# 网页图传（MVP）

板子：微雪 **Waveshare ESP32-S3-LCD-2**（ESP32-S3R8 / 8 MB PSRAM / 16 MB Flash / OV5640）

浏览器打开就能看到实时画面，支持单帧下载、拍照存 TF 卡、状态查看。

## 功能

| 路径 | 作用 |
| --- | --- |
| `/` | 网页界面（深色、手机自适应） |
| `/stream` | MJPEG 实时流（**在 8080 端口**，`multipart/x-mixed-replace`） |
| `/simple` | 低配预览：每秒一帧，不走 MJPEG，个别浏览器/扩展拦截时用它 |
| `/capture` | 下载当前一帧 JPG |
| `/shoot` | 拍照存到 TF 卡 `/photo/<毫秒>.jpg` |
| `/status` | 状态页（HTML；`/status.json` 是 JSON 版） |
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

## 网络：优先连家里路由，连不上才开热点

- 凭据放在 `src/wifi_secrets.h`（**已被 .gitignore 排除，不进仓库**），由 `wifi_config.h` 条件包含。
  没有该文件时自动退回热点模式，代码依然能编译。
- 连上路由后：板子出现在局域网里（实测 `192.168.88.32`），手机连同一个 Wi-Fi 即可访问。
- 同时启用 **mDNS**：同一局域网里用 `http://esp32cam.local/` 访问，不用记 IP。
- 15 秒连不上路由 → 自动开热点 `ESP32-LCD2-CAM / 12345678`（`192.168.4.1`），永远有兜底。

凭据文件格式（键名固定）：

```c
#define WIFI_SSID "你家 WiFi 名"
#define WIFI_PASS "密码"
```

## 屏幕上显示什么

板载 2 寸 ST7789T3（240×320，无触摸）在开机后显示状态页，每 3 秒刷新：

```
ESP32-S3 CAM              ← 标题栏
192.168.88.32             ← 局域网 IP（热点模式下显示 AP 模式 + 192.168.4.1）
esp32cam.local            ← mDNS 名字
────────────────
WiFi  -37 dBm
640x480  17.9 fps
Viewers 1  Frames 412
SD   none
Heap 244 KB
Up   128 s
```

引脚：SCLK 39 / MOSI 38 / DC 42 / CS 45 / BL 1（PWM 背光），RST 未接（不驱动）；
驱动库 `GFX Library for Arduino@1.4.9`（**注意：1.5+ 需要 arduino-esp32 3.x，本项目用 2.0.17，必须锁 1.4.9**）。

## 观看端管理（刷新页面不会把画面弄丢）

- 最多 2 个观看端，槽位满时**淘汰最久没写成功**的那个 —— 刷新页面留下的僵尸连接会被新页面顶掉
- 每次写入都校验返回值，写不全/写不动立即关连接
- 5 秒没有成功写出数据也判死（半开连接靠 `connected()` 判断不出来）

## 编译 / 烧录 / 看日志

```powershell
cd E:\ESP32开发
.\scripts\flash.ps1                    # 编译 + 烧录 + 跟串口（推荐）
```

或手动：

```powershell
cd E:\ESP32开发\firmware\webcam
..\..\.venv\Scripts\python.exe -m platformio run
cd E:\ESP32开发
.\.venv\Scripts\python.exe -m esptool -p COM16 -b 460800 write-flash 0x10000 firmware\webcam\.pio\build\esp32s3\firmware.bin
.\.venv\Scripts\python.exe scripts\serial_monitor.py -p COM16 -b 115200
```

> ⚠️ 别用 `pio run -t upload`：这块板的 USB-CDC 上 PlatformIO 的上传器会在切换波特率那步
> 掉线（`No serial data received`），同一根线用 esptool 直写却稳定 2.5 Mbit/s。

## 架构（v5）：视频流与控制面分离

```
80 端口  esp_http_server  →  /  /status  /status.json  /capture  /shoot  /restart
8080 端口 WiFiServer（自己写的裸 TCP）→  /stream  MJPEG
```

**为什么分两个端口**：ESP-IDF 4.4 的 `httpd` 是**单任务模型**，一个 handler 阻塞（MJPEG 无限推流）
整个 HTTP 服务就停摆 —— 表现就是"看着实时画面时，点任何按钮页面都刷不完"。
把流搬到独立端口由 `loop()` 自己推送后，80 端口的控制请求永远有空处理。

其他设计要点：

- 摄像头 `esp_camera_fb_get()` 用互斥锁保护，取帧带超时，不会两个请求互相卡死
- `/shoot` 先判卡：没插 TF 卡直接返回结果页，不去碰摄像头
- 视频流限速 ~25 fps，两个观看端共用一次抓帧
- 每个请求打印到串口：`[HTTP] GET /shoot`，出问题能直接看请求有没有到

## 页面为什么不用 JS

第一版页面靠 JS 拉 `/status` 并绑定按钮，在个别手机浏览器上脚本没跑，导致数值全是"—"、按钮没反应。
现在：**关键数值由服务端渲染进 HTML，按钮是普通链接**，JS 只做"每秒刷一次数字"的增强。


固件启动时会自己做一次 HTTP 自检（板子访问自己的 80 端口），串口可见：

```
[自检] /status 221 字节, 正文: {"mode":"AP","ip":"192.168.4.1","rssi":0,"res":"640x480",...}
```


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



