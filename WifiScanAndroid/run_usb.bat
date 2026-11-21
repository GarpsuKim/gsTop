@echo off
chcp 65001 >nul
set ADB=C:\Android\sdk\platform-tools\adb.exe
"%ADB%" install -r app\build\outputs\apk\debug\app-debug.apk
if errorlevel 1 (
    echo 설치 실패 - 폰 USB 디버깅/adb 연결을 확인하세요
	pause
    exit /b 1
)
"%ADB%" shell am start -n com.garpsu.wifiscan/.MainActivity
