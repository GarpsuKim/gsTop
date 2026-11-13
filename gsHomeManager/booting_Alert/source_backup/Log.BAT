@echo off
echo PC=%COMPUTERNAME%  >  log.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    echo IP=%%a >> log.txt
)
echo DATE=%DATE%  >>  log.txt
echo TIME=%TIME%  >>  log.txt
