#!/usr/bin/env pwsh
<#
.SYNOPSIS
  编译并通过 Wi-Fi 无线升级固件（HTTP OTA，不用插 USB）
.DESCRIPTION
  板子跑 v0.2.0 及以上固件时才有 /update 接口。流程：
    1) 编译出 firmware.bin（约 940 KB）
    2) POST 到 http://<板子IP>/update，边传边写 flash 的另一半分区
    3) 设备自动重启到新固件
  升级期间不要断电；串口与屏幕会显示进度。
.EXAMPLE
  .\scripts\ota.ps1
  .\scripts\ota.ps1 -Host 192.168.88.32
  .\scripts\ota.ps1 -SkipBuild
#>
param(
  [string]$Target = "192.168.88.32",
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
  try { & $py -m platformio run | Select-Object -Last 3 } finally { Pop-Location }
}
if (-not (Test-Path $bin)) { throw "找不到 $bin，先编译" }

$size = (Get-Item $bin).Length
Write-Host ("`n== OTA 推送 {0:N0} 字节 → http://{1}/update ==" -f $size, $Target) -ForegroundColor Cyan

$before = $null
try { $before = (Invoke-RestMethod "http://$Target/status.json" -TimeoutSec 5).ver } catch { }
if ($before) { Write-Host "升级前版本: $before" }

$sw = [System.Diagnostics.Stopwatch]::StartNew()
try {
  $r = Invoke-WebRequest -Uri "http://$Target/update" -Method Post -InFile $bin `
        -ContentType 'application/octet-stream' -TimeoutSec 180 -UseBasicParsing
  $sw.Stop()
  Write-Host ("HTTP {0}  {1}  用时 {2:N1} 秒  ({3:N0} KB/s)" -f `
    $r.StatusCode, $r.Content, $sw.Elapsed.TotalSeconds, ($size / 1KB / $sw.Elapsed.TotalSeconds)) -ForegroundColor Green
} catch {
  $sw.Stop()
  Write-Host "推送异常（设备重启会导致连接中断，属常见现象）: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host "`n== 等待设备重启 ==" -ForegroundColor Cyan
for ($i = 1; $i -le 20; $i++) {
  Start-Sleep -Seconds 2
  try {
    $s = Invoke-RestMethod "http://$Target/status.json" -TimeoutSec 4
    Write-Host ("设备已上线  版本 {0}  uptime {1} ms  heap {2:N0} KB" -f $s.ver, $s.uptime, ($s.heap / 1KB)) -ForegroundColor Green
    if ($before -and $s.ver -eq $before) { Write-Host "注意：版本号没变，检查是否真的写入了新固件" -ForegroundColor Yellow }
    exit 0
  } catch { Write-Host "  第 $i 次未就绪..." -ForegroundColor DarkGray }
}
Write-Host "设备 40 秒内未重新上线，检查串口日志" -ForegroundColor Red
exit 1
