
rem ===== Auto update: compare with the server exe (GitHub) BEFORE starting =====
rem Update only when the server exe has a HIGHER file version than the local exe (version.h / .rc).
rem Needs curl.exe and powershell (both included in Windows 10/11). On any problem the current exe is kept.
set "UPD_URL=https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.exe"
set "UPD_NEW=screen_telegram.exe.new"

rem Step 1 (cheap): download only version.txt (a few bytes) and compare with the local exe version.
rem   Same or older: skip the exe download. Newer, or version.txt missing/unreadable: fall through to the full check below.
set "VER_URL=https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/version.txt"
set "VER_NEW=version.txt.tmp"
del "%VER_NEW%" >nul 2>&1
"%SystemRoot%\System32\curl.exe" -L -f -s -S --connect-timeout 10 --max-time 30 -o "%VER_NEW%" "%VER_URL%" >>Log2.txt 2>&1
if errorlevel 1 (
    (echo UPDATE=version.txt not available - falling back to full exe check)>>Log2.txt
    goto UPD_FULL
)
rem exit code: 10 = server is newer, 0 = same or older, 1 = error (unreadable version.txt)
powershell -NoProfile -ExecutionPolicy Bypass -Command "try{ $n=[version]((Get-Content '%VER_NEW%' -TotalCount 1).Trim()); $o=[version]'0.0.0.0'; if(Test-Path 'screen_telegram.exe'){ $v=(Get-Item 'screen_telegram.exe').VersionInfo.FileVersion; if($v){$o=[version]$v} }; if($n -gt $o){exit 10}else{exit 0} }catch{exit 1}"
set "VERRC=%errorlevel%"
del "%VER_NEW%" >nul 2>&1
if "%VERRC%"=="0" (
    (echo UPDATE=none - version.txt says current exe is up to date, exe not downloaded)>>Log2.txt
    goto UPD_DONE
)
if "%VERRC%"=="10" (
    (echo UPDATE=version.txt says a newer exe exists - downloading)>>Log2.txt
)
:UPD_FULL
del "%UPD_NEW%" >nul 2>&1
"%SystemRoot%\System32\curl.exe" -L -f -s -S --connect-timeout 10 --max-time 180 -o "%UPD_NEW%" "%UPD_URL%" >>Log2.txt 2>&1
if errorlevel 1 (
    (echo UPDATE=skip - download failed or no network, keep current exe)>>Log2.txt
    goto UPD_DONE
)
rem exit code: 10 = server is newer, 0 = same or older, 1 = error (not an exe / no version info)
powershell -NoProfile -ExecutionPolicy Bypass -Command "try{ $n=[version](Get-Item '%UPD_NEW%').VersionInfo.FileVersion; $o=[version]'0.0.0.0'; if(Test-Path 'screen_telegram.exe'){ $v=(Get-Item 'screen_telegram.exe').VersionInfo.FileVersion; if($v){$o=[version]$v} }; if($n -gt $o){exit 10}else{exit 0} }catch{exit 1}"
if errorlevel 10 goto UPD_APPLY
if errorlevel 1 (
    (echo UPDATE=skip - downloaded file has no valid version info, keep current exe)>>Log2.txt
) else (
    (echo UPDATE=none - current exe is up to date)>>Log2.txt
)
goto UPD_DONE

:UPD_APPLY
tasklist /fi "imagename eq screen_telegram.exe" 2>nul | find /i "screen_telegram.exe" >nul
if not errorlevel 1 (
    (echo UPDATE=skip - new version found but screen_telegram.exe is running)>>Log2.txt
    goto UPD_DONE
)
if exist screen_telegram.exe copy /y screen_telegram.exe screen_telegram.exe.bak >nul 2>&1
move /y "%UPD_NEW%" screen_telegram.exe >nul 2>&1
if errorlevel 1 (
    (echo UPDATE=failed - could not replace exe, keep current exe)>>Log2.txt
) else (
    (echo UPDATE=done - new exe installed, previous one saved as screen_telegram.exe.bak)>>Log2.txt
)
:UPD_DONE
if exist "%UPD_NEW%" del "%UPD_NEW%" >nul 2>&1

exit  /b