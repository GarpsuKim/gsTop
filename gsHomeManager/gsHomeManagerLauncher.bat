@echo off
rem timeout /t 300 /nobreak >nul
tasklist /FI "IMAGENAME eq gsHomeManager.exe" | find /I "gsHomeManager.exe" >nul
if errorlevel 1 (
    start "" "c:\tools\gsHomeManager\gsHomeManagerLauncher.exe"
)
exit  /b  0