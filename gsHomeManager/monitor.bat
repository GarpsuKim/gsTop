@echo off

:LOOP

tasklist /FI "IMAGENAME eq gsHomeManager.exe" | find /I "gsHomeManager.exe" >nul

if errorlevel 1 (
    start "" "C:\tools\gsHomeManager\gsHomeManagerLauncher.exe"
)

timeout /t 300 /nobreak >nul

goto LOOP