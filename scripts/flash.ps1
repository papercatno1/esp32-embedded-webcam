#!/usr/bin/env pwsh
<#
.SYNOPSIS
  编译 + 烧录网页图传固件（走 esptool，绕开 PlatformIO 上传在这块板子 USB-CDC 上的不稳定）
.EXAMPLE
  .\scripts\flash.ps1
  .\scripts\flash.ps1 -Port COM16 -Baud 460800
#>
param(
  [string]$Port = "COM16",
  [int]$Baud = 460800,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$py   = Join-Path $root '.venv\Scripts\python.exe'
$proj = Join-Path $root 'firmware\webcam'
$bin  = Join-Path $proj '.pio\build\esp32s3\firmware.bin'

if (-not $SkipBuild) {
  Write-Host "== 编译 ==" -ForegroundColor Cyan
  Push-Location $proj
  try { & $py -m platformio run | Select-Object -Last 6 } finally { Pop-Location }
}

if (-not (Test-Path $bin)) { throw "找不到 $bin，先编译" }
$size = (Get-Item $bin).Length
Write-Host ("`n== 烧录 {0:N0} 字节 -> {1} 0x10000 ==" -f $size, $Port) -ForegroundColor Cyan

& $py -m esptool -p $Port -b $Baud write-flash 0x10000 $bin

Write-Host "`n== 看日志（Ctrl+C 退出）==" -ForegroundColor Cyan
& $py (Join-Path $root 'scripts\serial_monitor.py') -p $Port -b 115200
