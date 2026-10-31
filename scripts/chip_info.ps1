#!/usr/bin/env pwsh
<#
.SYNOPSIS
  一键探测 ESP32 板子：串口枚举 + 芯片/Flash 信息 + 分区表。
.EXAMPLE
  .\scripts\chip_info.ps1
  .\scripts\chip_info.ps1 -Port COM16
#>
param(
  [string]$Port = "",
  [int]$Baud = 460800
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$py   = Join-Path $root '.venv\Scripts\python.exe'
if (-not (Test-Path $py)) { throw "找不到虚拟环境: $py（先在项目根目录创建 .venv 并安装 esptool pyserial）" }

Write-Host "== 1) 枚举 Espressif USB 串口 ==" -ForegroundColor Cyan
$ports = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -like '*VID_303A*' }
if (-not $ports) {
  Write-Host "未发现 VID_303A(Espressif) 串口，检查 USB 线/驱动" -ForegroundColor Yellow
} else {
  $ports | Select-Object DeviceID, Name, PNPDeviceID | Format-List
}

if (-not $Port) {
  $Port = ($ports | Select-Object -First 1).DeviceID
  if (-not $Port) { throw "没有可用的 Espressif 串口，请用 -Port 指定" }
}
Write-Host "使用端口: $Port" -ForegroundColor Cyan

Write-Host "`n== 2) 芯片 / Flash 信息 ==" -ForegroundColor Cyan
& $py -m esptool -p $Port -b $Baud flash-id

Write-Host "`n== 3) 分区表 (0x8000, 4KB) ==" -ForegroundColor Cyan
$ptPath = Join-Path $env:TEMP 'esp32_partitions.bin'
& $py -m esptool -p $Port -b $Baud read-flash 0x8000 0x1000 $ptPath | Out-Null
& $py -c @"
import struct
d = open(r'$ptPath','rb').read()
for i in range(0, len(d), 32):
    e = d[i:i+32]
    if e[0:2] != b'\xaa\x50': break
    t, st = e[2], e[3]
    off, sz = struct.unpack('<II', e[4:12])
    label = e[12:28].split(b'\x00')[0].decode('ascii','replace')
    print(f'  {label:10s} type=0x{t:02x} sub=0x{st:02x} offset=0x{off:06x} size=0x{sz:06x} ({sz/1024:.0f}KB)')
"@
