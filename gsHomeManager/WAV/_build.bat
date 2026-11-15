
@echo off
chcp 65001 > nul
title WAV Player Build

echo.
echo ================================
echo       WAV PLAYER BUILDER
echo ================================
echo.

where g++ > nul 2>&1

if errorlevel 1 (
    echo ERROR: MinGW g++ not found.
    echo Install MinGW-w64 and add it to PATH.
    pause
    exit /b 1
)

if not exist WavPlayer.cpp (
    echo ERROR: WavPlayer.cpp not found.
    pause
    exit /b 1
)

if not exist miniaudio.h (
    echo ERROR: miniaudio.h not found.
    pause
    exit /b 1
)

echo Compiling...

g++ WavPlayer.cpp -o WAVPlayer.exe ^
-std=c++17 ^
-O2 ^
-static ^
-static-libgcc ^
-static-libstdc++ ^
-lwinmm -lole32 -luuid -lksuser -lavrt -mwindows

if errorlevel 1 (
    echo.
    echo BUILD FAILED!
    pause
    exit /b 1
)

echo.
echo ================================
echo BUILD SUCCESS!
echo WAVPlayer.exe created.
echo ================================

pause
