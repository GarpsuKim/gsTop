@echo off
cd /d "%~dp0"
g++ -O2 -std=c++17 -municode -o make_home.exe make_home.cpp -static -static-libgcc -static-libstdc++
if errorlevel 1 (
  echo Build FAILED
  exit /b 1
)
echo Build OK: make_home.exe
