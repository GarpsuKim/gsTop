@echo off
cd /d "%~dp0"

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

(echo PC=%COMPUTERNAME%)>logOFF.txt
(echo USERNAME=%USERNAME%)>>logOFF.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254"') do (
    (echo IP=%%a)>>logOFF.txt
)
(echo DATE=%DATE%)>>logOFF.txt
(echo TIME=%TIME%)>>logOFF.txt

rem Build mailLogoff.txt without COPY (COPY a+b appends a Ctrl-Z byte); blank line keeps the two parts apart
type mailbody_Logoff.txt > mailLogoff.txt
echo.>> mailLogoff.txt
type logOFF.txt >> mailLogoff.txt

rem All keys and recipients are read from booting_Alert.ini
booting_Alert.exe mailLogoff.txt  >> mailLogoff.txt
rem Pass "pause" as the first argument to keep the window open: booting_Alert_RUN.BAT pause
if /i "%~1"=="pause" pause

