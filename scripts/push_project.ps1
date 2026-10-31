#!/usr/bin/env pwsh
<#
.SYNOPSIS
  收工一键：提交并推送**项目仓库**（代码/文档）到 GitHub。
.DESCRIPTION
  项目仓库 = E:\ESP32开发（本仓库）
  远端     = git@github.com:papercatno1/esp32-embedded-webcam.git
  日志仓库（E:\ESP32开发\日志）用 scripts\push_log.ps1，两者独立。
.EXAMPLE
  .\scripts\push_project.ps1
  .\scripts\push_project.ps1 -Message "feat: 加拍照存卡"
#>
param(
  [string]$Message = "",
  [string]$Remote = "origin",
  [string]$Branch = "main"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$repoUrl = 'https://github.com/papercatno1/esp32-embedded-webcam'

Push-Location $root
try {
  if (-not (git remote)) {
    git remote add $Remote 'git@github.com:papercatno1/esp32-embedded-webcam.git'
    Write-Host "已添加远端 $Remote" -ForegroundColor Cyan
  }

  # 防呆：凭据文件绝不入库
  foreach ($p in @('firmware/webcam/src/wifi_secrets.h')) {
    $tracked = git ls-files --error-unmatch $p 2>$null
    if ($tracked) { throw "$p 已被 git 跟踪，先 git rm --cached 并确认 .gitignore 生效" }
  }

  git add -A
  $pending = git status --porcelain
  if ($pending) {
    if (-not $Message) { $Message = "chore: 更新 $(Get-Date -Format 'yyyy-MM-dd HH:mm')" }
    git commit -q -m $Message
    Write-Host "已提交: $Message" -ForegroundColor Green
  } else {
    Write-Host "工作区没有新内容，跳过提交" -ForegroundColor Yellow
  }

  Write-Host "`n推送到 $Remote/$Branch ..." -ForegroundColor Cyan
  git push -u $Remote $Branch
  git push $Remote --tags
  Write-Host "推送完成 -> $repoUrl" -ForegroundColor Green
}
catch {
  Write-Host "`n[失败] $($_.Exception.Message)" -ForegroundColor Red
  Write-Host @"

如果是 "Repository not found"，说明 GitHub 上还没建这个仓库：

  1) 浏览器打开: https://github.com/new?name=esp32-embedded-webcam
  2) 不要勾选 Add a README / .gitignore / license（保持空仓库）
  3) 创建后重新运行本脚本

"@ -ForegroundColor Yellow
  exit 1
}
finally {
  Pop-Location
}
