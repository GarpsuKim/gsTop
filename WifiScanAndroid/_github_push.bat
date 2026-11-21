@echo off
chcp 65001 >nul
setlocal
rem GitHub push: copy last commit to C:\tools\gsTop\WifiScanAndroid and push
rem Files in .gitignore (bot token, photo icon, build output) are never uploaded.

cd /d "%~dp0" || goto fail
if not exist "%~dp0app\build.gradle" goto fail

set "GH=C:\tools\gsTop"
set "SUB=WifiScanAndroid"

git diff --quiet HEAD
if errorlevel 1 goto notcommitted

if not exist "%GH%\.git" git clone https://github.com/GarpsuKim/gsTop "%GH%"
if not exist "%GH%\.git" goto fail
git -C "%GH%" pull
if errorlevel 1 goto fail

rem safety: only delete the WifiScanAndroid folder inside gsTop
if exist "%GH%\%SUB%\app" rmdir /s /q "%GH%\%SUB%"
if not exist "%GH%\%SUB%" mkdir "%GH%\%SUB%"
git archive HEAD | tar -x -C "%GH%\%SUB%"
if errorlevel 1 goto fail

for /f "delims=" %%m in ('git log -1 --pretty^=format:%%s') do set "MSG=%%m"
git -C "%GH%" add -A "%SUB%"
git -C "%GH%" commit -m "%SUB%: %MSG%"
git -C "%GH%" push
if errorlevel 1 goto fail

echo.
echo [OK] GitHub: https://github.com/GarpsuKim/gsTop/tree/main/%SUB%
pause
exit /b 0

:notcommitted
echo.
echo [STOP] 기록하지 않은 변경이 있습니다. 먼저 실행하세요:
echo        git add -A
echo        git commit -m "바꾼 내용"
pause
exit /b 1

:fail
echo.
echo [FAIL] GitHub 올리기 실패 - 메시지를 확인하세요.
pause
exit /b 1
