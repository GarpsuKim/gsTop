@echo off
rem install_tailscale.bat - install Tailscale with winget
where winget >nul 2>&1
if errorlevel 1 (
    echo ERROR: winget not found. Install "App Installer" from Microsoft Store.
    pause
    exit /b 1
)
echo Installing Tailscale...
winget install --id Tailscale.Tailscale -e --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
    echo.
    echo Install failed or Tailscale is already installed ^(exit code %errorlevel%^).
    pause
    exit /b 1
)
echo.
echo Done. Log in from the Tailscale tray icon, then run: tailscale ip -4
pause
