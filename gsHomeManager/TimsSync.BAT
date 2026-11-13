rem 타임서버 동기화 , 관리자 권한

chcp  65001
powershell -NoProfile -Command "Start-Process cmd -ArgumentList '/c net start w32time & w32tm /resync /force' -Verb RunAs -Wait"

exit /b 0