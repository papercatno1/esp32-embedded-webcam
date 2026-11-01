# 更新记录

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
