@echo off
chcp 65001 >nul
cd /d "%~dp0"
set ADB=C:\Android\sdk\platform-tools\adb.exe
set LAST=
if exist adb_addr.txt set /p LAST=<adb_addr.txt
if defined LAST (set /p ADDR=폰 IP:포트 [%LAST%]: ) else (set /p ADDR=폰 IP:포트: )
if "%ADDR%"=="" set ADDR=%LAST%
if "%ADDR%"=="" (
    echo 주소가 없습니다
	pause
    exit /b 1
)
echo %ADDR%>adb_addr.txt

"%ADB%" connect %ADDR%
call build.bat
if errorlevel 1 exit /b 1
"%ADB%" -s %ADDR% install -r app\build\outputs\apk\debug\app-debug.apk
if errorlevel 1 (
    echo 설치 실패 - 무선 디버깅 연결을 확인하세요
	pause
    exit /b 1
)
"%ADB%" -s %ADDR% shell am start -n com.garpsu.wifiscan/.MainActivity
