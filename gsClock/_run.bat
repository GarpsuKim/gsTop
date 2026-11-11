@echo off
if not exist gsClock.exe call _build.bat
start "" gsClock.exe
