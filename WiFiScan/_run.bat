@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem WiFiScan.exe has a single mode (no arguments): 1) Bluetooth scan -> result file, 2) Wi-Fi scan -> appended to it, 3) watch.
rem Rebuild first when WiFiScan.exe is missing or WiFiScan.cpp is newer than the exe.
set "NEEDBUILD="
if not exist WiFiScan.exe set "NEEDBUILD=1"
if exist WiFiScan.exe powershell -NoProfile -Command "if((Get-Item 'WiFiScan.cpp').LastWriteTime -gt (Get-Item 'WiFiScan.exe').LastWriteTime){exit 1}else{exit 0}"
if exist WiFiScan.exe if errorlevel 1 set "NEEDBUILD=1"
if defined NEEDBUILD call _build.bat
if not exist WiFiScan.exe exit /b 1
rem WiFiScan.exe is a GUI program (no console window, log only: WiFiScan_watch.log). Stop it with END in ..\screen_telegram\wifiscanTriger.txt (Telegram /stop).
start "" WiFiScan.exe
