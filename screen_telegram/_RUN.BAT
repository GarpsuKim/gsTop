@echo off
cd /d "%~dp0"
rem chcp 65001

set   screen_telegram_RUN_SLEEP=1
call  Log.bat
call  Ini_Update.bat
call  screen_telegram_exe_update.bat

rem All keys and recipients are read from screen_telegram.ini
rem echo screen_telegram.exe ...........

rem Single instance: do not start a second copy if it is already running
tasklist /fi "imagename eq screen_telegram.exe" 2>nul | find /i "screen_telegram.exe" >nul
if errorlevel 1 start "" screen_telegram.exe
rem if /i "%~1"=="pause" pause
exit
