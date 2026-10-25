@echo off
rem For Task Scheduler: keep the Kakao token alive (refresh only).
rem If re-auth is needed, no browser is opened; an SMS/Telegram alert is sent instead.
rem Keys are read from booting_Alert.ini (alert phone: KAKAO_ALERT_PHONE).
cd /d "%~dp0"
set KAKAO_NO_AUTH=1
booting_Alert.exe --kakao-keepalive >> keepalive_log.txt 2>&1
