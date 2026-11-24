@echo off
chcp 65001 >nul
setlocal
rem GitHub push: commit (git add -A, git commit) then copy to C:\tools\gsTop\clock and push
rem Files in .gitignore (bot token, adb address, build output) are never uploaded.

cd /d "%~dp0" || goto fail
if not exist "%~dp0app\build.gradle.kts" goto fail

set "GH=C:\tools\gsTop"
set "SUB=clock"

rem ---- 0) first time: make a local git repository ----
if not exist "%~dp0.git" (
    git init -b main
    if errorlevel 1 goto fail
)

rem ---- 1) commit message (blank = stop) ----
set "MSG="
set /p "MSG=바꾼 내용 (빈칸이면 종료): "
if not defined MSG goto stop
if "%MSG: =%"=="" goto stop

git add -A
git diff --cached --quiet
if errorlevel 1 (
    git commit -m "%MSG%"
    if errorlevel 1 goto fail
) else (
    echo [INFO] 새로 바뀐 파일이 없어 기록은 건너뛰고, 마지막 기록을 올립니다.
)

rem ---- 2) copy last commit into gsTop and push ----
if not exist "%GH%\.git" git clone https://github.com/GarpsuKim/gsTop "%GH%"
if not exist "%GH%\.git" goto fail
git -C "%GH%" pull
if errorlevel 1 goto fail

rem safety: only delete the clock folder inside gsTop
if exist "%GH%\%SUB%\app" rmdir /s /q "%GH%\%SUB%"
if not exist "%GH%\%SUB%" mkdir "%GH%\%SUB%"
git archive HEAD | tar -x -C "%GH%\%SUB%"
if errorlevel 1 goto fail

git -C "%GH%" add -A "%SUB%"
git -C "%GH%" commit -m "%SUB%: %MSG%"
git -C "%GH%" push
if errorlevel 1 goto fail

echo.
echo [OK] GitHub: https://github.com/GarpsuKim/gsTop/tree/main/%SUB%
pause
exit /b 0

:stop
echo [STOP] 입력이 없어 종료합니다.
pause
exit /b 1

:fail
echo.
echo [FAIL] GitHub 올리기 실패 - 메시지를 확인하세요.
pause
exit /b 1
