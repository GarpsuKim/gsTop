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
log.txt  ............... 부팅 정보 + 실행 로그 (BAT 실행 시마다 새로 작성)
ReadMe.MD .............  이 안내문
screen_telegram.cpp.001 ... 수정전 소스

참고 :
- 이미지는 JPEG(품질 85)로 전송. 전송 실패(네트워크/5xx/429) 시 최대 3회 시도
- 모니터 번호는 "주 모니터=1, 나머지는 왼쪽->오른쪽" 순이며, Windows 디스플레이 설정의 번호와 다를 수 있음
- 카메라가 여러 개면 screen_telegram.ini 의 CAMERA_INDEX 로 선택 (목록은 최초 캡처 시 log.txt 에 출력)
- 잠금 화면/로그온 전에는 화면 캡처를 건너뜀 (카메라는 계속 시도)
- 서비스(세션 0)로는 화면 캡처 불가. 사용자 로그온 후 시작프로그램/작업 스케줄러(로그온 시)로 실행
- Ctrl+C 또는 창 닫기 시 정상 종료
- 전송 전에 모든 이미지를 exe 폴더의 image 폴더에 저장 (파일명: YYYYMMDD_HHMMSS_Monitor1.jpg, ..._Camera.jpg)
  * 기본은 자동 삭제 없음 (30초 간격, 모니터+카메라면 하루 약 3GB). KEEP_DAYS=N (N일 지난 이미지 삭제), KEEP_MAX_FILES=N (최대 N개, 오래된 것부터 삭제) 로 자동 정리 가능. 이 프로그램이 만든 파일명(YYYYMMDD_HHMMSS_*.jpg/png)만 삭제함
- 이미지가 10MB 를 넘으면 품질/해상도를 단계적으로 낮춰 전송. 가로+세로 10000px 초과 또는 비율 20:1 초과이면 원본을 문서(sendDocument)로 전송
- CAMERA_KEEP_OPEN=TRUE 이면 카메라를 계속 열어 둠 (지연/LED 깜빡임 감소, 대신 다른 앱이 카메라를 쓸 수 없음. 기본 FALSE)
- SAVE_ONLY=TRUE 이면 이미지를 텔레그램으로 보내지 않고 image 폴더에 저장만 함. 명령 수신(폴링)은 계속 동작하며 /shot 만 전송 (TOKEN/CHAT_ID 가 없으면 명령 수신 없이 저장만)
- 텔레그램 명령 수신 (TELEGRAM_COMMANDS=TRUE): 봇에게 /shot(화면+카메라 즉시 캡처·전송) /screen(화면만 즉시 캡처·전송) /photo(카메라만 즉시 캡처·전송) /pause(이미지 전송 중단, 저장은 계속) /resume(전송 재개) /status(상태) /detectOn(침입 탐지 켜기) /detectOff(침입 탐지 끄기) /INTERVAL=nnn(캡처·전송 간격을 nnn초로 변경, 5~86400. 즉시 적용되고 INI 의 TELEGRAM_INTERVAL 도 갱신됨. /INTERVAL 만 보내면 현재 값 표시) /version(프로그램 버전과 빌드 시각 표시) /ini 키=값(INI 에 새 키 추가, 아래 설명) /stop(종료, Yes/No 버튼으로 확인) /help 또는 //(도움말) 전송 (명령은 대소문자를 구분하지 않음).
  * /stop 을 보내면 "종료하시겠습니까?(y/n)" 메시지에 Yes/No 버튼이 붙어 옴. Yes 를 눌러야 종료, No 는 취소. 버튼은 명령과 같은 chat/허용 사용자만 누를 수 있고, 5분이 지났거나 이미 처리한 메시지, 프로그램 재시작 전의 메시지 버튼은 무효(종료되지 않음) TELEGRAM_CHAT_ID 로 설정한 chat 의 명령만 처리. 명령은 별도 스레드가 받으므로 캡처/전송 중에도 /pause /resume /status /stop 은 수 초 내 반응 (/shot 은 진행 중인 캡처가 끝난 뒤 실행)
  * TELEGRAM_CHAT_ID 가 그룹(음수)이면 TELEGRAM_ALLOWED_USER_IDS=111,222 (명령 허용 사용자 ID) 를 반드시 설정해야 명령이 동작. 개인 chat 이면 생략 가능
  * 같은 봇 토큰을 다른 프로그램이 getUpdates/webhook 으로 쓰면 409 충돌이 나므로, 이 프로그램 전용 봇을 쓰는 것을 권장
