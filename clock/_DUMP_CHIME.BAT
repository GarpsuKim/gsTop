@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
rem Chime diagnosis: collect phone state over WIFI adb into prefs_dump\chime_diag.txt
rem  - chime settings (chime_prefs.xml), scheduled alarm, exact alarm permission,
rem    ringer mode, battery bucket / restrictions, recent chime log lines
rem Debug build of com.example.clock (WIFI build) is required (run-as).
rem NOTE: keep comments ASCII only (Korean in rem lines can break cmd parsing).

set "ADB=C:\Android\sdk\platform-tools\adb.exe"
set "PKG=com.example.clock"
set "OUT=prefs_dump\chime_diag.txt"

if not exist adb_addr.txt (
    echo adb_addr.txt 가 없습니다. 먼저 _RUN_WIFI.BAT 로 휴대폰을 연결하세요
    pause
    exit /b 1
)
set /p ADDR=<adb_addr.txt
echo 연결: %ADDR%
"%ADB%" connect %ADDR%
set "A="%ADB%" -s %ADDR%"

if not exist prefs_dump mkdir prefs_dump
> "%OUT%" echo ===== time
>>"%OUT%" %A% shell date
>>"%OUT%" echo.
>>"%OUT%" echo ===== chime_prefs.xml
>>"%OUT%" %A% exec-out run-as %PKG% cat shared_prefs/chime_prefs.xml
>>"%OUT%" echo.
>>"%OUT%" echo ===== ringer mode (0=silent 1=vibrate 2=normal)
>>"%OUT%" %A% shell settings get global mode_ringer
>>"%OUT%" echo.
>>"%OUT%" echo ===== scheduled alarms of %PKG%
>>"%OUT%" %A% shell "dumpsys alarm | grep -A3 %PKG%"
>>"%OUT%" echo.
>>"%OUT%" echo ===== exact alarm permission
>>"%OUT%" %A% shell appops get %PKG% SCHEDULE_EXACT_ALARM
>>"%OUT%" %A% shell "dumpsys package %PKG% | grep -i -E 'EXACT_ALARM|FOREGROUND_SERVICE|POST_NOTIFICATIONS'"
>>"%OUT%" echo.
>>"%OUT%" echo ===== standby bucket (10=active 20=working 30=frequent 40=rare 45=restricted 50=never)
>>"%OUT%" %A% shell am get-standby-bucket %PKG%
>>"%OUT%" echo.
>>"%OUT%" echo ===== battery / background restrictions
>>"%OUT%" %A% shell appops get %PKG% RUN_ANY_IN_BACKGROUND
>>"%OUT%" %A% shell "dumpsys deviceidle whitelist | grep %PKG%"
>>"%OUT%" %A% shell "dumpsys package %PKG% | grep -i -E 'stopped=|enabled='"
>>"%OUT%" echo.
>>"%OUT%" echo ===== recent log (ChimeService, AlarmManager for this app)
>>"%OUT%" %A% shell "logcat -d -v time | grep -i -E 'ChimeService|chime|%PKG%' | tail -n 80"

echo.
type "%OUT%"
echo.
echo 저장 완료: %~dp0%OUT%
pause
