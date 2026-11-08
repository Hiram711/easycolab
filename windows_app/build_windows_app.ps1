# EasyColab Windows 一键本地编译打包脚本 (PowerShell)
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
Set-Location $ProjectRoot

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  EasyColab Windows 一键编译脚本 (PySide6 + PyInstaller)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "未检测到 Python (推荐 3.11 / 3.12)，请先安装 Python 并勾选 Add to PATH。"
}

$VenvDir = Join-Path $ProjectRoot ".venv_win"
$VenvPy  = Join-Path $VenvDir "Scripts\python.exe"
$VenvPyi = Join-Path $VenvDir "Scripts\pyinstaller.exe"

if (-not (Test-Path $VenvPy)) {
    Write-Host "[1/4] 正在创建隔离编译环境 .venv_win ..." -ForegroundColor Yellow
    python -m venv $VenvDir
}

Write-Host "[2/4] 正在安装 PySide6 与 Colab 桥接依赖 ..." -ForegroundColor Yellow
& $VenvPy -m pip install --upgrade pip -q
& $VenvPy -m pip install -r (Join-Path $ProjectRoot "windows_app\requirements-windows.txt")

Write-Host "[3/4] 正在生成高清多尺寸图标 AppIcon.ico ..." -ForegroundColor Yellow
& $VenvPy (Join-Path $ProjectRoot "windows_app\generate_icon.py")

Write-Host "[4/4] 正在编译独立可执行程序 EasyColab.exe ..." -ForegroundColor Yellow
& $VenvPyi --noconfirm --clean `
    --distpath (Join-Path $ProjectRoot "dist") `
    --workpath (Join-Path $ProjectRoot "build\pyinstaller") `
    (Join-Path $ProjectRoot "windows_app\EasyColab.spec")

$ZipPath = Join-Path $ProjectRoot "EasyColab-Windows-x64.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Write-Host "正在生成免安装发行版压缩包 EasyColab-Windows-x64.zip ..." -ForegroundColor Yellow
Compress-Archive -Path (Join-Path $ProjectRoot "dist\EasyColab") -DestinationPath $ZipPath -Force

Write-Host ""
Write-Host "✅ 编译成功！" -ForegroundColor Green
Write-Host "  - 可执行程序: $ProjectRoot\dist\EasyColab\EasyColab.exe"
Write-Host "  - 发行压缩包: $ZipPath"
