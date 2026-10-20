@echo off
g++ -std=c++17 -O2 -pthread -o digital_clock.exe digital_clock.cpp
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
echo Build OK: digital_clock.exe
