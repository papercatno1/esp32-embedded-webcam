# 票据屏固件（firmware/notepad）M1 + M2

ESP32-S3-LCD-2 的第二个固件：**手机通过蓝牙把票据/便签文本推到屏幕上**，设备存下来随时看；
一段时间没人管就熄屏省电，**敲一下板子亮起来**。
Wi-Fi 只用于 OTA 升级，连不上也不影响 BLE 主功能。

## 硬件与依赖

| 项 | 值 |
|---|---|
| 屏幕 | ST7789T3 320×240，SPI：SCLK 39 / MOSI 38 / DC 42 / CS 45 / BL 1 |
| IMU | QMI8658A（I2C：SDA 48 / SCL 47，地址 0x6A），只开加速度计做敲击检测 |
| 存储 | FFat（`ffat` 分区，10 MB） |
| 蓝牙 | NimBLE-Arduino 1.4.x（arduino-esp32 2.0.17 / IDF 4.4） |
| 图形 | GFX Library for Arduino 1.4.9（**不能升 1.5+**，它要 core 3.x） |
| 中文字库 | `data/cjk16.bin` 295 KB（小字）+ `data/cjk24.bin` 589 KB（大字），各 7540 字 |

## 编译 / 烧录

```powershell
# 生成中文字库（首次或换字体时）
.\.venv\Scripts\python.exe scripts\gen_cjk_font.py --size 16
.\.venv\Scripts\python.exe scripts\gen_cjk_font.py --size 24

# 离线预览版式（不用连板子，直接出 PNG）
.\.venv\Scripts\python.exe scripts\preview_ui.py --out docs\img\票据屏-预览.png

# 编译 + 烧录（双槽写入 + 清 otadata，避免启动到旧固件）
.\scripts\flash.ps1 -Project notepad
```

> PIO 自带的 `-t upload` 在板载 USB-CDC 上会失败（切波特率后握手不上），一律走 esptool。

## BLE 协议

设备名 `ESP32-NOTE`，Nordic UART Service：

| 角色 | UUID | 属性 |
|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | – |
| RX（手机→设备） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write / WriteNoRsp |
| TX（设备→手机） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify |

一行一条指令，`\n` 结尾，UTF-8；回执通过 TX notify 返回。

| 指令 | 说明 | 回执 |
|---|---|---|
| `V` | 查版本 | `OK V 0.2.0` |
| `T 标题\|行1\|行2\|…` | 新增条目（`\|` 分隔，最多 12 行） | `OK T <id> <总数>` |
| `L` | 列出条目 | `OK L <总数>` + 每条 `ITEM <id> <标题>` |
| `P <id>` | 显示指定条目 | `OK P <第几条>` |
| `N` / `R` | 下一条 / 上一条 | `OK P <第几条>` |
| `D <id>` | 删除 | `OK D` / `ERR NOTFOUND` |
| `C` | 清空 | `OK C` |
| `B <0-100>` | 背光亮度 | `OK B <pct>` |
| `S <0\|1>` | 手动熄屏 / 点亮 | `OK S <0\|1>` |
| `O <秒>` | 自动熄屏超时，`0` = 常亮 | `OK O <秒>` |
| `A` | 读加速度（g） | `OK A <x> <y> <z>` / `ERR IMU` |
| 其它 | – | `ERR UNKNOWN` |

示例：

```text
T 高铁票 G106|2026-11-02 08:25 开|济南西 → 北京南|二等座 12车 12A号|票价 314.5 元
```

## 自测

```powershell
.\.venv\Scripts\python.exe scripts\ble_test.py          # BLE 全流程
.\.venv\Scripts\python.exe scripts\ble_test.py --cmd A  # 读一次加速度
.\.venv\Scripts\python.exe scripts\m2_test.py           # 熄屏/唤醒自测（敲击那步要人手动敲）
```

## 版式（320×240，24px 大字）

```text
┌──────────────────────────────┐
│ 标题（24px，最多 13 字）   1/3 │ 34px 标题条
├──────────────────────────────┤
│ 正文 24px，行距 28，最多 6 行  │
│ …                            │
├──────────────────────────────┤ 212
│ BLE: ESP32-NOTE        3 条   │ 16px 页脚
└──────────────────────────────┘
```

效果图：[docs/img/票据屏-预览.png](../../docs/img/票据屏-预览.png)（`scripts/preview_ui.py` 离线渲染）。

## M2：熄屏与敲击唤醒

- 上电默认 60 秒无交互熄屏（`O 60`），`O 0` 可关掉自动熄屏
- 熄屏时只关背光，屏幕内容保留；任何 BLE 指令、翻页都会自动点亮
- 熄屏期间每 25 ms 读一次 QMI8658 加速度，相邻采样三轴变化量之和 > 0.75 g 且距上次敲击 > 500 ms 判定为"敲一下"
- **IMU 不可用时不熄屏**（否则唤不醒，只能复位）
- 没有用 IMU 的 INT1 引脚：`GPIO3 = LCD_TE = IMU_INT1`，中断走轮询代替，省掉引脚冲突

## 关键实现点

- **BLE 写入是分片的**：MTU 517 下一条长指令会分多包，固件按 `\n` 累积重组后再执行
- **指令在 loop() 里执行**：BLE 回调只把整行塞进 FreeRTOS 队列，避免在 BLE 任务里做 SPI 刷屏和 FAT 写入
- **两套字号共用一套记录格式**：单字记录长度 = `8 + ceil(w/8) * h`（16px 为 40 B，24px 为 80 B），按码点二分查找
- **`GPIO0 = LCD_RST = BOOT`**：软件里不能把 GPIO0 拉低，否则会进下载模式
- 条目上限 8 条，超出挤掉最旧的；单行 100 B（约 33 汉字），标题 64 B

## 已知限制

- 每次指令全屏重绘（约 40 ms，肉眼看不出，但都不是局部刷新）
- 图片/截图负载（`I` 指令 + 1-bit RLE）尚未实现（M3）
- BLE 无配对加密，同频段任何人都能写——计划加 6 位 PIN 握手
- 敲击阈值 0.75 g 是估的，真机上如果桌子轻微震动就误亮，把 `power.cpp` 的 `TAP_THRESHOLD` 调大
