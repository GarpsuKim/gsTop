@echo off
chcp 65001 >nul
call gradlew.bat assembleDebug
if errorlevel 1 (
    echo ■■■■■■■■■ 빌드 실패
    echo ■■■■■■■■■ 빌드 실패
	pause 
    exit /b 1
)
echo 빌드 완료: app\build\outputs\apk\debug\app-debug.apk
pause