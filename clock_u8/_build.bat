@echo off
g++ -Os -s -static -mwindows -Wl,--gc-sections -o clock_u8.exe clock_u8.cpp -lcomdlg32 -lole32 -lcomctl32
if errorlevel 1 (echo BUILD FAILED) else (echo BUILD OK)
pause
