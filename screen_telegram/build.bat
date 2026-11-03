@echo off
cd /d "%~dp0"

rem Compile version resource (screen_telegram.rc, version.h)
windres -c 65001 -I. -O coff -i screen_telegram.rc -o screen_telegram_res.o > build_ERR.txt 2>&1
if errorlevel 1 (
  echo Resource build FAILED. See build_ERR.txt
  exit /b 1
)

g++ -O2 -std=c++17 -mwindows -o screen_telegram.exe screen_telegram.cpp screen_telegram_res.o ^
    -lgdiplus -lgdi32 -luser32 -lole32 -loleaut32 -lwinhttp -lmfuuid ^
    -static -static-libgcc -static-libstdc++ ^
    >> build_ERR.txt 2>&1

if errorlevel 1 (
  echo Build FAILED. See build_ERR.txt
  exit /b 1
)
del build_ERR.txt
echo Build OK: screen_telegram.exe
