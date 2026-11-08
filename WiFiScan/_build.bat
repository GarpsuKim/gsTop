@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem 실행 중인 WiFiScan.exe 는 덮어쓸 수 없으므로 먼저 확인
tasklist /fi "imagename eq WiFiScan.exe" 2>nul | find /i "WiFiScan.exe" >nul
if not errorlevel 1 (
    echo [중지 필요] WiFiScan.exe 가 실행 중이라 덮어쓸 수 없습니다. 종료한 뒤 다시 빌드하세요.
    pause
    exit /b 1
)
if exist buildERR.txt del buildERR.txt
g++ -O2 -std=c++17 -static -mwindows -o WiFiScan.exe WiFiScan.cpp -liphlpapi -lws2_32 -lbthprops 2> buildERR.txt
if errorlevel 1 (
    echo [빌드 실패] buildERR.txt 를 확인하세요.
    pause
    exit /b 1
)
for %%A in (buildERR.txt) do if %%~zA==0 del buildERR.txt
if exist buildERR.txt (
    echo [빌드 성공, 경고 있음] buildERR.txt 를 확인하세요.
) else (
    echo [빌드 성공] WiFiScan.exe
)
