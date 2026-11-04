기능 :
모니터와 웹캠의 캡쳐 이미지를 텔레그램 사용자에게 주기적으로 전송

C:\tools\screen_telegram>dir /b
screen_telegram.ini   ....... exe 실행에 필요한 파라메터
screen_telegram.cpp  ...... 이 소스
build.bat  ............. 컴파일 & 링크 (windres 로 버전 리소스 포함)
version.h  ............. 버전/저작권 정보 (버전을 올릴 때 이 파일만 수정)
screen_telegram.rc  ..... exe 속성(자세히 탭)에 표시되는 버전 정보 리소스 (UTF-8)
screen_telegram.exe  ..... 
screen_telegram_RUN.BAT ..... exe를 구동하는 명령어 파일
log.txt  ............... 부팅 정보(BAT) + 실행 로그(exe 가 직접 기록). BAT 실행 시마다 새로 작성되며 직전 로그는 log.old.txt 로 보관
ReadMe.MD .............  이 안내문
screen_telegram.cpp.001 ... 수정전 소스
screen_telegram_res.o ..... version.h/.rc 를 컴파일한 버전 리소스 오브젝트 (build.bat 이 생성, 코드는 없음)
detect.state ........... 침입 탐지 마지막 상태 (ON/OFF). 프로그램이 기록
image\ ................. 캡처 이미지 저장 폴더 (프로그램이 생성)

참고 :
- 이미지는 JPEG(품질 85)로 전송. 전송 실패(네트워크/5xx/429) 시 최대 3회 시도
- 모니터 번호는 "주 모니터=1, 나머지는 왼쪽->오른쪽" 순이며, Windows 디스플레이 설정의 번호와 다를 수 있음
- 카메라가 여러 개면 screen_telegram.ini 의 CAMERA_INDEX 로 선택 (목록은 최초 캡처 시 log.txt 에 출력)
- 잠금 화면/로그온 전에는 화면 캡처를 건너뜀 (카메라는 계속 시도)
- 서비스(세션 0)로는 화면 캡처 불가. 사용자 로그온 후 시작프로그램/작업 스케줄러(로그온 시)로 실행
- Ctrl+C 또는 창 닫기 시 정상 종료
- 전송 전에 모든 이미지를 exe 폴더의 image 폴더에 저장 (파일명: YYYYMMDD_HHMMSS_Monitor1.jpg, ..._Camera.jpg)
  * 기본은 자동 삭제 없음 (30초 간격, 모니터+카메라면 하루 약 3GB). KEEP_DAYS=N (N일 지난 이미지 삭제), KEEP_MAX_FILES=N (최대 N개, 오래된 것부터 삭제) 로 자동 정리 가능. 이 프로그램이 만든 파일명(YYYYMMDD_HHMMSS_*.jpg/png)만 삭제함
  * PREVIOUS_SESSION_CLEAR=TRUE 이면 프로그램을 시작할 때 log.txt 와 image 폴더의 모든 파일(하위 폴더 제외)을 삭제 (아래 설명)
  * 현재 INI: TELEGRAM_INTERVAL=1800(30분 간격), KEEP_DAYS=7, KEEP_MAX_FILES=999 (7일 지난 이미지 삭제, 최대 999개 유지)
