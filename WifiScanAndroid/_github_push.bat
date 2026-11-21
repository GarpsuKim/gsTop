@echo off
chcp 65001 >nul
rem ------------------------------------------------------------------
rem  GitHub(gsTop 저장소의 WifiScanAndroid 폴더)에 올리기
rem  1) 이 폴더에서 먼저 기록(commit):  git add -A  ^&  git commit -m "무엇을 바꿨는지"
rem  2) 이 파일 실행 -> 마지막 기록 상태를 C:\tools\gsTop\WifiScanAndroid 에 복사하고 push
rem  (.gitignore 에 적힌 파일 - 봇 토큰, 사진 아이콘, 빌드 결과 - 은 올라가지 않음)
rem ------------------------------------------------------------------
cd /d "%~dp0"
set GH=C:\tools\gsTop
set SUB=WifiScanAndroid

git diff --quiet HEAD
if errorlevel 1 (
    echo 아직 기록하지 않은 변경이 있습니다. 먼저 git add -A 와 git commit 을 하세요.
    pause
    exit /b 1
)

if not exist "%GH%\.git" git clone https://github.com/GarpsuKim/gsTop "%GH%"
git -C "%GH%" pull
if errorlevel 1 (
    echo gsTop 저장소를 받지 못했습니다.
    pause
    exit /b 1
)

if exist "%GH%\%SUB%" rmdir /s /q "%GH%\%SUB%"
mkdir "%GH%\%SUB%"
git archive HEAD | tar -x -C "%GH%\%SUB%"

for /f "delims=" %%m in ('git log -1 --pretty^=format:%%s') do set MSG=%%m
git -C "%GH%" add -A %SUB%
git -C "%GH%" commit -m "%SUB%: %MSG%"
git -C "%GH%" push
if errorlevel 1 (
    echo push 실패 - GitHub 로그인을 확인하세요.
    pause
    exit /b 1
)
echo GitHub 올리기 완료: https://github.com/GarpsuKim/gsTop/tree/main/%SUB%
pause
