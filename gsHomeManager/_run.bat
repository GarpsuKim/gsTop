@echo off
cd /d "%~dp0"
if exist gsHomeManagerLauncher.exe (start "" gsHomeManagerLauncher.exe) else start "" gsHomeManager.exe