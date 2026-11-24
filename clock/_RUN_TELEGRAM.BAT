@echo off
chcp 65001 >nul
cd /d "%~dp0"
set "APK=app\build\outputs\apk\debug\app-debug.apk"

if not exist telegram_config.bat (
    echo telegram_config.bat 파일이 없습니다
    pause
    exit /b 1
)
call "%~dp0telegram_config.bat"
if "%BOT_TOKEN%"=="" (
    echo telegram_config.bat 에 BOT_TOKEN, CHAT_ID 를 입력하세요
    pause
    exit /b 1
)

call "%~dp0_BUILD.BAT" nopause
if errorlevel 1 (
    pause
    exit /b 1
)

echo 텔레그램으로 APK 전송 중...
curl -s -o "%TEMP%\tg_clock.json" -F "chat_id=%CHAT_ID%" -F "caption=Clock %DATE% %TIME%" -F "document=@%APK%;filename=Clock.apk" "https://api.telegram.org/bot%BOT_TOKEN%/sendDocument"
findstr /c:"\"ok\":true" "%TEMP%\tg_clock.json" >nul
if errorlevel 1 (
    echo 전송 실패:
    type "%TEMP%\tg_clock.json"
    pause
    exit /b 1
)
echo 전송 완료 - 폰 텔레그램에서 Clock.apk 를 받아 설치하세요
pause
