# 安卓 App「票据屏」（android/）

作用：手机上粘贴一段文本 → 通过蓝牙发给板子 → 板子存进 FFat 并显示在 320×240 屏上。
设备端固件是 [`firmware/notepad`](../firmware/notepad/README.md)（BLE 名 `ESP32-NOTE`）。

## 一、怎么用

1. 板子上电（跑 notepad 固件），屏幕显示 `等待手机连接`。
2. 手机装 APK：`dist\票据屏-debug.apk`（构建方法见第四节；也可 `adb install -r` 直接装）。
3. 打开 App → 点 **扫描连接** → 首次会弹蓝牙权限，允许。
   - App 只认名字带 `ESP32-NOTE` 或带 NUS 服务 UUID 的设备。
   - 连上后状态栏变成 `已连接 · MTU 517`。
4. 把要显示的内容写进输入框：**第一行是标题，其余行是正文**，例如

   ```
   高铁票 G106
   2026-11-02 08:25 开
   济南西 → 北京南
   二等座 12车 12A号
   ```

   也可以先在别的 App 里选好文本 → 复制 → 点 **粘贴**。
5. 点 **发送**。日志区出现 `OK T 1 1` 就表示板子已存下并切换到该条；屏幕上同时刷新。
6. 其它按钮：**查版本**(`V`)、**看列表**(`L`)、**清空设备**(`C`)。

> 命令都是以 `\n` 结尾的纯文本行，回执走 BLE notify，App 日志区里 `->` 是发出的，`<-` 是收到的。

## 二、权限说明

| 权限 | 用途 |
| --- | --- |
| `BLUETOOTH_SCAN`（从不用于定位） | Android 12+ 扫描 |
| `BLUETOOTH_CONNECT` | Android 12+ 连接/读写特征 |
| `BLUETOOTH` / `BLUETOOTH_ADMIN` / `ACCESS_FINE_LOCATION` | 仅 Android 11 及以下需要 |

## 三、代码结构

```
android/
├─ settings.gradle.kts / build.gradle.kts / gradle.properties
└─ app/
   ├─ build.gradle.kts              compileSdk 34 / minSdk 26，零第三方依赖
   └─ src/main/
      ├─ AndroidManifest.xml
      ├─ java/com/papercatno1/notepad/
      │  ├─ MainActivity.kt         界面 + 权限 + 拼指令
      │  └─ BleLink.kt              BLE 扫描/连接/MTU/分片写
      └─ res/{layout,values}/
```

关键点：

- **零依赖**：只用平台 `BluetoothLeScanner` + `BluetoothGatt`，不引 AndroidX/Jetpack，构建几十秒完成，APK 不到 1 MB。
- **分片写**：`chunk = max(20, mtu - 3)`；MTU 协商到 517 时一包能带 514 字节，128 字节的票据指令一包就走完。
- **顺序队列**：分片入 `writeQueue`，`onCharacteristicWrite` 回调里逐个发，片间隔 25 ms，避免 GATT busy 丢包。
- **长文本限制**：一次发送上限 1000 字节（UTF-8），超了会提示拆成两张——设备端单条存的是纯文本，太长屏幕也放不下。

## 四、构建

工具链由 `scripts/setup_android.ps1` 装到 `tools\`（JDK17 + Android SDK + Gradle 8.7，均不入库）：

```powershell
.\scripts\setup_android.ps1      # 只需一次，约 13 分钟
.\scripts\build_apk.ps1          # 之后每次改代码跑这个
```

产物：`android\app\build\outputs\apk\debug\app-debug.apk`，并复制到 `dist\票据屏-debug.apk`。

**坑**：工程位于 `E:\ESP32开发`（中文路径），AGP 默认拒绝构建，`gradle.properties` 里已加
`android.overridePathCheck=true` 放行；本工程没有 NDK 原生代码，所以安全。

## 五、排错

| 现象 | 原因 / 处理 |
| --- | --- |
| 扫描 12 秒什么都没发现 | 板子没上电 / 固件不对（应为 notepad）；先把手机蓝牙关了再开；离板子近一点 |
| 一直停在「发现服务...」 | 设备端 NUS 服务没起来，看串口有没有 `[BLE] advertising` |
| `已连接` 但发送无回执 | 回执订阅没成功（CCCD 写入失败），断开重连一次 |
| 中文变方块 | 固件字库是 GB2312 全集；`¥`(半角)、Emoji、生僻字不在表里 → 改用全角 `￥` |
| 内容太长被拒 | 超过 1000 字节，拆成两张发 |
