@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem 와이파이(adb) 설치용 빌드 - _RUN_WIFI.BAT 이 부름
rem 사용법: _BUILD_WIFI.BAT          (직접 실행, 끝나면 pause)
rem         _BUILD_WIFI.BAT nopause  (다른 bat에서 call 할 때)

rem 설치된 화면(홈 화면 아이콘 아래, 위젯 목록)에 보이는 앱 이름
rem 이 줄은 app/build.gradle.kts 가 이 파일에서 직접 읽음 (아래 -PappNameBat 로 이 파일 이름을 알려 줌)
rem 형식 그대로 유지: set "AppIcon=이름"
set "AppIcon=영원한_시계"

rem 앱 ID 꼬리: 비움 = com.example.clock (_RUN_WIFI.BAT, _DUMP_PREFS.BAT 이 이 ID 를 씀)
set "AppIdSuffix="

echo 앱 이름: %AppIcon%
call "%~dp0gradlew.bat" assembleDebug "-PappNameBat=%~nx0"
if errorlevel 1 (
    echo ■■■■■■■■■ 빌드 실패
    echo ■■■■■■■■■ 빌드 실패
    if /i not "%~1"=="nopause" pause
    exit /b 1
)
echo 빌드 완료: app\build\outputs\apk\debug\app-debug.apk
if /i not "%~1"=="nopause" pause
exit /b 0
