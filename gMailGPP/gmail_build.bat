@echo off
cd /d "%~dp0"

rem curl installed by: winget install --id cURL.cURL -e
set "CURL_DIR="
for /d %%P in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\cURL.cURL_*") do (
  for /d %%D in ("%%P\curl-*-win64-mingw") do set "CURL_DIR=%%D"
)
if not defined CURL_DIR echo curl not found. Run: winget install --id cURL.cURL -e & exit /b 1
echo Using curl: %CURL_DIR%

g++ -std=c++17 -O2 -o gmail_send.exe gmail_send.cpp -I "%CURL_DIR%\include" -L "%CURL_DIR%\lib" -lcurl > build_ERR.txt 2>&1
if errorlevel 1 (
  echo Build FAILED. See build_ERR.txt
  exit /b 1
)
del build_ERR.txt

copy /y "%CURL_DIR%\bin\libcurl*.dll" . >nul
copy /y "%CURL_DIR%\bin\curl-ca-bundle.crt" . >nul 2>nul
echo Build OK: gmail_send.exe