- 이미지가 10MB 를 넘으면 품질/해상도를 단계적으로 낮춰 전송. 가로+세로 10000px 초과 또는 비율 20:1 초과이면 원본을 문서(sendDocument)로 전송
- CAMERA_KEEP_OPEN=TRUE 이면 카메라를 계속 열어 둠 (지연/LED 깜빡임 감소, 대신 다른 앱이 카메라를 쓸 수 없음. 기본 FALSE, 현재 INI 는 TRUE)
- SAVE_ONLY=TRUE 이면 이미지를 텔레그램으로 보내지 않고 image 폴더에 저장만 함. 명령 수신(폴링)은 계속 동작하며 /shot 만 전송 (TOKEN/CHAT_ID 가 없으면 명령 수신 없이 저장만)
- 텔레그램 명령 수신 (TELEGRAM_COMMANDS=TRUE): 봇에게 /shot(화면+카메라 즉시 캡처·전송) /screen(화면만 즉시 캡처·전송) /photo(카메라만 즉시 캡처·전송) /pause(이미지 전송 중단, 저장은 계속) /resume(전송 재개) /status(상태) /detectOn(침입 탐지 켜기) /detectOff(침입 탐지 끄기) /INTERVAL=nnn(캡처·전송 간격을 nnn초로 변경, 5~86400. 즉시 적용되고 INI 의 TELEGRAM_INTERVAL 도 갱신됨. /INTERVAL 만 보내면 현재 값 표시) /version(프로그램 버전과 빌드 시각 표시) /log(실행 로그 전송, 아래 설명) /CMD 명령(PC 에서 명령 실행 후 결과를 txt 로 전송, 아래 설명) /ini 키=값(INI 에 새 키 추가, 아래 설명) /stop(종료, Yes/No 버튼으로 확인) /help 또는 //(도움말) 전송 (명령은 대소문자를 구분하지 않음).
  * /stop 을 보내면 "종료하시겠습니까?(y/n)" 메시지에 Yes/No 버튼이 붙어 옴. Yes 를 눌러야 종료, No 는 취소. 버튼은 명령과 같은 chat/허용 사용자만 누를 수 있고, 5분이 지났거나 이미 처리한 메시지, 프로그램 재시작 전의 메시지 버튼은 무효(종료되지 않음) TELEGRAM_CHAT_ID 로 설정한 chat 의 명령만 처리. 명령은 별도 스레드가 받으므로 캡처/전송 중에도 /pause /resume /status /stop 은 수 초 내 반응 (/shot 은 진행 중인 캡처가 끝난 뒤 실행)
  * TELEGRAM_CHAT_ID 가 그룹(음수)이면 TELEGRAM_ALLOWED_USER_IDS=111,222 (명령 허용 사용자 ID) 를 반드시 설정해야 명령이 동작. 개인 chat 이면 생략 가능
  * 같은 봇 토큰을 다른 프로그램이 getUpdates/webhook 으로 쓰면 409 충돌이 나므로, 이 프로그램 전용 봇을 쓰는 것을 권장
- 보안: screen_telegram.ini 에 봇 토큰이 평문으로 들어 있으므로 공유/업로드 금지 (노출 시 BotFather 에서 /revoke)
- 침입 탐지 (CAMERA=TRUE 필요): INI 스위치가 없고 텔레그램 명령 /detectOn(켜기) /detectOff(끄기) 로 제어. 켜면 카메라를 상시 열어 INTRUSION_CHECK_MS(기본 1000ms)마다 프레임을 검사, 끄면 카메라를 해제.
  * INI 에 AUTO_DETECT=TRUE 이면 프로그램 시작 때 /detectOn 을 자동 수행 (시작 인사말에도 "침입 탐지: 켜짐"으로 표시). 현재 INI 는 TRUE 이므로 detect.state 가 OFF 여도 시작할 때 켜짐
  * AUTO_DETECT 가 FALSE/없음이면 exe 폴더의 detect.state 파일(ON/OFF)에 저장된 마지막 상태를 따름. 파일이 없으면 꺼짐
  * 움직임이 확정되면 "[침입탐지 : yyyy년 mm월 dd일 HH시 mm분 ss초]" 메시지를 보낸 뒤 카메라 이미지만 전송 (모니터 화면 캡처는 하지 않음. SAVE_ONLY, /pause 와 무관하게 무조건 전송)
  * 방식: 그레이스케일+블러 -> 이동평균 배경 차분 -> 픽셀 임계값(INTRUSION_PIXEL_THRESHOLD, 기본/현재 25) + 변화 면적(INTRUSION_AREA_PERCENT, 기본/현재 1.0%) -> 연속 프레임 확인(INTRUSION_CONFIRM_FRAMES, 기본 2 / 현재 INI 3) -> 쿨다운(INTRUSION_COOLDOWN, 기본 60초 / 현재 INI 120초). 프레임 검사 간격 INTRUSION_CHECK_MS 는 기본 1000ms / 현재 INI 200ms
  * 조명 켬/끔·자동 노출 변화(화면 60% 이상 변화)와 시작 직후 안정화 구간은 탐지하지 않음
  * 침입 탐지는 완전히 별도의 스레드로 동작: 주기 캡처, /pause, /shot, 텔레그램 전송 재시도 등과 무관하게 계속 프레임을 검사하고, 침입 확정 시 그 스레드가 직접 메시지 + 카메라 이미지(탐지된 바로 그 프레임) 저장·전송을 수행. 침입 이미지 파일명은 YYYYMMDD_HHMMSS_Intrusion_Camera.jpg
  * 탐지 중에는 카메라를 이 스레드가 전담(주기 캡처와 /photo 는 이 스레드의 최신 프레임을 사용)하므로 LED 가 항상 켜져 있고 다른 앱(화상회의 등)이 카메라를 쓸 수 없음
  * MAX_LOOP 횟수를 채운 시점에 침입 탐지가 켜져 있으면 프로그램이 종료되지 않고 계속 동작 (/stop 또는 Ctrl+C 로 종료)
  * 오탐이 많으면 AREA_PERCENT/PIXEL_THRESHOLD 를 올리고, 놓치면 낮춤
