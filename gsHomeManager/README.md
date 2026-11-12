# gsHomeManager

Windows 11용 텍스트 시계 + 집 감시(WiFiScan) 앱입니다. 기존 gsClock 과 WiFiScan 을 하나로 합쳤습니다. C++(g++/MinGW) 단일 소스 파일이며, 외부 라이브러리 없이 빌드됩니다. 버전 정보(exe 속성)는 `version.h` + `gsHomeManager.rc` 로 넣습니다. 아이콘은 `gsHomeManager.ico` 를 같은 rc 로 exe 에 넣습니다 (빌드할 때 `gsHomeManager.ico` 가 같은 폴더에 있어야 함).

## 특징

- 시분초 / 년월일 텍스트 시계, 글자가 창 안에서 좌우로 부드럽게 스크롤
- 일정 간격으로 창 위치를 랜덤 이동 (메뉴에서 간격 선택, 0 = 고정)
- 세계 도시 시계 (Global): 도시를 선택할 때마다 시계 창이 추가되며, 목록은 종료 시 저장되어 재시작 시 복원
- 알람 10개: 시각 지정, 동영상/음악 파일을 시스템 기본 플레이어로 실행, 플레이어 자동 닫기, 알람 테스트
- 바탕색, 투명도, 시분초/년월일 폰트(이름, 크기, 색) 변경
- 항상 위, 모든 모니터, 모든 가상 데스크탑 적용(On/Off)
- 시분초 / 년월일 표시 형식 선택 (각 10종)
- 음력 날짜 표시 (년월일 다음 줄, 천문연구원 공공API, 설정 > 음력 보이기)
- 창 더블클릭 시 타이틀바/메뉴바 숨김/표시 전환
- 이미 실행 중이면 기존 앱을 정상 종료(위치/도시 목록 저장)시키고 새로 시작, 응답이 없으면 강제 종료
- 시작할 때는 트레이 숨김 상태와 상관없이 항상 화면에 표시, 마지막 창 위치 저장
- WiFiScan: 앱 시작 시 자동으로 scan On. Bluetooth / Wi-Fi(LAN) scan 으로 집에 있는지 감시하고 `/HomeBack`, `/NotHome` 을 `..\screen_telegram\trigger.txt` 에 기록 (아래 WiFiScan 참고)
- trigger.txt 를 갱신할 때마다 모니터 중앙에 5초 동안 팝업 메시지를 표시
- WiFiScan 메뉴의 Wifi공유기 자동감시 On / Off: 정해 둔 시각에 scan 을 자동으로 켜고 끄며, 음악/동영상 재생·메모 표시도 가능 (설정하지 않으면 scan 에 영향 없음)
- 메인 윈도우 맨 아래 상태줄에 scan On / Off 가 될 때마다 시각과 사유를 표시
- IP cam: 안드로이드 폰 카메라 앱(DroidCam, IP Webcam 등)의 MJPEG 영상을 별도 창에 실시간 표시 (예전 ipcam 통합)
- 내부 문자열은 모두 UTF-8 (소스에 `wchar_t` 없음), 메모리 사용 최소화

## 빌드

MinGW-w64 의 g++ 와 windres 가 필요합니다. `gsHomeManager.cpp` 는 `version.h` 의 버전 문자열을 사용하므로 같은 폴더에 `version.h` 가 있어야 합니다. `_build.bat` 이 아래를 순서대로 수행합니다.

```bat
windres -c 65001 -I. -O coff -i gsHomeManager.rc -o gsHomeManager_res.o
g++ -Os -s -static -mwindows -Wl,--gc-sections -o gsHomeManager.exe gsHomeManager.cpp gsHomeManager_res.o -lcomdlg32 -lole32 -lcomctl32 -liphlpapi -lws2_32 -lbthprops -lgdiplus -lgdi32
```

- 실행 중인 `gsHomeManager.exe` 가 있으면 먼저 종료합니다 (파일 잠금으로 링크가 실패하는 것을 방지).
- 실패하면 소스와 `build_ERR.txt` 를 메모장으로 엽니다. 성공하면 경고(있으면)와 버전/크기를 표시합니다.
- 프로그램 이름(`APP_NAME`, 현재 gsHomeManager)과 버전은 `version.h` 의 `APP_NAME`, `APP_VERSION_NUM`, `APP_VERSION_STR` 만 고치면 됩니다. About 창, exe 속성, `version.txt` 에 모두 반영됩니다.
- 앱은 실행할 때마다 exe 와 같은 폴더에 `version.txt` (내용: `APP_VERSION_STR`)를 새로 만듭니다.
- `_run.bat` 은 exe 가 없으면 빌드 후 실행합니다.

