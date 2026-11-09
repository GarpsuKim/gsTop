@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem =====================================
rem Current Directory
rem =====================================
cd /d "%~dp0"

rem =====================================
rem User Input
rem =====================================
echo.
echo =====================================
echo          IP Webcam Launcher
echo =====================================
echo.

set /p "USER_INPUT=카메라의 IP 주소와 포트 (예: 192.168.0.70:8080): "

rem Remove accidental spaces
set "CAM=%USER_INPUT: =%"

if not defined CAM (
    echo [ERROR] IP address is empty.
    pause
    exit /b 1
)

rem =====================================
rem Log File
rem =====================================
set "LOG=%~dp0run.log"

(
echo =====================================
echo RUN DATE : %DATE%
echo RUN TIME : %TIME%
echo =====================================
echo CWD      : %CD%
echo CAM      : %CAM%
echo.
) > "%LOG%"

rem =====================================
rem Check EXE
rem =====================================
if not exist "%~dp0ipcam.exe" (
    echo [ERROR] ipcam.exe not found.
    echo [ERROR] ipcam.exe not found.>>"%LOG%"
    echo Run _Build.bat first.
    pause
    exit /b 1
)

rem =====================================
rem Ping Test
rem =====================================
echo.
echo [1] Ping test...

echo [PING TEST]>>"%LOG%"

for /f "tokens=1 delims=:" %%H in ("%CAM%") do (
    ping -n 2 -w 1000 "%%H" >>"%LOG%" 2>&1
)

rem =====================================
rem Execute IPCAM
rem =====================================
echo.
echo [2] Starting ipcam.exe...

echo [START] ipcam.exe %CAM%>>"%LOG%"

start "" /wait "%~dp0ipcam.exe" "%CAM%"

set "EXIT_CODE=%ERRORLEVEL%"

echo [EXIT CODE] %EXIT_CODE%>>"%LOG%"

rem =====================================
rem Display Logs
rem =====================================
echo.
echo =====================================
echo              RUN RESULT
echo =====================================
echo Exit Code: %EXIT_CODE%

echo.
echo --- run.log ---
type "%LOG%"

echo.
echo --- ipcam.log ---

if exist "%~dp0ipcam.log" (
    type "%~dp0ipcam.log"
) else (
    echo ipcam.log was NOT created.
)

echo.
echo =====================================

pause
endlocal
exit /b