#!/usr/bin/env pwsh
<#
.SYNOPSIS
  一键安装 Android 本地构建工具链（JDK 17 + cmdline-tools + SDK 组件 + Gradle）
.DESCRIPTION
  全部解压到 .\tools\ 下，不动系统环境变量。装完可用 .\scripts\android_env.ps1 载入环境。
  已存在同名目录时跳过对应步骤（可重复运行）。
.EXAMPLE
  .\scripts\setup_android.ps1
#>
param(
  [string]$Root = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools'),
  [string]$ApiLevel = "34",
  [string]$BuildTools = "34.0.0",
  [string]$GradleVersion = "8.7"
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force -Path $Root | Out-Null
function Log($m) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $m" -ForegroundColor Cyan }

# ---------- 1. JDK 17 ----------
$jdkDir = Join-Path $Root 'jdk17'
if (Test-Path (Join-Path $jdkDir 'bin\java.exe')) {
  Log "JDK 已存在，跳过"
} else {
  Log "下载 JDK 17 (Temurin)..."
  $zip = Join-Path $Root 'jdk17.zip'
  Invoke-WebRequest -Uri 'https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse' -OutFile $zip
  Log "解压 JDK..."
  $tmp = Join-Path $Root '_jdk_tmp'
  if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
  Expand-Archive -Path $zip -DestinationPath $tmp -Force
  $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
  Move-Item $inner.FullName $jdkDir
  Remove-Item $tmp, $zip -Recurse -Force
}
$env:JAVA_HOME = $jdkDir
$env:PATH = "$jdkDir\bin;$env:PATH"
Log (& "$jdkDir\bin\java.exe" -version 2>&1 | Select-Object -First 1)

# ---------- 2. Android cmdline-tools ----------
$sdk = Join-Path $Root 'android-sdk'
$cmdTools = Join-Path $sdk 'cmdline-tools\latest'
if (Test-Path (Join-Path $cmdTools 'bin\sdkmanager.bat')) {
  Log "cmdline-tools 已存在，跳过"
} else {
  Log "下载 Android cmdline-tools..."
  $zip = Join-Path $Root 'cmdline-tools.zip'
  Invoke-WebRequest -Uri 'https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip' -OutFile $zip
  Log "解压 cmdline-tools..."
  $tmp = Join-Path $Root '_ct_tmp'
  if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
  Expand-Archive -Path $zip -DestinationPath $tmp -Force
  New-Item -ItemType Directory -Force -Path (Split-Path $cmdTools) | Out-Null
  Move-Item (Join-Path $tmp 'cmdline-tools') $cmdTools
  Remove-Item $tmp, $zip -Recurse -Force
}

$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:PATH = "$sdk\cmdline-tools\latest\bin;$sdk\platform-tools;$env:PATH"

# ---------- 3. SDK 组件 ----------
Log "接受许可协议..."
$y = ("y`n" * 40)
$y | & (Join-Path $cmdTools 'bin\sdkmanager.bat') --sdk_root=$sdk --licenses 2>&1 | Select-Object -Last 3

Log "安装 platform-tools / platforms;android-$ApiLevel / build-tools;$BuildTools ..."
& (Join-Path $cmdTools 'bin\sdkmanager.bat') --sdk_root=$sdk `
    "platform-tools" "platforms;android-$ApiLevel" "build-tools;$BuildTools" 2>&1 |
    Where-Object { $_ -match 'done|Installing|Unzipping|Warning|Error' } | Select-Object -Last 12

# ---------- 4. Gradle ----------
$gradleDir = Join-Path $Root "gradle-$GradleVersion"
if (Test-Path (Join-Path $gradleDir 'bin\gradle.bat')) {
  Log "Gradle 已存在，跳过"
} else {
  Log "下载 Gradle $GradleVersion ..."
  $zip = Join-Path $Root 'gradle.zip'
  Invoke-WebRequest -Uri "https://services.gradle.org/distributions/gradle-$GradleVersion-bin.zip" -OutFile $zip
  Expand-Archive -Path $zip -DestinationPath $Root -Force
  Remove-Item $zip -Force
}

# ---------- 5. 汇总 ----------
Log "=== 安装完成 ==="
@{
  JAVA_HOME   = $jdkDir
  ANDROID_HOME= $sdk
  GRADLE      = (Join-Path $gradleDir 'bin\gradle.bat')
} | ConvertTo-Json | Out-File -Encoding utf8 (Join-Path $Root 'android_env.json')
Log "环境信息写入 $Root\android_env.json"
Log ("sdk 目录大小: {0:N0} MB" -f ((Get-ChildItem $sdk -Recurse -File | Measure-Object Length -Sum).Sum / 1MB))