- 시작 인사말: 준비가 끝나면 텔레그램으로 인사말(간격/카메라/침입 탐지 상태 포함)을 한 번 전송. TELEGRAM_GREETING=FALSE 로 끌 수 있음
- MAX_LOOP=N 이면 N회 캡처/전송 후 자동 종료 (0 또는 생략 시 무제한)
- 버전: 1.0.0.1 (exe 속성 > 자세히 탭, 시작 인사말, /status, 로그에 표시)
- 저작권: 김갑수. 이 프로그램은 개인에 한하여 비상업적 목적으로 자유롭게 사용 할 수 있습니다. 기업이나 단체 등 개인이 아닌 사용자는 개발자 김갑수와 개별 협의하여야 합니다.
- 자동 업데이트 (screen_telegram_RUN.BAT): exe 를 시작하기 전에 GitHub 의 서버 exe(https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.exe)를 받아, 서버 exe 의 파일 버전이 PC 의 exe 보다 높을 때만 교체한 뒤 실행. 결과는 log.txt 의 UPDATE= 줄에 기록
  * 교체 전 기존 exe 는 screen_telegram.exe.bak 로 보관. 네트워크 오류/버전 정보 없음/exe 실행 중이면 교체하지 않고 현재 exe 로 계속 실행
  * 새 버전 배포: version.h 의 버전을 올리고 build.bat 으로 빌드한 exe 를 GitHub 저장소의 screen_telegram 폴더에 올림 (버전이 같거나 낮으면 PC 는 교체하지 않음)
  * Windows 10/11 에 기본 포함된 curl.exe 와 powershell 사용. 다운로드 주소는 웹 페이지 주소(blob)가 아니라 raw 주소
- /ini 키=값 : 텔레그램에서 INI 파일 끝에 새 키를 추가 (예: /ini INTRUSION_COOLDOWN=120. [키=값] 처럼 대괄호로 감싸도 됨)
  * 같은 키(대소문자 무시)가 INI 에 이미 있으면 경고 메시지만 보내고 삽입하지 않음. 기존 값은 바꾸지 않으므로, 값을 바꾸려면 INI 를 직접 수정 (캡처 간격은 /INTERVAL=nnn)
  * ;KEY=값 처럼 주석 처리된 줄은 없는 키로 봄. 키 이름은 영문/숫자/_ (64자 이내, 대문자로 저장), 값은 한 줄 400자 이내
  * 프로그램이 해석하지 않는 키이면 추가는 하되 오타 주의 메시지를 함께 보냄
  * INI 는 시작할 때만 읽으므로 추가한 값은 프로그램을 다시 시작해야 적용됨. 로그에는 키 이름만 남기고 값은 남기지 않음
