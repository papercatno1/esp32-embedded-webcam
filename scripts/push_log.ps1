#!/usr/bin/env pwsh
<#
.SYNOPSIS
  收工一键：把当天的开发日志提交并推送到 GitHub（日志仓库）。
.DESCRIPTION
  日志仓库 = E:\ESP32开发\日志（独立 git 仓库）
  远端     = git@github.com:papercatno1/esp32-embedded-devlog.git（SSH，443 端口）
  项目仓库不参与本脚本，两者独立处理。
.EXAMPLE
  .\scripts\push_log.ps1
  .\scripts\push_log.ps1 -Message "日志: 2026-10-31 补记点屏过程"
#>
param(
  [string]$Message = "",
  [string]$Remote = "origin",
  [string]$Branch = "main"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logRepo = Join-Path $root '日志'
$repoUrl = 'https://github.com/papercatno1/esp32-embedded-devlog'

if (-not (Test-Path (Join-Path $logRepo '.git'))) { throw "找不到日志仓库: $logRepo" }

Push-Location $logRepo
try {
  if (-not (git remote)) {
    git remote add $Remote 'git@github.com:papercatno1/esp32-embedded-devlog.git'
    Write-Host "已添加远端 $Remote" -ForegroundColor Cyan
  }

  git add -A
  $pending = git status --porcelain
  if ($pending) {
    if (-not $Message) {
      $day = Get-Date -Format 'yyyy-MM-dd'
      $Message = "日志: $day 更新"
    }
    git commit -q -m $Message
    Write-Host "已提交: $Message" -ForegroundColor Green
    $pending | ForEach-Object { Write-Host "  $_" }
  } else {
    Write-Host "工作区没有新内容，跳过提交" -ForegroundColor Yellow
  }

  Write-Host "`n推送到 $Remote/$Branch ..." -ForegroundColor Cyan
  git push -u $Remote $Branch
  if ($LASTEXITCODE -ne 0) { throw "推送失败 (exit $LASTEXITCODE)" }
  Write-Host "推送完成 -> $repoUrl" -ForegroundColor Green
}
catch {
  Write-Host "`n[失败] $($_.Exception.Message)" -ForegroundColor Red
  Write-Host @"

如果是 "Repository not found"，说明 GitHub 上还没建这个仓库（本地已全部准备好，只差一次创建）：

  1) 浏览器打开: https://github.com/new?name=esp32-embedded-devlog
  2) Repository name 填: esp32-embedded-devlog
  3) 不要勾选 Add a README / .gitignore / license（保持空仓库）
  4) 点 Create repository，然后重新运行本脚本

"@ -ForegroundColor Yellow
  exit 1
}
finally {
  Pop-Location
}