## 사용법

창을 우클릭하면 메뉴가 나타납니다.

| 메뉴 | 설명 |
|---|---|
| 설정 | 초기화, 내 폰 등록(WiFiScan 대화상자), 바탕색, 투명도, 폰트, 위치 이동 간격, 시분초/년월일 표시 형식, 항상 위, 모든 모니터/가상 데스크탑 |
| Global | 도시 선택(체크 표시, 중복 선택 불가). 도시 창을 닫으면 그 도시 시계만 종료 |
| 알람 | 알람1 ~ 알람10, 시스템 재시작, 프로그램 실행 (아래 참고) |
| WiFiScan | WifiScan모니터링(설정/진행 상황 대화상자), Wifi공유기 자동감시 On / Off, HomeBack 프로그램, NotHome 프로그램 (아래 참고) |
| IP cam | IP cam 등록(IP address / port 를 ini 에 저장), IP cam 영상(영상 창 열기) |
| Help | About(프로그램 이름/버전/개발자), ReadMe.MD(기본 브라우저로 열기), 홈페이지(주소 표시), 최신 버전 다운로드(GitHub zip 다운로드 링크 열기) |
| 종료 | 확인창(30초, Yes/No) 후 앱 종료. 창 X 버튼, ALT+F4 도 동일 |

### 알람

각 알람 서브메뉴 항목:

- **사용**: 매일 반복
- **시각 설정**: 시:분:초
- **동영상/음악 파일 선택**: 알람 시각에 시스템 기본 플레이어로 실행
- **플레이어 자동 닫기**: 끔 / 재생이 끝나면 / 지정 시간 후
- **닫기 시간 설정**: 닫기까지의 시간 (0 = 닫지 않음)
- **알람 지우기**
- **알람 테스트**: 선택된 파일을 1회만 실행 (설정 변경 없음)

자동 닫기는 파일 재생 길이(+3초)를 셸 속성으로 읽어 계산하며, 읽지 못하면 지정 시간을 사용합니다. 실행 후 8초 안에 새로 생긴 플레이어 창을 찾아 `WM_CLOSE` 를 보내는 방식이므로 다음 제약이 있습니다.

- 이미 열려 있던 플레이어 창을 재사용하거나 UWP 앱이 창을 다르게 만들면 닫히지 않을 수 있습니다.
- 재생 중 일시정지/이동하면 길이 기준 시점이 어긋납니다.

### Wifi공유기 자동감시 On / Off

WiFiScan 메뉴의 두 항목으로, 알람과 같은 방식(사용 / 시각 설정 / 동영상·음악 파일 / 메모 / 플레이어 자동 닫기 / 지우기 / 테스트)으로 설정합니다. 시각을 정하면 바로 사용 상태가 되며 매일 반복됩니다.

- On 시각이 되면 scan 을 시작하고, Off 시각이 되면 scan 을 정지합니다. 이미 그 상태이면 그대로 두고 상태줄에만 표시합니다.
- 같은 시각에 지정한 음악/동영상을 시스템 기본 플레이어로 재생하고, 메모가 있으면 모니터 중앙에 30초 표시합니다. 파일/메모는 없어도 됩니다.
- ini 의 `[WiFiScan]` 에 `ScanOnTime` / `ScanOffTime` 이 없으면 아무 영향이 없습니다.
- '파일/메모 테스트' 는 파일과 메모만 실행하고 scan 상태는 바꾸지 않습니다.

### HomeBack 프로그램 / NotHome 프로그램

WiFiScan 메뉴의 두 항목으로, '프로그램 실행'과 같은 방식(사용 / BAT·CMD·EXE 파일 선택 / 메모 / 지우기 / 테스트)이며 시각 설정만 없습니다. `trigger.txt` 에 `/HomeBack` 또는 `/NotHome` 을 쓸 때마다 해당 프로그램을 실행하고, 메모가 있으면 모니터 중앙에 30초 표시합니다. 앱이 시작될 때 현재 상태를 한 번 전송하므로 그때도 실행됩니다. ini 키는 `[WiFiScan]` 의 `HomeBackProgOn/File/Memo`, `NotHomeProgOn/File/Memo` 입니다.

