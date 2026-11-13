
@echo off
chcp 65001 > nul
setlocal EnableDelayedExpansion
title Wi-Fi Network Scanner

:: 현재 IPv4 주소 확인
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /c:"IPv4"') do (
    set IP=%%a
    goto FOUND
)

:FOUND
set IP=%IP: =%

for /f "tokens=1,2,3 delims=." %%a in ("%IP%") do (
    set NET=%%a.%%b.%%c
)

set OUT=wifi_scan_result.csv

echo =========================================
echo          WIFI NETWORK SCANNER
echo =========================================
echo Network: %NET%.0/24
echo.

echo [1/3] Scanning network...

for /L %%i in (1,1,254) do (
    ping -n 1 -w 150 %NET%.%%i > nul
)

echo [2/3] Collecting ARP information...

echo IP,MAC,DEVICE_NAME,PING_STATUS > "%OUT%"

for /f "tokens=1,2" %%a in ('arp -a ^| findstr "%NET%."') do (
    set ADDR=%%a
    set MAC=%%b
    set NAME=Unknown
    set STATUS=OFFLINE

    ping -n 1 -w 300 !ADDR! > nul
    if !errorlevel! equ 0 set STATUS=ONLINE

    for /f "tokens=2 delims= " %%n in ('ping -a -n 1 -w 300 !ADDR! ^| findstr /i "Pinging"') do (
        set NAME=%%n
    )

    if "!NAME!"=="Unknown" (
        for /f "tokens=1" %%n in ('nbtstat -A !ADDR! ^| findstr /i "<00>"') do (
            set NAME=%%n
        )
    )

    echo !ADDR!    !MAC!    !NAME!    !STATUS!

    echo !ADDR!,!MAC!,!NAME!,!STATUS!>> "%OUT%"
)

echo.
echo =========================================
echo Scan completed!
echo Result file: %OUT%
echo =========================================
pause
