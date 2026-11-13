@echo off
cd /d "%~dp0"
rem cd  C:\tools\gMailGPP
set SOLAPI_API_KEY=NCSBFF94ICD2L6TK
set SOLAPI_API_SECRET=PE2J445NDHQMC8CT3GSPSHC6CFBSGOE1
set SOLAPI_FROM=01045767260

set KAKAO_REST_API_KEY=da3b11c16dfdd5acc257a39943d7923c
set KAKAO_CLIENT_SECRET=XEjOs0QjyQs2xt3panp2s3nRfGRPQXyr
set KAKAO_REDIRECT=https://localhost:8080/callback

echo PC=%COMPUTERNAME%  >  log.txt
echo USERNAME=%USERNAME%  >>  log.txt
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4"') do (
    echo IP=%%a >> log.txt
)
echo DATE=%DATE%  >>  log.txt
echo TIME=%TIME%  >>  log.txt

del mail.txt
copy  mailbody.txt + log.txt  mail.txt
gmail_send.exe  ewpirqlxqbdonaaa    mail.txt 