@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
rem Release build signed with the release key - for GitHub Releases / Play Store.
rem usage: _BUILD_RELEASE.BAT        signed APK -> app\build\outputs\apk\release\app-release.apk
rem        _BUILD_RELEASE.BAT aab    signed APK + AAB for Play -> app\build\outputs\bundle\release\app-release.aab
rem The keystore password is asked every time (hidden input) and is not stored anywhere.
rem Keystore: C:\tools\keys\garpsukim-release.jks  (alias garpsukim)
rem   Keep the keystore file AND its password backed up outside this PC.
rem   If it is lost, installed users can never get updates (they must uninstall and reinstall).
rem AppIcon / AppIdSuffix: read by app/build.gradle.kts from this file (UTF-8), like the other build bats.
rem NOTE: keep comments ASCII only. Korean text in rem lines after chcp 65001
rem       can make cmd misread the file.
set "AppIcon=영원한 시계"
set "AppIdSuffix="
set "KEYSTORE=C:\tools\keys\garpsukim-release.jks"

if not exist "%KEYSTORE%" (
    echo 서명 키가 없습니다: %KEYSTORE%
    echo 먼저 keytool 로 서명 키를 만드세요.
    pause
    exit /b 1
)

rem ---- keystore password (hidden input, kept only in this window's environment until the build ends) ----
set "CLOCK_KEYSTORE_PASSWORD="
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "$s = Read-Host 'Keystore password' -AsSecureString; [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))"`) do set "CLOCK_KEYSTORE_PASSWORD=%%p"
if not defined CLOCK_KEYSTORE_PASSWORD (
    echo 비밀번호가 입력되지 않았습니다
    pause
    exit /b 1
)

set "TASKS=assembleRelease"
if /i "%~1"=="aab" set "TASKS=assembleRelease bundleRelease"

echo 앱 이름: %AppIcon%
call "%~dp0gradlew.bat" %TASKS% "-PappNameBat=%~nx0" "-Pclock.keystore=%KEYSTORE%"
set "RC=%errorlevel%"
set "CLOCK_KEYSTORE_PASSWORD="
if not "%RC%"=="0" (
    echo ■■■■■■■■■ 빌드 실패 - 비밀번호가 틀렸거나 코드 오류일 수 있습니다
    pause
    exit /b 1
)
if not exist "app\build\outputs\apk\release\app-release.apk" (
    echo ■■■■■■■■■ 서명된 APK 가 없습니다 ^(app-release-unsigned.apk 만 있으면 서명 설정이 안 된 것^)
    pause
    exit /b 1
)
echo 빌드 완료 (서명됨): app\build\outputs\apk\release\app-release.apk
if /i "%~1"=="aab" echo Play 스토어용: app\build\outputs\bundle\release\app-release.aab
pause
exit /b 0
