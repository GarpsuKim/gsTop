@echo off
chcp 65001
rem  rdp_server.bat
cd /d "%~dp0"
start "" rdp_server.exe
start "" c:\windows\system32\osk.exe
c:\windows\system32\ipconfig.exe  -all
echo *****************************
echo rdp_Server running ..........
echo *****************************
exit  /b 0
pause




