@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem 사용법: _BUILD.BAT          (직접 실행, 끝나면 pause)
rem         _BUILD.BAT nopause  (다른 bat에서 call 할 때)

call "%~dp0gradlew.bat" assembleDebug
if errorlevel 1 (
    echo ■■■■■■■■■ 빌드 실패
    echo ■■■■■■■■■ 빌드 실패
    if /i not "%~1"=="nopause" pause
    exit /b 1
)
echo 빌드 완료: app\build\outputs\apk\debug\app-debug.apk
if /i not "%~1"=="nopause" pause
exit /b 0
