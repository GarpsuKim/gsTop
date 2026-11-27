@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
rem 휴대폰(와이파이 adb)에서 지금 쓰는 설정 파일을 꺼내 prefs_dump 폴더에 저장
rem  - clock_prefs.xml : 화면 설정
rem  - chime_prefs.xml : 시보 설정
rem 디버그 빌드 앱이어야 함 (run-as 사용)

set "ADB=C:\Android\sdk\platform-tools\adb.exe"
set "PKG=io.github.garpsukim.clock.debug"

if not exist adb_addr.txt (
    echo adb_addr.txt 가 없습니다. 먼저 _RUN_WIFI.BAT 로 휴대폰을 연결하세요
    pause
    exit /b 1
)
set /p ADDR=<adb_addr.txt
echo 연결: %ADDR%
"%ADB%" connect %ADDR%

if not exist prefs_dump mkdir prefs_dump
"%ADB%" -s %ADDR% exec-out run-as %PKG% cat shared_prefs/clock_prefs.xml > prefs_dump\clock_prefs.xml
"%ADB%" -s %ADDR% exec-out run-as %PKG% cat shared_prefs/chime_prefs.xml > prefs_dump\chime_prefs.xml

echo.
echo ===== prefs_dump\clock_prefs.xml =====
type prefs_dump\clock_prefs.xml
echo.
echo ===== prefs_dump\chime_prefs.xml =====
type prefs_dump\chime_prefs.xml
echo.
echo 저장 완료: %~dp0prefs_dump
pause
