@echo off
chcp 65001 >nul
setlocal
set SDK=C:\Android\sdk
set SM=%SDK%\cmdline-tools\latest\bin\sdkmanager.bat
set ZIP=%TEMP%\cmdline-tools.zip

if exist "%SM%" goto install

echo [1/3] 명령줄 도구 다운로드
mkdir "%SDK%" 2>nul
curl -L -o "%ZIP%" https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip
if errorlevel 1 goto fail
powershell -NoProfile -Command "Expand-Archive -Force '%ZIP%' '%SDK%\tmp'"
if errorlevel 1 goto fail
mkdir "%SDK%\cmdline-tools" 2>nul
move "%SDK%\tmp\cmdline-tools" "%SDK%\cmdline-tools\latest" >nul
rmdir /s /q "%SDK%\tmp"
del "%ZIP%"

:install
echo [2/3] SDK 구성요소 설치 및 라이선스 동의
(for /l %%i in (1,1,30) do @echo y) | call "%SM%" --sdk_root=%SDK% --licenses >nul
call "%SM%" --sdk_root=%SDK% "platform-tools" "platforms;android-34" "build-tools;34.0.0"
if errorlevel 1 goto fail

echo [3/3] local.properties 생성
echo sdk.dir=C:\\Android\\sdk>"%~dp0local.properties"

echo 설정 완료
exit /b 0

:fail
echo 설정 실패
exit /b 1
