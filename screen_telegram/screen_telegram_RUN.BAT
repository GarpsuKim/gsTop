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

rem Keep the previous run log (useful if the last run crashed). screen_telegram.exe appends to log.txt itself.
if exist log.txt move /y log.txt log.old.txt >nul 2>&1
(echo PC=%COMPUTERNAME%)>log.txt
(echo USERNAME=%USERNAME%)>>log.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "169.254"') do (
    for /f "tokens=*" %%b in ("%%a") do (echo IP=%%b)>>log.txt
)
(echo DATE=%DATE%)>>log.txt
(echo TIME=%TIME%)>>log.txt

rem ===== If screen_telegram.ini does not exist in this folder, download it from the server (GitHub) =====
if exist "screen_telegram.ini" goto INI_DONE
set "INI_URL=https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.ini"
(echo INI=missing - downloading from server)>>log.txt
del "screen_telegram.ini.tmp" >nul 2>&1
curl.exe -L -f -s -S --connect-timeout 10 --max-time 60 -o "screen_telegram.ini.tmp" "%INI_URL%" >>log.txt 2>&1
if errorlevel 1 (
    (echo INI=download failed - screen_telegram.exe cannot run without screen_telegram.ini)>>log.txt
    goto INI_CLEAN
)
rem sanity check: a real ini contains TELEGRAM_ keys (an HTML error page does not)
findstr /i /c:"TELEGRAM_" "screen_telegram.ini.tmp" >nul 2>&1
if errorlevel 1 (
    (echo INI=downloaded file is not a valid ini - discarded)>>log.txt
    goto INI_CLEAN
)
move /y "screen_telegram.ini.tmp" "screen_telegram.ini" >nul 2>&1
if errorlevel 1 (
    (echo INI=could not save screen_telegram.ini)>>log.txt
) else (
    (echo INI=downloaded - edit TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID if needed)>>log.txt
)
:INI_CLEAN
if exist "screen_telegram.ini.tmp" del "screen_telegram.ini.tmp" >nul 2>&1
:INI_DONE

rem ===== Auto update: compare with the server exe (GitHub) BEFORE starting =====
rem Update only when the server exe has a HIGHER file version than the local exe (version.h / .rc).
rem Needs curl.exe and powershell (both included in Windows 10/11). On any problem the current exe is kept.
set "UPD_URL=https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.exe"
set "UPD_NEW=screen_telegram.exe.new"
del "%UPD_NEW%" >nul 2>&1
curl.exe -L -f -s -S --connect-timeout 10 --max-time 180 -o "%UPD_NEW%" "%UPD_URL%" >>log.txt 2>&1
if errorlevel 1 (
    (echo UPDATE=skip - download failed or no network, keep current exe)>>log.txt
    goto UPD_DONE
)
rem exit code: 10 = server is newer, 0 = same or older, 1 = error (not an exe / no version info)
powershell -NoProfile -ExecutionPolicy Bypass -Command "try{ $n=[version](Get-Item '%UPD_NEW%').VersionInfo.FileVersion; $o=[version]'0.0.0.0'; if(Test-Path 'screen_telegram.exe'){ $v=(Get-Item 'screen_telegram.exe').VersionInfo.FileVersion; if($v){$o=[version]$v} }; if($n -gt $o){exit 10}else{exit 0} }catch{exit 1}"
if errorlevel 10 goto UPD_APPLY
if errorlevel 1 (
    (echo UPDATE=skip - downloaded file has no valid version info, keep current exe)>>log.txt
) else (
    (echo UPDATE=none - current exe is up to date)>>log.txt
)
goto UPD_DONE

:UPD_APPLY
tasklist /fi "imagename eq screen_telegram.exe" 2>nul | find /i "screen_telegram.exe" >nul
if not errorlevel 1 (
    (echo UPDATE=skip - new version found but screen_telegram.exe is running)>>log.txt
    goto UPD_DONE
)
if exist screen_telegram.exe copy /y screen_telegram.exe screen_telegram.exe.bak >nul 2>&1
move /y "%UPD_NEW%" screen_telegram.exe >nul 2>&1
if errorlevel 1 (
    (echo UPDATE=failed - could not replace exe, keep current exe)>>log.txt
) else (
    (echo UPDATE=done - new exe installed, previous one saved as screen_telegram.exe.bak)>>log.txt
)
:UPD_DONE
if exist "%UPD_NEW%" del "%UPD_NEW%" >nul 2>&1


rem All keys and recipients are read from screen_telegram.ini
echo screen_telegram.exe ...........
start "" screen_telegram.exe
if /i "%~1"=="pause" pause