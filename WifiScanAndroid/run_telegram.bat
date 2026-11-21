@echo off
chcp 65001 >nul
cd /d "%~dp0"
call build.bat
if errorlevel 1 exit /b 1

if not exist telegram_config.bat (
    echo telegram_config.bat 파일이 없습니다
	pause	
    exit /b 1
)
call telegram_config.bat
if "%BOT_TOKEN%"=="" (
    echo telegram_config.bat 에 BOT_TOKEN, CHAT_ID 를 입력하세요
	pause
    exit /b 1
)

echo 텔레그램으로 APK 전송 중...
curl -s -o "%TEMP%\tg.json" -F "chat_id=%CHAT_ID%" -F "caption=WifiScan %DATE% %TIME%" -F "document=@app\build\outputs\apk\debug\app-debug.apk" "https://api.telegram.org/bot%BOT_TOKEN%/sendDocument"
findstr /c:"\"ok\":true" "%TEMP%\tg.json" >nul
if errorlevel 1 (
    echo 전송 실패:
    type "%TEMP%\tg.json"
	pause
    exit /b 1
)
echo 전송 완료 - 폰 텔레그램에서 APK를 받아 설치하세요
pause