@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
set "ADB=C:\Android\sdk\platform-tools\adb.exe"

rem last address saved as ip:port in adb_addr.txt
set "LASTIP="
set "LASTPORT="
if exist adb_addr.txt for /f "usebackq tokens=1,2 delims=:" %%a in ("adb_addr.txt") do (
    set "LASTIP=%%a"
    set "LASTPORT=%%b"
)

rem ---- step 1: IP (blank = last IP) ----
set "IP="
if defined LASTIP goto askip_last
set /p "IP=1) 폰 IP: "
goto ipdone
:askip_last
set /p "IP=1) 폰 IP [%LASTIP%] (Enter = 그대로): "
if "%IP%"=="" set "IP=%LASTIP%"
:ipdone
if "%IP%"=="" goto noaddr

rem ---- step 2: port (wireless debugging port changes every time) ----
set "PORT="
if defined LASTPORT goto askport_last
set /p "PORT=2) 포트: "
goto portdone
:askport_last
set /p "PORT=2) 포트 [%LASTPORT%] (Enter = 그대로): "
if "%PORT%"=="" set "PORT=%LASTPORT%"
:portdone
if "%PORT%"=="" goto noaddr

set "ADDR=%IP%:%PORT%"
>adb_addr.txt echo %ADDR%
echo 연결: %ADDR%

"%ADB%" connect %ADDR%
call build.bat
if errorlevel 1 exit /b 1
"%ADB%" -s %ADDR% install -r app\build\outputs\apk\debug\app-debug.apk
if errorlevel 1 goto installfail
"%ADB%" -s %ADDR% shell am start -n com.garpsu.wifiscan/.MainActivity
exit /b 0

:noaddr
echo 주소가 없습니다
pause
exit /b 1

:installfail
echo 설치 실패 - 무선 디버깅 연결을 확인하세요
pause
exit /b 1
