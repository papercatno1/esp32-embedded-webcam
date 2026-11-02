# ESP32 开发项目

工作目录：`E:\ESP32开发`
设备：**微雪 Waveshare ESP32-S3-LCD-2**（2 寸 IPS 240×320，ESP32-S3R8 + 8 MB PSRAM + 16 MB Flash，OV5640 摄像头，屏幕无触摸）

**当前状态（v0.9.0，2026-11-02）**：网页图传（v0.2.1 含 HTTP OTA）、**票据屏固件 0.4.0**（双字号 16/24px + 熄屏唤醒 + 翻牌翻页 + 页脚状态 + 文字/图片上屏 + 三模式：票据屏/原生系统/照相（MJPEG 网页））与**安卓 App 0.6.1「ESP32 控制台」**（功能菜单 + 原生系统页 + 照相 WebView + 票据屏：文字推送、两步发图）。
- 网页图传：`http://esp32cam.local/`（实测 `192.168.88.32`），80 端口控制面 / 8080 端口 MJPEG 数据面
- 票据屏：BLE `ESP32-NOTE`（NUS 服务）接收手机推送的票据文本，存 FFat 并显示在屏幕上；**24px 大字，60 秒无交互自动熄屏，敲一下板子点亮**；`http://esp32note.local/ota` 可无线升级
- 安卓 App「票据屏」：扫描连接 → 粘贴文本 → 发送，见 [docs/安卓App.md](docs/安卓App.md)
- 摄像头 `PID=0x5640`（OV5640）实测通过；整片 Flash 已备份可回滚
- 详见 [CHANGELOG.md](CHANGELOG.md) · [firmware/webcam](firmware/webcam/README.md) · [firmware/notepad](firmware/notepad/README.md) · [android/](docs/安卓App.md) · [docs/](docs/)


## 目录结构

```
E:\ESP32开发
├─ firmware\webcam\      ▶ 网页图传工程（PlatformIO）
│  ├─ platformio.ini        esp32s3 + arduino + 8MB PSRAM + 16MB 分区
│  ├─ partitions_16MB.csv   与出厂固件同布局
│  ├─ README.md             编译/烧录/接口说明/排查表
│  └─ src\                  main.cpp / camera_pins.h / wifi_config.h / www.h
├─ firmware\notepad\     ▶ 票据屏工程（BLE + 中文点阵字库 + FFat + OTA + IMU 敲击唤醒）
│  ├─ data\cjk16.bin        16×16 字库（7540 字，301 KB，编入 flash）
│  ├─ data\cjk24.bin        24×24 字库（7540 字，603 KB，正文默认用这套）
│  └─ src\                  main.cpp / cjk.* / notes.* / ui.* / qmi8658.* / power.* / wifi_config.h
├─ android\              ▶ 手机端 App（Kotlin，零第三方依赖，见 docs/安卓App.md）
│  └─ app\src\main\        MainActivity.kt / BleLink.kt / activity_main.xml
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
│  ├─ 引脚定义.md        v2：官方原理图核对过的完整引脚表 + 坑 + 可粘贴代码
│  ├─ datasheet\         官方原理图 PDF（V2）、WorkBuddy 整理稿
│  └─ img\               丝印、摄像头标签、接线标注、官方引脚图、原理图矩阵截图
├─ scripts\
│  ├─ chip_info.ps1     一键探测板子（端口 / 芯片 / Flash / 分区）
│  ├─ serial_monitor.py 串口监视 + 复位抓启动日志（带自动重连）
│  ├─ flash.ps1         编译 + 双槽烧录 + 跟串口（-Project webcam|notepad）
│  ├─ cam_test.ps1      图传自测：/status + /capture + /shoot
│  ├─ verify_cam.py     图传端到端自测（状态/单帧/拍照/流与控制面并发）
│  ├─ ota.ps1           无线升级：编译 + POST /update + 等待上线 + 版本比对
│  ├─ ble_test.py       票据屏自测：扫描 → 连接 → 下发 → 收回执
│  ├─ m2_test.py        熄屏/唤醒自测（自动熄屏、误唤醒、指令唤醒）
│  ├─ gen_cjk_font.py   中文点阵字库生成（系统字体 → cjk{N}.bin，--size 16/24）
│  ├─ preview_ui.py     离线渲染 320×240 版式预览 PNG（不用连板子）
│  ├─ setup_android.ps1 Android 工具链一键安装（装到 tools\）
│  ├─ build_apk.ps1     构建安卓 App 并复制到 dist\
│  ├─ fw_inspect.py     固件离线分析（分区表 / app 描述符 / 关键词串）
│  ├─ webget.py         本地抓网页转文本（绕开本机 fake-ip 导致的 fetch 限制）
│  ├─ push_log.ps1      收工一键：提交并推送开发日志仓库
│  └─ push_project.ps1  收工一键：提交并推送本工程仓库
├─ 日志\                开发日志（**独立 git 仓库**，自己的远端，已 gitignore）
├─ tools\               本地工具链 JDK17 / Android SDK / Gradle（已 gitignore）
├─ dist\                构建产物（APK 等，已 gitignore）
├─ dump\                Flash 备份（已 gitignore，不入库）
├─ logs\                串口日志、抓到的快照（已 gitignore）
└─ .venv\               Python 虚拟环境（esptool + pyserial + platformio，已 gitignore）
```

## 网页图传（第一版）

```powershell
cd E:\ESP32开发
.\scripts\flash.ps1                    # 编译 + 烧录 + 跟串口（推荐）
.\scripts\verify_cam.py 192.168.88.32  # 端到端自测：状态/单帧/拍照/流与控制面并发
```

