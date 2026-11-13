rem ===== If screen_telegram.ini does not exist in this folder, download it from the server (GitHub) =====
if exist "screen_telegram.ini" goto INI_DONE


rem ==========================================
rem Backup previous log
rem ==========================================

if not exist "OLD\" mkdir "OLD"

if exist "Log_IniDownload.txt" (
    move /y "Log_IniDownload.txt" "OLD\Log_IniDownload.txt" >nul 2>&1
)

rem ==========================================
rem Create new log
rem ==========================================

set "INI_URL=https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.ini"
(echo INI=missing - downloading from server)>>Log_IniDownload.txt
del "screen_telegram.ini.tmp" >nul 2>&1
"%SystemRoot%\System32\curl.exe" -L -f -s -S --connect-timeout 10 --max-time 60 -o "screen_telegram.ini.tmp" "%INI_URL%" >>Log_IniDownload.txt 2>&1
if errorlevel 1 (
    (echo INI=download failed - screen_telegram.exe cannot run without screen_telegram.ini)>>Log_IniDownload.txt
    goto INI_CLEAN
)
rem sanity check: a real ini contains TELEGRAM_ keys (an HTML error page does not)
findstr /i /c:"TELEGRAM_" "screen_telegram.ini.tmp" >nul 2>&1
if errorlevel 1 (
    (echo INI=downloaded file is not a valid ini - discarded)>>Log_IniDownload.txt
    goto INI_CLEAN
)
move /y "screen_telegram.ini.tmp" "screen_telegram.ini" >nul 2>&1
if errorlevel 1 (
    (echo INI=could not save screen_telegram.ini)>>Log_IniDownload.txt
) else (
    (echo INI=downloaded - edit TELEGRAM_BOT_TOKEN and TELEGRAM_CHAT_ID if needed)>>Log_IniDownload.txt
)
:INI_CLEAN
if exist "screen_telegram.ini.tmp" del "screen_telegram.ini.tmp" >nul 2>&1
:INI_DONE
exit /b