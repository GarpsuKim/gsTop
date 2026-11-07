@echo off
setlocal EnableExtensions
cd /d "%~dp0"

chcp 65001 >nul

rem ==========================================
rem BOOT LOG - Windows Startup
rem ==========================================

rem Network IPv4 wait (maximum approximately 60 sec)
set /a TRIES=0
set "HAVEIP="

:WAITIP
set "HAVEIP="

for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254."') do (
    set "HAVEIP=1"
)

if defined HAVEIP goto GOTIP

set /a TRIES+=1
if %TRIES% geq 12 goto GOTIP

ping -n 6 127.0.0.1 >nul
goto WAITIP

:GOTIP

rem Backup previous log
if exist "Log.txt" (
    move /y "Log.txt" "BootLog_old.txt" >nul 2>&1
)

rem Create new log
(
    echo ========================================
    echo          WINDOWS BOOT LOG
    echo ========================================
    echo PC=%COMPUTERNAME%
    echo USERNAME=%USERNAME%
    echo DATE=%DATE%
    echo TIME=%TIME%
    echo NETWORK_WAIT_TRIES=%TRIES%
    echo.
    echo [IPv4 ADDRESS]
    for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254."') do (
        for /f "tokens=*" %%b in ("%%a") do echo IP=%%b
    )
    echo.
    echo ========================================
) > "Log.txt"

rem Open log file
start "" notepad.exe "%~dp0Log.txt"

rem Open external IP information website
start "" "https://ip-tools.app/myip/"

endlocal
exit /b