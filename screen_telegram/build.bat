@echo off
cd /d "%~dp0"

rem Compile version resource (screen_telegram.rc, version.h)
windres -c 65001 -I. -O coff -i screen_telegram.rc -o screen_telegram_res.o > build_ERR.txt 2>&1
if errorlevel 1 (
  echo Resource build FAILED. See build_ERR.txt
  exit /b 1
)

g++ -O2 -std=c++17 -mwindows -o screen_telegram.exe screen_telegram.cpp screen_telegram_res.o ^
    -lgdiplus -lgdi32 -luser32 -lole32 -loleaut32 -lwinhttp -lmfuuid -lcomdlg32 ^
    -static -static-libgcc -static-libstdc++ ^
    >> build_ERR.txt 2>&1

if errorlevel 1 (
  echo Build FAILED. See build_ERR.txt
  exit /b 1
)
del build_ERR.txt

rem Generate version.txt from version.h (APP_VERSION_STR). Upload it to the server together with screen_telegram.exe.
rem screen_telegram_RUN.BAT downloads this tiny file first and fetches the exe only when it is newer.
set "APPVER="
for /f "tokens=3" %%v in ('findstr /b /c:"#define APP_VERSION_STR" version.h') do set "APPVER=%%~v"
if not defined APPVER (
  echo Build OK, but version.txt was NOT created: APP_VERSION_STR not found in version.h
  exit /b 1
)
(echo %APPVER%)>version.txt
echo Build OK: screen_telegram.exe  (version %APPVER%, version.txt written)
echo Upload screen_telegram.exe and version.txt to the server.
