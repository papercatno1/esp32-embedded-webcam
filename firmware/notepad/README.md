# 票据屏固件（firmware/notepad）M1

ESP32-S3-LCD-2 的第二个固件：**手机通过蓝牙把票据/便签文本推到屏幕上**，设备存下来随时看。
Wi-Fi 只用于 OTA 升级，连不上也不影响 BLE 主功能。

## 硬件与依赖

| 项 | 值 |
|---|---|
| 屏幕 | ST7789T3 320×240，SPI：SCLK 39 / MOSI 38 / DC 42 / CS 45 / BL 1 |
| 存储 | FFat（`ffat` 分区，10 MB） |
| 蓝牙 | NimBLE-Arduino 1.4.x（arduino-esp32 2.0.17 / IDF 4.4） |
| 图形 | GFX Library for Arduino 1.4.9（**不能升 1.5+**，它要 core 3.x） |
| 中文字库 | `data/cjk16.bin`，295 KB，7540 字（GB2312 全字集 + ASCII） |

## 编译 / 烧录

```powershell
# 生成中文字库（首次或换字体时）
.\.venv\Scripts\python.exe scripts\gen_cjk_font.py

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
| `V` | 查版本 | `OK V 0.1.0` |
| `T 标题\|行1\|行2\|…` | 新增条目（`\|` 分隔，最多 12 行） | `OK T <id> <总数>` |
| `L` | 列出条目 | `OK L <总数>` + 每条 `ITEM <id> <标题>` |
| `P <id>` | 显示指定条目 | `OK P <第几条>` |
| `N` / `R` | 下一条 / 上一条 | `OK P <第几条>` |
| `D <id>` | 删除 | `OK D` / `ERR NOTFOUND` |
| `C` | 清空 | `OK C` |
| `B <0-100>` | 背光亮度 | `OK B <pct>` |
| `S <0\|1>` | 背光关/开 | `OK S <0\|1>` |
| 其它 | – | `ERR UNKNOWN` |

示例：

```text
T 高铁票 G106|2026-11-02 08:25 开|济南西 → 北京南|二等座 12车 12A号|票价 314.5 元
```

## 自测

```powershell
.\.venv\Scripts\python.exe scripts\ble_test.py          # 全流程
.\.venv\Scripts\python.exe scripts\ble_test.py --cmd V  # 单条
```

## 关键实现点

- **BLE 写入是分片的**：MTU 517 下一条长指令会分多包，固件按 `\n` 累积重组后再执行
- **指令在 loop() 里执行**：BLE 回调只把整行塞进 FreeRTOS 队列，避免在 BLE 任务里做 SPI 刷屏和 FAT 写入
- **字库按码点二分**：`cjk16.bin` 记录定长 40 B（cp u32 / advance u8 / pad3 / bitmap 32 B），查表 O(log n)
- **`GPIO0 = LCD_RST = BOOT`**：软件里不能把 GPIO0 拉低，否则会进下载模式
- 条目上限 8 条，超出挤掉最旧的；单行 100 B（约 33 汉字），标题 64 B

## 已知限制（M2 待办）

- 画面内容变化才刷新，目前每次指令全屏重绘（有轻微闪烁）
- 无自动熄屏/敲击唤醒（IMU 中断在 GPIO3，与 LCD TE 共用，需二选一）
- 图片/截图负载（`I` 指令 + 1-bit RLE）尚未实现
- BLE 无配对加密，同频段任何人都能写——M2 加 6 位 PIN 握手
