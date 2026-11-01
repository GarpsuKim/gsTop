@echo off
cd /d "%~dp0"
chcp 65001

rem Wait (up to ~60s) until a real IPv4 address exists (right after boot the network may not be ready; 169.254.x.x is ignored)
set /a TRIES=0
:WAITIP
set "HAVEIP="
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254"') do set "HAVEIP=1"
if defined HAVEIP goto GOTIP
set /a TRIES+=1
if %TRIES% geq 12 goto GOTIP
ping -n 6 127.0.0.1 >nul
goto WAITIP
:GOTIP

(echo PC=%COMPUTERNAME%)>log.txt
(echo USERNAME=%USERNAME%)>>log.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254"') do (
    for /f "tokens=*" %%b in ("%%a") do (echo IP=%%b)>>log.txt
)
(echo DATE=%DATE%)>>log.txt
(echo TIME=%TIME%)>>log.txt


rem All keys and recipients are read from booting_Alert.ini
echo screen_telegram.exe ...........
screen_telegram.exe >> log.txt 2>&1
if /i "%~1"=="pause" pause