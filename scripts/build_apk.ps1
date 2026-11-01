# 编译安卓 App（票据屏）
#
#   .\scripts\build_apk.ps1            # 增量构建
#   .\scripts\build_apk.ps1 -Clean     # 先 clean 再构建
#
# 产物：android\app\build\outputs\apk\debug\app-debug.apk
# 同时复制一份到 dist\票据屏-$ver-debug.apk（dist\ 不入库），方便传手机。

param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$envFile = Join-Path $root 'tools\android_env.json'
if (-not (Test-Path $envFile)) {
    throw "找不到 $envFile，先运行 .\scripts\setup_android.ps1 安装工具链"
}

$envs = Get-Content $envFile -Raw | ConvertFrom-Json
$env:JAVA_HOME       = $envs.JAVA_HOME
$env:ANDROID_HOME    = $envs.ANDROID_HOME
$env:ANDROID_SDK_ROOT = $envs.ANDROID_HOME
$env:PATH            = "$($envs.JAVA_HOME)\bin;$env:PATH"

Write-Host "JDK    : $env:JAVA_HOME"
Write-Host "SDK    : $env:ANDROID_HOME"
Write-Host "Gradle : $($envs.GRADLE)"

$tasks = @()
if ($Clean) { $tasks += 'clean' }
$tasks += 'assembleDebug'

& $envs.GRADLE -p (Join-Path $root 'android') @tasks --no-daemon --console=plain
if ($LASTEXITCODE -ne 0) { throw "Gradle 构建失败（exit $LASTEXITCODE）" }

# 从 build.gradle.kts 里读版本号，装包时一眼能认出版本
$gradle = Join-Path $PSScriptRoot "..\android\app\build.gradle.kts"
$ver = "0.0.0"
if (Test-Path $gradle) {
    $m = Select-String -Path $gradle -Pattern 'versionName\s*=\s*"([^"]+)"'
    if ($m) { $ver = $m.Matches[0].Groups[1].Value }
}

$apk = Join-Path $root 'android\app\build\outputs\apk\debug\app-debug.apk'
if (-not (Test-Path $apk)) { throw "构建完成但找不到 $apk" }

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist ("票据屏-$ver-debug.apk")
Copy-Item $apk $out -Force

$size = [math]::Round((Get-Item $out).Length / 1KB, 1)
Write-Host ""
Write-Host "APK : $out  ($size KB)" -ForegroundColor Green
Write-Host "装机：手机连电脑后 adb install -r `"$out`"，或直接把文件传到手机点安装"
