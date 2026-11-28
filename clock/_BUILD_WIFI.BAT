@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem WIFI(adb) build - called by _RUN_WIFI.BAT
rem usage: _BUILD_WIFI.BAT          (run directly, pause at end)
rem        _BUILD_WIFI.BAT nopause  (when called from another bat)
rem
rem AppIcon     = app name shown on the home screen / widget list
rem AppIdSuffix = added after io.github.garpsukim.clock (empty = io.github.garpsukim.clock)
rem               debug builds also get ".debug" at the end -> io.github.garpsukim.clock.debug
rem app/build.gradle.kts reads these two lines from this file (UTF-8),
rem because this bat passes its own file name with -PappNameBat.
rem Keep the format: set "AppIcon=name"
rem NOTE: keep comments ASCII only. Korean text in rem lines after chcp 65001
rem       can make cmd misread the file ("'D_WIFI.BAT' is not recognized").
set "AppIcon=영원한_시계3"
set "AppIdSuffix="

echo 앱 이름: %AppIcon%
call "%~dp0gradlew.bat" assembleDebug "-PappNameBat=%~nx0"
if errorlevel 1 (
    echo ■■■■■■■■■ 빌드 실패
    echo ■■■■■■■■■ 빌드 실패
    if /i not "%~1"=="nopause" pause
    exit /b 1
)
echo 빌드 완료: app\build\outputs\apk\debug\app-debug.apk
if /i not "%~1"=="nopause" pause
exit /b 0
