# ESP32 开发项目

工作目录：`E:\ESP32开发`
设备：**微雪 Waveshare ESP32-S3-LCD-2**（2 寸 IPS 240×320，ESP32-S3R8 + 16MB Flash，板载摄像头接口 / TF 卡槽）
当前状态：**板子已通过 USB-C 连上电脑，串口 `COM16` 可用，esptool 可正常识别并读写 Flash**（详见 [docs/设备信息.md](docs/设备信息.md)）。

## 目录结构

```
E:\ESP32开发
├─ 设备图片\            原始设备照片（板子正面 / 屏幕 / 摄像头）
│  ├─ 板子正面照.jpg
│  ├─ 板子屏幕与接口.jpg   屏幕模块（FPC 排线）
│  └─ 摄像头照片.jpg      OV5640 摄像头（SKU 29456）
├─ docs\
│  ├─ 设备信息.md        硬件判读 + 芯片实测数据 + 现有固件信息
│  └─ img\               丝印、摄像头标签等取证特写
├─ scripts\
│  ├─ chip_info.ps1     一键探测板子（端口 / 芯片 / Flash / 分区）
│  ├─ serial_monitor.py 串口监视 + 复位抓启动日志
│  └─ fw_inspect.py     固件离线分析（分区表 / app 描述符 / 关键词串）
├─ dump\                Flash 备份（已 gitignore，不入库）
├─ logs\                串口日志（已 gitignore）
└─ .venv\               Python 虚拟环境（esptool + pyserial，已 gitignore）
```

## 环境

已在本目录建好虚拟环境 `.venv`，内含 `esptool 5.6.0` 与 `pyserial 3.5`：

```powershell
cd E:\ESP32开发
.\.venv\Scripts\python.exe -m esptool version
```

- 虚拟环境 Python：3.12.4（系统 `C:\Users\nuc\AppData\Local\Programs\Python\Python312`）
- 串口：`COM16`（USB-Serial/JTAG，原生 USB，非 CH340/CP2102）
- 板子 MAC：`b8:1f:3f:c8:25:24`

## 常用命令

```powershell
# 一键探测（端口 + 芯片 + Flash + 分区表）
.\scripts\chip_info.ps1

# 芯片信息
.\.venv\Scripts\python.exe -m esptool -p COM16 -b 460800 flash-id

# 备份整片 Flash（16MB，约 1 分钟）
.\.venv\Scripts\python.exe -m esptool -p COM16 -b 921600 read-flash 0x0 0x1000000 dump\full_flash.bin

# 恢复备份
.\.venv\Scripts\python.exe -m esptool -p COM16 -b 921600 write-flash 0x0 dump\full_flash.bin

# 抓启动日志 / 监视串口（Ctrl+C 退出）
.\.venv\Scripts\python.exe scripts\serial_monitor.py -p COM16 -b 115200 --reset

# 离线分析已备份的固件（分区表 / app 描述符 / 关键词串）
.\.venv\Scripts\python.exe scripts\fw_inspect.py --partitions dump\partitions.bin --app dump\app0.bin
```

## 板上现有固件

出厂 `01_factory` 演示固件（Arduino-ESP32 3.1.1 / IDF v5.3.2 编译，编译时间 2025-01-06），
含 LVGL 界面、摄像头、SD 卡、Wi-Fi 网页服务等任务。**动 Flash 之前先备份**（见上）。

## 后续可做

- 装 Arduino CLI 或 PlatformIO，做 Hello World / LVGL 点屏
- 备份出厂固件后刷测试固件，保留 `dump\full_flash.bin` 作为回滚点
- 接 OV5640 做拍照 + Wi-Fi 图传；屏幕 FPC 装到板载 40pin 座上验证点屏
