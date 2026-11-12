@echo off
chcp 65001
setlocal
cd /d "%~dp0"

set "TARGET=%AppData%\Microsoft\Windows\Start Menu\Programs"

if not exist gsHomeManager.exe (echo gsHomeManager.exe 가 없습니다. & pause & exit /b 1)

rem gsHomeManager.lnk 를 이 폴더의 gsHomeManager.exe 기준으로 새로 만들어 시작 메뉴에 등록 (미리 만든 lnk 는 경로가 달라지면 깨짐)
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%TARGET%\gsHomeManager.lnk'); $s.TargetPath='%~dp0gsHomeManager.exe'; $s.WorkingDirectory='%~dp0'; $s.Save()"
if errorlevel 1 (echo 등록 실패 & pause & exit /b 1)

echo 시작 메뉴에 등록했습니다: %TARGET%\gsHomeManager.lnk
pause