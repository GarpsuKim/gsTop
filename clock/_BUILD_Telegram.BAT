@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem 사용법: _BUILD.BAT          (직접 실행, 끝나면 pause)
rem         _BUILD.BAT nopause  (다른 bat에서 call 할 때)

rem 설치된 화면(홈 화면 아이콘 아래, 위젯 목록)에 보이는 앱 이름
rem 이 줄은 app/build.gradle.kts 가 이 파일에서 직접 읽음 (bat 끼리 값을 넘기지 않음)
rem 형식 그대로 유지: set "AppIcon=이름"
set "AppIcon=KJA시계"

echo 앱 이름: %AppIcon%
call "%~dp0gradlew.bat" assembleDebug
if errorlevel 1 (
    echo ■■■■■■■■■ 빌드 실패
    echo ■■■■■■■■■ 빌드 실패
    if /i not "%~1"=="nopause" pause
    exit /b 1
)
echo 빌드 완료: app\build\outputs\apk\debug\app-debug.apk
if /i not "%~1"=="nopause" pause
exit /b 0
