@echo off
chcp 65001
setlocal
cd /d "%~dp0"

set "TARGET=%AppData%\Microsoft\Windows\Start Menu\Programs"

rem gsHomeManagerLauncher.exe 가 있으면 그것을 시작 메뉴에 등록 (실행할 때마다 서버 버전을 확인해 갱신), 없으면 gsHomeManager.exe
set "EXE=gsHomeManager.exe"
if exist gsHomeManagerLauncher.exe set "EXE=gsHomeManagerLauncher.exe"
if not exist %EXE% (echo %EXE% 가 없습니다. & pause & exit /b 1)

rem gsHomeManager.lnk 를 이 폴더의 gsHomeManager.exe 기준으로 새로 만들어 시작 메뉴에 등록 (미리 만든 lnk 는 경로가 달라지면 깨짐)
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%TARGET%\gsHomeManager.lnk'); $s.TargetPath='%~dp0%EXE%'; $s.WorkingDirectory='%~dp0'; $s.Save()"
if errorlevel 1 (echo 등록 실패 & pause & exit /b 1)

echo 시작 메뉴에 등록했습니다: %TARGET%\gsHomeManager.lnk
pause