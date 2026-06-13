@echo off
setlocal
cd /d "%~dp0"

if not exist "ClipKeeper.exe" (
    echo 未找到 ClipKeeper.exe。
    echo 请先运行 build_gcc.bat 编译项目，然后再启动。
    pause
    exit /b 1
)

start "" "%~dp0ClipKeeper.exe"
endlocal