上电后板子连家里 Wi-Fi，访问 `http://<板子IP>/` 或 `http://esp32cam.local/`；
连不上路由时自动开热点 `ESP32-LCD2-CAM / 12345678`（`192.168.4.1`）。
Wi-Fi 凭据放 `firmware/webcam/src/wifi_secrets.h`，**该文件不入库**。

| 接口 | 作用 |
| --- | --- |
| `/` | 网页界面 |
| `/stream` | MJPEG 实时流 |
| `/capture` | 下载当前帧 |
| `/shoot` | 拍照存 TF 卡 `/photo/` |
| `/status` | JSON 状态 |
| `/restart` | 软重启 |

连上热点后一条命令自测：`.\scripts\cam_test.ps1`


## 票据屏（蓝牙推送 + 中文显示）

手机 App 把文本通过 BLE（NUS 服务）发给板子，板子存进 FFat 并显示在 320×240 屏上；最多 8 条，可翻页/删除。

```powershell
.\scripts\flash.ps1 -Project notepad   # 烧票据屏固件
.\scripts\ble_test.py                  # 电脑侧端到端自测（需要 BLE 适配器）
.\scripts\setup_android.ps1            # 首次：装安卓工具链（tools\，约 13 分钟）
.\scripts\build_apk.ps1                # 构建 App → dist\票据屏-debug.apk
```

设备名 `ESP32-NOTE`；指令以 `\n` 结尾，回执走 notify：

| 指令 | 说明 |
| --- | --- |
| `V` | 版本 |
| `T 标题\|行1\|行2…` | 新增条目并切到该条 |
| `L` | 列出全部条目 |
| `P <id>` / `N` / `R` | 切到指定条 / 下一条 / 上一条 |
| `D <id>` / `C` | 删除一条 / 全部清空 |
| `B <0-100>` / `S <0\|1>` | 亮度 / 手动熄屏·点亮 |
| `O <秒>` | 自动熄屏超时（0 = 常亮，默认 60 秒） |
| `K <1\|-1\|t>` | 甩动翻页方向（`t`=翻转），存 NVS，重启不丢 | `OK K <1\|-1>` |
| `Q` | 甩动统计：次数 / 最近角度 / 轴 + 实时与峰值角速度 + 当前页码 |
| `I <w> <h>` | 开始收一张图（w≤320, h≤212） | `OK I START WxH` |
| `A <base64>` | 送一段 RLE（base64，可多次） | `OK A <累计字节>` |
| `E` | 结束收图（解码 + 落盘 + 上屏） | `OK E <id> <字节> <FNV32>` |
| `Z` | 放弃本次收图 | `OK Z` |
| `M` | 查 TF 卡状态（类型/容量/能否写） | `OK M <类型> <MB> w=<0\|1>` / `ERR NOCARD` / `ERR NOFAT` |
| `MODE sys\|note\|cam` | 切换运行模式（原生系统 / 票据屏 / 照相占位） | `OK MODE <名字>` |
| `SYS` | 系统信息一次取全 | `OK SYS ver=.. mode=.. psram=..MB flash=..MB tf=..MB bat=..V temp=..C heap=..KB wifi=.. ble=.. up=..s notes=..` |
| `A` / `G` | 读三轴加速度 / 加速度 + 陀螺仪 |

熄屏后敲一下板子（或桌面）就亮，不用按键——`GPIO0` 是 BOOT + LCD 复位脚，碰它会复位屏幕。
翻页同理：**顺时针/拧紧 = 下一条，逆时针/拧松 = 上一条**（默认 `sn=-1`）。方向反了用 `K t` 现场翻转，存在 NVS 里，重启不丢；`Q` 看次数/角度/峰值。

App 使用说明见 [docs/安卓App.md](docs/安卓App.md)，字库细节见 [docs/中文点阵字库.md](docs/中文点阵字库.md)。

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
- [x] **完整引脚表**（官方原理图 V2 核对：LCD / 摄像头 / I²C / SD / ADC 全部到手）
- [x] 摄像头验证（`esp_camera_init` 通过，PID `0x5640`）
- [x] 网页图传 v0.1.0（页面 / MJPEG / 拍照 / 状态 / 屏幕 IP 显示）
- [x] 断链根治（lwIP 10 socket 上限 → 8080 独立数据面）· v0.2.0
- [x] HTTP OTA 无线升级（`/ota` + `POST /update`）· v0.2.1
- [x] 票据屏 M1：BLE NUS + 中文点阵 + FFat + 翻页 · v0.3.0
- [x] 安卓 App 0.1.0：扫描连接 + 粘贴 + 发送 · v0.4.0
- [x] 票据屏 M2：24px 大字 + 60 秒熄屏 + IMU 敲击唤醒 · v0.5.0
- [x] 票据屏 0.3.0：页脚状态（连接/熄屏设置/页码）+ 陀螺仪甩动翻页 + App 上一条/下一条 · v0.6.0
- [x] 甩动标定：量程 ±2048 dps + dt 积分 + 方向可翻转并存 NVS + 串口下指令 · v0.6.4
- [x] 传图片上屏：1-bit + RLE + FFat 持久化（3 张）+ App 选图发送 · v0.7.0
- [ ] 票据屏 M3：截图裁剪上传（手机上框选 → 二值图 → BLE 分包）
- [ ] 票据屏 M4：OCR 自动裁票面

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

- 票据屏 M3：手机截屏 → 裁剪 → 1-bit 二值化 → BLE 分包上传（`I` 指令 + RLE，2–5 KB 一屏）
- 票据屏 M4：OCR 自动定位票面（手机上抓文字块坐标，裁得更准）
- 运动侦测推送 / SD 卡录像 / 屏幕第二页（IMU 与电量）
- 摄像头自检固件仍在 `firmware/webcam`，两者共用 OTA 机制，互刷靠 `flash.ps1 -Project`

