@echo off
chcp 65001
setlocal
cd /d "%~dp0"
set APP=gsHomeManager
set ERR=build_ERR.txt

rem --- 필수 도구/입력 파일 확인
where g++ >nul 2>&1 || (echo [%APP%] g++ not found in PATH & pause & exit /b 1)
where windres >nul 2>&1 || (echo [%APP%] windres not found in PATH & pause & exit /b 1)
for %%f in (%APP%.cpp %APP%.rc %APP%.ico version.h) do if not exist %%f (echo [%APP%] missing file: %%f & pause & exit /b 1)

rem --- 실행 중인 시계가 exe 를 잠그고 있으면 링크가 Permission denied 로 실패하므로 먼저 종료
rem     (screen_telegram.exe 는 gsHomeManager.cpp 에 통합됨. 옛 exe 가 떠 있으면 텔레그램 getUpdates 409 충돌이 나므로 같이 종료)
taskkill /f /im screen_telegram.exe >nul 2>&1
taskkill /f /im %APP%.exe >nul 2>&1 && timeout /t 1 /nobreak >nul
if exist %ERR% del %ERR%

rem --- 1) 버전 리소스 (gsHomeManager.rc + version.h -> gsHomeManager_res.o)

@echo on
echo  ========================
windres -c 65001 -I. -O coff -i %APP%.rc -o %APP%_res.o > %ERR% 2>&1
if errorlevel 1 goto :fail

rem --- 2) 컴파일 + 링크 (-lwininet 은 불필요: wininet.dll 을 실행 중에 불러옴. 텔레그램 통합으로 -lwinhttp -lmfuuid -loleaut32 추가)
g++ -Os -s -static -mwindows -Wl,--gc-sections -o %APP%.exe %APP%.cpp %APP%_res.o -lcomdlg32 -lole32 -loleaut32 -lcomctl32 -liphlpapi -lws2_32 -lbthprops -lgdiplus -lgdi32 -lwinhttp -lmfuuid >> %ERR% 2>&1
if errorlevel 1 goto :fail
@echo off

rem --- 성공: 경고가 있으면 보여 주고, 중간 파일/로그 정리
set VER=?
for /f "tokens=3" %%v in ('findstr /b /c:"#define APP_VERSION_STR" version.h') do set VER=%%~v
rem --- 새 version.txt 생성 (내용: version.h 의 APP_VERSION_STR). exe 와 함께 GitHub 에 올려야 launcher 가 갱신을 인식함
if not "%VER%"=="?" (>version.txt echo %VER%)
for %%f in (%ERR%) do if %%~zf gtr 0 (echo [%APP%] warnings: & type %ERR%)
del %APP%_res.o >nul 2>&1
del %ERR% >nul 2>&1
echo ================
for %%f in (%APP%.exe) do echo [%APP%] BUILD OK  v%VER%  %%~zf bytes
echo ================
pause
exit /b 0

:fail
echo [%APP%] Build FAILED. See %ERR%
start "" c:\Windows\system32\notepad.exe %APP%.cpp
start "" c:\Windows\system32\notepad.exe %ERR%
exit /b 1