- 보안: screen_telegram.ini 에 봇 토큰이 평문으로 들어 있으므로 공유/업로드 금지 (노출 시 BotFather 에서 /revoke)
- 침입 탐지 (CAMERA=TRUE 필요): INI 스위치가 없고 텔레그램 명령 /detectOn(켜기) /detectOff(끄기) 로 제어. 켜면 카메라를 상시 열어 INTRUSION_CHECK_MS(기본 1000ms)마다 프레임을 검사, 끄면 카메라를 해제.
  * INI 에 AUTO_DETECT=TRUE 이면 프로그램 시작 때 /detectOn 을 자동 수행 (시작 인사말에도 "침입 탐지: 켜짐"으로 표시)
  * AUTO_DETECT 가 FALSE/없음이면 exe 폴더의 detect.state 파일(ON/OFF)에 저장된 마지막 상태를 따름. 파일이 없으면 꺼짐
  * 움직임이 확정되면 "[침입탐지 : yyyy년 mm월 dd일 HH시 mm분 ss초]" 메시지를 보낸 뒤 카메라 이미지만 전송 (모니터 화면 캡처는 하지 않음. SAVE_ONLY, /pause 와 무관하게 무조건 전송)
  * 방식: 그레이스케일+블러 -> 이동평균 배경 차분 -> 픽셀 임계값(INTRUSION_PIXEL_THRESHOLD=25) + 변화 면적(INTRUSION_AREA_PERCENT=1.0%) -> 연속 프레임 확인(INTRUSION_CONFIRM_FRAMES=2) -> 쿨다운(INTRUSION_COOLDOWN=60초)
  * 조명 켬/끔·자동 노출 변화(화면 60% 이상 변화)와 시작 직후 안정화 구간은 탐지하지 않음
  * 침입 탐지는 완전히 별도의 스레드로 동작: 주기 캡처, /pause, /shot, 텔레그램 전송 재시도 등과 무관하게 계속 프레임을 검사하고, 침입 확정 시 그 스레드가 직접 메시지 + 카메라 이미지(탐지된 바로 그 프레임) 저장·전송을 수행. 침입 이미지 파일명은 YYYYMMDD_HHMMSS_Intrusion_Camera.jpg
  * 탐지 중에는 카메라를 이 스레드가 전담(주기 캡처와 /photo 는 이 스레드의 최신 프레임을 사용)하므로 LED 가 항상 켜져 있고 다른 앱(화상회의 등)이 카메라를 쓸 수 없음
  * MAX_LOOP 횟수를 채운 시점에 침입 탐지가 켜져 있으면 프로그램이 종료되지 않고 계속 동작 (/stop 또는 Ctrl+C 로 종료)
  * 오탐이 많으면 AREA_PERCENT/PIXEL_THRESHOLD 를 올리고, 놓치면 낮춤
- 시작 인사말: 준비가 끝나면 텔레그램으로 인사말(간격/카메라/침입 탐지 상태 포함)을 한 번 전송. TELEGRAM_GREETING=FALSE 로 끌 수 있음
- MAX_LOOP=N 이면 N회 캡처/전송 후 자동 종료 (0 또는 생략 시 무제한)
- 버전: 1.0.0.0 (exe 속성 > 자세히 탭, 시작 인사말, /status, 로그에 표시)
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
