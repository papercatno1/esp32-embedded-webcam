# 更新记录

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
