@echo off
setlocal
cd /d "%~dp0"
set APP=gsHomeManagerLauncher

where g++ >nul 2>&1 || (echo [%APP%] g++ not found in PATH & pause & exit /b 1)
if not exist %APP%.cpp (echo [%APP%] missing file: %APP%.cpp & pause & exit /b 1)

taskkill /f /im %APP%.exe >nul 2>&1

g++ -std=c++17 -O2 -s -static -mwindows -municode -Wl,--gc-sections -o %APP%.exe %APP%.cpp -lversion -lwinhttp
if errorlevel 1 (echo [%APP%] Build FAILED & pause & exit /b 1)

for %%f in (%APP%.exe) do echo [%APP%] BUILD OK  %%~zf bytes
pause
exit /b 0