- INI 자동 다운로드 (screen_telegram_RUN.BAT): 현재 폴더에 screen_telegram.ini 가 없으면 GitHub(https://github.com/GarpsuKim/gsTop/raw/main/screen_telegram/screen_telegram.ini)에서 받아 저장. 이미 있으면 절대 덮어쓰지 않음. 받은 파일에 TELEGRAM_ 키가 없으면(오류 페이지 등) 버린다. 결과는 log.txt 의 INI= 줄에 기록
  * 서버의 INI 는 공개 저장소에 올라가므로 실제 봇 토큰/chat id 를 넣지 말고 견본 값으로 올릴 것. 내려받은 뒤 TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID 를 PC 에서 직접 수정
- 실행 로그: exe 가 exe 폴더의 log.txt 에 직접 기록 (콘솔 없는 -mwindows 빌드에서도 남음). 형식 [날짜 시각.밀리초] [T스레드번호] 메시지. 시작 시 버전/PID/폴더, 종료 시 종료 코드, 처리되지 않은 예외/크래시는 원인 코드와 주소를 기록. 5MB 를 넘으면 log.old.txt 로 넘기고 새로 시작. 로그에는 봇 토큰과 /ini 로 넣은 값은 남기지 않음
- /log [n|all] : 실행 로그(log.txt) 전송. 인자 없으면 최근 50줄, /log n 은 최근 n줄(1~2000), /log all 은 log.txt 전체
  * 내용이 텔레그램 메시지 길이(4096자)를 넘으면(또는 /log all) 자동으로 .txt 문서로 전송. log.old.txt(회전된 이전 로그)는 보지 않고 현재 log.txt 만 대상으로 함
- /CMD 명령 : /CMD 뒤의 문자열 전체를 이 PC 의 cmd.exe 에서 실행하고, 그 결과(표준 출력+오류)를 텍스트 파일(cmd_YYYYMMDD_HHMMSS.txt)로 텔레그램에 전송 (예: /CMD dir /b, /CMD ipconfig /all, /CMD type log.txt | findstr UPDATE)
  * [ ] 같은 구분 기호는 없음. 명령 뒤 문자열(앞뒤 공백만 제거)이 그대로 cmd.exe /c 에 전달됨. 명령이 비어 있으면 사용법만 알려 줌
  * 캡션에 실행한 명령, 종료 코드, 소요 시간, 출력 크기를 표시. 출력이 없으면 파일 없이 "(출력 없음)" 메시지만 전송
  * 실행 창은 숨김, 표준 입력은 NUL (입력을 기다리는 명령은 바로 끝남). 시작 폴더는 프로그램의 현재 폴더
  * 제한 시간 60초: 넘으면 하위 프로세스까지 강제 종료하고 그때까지의 출력을 전송. 출력 20MB 초과분은 잘림
  * 실행하는 동안 명령 수신 스레드가 대기하므로 다른 텔레그램 명령(/stop 포함)은 끝난 뒤 처리됨 (최대 60초)
  * 한글 출력: 출력이 UTF-8 이 아니면 콘솔 코드 페이지(한국어 Windows 는 CP949)에서 UTF-8 로 변환해 전송
  * 보안 주의: 허용된 chat/사용자(TELEGRAM_CHAT_ID, TELEGRAM_ALLOWED_USER_IDS)가 PC 에서 임의의 명령을 실행할 수 있는 기능. 텔레그램 계정과 그룹 chat 의 허용 사용자 목록을 안전하게 관리할 것. 실행한 명령(200자까지)과 종료 코드는 log.txt 에 기록
  * 프로그램이 사용자 로그온 세션에서 실행 중인 권한으로 동작 (관리자 권한이 필요한 명령은 exe 를 관리자로 실행해야 함)
- PREVIOUS_SESSION_CLEAR=TRUE : 프로그램 시작 직후(INI 를 읽은 뒤, 캡처/명령 수신 전)에 이전 세션의 흔적을 지움. 기본 FALSE
  * 삭제 대상: exe 폴더의 log.txt 와 image 폴더 안의 모든 파일 (이 프로그램이 만든 이미지가 아니어도 삭제. 하위 폴더는 건드리지 않음). log.old.txt 는 삭제하지 않음
  * 삭제는 복구할 수 없음. TRUE 이면 재시작할 때마다 이미지가 전부 사라지므로 보관이 필요하면 FALSE 로 둘 것
  * screen_telegram_RUN.BAT 이 exe 시작 전에 log.txt 에 쓴 부팅 정보(PC/IP/UPDATE= 줄)도 함께 지워짐. 이전 log.txt 는 BAT 이 log.old.txt 로 보관하므로 직전 로그는 거기에 남음
  * 삭제 뒤 log.txt 에 "프로그램 시작 (이전 세션 정리 후)" 와 삭제 결과(log.txt 삭제 여부, 지운 이미지 수, 실패 수)를 기록. log.txt 가 다른 프로그램에 열려 있어 지울 수 없으면 내용만 비움
  * 시작 때 한 번만 수행. /ini 로 추가해도 다음 시작부터 적용

screen_telegram.ini 설정 요약 (현재 INI 기준. 줄 맨 앞 ; 또는 # 은 주석, 값 뒤 "공백 + ; 또는 #" 도 주석):
  키                          현재 값    설명 (기본값)
  TELEGRAM_GREETING           TRUE       시작 인사말 전송 (TRUE)
  TELEGRAM_BOT_TOKEN          (비공개)   BotFather 의 봇 토큰. 공유/업로드 금지
  TELEGRAM_CHAT_ID            (비공개)   수신 chat id. 음수(그룹)이면 TELEGRAM_ALLOWED_USER_IDS 필수
  TELEGRAM_ALLOWED_USER_IDS   (없음)     명령 허용 사용자 ID 목록 (111,222). 개인 chat 이면 생략
  TELEGRAM_INTERVAL           1800       캡처/전송 간격(초). /INTERVAL=nnn 으로 변경하면 이 값도 갱신
  MONITOR1 / MONITOR2 / MONITOR3  TRUE / FALSE / FALSE  모니터별 캡처 여부 (미기재 시 TRUE)
  CAMERA                      TRUE       웹캠 캡처 (FALSE). 침입 탐지에도 필요
  CAMERA_INDEX                0          카메라 번호(0부터) (0)
  SAVE_ONLY                   FALSE      TRUE 이면 저장만, /shot 만 전송 (FALSE)
  TELEGRAM_COMMANDS           TRUE       텔레그램 명령 수신 (TRUE)
  KEEP_DAYS                   7          N일 지난 이미지 삭제 (0=삭제 안 함)
  KEEP_MAX_FILES              999        이미지 최대 개수 (0=제한 없음)
  PREVIOUS_SESSION_CLEAR      TRUE       시작할 때 log.txt 와 image 폴더 파일 전부 삭제 (FALSE)
  CAMERA_KEEP_OPEN            TRUE       카메라 계속 열어 둠 (FALSE)
  AUTO_DETECT                 TRUE       시작 때 침입 탐지 자동 켬 (FALSE, 그러면 detect.state 를 따름)
  INTRUSION_PIXEL_THRESHOLD   25         픽셀 밝기 변화 임계값 1~255 (25)
  INTRUSION_AREA_PERCENT      1.0        변화 면적 % (1.0)
  INTRUSION_CONFIRM_FRAMES    3          연속 확인 프레임 수 (2)
  INTRUSION_COOLDOWN          120        재탐지까지 대기 초 (60)
  INTRUSION_CHECK_MS          200        프레임 검사 간격 ms, 200~10000 (1000)
  MAX_LOOP                    0          캡처/전송 횟수 후 종료 (0=무제한)
  TEST                        kkkkk      프로그램이 해석하지 않는 키 (삭제해도 됨)
