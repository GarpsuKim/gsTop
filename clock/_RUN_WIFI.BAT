@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
set "ADB=C:\Android\sdk\platform-tools\adb.exe"
set "APK=app\build\outputs\apk\debug\app-debug.apk"
set "ACTIVITY=io.github.garpsukim.clock.debug/com.example.clock.MainActivity"

rem 마지막 주소는 adb_addr.txt 에 ip:port 로 저장
set "LASTIP="
set "LASTPORT="
if exist adb_addr.txt for /f "usebackq tokens=1,2 delims=:" %%a in ("adb_addr.txt") do (
    set "LASTIP=%%a"
    set "LASTPORT=%%b"
)

rem ---- 1단계: IP (Enter = 마지막 IP) ----
set "IP="
if defined LASTIP goto askip_last
set /p "IP=1) 폰 IP: "
goto ipdone
:askip_last
set /p "IP=1) 폰 IP [%LASTIP%] (Enter = 그대로): "
if "%IP%"=="" set "IP=%LASTIP%"
:ipdone
if "%IP%"=="" goto noaddr

rem ---- 2단계: 포트 (무선 디버깅 포트는 매번 바뀜) ----
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

call "%~dp0_BUILD_WIFI.BAT" nopause
if errorlevel 1 goto buildfail

echo 설치 중...
"%ADB%" -s %ADDR% install -r "%APK%"
if errorlevel 1 goto installfail
"%ADB%" -s %ADDR% shell am start -n %ACTIVITY%
echo 실행 완료
exit /b 0

:noaddr
echo 주소가 없습니다
pause
exit /b 1

:buildfail
pause
exit /b 1

:installfail
echo 설치 실패 - 폰의 무선 디버깅 연결(IP/포트)을 확인하세요
pause
exit /b 1
