@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem Telegram build - called by _RUN_TELEGRAM.BAT
rem usage: _BUILD_Telegram.BAT          (run directly, pause at end)
rem        _BUILD_Telegram.BAT nopause  (when called from another bat)
rem
rem AppIcon     = app name shown on the home screen / widget list
rem AppIdSuffix = added after io.github.garpsukim.clock -> a separate app from the WIFI build,
rem               so both can be installed on one phone.
rem               lowercase letter first, then lowercase letters / digits / _ only.
rem               empty = same app as the WIFI build (overwrites it)
rem app/build.gradle.kts reads these two lines from this file (UTF-8),
rem because this bat passes its own file name with -PappNameBat.
rem Keep the format: set "AppIcon=name"
rem NOTE: keep comments ASCII only. Korean text in rem lines after chcp 65001
rem       can make cmd misread the file.
set "AppIcon=KJA시계"
set "AppIdSuffix=.kja"

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
