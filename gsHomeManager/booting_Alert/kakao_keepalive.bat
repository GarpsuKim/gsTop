@echo off
rem For Task Scheduler: keep the Kakao token alive (refresh only).
rem If re-auth is needed, no browser is opened; an SMS/Telegram alert is sent instead (at most once per ALERT_INTERVAL_HOURS).
rem Keys are read from booting_Alert.ini.
cd /d "%~dp0"
set KAKAO_NO_AUTH=TRUE
booting_Alert.exe --kakao-keepalive >> keepalive_log.txt 2>&1
