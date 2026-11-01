@echo off
cd /d "%~dp0"

g++ -O2 -std=c++17 -o screen_telegram.exe screen_telegram.cpp ^
    -lgdiplus -lgdi32 -luser32 -lole32 -loleaut32 -lwinhttp -lmfuuid ^
    -static -static-libgcc -static-libstdc++ ^
    > build_ERR.txt 2>&1

if errorlevel 1 (
  echo Build FAILED. See build_ERR.txt
  exit /b 1
)
del build_ERR.txt
echo Build OK: screen_telegram.exe
