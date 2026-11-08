@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem 폰의 IP Webcam 주소 (IP:포트) 를 아래에 맞게 수정하세요
rem set CAM=192.168.0.70:8080
set CAM=192.168.0.70:4747
set LOG=run.log
echo ==== run %date% %time% ==== > %LOG%
echo cwd=%CD% >> %LOG%
echo CAM=%CAM% >> %LOG%
if not exist ipcam.exe (
    echo [ERROR] ipcam.exe not found. Run _Build.bat first.
    echo [ERROR] ipcam.exe not found >> %LOG%
    pause
    exit /b 1
)
echo ping test... >> %LOG%
for /f "tokens=1 delims=:" %%H in ("%CAM%") do ping -n 2 -w 1000 %%H >> %LOG% 2>&1
echo starting ipcam.exe %CAM% >> %LOG%
start "" /wait ipcam.exe %CAM%
echo ipcam.exe exit code=%errorlevel% >> %LOG%
echo.
echo --- run.log ---
type %LOG%
echo.
echo --- ipcam.log ---
if exist ipcam.log (type ipcam.log) else (echo ipcam.log was NOT created - the exe did not start)
pause
exit /b 0