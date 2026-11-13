@echo off
setlocal
cd /d "%~dp0"
set APP=gsHomeManager
set ERR=build_ERR.txt

rem --- 필수 도구/입력 파일 확인
where g++ >nul 2>&1 || (echo [%APP%] g++ not found in PATH & pause & exit /b 1)
where windres >nul 2>&1 || (echo [%APP%] windres not found in PATH & pause & exit /b 1)
for %%f in (%APP%.cpp %APP%.rc %APP%.ico version.h) do if not exist %%f (echo [%APP%] missing file: %%f & pause & exit /b 1)

rem --- 실행 중인 시계가 exe 를 잠그고 있으면 링크가 Permission denied 로 실패하므로 먼저 종료
taskkill /f /im %APP%.exe >nul 2>&1 && timeout /t 1 /nobreak >nul
if exist %ERR% del %ERR%

rem --- 1) 버전 리소스 (gsHomeManager.rc + version.h -> gsHomeManager_res.o)
windres -c 65001 -I. -O coff -i %APP%.rc -o %APP%_res.o > %ERR% 2>&1
if errorlevel 1 goto :fail

rem --- 2) 컴파일 + 링크 (-lwininet 은 불필요: wininet.dll 을 실행 중에 불러옴)
g++ -Os -s -static -mwindows -Wl,--gc-sections -o %APP%.exe %APP%.cpp %APP%_res.o -lcomdlg32 -lole32 -lcomctl32 -liphlpapi -lws2_32 -lbthprops   -lgdiplus -lgdi32  >> %ERR% 2>&1
if errorlevel 1 goto :fail

rem --- 성공: 경고가 있으면 보여 주고, 중간 파일/로그 정리
set VER=?
for /f "tokens=3" %%v in ('findstr /b /c:"#define APP_VERSION_STR" version.h') do set VER=%%~v
for %%f in (%ERR%) do if %%~zf gtr 0 (echo [%APP%] warnings: & type %ERR%)
del %APP%_res.o >nul 2>&1
del %ERR% >nul 2>&1
for %%f in (%APP%.exe) do echo [%APP%] BUILD OK  v%VER%  %%~zf bytes
pause
exit /b 0

:fail
echo [%APP%] Build FAILED. See %ERR%
start "" c:\Windows\system32\notepad.exe %APP%.cpp
start "" c:\Windows\system32\notepad.exe %ERR%
exit /b 1
