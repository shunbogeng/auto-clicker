# 把 Release 版 AutoClicker.exe 打包成便携 zip（可选再出 Inno Setup 安装包）
# 用法：pwsh packaging/windows/build_windows.ps1 -BuildDir build -OpenCvDir C:\opencv\build
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [string]$OpenCvDir = $env:OpenCV_DIR
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $root

$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "AutoClicker.exe" |
    Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if (-not $exe) { $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "AutoClicker.exe" | Select-Object -First 1 }
if (-not $exe) { throw "找不到 AutoClicker.exe，先构建 Release 版本" }

$stage = Join-Path $root "$OutDir\AutoClicker"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item $exe.FullName (Join-Path $stage "AutoClicker.exe")

# Qt 运行时
$windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    $qtBin = Join-Path $env:QT_ROOT_DIR "bin"
    if (Test-Path (Join-Path $qtBin "windeployqt.exe")) {
        $windeployqt = Join-Path $qtBin "windeployqt.exe"
    }
}
if ($windeployqt) {
    & $windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler `
        (Join-Path $stage "AutoClicker.exe")
} else {
    Write-Warning "没找到 windeployqt，Qt DLL 需要手动补"
}

# OpenCV 运行时（windeployqt 不管这个）
if ($OpenCvDir) {
    $bin = Join-Path $OpenCvDir "x64\vc16\bin"
    if (-not (Test-Path $bin)) { $bin = Join-Path (Split-Path $OpenCvDir -Parent) "bin" }
    if (Test-Path $bin) {
        Get-ChildItem $bin -Filter "opencv_*.dll" | Copy-Item -Destination $stage
        $ff = Join-Path $bin "opencv_videoio_ffmpeg*.dll"
        Get-ChildItem $bin -Filter "opencv_videoio_ffmpeg*.dll" -ErrorAction SilentlyContinue |
            Copy-Item -Destination $stage
    } else {
        Write-Warning "没找到 OpenCV 的 dll 目录：$bin"
    }
}

# 一键解除「来自网络」标记再启动（没有代码签名时，这是最省事的免费办法）
$bat = Join-Path $stage "Unblock-and-Run.bat"
$batLines = @(
    '@echo off',
    'powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath ''%~dp0'' -Recurse -File | Unblock-File"',
    'start "" "%~dp0AutoClicker.exe"'
)
$batText = ($batLines -join "`r`n") + "`r`n"
[IO.File]::WriteAllText($bat, $batText, (New-Object Text.UTF8Encoding $false))

Copy-Item "docs\使用说明.md" $stage -ErrorAction SilentlyContinue
Copy-Item "LICENSE" $stage -ErrorAction SilentlyContinue
Copy-Item "packaging\THIRD-PARTY-NOTICES.md" $stage -ErrorAction SilentlyContinue

$zip = Join-Path $root "$OutDir\AutoClicker-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "便携版：$zip"

# Inno Setup 安装器（如果这台机器装了 ISCC）
$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if ($iscc) {
    & $iscc "/DStageDir=$stage" "/O$OutDir" "packaging\windows\AutoClicker.iss"
    Write-Host "安装包：$OutDir\AutoClicker-Setup.exe"
}
