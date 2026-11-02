기능 :
모니터와 웹캠의 캡쳐 이미지를 텔레그램 사용자에게 주기적으로 전송

C:\tools\screen_telegram>dir /b
booting_Alert.ini   ....... exe 실행에 필요한 파라메터
screen_telegram.cpp  ...... 이 소스
build.bat  ............. 컴파일 & 링크
screen_telegram.exe  ..... 
screen_telegram_RUN.BAT ..... exe를 구동하는 명령어 파일
log.txt  ............... 부팅 정보 + 실행 로그 (BAT 실행 시마다 새로 작성)
ReadMe.MD .............  이 안내문
screen_telegram.cpp.001 ... 수정전 소스

참고 :
- 이미지는 JPEG(품질 85)로 전송. 전송 실패(네트워크/5xx/429) 시 최대 3회 시도
- 모니터 번호는 "주 모니터=1, 나머지는 왼쪽->오른쪽" 순이며, Windows 디스플레이 설정의 번호와 다를 수 있음
- 카메라가 여러 개면 booting_Alert.ini 의 CAMERA_INDEX 로 선택 (목록은 최초 캡처 시 log.txt 에 출력)
- 잠금 화면/로그온 전에는 화면 캡처를 건너뜀 (카메라는 계속 시도)
- 서비스(세션 0)로는 화면 캡처 불가. 사용자 로그온 후 시작프로그램/작업 스케줄러(로그온 시)로 실행
- Ctrl+C 또는 창 닫기 시 정상 종료
- 전송 전에 모든 이미지를 exe 폴더의 image 폴더에 저장 (파일명: YYYYMMDD_HHMMSS_Monitor1.jpg, ..._Camera.jpg). 자동 삭제는 없으므로 주기적으로 정리 필요
- SAVE_ONLY=TRUE 이면 텔레그램 전송을 생략하고 image 폴더에 저장만 함 (TOKEN/CHAT_ID 없어도 실행)
- MAX_LOOP=N 이면 N회 캡처/전송 후 자동 종료 (0 또는 생략 시 무제한)
