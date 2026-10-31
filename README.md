# ESP32 开发项目

工作目录：`E:\ESP32开发`
设备：**微雪 Waveshare ESP32-S3-LCD-2**（2 寸 IPS 240×320，ESP32-S3R8 + 16MB Flash，板载摄像头接口 / TF 卡槽，屏幕无触摸）
当前状态：**串口 `COM16` 可用；整片 16 MB Flash 已备份；屏幕已点亮并运行出厂 demo；OV5640 已装待功能验证**（详见 [docs/设备信息.md](docs/设备信息.md)、[docs/固件备份.md](docs/固件备份.md)）。

## 目录结构

```
E:\ESP32开发
├─ 设备图片\            设备照片（板子正面 / 屏幕 / 摄像头 / 装机后实拍）
│  ├─ 板子正面照.jpg        装机前
│  ├─ 板子屏幕与接口.jpg     屏幕模块（FPC 排线）
│  ├─ 摄像头照片.jpg        OV5640 摄像头（SKU 29456）
│  ├─ 摄像头连接图.jpg      装上摄像头后
│  └─ 屏幕显示图.jpg        屏幕点亮后（出厂 demo）
├─ docs\
│  ├─ 设备信息.md        硬件判读 + 芯片实测数据 + 现有固件 + 实机验证进展
│  ├─ 固件备份.md        16 MB 整片备份记录（大小/SHA256/恢复步骤）
│  ├─ 工具链方案.md      装什么 + 各功能怎么开发 + 里程碑
│  ├─ 引脚定义.md        排针表 + 按键 + 屏驱动 IC（ST7789）取证 + 待补内部引脚
│  └─ img\               丝印、摄像头标签、接线标注、官方引脚图
├─ scripts\
│  ├─ chip_info.ps1     一键探测板子（端口 / 芯片 / Flash / 分区）
│  ├─ serial_monitor.py 串口监视 + 复位抓启动日志
│  ├─ fw_inspect.py     固件离线分析（分区表 / app 描述符 / 关键词串）
│  └─ push_log.ps1      收工一键：提交并推送开发日志仓库
├─ 日志\                开发日志（**独立 git 仓库**，自己的远端，已 gitignore）
├─ dump\                Flash 备份（已 gitignore，不入库）
├─ logs\                串口日志（已 gitignore）
└─ .venv\               Python 虚拟环境（esptool + pyserial，已 gitignore）
```

## 开发日志（独立仓库）

日志与代码**分开管理**：日志记过程（文字/照片/命令/决策），本仓库放工程代码。

- 日志仓库存放位置：`E:\ESP32开发\日志`（自带 `.git`，不被本仓库跟踪）
- 远端：`git@github.com:papercatno1/esp32-embedded-devlog.git`
- 规则：每次对话追加一个"回合"小节到当天 `日志\YYYY-MM-DD\日志.md`，只增不改；图片放 `图片\NN-说明.jpg`
- 收工推送：

```powershell
.\scripts\push_log.ps1                                          # 自动用当天日期做提交信息
.\scripts\push_log.ps1 -Message "日志: 2026-10-31 点屏记录"      # 自定义提交信息
```

首次使用需在 GitHub 上手动建空仓库 `esp32-embedded-devlog`（脚本会给出链接）。
GitHub 不接受中文仓库名，故仓库名用 ASCII；项目名仍为「ESP32 嵌入式开发」。

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
含 LVGL 界面、摄像头、SD 卡、Wi-Fi 网页服务等任务，界面左栏为 `System / QMI8658 / Camera / WiFi`。

**整片 Flash 已于 2026-10-31 13:51 备份**：`dump\full_flash.bin`（16,777,216 B，SHA256 `31BA1ECF…12FD`），
详见 [docs/固件备份.md](docs/固件备份.md)。刷机出问题可整片回滚。

## 进度

- [x] 硬件识别（Waveshare ESP32-S3-LCD-2）
- [x] 串口连通（COM16，esptool 可读写）
- [x] 出厂固件取证（分区表 / app 描述符 / 关键字符串）
- [x] **整片 16 MB Flash 备份 + 校验**（13:51）
- [x] **屏幕点亮**（40pin FPC，出厂 demo 正常显示 System 页）
- [x] 摄像头安装（OV5640 已插入 24pin 座）
- [x] 工具链方案规划（[docs/工具链方案.md](docs/工具链方案.md)）
- [x] 排针定义 + 屏驱动 IC 确认（ST7789，[docs/引脚定义.md](docs/引脚定义.md)）
- [ ] 摄像头功能验证（跑一次 `esp_camera_init`，看能否出图）
- [ ] 装 PlatformIO（M0：blink + 串口闭环）
- [ ] 开发方案定稿 → 建项目仓库

## 出厂 demo 的界面真相（从固件字符串表挖出来的）

app 区 `0x1039e` 附近整张 UI 文案表：

```
system_task | app_wifi_task | System | QMI8658 | Camera | WiFi
accel_x | accel_y | accel_z | gyro_x | gyro_y | gyro_z
Chip | ESP32-S3 | ChipTemp | 0.0C | PSRAM | FLASH | SD | Battery | 0.0V
WiFi STA Setting | 0.0.0.0 | PASS | No data
```

- 左栏四页 `System / QMI8658 / Camera / WiFi` 是**点按导航**（这套 demo 与触摸版共用），本机无触摸 → 点不动。
- `WiFi STA Setting` 表明 Wi-Fi 页是**在屏上输 SSID/密码**的 STA 配置页，不是热点；NVS 里还留着微雪工厂测试的
  `sta.ssid = WSTEST` / `sta.pswd = waveshare0755`（他们自己的测试路由，这里用不上）。
- 结论：**出厂 demo 的 Camera 页在这块无触摸板上走不通**，验证摄像头只能靠刷自检固件（见工具链方案 M2）。

## 后续可做

- 按 [docs/工具链方案.md](docs/工具链方案.md) 装 PlatformIO，跑 M0→M1→M2：blink / 点屏 / 摄像头自检
- 摄像头自检顺带定论"FPC 到底接对没有"（`esp_camera_init` 返回 `ESP_OK` = 对）
- 定方案后开工程：LVGL 界面 / 拍照图传 / SD 卡录像 / 传感器终端