### 상태줄과 trigger.txt 팝업

- 메인 윈도우 맨 아래 줄에 `HH:MM:SS  scan On - 사유` / `scan Off - 사유` 형식으로 마지막 상태를 표시합니다 (프로그램 시작, 대화상자, 예약 시각, END 파일).
- `trigger.txt` (`/HomeBack`, `/NotHome`) 를 갱신할 때마다 모니터 중앙에 5초 동안 팝업을 띄웁니다 (클릭하면 닫힘).

## WiFiScan

앱이 시작되면 scan 이 자동으로 On 되며, 메뉴 **WiFiScan > WifiScan모니터링** 으로 대화상자를 엽니다.

- 입력: scan 주기(초). 블루투스 MAC / LAN 카드 MAC 칸은 직접 입력할 수 없고, 목록의 체크박스로만 지정합니다
- 메뉴 **설정 > 내 폰 등록** 은 같은 대화상자를 '본인 스마트폰 항목에 체크하고, 저장 버튼을 누르세요' 안내와 함께 엽니다
- 설정값은 `gsHomeManager.ini` 의 `[WiFiScan]` 에 저장 — `gsHomeManager.ini` 의 `[WiFiScan]` 에 저장
- 체크박스: Wi-Fi, Bluetooth (체크하면 scan 대상)
- 버튼: **저장**(저장 후 바로 적용), **취소**(저장 안 함), **scan재시작**(저장 후 재시작), **scan Off**(작업 정지)
- 대화상자 중하단: IP 주소, 컴퓨터 이름, MAC 주소, 네트워크 어댑터 제조사, 사용자 텍스트 목록과 진행 상황 (빨강=OFF, 노랑=확인 중, 초록=ON)
- 목록 맨 앞 체크박스: 체크한 행의 MAC 이 해당 입력 칸(블루투스 행 = 블루투스 MAC, Wi-Fi 행 = LAN 카드 MAC)에 들어가며, **저장/scan재시작** 때 ini 에 기록됩니다. 입력 칸과 같은 MAC 의 행은 자동으로 체크됩니다. 블루투스 행의 IP Address 칸에는 `<blue tooth>` 로 표시되고, 모든 칸이 빈 행은 표시하지 않습니다.
- 대화상자를 닫아도 scan 은 계속 진행됩니다 (멈추려면 scan Off).
- 생성 파일은 기존 WiFiScan 과 동일: `WiFiScan_result.txt`, `WiFiScan_watch.log`, `oui.txt`(제조사 조회), `..\screen_telegram\trigger.txt`, `..\screen_telegram\wifiscanTriger.txt`
- 기존 WiFiScan.exe 는 중복되므로 함께 실행하지 마세요.

## IP cam

폰의 카메라 앱(**DroidCam** 포트 4747, **IP Webcam** 포트 8080)이 내보내는 MJPEG 영상(`GET /video`)을 보여 줍니다. 폰과 PC 가 같은 Wi-Fi 에 있어야 하고, 폰 앱의 서버를 먼저 시작하세요.

