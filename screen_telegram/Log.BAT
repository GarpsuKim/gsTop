
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

rem ==========================================
rem Backup previous log
rem ==========================================

if not exist "OLD\" mkdir "OLD"

for /f %%a in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "BACKUPDATE=%%a"
if exist "Log1.txt" (
    move /y "Log1.txt" "OLD\Log1_%BACKUPDATE%.txt" >nul 2>&1
)

rem ==========================================
rem Create new log
rem ==========================================

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
) > "Log1.txt"

if defined screen_telegram_RUN_SLEEP goto skip_SLEEP
rem ==========================================
rem Open log and website
rem ==========================================

rem start "" notepad.exe "%~dp0Log1.txt"
start "" notepad.exe Log1.txt
start "" "https://ip-tools.app/myip/"

:skip_SLEEP
endlocal
exit /b
