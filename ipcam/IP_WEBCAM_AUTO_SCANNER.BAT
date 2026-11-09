```bat
@echo off
chcp 65001 >nul
setlocal EnableExtensions

cd /d "%~dp0"

set "OUT=%~dp0CameraList.txt"

echo ========================================
echo       IP WEBCAM AUTO SCANNER
echo ========================================
echo.
echo Searching network...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $cfg=Get-NetIPConfiguration | Where-Object {$_.IPv4DefaultGateway -and $_.IPv4Address} | Select-Object -First 1; if (!$cfg) { Write-Host 'No active LAN connection'; exit 1 }; $local=$cfg.IPv4Address[0].IPAddress; $prefix=($local -split '\.')[0..2] -join '.'; $ports=@(8080,4747,80,8000,8081); $found=@(); Write-Host ('My PC IP: '+$local); Write-Host ('Network: '+$prefix+'.0/24'); foreach($n in 1..254) { $ip=$prefix+'.'+$n; if($ip -eq $local) { continue }; foreach($p in $ports) { $c=New-Object System.Net.Sockets.TcpClient; try { $ar=$c.BeginConnect($ip,$p,$null,$null); if($ar.AsyncWaitHandle.WaitOne(180)) { $c.EndConnect($ar); $line=$ip+':'+$p; Write-Host ('[FOUND] '+$line); $found+= $line }; } catch {} finally { $c.Close() } } }; $result=@('IP WEBCAM SCAN RESULT','PC IP: '+$local,'Network: '+$prefix+'.0/24','Scan Date: '+(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'),'--------------------------------'); if($found.Count -gt 0) { $result+= $found } else { $result+='No open camera candidate found.' }; $result | Set-Content -LiteralPath '%OUT%' -Encoding UTF8; Write-Host ''; Write-Host ('Saved: %OUT%'); Write-Host ('Found: '+$found.Count)"

echo.
echo ========================================
echo Scan completed.
echo Result: CameraList.txt
echo ========================================

if exist "%OUT%" (
    echo.
    type "%OUT%"
)

echo.
pause
endlocal
```