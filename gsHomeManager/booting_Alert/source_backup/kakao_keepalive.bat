@echo off
rem For Task Scheduler: keep the Kakao token alive (refresh only).
rem If re-auth is needed, no browser is opened; an SMS alert is sent instead.
cd /d "%~dp0"
call env_local.bat
set KAKAO_NO_AUTH=1
set KAKAO_ALERT_PHONE=01045767260
gmail_send.exe --kakao-keepalive >> keepalive_log.txt 2>&1
