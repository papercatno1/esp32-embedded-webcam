# 更新记录

## v0.4.0 — 2026-11-01 · 安卓 App（票据屏手机端）

**新增 `android/`：票据屏 App 0.1.0**
- Kotlin + 平台 API，**零第三方依赖**，APK 809 KB，minSdk 26 / targetSdk 34
- 扫描（按设备名 `ESP32-NOTE` + NUS 服务 UUID 双过滤，12 秒自动停扫）→ 连接 → 协商 MTU(517) → 订阅回执
- 输入框多行文本：第一行当标题，其余行当正文，拼成 `T 标题|行1|…` 一次发完；支持「粘贴」剪贴板
- 按 `max(20, mtu-3)` 分片顺序写（`onCharacteristicWrite` 驱动 + 25 ms 间隔），1000 字节上限
- 按钮：查版本 `V` / 看列表 `L` / 清空设备 `C`；日志区实时显示 `->` 发出与 `<-` 回执

**工具**
- `scripts/setup_android.ps1`：JDK17 + Android SDK(platform-tools, android-34, build-tools 34.0.0) + Gradle 8.7 一键装到 `tools\`
- `scripts/build_apk.ps1`：构建并复制到 `dist\票据屏-debug.apk`

**坑**
- 工程在中文路径下 AGP 直接拒构建 → `gradle.properties` 加 `android.overridePathCheck=true`

## v0.3.0 — 2026-11-01 · 票据屏 M1（蓝牙推送 + 中文显示）

**新固件 `firmware/notepad`**
- BLE NUS 服务（`6E400001/2/3-B5A3-F393-E0A9-E50E24DCCA9E`）：手机写入指令，设备 notify 回执；实测 MTU 协商到 517
- 指令集：`V` 版本 · `T` 新增条目 · `L` 列表 · `P/N/R` 切页 · `D` 删除 · `C` 清空 · `B` 亮度 · `S` 背光开关
- 条目存 FFat（10 MB 分区），掉电不丢，最多保留 8 条
- 16×16 中文字库（GB2312 全字集 7540 字，295 KB）编入 flash，按 UTF-8 码点二分查找
- 320×240 UI：标题条 + 自动折行正文（每行最多 20 个汉字）+ 页码
- 保留 HTTP OTA（`http://esp32note.local/ota`）与 `/status.json`

**工具**
- `scripts/gen_cjk_font.py`：从系统字体生成点阵字库二进制
- `scripts/ble_test.py`：扫描 → 连接 → 下发 → 收回执，端到端自测
- `scripts/setup_android.ps1`：Android 工具链一键安装（JDK17 + cmdline-tools + SDK + Gradle）
- `scripts/flash.ps1`：新增 `-Project`；改为双槽写入 + 先清 otadata，避免 OTA 残留把板子启动到旧固件

**实测**
- BLE 全流程通过：`OK V 0.1.0` → `OK C` → `OK T 1 1` → `ITEM 1 高铁票 G106` → `OK P 1`
- 128 字节含中文的 `T` 指令一次写入成功，条目落盘，屏幕显示，`/status.json` 中 `notes=1`

## v0.2.1 — 2026-11-01 · HTTP OTA 无线升级

**固件 `firmware/webcam`**
- 新增 `GET /ota` 上传页（手机/电脑浏览器都能用，带进度条）
- 新增 `POST /update` 接收原始 app 镜像，边收边写另一侧 app 分区，完成后自动重启
- 屏幕升级进度：大字号百分比 + 进度条；串口每 5% 打一行
- `/status.json` 增加 `ver` 字段；启动横幅带版本号
- 升级前自动踢掉流客户端，避免抢 socket 与内存

**实测**
- 943 KB 镜像：4.5–5.8 秒传完（157–210 KB/s），约 9 秒重启回新版本
- 连续两次升级均成功，版本号正确切换

**工具**
- `scripts/ota.ps1`：编译 + 推送 + 等待上线，一条命令完成无线升级

## v0.1.0 — 2026-10-31 · 首版网页图传

**硬件**
- 识别并核实：微雪 Waveshare ESP32-S3-LCD-2（ESP32-S3R8 / 8 MB PSRAM / 16 MB Flash）
- 摄像头 OV5640 实测通过（`PID=0x5640`）；屏幕 ST7789T3 点亮
- 整片 16 MB Flash 备份 + 三重校验（`docs/固件备份.md`）
- 引脚表按官方原理图 V2 逐行核对（`docs/引脚定义.md`）

**固件 `firmware/webcam`**
- OV5640 抓帧 → 浏览器 MJPEG 实时预览（约 15–20 fps @ VGA）
- 网络：优先连家里路由（STA），15 秒连不上自动退回热点；mDNS `esp32cam.local`
- 架构：**80 端口控制面 / 8080 端口数据面**（绕开 httpd 单任务模型与 lwIP 10 socket 上限）
- 屏幕状态页：IP、mDNS 名、信号、帧率、观看端、TF 卡、堆、运行时长，每 3 秒刷新
- 接口：`/`、`/status`、`/status.json`、`/diag`、`/simple`、`/capture`、`/shoot`、`/restart`
- 流连接自愈：单观看端、新连接顶掉旧的、`SO_LINGER` 分级关闭、页面 JS 看门狗

**工具**
- `scripts/flash.ps1` 编译 + esptool 烧录 + 串口
- `scripts/verify_cam.py` 端到端自测（状态 / 单帧 / 拍照 / 流与控制面并发）
- `scripts/serial_monitor.py` 串口监视（USB-CDC 断线自动重连）
- `scripts/fw_inspect.py` 固件离线分析；`scripts/webget.py` 本机 fake-ip 环境网页抓取

**验收**
| 场景 | 结果 |
| --- | --- |
| 间隔 3 秒刷新页面 | 6/6 |
| 立即重连 | 6/6 |
| 旧连接重叠 | 6/6 |
| 边刷 `/capture` 边连流 | 6/6 |
