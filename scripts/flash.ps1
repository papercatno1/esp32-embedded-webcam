#!/usr/bin/env pwsh
<#
.SYNOPSIS
  编译 + 烧录固件（走 esptool，绕开 PlatformIO 上传在这块板子 USB-CDC 上的不稳定）
.DESCRIPTION
  -Project 指定固件目录（webcam / notepad）。
  默认同时写入 app0(0x10000) 与 app1(0x310000) 并清空 otadata：
  换固件时两槽一致，避免 OTA 留下的 otadata 让板子启动到旧固件。
.EXAMPLE
  .\scripts\flash.ps1 -Project notepad
  .\scripts\flash.ps1 -Project webcam -Port COM16 -Baud 921600
#>
param(
  [string]$Project = "webcam",
  [string]$Port = "COM16",
  [int]$Baud = 460800,
  [switch]$SkipBuild,
  [switch]$App0Only,
  [switch]$NoMonitor
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$py   = Join-Path $root '.venv\Scripts\python.exe'
$proj = Join-Path $root "firmware\$Project"
$bin  = Join-Path $proj '.pio\build\esp32s3\firmware.bin'

# 等串口出现（板子复位后 USB CDC 会重新枚举，期间端口短暂消失）
function Wait-Port([string]$Name, [int]$TimeoutSec = 15) {
  for ($i = 0; $i -lt $TimeoutSec * 2; $i++) {
    if (Test-Path "\\.\$Name") { return $true }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

if (-not (Test-Path $proj)) { throw "工程不存在: $proj" }

if (-not $SkipBuild) {
  Write-Host "== 编译 $Project ==" -ForegroundColor Cyan
  Push-Location $proj
  try { & $py -m platformio run | Select-Object -Last 6 } finally { Pop-Location }
}

if (-not (Test-Path $bin)) { throw "找不到 $bin，先编译" }
$size = (Get-Item $bin).Length
Write-Host ("`n== 烧录 {0:N0} 字节 -> {1} ==" -f $size, $Port) -ForegroundColor Cyan

if ($App0Only) {
  $null = Wait-Port -Name $Port
  & $py -m esptool -p $Port -b $Baud write-flash 0x10000 $bin
} else {
  # 先清 otadata 再写两槽：两次 esptool 之间不会插入复位，避免 CDC 重枚举导致端口消失
  $null = Wait-Port -Name $Port
  & $py -m esptool -p $Port -b $Baud erase-region 0xe000 0x2000
  $null = Wait-Port -Name $Port
  & $py -m esptool -p $Port -b $Baud write-flash 0x10000 $bin 0x310000 $bin
}

if ($NoMonitor) { exit 0 }
Write-Host "`n== 看日志（Ctrl+C 退出）==" -ForegroundColor Cyan
& $py (Join-Path $root 'scripts\serial_monitor.py') -p $Port -b 115200