- **IP cam 등록**: 대화상자에 IP address 와 port 번호를 입력하면 `gsHomeManager.ini` 의 `[IPCam]` (`Host`, `Port`) 에 저장됩니다. IP address 칸에 `192.168.0.70:4747` 처럼 써도 됩니다.
- **IP cam 영상**: 영상 창을 엽니다 (등록된 주소가 없으면 먼저 등록 대화상자). 창 크기에 맞춰 비율을 유지해 표시하고, 제목줄에 fps 를 표시하며, 연결이 끊기면 2초 간격으로 자동 재접속합니다. `Esc` 또는 창 닫기(X) 는 영상 창만 닫고 앱은 계속 실행됩니다.
  등록 대화상자 아래쪽에 DroidCam 설치 안내와 Google Play 주소가 있으며, 주소를 누르면 시스템 기본 브라우저로 열립니다.

  영상 창 위쪽 버튼:
  - **우로 90도 회전 / 좌로 90도 회전**: 표시를 시계/반시계 방향으로 90도씩 회전합니다 (저장되는 사진·동영상에도 적용).
  - **사진 저장**: 현재 프레임을 JPEG 로 저장합니다.
  - **동영상 촬영**: 처음 누르면 녹화 시작, 다시 누르면 녹화 종료 및 저장 (AVI / MJPG, 외부 라이브러리 없음). 녹화 중에는 제목줄에 `REC mm:ss` 가 표시되고 회전 버튼은 잠깁니다. 파일 크기가 약 1.9GB 에 이르면 자동 종료됩니다.
  - **저장 폴더 열기**: 저장 폴더(`IPCam\`)를 탐색기로 엽니다.
  - 저장 위치: exe 와 같은 폴더의 `IPCam\` (`ipcam_YYYYMMDD_HHMMSS.jpg / .avi`). 결과는 영상 아래쪽에 6초간 표시되고 `ipcam.log` 에도 기록됩니다.
- 로그: exe 와 같은 폴더의 `ipcam.log` (예전 ipcam 과 같은 형식, 영상 창을 열 때마다 새로 작성).
- 제한: 소리/녹화/HTTP 인증은 지원하지 않으며, 영상 경로는 `/video` 로 고정입니다.

## 설정 파일 (`gsHomeManager.ini`)

exe 와 같은 폴더에 있으며, 없으면 설명 주석이 포함된 기본 파일이 생성됩니다. 시작 시 값이 적용됩니다.

| 섹션 | 키 |
|---|---|
| `[Timer]` | `MoveMs` (0 = 이동 안 함), `TimeScrollMs`, `DateScrollMs` |
| `[Style]` | `BackColor`, `Opacity`, `TimeFont`, `TimeSize`, `TimeColor`, `DateFont`, `DateSize`, `DateColor` |
| `[Option]` | `AlwaysOnTop`, `AllMonitors`, `AllDesktops`, `TimeFormat`(0~9), `DateFormat`(0~9), `ShowLunar` |
| `[Lunar]` | `ApiUrl`, `ApiKey` (음력 조회용 공공API 주소/인증키. 둘 다 있어야 '음력 보이기' 사용 가능) |
| `[Global]` | `BaseCity`, `Cities` |
| `[Alarm]` | `AlarmNOn`, `AlarmNTime`, `AlarmNFile`, `AlarmNClose` (0/1/2), `AlarmNCloseSec` |
| `[WiFiScan]` | `ScanBluetooth`, `ScanWifi`, `HomeBluetooth`, `HomeMac`, `HomeIp`, `WiFiScanWatchInterval`(0=15초, 최소 5), `NotHome`(0=60초), `ScanOnUse`/`ScanOnTime`/`ScanOnFile`/`ScanOnMemo`/`ScanOnClose`/`ScanOnCloseSec` 와 `ScanOff...` 같은 키 (scan 예약), `HomeBackProgOn`/`HomeBackProgFile`/`HomeBackProgMemo`, `NotHomeProgOn`/`NotHomeProgFile`/`NotHomeProgMemo` |
| `[IPCam]` | `Host` (IP address), `Port` (기본 4747) |
| `[Window]` | `X`, `Y` (마지막 위치) |

## 로그 (`gsHomeManager.log`)

exe 와 같은 폴더에 UTF-8 로그를 남깁니다. 한 줄에 한 동작이며 `[YYYY-MM-DD HH:MM:SS.mmm] 내용` 형식입니다.

- 기록 대상: 앱 시작/종료와 설정 로드, 중복 실행 차단, 모든 메뉴 명령, 알람 시각 도달/파일 실행/플레이어 자동 닫기, 프로그램 실행, 시스템 재시작 확인창과 결과, 종료 확인창과 결과, 메모 표시, 창 그룹 이동, 시계 보이기/숨기기와 트레이, 도시 창 추가/닫힘, 글꼴/바탕색 변경, Windows 종료/로그오프 요청
- 기록할 때만 파일을 열고 바로 닫으므로 상주 메모리/핸들이 없습니다.
- 1MB 를 넘으면 `gsHomeManager.log.old` 로 넘기고 새로 시작합니다.

## 참고

- 모든 가상 데스크탑 적용은 비공개 셸 COM 인터페이스(`IVirtualDesktopPinnedApps`)를 사용하므로 Windows 버전에 따라 동작하지 않을 수 있습니다.
- 도시별 시간대는 레지스트리의 시간대 정보를 사용합니다.

## 요구 사항

Windows 11, 블루투스 어댑터(블루투스 scan 사용 시), MinGW-w64 g++ (빌드 시)
