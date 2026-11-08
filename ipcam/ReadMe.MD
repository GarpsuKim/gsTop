# ipcam — 안드로이드 폰 카메라 실시간 뷰어 (Windows 11)

안드로이드 폰의 카메라 앱(**DroidCam**, **IP Webcam** 등)이 내보내는 MJPEG 영상을
Windows 11에서 실시간으로 보여 주는 작은 C++ 프로그램입니다.
외부 라이브러리 없이 Win32 + Winsock + GDI+ 만 사용하며, 소스는 `ipcam.cpp` 한 파일입니다.

## 특징

- 외부 라이브러리 불필요 (OpenCV 등 없음), 단일 소스 파일
- 창 크기에 맞춰 비율 유지(레터박스) 표시
- 제목줄에 현재 fps 표시
- 연결이 끊기거나 실패하면 2초 간격으로 자동 재접속
- 상세 로그 파일(`ipcam.log`) 자동 생성, 크래시 시 예외 코드 기록
- 클래스 구조로 분리되어 확장이 쉬움

## 요구 사항

- Windows 11 (GDI+ 내장)
- MinGW-w64 `g++` (PATH에 등록). 예: WinLibs (`winget`으로 설치 가능)
- 폰과 PC가 **같은 Wi-Fi(같은 네트워크)** 에 연결되어 있을 것

## 폰 참고 사항(1) **DroidCam** 설치 주소

- https://play.google.com/store/apps/details?id=com.dev47apps.obsdroidcam

## 폰 참고 사항(2) **IP Webcam** 설치 주소

- https://play.google.com/store/apps/details?id=com.pas.webcam



## 파일 구성

| 파일 | 설명 |
|---|---|
| `ipcam.cpp` | 프로그램 소스 |
| `_Build.bat` | 빌드 스크립트 (성공하면 `build_ERR.log` 삭제, 실패하면 로그 출력) |
| `_Run.bat` | 실행 스크립트 (ping 확인 후 실행, 종료 뒤 로그 출력) |
| `ipcam.exe` | 빌드 결과물 |
| `ipcam.log` | 실행 로그 (exe와 같은 폴더, 실행할 때마다 새로 작성) |
| `run.log` | `_Run.bat` 실행 기록 |

## 빌드

`_Build.bat`을 실행하거나, 직접 다음 명령을 사용합니다.

```
g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static
```

## 실행

```
ipcam.exe 폰IP:포트
```

예)

```
ipcam.exe 192.168.0.70:4747
```

또는 `_Run.bat` 안의 `set CAM=` 줄을 폰 주소로 고친 뒤 실행합니다.

- 인자를 생략하면 소스에 지정된 기본값(`Config` 구조체)으로 접속합니다.
- 영상 경로는 `/video` 로 고정입니다.

### 앱별 기본 포트

| 폰 앱 | 포트 | 비고 |
|---|---|---|
| DroidCam | 4747 | 앱 화면에 표시된 Wi-Fi IP 사용 |
| IP Webcam | 8080 | 앱에서 "서버 시작" 후 표시되는 주소 사용 |

> 폰 앱을 먼저 실행(서버 시작)한 뒤 `ipcam.exe`를 실행하세요.
> 서버가 준비되기 전이라면 접속이 거부되지만, 자동 재접속으로 연결됩니다.

## 조작

| 키 | 동작 |
|---|---|
| `Esc` | 종료 |
| 창 닫기(X) | 종료 |

## 동작 방식

1. Winsock으로 폰의 HTTP 서버에 접속해 `GET /video` 요청을 보냅니다.
2. 응답(`multipart/x-mixed-replace`)에서 JPEG 시작(`FF D8`)과 끝(`FF D9`) 표시를 찾아 프레임을 잘라냅니다.
3. GDI+ 로 JPEG를 디코딩해 가장 최근 프레임만 보관합니다.
4. 창이 더블 버퍼링으로 화면에 그립니다.

네트워크 수신은 별도 스레드에서, 화면 그리기는 UI 스레드에서 동작합니다.

## 클래스 구조

| 클래스 | 역할 |
|---|---|
| `CritSec` / `ScopedLock` | Win32 크리티컬 섹션 래퍼 |
| `Logger` | 스레드 안전 파일 로거 (`LOG(...)` 매크로) |
| `Config` | 명령줄에서 host / port / path 파싱 |
| `WinsockSession` | WSAStartup / WSACleanup (RAII) |
| `GdiplusSession` | GdiplusStartup / GdiplusShutdown (RAII) |
| `JpegDecoder` | JPEG 바이트를 비트맵으로 변환 |
| `FrameStore` | 최신 프레임, 상태 문구, fps 카운터 (스레드 안전) |
| `MjpegClient` | 접속, 수신, 프레임 추출, 게시 (네트워크 스레드) |
| `ViewerWindow` | Win32 창, 메시지 처리, 그리기 |
| `App` | 위 객체들의 생성 순서와 종료 순서를 관리 |

## 로그 읽는 법

`ipcam.log`에는 시간, 스레드 ID, 내용이 기록됩니다. 정상 실행 예:

```
connecting to 192.168.0.70:4747 ...
connected
response head: HTTP/1.1 200 OK ...
first frame decoded: 1280x720
frames so far: 300
```

## 문제 해결

| 증상 / 로그 | 원인과 조치 |
|---|---|
| `g++ not found` | MinGW-w64의 `bin` 폴더를 PATH에 추가 |
| 빌드 오류 `'https' does not name a type` | 소스에 주석 처리하지 않은 텍스트(URL 등)가 있음. 앞에 `//` 추가 |
| `connect() failed wsaerr=10061` | 폰 앱의 서버가 꺼져 있거나 포트가 다름. 앱을 실행하고 포트 확인 |
| `connect() failed wsaerr=10060` / 시간 초과 | IP 오류, 다른 네트워크, 방화벽·공유기 AP 격리 설정 확인 |
| `HTTP error: ... 401` | 폰 앱에 로그인(ID/비밀번호)이 설정됨. 현재 버전은 인증 미지원 |
| 화면이 계속 "Connecting..." | `ipcam.log`의 접속·응답 기록 확인 |
| `ipcam.log`가 생기지 않음 | exe가 시작되지 못함. `run.log`와 종료 코드 확인 |

## 제한 사항

- 소리(오디오)와 녹화는 지원하지 않습니다.
- HTTP 인증(Basic 등)은 지원하지 않습니다.
- 영상 경로가 `/video` 가 아닌 앱은 소스의 `Config::path` 를 수정해야 합니다.
- 가상 웹캠 방식 앱(Iriun, Camo, Phone Link 등)은 HTTP 스트림이 없어 이 프로그램으로 볼 수 없습니다.
