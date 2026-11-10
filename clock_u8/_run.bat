@echo off
rem =====================================
rem Current Directory
rem =====================================
rem cd /d "%~dp0"
cd C:\tools\clock_u8
if not exist clock_u8.exe call _build.bat
start "" clock_u8.exe
