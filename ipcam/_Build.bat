@echo off
chcp 65001 >nul
cd /d "%~dp0"
set LOG=build_ERR.log
echo ==== build %date% %time% ==== > %LOG%
echo cwd=%CD% >> %LOG%
where g++ >> %LOG% 2>&1
if errorlevel 1 (
    echo [ERROR] g++ not found. Add MinGW-w64 bin folder to PATH.
    echo [ERROR] g++ not found >> %LOG%
    pause
    exit /b 1
)
g++ --version >> %LOG% 2>&1
g++ -v 2>&1 | findstr /i "Thread model Target" >> %LOG%
if not exist ipcam.cpp (
    echo [ERROR] ipcam.cpp not found in %CD%
    echo [ERROR] ipcam.cpp not found >> %LOG%
    pause
    exit /b 1
)
if exist ipcam.exe del ipcam.exe
echo g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static >> %LOG%

echo g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static 

g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static >> %LOG% 2>&1
set RC=%errorlevel%
echo g++ exit code=%RC% >> %LOG%
if not %RC%==0 (
    echo.
    echo [ERROR] Build failed. See build.log
	type %LOG%
    pause
    exit /b 1
)
if not exist ipcam.exe (
    echo [ERROR] ipcam.exe was not created. See build.log
	type %LOG%
    pause
    exit /b 1
)
echo ==================== 
echo Build OK: ipcam.exe
echo ==================== 
timeout /t 3 /nobreak > nul
del %LOG%
exit /b 0