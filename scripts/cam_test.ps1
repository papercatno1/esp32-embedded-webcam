#!/usr/bin/env pwsh
<#
.SYNOPSIS
  图传自测：连上板子热点后，一条命令验证 /status、/capture、/shoot
.EXAMPLE
  .\scripts\cam_test.ps1
  .\scripts\cam_test.ps1 -Host 192.168.4.1 -Out snap.jpg
#>
param(
  [string]$CamHost = "192.168.4.1",
  [string]$Out = "logs\snap.jpg"
)

$ErrorActionPreference = 'Continue'
$base = "http://$CamHost"

Write-Host "== 1) /status ==" -ForegroundColor Cyan
try {
  $s = Invoke-RestMethod -Uri "$base/status" -TimeoutSec 8
  $s | Format-List
} catch {
  Write-Host "状态获取失败：$($_.Exception.Message)" -ForegroundColor Red
  Write-Host "检查：是否已连上热点 ESP32-LCD2-CAM？本机 IP 应为 192.168.4.x" -ForegroundColor Yellow
  exit 1
}

Write-Host "`n== 2) /capture 下载一帧 ==" -ForegroundColor Cyan
try {
  New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null
  Invoke-WebRequest -Uri "$base/capture" -OutFile $Out -TimeoutSec 15
  $f = Get-Item $Out
  Write-Host ("已保存 {0}（{1:N1} KB）" -f $f.FullName, ($f.Length/1KB)) -ForegroundColor Green
} catch {
  Write-Host "下载失败：$($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n== 3) /shoot 拍照存卡 ==" -ForegroundColor Cyan
try {
  $r = Invoke-WebRequest -Uri "$base/shoot" -TimeoutSec 15 -UseBasicParsing
  Write-Host $r.Content -ForegroundColor Green
} catch {
  Write-Host "拍照失败：$($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "`n提示：/stream 要在浏览器里看（MJPEG 不能直接 Out-File）" -ForegroundColor DarkGray
