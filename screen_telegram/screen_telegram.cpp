// screen_telegram.cpp
// Windows 11 / MinGW-w64 g++
//
// screen_telegram.ini 의 설정:
//   TELEGRAM_BOT_TOKEN=xxxxxxxxxx
//   TELEGRAM_CHAT_ID=yyyyyyy
//   TELEGRAM_INTERVAL=60
//   MONITOR1=TRUE          (모니터1 캡처 여부, 미기재 시 기본 TRUE)
//   MONITOR2=TRUE
//   MONITOR3=FALSE
//   CAMERA=TRUE            (웹캠 캡처 여부, 기본 FALSE)
//   CAMERA_INDEX=0         (사용할 카메라 번호, 0부터. 기본 0. 목록은 로그에 출력)
//   MAX_LOOP=9             (캡처/전송 반복 횟수. 이 횟수만큼 실행 후 종료. 0 또는 미기재 시 무제한)
//   SAVE_ONLY=TRUE         (TRUE 이면 이미지를 텔레그램으로 보내지 않고 image 폴더에 저장만. 기본 FALSE.
//                           명령 수신(/shot 등)은 계속 동작하며 /shot 만 전송한다.
//                           TOKEN/CHAT_ID 가 없으면 명령 수신 없이 저장만 한다)
//   TELEGRAM_COMMANDS=TRUE (TRUE 이면 텔레그램 봇 명령 수신: /shot /pause /resume /status /detectOn /detectOff /stop /help (// 는 /help 와 동일). 기본 TRUE.
//                           /stop 은 바로 종료하지 않고 Yes/No 버튼으로 확인한다.
//                           TELEGRAM_CHAT_ID 에서 온 명령만 처리. SAVE_ONLY=TRUE 여도 동작)
//   TELEGRAM_ALLOWED_USER_IDS=111,222  (명령을 내릴 수 있는 텔레그램 사용자 ID 목록. 미기재 시 개인 chat 만 허용.
//                           TELEGRAM_CHAT_ID 가 그룹(음수)이면 이 목록이 없을 때 명령을 모두 거부한다)
//   KEEP_DAYS=7            (image 폴더에서 N일 지난 이미지 자동 삭제. 0 또는 미기재 시 삭제 안 함)
//   KEEP_MAX_FILES=5000    (image 폴더 이미지 최대 개수. 초과분은 오래된 것부터 삭제. 0 또는 미기재 시 제한 없음)
//   PREVIOUS_SESSION_CLEAR=TRUE  (TRUE 이면 프로그램 시작할 때 log.txt 와 image 폴더의 모든 파일을 삭제. 기본 FALSE)
//   TELEGRAM_GREETING=TRUE (시작 준비가 끝나면 텔레그램으로 인사말을 한 번 보냄. 기본 TRUE)
//   AUTO_DETECT=TRUE       (프로그램 시작 때 텔레그램 /detectOn 과 동일하게 침입 탐지를 자동으로 켠다. 기본 FALSE.
//                           FALSE 이면 detect.state 에 저장된 마지막 상태(켜짐/꺼짐)를 따른다. CAMERA=TRUE 필요)
//   (텔레그램 /CMD 명령 은 /CMD 뒤의 문자열 전체를 PC 의 cmd.exe 에서 실행하고 출력(stdout+stderr)을 .txt 문서로 전송한다.
//    실행 시간 제한 60초(초과 시 하위 프로세스까지 종료), 출력 20MB 초과분은 잘림, 표준 입력은 NUL.)
//   UPLOAD_DIR=upload              (폰 -> PC 파일 수신 저장 폴더. 상대 경로는 exe 폴더 기준, 기본 upload. 폴더는 한 단계만 자동 생성)
//   UPLOAD_SHOW_IMAGE=TRUE         (폰에서 받은 이미지를 PC 화면에 장당 별개의 창으로 띄움. 기본 TRUE. jpg/png/bmp/gif/tif)
//   UPLOAD_PLAY_MEDIA=TRUE         (폰에서 받은 .mp4 / .mp3 파일을 저장 직후 PC 의 기본 플레이어로 실행. 기본 TRUE. 예전 이름 UPLOAD_PLAY_VIDEO 도 인정)
//   (폰에서 봇 채팅에 파일/사진을 보내면 허용된 chat/사용자에 한해 PC 의 UPLOAD_DIR 에 자동 저장하고 결과를 알려 준다.
//    봇 API 한도로 파일 1개당 20MB 까지. 같은 이름이 있으면 _1, _2 를 붙이고 덮어쓰지 않으며, 저장만 하고 실행하지 않는다.)
//   (텔레그램 /HomeBack 은 귀가 인사 "[집에 돌아 오신 것을 환영합니다. 푹 쉬세요]" 를 보낸다. 인사 뒤에 /detectOff 를 자동 처리한다.)
//   (텔레그램 /NotHome 은 외출 인사 "안녕히 다녀오세요. 제가 집을 잘 지키고 있겠습니다." 를 보낸다. 인사 뒤에 /detectOn 을 자동 처리한다.)
//   (텔레그램 /download 는 PC 에 파일 탐색기(열기 창)를 띄워 사용자가 고른 여러 파일을 텔레그램으로 보낸다.
//    창은 5분 안에 고르지 않으면 취소되고, 파일 1개당 49MB 까지, 빈 파일은 제외한다.)
//   (텔레그램 /version 은 버전을, /ini 키=값 은 INI 끝에 새 키를 추가한다. 이미 있는 키는 경고만 하고 바꾸지 않으며,
//    추가한 값은 프로그램을 다시 시작해야 적용된다.)
//   (캡처/전송 간격 TELEGRAM_INTERVAL 은 텔레그램 명령 /INTERVAL=nnn (5~86400초) 로 바꿀 수 있다.
//    바로 적용되고 이 INI 의 TELEGRAM_INTERVAL 값도 갱신된다. /INTERVAL 만 보내면 현재 값을 알려 준다.)
//   (침입 탐지 켜기/끄기는 텔레그램 명령 /detectOn /detectOff 로 한다.
//    카메라 침입 탐지(별도 스레드로 독립 동작): 모션 감지 시 "[침입탐지 : yyyy년 mm월 dd일 HH시 mm분 ss초]" 메시지를 보내고
//    카메라 이미지만 전송 (모니터 화면은 제외. SAVE_ONLY, /pause 와 무관하게 무조건 전송). CAMERA=TRUE 필요,
//    켜져 있는 동안 카메라를 상시 열어 두므로 다른 앱은 카메라를 쓸 수 없음.
//    켜짐/꺼짐 상태는 exe 폴더의 detect.state 파일에 저장되어 재시작 후에도 유지된다. 파일이 없으면 꺼짐)
//   INTRUSION_PIXEL_THRESHOLD=25   (픽셀 밝기 변화 임계값 1~255, 기본 25)
//   INTRUSION_AREA_PERCENT=1.0     (변화 픽셀이 화면의 몇 % 이상이면 움직임으로 판단, 기본 1.0)
//   INTRUSION_CONFIRM_FRAMES=2     (연속 몇 프레임 움직임이 있어야 침입으로 확정, 기본 2)
//   INTRUSION_COOLDOWN=60          (한 번 탐지 후 재탐지까지 최소 대기 초, 기본 60)
//   INTRUSION_CHECK_MS=1000        (카메라 프레임 검사 간격 ms, 200~10000, 기본 1000)
//   CAMERA_KEEP_OPEN=FALSE (TRUE 이면 카메라를 계속 열어 둠: 지연/LED 깜빡임은 줄지만 다른 앱이 카메라를 못 씀. 기본 FALSE)
//
// 주기적으로 모니터별 화면(및 설정 시 웹캠)을 캡처하여 텔레그램으로 전송한다.
//
// ---- 클래스 구성 ----
//   Logger          시각이 붙은 로그 출력
//   StrUtil         문자열 유틸 (trim, bool 파싱, UTF-8 <-> UTF-16)
//   SysUtil         시스템 유틸 (파일 존재, exe 폴더, 호스트명, 시각 문자열)
//   StopSignal      종료 요청 (Ctrl+C / 콘솔 닫기 / 로그오프) 이벤트
//   ComScope        COM 초기화/해제 (RAII)
//   ScopedBitmap    HBITMAP 소유권 관리 (RAII)
//   Config          screen_telegram.ini 설정
//   ImageEncoder    GDI+ 세션 + PNG/JPEG 인코딩
//   TelegramClient  텔레그램 sendPhoto / sendMessage / getUpdates (WinHTTP, 연결 재사용) + 재시도
//   CommandListener 텔레그램 봇 명령 수신 (getUpdates, 허용된 chat 만)
//   ImageWindow     폰에서 받은 이미지를 보여 주는 창 (이미지 1장 = 창 1개 = 스레드 1개)
//   Shot            캡처 결과 1장 (라벨 + 비트맵)
//   ICaptureSource  캡처 소스 인터페이스
//   MonitorCapture  모니터 화면 캡처 (ICaptureSource)
//   CameraCapture   웹캠 캡처, Media Foundation (ICaptureSource)
//   ScreenTelegramApp  전체 흐름 (설정 로드 -> 주기 반복 캡처 -> image 폴더 저장 -> 전송)
//
// 빌드:
//   g++ -O2 -std=c++17 -mwindows -o screen_telegram.exe screen_telegram.cpp ^
//       -lgdiplus -lgdi32 -luser32 -lole32 -loleaut32 -lwinhttp -lmfuuid ^
//       -static -static-libgcc -static-libstdc++
//   (-mwindows: 콘솔 창 없는 GUI 서브시스템 + WinMain 시작. 종료는 텔레그램 /stop 또는 작업 관리자)
//
// 주의: mfplat.dll / mfreadwrite.dll (카메라용) 은 빌드 시점에 링크하지 않고,
// 실행 중 LoadLibrary 로 동적으로 찾는다. 해당 DLL이 없는 환경(Windows N/KN 등)
// 에서도 exe 자체는 정상적으로 시작되며, 카메라 기능만 자동으로 비활성화된다.
// -lmfuuid 는 GUID 상수만 담긴 순수 정적 라이브러리이므로 DLL 의존성이 없다.
//
// 실행:
//   screen_telegram.exe [ini파일경로]
//   (인자가 없으면 exe 폴더 -> 현재 폴더의 screen_telegram.ini 를 찾음)

#include <windows.h>
#include <objidl.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <winhttp.h>
#include <shellapi.h>
#include <commdlg.h>   // GetOpenFileNameW (/download 파일 선택 창, -lcomdlg32)

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cctype>
#include <cmath>
#include <cwctype>
#include <cstdarg>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "version.h"   // APP_VERSION_STR 등 (exe 속성 리소스 screen_telegram.rc 와 공용)

// 복사 금지 선언용 매크로
#define NO_COPY(T)               \
    T(const T&) = delete;        \
    T& operator=(const T&) = delete

// CRITICAL_SECTION 을 잡았다가 스코프를 벗어나면 풀어 주는 RAII 락
struct CsLock {
    CRITICAL_SECTION* cs;
    explicit CsLock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~CsLock() { LeaveCriticalSection(cs); }
    CsLock(const CsLock&) = delete;
    CsLock& operator=(const CsLock&) = delete;
};

// ================================================================ Logger
class Logger {
public:
    // exe 폴더의 log.txt 에 한 줄씩 덧붙인다 (UTF-8, CRLF). 콘솔이 없는 -mwindows 빌드에서도 기록된다.
    //   형식: [2026-11-04 10:40:12.345] [T1234] 메시지      (T 뒤는 스레드 ID: 메인/명령/침입 탐지 스레드 구분용)
    // 여러 스레드가 동시에 호출해도 안전하다. 호출할 때마다 파일을 열고 닫으므로 BAT 이 같은 파일에 쓰거나
    // 사용자가 메모장으로 열어 둔 상태에서도 동작하고, 비정상 종료 직전의 기록도 남는다.
    // 파일이 5MB 를 넘으면 log.old.txt 로 넘기고 새로 시작한다. 파일에 쓰지 못하면 OutputDebugString 으로만 출력한다.
    static void log(const char* fmt, ...)
    {
        char msg[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        write(msg, false);
    }

    // 치명적 오류용: 다른 스레드가 로그 잠금을 쥔 채 죽었어도 멈추지 않도록 잠금을 기다리지 않고 기록한다.
    static void logCrash(const char* fmt, ...)
    {
        char msg[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        write(msg, true);
    }

    // log.txt 를 지운다 (PREVIOUS_SESSION_CLEAR 용). 로그 잠금을 잡고 삭제하며, 삭제가 막히면(다른 프로그램이 열어 둠) 내용만 비운다.
    // log.old.txt 는 건드리지 않는다. 지웠으면 true.
    static bool clearFile()
    {
        CsLock lock(&guard().cs);
        if (DeleteFileW(logPath(false).c_str())) return true;
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        HANDLE h = CreateFileW(logPath(false).c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
    }

    // /log 명령용: log.txt 의 마지막 n 줄을 UTF-8 문자열로 돌려준다 (파일이 없으면 빈 문자열).
    // 로그를 쓰는 중에 호출해도 안전하게 읽는다(공유 읽기). log.old.txt 는 보지 않고 현재 log.txt 만 본다.
    static std::string tailLines(int n)
    {
        const std::string content = readWhole(logPath(false));
        if (content.empty() || n <= 0) return content;
        std::vector<size_t> starts;
        starts.push_back(0);
        for (size_t i = 0; i + 1 < content.size(); ++i)
            if (content[i] == '\n') starts.push_back(i + 1);
        const size_t from = starts.size() > (size_t)n ? starts[starts.size() - (size_t)n] : 0;
        return content.substr(from);
    }

private:
    static std::string readWhole(const std::wstring& path)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return std::string();
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) { CloseHandle(h); return std::string(); }
        std::string out;
        out.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        BOOL ok = ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr);
        CloseHandle(h);
        if (!ok) return std::string();
        out.resize(rd);
        return out;
    }

    static constexpr ULONGLONG kMaxBytes = 5ULL * 1024 * 1024;

    struct Guard { // 정적 CRITICAL_SECTION (첫 호출 시 한 번만 초기화됨, C++11 스레드 안전)
        CRITICAL_SECTION cs;
        Guard() { InitializeCriticalSection(&cs); }
    };

    static Guard& guard()
    {
        static Guard g;
        return g;
    }

    static const std::wstring& logPath(bool old)
    {
        static const std::wstring paths[2] = { makePath(L"log.txt"), makePath(L"log.old.txt") };
        return paths[old ? 1 : 0];
    }

    static std::wstring makePath(const wchar_t* name)
    {
        wchar_t buf[MAX_PATH * 4];
        DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        std::wstring dir(buf, n);
        size_t slash = dir.find_last_of(L"\\/");
        dir = slash == std::wstring::npos ? L"." : dir.substr(0, slash);
        return dir + L"\\" + name;
    }

    static void write(const char* msg, bool crash)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char head[64];
        snprintf(head, sizeof(head), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [T%lu] ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 (unsigned long)GetCurrentThreadId());

        std::string line = head;
        for (const char* c = msg; *c; ++c) // 한 항목은 항상 한 줄 (응답 본문의 줄바꿈 제거)
            line += (*c == '\r' || *c == '\n') ? ' ' : *c;
        line += "\r\n";

        OutputDebugStringA(line.c_str()); // 디버거(DebugView 등)로도 볼 수 있음

        Guard& g = guard();
        bool locked = false;
        if (crash) locked = TryEnterCriticalSection(&g.cs) != 0;
        else { EnterCriticalSection(&g.cs); locked = true; }

        appendToFile(line);

        if (locked) LeaveCriticalSection(&g.cs);
    }

    static HANDLE openForAppend()
    {
        return CreateFileW(logPath(false).c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    static void appendToFile(const std::string& line)
    {
        HANDLE h = openForAppend();
        if (h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz;
        if (GetFileSizeEx(h, &sz) && (ULONGLONG)sz.QuadPart >= kMaxBytes) {
            CloseHandle(h);
            MoveFileExW(logPath(false).c_str(), logPath(true).c_str(), MOVEFILE_REPLACE_EXISTING);
            h = openForAppend();
            if (h == INVALID_HANDLE_VALUE) return;
        }
        DWORD wr = 0;
        WriteFile(h, line.data(), (DWORD)line.size(), &wr, nullptr);
        CloseHandle(h);
    }
};

// ================================================================ StrUtil
class StrUtil {
public:
    static std::string trim(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && (unsigned char)s[b] <= ' ') ++b;
        while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
        return s.substr(b, e - b);
    }

    static std::string upper(std::string s)
    {
        for (auto& c : s) c = (char)toupper((unsigned char)c);
        return s;
    }

    static bool parseBool(const std::string& v, bool defVal)
    {
        std::string s = upper(v);
        if (s == "TRUE" || s == "1" || s == "YES" || s == "Y" || s == "ON") return true;
        if (s == "FALSE" || s == "0" || s == "NO" || s == "N" || s == "OFF") return false;
        return defVal;
    }

    static std::wstring toWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    // len 문자 길이의 UTF-16 문자열(널 종료 불필요)을 UTF-8 로 변환. len=-1 이면 널 종료로 간주.
    static std::string toUtf8(const wchar_t* w, int len = -1)
    {
        int m = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
        if (m <= 0) return std::string();
        std::string s(m, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], m, nullptr, nullptr);
        if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }
};

// ================================================================ SysUtil
class SysUtil {
public:
    static bool fileExists(const std::string& p)
    {
        DWORD a = GetFileAttributesW(StrUtil::toWide(p).c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    static std::string exeDir()
    {
        std::vector<wchar_t> buf(32768);
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        std::string p = StrUtil::toUtf8(buf.data(), (int)n);
        size_t pos = p.find_last_of("\\/");
        return pos == std::string::npos ? "." : p.substr(0, pos);
    }

    // 컴퓨터 이름을 UTF-8 로 반환 (한글 이름 대비)
    static std::string hostName()
    {
        wchar_t w[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetComputerNameW(w, &n) || n == 0) return "PC";
        std::string s = StrUtil::toUtf8(w, (int)n);
        return s.empty() ? std::string("PC") : s;
    }

    // 파일명용 시각 (예: 20261102_142400)
    static std::string fileStamp()
    {
        char tbuf[32];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", &tmv);
        return tbuf;
    }

    // 폴더가 없으면 생성. 이미 있거나 생성에 성공하면 true.
    static bool ensureDir(const std::string& dir)
    {
        if (CreateDirectoryW(StrUtil::toWide(dir).c_str(), nullptr)) return true;
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }

    static bool writeFile(const std::string& path, const std::vector<BYTE>& data)
    {
        HANDLE h = CreateFileW(StrUtil::toWide(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool ok = true;
        size_t off = 0;
        while (off < data.size()) {
            DWORD chunk = (DWORD)std::min<size_t>(data.size() - off, (size_t)1 << 24);
            DWORD wr = 0;
            if (!WriteFile(h, data.data() + off, chunk, &wr, nullptr) || wr == 0) { ok = false; break; }
            off += wr;
        }
        CloseHandle(h);
        return ok;
    }

    // INI 파일에서 key(대문자로 전달, 비교는 대소문자 무시)의 값을 newVal 로 바꾼다.
    // 다른 줄, 줄바꿈(CRLF/LF), BOM, 값 뒤의 인라인 주석은 그대로 유지한다.
    // 키가 없으면 파일 끝에 "KEY=값" 줄을 추가한다. 임시 파일에 먼저 쓴 뒤 교체하므로 쓰다 실패해도 원본은 보존된다.
    static bool updateIniValue(const std::string& path, const std::string& key, const std::string& newVal)
    {
        std::string content;
        if (!readFile(path, content)) return false;
        const std::string eol = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";

        std::string out;
        out.reserve(content.size() + 64);
        bool replaced = false;
        size_t pos = 0;
        while (pos < content.size()) {
            size_t nl = content.find('\n', pos);
            size_t lineEnd = nl == std::string::npos ? content.size() : nl + 1;
            std::string line = content.substr(pos, lineEnd - pos);
            pos = lineEnd;

            if (!replaced) {
                size_t bodyLen = line.size();
                while (bodyLen > 0 && (line[bodyLen - 1] == '\n' || line[bodyLen - 1] == '\r')) --bodyLen;
                const std::string body = line.substr(0, bodyLen);
                const std::string term = line.substr(bodyLen);

                size_t b = 0;
                while (b < body.size() && (unsigned char)body[b] <= ' ') ++b;
                if (b < body.size() && body[b] != ';' && body[b] != '#' && body[b] != '[') {
                    size_t eq = body.find('=', b);
                    if (eq != std::string::npos &&
                        StrUtil::upper(StrUtil::trim(body.substr(b, eq - b))) == key) {
                        // 값 뒤 인라인 주석("공백 + ; 또는 #")은 유지 (Config::load 와 같은 규칙)
                        size_t cmt = std::string::npos;
                        for (size_t i = eq + 2; i < body.size(); ++i) {
                            if ((body[i] == ';' || body[i] == '#') && (unsigned char)body[i - 1] <= ' ') {
                                cmt = i;
                                break;
                            }
                        }
                        std::string tail;
                        if (cmt != std::string::npos) {
                            size_t ws = cmt;
                            while (ws > eq + 1 && (unsigned char)body[ws - 1] <= ' ') --ws;
                            tail = body.substr(ws);
                        }
                        line = body.substr(0, eq + 1) + newVal + tail + term;
                        replaced = true;
                    }
                }
            }
            out += line;
        }
        if (!replaced) {
            if (!out.empty() && out.back() != '\n') out += eol;
            out += key + "=" + newVal + eol;
        }

        return replaceFileAtomic(path, out);
    }

    // 임시 파일에 먼저 쓴 뒤 교체한다. 쓰다 실패해도 원본은 보존된다.
    static bool replaceFileAtomic(const std::string& path, const std::string& content)
    {
        const std::string tmp = path + ".tmp";
        if (!writeFile(tmp, std::vector<BYTE>(content.begin(), content.end()))) {
            DeleteFileW(StrUtil::toWide(tmp).c_str());
            return false;
        }
        if (!MoveFileExW(StrUtil::toWide(tmp).c_str(), StrUtil::toWide(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(StrUtil::toWide(tmp).c_str());
            return false;
        }
        return true;
    }

    // INI 내용에 key(대문자로 전달)가 '활성' 줄(주석 ; # 이나 [섹션] 이 아닌 줄)로 이미 있는지.
    // Config::load 와 같은 규칙이며 대소문자는 무시한다. 주석 처리된 줄(;KEY=..)은 없는 것으로 본다.
    static bool iniHasKey(const std::string& content, const std::string& key)
    {
        std::istringstream f(content);
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (first) { // UTF-8 BOM
                first = false;
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                    (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                    line.erase(0, 3);
            }
            line = StrUtil::trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (StrUtil::upper(StrUtil::trim(line.substr(0, eq))) == StrUtil::upper(key)) return true;
        }
        return false;
    }

    enum class IniAdd { Added, Exists, Error };

    // INI 끝에 "KEY=값" 줄을 추가한다. 같은 키가 이미 있으면 아무것도 바꾸지 않고 Exists 를 돌려준다.
    static IniAdd addIniKey(const std::string& path, const std::string& key, const std::string& val)
    {
        std::string content;
        if (!readFile(path, content)) return IniAdd::Error;
        if (iniHasKey(content, key)) return IniAdd::Exists;
        const std::string eol = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        if (!content.empty() && content.back() != '\n') content += eol;
        content += key + "=" + val + eol;
        return replaceFileAtomic(path, content) ? IniAdd::Added : IniAdd::Error;
    }

    // 파일 전체를 읽는다 (경로는 UTF-8). 열 수 없으면 false.
    static bool readFile(const std::string& path, std::string& out)
    {
        out.clear();
        HANDLE h = CreateFileW(StrUtil::toWide(path).c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char buf[65536];
        DWORD rd = 0;
        while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) out.append(buf, rd);
        CloseHandle(h);
        return true;
    }

    // time_t -> "YYYYMMDD_HHMMSS"
    static std::string stampOf(time_t t)
    {
        char tbuf[32];
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", &tmv);
        return tbuf;
    }

    // "YYYYMMDD_HHMMSS_태그.jpg|png" 형식의 이 프로그램이 만든 파일인지 확인 (다른 파일은 건드리지 않음)
    static bool isOurImageName(const std::wstring& n)
    {
        if (n.size() < 20) return false;
        for (int i = 0; i < 15; ++i) {
            if (i == 8) { if (n[i] != L'_') return false; }
            else if (!iswdigit(n[i])) return false;
        }
        if (n[15] != L'_') return false;
        std::wstring ext = n.substr(n.size() - 4);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext == L".jpg" || ext == L".png";
    }

    // image 폴더 안의 모든 파일(하위 폴더는 제외)을 삭제하고 지운 개수를 돌려준다. failed 에 삭제 실패 개수. (PREVIOUS_SESSION_CLEAR 용)
    static size_t clearFolderFiles(const std::string& dir, size_t& failed)
    {
        failed = 0;
        const std::wstring wdir = StrUtil::toWide(dir);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((wdir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return 0; // 폴더가 없으면 지울 것도 없음
        size_t deleted = 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring path = wdir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(path.c_str())) ++deleted; else ++failed;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return deleted;
    }

    // image 폴더 정리: keepDays 일보다 오래된 파일, 그리고 keepMax 개를 넘는 오래된 파일을 삭제.
    // (파일명의 시각 순서로 오래됨을 판단한다. 둘 다 0 이면 아무것도 하지 않음)
    static void cleanupImages(const std::string& dir, int keepDays, int keepMax)
    {
        if (keepDays <= 0 && keepMax <= 0) return;
        const std::wstring wdir = StrUtil::toWide(dir);

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((wdir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        std::vector<std::wstring> names;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && isOurImageName(fd.cFileName))
                names.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(names.begin(), names.end()); // 이름이 시각순이므로 오름차순 = 오래된 순

        size_t first = 0; // names[0..first) 삭제 대상
        if (keepDays > 0) {
            const std::wstring cutoff =
                StrUtil::toWide(stampOf(time(nullptr) - (time_t)keepDays * 86400));
            while (first < names.size() && names[first].compare(0, 15, cutoff) < 0) ++first;
        }
        if (keepMax > 0 && names.size() - first > (size_t)keepMax)
            first = names.size() - (size_t)keepMax;

        size_t deleted = 0;
        for (size_t i = 0; i < first; ++i)
            if (DeleteFileW((wdir + L"\\" + names[i]).c_str())) ++deleted;
        if (deleted > 0)
            Logger::log("image 폴더 정리: 오래된 이미지 %zu개 삭제 (남은 %zu개)", deleted,
                        names.size() - deleted);
    }

    static std::string timestamp()
    {
        char tbuf[32];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        return tbuf;
    }
};

// ================================================================ StopSignal
// 종료 요청 이벤트. Ctrl+C / 콘솔 닫기 / 로그오프 / 종료 시 설정된다.
class StopSignal {
public:
    StopSignal() { event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~StopSignal()
    {
        if (s_instance == this) {
            SetConsoleCtrlHandler(handler, FALSE);
            s_instance = nullptr;
        }
        if (event_) CloseHandle(event_);
    }
    NO_COPY(StopSignal);

    void install()
    {
        s_instance = this;
        SetConsoleCtrlHandler(handler, TRUE);
    }

    void request() { if (event_) SetEvent(event_); }

    HANDLE handle() const { return event_; }

    bool requested() const
    {
        return event_ && WaitForSingleObject(event_, 0) == WAIT_OBJECT_0;
    }

    // ms 동안 대기. 종료 요청이 오면 즉시 true 반환, 시간이 다 되면 false.
    bool wait(DWORD ms) const
    {
        return event_ && WaitForSingleObject(event_, ms) == WAIT_OBJECT_0;
    }

private:
    static BOOL WINAPI handler(DWORD)
    {
        if (s_instance) s_instance->request();
        return TRUE;
    }

    HANDLE event_ = nullptr;
    static StopSignal* s_instance;
};
StopSignal* StopSignal::s_instance = nullptr;

// ================================================================ ComScope
// STA 로 초기화: 일부 구형 웹캠 드라이버(DirectShow 래핑)는 MTA 에서
// MFEnumDeviceSources 가 장치를 하나도 찾지 못하는 경우가 있음.
class ComScope {
public:
    ComScope() : ok_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() { if (ok_) CoUninitialize(); }
    NO_COPY(ComScope);

private:
    bool ok_;
};

// ================================================================ ScopedBitmap
class ScopedBitmap {
public:
    ScopedBitmap() = default;
    explicit ScopedBitmap(HBITMAP h) : h_(h) {}
    ~ScopedBitmap() { reset(); }
    NO_COPY(ScopedBitmap);

    ScopedBitmap(ScopedBitmap&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    ScopedBitmap& operator=(ScopedBitmap&& o) noexcept
    {
        if (this != &o) {
            reset();
            h_ = o.h_;
            o.h_ = nullptr;
        }
        return *this;
    }

    HBITMAP get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

    void reset()
    {
        if (h_) DeleteObject(h_);
        h_ = nullptr;
    }

private:
    HBITMAP h_ = nullptr;
};

// ================================================================ Config
class Config {
public:
    std::string token;
    std::string chatId;
    volatile int interval = 60; // 명령 스레드(/INTERVAL=nnn)가 런타임에 바꾸고 메인 스레드가 읽음

    // MONITOR1=TRUE / MONITOR2=TRUE / MONITOR3=FALSE ...
    // 명시되지 않은 모니터는 기본값(true, 즉 캡처함)으로 처리된다.
    std::map<int, bool> monitorEnabled;
    bool cameraEnabled = false; // CAMERA=TRUE
    int cameraIndex = 0;        // CAMERA_INDEX=0
    int maxLoop = 0;            // MAX_LOOP=9 (0 = 무제한)
    bool saveOnly = false;      // SAVE_ONLY=TRUE (전송 생략, 저장만)
    bool commands = true;       // TELEGRAM_COMMANDS=TRUE (봇 명령 수신)
    int keepDays = 0;           // KEEP_DAYS=7 (0 = 자동 삭제 안 함)
    int keepMax = 0;            // KEEP_MAX_FILES=5000 (0 = 제한 없음)
    bool previousSessionClear = false; // PREVIOUS_SESSION_CLEAR=TRUE (시작 때 log.txt 와 image 폴더 파일 전부 삭제)
    bool cameraKeepOpen = false; // CAMERA_KEEP_OPEN=TRUE (카메라를 계속 열어 둠)
    bool uploadPlayMedia = true; // UPLOAD_PLAY_MEDIA=TRUE (받은 .mp4/.mp3 를 PC 기본 플레이어로 실행)
    bool uploadShowImage = true; // UPLOAD_SHOW_IMAGE=TRUE (받은 이미지를 장당 별개의 창으로 표시)
    std::string uploadDir;       // UPLOAD_DIR=upload (폰 -> PC 파일 수신 폴더, 비어 있으면 exe 폴더\\upload)
    std::vector<std::string> allowedUsers; // TELEGRAM_ALLOWED_USER_IDS=111,222

    bool greeting = true;             // TELEGRAM_GREETING=TRUE (시작 인사말)

    // 침입 탐지 (카메라 모션 감지) - 켜기/끄기는 텔레그램 /detectOn /detectOff
    bool autoDetect = false;          // AUTO_DETECT=TRUE : 프로그램 시작 때 /detectOn 을 자동 수행
    int intrusionPixelThr = 25;       // INTRUSION_PIXEL_THRESHOLD
    double intrusionAreaPct = 1.0;    // INTRUSION_AREA_PERCENT
    int intrusionConfirm = 2;         // INTRUSION_CONFIRM_FRAMES
    int intrusionCooldown = 60;       // INTRUSION_COOLDOWN (초)
    int intrusionCheckMs = 1000;      // INTRUSION_CHECK_MS

    // 해당 모니터(1부터 시작하는 순번)가 캡처 대상인지 확인. 미지정 시 기본 true.
    bool isMonitorEnabled(int oneBasedIndex) const
    {
        auto it = monitorEnabled.find(oneBasedIndex);
        return it == monitorEnabled.end() ? true : it->second;
    }

    // INI 파일을 읽는다. 파일을 열 수 없으면 false.
    bool load(const std::string& path)
    {
        std::string content;
        if (!SysUtil::readFile(path, content)) return false;
        std::istringstream f(content);

        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            // UTF-8 BOM 제거
            if (first) {
                first = false;
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                    (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                    line.erase(0, 3);
            }
            line = StrUtil::trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = StrUtil::trim(line.substr(0, eq));
            std::string val = StrUtil::trim(line.substr(eq + 1));
            // 값 뒤 인라인 주석 제거: "공백 + ; 또는 #" (예: CAMERA=TRUE ; 메모)
            for (size_t i = 1; i < val.size(); ++i) {
                if ((val[i] == ';' || val[i] == '#') && (unsigned char)val[i - 1] <= ' ') {
                    val = StrUtil::trim(val.substr(0, i));
                    break;
                }
            }
            if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                    (val.front() == '\'' && val.back() == '\'')))
                val = val.substr(1, val.size() - 2);

            key = StrUtil::upper(key); // 대소문자 무시
            apply(key, val);
        }
        return true;
    }

    // 이 프로그램이 해석하는 키인지 (대문자로 전달). /ini 로 추가한 키의 오타 점검용.
    static bool isKnownKey(const std::string& key)
    {
        static const char* const names[] = {
            "TELEGRAM_BOT_TOKEN", "TELEGRAM_CHAT_ID", "TELEGRAM_INTERVAL", "TELEGRAM_COMMANDS",
            "TELEGRAM_ALLOWED_USER_IDS", "TELEGRAM_GREETING", "SAVE_ONLY", "MAX_LOOP",
            "CAMERA", "CAMERA_INDEX", "CAMERA_KEEP_OPEN", "KEEP_DAYS", "KEEP_MAX_FILES", "PREVIOUS_SESSION_CLEAR",
            "UPLOAD_DIR", "UPLOAD_SHOW_IMAGE", "UPLOAD_PLAY_MEDIA", "UPLOAD_PLAY_VIDEO", "AUTO_DETECT", "INTRUSION_PIXEL_THRESHOLD", "INTRUSION_AREA_PERCENT",
            "INTRUSION_CONFIRM_FRAMES", "INTRUSION_COOLDOWN", "INTRUSION_CHECK_MS"
        };
        for (const char* n : names)
            if (key == n) return true;
        if (key.rfind("MONITOR", 0) == 0) { // MONITOR1, MONITOR2, ...
            const std::string num = key.substr(7);
            return !num.empty() && num.find_first_not_of("0123456789") == std::string::npos;
        }
        return false;
    }

private:
    void apply(const std::string& key, const std::string& val)
    {
        if (key == "TELEGRAM_BOT_TOKEN") token = val;
        else if (key == "TELEGRAM_CHAT_ID") chatId = val;
        else if (key == "TELEGRAM_INTERVAL") {
            int v = atoi(val.c_str());
            if (v > 0) interval = v;
        }
        else if (key == "TELEGRAM_COMMANDS") {
            commands = StrUtil::parseBool(val, true);
        }
        else if (key == "SAVE_ONLY") {
            saveOnly = StrUtil::parseBool(val, false);
        }
        else if (key == "MAX_LOOP") {
            int v = atoi(val.c_str());
            if (v >= 0) maxLoop = v;
        }
        else if (key == "CAMERA_INDEX") {
            int v = atoi(val.c_str());
            if (v >= 0) cameraIndex = v;
        }
        else if (key == "CAMERA") {
            cameraEnabled = StrUtil::parseBool(val, false);
        }
        else if (key == "KEEP_DAYS") {
            int v = atoi(val.c_str());
            if (v >= 0) keepDays = v;
        }
        else if (key == "PREVIOUS_SESSION_CLEAR") {
            previousSessionClear = StrUtil::parseBool(val, false);
        }
        else if (key == "KEEP_MAX_FILES") {
            int v = atoi(val.c_str());
            if (v >= 0) keepMax = v;
        }
        else if (key == "CAMERA_KEEP_OPEN") {
            cameraKeepOpen = StrUtil::parseBool(val, false);
        }
        else if (key == "UPLOAD_PLAY_MEDIA" || key == "UPLOAD_PLAY_VIDEO") {
            uploadPlayMedia = StrUtil::parseBool(val, true);
        }
        else if (key == "UPLOAD_SHOW_IMAGE") {
            uploadShowImage = StrUtil::parseBool(val, true);
        }
        else if (key == "UPLOAD_DIR") {
            uploadDir = val;
        }
        else if (key == "TELEGRAM_ALLOWED_USER_IDS") {
            allowedUsers.clear();
            std::string cur;
            for (size_t i = 0; i <= val.size(); ++i) {
                char c = i < val.size() ? val[i] : ',';
                if (c == ',' || c == ';' || c == ' ' || c == '\t') {
                    if (!cur.empty()) allowedUsers.push_back(cur);
                    cur.clear();
                } else cur += c;
            }
        }
        else if (key == "TELEGRAM_GREETING") {
            greeting = StrUtil::parseBool(val, true);
        }
        else if (key == "AUTO_DETECT") {
            autoDetect = StrUtil::parseBool(val, false);
        }
        else if (key == "INTRUSION_PIXEL_THRESHOLD") {
            int v = atoi(val.c_str());
            if (v >= 1 && v <= 255) intrusionPixelThr = v;
        }
        else if (key == "INTRUSION_AREA_PERCENT") {
            double v = atof(val.c_str());
            if (v >= 0.01 && v <= 100.0) intrusionAreaPct = v;
        }
        else if (key == "INTRUSION_CONFIRM_FRAMES") {
            int v = atoi(val.c_str());
            if (v >= 1 && v <= 10) intrusionConfirm = v;
        }
        else if (key == "INTRUSION_COOLDOWN") {
            int v = atoi(val.c_str());
            if (v >= 0 && v <= 3600) intrusionCooldown = v;
        }
        else if (key == "INTRUSION_CHECK_MS") {
            int v = atoi(val.c_str());
            if (v >= 200 && v <= 10000) intrusionCheckMs = v;
        }
        else if (key.rfind("MONITOR", 0) == 0) {
            // MONITOR1, MONITOR2, MONITOR3, ... 형태 처리
            std::string numPart = key.substr(7);
            if (!numPart.empty() &&
                numPart.find_first_not_of("0123456789") == std::string::npos) {
                int idx = atoi(numPart.c_str());
                if (idx >= 1) monitorEnabled[idx] = StrUtil::parseBool(val, true);
            }
        }
    }
};

// ================================================================ ImageEncoder
// GDI+ 세션을 소유하며 비트맵을 PNG/JPEG 바이트로 인코딩한다.
class ImageEncoder {
public:
    ImageEncoder()
    {
        Gdiplus::GdiplusStartupInput gsi;
        ok_ = (Gdiplus::GdiplusStartup(&token_, &gsi, nullptr) == Gdiplus::Ok);
    }
    ~ImageEncoder()
    {
        if (ok_) Gdiplus::GdiplusShutdown(token_);
    }
    NO_COPY(ImageEncoder);

    bool ok() const { return ok_; }

    bool encode(Gdiplus::Bitmap& bmp, bool jpeg, ULONG quality, std::vector<BYTE>& out) const
    {
        out.clear();
        CLSID clsid;
        if (!getEncoderClsid(jpeg ? L"image/jpeg" : L"image/png", &clsid))
            return false;

        bool ok = false;
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;

        {
            Gdiplus::Status st;
            if (jpeg) {
                Gdiplus::EncoderParameters ep;
                ep.Count = 1;
                ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                ep.Parameter[0].NumberOfValues = 1;
                ep.Parameter[0].Value = &quality;
                st = bmp.Save(stream, &clsid, &ep);
            } else {
                st = bmp.Save(stream, &clsid, nullptr);
            }

            if (st == Gdiplus::Ok) {
                STATSTG ss;
                HGLOBAL hg = nullptr;
                if (stream->Stat(&ss, STATFLAG_NONAME) == S_OK &&
                    GetHGlobalFromStream(stream, &hg) == S_OK) {
                    SIZE_T n = (SIZE_T)ss.cbSize.QuadPart;
                    void* p = GlobalLock(hg);
                    if (p) {
                        out.assign((BYTE*)p, (BYTE*)p + n);
                        GlobalUnlock(hg);
                        ok = true;
                    }
                }
            }
        }
        stream->Release();
        return ok;
    }

    // 전송용 축소본: 긴 변이 maxDim 을 넘을 때만 축소해 JPEG 로 인코딩한다 (업로드 용량 절감).
    // 축소할 필요가 없거나 실패하면 false 이며, 호출자는 encodeForTelegram() 결과를 그대로 쓴다.
    bool encodeScaledJpeg(HBITMAP hBmp, UINT maxDim, ULONG quality,
                          std::vector<BYTE>& out, UINT& nw, UINT& nh) const
    {
        out.clear();
        Gdiplus::Bitmap src(hBmp, nullptr);
        const UINT w = src.GetWidth(), h = src.GetHeight();
        const UINT longSide = std::max(w, h);
        if (w == 0 || h == 0 || longSide <= maxDim) return false;

        const double s = (double)maxDim / (double)longSide;
        nw = std::max<UINT>(1, (UINT)(w * s + 0.5));
        nh = std::max<UINT>(1, (UINT)(h * s + 0.5));
        Gdiplus::Bitmap dst(nw, nh, PixelFormat24bppRGB);
        {
            Gdiplus::Graphics g(&dst);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(&src, 0, 0, (INT)nw, (INT)nh);
        }
        return encode(dst, true, quality, out);
    }

    // 텔레그램 전송용 인코딩.
    //  - 기본은 JPEG(품질 85). 사진 한도(10MB)를 넘으면 품질/해상도를 단계적으로 낮춰 다시 인코딩.
    //  - 가로+세로 > 10000px 이거나 가로세로 비율 > 20 이면 sendPhoto 가 거부하므로
    //    원본 그대로 문서(sendDocument, 한도 50MB)로 보내도록 asDocument=true 로 알린다.
    //  - 끝까지 한도를 못 맞추면 가장 좋은 품질의 JPEG 를 문서로 보낸다 (49MB 이하일 때).
    bool encodeForTelegram(HBITMAP hBmp, std::vector<BYTE>& out, bool& jpeg, bool& asDocument) const
    {
        jpeg = true;
        asDocument = false;
        out.clear();

        Gdiplus::Bitmap src(hBmp, nullptr);
        const UINT w = src.GetWidth(), h = src.GetHeight();
        if (w == 0 || h == 0) return false;

        const size_t PHOTO_LIMIT = 9500000;   // sendPhoto 10MB 한도에 여유
        const size_t DOC_LIMIT = 49000000;    // sendDocument 50MB 한도에 여유
        const double ratio = (double)std::max(w, h) / (double)std::min(w, h);
        if ((unsigned long long)w + h > 10000ULL || ratio > 20.0) asDocument = true;

        CLSID jpegClsid;
        if (!getEncoderClsid(L"image/jpeg", &jpegClsid)) {
            // JPEG 인코더가 없는 비정상 환경: PNG 로 대체
            jpeg = false;
            if (!encode(src, false, 0, out)) return false;
            if (out.size() > PHOTO_LIMIT) asDocument = true;
            return out.size() <= DOC_LIMIT;
        }

        struct Step { ULONG quality; double scale; };
        static const Step steps[] = {
            {85, 1.0}, {70, 1.0}, {60, 0.75}, {50, 0.5}, {40, 0.35}
        };
        const size_t limit = asDocument ? DOC_LIMIT : PHOTO_LIMIT;

        std::vector<BYTE> first; // 가장 좋은 품질(1단계) 결과 - 마지막 수단용
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
            const Step& s = steps[i];
            bool ok;
            if (s.scale >= 1.0) {
                ok = encode(src, true, s.quality, out);
            } else {
                UINT nw = std::max<UINT>(1, (UINT)(w * s.scale + 0.5));
                UINT nh = std::max<UINT>(1, (UINT)(h * s.scale + 0.5));
                Gdiplus::Bitmap dst(nw, nh, PixelFormat24bppRGB);
                {
                    Gdiplus::Graphics g(&dst);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.DrawImage(&src, 0, 0, (INT)nw, (INT)nh);
                }
                ok = encode(dst, true, s.quality, out);
            }
            if (!ok) return false;
            if (i == 0) first = out;
            if (out.size() <= limit) {
                if (i > 0)
                    Logger::log("이미지가 커서 품질/해상도를 낮춤 (품질 %lu, %.0f%%, %zu KB)",
                                (unsigned long)s.quality, s.scale * 100.0, out.size() / 1024);
                return true;
            }
        }

        // 사진 한도를 맞추지 못함 -> 원본 품질 JPEG 를 문서로 전송
        if (!asDocument && first.size() <= DOC_LIMIT) {
            asDocument = true;
            out.swap(first);
            return true;
        }
        return false;
    }

private:
    static bool getEncoderClsid(const WCHAR* mime, CLSID* clsid)
    {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return false;

        std::vector<BYTE> buf(size);
        auto* enc = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, enc);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(enc[i].MimeType, mime) == 0) {
                *clsid = enc[i].Clsid;
                return true;
            }
        }
        return false;
    }

    ULONG_PTR token_ = 0;
    bool ok_ = false;
};

// ================================================================ TelegramClient
class TelegramClient {
public:
    TelegramClient(std::string token, std::string chatId, const StopSignal& stop)
        : token_(std::move(token)), chatId_(std::move(chatId)), stop_(stop)
    {
        InitializeCriticalSection(&connCs_);
    }
    ~TelegramClient()
    {
        if (hConn_) WinHttpCloseHandle(hConn_);
        if (hSession_) WinHttpCloseHandle(hSession_);
        DeleteCriticalSection(&connCs_);
    }
    NO_COPY(TelegramClient);

    // 전송 1회 시도. 성공 시 true, 실패 시 resp 에 오류 내용.
    // asDoc=true 이면 sendDocument(원본 유지, 50MB), 아니면 sendPhoto(10MB).
    bool sendImage(const std::string& caption, const std::vector<BYTE>& img,
                   bool jpeg, bool asDoc, std::string& resp) const
    {
        resp.clear();
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";

        std::string body;
        body.reserve(img.size() + 1024);
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "caption", caption);

        body += "--" + boundary + "\r\n";
        body += std::string("Content-Disposition: form-data; name=\"") +
                (asDoc ? "document" : "photo") + "\"; filename=\"screen" +
                (jpeg ? ".jpg" : ".png") + "\"\r\n";
        body += std::string("Content-Type: ") + (jpeg ? "image/jpeg" : "image/png") + "\r\n\r\n";
        body.append((const char*)img.data(), img.size());
        body += "\r\n--" + boundary + "--\r\n";

        return post(asDoc ? L"sendDocument" : L"sendPhoto",
                    "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 텍스트를 .txt 파일로 전송 (/log 등 내용이 길 때). text 는 UTF-8.
    bool sendTextDocument(const std::string& caption, const std::string& filename,
                          const std::string& text, std::string& resp) const
    {
        resp.clear();
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";

        std::string body;
        body.reserve(text.size() + 1024);
        addField(body, boundary, "chat_id", chatId_);
        if (!caption.empty()) addField(body, boundary, "caption", caption);

        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"document\"; filename=\"" + filename + "\"\r\n";
        body += "Content-Type: text/plain; charset=utf-8\r\n\r\n";
        body += text;
        body += "\r\n--" + boundary + "--\r\n";

        return post(L"sendDocument", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 파일 원본 바이트를 문서로 전송 (/download). filename 은 UTF-8.
    bool sendFileDocument(const std::string& caption, const std::string& filename,
                          const std::vector<BYTE>& data, std::string& resp) const
    {
        resp.clear();
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";

        std::string body;
        body.reserve(data.size() + 1024);
        addField(body, boundary, "chat_id", chatId_);
        if (!caption.empty()) addField(body, boundary, "caption", caption);

        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"document\"; filename=\"" + filename + "\"\r\n";
        body += "Content-Type: application/octet-stream\r\n\r\n";
        body.append((const char*)data.data(), data.size());
        body += "\r\n--" + boundary + "--\r\n";

        return post(L"sendDocument", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // getFile: file_id -> file_path (폰에서 받은 파일 내려받기용). 실패 시 resp 에 오류 내용.
    bool getFilePath(const std::string& fileId, std::string& filePath, std::string& resp) const
    {
        filePath.clear();
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "file_id", fileId);
        body += "--" + boundary + "--\r\n";
        if (!post(L"getFile", "multipart/form-data; boundary=" + boundary, body, resp)) return false;
        const std::string k = "\"file_path\":\"";
        size_t p = resp.find(k);
        if (p == std::string::npos) return false;
        p += k.size();
        const size_t e = resp.find('"', p);
        if (e == std::string::npos) return false;
        std::string fp = resp.substr(p, e - p);
        for (size_t i; (i = fp.find("\\/")) != std::string::npos; ) fp.replace(i, 2, "/");
        filePath = fp;
        return !filePath.empty();
    }

    // getFile 로 얻은 file_path 의 파일을 내려받는다 (한도 kMaxDownload). 네트워크 오류면 1회 재시도.
    bool downloadFile(const std::string& filePath, std::vector<BYTE>& data, std::string& err) const
    {
        for (int attempt = 0; attempt < 2; ++attempt) {
            data.clear();
            err.clear();
            HINTERNET hConn = connection();
            if (!hConn) { err = "WinHTTP error (open)"; return false; }
            const std::wstring path = L"/file/bot" + StrUtil::toWide(token_) + L"/" + StrUtil::toWide(filePath);
            HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!hReq) { err = "WinHTTP error (request)"; return false; }
            bool ok = false, net = false;
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    ok = true;
                    for (;;) {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(hReq, &avail)) { ok = false; net = true; err = "다운로드 중단"; break; }
                        if (avail == 0) break;
                        if (data.size() + avail > kMaxDownload) { ok = false; err = "파일이 너무 큽니다"; break; }
                        const size_t old = data.size();
                        data.resize(old + avail);
                        DWORD rd = 0;
                        if (!WinHttpReadData(hReq, &data[old], avail, &rd)) { ok = false; net = true; err = "다운로드 중단"; break; }
                        data.resize(old + rd);
                        if (rd == 0) break;
                    }
                } else {
                    err = "HTTP " + std::to_string(status);
                }
            } else {
                net = true;
                err = "WinHTTP error " + std::to_string(GetLastError());
            }
            WinHttpCloseHandle(hReq);
            if (ok) return true;
            if (!net || stop_.requested()) break;
        }
        data.clear();
        return false;
    }

    // 텍스트 메시지 전송 (설정된 chat 으로)
    bool sendMessage(const std::string& text, std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "text", text);
        body += "--" + boundary + "--\r\n";
        return post(L"sendMessage", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 인라인 키보드(버튼)가 붙은 텍스트 메시지 전송. replyMarkup 은 reply_markup JSON 문자열.
    bool sendMessageWithKeyboard(const std::string& text, const std::string& replyMarkup,
                                 std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "text", text);
        addField(body, boundary, "reply_markup", replyMarkup);
        body += "--" + boundary + "--\r\n";
        return post(L"sendMessage", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 버튼을 눌렀을 때 텔레그램 앱의 로딩 표시를 없앤다 (text 가 있으면 잠깐 알림으로 표시)
    bool answerCallbackQuery(const std::string& callbackId, const std::string& text,
                             std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "callback_query_id", callbackId);
        if (!text.empty()) addField(body, boundary, "text", text);
        body += "--" + boundary + "--\r\n";
        return post(L"answerCallbackQuery", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 이미 보낸 메시지의 내용을 바꾼다. reply_markup 을 생략하므로 붙어 있던 버튼은 사라진다.
    bool editMessageText(const std::string& messageId, const std::string& text,
                         std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "message_id", messageId);
        addField(body, boundary, "text", text);
        body += "--" + boundary + "--\r\n";
        return post(L"editMessageText", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // sendMessage 응답({"ok":true,"result":{"message_id":N,...}})에서 message_id 를 꺼낸다. 없으면 "".
    static std::string extractMessageId(const std::string& resp)
    {
        const std::string k = "\"message_id\":";
        size_t p = resp.find(k);
        if (p == std::string::npos) return "";
        p += k.size();
        size_t e = p;
        while (e < resp.size() && isdigit((unsigned char)resp[e])) ++e;
        return resp.substr(p, e - p);
    }

    // 새 업데이트 조회 (롱폴링). timeoutSec 동안 새 메시지/버튼 클릭을 기다린다.
    bool getUpdates(long long offset, int timeoutSec, std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "offset", std::to_string(offset));
        addField(body, boundary, "timeout", std::to_string(timeoutSec));
        addField(body, boundary, "allowed_updates", "[\"message\",\"callback_query\"]");
        body += "--" + boundary + "--\r\n";
        return post(L"getUpdates", "multipart/form-data; boundary=" + boundary, body, resp);
    }

    // 전송 실패 시 재시도 (네트워크 오류, 5xx, 429 retry_after). 최대 3회.
    bool sendImageWithRetry(const std::string& caption, const std::vector<BYTE>& img,
                            bool jpeg, bool asDoc, std::string& resp) const
    {
        const int maxTry = 3;
        for (int attempt = 1; ; ++attempt) {
            if (sendImage(caption, img, jpeg, asDoc, resp)) return true;
            if (attempt >= maxTry) return false;

            bool net = resp.empty() || resp.rfind("WinHTTP error", 0) == 0;
            bool srv = resp.find("\"error_code\":5") != std::string::npos;
            int retryAfter = 0;
            size_t p = resp.find("\"retry_after\":");
            if (p != std::string::npos) retryAfter = atoi(resp.c_str() + p + 14);
            if (!net && !srv && retryAfter <= 0) return false; // 재시도해도 소용없는 오류

            int waitSec = retryAfter > 0 ? std::min(retryAfter + 1, 60) : 5 * attempt;
            Logger::log("전송 실패, %d초 후 재시도 (%d/%d)", waitSec, attempt, maxTry - 1);
            if (stop_.wait((DWORD)waitSec * 1000)) return false; // 종료 요청
        }
    }

private:
    static constexpr size_t kMaxDownload = 21u * 1024 * 1024; // getFile 한도(20MB)에 여유

    // WinHTTP 세션/연결 핸들은 한 번만 만들어 모든 스레드(메인/명령/침입 탐지)가 공유한다.
    // 요청마다 세션을 새로 열면 자동 프록시 탐색과 TLS 핸드셰이크를 매번 반복하게 된다.
    // (WinHttpConnect 는 소켓을 열지 않는다. 실제 소켓은 세션이 keep-alive 로 풀링하고 끊기면 알아서 다시 연다.
    //  WinHTTP 핸들은 스레드 안전하므로 요청 핸들만 호출마다 만들면 된다.)
    HINTERNET connection() const
    {
        CsLock lk(&connCs_);
        if (hConn_) return hConn_;
        if (!hSession_) {
            hSession_ = WinHttpOpen(L"ScreenTelegram/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession_) return nullptr;
            WinHttpSetTimeouts(hSession_, 10000, 10000, 60000, 120000);
        }
        hConn_ = WinHttpConnect(hSession_, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
        return hConn_;
    }

    // POST. 응답 본문을 resp 에 담고, "ok":true 이면 true.
    // 오래 놀던 keep-alive 소켓이 끊겨 있던 경우(12030/12032)에는 즉시 한 번 더 시도한다 (새 소켓으로 연결됨).
    bool post(const wchar_t* method, const std::string& contentType,
              const std::string& body, std::string& resp) const
    {
        if (postOnce(method, contentType, body, resp)) return true;
        if (resp == "WinHTTP error 12030" || resp == "WinHTTP error 12032")
            return postOnce(method, contentType, body, resp);
        return false;
    }

    // POST 1회.
    bool postOnce(const wchar_t* method, const std::string& contentType,
                  const std::string& body, std::string& resp) const
    {
        resp.clear();
        HINTERNET hConn = connection();
        if (!hConn) {
            resp = "WinHTTP error (open)";
            return false;
        }

        bool ok = false;
        HINTERNET hReq = nullptr;
        if (hConn) {
            std::wstring path = L"/bot" + StrUtil::toWide(token_) + L"/" + method;
            hReq = WinHttpOpenRequest(hConn, L"POST", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE);
        }
        if (hReq) {
            std::wstring hdr = L"Content-Type: " + StrUtil::toWide(contentType);
            if (WinHttpSendRequest(hReq, hdr.c_str(), (DWORD)-1L,
                                   (LPVOID)body.data(), (DWORD)body.size(),
                                   (DWORD)body.size(), 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD rd = 0;
                    if (!WinHttpReadData(hReq, &chunk[0], avail, &rd) || rd == 0) break;
                    resp.append(chunk.data(), rd);
                }
                ok = resp.find("\"ok\":true") != std::string::npos;
            } else {
                char e[64];
                snprintf(e, sizeof(e), "WinHTTP error %lu", GetLastError());
                resp = e;
            }
        } else {
            resp = "WinHTTP error (request)";
        }
        if (hReq) WinHttpCloseHandle(hReq); // 세션/연결 핸들은 재사용하므로 닫지 않는다
        return ok;
    }

    static void addField(std::string& body, const std::string& boundary,
                         const std::string& name, const std::string& value)
    {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
        body += value + "\r\n";
    }

    std::string token_;
    std::string chatId_;
    const StopSignal& stop_;
    mutable HINTERNET hSession_ = nullptr; // connection() 이 늦게 만들고 소멸자에서 닫는다
    mutable HINTERNET hConn_ = nullptr;
    mutable CRITICAL_SECTION connCs_;      // 위 두 핸들의 생성 보호
};

// ================================================================ CommandListener
// 텔레그램 봇 명령 수신 (getUpdates). 설정된 chat_id 에서 온 명령만 허용한다.
// 주의: 같은 봇 토큰으로 다른 프로그램이 getUpdates/webhook 을 쓰면 409 충돌이 난다.
// 수신 이벤트 1건: 일반 명령(/shot 등) 또는 인라인 버튼 클릭(callback_query)
struct BotEvent {
    std::string cmd;        // 명령(소문자, '/' 포함, @봇이름 제거). 버튼 클릭이면 비어 있음
    std::string arg;        // 명령 뒤의 인자(JSON 이스케이프 해제, 앞뒤 공백 제거, 대소문자 유지). "/ini A=1" -> "A=1"
    std::string cbData;     // 버튼 클릭: callback_data (예: "stop_yes"). 일반 명령이면 비어 있음
    std::string cbId;       // 버튼 클릭: callback_query id (answerCallbackQuery 용)
    std::string messageId;  // 버튼 클릭: 버튼이 붙어 있던 메시지의 id
    std::string fileId;     // 파일 수신: 첨부된 파일의 file_id (document/photo/video/audio/voice/animation/video_note)
    std::string fileName;   // 파일 수신: 원본 파일명(UTF-8). 사진처럼 이름이 없으면 비어 있음
    std::string fileKind;   // 파일 수신: "document", "photo" 등
    long long fileSize = 0; // 파일 수신: 크기(바이트). 모르면 0
    bool isCallback() const { return !cbData.empty(); }
    bool isFile() const { return !fileId.empty(); }
};

class CommandListener {
public:
    CommandListener(const TelegramClient& tg, std::string chatId,
                    std::vector<std::string> allowedUsers)
        : tg_(tg), chatId_(StrUtil::trim(chatId)), users_(std::move(allowedUsers)) {}

    // 시작 시 쌓여 있던 예전 명령/버튼 클릭은 실행하지 않도록 모두 건너뛴다.
    void skipPending()
    {
        for (int i = 0; i < 20; ++i) {
            int n = 0;
            if (!fetch(0, nullptr, n) || n == 0) break;
        }
    }

    // 최대 timeoutSec 초 기다리며 새 명령/버튼 클릭을 읽는다. 통신 실패 시 false.
    bool poll(int timeoutSec, std::vector<BotEvent>& events)
    {
        int n = 0;
        return fetch(timeoutSec, &events, n);
    }

private:
    bool fetch(int timeoutSec, std::vector<BotEvent>* events, int& count)
    {
        count = 0;
        std::string resp;
        if (!tg_.getUpdates(offset_, timeoutSec, resp)) {
            if (!failing_) { // 같은 오류 반복 로그 방지
                failing_ = true;
                Logger::log("명령 수신 실패: %s%s", resp.substr(0, 200).c_str(),
                            resp.find("\"error_code\":409") != std::string::npos
                                ? " (같은 봇 토큰을 다른 프로그램이 getUpdates/webhook 으로 사용 중)"
                                : "");
            }
            return false;
        }
        if (failing_) {
            failing_ = false;
            Logger::log("명령 수신 복구");
        }

        // 응답은 {"ok":true,"result":[{"update_id":N,"message":{...}},...]} 형태.
        // "update_id": 위치로 잘라 항목별로 처리한다.
        const std::string key = "\"update_id\":";
        std::vector<size_t> starts;
        for (size_t p = resp.find(key); p != std::string::npos; p = resp.find(key, p + key.size()))
            starts.push_back(p);

        for (size_t i = 0; i < starts.size(); ++i) {
            size_t end = (i + 1 < starts.size()) ? starts[i + 1] : resp.size();
            std::string seg = resp.substr(starts[i], end - starts[i]);

            long long uid = atoll(seg.c_str() + key.size());
            if (uid >= offset_) offset_ = uid + 1; // 처리한 업데이트는 다시 받지 않음
            ++count;
            if (!events) continue;

            // 버튼 클릭(callback_query)이면 메시지 텍스트가 아니라 callback_data 로 판단한다.
            // (callback_query 안의 "text" 는 버튼이 붙은 봇 메시지의 본문이므로 명령으로 해석하면 안 됨)
            BotEvent ev;
            std::string what; // 로그용
            const std::string cbKey = "\"callback_query\":{\"id\":\"";
            const size_t cbPos = seg.find(cbKey);
            if (cbPos != std::string::npos) {
                size_t s = cbPos + cbKey.size();
                size_t e = seg.find('"', s);
                ev.cbId = e == std::string::npos ? "" : seg.substr(s, e - s);
                ev.cbData = extractCallbackData(seg);
                const std::string mk = "\"message_id\":";
                size_t mp = seg.find(mk, cbPos);
                if (mp != std::string::npos) {
                    mp += mk.size();
                    size_t me = mp;
                    while (me < seg.size() && isdigit((unsigned char)seg[me])) ++me;
                    ev.messageId = seg.substr(mp, me - mp);
                }
                if (ev.cbData.empty()) continue;
                what = "버튼 " + ev.cbData;
            } else {
                const std::string rawText = extractText(seg);
                ev.cmd = extractCommand(rawText);
                if (!ev.cmd.empty()) {
                    ev.arg = extractArg(rawText);
                    what = ev.cmd;
                } else if (extractFile(seg, ev)) {   // 명령이 아니고 파일이 첨부된 메시지 (폰 -> PC)
                    what = "파일 " + (ev.fileName.empty() ? ev.fileKind : ev.fileName);
                } else {
                    continue;
                }
            }

            std::string chat = extractChatId(seg);
            if (chat != chatId_) {
                Logger::log("허용되지 않은 chat id(%s)의 명령 무시: %s", chat.c_str(), what.c_str());
                continue;
            }

            // 보낸 사람 확인: 허용 목록이 있으면 목록에 있는 사용자만,
            // 없으면 개인 chat(양수 id)에서만 허용 (그룹이면 아무나 /stop 을 보낼 수 있으므로 거부)
            // 버튼 클릭도 동일하게 검사한다 (callback_query.from = 버튼을 누른 사람).
            const std::string from = extractUserId(seg);
            if (!users_.empty()) {
                if (std::find(users_.begin(), users_.end(), from) == users_.end()) {
                    Logger::log("허용되지 않은 사용자(%s)의 명령 무시: %s", from.c_str(), what.c_str());
                    continue;
                }
            } else if (!chatId_.empty() && chatId_[0] == '-') {
                if (!warnedGroup_) {
                    warnedGroup_ = true;
                    Logger::log("그룹 chat 에서는 TELEGRAM_ALLOWED_USER_IDS 를 설정해야 명령이 동작합니다. 명령 무시.");
                }
                continue;
            }
            events->push_back(ev);
        }
        return true;
    }

    // s[p] 가 '{' 또는 '[' 일 때 짝이 맞는 끝까지의 부분 문자열 (문자열 안의 괄호는 무시). 실패 시 "".
    static std::string balanced(const std::string& s, size_t p)
    {
        int depth = 0;
        bool inStr = false;
        for (size_t i = p; i < s.size(); ++i) {
            const char c = s[i];
            if (inStr) {
                if (c == '\\') ++i;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') inStr = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') { if (--depth == 0) return s.substr(p, i - p + 1); }
        }
        return std::string();
    }

    // 객체 obj 의 바로 아래(깊이 1) key 의 값. 문자열이면 따옴표 안쪽(이스케이프 그대로), 아니면 원문. 없으면 "".
    // (document 안의 thumbnail 에도 file_id 가 있으므로 깊이를 구분해야 한다)
    static std::string topValue(const std::string& obj, const std::string& key)
    {
        int depth = 0;
        for (size_t i = 0; i < obj.size(); ++i) {
            const char c = obj[i];
            if (c == '"') {
                size_t e = i + 1;
                while (e < obj.size() && obj[e] != '"') { if (obj[e] == '\\') ++e; ++e; }
                if (depth == 1 && e < obj.size() && e + 1 < obj.size() && obj[e + 1] == ':' &&
                    obj.compare(i + 1, e - i - 1, key) == 0) {
                    const size_t v = e + 2;
                    if (v < obj.size() && obj[v] == '"') {
                        size_t ve = v + 1;
                        while (ve < obj.size() && obj[ve] != '"') { if (obj[ve] == '\\') ++ve; ++ve; }
                        return obj.substr(v + 1, ve - v - 1);
                    }
                    size_t ve = v;
                    while (ve < obj.size() && obj[ve] != ',' && obj[ve] != '}') ++ve;
                    return obj.substr(v, ve - v);
                }
                i = e;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
            }
        }
        return std::string();
    }

    // 파일이 첨부된 메시지면 ev 의 file* 를 채우고 true. photo 는 크기별 배열의 마지막(가장 큰 것)을 쓴다.
    static bool extractFile(const std::string& seg, BotEvent& ev)
    {
        static const char* const kinds[] = {"document", "video", "audio", "voice", "animation", "video_note"};
        std::string obj;
        for (const char* k : kinds) {
            const std::string key = std::string("\"") + k + "\":{";
            const size_t p = seg.find(key);
            if (p == std::string::npos) continue;
            obj = balanced(seg, p + key.size() - 1);
            ev.fileKind = k;
            break;
        }
        if (obj.empty()) {
            const std::string key = "\"photo\":[";
            const size_t p = seg.find(key);
            if (p == std::string::npos) return false;
            const std::string arr = balanced(seg, p + key.size() - 1);
            const size_t last = arr.rfind('{');
            if (last == std::string::npos) return false;
            obj = balanced(arr, last);
            ev.fileKind = "photo";
        }
        ev.fileId = topValue(obj, "file_id");
        if (ev.fileId.empty()) return false;
        ev.fileName = ev.fileKind == "photo" ? std::string() : unescapeJson(topValue(obj, "file_name"));
        ev.fileSize = atoll(topValue(obj, "file_size").c_str());
        return true;
    }

    // callback_query 의 "data":"..." (메시지 본문 뒤, 항목 끝쪽에 있으므로 마지막 것을 사용)
    static std::string extractCallbackData(const std::string& seg)
    {
        const std::string k = "\"data\":\"";
        size_t p = seg.rfind(k);
        if (p == std::string::npos) return "";
        p += k.size();
        size_t e = seg.find('"', p);
        return e == std::string::npos ? "" : seg.substr(p, e - p);
    }

    // message.from.id (메시지에서 "from" 이 chat 보다 먼저 나온다)
    static std::string extractUserId(const std::string& seg)
    {
        const std::string k = "\"from\":{\"id\":";
        size_t p = seg.find(k);
        if (p == std::string::npos) return "";
        p += k.size();
        size_t e = p;
        while (e < seg.size() && isdigit((unsigned char)seg[e])) ++e;
        return seg.substr(p, e - p);
    }

    static std::string extractChatId(const std::string& seg)
    {
        const std::string k = "\"chat\":{\"id\":";
        size_t p = seg.find(k);
        if (p == std::string::npos) return "";
        p += k.size();
        size_t e = p;
        while (e < seg.size() && (seg[e] == '-' || isdigit((unsigned char)seg[e]))) ++e;
        return seg.substr(p, e - p);
    }

    // "text":"..." 의 원문(이스케이프는 그대로 둠)
    static std::string extractText(const std::string& seg)
    {
        const std::string k = "\"text\":\"";
        size_t p = seg.find(k);
        if (p == std::string::npos) return "";
        std::string out;
        for (p += k.size(); p < seg.size() && seg[p] != '"'; ++p) {
            if (seg[p] == '\\' && p + 1 < seg.size()) out += seg[p++];
            out += seg[p];
        }
        return out;
    }

    // "/Shot@MyBot 인자" -> "/shot"
    static std::string extractCommand(const std::string& text)
    {
        if (text.empty() || text[0] != '/') return "";
        size_t e = 1;
        while (e < text.size() && text[e] != ' ' && text[e] != '@' && text[e] != '\\') ++e;
        std::string c = text.substr(0, e);
        for (auto& ch : c) ch = (char)tolower((unsigned char)ch);
        return c;
    }

    // "/ini@MyBot KEY=값" -> "KEY=값". raw 는 JSON 이스케이프가 그대로 남아 있는 텍스트.
    static std::string extractArg(const std::string& raw)
    {
        size_t e = 1;
        while (e < raw.size() && raw[e] != ' ' && raw[e] != '@' && raw[e] != '\\') ++e;
        if (e < raw.size() && raw[e] == '@') // @봇이름 건너뜀
            while (e < raw.size() && raw[e] != ' ' && raw[e] != '\\') ++e;
        return StrUtil::trim(unescapeJson(raw.substr(e)));
    }

    static bool hex4(const std::string& s, size_t pos, unsigned& v)
    {
        if (pos + 4 > s.size()) return false;
        v = 0;
        for (size_t k = 0; k < 4; ++k) {
            const char c = s[pos + k];
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
            else return false;
            v = (v << 4) | d;
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        }
    }

    // JSON 문자열 이스케이프(\" \\ \/ \n \t \uXXXX, 서로게이트 쌍 포함)를 UTF-8 로 되돌린다.
    static std::string unescapeJson(const std::string& s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c != '\\' || i + 1 >= s.size()) { out += c; continue; }
            const char n = s[++i];
            switch (n) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(s, i + 1, cp)) { out += "\\u"; break; }
                i += 4;
                unsigned lo = 0;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 < s.size() && s[i + 1] == '\\' &&
                    s[i + 2] == 'u' && hex4(s, i + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                appendUtf8(out, cp);
                break;
            }
            default: out += n; break; // \" \\ \/ 등
            }
        }
        return out;
    }

    const TelegramClient& tg_;
    std::string chatId_;
    std::vector<std::string> users_;
    long long offset_ = 0;
    bool failing_ = false;
    bool warnedGroup_ = false;
};

// ================================================================ 캡처 소스
// 캡처 결과 1장: 라벨(예: "Monitor 1/2 (1920x1080)", "Camera") + 비트맵
struct Shot {
    std::string label;  // 캡션/로그용
    std::string tag;    // 파일명용 (예: "Monitor1", "Camera")
    ScopedBitmap bmp;

    Shot(std::string l, std::string t, HBITMAP h)
        : label(std::move(l)), tag(std::move(t)), bmp(h) {}
    Shot(Shot&&) noexcept = default;
    Shot& operator=(Shot&&) noexcept = default;
};

// 캡처 소스 인터페이스. 캡처한 이미지를 out 에 추가한다. (실패 시 로그만 남기고 추가 안 함)
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual void capture(std::vector<Shot>& out) = 0;
    virtual bool isCamera() const { return false; } // /photo, /screen 필터용
};

// 어떤 소스를 캡처할지 (/shot=All, /screen=ScreenOnly, /photo=CameraOnly)
enum class CaptureFilter { All, ScreenOnly, CameraOnly };

// ---------------------------------------------------------------- MonitorCapture
class MonitorCapture : public ICaptureSource {
public:
    explicit MonitorCapture(const Config& cfg) : cfg_(cfg) {}

    void capture(std::vector<Shot>& out) override
    {
        std::vector<MonInfo> mons = enumerate();
        if (mons.empty()) {
            Logger::log("모니터를 찾을 수 없습니다.");
            return;
        }

        // 잠금 화면/로그온 전(보안 데스크톱)에서는 화면 캡처가 불가하므로 건너뜀
        HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!hDesk) {
            Logger::log("입력 데스크톱 접근 불가 (잠금 화면/로그온 전/원격 끊김) - 화면 캡처 건너뜀");
            return;
        }
        CloseDesktop(hDesk);

        for (size_t i = 0; i < mons.size(); ++i) {
            int oneBased = (int)i + 1;
            if (!cfg_.isMonitorEnabled(oneBased)) {
                if (skipLogged_.insert(oneBased).second) // 처음 한 번만 로그
                    Logger::log("모니터 %d 는 INI 설정에 의해 건너뜀 (MONITOR%d=FALSE)",
                                oneBased, oneBased);
                continue;
            }

            const RECT& rc = mons[i].rc;
            int w = rc.right - rc.left, h = rc.bottom - rc.top;

            HBITMAP hBmp = grab(rc);
            if (!hBmp) {
                Logger::log("모니터 %zu 캡처 실패", i + 1);
                continue;
            }

            char label[96];
            snprintf(label, sizeof(label), "Monitor %zu/%zu (%dx%d)",
                     i + 1, mons.size(), w, h);
            out.emplace_back(label, "Monitor" + std::to_string(oneBased), hBmp);
        }
    }

private:
    struct MonInfo {
        RECT rc;
        bool primary;
    };

    static BOOL CALLBACK enumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
    {
        auto* v = reinterpret_cast<std::vector<MonInfo>*>(lp);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(hMon, &mi)) {
            MonInfo m;
            m.rc = mi.rcMonitor;
            m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            v->push_back(m);
        }
        return TRUE;
    }

    // 주 모니터를 1번으로, 나머지는 왼쪽->오른쪽 순
    static std::vector<MonInfo> enumerate()
    {
        std::vector<MonInfo> v;
        EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&v));
        std::stable_sort(v.begin(), v.end(), [](const MonInfo& a, const MonInfo& b) {
            if (a.primary != b.primary) return a.primary;
            if (a.rc.left != b.rc.left) return a.rc.left < b.rc.left;
            return a.rc.top < b.rc.top;
        });
        return v;
    }

    static HBITMAP grab(const RECT& rc)
    {
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return nullptr;

        HDC hScreen = GetDC(nullptr);
        HDC hMem = CreateCompatibleDC(hScreen);
        HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
        HGDIOBJ old = SelectObject(hMem, hBmp);

        BOOL ok = BitBlt(hMem, 0, 0, w, h, hScreen, rc.left, rc.top,
                         SRCCOPY | CAPTUREBLT);

        SelectObject(hMem, old);
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScreen);

        if (!ok) {
            DeleteObject(hBmp);
            return nullptr;
        }
        return hBmp;
    }

    const Config& cfg_;
    std::set<int> skipLogged_;
};

// ---------------------------------------------------------------- CameraCapture
// Media Foundation 함수를 런타임에 동적으로 로드한다.
// MF 함수를 직접(암시적 링크) 호출하면, mfplat.dll / mfreadwrite.dll 을 로드하지
// 못하는 환경(Windows N/KN 등)에서 exe 가 "시작조차" 못 한다. 이를 막기 위해
// LoadLibrary/GetProcAddress 로 실행 중에만 찾고, 실패하면 카메라만 비활성화한다.
class CameraCapture : public ICaptureSource {
public:
    explicit CameraCapture(const Config& cfg) : cfg_(cfg)
    {
        InitializeCriticalSection(&frameCs_);
        InitializeCriticalSection(&camCs_);
    }
    ~CameraCapture() override
    {
        closeReader();
        if (started_ && api_.pMFShutdown) api_.pMFShutdown();
        DeleteCriticalSection(&camCs_);
        DeleteCriticalSection(&frameCs_);
    }
    NO_COPY(CameraCapture);

    // MF 초기화. 실패하면 false (이 경우 소스를 사용하지 않는다).
    bool start()
    {
        std::string err;
        if (loadApi(err) && SUCCEEDED(api_.pMFStartup(MF_VERSION, 0))) {
            started_ = true;
            return true;
        }
        Logger::log("카메라 기능 비활성화: %s", err.c_str());
        Logger::log("(화면 캡처 기능은 정상적으로 계속 동작합니다)");
        return false;
    }

    bool isCamera() const override { return true; }

    // ---- 침입 탐지 스레드 전용 API (공유 모드에서는 이 스레드만 카메라에 접근한다) ----

    // 공유 모드 전환 (/detectOn, /detectOff 로 런타임에 바뀜). 탐지 스레드에서만 호출한다.
    //  on=true : 메인 스레드의 capture() 는 카메라를 직접 열지 않고, 탐지 스레드가 저장해 둔 최신 프레임을 사용한다.
    //  on=false: 탐지 스레드가 연 카메라를 그 스레드에서 닫고, 메인 스레드가 다시 직접(필요할 때만) 카메라를 연다.
    // 메인 스레드가 카메라를 쓰는 중이면 끝날 때까지 기다린다 (camCs_).
    void setSharedMode(bool on)
    {
        CsLock lk(&camCs_);
        closeReader(); // 어느 쪽이 열어 둔 것이든 모드가 바뀌면 닫고 새 소유자가 다시 연다
        sharedMode_ = on;
        EnterCriticalSection(&frameCs_);
        latest_.raw.clear(); // 오래된 프레임이 남지 않도록
        latest_.tick = 0;
        LeaveCriticalSection(&frameCs_);
    }

    // 카메라 프레임 원본(RGB32) 1장을 읽는다. 카메라가 닫혀 있으면 연다. 실패하면 닫고 false (다음 호출에서 다시 연다).
    bool grabFrame(std::vector<BYTE>& raw, LONG& stride, std::string& errMsg)
    {
        CsLock lk(&camCs_);
        if (!loadApi(errMsg)) return false;

        int warmup = 1; // 열어 둔 동안 쌓인 오래된 프레임 1장을 버리고 최신 프레임 사용
        if (!reader_) {
            if (!openReader(errMsg)) return false;
            warmup = 5;
        }
        if (!readRaw(warmup, raw, stride, errMsg)) {
            closeReader();
            return false;
        }

        if (sharedMode_) { // 메인 스레드(/shot, 주기 캡처)가 쓸 수 있도록 최신 프레임 보관
            EnterCriticalSection(&frameCs_);
            latest_.raw = raw;
            latest_.stride = stride;
            latest_.w = w_;
            latest_.h = h_;
            latest_.bottomUp = bottomUp_;
            latest_.tick = GetTickCount64();
            LeaveCriticalSection(&frameCs_);
        }
        return true;
    }

    // 프레임 원본을 축소(가로 약 160px)한 그레이스케일로 변환
    bool makeGray(const std::vector<BYTE>& raw, LONG stride, std::vector<BYTE>& gray, int& gw, int& gh) const
    {
        const int step = std::max(1, (int)w_ / 160);
        gw = (int)w_ / step;
        gh = (int)h_ / step;
        if (gw <= 0 || gh <= 0 || raw.size() < (size_t)stride * h_) return false;
        gray.assign((size_t)gw * gh, 0);
        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                unsigned sum = 0;
                for (int yy = 0; yy < step; ++yy) {
                    const BYTE* p = raw.data() + (size_t)(y * step + yy) * stride + (size_t)x * step * 4;
                    for (int xx = 0; xx < step; ++xx, p += 4)
                        sum += (29u * p[0] + 150u * p[1] + 77u * p[2]) >> 8; // B,G,R -> 밝기
                }
                gray[(size_t)y * gw + x] = (BYTE)(sum / (unsigned)(step * step));
            }
        }
        return true;
    }

    // 프레임 원본 -> HBITMAP (호출자가 소유)
    HBITMAP makeBitmap(const std::vector<BYTE>& raw, LONG stride) const
    {
        if (raw.size() < (size_t)stride * h_) return nullptr;
        return frameToBitmap(raw.data(), stride, w_, h_, bottomUp_);
    }

    void capture(std::vector<Shot>& out) override
    {
        std::string err;
        HBITMAP h = captureFrame(err);
        if (!h) {
            Logger::log("카메라 캡처 실패: %s", err.c_str());
            return;
        }
        out.emplace_back("Camera", "Camera", h);
    }

private:
    typedef HRESULT(WINAPI* PFN_MFStartup)(ULONG, DWORD);
    typedef HRESULT(WINAPI* PFN_MFShutdown)(void);
    typedef HRESULT(WINAPI* PFN_MFCreateAttributes)(IMFAttributes**, UINT32);
    typedef HRESULT(WINAPI* PFN_MFEnumDeviceSources)(IMFAttributes*, IMFActivate***, UINT32*);
    typedef HRESULT(WINAPI* PFN_MFCreateMediaType)(IMFMediaType**);
    typedef HRESULT(WINAPI* PFN_MFCreateSourceReaderFromMediaSource)(
        IMFMediaSource*, IMFAttributes*, IMFSourceReader**);

    struct Api {
        HMODULE hMfplat = nullptr;
        HMODULE hMf = nullptr;         // MFEnumDeviceSources 는 mf.dll 소속
        HMODULE hMfreadwrite = nullptr;
        PFN_MFStartup pMFStartup = nullptr;
        PFN_MFShutdown pMFShutdown = nullptr;
        PFN_MFCreateAttributes pMFCreateAttributes = nullptr;
        PFN_MFEnumDeviceSources pMFEnumDeviceSources = nullptr;
        PFN_MFCreateMediaType pMFCreateMediaType = nullptr;
        PFN_MFCreateSourceReaderFromMediaSource pMFCreateSourceReaderFromMediaSource = nullptr;
        bool ready = false;
    };

    // 참고(MSDN): MFStartup/MFShutdown/MFCreateAttributes/MFCreateMediaType -> mfplat.dll
    //            MFEnumDeviceSources                                        -> mf.dll
    //            MFCreateSourceReaderFromMediaSource                        -> mfreadwrite.dll
    bool loadApi(std::string& errMsg)
    {
        if (api_.ready) return true;

        api_.hMfplat = LoadLibraryW(L"mfplat.dll");
        api_.hMf = LoadLibraryW(L"mf.dll");
        api_.hMfreadwrite = LoadLibraryW(L"mfreadwrite.dll");
        if (!api_.hMfplat || !api_.hMf || !api_.hMfreadwrite) {
            errMsg = "mfplat.dll / mf.dll / mfreadwrite.dll 중 일부를 찾을 수 없습니다. "
                     "(Windows N/KN 에디션이면 'Media Feature Pack' 설치 필요)";
            return false;
        }

        api_.pMFStartup = (PFN_MFStartup)GetProcAddress(api_.hMfplat, "MFStartup");
        api_.pMFShutdown = (PFN_MFShutdown)GetProcAddress(api_.hMfplat, "MFShutdown");
        api_.pMFCreateAttributes =
            (PFN_MFCreateAttributes)GetProcAddress(api_.hMfplat, "MFCreateAttributes");
        api_.pMFEnumDeviceSources =
            (PFN_MFEnumDeviceSources)GetProcAddress(api_.hMf, "MFEnumDeviceSources");
        api_.pMFCreateMediaType =
            (PFN_MFCreateMediaType)GetProcAddress(api_.hMfplat, "MFCreateMediaType");
        api_.pMFCreateSourceReaderFromMediaSource =
            (PFN_MFCreateSourceReaderFromMediaSource)GetProcAddress(
                api_.hMfreadwrite, "MFCreateSourceReaderFromMediaSource");

        if (!api_.pMFStartup) errMsg += "MFStartup(mfplat.dll) ";
        if (!api_.pMFShutdown) errMsg += "MFShutdown(mfplat.dll) ";
        if (!api_.pMFCreateAttributes) errMsg += "MFCreateAttributes(mfplat.dll) ";
        if (!api_.pMFEnumDeviceSources) errMsg += "MFEnumDeviceSources(mf.dll) ";
        if (!api_.pMFCreateMediaType) errMsg += "MFCreateMediaType(mfplat.dll) ";
        if (!api_.pMFCreateSourceReaderFromMediaSource)
            errMsg += "MFCreateSourceReaderFromMediaSource(mfreadwrite.dll) ";

        if (!errMsg.empty()) {
            errMsg = "Media Foundation 함수를 찾지 못함: " + errMsg;
            return false;
        }

        api_.ready = true;
        return true;
    }

    // RGB32(BGRA) 프레임 버퍼를 HBITMAP(DIB)으로 변환
    // bottomUp=true 이면 버퍼의 첫 행이 화면 맨 아래 행이므로 뒤집어서 복사한다.
    // 또한 MF RGB32 의 4번째 바이트(X)는 0 일 수 있어 알파를 0xFF 로 채운다.
    static HBITMAP frameToBitmap(const BYTE* data, LONG stride, UINT32 w, UINT32 h,
                                 bool bottomUp)
    {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)w;
        bi.bmiHeader.biHeight = -(LONG)h; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC hdc = GetDC(nullptr);
        HBITMAP hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!hBmp || !bits) return nullptr;

        BYTE* dst = (BYTE*)bits;
        LONG rowBytes = (LONG)w * 4;
        for (UINT32 y = 0; y < h; ++y) {
            const BYTE* srcRow = data + (size_t)(bottomUp ? (h - 1 - y) : y) * stride;
            BYTE* d = dst + (size_t)y * rowBytes;
            memcpy(d, srcRow, (size_t)rowBytes);
            for (LONG x = 3; x < rowBytes; x += 4) d[x] = 0xFF; // 알파 불투명
        }
        return hBmp;
    }

    // 최초 1회 카메라 목록을 로그에 남김 (CAMERA_INDEX 선택용)
    void logDevicesOnce(IMFActivate** devices, UINT32 count)
    {
        if (listed_) return;
        listed_ = true;
        for (UINT32 i = 0; i < count; ++i) {
            WCHAR* nm = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(devices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nm, &len)) && nm) {
                Logger::log("  카메라 #%u: %s", (unsigned)i, StrUtil::toUtf8(nm).c_str());
                CoTaskMemFree(nm);
            }
        }
    }

    void closeReader()
    {
        if (reader_) { reader_->Release(); reader_ = nullptr; }
        if (source_) { source_->Shutdown(); source_->Release(); source_ = nullptr; }
    }

    // cfg_.cameraIndex 번째 웹캠을 열어 reader_ 를 준비한다. 실패 시 false (열린 것은 모두 정리).
    bool openReader(std::string& errMsg)
    {
        IMFAttributes* pAttr = nullptr;
        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        IMFAttributes* pReaderAttr = nullptr;
        IMFMediaType* pType = nullptr;

        HRESULT hr = api_.pMFCreateAttributes(&pAttr, 1);
        if (SUCCEEDED(hr))
            hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr))
            hr = api_.pMFEnumDeviceSources(pAttr, &ppDevices, &count);

        if (SUCCEEDED(hr) && count == 0) {
            errMsg = "카메라 장치를 찾을 수 없습니다.";
            hr = E_FAIL;
        }

        if (SUCCEEDED(hr)) {
            logDevicesOnce(ppDevices, count);
            UINT32 idx = 0;
            if (cfg_.cameraIndex >= 0 && (UINT32)cfg_.cameraIndex < count)
                idx = (UINT32)cfg_.cameraIndex;
            else
                Logger::log("CAMERA_INDEX=%d 가 범위를 벗어나 0번 카메라를 사용합니다.",
                            cfg_.cameraIndex);
            hr = ppDevices[idx]->ActivateObject(IID_PPV_ARGS(&source_));
        }

        // 리더가 카메라의 원본 포맷(MJPEG/YUY2/NV12 등)을 RGB32 로 자동
        // 변환할 수 있도록 허용한다. 이게 없으면 원본 포맷과 RGB32 가 바로
        // 맞지 않는 경우 MF_E_INVALIDMEDIATYPE(0xC00D36B4) 로 실패한다.
        if (SUCCEEDED(hr))
            hr = api_.pMFCreateAttributes(&pReaderAttr, 1);
        if (SUCCEEDED(hr))
            hr = pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr))
            hr = api_.pMFCreateSourceReaderFromMediaSource(source_, pReaderAttr, &reader_);

        // 출력 포맷을 RGB32(BGRA) 로 강제 설정
        if (SUCCEEDED(hr))
            hr = api_.pMFCreateMediaType(&pType);
        if (SUCCEEDED(hr))
            hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr))
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr))
            hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);

        w_ = h_ = 0;
        bottomUp_ = true; // MF RGB 규약: stride 가 음수이면 bottom-up (속성 없을 때 기본)
        if (SUCCEEDED(hr)) {
            IMFMediaType* pCur = nullptr;
            hr = reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCur);
            if (SUCCEEDED(hr)) {
                MFGetAttributeSize(pCur, MF_MT_FRAME_SIZE, &w_, &h_);
                UINT32 ds = 0;
                if (SUCCEEDED(pCur->GetUINT32(MF_MT_DEFAULT_STRIDE, &ds)))
                    bottomUp_ = ((LONG)ds < 0);
                pCur->Release();
            }
        }

        if (SUCCEEDED(hr) && (w_ == 0 || h_ == 0)) {
            errMsg = "카메라 프레임 크기를 확인할 수 없습니다.";
            hr = E_FAIL;
        }

        if (FAILED(hr) && errMsg.empty()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "카메라 열기 실패 (0x%08lX)", (unsigned long)hr);
            errMsg = buf;
        }

        if (pType) pType->Release();
        if (pReaderAttr) pReaderAttr->Release();
        if (ppDevices) {
            for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
            CoTaskMemFree(ppDevices);
        }
        if (pAttr) pAttr->Release();

        if (FAILED(hr)) {
            closeReader();
            return false;
        }
        return true;
    }

    // 열려 있는 reader_ 에서 프레임 원본(RGB32)을 읽어 raw 에 복사한다. 앞의 warmupFrames 장은 버린다
    // (자동 노출/화이트밸런스 안정화, 또는 열어 두는 동안 쌓인 오래된 프레임 제거).
    bool readRaw(int warmupFrames, std::vector<BYTE>& raw, LONG& stride, std::string& errMsg)
    {
        bool got = false;
        HRESULT hr = S_OK;
        for (int attempt = 0; attempt < warmupFrames + 3 && SUCCEEDED(hr); ++attempt) {
            DWORD flags = 0;
            IMFSample* pSample = nullptr;
            hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                     nullptr, &flags, nullptr, &pSample);
            if (FAILED(hr)) break;
            if (!pSample) continue; // 타임아웃 등으로 샘플 없음, 재시도

            if (attempt < warmupFrames) {
                pSample->Release();
                continue;
            }

            IMFMediaBuffer* pBuf = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuf);
            if (SUCCEEDED(hr)) {
                BYTE* data = nullptr;
                DWORD maxLen = 0, curLen = 0;
                hr = pBuf->Lock(&data, &maxLen, &curLen);
                if (SUCCEEDED(hr)) {
                    stride = (LONG)(curLen / h_);
                    if (stride < (LONG)w_ * 4) stride = (LONG)w_ * 4;
                    if ((size_t)stride * h_ <= curLen) {
                        raw.assign(data, data + curLen);
                        got = true;
                    } else {
                        hr = E_FAIL; // 버퍼가 프레임보다 작음 (범위 밖 읽기 방지)
                    }
                    pBuf->Unlock();
                }
                pBuf->Release();
            }
            pSample->Release();
            break;
        }

        if (!got) {
            char buf[64];
            snprintf(buf, sizeof(buf), "카메라 캡처 실패 (0x%08lX)", (unsigned long)hr);
            errMsg = buf;
        }
        return got;
    }

    HBITMAP readFrame(int warmupFrames, std::string& errMsg)
    {
        std::vector<BYTE> raw;
        LONG stride = 0;
        if (!readRaw(warmupFrames, raw, stride, errMsg)) return nullptr;
        HBITMAP h = frameToBitmap(raw.data(), stride, w_, h_, bottomUp_);
        if (!h) errMsg = "카메라 프레임 변환 실패";
        return h;
    }

    // 공유 모드: 탐지 스레드가 저장한 최신 프레임으로 비트맵 생성. 아직 첫 프레임이 없으면 잠시 기다린다.
    HBITMAP bitmapFromLatest(std::string& errMsg)
    {
        for (int i = 0; i < 80; ++i) { // 최대 약 8초 (카메라가 막 열리는 중일 수 있음)
            EnterCriticalSection(&frameCs_);
            const bool fresh = !latest_.raw.empty() && GetTickCount64() - latest_.tick < 10000;
            if (fresh) {
                std::vector<BYTE> raw = latest_.raw;
                const LONG stride = latest_.stride;
                const UINT32 w = latest_.w, h = latest_.h;
                const bool bu = latest_.bottomUp;
                LeaveCriticalSection(&frameCs_);
                HBITMAP bmp = frameToBitmap(raw.data(), stride, w, h, bu);
                if (!bmp) errMsg = "카메라 프레임 변환 실패";
                return bmp;
            }
            LeaveCriticalSection(&frameCs_);
            Sleep(100);
        }
        errMsg = "침입 탐지 스레드에서 최신 카메라 프레임을 받지 못했습니다.";
        return nullptr;
    }

    // 한 프레임 캡처 (실패 시 nullptr). CAMERA_KEEP_OPEN=FALSE 이면 매번 열고 닫는다.
    // 열어 두는 모드에서 읽기에 실패하면 닫아서 다음 주기에 다시 연다.
    HBITMAP captureFrame(std::string& errMsg)
    {
        {
            CsLock lk(&camCs_); // 모드 전환(setSharedMode)/탐지 스레드의 프레임 읽기와 동시에 카메라를 열지 않도록
            if (!sharedMode_) {
                if (!loadApi(errMsg))
                    return nullptr;

                int warmup = 2; // 이미 열려 있는 경우
                if (!reader_) {
                    if (!openReader(errMsg)) return nullptr;
                    warmup = 5;
                }
                HBITMAP result = readFrame(warmup, errMsg);
                if (!result || !cfg_.cameraKeepOpen) closeReader();
                return result;
            }
        }
        return bitmapFromLatest(errMsg); // 침입 탐지 스레드가 카메라를 전담 (락 밖에서 대기)
    }

    const Config& cfg_;
    Api api_;
    bool started_ = false;
    bool listed_ = false;
    IMFMediaSource* source_ = nullptr;
    IMFSourceReader* reader_ = nullptr;
    UINT32 w_ = 0, h_ = 0;
    bool bottomUp_ = true;

    // 탐지 스레드 -> 메인 스레드 최신 프레임 공유 (frameCs_ 로 보호)
    struct LatestFrame {
        std::vector<BYTE> raw;
        LONG stride = 0;
        UINT32 w = 0, h = 0;
        bool bottomUp = true;
        ULONGLONG tick = 0;
    };
    LatestFrame latest_;
    CRITICAL_SECTION frameCs_;
    CRITICAL_SECTION camCs_;       // reader_/source_ 와 sharedMode_ 보호 (카메라 접근 직렬화)
    volatile bool sharedMode_ = false;
};

// ================================================================ MotionDetector
// 침입(움직임) 탐지. 보안 카메라/VMS 에서 널리 쓰는 프레임 차분(frame differencing) +
// 배경 모델링(running average) 방식에 오탐 억제 장치를 더했다.
//   1) 그레이스케일 + 3x3 박스 블러        : 센서 노이즈 억제
//   2) 이동 평균 배경 모델                 : 서서히 변하는 조명/그림자에 적응
//   3) 픽셀 임계값 + 변화 면적 비율 임계값 : 작은 노이즈/벌레 등 무시
//   4) 연속 N프레임 확인                   : 순간적인 깜빡임 무시
//   5) 전역 밝기 변화 거부                 : 조명 켬/끔, 자동 노출 변화로 화면 대부분이 바뀌면 탐지 안 함
//   6) 워밍업                              : 시작/카메라 재연결 직후 노출 안정화 동안 탐지 안 함
//   7) 쿨다운                              : 한 번 탐지 후 일정 시간 재탐지 억제 (알림 폭주 방지)
class MotionDetector {
public:
    struct Params {
        int pixelThreshold = 25;     // 픽셀 밝기 차이 임계값 (0~255)
        double areaPercent = 1.0;    // 변화 픽셀 비율(%) 임계값
        int confirmFrames = 2;       // 연속 프레임 수
        int cooldownSec = 60;        // 재탐지 대기(초)
        double lightingPercent = 60.0; // 이 비율 이상 변하면 조명 변화로 간주 (탐지 안 함, 배경 재설정)
        int warmupFrames = 10;       // 시작 시 안정화 프레임 수
        float alpha = 0.05f;         // 배경 학습률
    };

    MotionDetector() = default;
    explicit MotionDetector(const Params& p) : p_(p) {}

    // 그레이 프레임 1장을 넣고, 침입으로 확정되면 true. areaOut 에 이번 프레임의 변화 면적(%) 반환.
    bool update(const std::vector<BYTE>& gray, int w, int h, ULONGLONG nowMs, double* areaOut = nullptr)
    {
        if (areaOut) *areaOut = 0.0;
        if (w <= 0 || h <= 0 || gray.size() != (size_t)w * h) return false;

        std::vector<BYTE> cur = blur(gray, w, h);
        const size_t n = cur.size();
        if (bg_.size() != n || w_ != w || h_ != h) { // 최초 또는 해상도 변경
            w_ = w; h_ = h;
            resetBackground(cur);
            warmup_ = p_.warmupFrames;
            return false;
        }

        std::vector<char> mask(n, 0);
        size_t changed = 0;
        for (size_t i = 0; i < n; ++i) {
            if (std::fabs((float)cur[i] - bg_[i]) > (float)p_.pixelThreshold) {
                mask[i] = 1;
                ++changed;
            }
        }
        const double pct = 100.0 * (double)changed / (double)n;
        if (areaOut) *areaOut = pct;

        if (warmup_ > 0) { // 노출 안정화 중: 배경을 빠르게 따라간다
            --warmup_;
            for (size_t i = 0; i < n; ++i) bg_[i] += 0.5f * ((float)cur[i] - bg_[i]);
            consec_ = 0;
            return false;
        }

        if (pct >= p_.lightingPercent) { // 조명/노출 급변: 침입이 아니라 장면 전체 변화
            resetBackground(cur);
            warmup_ = 3;
            consec_ = 0;
            return false;
        }

        if (pct >= p_.areaPercent) ++consec_;
        else consec_ = 0;

        if (consec_ >= p_.confirmFrames && nowMs >= nextAllowedMs_) {
            nextAllowedMs_ = nowMs + (ULONGLONG)p_.cooldownSec * 1000ULL;
            consec_ = 0;
            resetBackground(cur); // 새 장면을 배경으로 받아들여 같은 변화로 반복 탐지되지 않게 함
            return true;
        }

        // 배경 갱신: 변화 없는 픽셀은 정상 속도, 변한 픽셀은 아주 느리게 (침입자가 배경에 녹지 않도록)
        const float slow = p_.alpha * 0.1f;
        for (size_t i = 0; i < n; ++i)
            bg_[i] += (mask[i] ? slow : p_.alpha) * ((float)cur[i] - bg_[i]);
        return false;
    }

private:
    static std::vector<BYTE> blur(const std::vector<BYTE>& g, int w, int h)
    {
        std::vector<BYTE> out(g.size());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned sum = 0, cnt = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= w) continue;
                        sum += g[(size_t)yy * w + xx];
                        ++cnt;
                    }
                }
                out[(size_t)y * w + x] = (BYTE)(sum / cnt);
            }
        }
        return out;
    }

    void resetBackground(const std::vector<BYTE>& cur)
    {
        bg_.assign(cur.begin(), cur.end());
    }

    Params p_;
    std::vector<float> bg_;
    int w_ = 0, h_ = 0;
    int warmup_ = 0;
    int consec_ = 0;
    ULONGLONG nextAllowedMs_ = 0;
};

// ================================================================ ImageWindow
// 폰에서 받은 이미지를 PC 화면에 보여 주는 창. 이미지 1장마다 창 1개(전용 스레드 + 메시지 루프)를 만든다.
// 창은 화면의 80% 안에 맞춰 열리고 크기 조절 가능(비율 유지). Esc 또는 닫기 버튼으로 닫는다.
class ImageWindow {
public:
    // 이미지 바이트(jpg/png/bmp/gif/tif)를 디코딩해 새 창으로 띄운다. 디코딩에 실패하면 false.
    static bool show(const std::vector<BYTE>& data, const std::string& title)
    {
        int w = 0, h = 0;
        HBITMAP hb = decode(data, w, h);
        if (!hb) return false;
        Ctx* c = new Ctx{hb, w, h, StrUtil::toWide(title), (int)InterlockedIncrement(&counter_)};
        HANDLE t = CreateThread(nullptr, 0, threadProc, c, 0, nullptr);
        if (!t) {
            DeleteObject(hb);
            delete c;
            return false;
        }
        CloseHandle(t);
        return true;
    }

private:
    struct Ctx {
        HBITMAP bmp;
        int w, h;
        std::wstring title;
        int slot; // 창이 정확히 겹치지 않도록 위치를 조금씩 어긋나게 하는 번호
    };
    static inline LONG counter_ = 0;
    static constexpr const wchar_t* kClass = L"ScreenTelegramImageView";

    // GDI+ 로 디코딩해 HBITMAP 으로 돌려준다 (EXIF 방향 반영, 투명은 흰색).
    static HBITMAP decode(const std::vector<BYTE>& data, int& w, int& h)
    {
        ImageEncoder gdi; // GDI+ 세션 (다른 곳의 세션과 독립적으로 시작/종료해도 안전)
        if (!gdi.ok() || data.empty()) return nullptr;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!hg) return nullptr;
        void* p = GlobalLock(hg);
        if (!p) { GlobalFree(hg); return nullptr; }
        memcpy(p, data.data(), data.size());
        GlobalUnlock(hg);
        IStream* st = nullptr;
        if (CreateStreamOnHGlobal(hg, TRUE, &st) != S_OK) { GlobalFree(hg); return nullptr; }
        HBITMAP hb = nullptr;
        {
            Gdiplus::Bitmap bmp(st);
            if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
                const UINT sz = bmp.GetPropertyItemSize(PropertyTagOrientation);
                if (sz >= sizeof(Gdiplus::PropertyItem)) {
                    std::vector<BYTE> buf(sz);
                    auto* pi = reinterpret_cast<Gdiplus::PropertyItem*>(buf.data());
                    if (bmp.GetPropertyItem(PropertyTagOrientation, sz, pi) == Gdiplus::Ok &&
                        pi->type == PropertyTagTypeShort && pi->length >= 2) {
                        switch (*(const WORD*)pi->value) {
                        case 2: bmp.RotateFlip(Gdiplus::RotateNoneFlipX); break;
                        case 3: bmp.RotateFlip(Gdiplus::Rotate180FlipNone); break;
                        case 4: bmp.RotateFlip(Gdiplus::RotateNoneFlipY); break;
                        case 5: bmp.RotateFlip(Gdiplus::Rotate90FlipX); break;
                        case 6: bmp.RotateFlip(Gdiplus::Rotate90FlipNone); break;
                        case 7: bmp.RotateFlip(Gdiplus::Rotate270FlipX); break;
                        case 8: bmp.RotateFlip(Gdiplus::Rotate270FlipNone); break;
                        default: break;
                        }
                    }
                }
                w = (int)bmp.GetWidth();
                h = (int)bmp.GetHeight();
                if (bmp.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hb) != Gdiplus::Ok) hb = nullptr;
            }
        } // bmp 를 스트림보다 먼저 해제
        st->Release();
        return hb;
    }

    static LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
    {
        Ctx* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
        switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
            break;
        case WM_ERASEBKGND:
            return 1; // WM_PAINT 에서 전체를 칠한다 (깜빡임 방지)
        case WM_SIZE:
            InvalidateRect(hw, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(hw);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hw, &ps);
            RECT rc;
            GetClientRect(hw, &rc);
            FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (c && c->bmp && c->w > 0 && c->h > 0 && rc.right > 0 && rc.bottom > 0) {
                const double sc = std::min((double)rc.right / c->w, (double)rc.bottom / c->h);
                const int dw = std::max(1, (int)(c->w * sc)), dh = std::max(1, (int)(c->h * sc));
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(mem, c->bmp);
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, nullptr);
                StretchBlt(dc, (rc.right - dw) / 2, (rc.bottom - dh) / 2, dw, dh, mem, 0, 0, c->w, c->h, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            EndPaint(hw, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hw, msg, wp, lp);
    }

    static DWORD WINAPI threadProc(LPVOID param)
    {
        std::unique_ptr<Ctx> c(static_cast<Ctx*>(param));
        const HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // 32512 = IDC_ARROW
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc); // 이미 등록돼 있으면 실패하지만 그대로 쓰면 된다

        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        const double sc = std::min(1.0, std::min((wa.right - wa.left) * 0.8 / c->w, (wa.bottom - wa.top) * 0.8 / c->h));
        RECT r = {0, 0, std::max(200, (int)(c->w * sc)), std::max(150, (int)(c->h * sc))};
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
        const int off = (c->slot % 10) * 32;
        HWND hw = CreateWindowExW(0, kClass, c->title.c_str(), WS_OVERLAPPEDWINDOW,
                                  wa.left + 40 + off, wa.top + 40 + off, r.right - r.left, r.bottom - r.top,
                                  nullptr, nullptr, inst, c.get());
        if (hw) {
            ShowWindow(hw, SW_SHOW);
            // 다른 창 뒤에 숨지 않도록 잠깐 최상위로 올렸다가 되돌린다 (계속 최상위로 남기지는 않음)
            SetWindowPos(hw, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowPos(hw, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(hw);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        DeleteObject(c->bmp);
        return 0;
    }
};

// ================================================================ ScreenTelegramApp
// 스레드 구성:
//   침입 탐지 스레드: 카메라 프레임 분석 -> 침입 확정 시 알림 메시지 + 카메라 이미지 저장·전송을 스스로 수행.
//                  (메인 주기 캡처, /pause, /shot, 전송 재시도 등과 무관하게 독립 동작. 카메라는 이 스레드만 접근하고
//                   메인 스레드는 이 스레드가 공유하는 최신 프레임을 받아 쓴다)
//   메인 스레드   : 주기 캡처/저장/전송 (GDI+, 카메라 사용), /shot 처리
//   명령 스레드   : 텔레그램 getUpdates 롱폴링. /pause /resume /status /stop /help 는 즉시 처리하고,
//                  /shot 은 메인 스레드에 요청만 전달한다. (캡처/전송 중에도 명령에 바로 반응)
class ScreenTelegramApp {
public:
    ScreenTelegramApp()
    {
        InitializeCriticalSection(&cs_);
        InitializeCriticalSection(&ioCs_);
        shotEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // 자동 리셋
    }
    ~ScreenTelegramApp()
    {
        if (shotEvent_) CloseHandle(shotEvent_);
        DeleteCriticalSection(&cs_);
        DeleteCriticalSection(&ioCs_);
    }
    NO_COPY(ScreenTelegramApp);

    int run()
    {
        // 고DPI 환경에서도 실제 해상도로 캡처
        SetProcessDPIAware();

        Logger::log("==================== 프로그램 시작 ====================");
        Logger::log("screen_telegram v%s (빌드 %s %s), PID %lu", APP_VERSION_STR, __DATE__, __TIME__,
                    (unsigned long)GetCurrentProcessId());
        Logger::log("exe 폴더: %s", SysUtil::exeDir().c_str());
        const std::string ini = resolveIniPath();
        iniPath_ = ini; // /INTERVAL=nnn 이 값을 저장할 때 사용
        if (!cfg_.load(ini)) {
            Logger::log("INI 파일을 열 수 없습니다: %s", ini.c_str());
            return 1;
        }
        if (cfg_.previousSessionClear) {
            // 시작 직후(INI 를 읽은 뒤, 캡처/명령 수신 전)에 이전 세션의 흔적을 지운다.
            const bool logOk = Logger::clearFile();
            size_t failed = 0;
            const size_t n = SysUtil::clearFolderFiles(imageDir_, failed);
            Logger::log("==================== 프로그램 시작 (이전 세션 정리 후) ====================");
            Logger::log("screen_telegram v%s, PID %lu", APP_VERSION_STR, (unsigned long)GetCurrentProcessId());
            Logger::log("PREVIOUS_SESSION_CLEAR=TRUE: log.txt %s, image 폴더 파일 %zu개 삭제%s",
                        logOk ? "삭제" : "삭제 실패", n,
                        failed ? (", 삭제 실패 " + std::to_string(failed) + "개").c_str() : "");
        }
        if (cfg_.token.empty() || cfg_.chatId.empty()) {
            if (!cfg_.saveOnly) {
                Logger::log("TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 INI 에 없습니다.");
                return 1;
            }
            if (cfg_.commands) { // SAVE_ONLY 는 토큰 없이도 실행 가능, 대신 명령 수신은 불가
                cfg_.commands = false;
                Logger::log("TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 없어 명령 수신은 사용하지 않습니다.");
            }
        }
        Logger::log("설정 로드: %s (간격 %d초, 카메라 %s, 반복 %s)", ini.c_str(), cfg_.interval,
                    cfg_.cameraEnabled ? "사용" : "사용안함",
                    cfg_.maxLoop > 0 ? std::to_string(cfg_.maxLoop).c_str() : "무제한");
        if (cfg_.saveOnly)
            Logger::log("SAVE_ONLY=TRUE: 이미지는 텔레그램으로 보내지 않고 image 폴더에 저장만 합니다. (/shot 제외)");
        if (cfg_.keepDays > 0 || cfg_.keepMax > 0)
            Logger::log("image 폴더 자동 정리: %d일 보관, 최대 %d개 (0 = 제한 없음)",
                        cfg_.keepDays, cfg_.keepMax);
        else
            Logger::log("image 폴더 자동 정리 안 함 (KEEP_DAYS / KEEP_MAX_FILES 로 설정 가능)");
        for (const auto& kv : cfg_.monitorEnabled)
            Logger::log("  MONITOR%d = %s", kv.first, kv.second ? "TRUE" : "FALSE");

        // 선언 순서의 역순으로 해제된다: 소스(MFShutdown) -> 전송 -> GDI+ -> COM
        ComScope com;
        ImageEncoder encoder;
        if (!encoder.ok()) {
            Logger::log("GDI+ 초기화 실패");
            return 1;
        }
        TelegramClient telegram(cfg_.token, cfg_.chatId, stop_);
        encoder_ = &encoder;
        telegram_ = &telegram;

        // 텔레그램 명령 수신 (SAVE_ONLY=TRUE 여도 폴링은 계속한다)
        std::unique_ptr<CommandListener> listener;
        if (cfg_.commands) {
            listener = std::make_unique<CommandListener>(telegram, cfg_.chatId, cfg_.allowedUsers);
            listener->skipPending();
            Logger::log("텔레그램 명령 수신 활성: /shot /screen /photo /pause /resume /status /detectOn /detectOff /INTERVAL=nnn /ini /log /CMD /version /stop /help");
            if (cfg_.allowedUsers.empty() && StrUtil::trim(cfg_.chatId)[0] == '-')
                Logger::log("경고: TELEGRAM_CHAT_ID 가 그룹입니다. TELEGRAM_ALLOWED_USER_IDS 를 설정하지 않으면 명령이 거부됩니다.");
        }

        std::vector<std::unique_ptr<ICaptureSource>> sources;
        sources.push_back(std::make_unique<MonitorCapture>(cfg_));
        if (cfg_.cameraEnabled) {
            auto cam = std::make_unique<CameraCapture>(cfg_);
            if (cam->start()) { // 실패해도 앱은 계속 실행
                camera_ = cam.get();
                sources.push_back(std::move(cam));
            }
        }
        cameraReady_ = (camera_ != nullptr);

        // 침입 탐지: INI 스위치 대신 텔레그램 /detectOn /detectOff 로 켜고 끈다.
        // 켜져 있는 동안 카메라를 상시 열어 두고 INTRUSION_CHECK_MS 마다 프레임을 검사한다.
        // 마지막 상태는 detect.state 파일에 저장되어 재시작 후에도 이어진다 (파일이 없으면 꺼짐).
        detParams_.pixelThreshold = cfg_.intrusionPixelThr;
        detParams_.areaPercent = cfg_.intrusionAreaPct;
        detParams_.confirmFrames = cfg_.intrusionConfirm;
        detParams_.cooldownSec = cfg_.intrusionCooldown;
        // 시작 시 켜지는 조건: AUTO_DETECT=TRUE (매번 /detectOn 자동 수행) 이거나, 마지막 상태가 '켜짐'.
        if (camera_) {
            if (cfg_.autoDetect) {
                if (!applyDetect(true)) // /detectOn 과 동일: 켜고 상태 저장
                    Logger::log("detect.state 저장 실패 (침입 탐지는 켜집니다)");
                Logger::log("침입 탐지: AUTO_DETECT=TRUE 이므로 /detectOn 을 자동 수행했습니다. (/detectOff 로 끄기)");
            } else if (loadDetectState()) {
                InterlockedExchange(&detectOn_, 1);
                Logger::log("침입 탐지: 이전 상태가 '켜짐' 이므로 시작합니다. (/detectOff 로 끄기)");
            } else {
                Logger::log("침입 탐지: 꺼짐 (텔레그램 /detectOn 으로 켜기, 또는 INI 에 AUTO_DETECT=TRUE)");
            }
        } else if (cfg_.autoDetect || loadDetectState()) {
            Logger::log("침입 탐지를 켜도록 설정돼 있지만 카메라를 사용할 수 없어 사용하지 않습니다. (CAMERA=TRUE 및 카메라 연결 필요)");
        }

        stop_.install();
        startTick_ = GetTickCount64();
        if (listener) startWorker(*listener, telegram);
        sendGreeting(telegram); // 준비 완료 인사말 (1회)
        if (camera_) startIntrusion(); // 침입 탐지 스레드 시작 (꺼져 있으면 대기만 함, 다른 기능과 독립)

        // 실행 간격 유지 (처리 시간만큼 보정). 종료 요청이 오면 대기 중에도 즉시 탈출.
        while (!stop_.requested()) {
            ULONGLONG start = GetTickCount64();
            periodStart_ = start; // /INTERVAL 변경 시 남은 대기 시간을 다시 계산하는 기준

            cycle(sources, encoder, telegram);

            // MAX_LOOP 횟수만큼 실행했으면 마지막 대기 없이 바로 종료
            LONG done = InterlockedIncrement(&loopCount_);
            if (cfg_.maxLoop > 0 && done >= cfg_.maxLoop) {
                Logger::log("MAX_LOOP=%d 회 완료", cfg_.maxLoop);
                if (detectOn_) { // 침입 탐지는 독립 기능이므로 반복이 끝나도 계속 동작
                    Logger::log("침입 탐지가 켜져 있어 계속 실행합니다. (/stop 또는 Ctrl+C 로 종료)");
                    while (idle(3600000, sources, encoder, telegram)) {} // /shot 등 명령은 계속 처리
                }
                break;
            }

            ULONGLONG elapsed = GetTickCount64() - start;
            ULONGLONG period = (ULONGLONG)cfg_.interval * 1000ULL;
            DWORD wait = elapsed < period ? (DWORD)(period - elapsed) : 0;
            if (!idle(wait, sources, encoder, telegram, true)) break;
        }

        stop_.request(); // 명령/침입 탐지 스레드도 종료시킨다
        stopWorker();
        stopIntrusion();
        Logger::log("종료합니다.");
        return 0;
    }

private:
    struct WorkerCtx {
        ScreenTelegramApp* app = nullptr;
        CommandListener* listener = nullptr;
        const TelegramClient* telegram = nullptr;
    };

    static DWORD WINAPI workerThunk(LPVOID p)
    {
        auto* c = static_cast<WorkerCtx*>(p);
        c->app->commandLoop(*c->listener, *c->telegram);
        return 0;
    }

    void startWorker(CommandListener& listener, const TelegramClient& telegram)
    {
        ctx_.app = this;
        ctx_.listener = &listener;
        ctx_.telegram = &telegram;
        worker_ = CreateThread(nullptr, 0, workerThunk, &ctx_, 0, nullptr);
        if (!worker_) Logger::log("명령 수신 스레드 생성 실패 - 명령을 사용할 수 없습니다.");
    }

    void stopWorker()
    {
        if (!worker_) return;
        // 롱폴링(최대 kPollSec 초) 중이면 끝날 때까지 잠시 기다린다. 너무 길면 강제 종료 (어차피 프로세스 종료 중)
        if (WaitForSingleObject(worker_, 20000) == WAIT_TIMEOUT) {
            Logger::log("명령 수신 스레드 종료 지연 - 강제 종료");
            TerminateThread(worker_, 0);
        }
        CloseHandle(worker_);
        worker_ = nullptr;
    }

    static std::string resolveIniPath()
    {
        // 인자는 UTF-16 로 받아 UTF-8 로 변환 (한글 경로 대비)
        std::string arg;
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv) {
            if (wargc >= 2) arg = StrUtil::toUtf8(wargv[1]);
            LocalFree(wargv);
        }
        if (!arg.empty()) return arg;
        std::string p1 = SysUtil::exeDir() + "\\screen_telegram.ini";
        return SysUtil::fileExists(p1) ? p1 : "screen_telegram.ini";
    }

    // 시작 준비가 끝났음을 텔레그램으로 알린다 (프로그램 시작당 1회). 부팅 직후 네트워크가 늦을 수 있어 최대 3회 시도.
    void sendGreeting(const TelegramClient& telegram)
    {
        if (!cfg_.greeting || cfg_.token.empty() || cfg_.chatId.empty()) return;

        const std::string host = SysUtil::hostName();
        std::string text = "안녕하세요! [" + host + "] screen_telegram v" + APP_VERSION_STR + " 준비가 완료되었습니다.\n" +
                           "간격: " + std::to_string(cfg_.interval) + "초 / 카메라: " +
                           (cameraReady_ ? "사용" : "사용안함") + " / 침입 탐지: " +
                           (detectOn_ ? "켜짐" : "꺼짐") + "\n" +
                           "시작 시각: " + SysUtil::timestamp();
        if (cfg_.commands)
            text += "\n명령 목록은 /help 로 확인하세요. (침입 탐지: /detectOn /detectOff)";

        std::string resp;
        for (int attempt = 1; attempt <= 3; ++attempt) {
            if (telegram.sendMessage(text, resp)) {
                Logger::log("시작 인사말 전송 완료");
                return;
            }
            Logger::log("시작 인사말 전송 실패 (%d/3): %s", attempt, resp.substr(0, 120).c_str());
            if (attempt < 3 && stop_.wait(3000)) return;
        }
    }

    // ---- 명령 스레드 ----
    void commandLoop(CommandListener& listener, const TelegramClient& telegram)
    {
        while (!stop_.requested()) {
            std::vector<BotEvent> events;
            if (!listener.poll(kPollSec, events)) {
                // 통신 실패: 바로 재시도하면 CPU/네트워크를 낭비하므로 잠시 대기
                if (stop_.wait(5000)) break;
                continue;
            }
            for (const auto& ev : events) {
                if (stop_.requested()) break;
                if (ev.isCallback()) handleCallback(ev, telegram);
                else if (ev.isFile()) handleUpload(ev, telegram);
                else handleCommand(ev.cmd, ev.arg, telegram);
            }
        }
    }

    // 메인 스레드에서 ms 동안 대기한다. 명령 스레드의 캡처 요청(/shot /screen /photo)이 오면 즉시 처리하고
    // 남은 시간만큼 계속 대기.
    // followInterval=true 이면 대기 중 /INTERVAL 로 간격이 바뀌었을 때 (주기 시작 시각 + 새 간격)으로 끝나는 시각을 다시 계산한다.
    // 종료 요청이 오면 false.
    bool idle(DWORD ms, std::vector<std::unique_ptr<ICaptureSource>>& sources,
              const ImageEncoder& encoder, const TelegramClient& telegram,
              bool followInterval = false)
    {
        ULONGLONG end = GetTickCount64() + ms;
        HANDLE h[2] = { stop_.handle(), shotEvent_ };
        for (;;) {
            ULONGLONG now = GetTickCount64();
            DWORD remain = now >= end ? 0 : (DWORD)(end - now);
            DWORD r = WaitForMultipleObjects(2, h, FALSE, remain);
            if (r == WAIT_OBJECT_0) return false;                 // 종료 요청
            if (r == WAIT_OBJECT_0 + 1) {                         // 캡처 요청
                LONG mask = InterlockedExchange(&shotMask_, 0);
                if (mask) {
                    CaptureFilter f = mask == 3 ? CaptureFilter::All
                                    : (mask == 1 ? CaptureFilter::ScreenOnly : CaptureFilter::CameraOnly);
                    Logger::log("즉시 캡처 처리 (%s)",
                                f == CaptureFilter::All ? "화면+카메라"
                                : (f == CaptureFilter::ScreenOnly ? "화면만" : "카메라만"));
                    cycle(sources, encoder, telegram, true, f); // 중단 상태여도 전송, 반복 횟수에는 포함 안 함
                }
                if (followInterval && InterlockedExchange(&intervalChanged_, 0)) {
                    end = periodStart_ + (ULONGLONG)cfg_.interval * 1000ULL; // 이미 지났으면 바로 다음 캡처
                    Logger::log("캡처 간격 변경 반영: %d초", (int)cfg_.interval);
                }
                continue;
            }
            if (r == WAIT_FAILED) return !stop_.wait(remain);

            return true; // 시간 경과
        }
    }

    // ---- 침입 탐지 스레드 ----
    static DWORD WINAPI intrusionThunk(LPVOID p)
    {
        static_cast<ScreenTelegramApp*>(p)->intrusionLoop();
        return 0;
    }

    void startIntrusion()
    {
        intrusionThread_ = CreateThread(nullptr, 0, intrusionThunk, this, 0, nullptr);
        if (!intrusionThread_) Logger::log("침입 탐지 스레드 생성 실패 - 침입 탐지를 사용할 수 없습니다.");
        else Logger::log("침입 탐지 스레드 시작");
    }

    void stopIntrusion()
    {
        if (!intrusionThread_) return;
        // 진행 중인 전송/프레임 읽기가 끝나도록 충분히 기다린다 (정상이면 즉시 끝남)
        if (WaitForSingleObject(intrusionThread_, 60000) == WAIT_TIMEOUT) {
            Logger::log("침입 탐지 스레드 종료 지연 - 강제 종료");
            TerminateThread(intrusionThread_, 0);
        }
        CloseHandle(intrusionThread_);
        intrusionThread_ = nullptr;
    }

    // 카메라 프레임을 INTRUSION_CHECK_MS 간격으로 분석한다. 침입이 확정되면 이 스레드가 직접
    // 알림 메시지를 보내고 카메라 이미지(탐지된 그 프레임)를 저장·전송한다 (메인 스레드 상태와 무관).
    void intrusionLoop()
    {
        // 카메라 장치 열거가 STA 에서 안정적이므로 이 스레드도 STA 로 초기화
        const bool comOk = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        std::vector<BYTE> raw, gray;
        LONG stride = 0;
        int gw = 0, gh = 0;
        bool failing = false;
        bool active = false; // 이 스레드가 카메라를 잡고 탐지 중인지

        while (!stop_.requested()) {
            // /detectOff 상태: 카메라를 놓고(메인 스레드가 필요할 때만 직접 열도록) 켜질 때까지 대기
            if (!detectOn_) {
                if (active) {
                    camera_->setSharedMode(false);
                    active = false;
                    failing = false;
                    Logger::log("침입 탐지 중지 (카메라 해제)");
                }
                if (stop_.wait(300)) break;
                continue;
            }
            // /detectOn 으로 켜진 직후: 탐지기를 새로 만들고(배경 재학습) 카메라 전담 모드로 전환
            if (!active) {
                detector_ = MotionDetector(detParams_);
                camera_->setSharedMode(true);
                active = true;
                Logger::log("침입 탐지 시작: 검사 %dms, 픽셀 임계 %d, 면적 %.2f%%, 연속 %d프레임, 쿨다운 %d초",
                            cfg_.intrusionCheckMs, detParams_.pixelThreshold, detParams_.areaPercent,
                            detParams_.confirmFrames, detParams_.cooldownSec);
            }

            const ULONGLONG t0 = GetTickCount64();
            std::string err;
            if (!camera_->grabFrame(raw, stride, err)) {
                if (!failing) { // 같은 오류 반복 로그 방지
                    failing = true;
                    Logger::log("침입 탐지용 카메라 프레임 읽기 실패: %s", err.c_str());
                }
                if (stop_.wait(2000)) break; // 잠시 후 다시 열어 본다
                continue;
            }
            if (failing) {
                failing = false;
                Logger::log("침입 탐지 카메라 복구");
            }

            double area = 0.0;
            if (camera_->makeGray(raw, stride, gray, gw, gh) &&
                detector_.update(gray, gw, gh, t0, &area))
                onIntrusion(area, raw, stride);

            const ULONGLONG el = GetTickCount64() - t0;
            const DWORD wait = el < (ULONGLONG)cfg_.intrusionCheckMs
                                   ? (DWORD)((ULONGLONG)cfg_.intrusionCheckMs - el) : 0;
            if (stop_.wait(wait)) break;
        }

        if (active) camera_->setSharedMode(false); // 스레드에서 연 카메라는 이 스레드에서 정리
        if (comOk) CoUninitialize();
    }

    // 침입 확정: 알림 메시지 -> 카메라 이미지만 저장/전송 (모니터 화면 캡처는 하지 않음)
    // (SAVE_ONLY, /pause 와 무관하게 무조건 전송). 카메라 이미지는 침입을 탐지한 바로 그 프레임을 사용한다.
    void onIntrusion(double area, const std::vector<BYTE>& raw, LONG stride)
    {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        char text[128];
        snprintf(text, sizeof(text), "[침입탐지 : %04d년 %02d월 %02d일 %02d시 %02d분 %02d초]",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        Logger::log("%s (변화 영역 %.1f%%)", text, area);

        if (!cfg_.token.empty() && !cfg_.chatId.empty()) {
            std::string resp;
            for (int attempt = 1; attempt <= 3; ++attempt) {
                if (telegram_->sendMessage(text, resp)) break;
                Logger::log("침입 알림 메시지 전송 실패 (%d/3): %s", attempt, resp.substr(0, 120).c_str());
                if (stop_.wait(2000)) return;
            }
        }

        std::vector<Shot> shots;
        HBITMAP hb = camera_->makeBitmap(raw, stride);
        if (hb) shots.emplace_back("Camera", "Camera", hb);
        emitShots(shots, *encoder_, *telegram_, true, SysUtil::hostName(), SysUtil::timestamp(),
                  "Intrusion_", "침입탐지 ");
    }

    // 명령 스레드에서 호출됨. /shot 은 메인 스레드로 넘긴다.
    void handleCommand(const std::string& cmd, const std::string& arg, const TelegramClient& telegram)
    {
        Logger::log("명령 수신: %s", cmd.c_str());
        const std::string host = SysUtil::hostName();
        std::string resp;

        if (cmd == "/shot" || cmd == "/screen" || cmd == "/photo") {
            const LONG mask = cmd == "/shot" ? 3 : (cmd == "/screen" ? 1 : 2); // 1=화면 2=카메라
            if (cmd == "/photo" && !cameraReady_) {
                telegram.sendMessage("[" + host + "] 카메라를 사용할 수 없습니다. "
                                     "(CAMERA=TRUE 설정과 카메라 연결을 확인하세요)", resp);
            } else {
                InterlockedOr(&shotMask_, mask);
                SetEvent(shotEvent_);
            }
        } else if (cmd == "/status") {
            ULONGLONG sec = (GetTickCount64() - startTick_) / 1000;
            const std::string maxLoopStr =
                cfg_.maxLoop > 0 ? std::to_string(cfg_.maxLoop) : std::string("무제한");
            const std::string last = getLastCycle();
            char keep[96];
            if (cfg_.keepDays > 0 || cfg_.keepMax > 0)
                snprintf(keep, sizeof(keep), "%d일 / 최대 %d개 (0=제한 없음)", cfg_.keepDays, cfg_.keepMax);
            else
                snprintf(keep, sizeof(keep), "자동 정리 안 함");
            char buf[800];
            snprintf(buf, sizeof(buf),
                     "[%s] 실행 중 (v" APP_VERSION_STR ")\n가동 시간: %llu일 %02llu:%02llu:%02llu\n간격: %d초\n"
                     "반복: %d / %s\n이미지 전송: %s\n카메라: %s\n이미지 보관: %s\n침입 탐지: %s\n마지막 캡처: %s",
                     host.c_str(), sec / 86400, (sec / 3600) % 24, (sec / 60) % 60, sec % 60,
                     cfg_.interval, (int)loopCount_, maxLoopStr.c_str(),
                     cfg_.saveOnly ? "안 함 (SAVE_ONLY=TRUE, /shot 만 전송)"
                                   : (paused_ ? "중단됨 (/resume 으로 재개)" : "진행 중"),
                     cfg_.cameraEnabled ? "사용" : "사용안함", keep,
                     detectOn_ ? "켜짐 (/detectOff 로 끄기)" : "꺼짐 (/detectOn 으로 켜기)",
                     last.empty() ? "-" : last.c_str());
            telegram.sendMessage(buf, resp);
        } else if (cmd == "/detecton" || cmd == "/detectoff") {
            // 명령은 소문자로 정규화되어 들어온다 (/detectOn -> /detecton)
            const bool on = (cmd == "/detecton");
            if (!camera_) {
                telegram.sendMessage("[" + host + "] 카메라를 사용할 수 없어 침입 탐지를 켜거나 끌 수 없습니다. "
                                     "(CAMERA=TRUE 설정과 카메라 연결을 확인하세요)", resp);
            } else if ((detectOn_ != 0) == on) {
                telegram.sendMessage("[" + host + "] 침입 탐지는 이미 " + (on ? "켜져" : "꺼져") + " 있습니다.", resp);
            } else {
                const bool saved = applyDetect(on);
                std::string msg = "[" + host + "] 침입 탐지를 " + (on ? "켰습니다." : "껐습니다.");
                if (on)
                    msg += " (카메라를 상시 사용하므로 다른 앱은 카메라를 쓸 수 없습니다. 끄려면 /detectOff)";
                else
                    msg += " (카메라를 해제합니다. 다시 켜려면 /detectOn)";
                if (!saved)
                    msg += "\n주의: 상태 파일(detect.state) 저장에 실패해 재시작하면 이전 상태로 돌아갑니다.";
                telegram.sendMessage(msg, resp);
                Logger::log("침입 탐지 %s (텔레그램 명령)", on ? "켬" : "끔");
            }
        } else if (cmd == "/version") {
            telegram.sendMessage("[" + host + "] screen_telegram v" + APP_VERSION_STR +
                                 "\n빌드: " + __DATE__ + " " + __TIME__, resp);
        } else if (cmd == "/log") {
            handleLogCmd(arg, host, telegram);
        } else if (cmd == "/homeback") { // 명령은 소문자로 정규화되어 들어온다 (/HomeBack -> /homeback)
            telegram.sendMessage("[집에 돌아 오신 것을 환영합니다. 푹 쉬세요]", resp);
            Logger::log("/HomeBack: 귀가 인사 전송 -> /detectOff 자동 처리");
            handleCommand("/detectoff", "", telegram); // 인사말 뒤에 침입 탐지 끄기 (카메라 없음/이미 꺼짐이면 그 안내 메시지가 나감)
        } else if (cmd == "/nothome") { // /NotHome -> /nothome
            telegram.sendMessage("안녕히 다녀오세요. 제가 집을 잘 지키고 있겠습니다.", resp);
            Logger::log("/NotHome: 외출 인사 전송 -> /detectOn 자동 처리");
            handleCommand("/detecton", "", telegram); // 인사말 뒤에 침입 탐지 켜기 (카메라 없음/이미 켜짐이면 그 안내 메시지가 나감)
        } else if (cmd == "/download") {
            handleDownload(host, telegram);
        } else if (cmd == "/cmd") {
            handleCmdCmd(arg, host, telegram);
        } else if (cmd == "/ini") {
            handleIni(arg, host, telegram);
        } else if (cmd == "/interval" || cmd.rfind("/interval=", 0) == 0) {
            // /INTERVAL=nnn : 캡처/전송 간격(초) 변경. 명령은 소문자로 정규화되어 들어온다 (/INTERVAL=300 -> /interval=300).
            // 값을 바로 적용하고 INI 의 TELEGRAM_INTERVAL 도 갱신한다. "/INTERVAL" 만 보내면 현재 값과 사용법을 알려 준다.
            const size_t eq = cmd.find('=');
            if (eq == std::string::npos) {
                telegram.sendMessage("[" + host + "] 현재 캡처 간격: " + std::to_string((int)cfg_.interval) +
                                     "초\n사용법: /INTERVAL=nnn (" + std::to_string(kMinInterval) + "~" +
                                     std::to_string(kMaxInterval) + "초)", resp);
            } else {
                const std::string num = cmd.substr(eq + 1);
                const bool digits = !num.empty() && num.size() <= 6 &&
                                    num.find_first_not_of("0123456789") == std::string::npos;
                const long v = digits ? atol(num.c_str()) : 0;
                if (!digits || v < kMinInterval || v > kMaxInterval) {
                    telegram.sendMessage("[" + host + "] 잘못된 값입니다: " + num + "\n사용법: /INTERVAL=nnn (" +
                                         std::to_string(kMinInterval) + "~" + std::to_string(kMaxInterval) +
                                         "초, 숫자만)", resp);
                } else if ((int)v == (int)cfg_.interval) {
                    telegram.sendMessage("[" + host + "] 캡처 간격은 이미 " + std::to_string(v) + "초입니다.", resp);
                } else {
                    const int old = cfg_.interval;
                    cfg_.interval = (int)v;
                    InterlockedExchange(&intervalChanged_, 1);
                    SetEvent(shotEvent_); // 대기 중인 메인 스레드를 깨워 새 간격으로 남은 시간을 다시 계산
                    const bool saved = SysUtil::updateIniValue(iniPath_, "TELEGRAM_INTERVAL", std::to_string(v));
                    std::string msg = "[" + host + "] 캡처 간격을 " + std::to_string(old) + "초 -> " +
                                      std::to_string(v) + "초로 변경했습니다.";
                    if (!saved)
                        msg += "\n주의: INI 파일 저장에 실패해 재시작하면 이전 값으로 돌아갑니다.";
                    telegram.sendMessage(msg, resp);
                    Logger::log("캡처 간격 변경 %d -> %ld초 (텔레그램 명령, INI %s)", old, v,
                                saved ? "저장됨" : "저장 실패");
                }
            }
        } else if (cmd == "/pause") {
            InterlockedExchange(&paused_, 1); // 캡처/저장은 계속하고 텔레그램 이미지 전송만 중단
            telegram.sendMessage("[" + host + "] 이미지 전송을 중단합니다. "
                                 "(저장은 계속되고, 침입 탐지는 계속 동작합니다. /resume 으로 재개)", resp);
        } else if (cmd == "/resume") {
            InterlockedExchange(&paused_, 0);
            telegram.sendMessage("[" + host + "] " + (cfg_.saveOnly
                ? "SAVE_ONLY=TRUE 이므로 /shot 으로만 이미지를 전송합니다."
                : "이미지 전송을 다시 시작합니다."), resp);
        } else if (cmd == "/stop") {
            // 바로 종료하지 않고 Yes/No 버튼으로 확인한다. 버튼 처리는 handleCallback().
            // 확인 메시지 전송에 실패하면 종료하지 않는다.
            const std::string keyboard =
                "{\"inline_keyboard\":[[{\"text\":\"Yes\",\"callback_data\":\"stop_yes\"},"
                "{\"text\":\"No\",\"callback_data\":\"stop_no\"}]]}";
            if (telegram.sendMessageWithKeyboard("[" + host + "] 종료하시겠습니까?(y/n)", keyboard, resp)) {
                stopMsgId_ = TelegramClient::extractMessageId(resp);
                stopAskTick_ = GetTickCount64();
            } else {
                Logger::log("종료 확인 메시지 전송 실패 - 종료하지 않음: %s", resp.substr(0, 120).c_str());
            }
        } else {
            std::string text = "[" + host + "]\n";
            if (cmd != "/help" && cmd != "/start" && cmd != "//") // "//" 는 /help 와 동일
                text += "알 수 없는 명령: " + cmd + "\n";
            text += "/shot - 지금 화면+카메라 캡처해서 전송\n/screen - 지금 화면만 캡처해서 전송\n"
                    "/photo - 지금 카메라만 캡처해서 전송\n/pause - 이미지 전송 중단\n"
                    "/resume - 이미지 전송 재개\n/status - 상태 확인\n"
                    "/detectOn - 침입 탐지 켜기\n/detectOff - 침입 탐지 끄기\n"
                    "/INTERVAL=nnn - 캡처 간격(초) 변경 (/INTERVAL 만 보내면 현재 값)\n"
                    "/ini 키=값 - INI 에 새 키 추가 (이미 있는 키는 경고만, 재시작 후 적용)\n"
                    "/log [n|all] - 실행 로그 전송 (기본 최근 50줄, 길면 문서로)\n"
                    "/CMD 명령 - 뒤의 문자열을 PC 에서 실행하고 결과를 txt 파일로 전송\n"
                    "/HomeBack - 귀가 인사 전송 후 침입 탐지 끄기\n"
                    "/NotHome - 외출 인사 전송 후 침입 탐지 켜기\n"
                    "/download - PC 에 파일 선택 창을 띄워 고른 파일(여러 개)을 전송\n"
                    "(파일 첨부) - 봇 채팅에 파일을 보내면 PC 의 upload 폴더에 저장 (20MB 까지), 이미지는 창으로 표시, mp4/mp3 는 기본 플레이어로 실행\n"
                    "/version - 버전 표시\n"
                    "/stop - 프로그램 종료 (Yes/No 확인)\n/help 또는 // - 도움말";
            telegram.sendMessage(text, resp);
        }
    }

    // /log [n] : log.txt 의 마지막 n 줄(기본 50, 1~2000)을 보낸다. 텔레그램 메시지 길이(4096자)를 넘으면
    // 텍스트 문서(.txt)로 보낸다. "/log all" 은 log.txt 전체를 문서로 보낸다. (명령 스레드에서 호출)
    void handleLogCmd(const std::string& arg, const std::string& host, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + host + "] ";
        const std::string usage = "\n사용법: /log (최근 " + std::to_string(kDefaultLogLines) +
                                  "줄) / /log n (최근 n줄, 1~" + std::to_string(kMaxLogLines) +
                                  ") / /log all (log.txt 전체, 문서로)";
        const bool all = StrUtil::upper(arg) == "ALL";

        int n = kDefaultLogLines;
        if (!all && !arg.empty()) {
            const bool digits = arg.size() <= 6 && arg.find_first_not_of("0123456789") == std::string::npos;
            const long v = digits ? atol(arg.c_str()) : 0;
            if (!digits || v < 1 || v > kMaxLogLines) {
                telegram.sendMessage(pre + "잘못된 값입니다: " + arg + usage, resp);
                return;
            }
            n = (int)v;
        }

        const std::string text = all ? Logger::tailLines(kMaxLogLines) : Logger::tailLines(n);
        if (text.empty()) {
            telegram.sendMessage(pre + "log.txt 가 없거나 비어 있습니다." + usage, resp);
            return;
        }

        // 메시지로 보내기엔 너무 길면(텔레그램 한도 4096자, 캡션/안내문 여유분 확보) 문서로 전환
        if (!all && text.size() <= 3500) {
            if (!telegram.sendMessage(pre + "log.txt 최근 " + std::to_string(n) + "줄\n\n" + text, resp))
                Logger::log("/log 전송 실패: %s", resp.substr(0, 150).c_str());
            return;
        }

        char fname[64];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(fname, sizeof(fname), "log_%Y%m%d_%H%M%S.txt", &tmv);
        const std::string caption = pre + (all ? "log.txt 전체" : ("log.txt 최근 " + std::to_string(n) + "줄")) +
                                    " (" + std::to_string(text.size() / 1024) + " KB)";
        if (!telegram.sendTextDocument(caption, fname, text, resp)) {
            telegram.sendMessage(pre + "로그 전송 실패: " + resp.substr(0, 200), resp);
            Logger::log("/log 문서 전송 실패: %s", resp.substr(0, 150).c_str());
        }
    }

    // cmd.exe /c 로 명령(UTF-8)을 실행하고 stdout+stderr 를 합쳐 UTF-8 로 out 에 담는다. 표준 입력은 NUL.
    // kCmdTimeoutMs 를 넘으면 하위 프로세스까지 종료(timedOut). 출력이 kCmdMaxOut 을 넘으면 잘라 낸다(truncated).
    // 프로세스를 시작하지 못하면 false (err 에 사유).
    static bool runShell(const std::string& cmdUtf8, std::string& out, DWORD& exitCode,
                         bool& timedOut, bool& truncated, std::string& err)
    {
        out.clear(); exitCode = 0; timedOut = false; truncated = false;

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) { err = "CreatePipe 실패"; return false; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 OPEN_EXISTING, 0, nullptr);

        wchar_t comspec[MAX_PATH] = L"";
        if (!GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) || !comspec[0]) {
            GetSystemDirectoryW(comspec, MAX_PATH);
            wcscat(comspec, L"\\cmd.exe");
        }
        // /s: 바깥쪽 따옴표 한 쌍만 벗기고 안쪽 따옴표는 그대로 둔다
        std::wstring line = L"\"" + std::wstring(comspec) + L"\" /d /s /c \"" + StrUtil::toWide(cmdUtf8) + L"\"";

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nul;
        si.hStdOutput = wr;
        si.hStdError = wr;

        HANDLE job = CreateJobObjectW(nullptr, nullptr); // 시간 초과 시 하위 프로세스까지 종료하기 위함
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> buf(line.begin(), line.end());
        buf.push_back(L'\0');
        const BOOL ok = CreateProcessW(comspec, buf.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
        CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) {
            err = "프로세스 시작 실패 (오류 " + std::to_string(GetLastError()) + ")";
            CloseHandle(rd);
            if (job) CloseHandle(job);
            return false;
        }
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);

        std::string raw;
        const ULONGLONG t0 = GetTickCount64();
        for (;;) {
            // 종료 여부를 먼저 보고 파이프를 비운다 (종료 후 남은 출력까지 읽기 위함)
            const bool exited = WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;
            if (avail > 0) {
                char b[8192];
                DWORD n = 0;
                if (!ReadFile(rd, b, avail < sizeof(b) ? avail : (DWORD)sizeof(b), &n, nullptr) || n == 0) break;
                if (raw.size() < kCmdMaxOut) raw.append(b, n);
                else truncated = true;
                continue;
            }
            if (exited) break;
            if (GetTickCount64() - t0 > kCmdTimeoutMs) {
                timedOut = true;
                if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 3000);
                break;
            }
            Sleep(50);
        }
        if (raw.size() > kCmdMaxOut) { raw.resize(kCmdMaxOut); truncated = true; }

        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(rd);
        if (job) CloseHandle(job);

        // 출력 문자 집합: UTF-8 로 유효하면(chcp 65001 등) 그대로, 아니면 콘솔 OEM 코드 페이지(한국어 Windows 는 CP949)로 변환
        if (raw.empty()) return true;
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0) > 0) {
            out = raw;
        } else {
            const int wn = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), nullptr, 0);
            if (wn > 0) {
                std::wstring w((size_t)wn, L'\0');
                MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), &w[0], wn);
                out = StrUtil::toUtf8(w.c_str(), wn);
            } else {
                out = raw;
            }
        }
        return true;
    }

    // 폰 -> PC : 허용된 chat/사용자가 봇 채팅에 보낸 파일(document/photo/video/audio/voice 등)을 받아 UPLOAD_DIR 에 저장한다.
    // 파일명은 경로/금지 문자/예약어를 정리하고, 같은 이름이 있으면 _1, _2 를 붙이며 덮어쓰지 않는다. (명령 스레드에서 호출)
    static std::wstring safeFileName(const std::string& utf8)
    {
        std::wstring w = StrUtil::toWide(utf8);
        const size_t pos = w.find_last_of(L"\\/");
        if (pos != std::wstring::npos) w = w.substr(pos + 1);
        for (auto& c : w)
            if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) c = L'_';
        if (w.size() > 150) {
            const size_t dot = w.rfind(L'.');
            const std::wstring ext = (dot != std::wstring::npos && w.size() - dot <= 16) ? w.substr(dot) : L"";
            w = w.substr(0, 150 - ext.size()) + ext;
        }
        while (!w.empty() && (w.back() == L' ' || w.back() == L'.')) w.pop_back();
        size_t b = 0;
        while (b < w.size() && w[b] == L' ') ++b;
        w = w.substr(b);
        if (w.empty()) return L"";
        std::wstring stem = w.substr(0, w.find(L'.'));
        for (auto& c : stem) c = (wchar_t)towupper(c);
        bool reserved = stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL";
        if (stem.size() == 4 && (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0) &&
            stem[3] >= L'1' && stem[3] <= L'9')
            reserved = true;
        if (reserved) w = L"_" + w;
        return w;
    }

    // dir\name 으로 새 파일을 만든다 (이미 있으면 name_1.ext, name_2.ext ...). CREATE_NEW 라 덮어쓰지 않는다.
    static bool createUniqueFile(const std::wstring& dir, const std::wstring& name, std::wstring& outPath, HANDLE& h)
    {
        const size_t dot = name.rfind(L'.');
        const bool hasExt = dot != std::wstring::npos && dot > 0;
        const std::wstring stem = hasExt ? name.substr(0, dot) : name;
        const std::wstring ext = hasExt ? name.substr(dot) : L"";
        for (int i = 0; i < 1000; ++i) {
            const std::wstring cand = dir + L"\\" + stem + (i ? L"_" + std::to_wstring(i) : L"") + ext;
            h = CreateFileW(cand.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) { outPath = cand; return true; }
            const DWORD e = GetLastError();
            if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) return false;
        }
        return false;
    }

    // 경로의 확장자가 ext(소문자)와 같은지 (대소문자 무시)
    static bool hasExt(const std::wstring& path, const wchar_t* ext)
    {
        const size_t dot = path.rfind(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring e = path.substr(dot + 1);
        for (auto& c : e) c = (wchar_t)towlower(c);
        return e == ext;
    }

    // 사진(photo)이거나 GDI+ 가 읽는 확장자(jpg/png/bmp/gif/tif)의 파일이면 이미지로 본다.
    static bool isImage(const BotEvent& ev, const std::wstring& path)
    {
        if (ev.fileKind == "photo") return true;
        const size_t dot = path.rfind(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring ext = path.substr(dot + 1);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"bmp" || ext == L"gif" ||
               ext == L"tif" || ext == L"tiff";
    }

    static std::string fmtSize(unsigned long long b)
    {
        char t[32];
        if (b >= 1048576ULL) snprintf(t, sizeof(t), "%.1fMB", (double)b / 1048576.0);
        else snprintf(t, sizeof(t), "%lluKB", (b + 1023) / 1024);
        return t;
    }

    std::string uploadDir() const
    {
        std::string d = StrUtil::trim(cfg_.uploadDir);
        while (!d.empty() && (d.back() == '\\' || d.back() == '/')) d.pop_back();
        if (d.empty()) return SysUtil::exeDir() + "\\upload";
        if (d.size() >= 2 && (d[1] == ':' || (d[0] == '\\' && d[1] == '\\'))) return d; // 절대 경로
        return SysUtil::exeDir() + "\\" + d;
    }

    void handleUpload(const BotEvent& ev, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + SysUtil::hostName() + "] ";
        const std::string label = ev.fileName.empty() ? ev.fileKind : ev.fileName;
        Logger::log("파일 수신: %s (%lld바이트)", label.c_str(), ev.fileSize);

        if (ev.fileSize > (long long)kUploadMaxBytes) {
            telegram.sendMessage(pre + "파일이 너무 커서 받을 수 없습니다: " + label + " (" +
                                 fmtSize((unsigned long long)ev.fileSize) + ", 봇 API 한도 20MB)", resp);
            return;
        }
        std::string filePath;
        if (!telegram.getFilePath(ev.fileId, filePath, resp)) {
            const bool big = resp.find("too big") != std::string::npos;
            telegram.sendMessage(pre + (big ? "파일이 너무 커서 받을 수 없습니다 (봇 API 한도 20MB): "
                                            : "파일 정보를 가져오지 못했습니다: ") +
                                 (big ? label : resp.substr(0, 150)), resp);
            Logger::log("파일 수신 실패(getFile): %s", resp.substr(0, 150).c_str());
            return;
        }
        std::wstring name = safeFileName(ev.fileName);
        if (name.empty()) { // 사진 등 이름이 없으면 시각 + 텔레그램이 정한 이름(예: file_12.jpg)
            const size_t sl = filePath.find_last_of('/');
            name = safeFileName(SysUtil::fileStamp() + "_" + (sl == std::string::npos ? filePath : filePath.substr(sl + 1)));
            if (name.empty()) name = StrUtil::toWide(SysUtil::fileStamp() + ".bin");
        }

        std::vector<BYTE> data;
        std::string err;
        if (!telegram.downloadFile(filePath, data, err)) {
            telegram.sendMessage(pre + "파일을 내려받지 못했습니다: " + label + " (" + err + ")", resp);
            Logger::log("파일 수신 실패(download): %s : %s", label.c_str(), err.c_str());
            return;
        }

        const std::string dir = uploadDir();
        if (!SysUtil::ensureDir(dir)) {
            telegram.sendMessage(pre + "저장 폴더를 만들 수 없습니다: " + dir, resp);
            Logger::log("upload 폴더 생성 실패: %s", dir.c_str());
            return;
        }
        std::wstring outPath;
        HANDLE h = INVALID_HANDLE_VALUE;
        if (!createUniqueFile(StrUtil::toWide(dir), name, outPath, h)) {
            telegram.sendMessage(pre + "파일을 만들 수 없습니다: " + label + " (오류 " + std::to_string(GetLastError()) + ")", resp);
            return;
        }
        bool ok = true;
        size_t put = 0;
        while (put < data.size()) {
            DWORD wr = 0;
            const DWORD want = (DWORD)std::min<size_t>(data.size() - put, 16u * 1024 * 1024);
            if (!WriteFile(h, data.data() + put, want, &wr, nullptr) || wr == 0) { ok = false; break; }
            put += wr;
        }
        CloseHandle(h);
        const std::string saved = StrUtil::toUtf8(outPath.c_str());
        if (!ok) {
            DeleteFileW(outPath.c_str()); // 쓰다 만 파일은 남기지 않는다
            telegram.sendMessage(pre + "파일 저장 실패 (디스크 공간/권한 확인): " + saved, resp);
            Logger::log("파일 저장 실패: %s", saved.c_str());
            return;
        }
        Logger::log("파일 저장: %s (%zu바이트)", saved.c_str(), data.size());
        if (cfg_.uploadShowImage && isImage(ev, outPath)) { // 이미지면 PC 화면에 장당 별개의 창으로 표시
            const size_t sl = outPath.find_last_of(L"\\/");
            const std::string title = StrUtil::toUtf8((sl == std::wstring::npos ? outPath : outPath.substr(sl + 1)).c_str());
            if (!ImageWindow::show(data, title)) Logger::log("이미지 창 표시 실패(지원하지 않는 형식): %s", saved.c_str());
        }
        if (cfg_.uploadPlayMedia && (hasExt(outPath, L"mp4") || hasExt(outPath, L"mp3"))) { // .mp4/.mp3 만 PC 기본 플레이어로 실행 (다른 확장자는 실행하지 않음)
            ComScope com; // ShellExecute 는 COM 초기화된 스레드에서 호출하도록 권장
            const HINSTANCE r = ShellExecuteW(nullptr, L"open", outPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if ((INT_PTR)r > 32) Logger::log("미디어 재생 실행: %s", saved.c_str());
            else Logger::log("미디어 재생 실행 실패(코드 %lld): %s", (long long)(INT_PTR)r, saved.c_str());
        }
        telegram.sendMessage(pre + "저장 완료: " + saved + " (" + fmtSize(data.size()) + ")", resp);
    }

    // /download : PC 화면에 파일 탐색기(열기 창)를 띄워 사용자가 여러 파일을 고르면 하나씩 문서로 전송한다.
    // 창이 떠 있는 동안 명령 스레드가 대기한다 (kDownloadTimeoutMs 안에 고르지 않으면 자동 취소). (명령 스레드에서 호출)
    static UINT_PTR CALLBACK downloadDlgHook(HWND h, UINT msg, WPARAM, LPARAM)
    {
        if (msg == WM_INITDIALOG) {
            HWND dlg = GetParent(h);
            SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); // 다른 창 뒤로 숨지 않게
            SetForegroundWindow(dlg);
            SetTimer(h, 1, kDownloadTimeoutMs, nullptr);
        } else if (msg == WM_TIMER) {
            KillTimer(h, 1);
            PostMessageW(GetParent(h), WM_COMMAND, IDCANCEL, 0);
        }
        return 0;
    }

    // 취소/시간 초과면 false + err 비움, 대화상자 오류면 false + err 설정.
    static bool pickFiles(std::vector<std::wstring>& files, std::string& err)
    {
        files.clear();
        err.clear();
        std::vector<wchar_t> buf(kPickBufChars, L'\0');
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = buf.data();
        ofn.nMaxFile = (DWORD)buf.size();
        ofn.lpstrFilter = L"모든 파일 (*.*)\0*.*\0";
        ofn.lpstrTitle = L"텔레그램으로 보낼 파일 선택 (여러 개 선택 가능)";
        ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                    OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ENABLEHOOK;
        ofn.lpfnHook = downloadDlgHook;
        if (!GetOpenFileNameW(&ofn)) {
            const DWORD e = CommDlgExtendedError();
            if (e) {
                char b[32];
                snprintf(b, sizeof(b), "코드 0x%lX", (unsigned long)e);
                err = b;
            }
            return false;
        }
        // 1개 선택: "전체경로\0\0". 여러 개: "폴더\0파일1\0파일2\0\0"
        const wchar_t* p = buf.data();
        const std::wstring first = p;
        p += first.size() + 1;
        if (*p == L'\0') {
            files.push_back(first);
        } else {
            const std::wstring dir = (!first.empty() && first.back() == L'\\') ? first : first + L"\\";
            while (*p) {
                std::wstring name = p;
                files.push_back(dir + name);
                p += name.size() + 1;
            }
        }
        return !files.empty();
    }

    static bool readWholeFile(const std::wstring& path, std::vector<BYTE>& out, std::string& err)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            err = "파일을 열 수 없음 (오류 " + std::to_string(GetLastError()) + ")";
            return false;
        }
        LARGE_INTEGER sz = {};
        bool ok = false;
        if (!GetFileSizeEx(h, &sz)) {
            err = "파일 크기를 알 수 없음";
        } else if (sz.QuadPart == 0) {
            err = "빈 파일 (텔레그램은 0바이트 파일을 받지 않음)";
        } else if (sz.QuadPart > (LONGLONG)kDownloadMaxBytes) {
            err = "너무 큼 (" + std::to_string(sz.QuadPart / 1024 / 1024) + "MB, 한도 49MB)";
        } else {
            out.assign((size_t)sz.QuadPart, 0);
            size_t got = 0;
            ok = true;
            while (got < out.size()) {
                DWORD rd = 0;
                const DWORD want = (DWORD)std::min<size_t>(out.size() - got, 16u * 1024 * 1024);
                if (!ReadFile(h, out.data() + got, want, &rd, nullptr) || rd == 0) {
                    err = "파일 읽기 실패 (오류 " + std::to_string(GetLastError()) + ")";
                    ok = false;
                    break;
                }
                got += rd;
            }
        }
        CloseHandle(h);
        return ok;
    }

    void handleDownload(const std::string& host, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + host + "] ";
        ComScope com; // 공용 대화상자(셸 확장)용
        telegram.sendMessage(pre + "PC 에 파일 선택 창을 띄웠습니다. 보낼 파일을 고르세요. "
                             "(여러 개 선택 가능, " + std::to_string(kDownloadTimeoutMs / 60000) +
                             "분 안에 고르지 않으면 취소)", resp);
        Logger::log("/download: 파일 선택 창 표시");

        std::vector<std::wstring> files;
        std::string err;
        if (!pickFiles(files, err)) {
            if (err.empty()) {
                telegram.sendMessage(pre + "파일 선택이 취소되었거나 시간이 초과되었습니다.", resp);
                Logger::log("/download: 선택 취소 또는 시간 초과");
            } else {
                telegram.sendMessage(pre + "파일 선택 창을 열 수 없습니다: " + err, resp);
                Logger::log("/download: 선택 창 오류 %s", err.c_str());
            }
            return;
        }

        int okCnt = 0, failCnt = 0;
        std::string failList;
        for (const auto& w : files) {
            if (stop_.requested()) break;
            const size_t pos = w.find_last_of(L"\\/");
            const std::string name = StrUtil::toUtf8((pos == std::wstring::npos ? w : w.substr(pos + 1)).c_str());
            std::vector<BYTE> data;
            std::string e;
            if (readWholeFile(w, data, e)) {
                if (telegram.sendFileDocument(pre + name, name, data, resp)) {
                    ++okCnt;
                    Logger::log("/download 전송: %s (%zu바이트)", name.c_str(), data.size());
                    continue;
                }
                e = "전송 실패: " + resp.substr(0, 100);
            }
            ++failCnt;
            failList += "\n- " + name + " : " + e;
            Logger::log("/download 실패: %s : %s", name.c_str(), e.c_str());
        }
        std::string msg = pre + "/download 완료: 성공 " + std::to_string(okCnt) + "개, 실패 " +
                          std::to_string(failCnt) + "개 (선택 " + std::to_string(files.size()) + "개)";
        if (!failList.empty()) msg += failList;
        telegram.sendMessage(msg, resp);
    }

    // /CMD 명령 : /CMD 뒤의 문자열 전체(앞뒤 공백 제거)를 cmd.exe 로 실행하고 출력을 .txt 문서로 보낸다. (명령 스레드에서 호출)
    // 실행하는 동안 이 명령 스레드가 막히므로 다른 텔레그램 명령은 끝난 뒤(최대 kCmdTimeoutMs) 처리된다.
    void handleCmdCmd(const std::string& arg, const std::string& host, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + host + "] ";
        const std::string usage = "\n사용법: /CMD 실행할 명령  (예: /CMD dir /b)\n"
                                  "/CMD 뒤의 문자열 전체를 PC 의 cmd.exe 에서 실행하고 결과를 txt 파일로 보냅니다.";
        const std::string cmdline = StrUtil::trim(arg);
        if (cmdline.empty()) {
            telegram.sendMessage(pre + "실행할 명령이 없습니다." + usage, resp);
            return;
        }

        Logger::log("/CMD 실행: %.200s%s", cmdline.c_str(), cmdline.size() > 200 ? "..." : "");
        std::string out, err;
        DWORD code = 0;
        bool timedOut = false, truncated = false;
        const ULONGLONG t0 = GetTickCount64();
        if (!runShell(cmdline, out, code, timedOut, truncated, err)) {
            telegram.sendMessage(pre + "/CMD 실행 실패: " + err, resp);
            Logger::log("/CMD 실행 실패: %s", err.c_str());
            return;
        }
        const double sec = (double)(GetTickCount64() - t0) / 1000.0;
        Logger::log("/CMD 종료: 코드 %lu, 출력 %zu바이트, %.1f초%s%s", (unsigned long)code, out.size(), sec,
                    timedOut ? ", 시간 초과" : "", truncated ? ", 잘림" : "");

        std::string head = pre + "/CMD " + (cmdline.size() > 300 ? cmdline.substr(0, 300) + "..." : cmdline) + "\n";
        char info[160];
        snprintf(info, sizeof(info), "종료 코드 %lu, %.1f초, 출력 %zu바이트", (unsigned long)code, sec, out.size());
        head += info;
        if (timedOut) head += "\n시간 초과(" + std::to_string(kCmdTimeoutMs / 1000) + "초)로 강제 종료했습니다.";
        if (truncated) head += "\n출력이 " + std::to_string(kCmdMaxOut / 1024 / 1024) + "MB 를 넘어 뒷부분은 잘렸습니다.";

        if (out.empty()) {
            telegram.sendMessage(head + "\n(출력 없음)", resp);
            return;
        }
        char fname[64];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(fname, sizeof(fname), "cmd_%Y%m%d_%H%M%S.txt", &tmv);
        if (!telegram.sendTextDocument(head, fname, out, resp)) {
            telegram.sendMessage(pre + "/CMD 결과 전송 실패: " + resp.substr(0, 200), resp);
            Logger::log("/CMD 결과 문서 전송 실패: %s", resp.substr(0, 150).c_str());
        }
    }

    // /ini 키=값 : INI 파일 끝에 새 키를 추가한다. 같은 키(대소문자 무시)가 이미 있으면 경고만 하고 삽입하지 않는다.
    // 값은 Config 가 시작할 때만 읽으므로 추가한 값은 프로그램을 다시 시작해야 적용된다. (명령 스레드에서 호출)
    void handleIni(std::string arg, const std::string& host, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + host + "] ";
        const std::string usage = "\n사용법: /ini 키=값  (예: /ini INTRUSION_COOLDOWN=120)\n"
                                  "INI 에 새 키만 추가합니다. 이미 있는 키는 바꾸지 않고 경고만 합니다. "
                                  "추가한 값은 프로그램을 다시 시작해야 적용됩니다. (캡처 간격은 /INTERVAL=nnn)";

        // [키=값] 처럼 대괄호로 감싸 보내도 허용 (= 가 있을 때만. [섹션] 줄은 추가하지 않음)
        if (arg.size() >= 2 && arg.front() == '[' && arg.back() == ']' && arg.find('=') != std::string::npos)
            arg = StrUtil::trim(arg.substr(1, arg.size() - 2));

        if (arg.empty()) {
            telegram.sendMessage(pre + "추가할 내용이 없습니다." + usage, resp);
            return;
        }
        const size_t eq = arg.find('=');
        if (eq == std::string::npos || arg.find_first_of("\r\n") != std::string::npos) {
            telegram.sendMessage(pre + "형식이 올바르지 않습니다. 한 줄의 '키=값' 이어야 합니다." + usage, resp);
            return;
        }
        const std::string key = StrUtil::upper(StrUtil::trim(arg.substr(0, eq)));
        const std::string val = StrUtil::trim(arg.substr(eq + 1));
        bool keyOk = !key.empty() && key.size() <= 64;
        for (char c : key)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) keyOk = false;
        if (!keyOk || val.size() > 400) {
            telegram.sendMessage(pre + "키 이름은 영문/숫자/_ 만 쓸 수 있고(64자 이내), 값은 400자 이내여야 합니다." + usage, resp);
            return;
        }

        switch (SysUtil::addIniKey(iniPath_, key, val)) {
        case SysUtil::IniAdd::Exists:
            telegram.sendMessage(pre + "경고: INI 에 이미 " + key + " 키가 있어 삽입하지 않았습니다. "
                                 "(기존 값은 그대로입니다. 값을 바꾸려면 INI 파일을 직접 수정하세요)", resp);
            Logger::log("/ini: 키가 이미 있어 삽입하지 않음: %s", key.c_str());
            break;
        case SysUtil::IniAdd::Added: {
            std::string msg = pre + "INI 에 추가했습니다: " + key + "=" + val +
                              "\n프로그램을 다시 시작해야 적용됩니다.";
            if (!Config::isKnownKey(key))
                msg += "\n주의: 이 프로그램이 사용하지 않는 키 이름입니다. 오타가 아닌지 확인하세요.";
            telegram.sendMessage(msg, resp);
            Logger::log("/ini: INI 에 키 추가: %s (값은 로그에 남기지 않음)", key.c_str());
            break;
        }
        default:
            telegram.sendMessage(pre + "INI 파일을 읽거나 쓰지 못했습니다. (경로/권한 확인)", resp);
            Logger::log("/ini: INI 쓰기 실패: %s", iniPath_.c_str());
            break;
        }
    }

    // 인라인 버튼 클릭 처리 (명령 스레드에서 호출). 지금은 /stop 의 Yes/No 확인 버튼만 있다.
    // 현재 대기 중인 확인 메시지의 버튼만 유효하다. 재시작 전의 오래된 메시지나 이미 처리된 메시지,
    // 5분이 지난 메시지의 버튼은 종료로 이어지지 않는다.
    void handleCallback(const BotEvent& ev, const TelegramClient& telegram)
    {
        Logger::log("버튼 수신: %s", ev.cbData.c_str());
        const std::string host = SysUtil::hostName();
        std::string resp;

        const bool yes = (ev.cbData == "stop_yes");
        const bool no = (ev.cbData == "stop_no");
        if (!yes && !no) { // 알 수 없는 버튼: 로딩 표시만 끈다
            telegram.answerCallbackQuery(ev.cbId, "", resp);
            return;
        }

        const bool valid = !stopMsgId_.empty() && ev.messageId == stopMsgId_ &&
                           GetTickCount64() - stopAskTick_ < 300000ULL;
        if (!valid) {
            telegram.answerCallbackQuery(ev.cbId, "만료된 요청입니다. /stop 을 다시 보내세요.", resp);
            if (!ev.messageId.empty())
                telegram.editMessageText(ev.messageId, "[" + host + "] 종료 확인이 만료되었습니다.", resp);
            return;
        }

        stopMsgId_.clear(); // 한 번 처리한 확인 메시지는 다시 쓸 수 없다
        if (yes) {
            telegram.answerCallbackQuery(ev.cbId, "종료합니다.", resp);
            telegram.editMessageText(ev.messageId, "[" + host + "] 종료합니다.", resp);
            Logger::log("종료 확인(Yes) - 종료합니다.");
            stop_.request();
        } else {
            telegram.answerCallbackQuery(ev.cbId, "종료를 취소했습니다.", resp);
            telegram.editMessageText(ev.messageId, "[" + host + "] 종료를 취소했습니다.", resp);
            Logger::log("종료 확인(No) - 종료 취소");
        }
    }

    // 침입 탐지를 켜거나 끈다 (/detectOn /detectOff 와 AUTO_DETECT 가 공통으로 사용).
    // 탐지 스레드가 detectOn_ 을 보고 카메라를 잡거나 놓는다. 반환값: 상태 파일 저장 성공 여부.
    bool applyDetect(bool on)
    {
        InterlockedExchange(&detectOn_, on ? 1 : 0);
        return saveDetectState(on);
    }

    // 침입 탐지 켜짐/꺼짐 상태 저장 (exe 폴더의 detect.state, 내용 "ON" / "OFF").
    // 재시작/재부팅 후에도 마지막 상태가 이어지도록 한다. 파일이 없거나 ON 이 아니면 꺼짐.
    bool saveDetectState(bool on) const
    {
        const std::string s = on ? "ON" : "OFF";
        return SysUtil::writeFile(stateFile_, std::vector<BYTE>(s.begin(), s.end()));
    }

    bool loadDetectState() const
    {
        std::string s;
        if (!SysUtil::readFile(stateFile_, s)) return false;
        return StrUtil::upper(StrUtil::trim(s)) == "ON";
    }

    void setLastCycle(const std::string& s)
    {
        EnterCriticalSection(&cs_);
        lastCycle_ = s;
        LeaveCriticalSection(&cs_);
    }

    std::string getLastCycle()
    {
        EnterCriticalSection(&cs_);
        std::string s = lastCycle_;
        LeaveCriticalSection(&cs_);
        return s;
    }

    // 현재 폴더(exe 폴더)의 image 폴더에 "YYYYMMDD_HHMMSS_태그.jpg|png" 로 저장
    bool saveImage(const std::string& tag, const std::vector<BYTE>& img, bool jpeg)
    {
        if (!SysUtil::ensureDir(imageDir_)) {
            Logger::log("image 폴더 생성 실패: %s", imageDir_.c_str());
            return false;
        }
        const std::string path = imageDir_ + "\\" + SysUtil::fileStamp() + "_" + tag +
                                 (jpeg ? ".jpg" : ".png");
        if (SysUtil::writeFile(path, img)) {
            Logger::log("저장: %s (%zu KB)", path.c_str(), img.size() / 1024);
            return true;
        }
        Logger::log("이미지 저장 실패: %s", path.c_str());
        return false;
    }

    // 1회 주기: 모든 소스 캡처 -> 인코딩 -> image 폴더 저장 -> 텔레그램 전송 -> 오래된 이미지 정리
    void cycle(std::vector<std::unique_ptr<ICaptureSource>>& sources,
               const ImageEncoder& encoder, const TelegramClient& telegram,
               bool forceSend = false, CaptureFilter filter = CaptureFilter::All)
    {
        const std::string host = SysUtil::hostName();
        const std::string ts = SysUtil::timestamp();
        setLastCycle(ts);

        std::vector<Shot> shots;
        for (auto& src : sources) {
            if (filter == CaptureFilter::ScreenOnly && src->isCamera()) continue;
            if (filter == CaptureFilter::CameraOnly && !src->isCamera()) continue;
            src->capture(shots);
        }

        emitShots(shots, encoder, telegram, forceSend, host, ts, "", "");
    }

    // 캡처된 이미지를 인코딩 -> image 폴더 저장 -> 텔레그램 전송 -> 오래된 이미지 정리.
    // 메인 스레드(주기/명령 캡처)와 침입 탐지 스레드가 함께 사용하므로 스레드 안전해야 한다.
    // filePrefix: 저장 파일명 태그 앞에 붙임 (침입 이미지는 "Intrusion_"), labelPrefix: 캡션 앞에 붙임.
    void emitShots(std::vector<Shot>& shots, const ImageEncoder& encoder, const TelegramClient& telegram,
                   bool forceSend, const std::string& host, const std::string& ts,
                   const std::string& filePrefix, const std::string& labelPrefix)
    {
        bool saved = false;
        // 이번 호출에서 실제로 전송할 가능성이 있는지 (아니면 전송용 축소본을 만들 필요가 없다)
        const bool willSend = (forceSend || !(cfg_.saveOnly || paused_ != 0)) &&
                              !cfg_.token.empty() && !cfg_.chatId.empty();
        for (auto& shot : shots) {
            if (stop_.requested()) break;

            std::vector<BYTE> img;
            bool jpeg = false, asDoc = false;
            bool enc = encoder.encodeForTelegram(shot.bmp.get(), img, jpeg, asDoc);

            // 저장본(img)은 원본 해상도 그대로 두고, 큰 화면(4K 등)은 전송용만 줄여서 올린다.
            std::vector<BYTE> sendImg;
            UINT sw = 0, sh = 0;
            if (!(enc && jpeg && !asDoc && willSend &&
                  encoder.encodeScaledJpeg(shot.bmp.get(), kSendMaxDim, 85, sendImg, sw, sh) &&
                  !sendImg.empty() && sendImg.size() < img.size()))
                sendImg.clear();
            shot.bmp.reset();

            if (!enc) {
                Logger::log("%s 이미지 인코딩 실패", shot.label.c_str());
                continue;
            }

            // 전송 전에 image 폴더에 저장 (저장 실패해도 전송은 계속)
            if (saveImage(filePrefix + shot.tag, img, jpeg)) saved = true;
            // 저장만 하고 전송은 생략 (SAVE_ONLY, 또는 /pause 상태). forceSend(/shot, 침입 탐지)는 항상 전송.
            // 캡처 도중 명령 스레드가 /pause 를 처리하면 남은 이미지부터 바로 전송이 중단된다.
            if (!forceSend && (cfg_.saveOnly || paused_ != 0)) continue;
            if (cfg_.token.empty() || cfg_.chatId.empty()) continue; // 토큰 없이 저장만 하는 모드

            const std::string label = labelPrefix + shot.label;
            const std::string caption = "[" + host + "] " + label + " " + ts;
            std::string resp;
            const std::vector<BYTE>& payload = sendImg.empty() ? img : sendImg;
            const ULONGLONG tSend = GetTickCount64();
            if (telegram.sendImageWithRetry(caption, payload, jpeg, asDoc, resp))
                Logger::log("%s 전송 성공 (%zu KB, %s%s%s, %llu ms)", label.c_str(),
                            payload.size() / 1024, jpeg ? "JPEG" : "PNG",
                            asDoc ? ", 문서로 전송" : "",
                            sendImg.empty() ? "" : (", 축소 " + std::to_string(sw) + "x" + std::to_string(sh)).c_str(),
                            (unsigned long long)(GetTickCount64() - tSend));
            else
                Logger::log("%s 전송 실패: %s", label.c_str(), resp.c_str());
        }

        if (saved) {
            EnterCriticalSection(&ioCs_); // 두 스레드가 동시에 폴더를 정리하지 않도록
            SysUtil::cleanupImages(imageDir_, cfg_.keepDays, cfg_.keepMax);
            LeaveCriticalSection(&ioCs_);
        }
    }

    Config cfg_;
    StopSignal stop_;
    volatile LONG paused_ = 0;       // /pause 상태 (텔레그램 이미지 전송 중단) - 명령 스레드가 변경
    volatile LONG shotMask_ = 0;     // 캡처 요청 비트 (1=화면, 2=카메라) (명령 스레드 -> 메인 스레드)
    CameraCapture* camera_ = nullptr; // sources 가 소유. 침입 탐지용 직접 접근
    bool cameraReady_ = false;       // 카메라 초기화 성공 여부 (명령 스레드 시작 전에 설정)
    std::string stopMsgId_;          // 대기 중인 /stop 확인 메시지의 id (명령 스레드 전용)
    ULONGLONG stopAskTick_ = 0;      // 그 확인 메시지를 보낸 시각 (GetTickCount64)
    volatile LONG detectOn_ = 0;     // 침입 탐지 켜짐 (/detectOn /detectOff 로 명령 스레드가 변경, 탐지 스레드가 읽음)
    MotionDetector::Params detParams_;
    MotionDetector detector_;        // 탐지 스레드 전용 (켜질 때마다 새로 만듦)
    volatile LONG loopCount_ = 0;    // 완료한 반복 횟수 (/shot 은 제외)
    ULONGLONG startTick_ = 0;        // 시작 시각 (가동 시간 계산용)
    static constexpr int kPollSec = 10;         // 명령 롱폴링 대기(초). 연결을 재사용하므로 길게 잡아도 종료 반응은 최대 이 시간
    static constexpr UINT kSendMaxDim = 2560;   // 전송용 이미지의 긴 변 상한(px). 텔레그램 사진은 어차피 이 크기까지만 보관
    static constexpr int kMinInterval = 5;      // /INTERVAL 허용 범위(초). 너무 짧으면 텔레그램 전송 제한(429)에 걸림
    static constexpr int kMaxInterval = 86400;
    static constexpr int kDefaultLogLines = 50; // /log 기본 줄 수
    static constexpr int kMaxLogLines = 2000;   // /log n 허용 최대 줄 수
    static constexpr size_t kUploadMaxBytes = 20u * 1024 * 1024;   // 폰 -> PC 수신 한도 (봇 API getFile 20MB)
    static constexpr UINT kDownloadTimeoutMs = 300000;            // /download 파일 선택 창 대기 시간(5분)
    static constexpr size_t kDownloadMaxBytes = 49000000;         // /download 파일 1개 한도 (sendDocument 50MB)
    static constexpr size_t kPickBufChars = 262144;               // 파일 선택 창 결과 버퍼(문자 수)
    static constexpr DWORD kCmdTimeoutMs = 60000;                 // /CMD 최대 실행 시간
    static constexpr size_t kCmdMaxOut = 20u * 1024u * 1024u;     // /CMD 출력 최대 크기 (sendDocument 50MB 한도에 여유)
    std::string iniPath_;            // 로드한 INI 경로 (/INTERVAL 저장용)
    volatile LONG intervalChanged_ = 0; // /INTERVAL 로 간격이 바뀜 (명령 스레드 -> 메인 스레드)
    ULONGLONG periodStart_ = 0;      // 현재 주기의 시작 시각 (메인 스레드 전용)
    std::string lastCycle_;          // 마지막 캡처 시각 (cs_ 로 보호)
    CRITICAL_SECTION cs_;
    HANDLE shotEvent_ = nullptr;
    HANDLE worker_ = nullptr;
    WorkerCtx ctx_;
    CRITICAL_SECTION ioCs_;          // image 폴더 정리 직렬화
    HANDLE intrusionThread_ = nullptr;
    const ImageEncoder* encoder_ = nullptr;      // 침입 탐지 스레드가 사용 (run() 의 지역 객체, 스레드 종료 후 해제)
    const TelegramClient* telegram_ = nullptr;
    const std::string imageDir_ = SysUtil::exeDir() + "\\image";
    const std::string stateFile_ = SysUtil::exeDir() + "\\detect.state"; // 침입 탐지 켜짐/꺼짐 상태 저장
};

// ================================================================ main
// 시작 함수: WinMain (명령줄 인자는 resolveIniPath() 가 GetCommandLineW 로 직접 읽으므로 매개변수는 사용하지 않음)
// 처리되지 않은 SEH 예외(접근 위반 등)가 나면 원인을 log.txt 에 남기고 종료한다.
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    Logger::logCrash("치명적 오류: 처리되지 않은 예외 0x%08lX, 주소 %p (스레드 T%lu) - 프로그램이 비정상 종료됩니다.",
                     r ? (unsigned long)r->ExceptionCode : 0UL, r ? r->ExceptionAddress : nullptr,
                     (unsigned long)GetCurrentThreadId());
    return EXCEPTION_EXECUTE_HANDLER;
}

// 잡히지 않은 C++ 예외 / std::terminate
static void terminateHandler()
{
    Logger::logCrash("치명적 오류: 처리되지 않은 C++ 예외 (std::terminate) - 프로그램이 비정상 종료됩니다.");
    ExitProcess(3);
}

// 중복 실행 방지: 같은 폴더의 exe 마다 하나씩 이름 있는 뮤텍스를 잡는다.
// (RUN.BAT 을 두 번 실행하거나, 시작프로그램 + 수동 실행이 겹쳐도 두 번째 인스턴스는 바로 종료한다.
//  둘이 동시에 돌면 getUpdates 409 충돌, 이중 캡처/전송, 카메라 경합이 생긴다.)
// 뮤텍스 이름은 exe 폴더 경로의 해시라서, 서로 다른 폴더의 복사본(테스트용 등)은 따로 실행할 수 있다.
// 프로세스가 어떻게 끝나든(크래시, /stop, 작업 관리자) OS 가 핸들을 닫아 자동으로 풀린다.
// 반환: 핸들(종료 때 닫음). another=true 이면 이미 다른 인스턴스가 실행 중.
static HANDLE acquireSingleInstance(bool& another)
{
    another = false;
    const std::wstring dir = StrUtil::toWide(SysUtil::exeDir());
    unsigned long long h = 1469598103934665603ULL; // FNV-1a 64bit
    for (wchar_t c : dir) {
        h ^= (unsigned long long)towlower(c);
        h *= 1099511628211ULL;
    }
    std::wstring name = L"Global\\ScreenTelegram_";
    for (int shift = 60; shift >= 0; shift -= 4)
        name += L"0123456789ABCDEF"[(h >> shift) & 0xF];

    HANDLE m = CreateMutexW(nullptr, FALSE, name.c_str());
    const DWORD err = GetLastError();
    if (m && err == ERROR_ALREADY_EXISTS) another = true;
    // 다른 사용자 계정이 만든 뮤텍스는 열 권한이 없어 ACCESS_DENIED 가 되는데, 이것도 이미 실행 중이라는 뜻이다.
    if (!m && err == ERROR_ACCESS_DENIED) another = true;
    return m; // 그 밖의 이유로 만들지 못하면(nullptr, another=false) 중복 방지 없이 그냥 실행한다
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SetUnhandledExceptionFilter(crashFilter);
    std::set_terminate(terminateHandler);

    bool another = false;
    HANDLE instanceMutex = acquireSingleInstance(another);
    if (another) {
        // 시작 로그(PREVIOUS_SESSION_CLEAR 정리 포함)보다 먼저 빠지므로 실행 중인 쪽의 로그/이미지는 건드리지 않는다.
        Logger::log("이미 실행 중인 screen_telegram 이 있어 이 인스턴스는 바로 종료합니다. (PID %lu)",
                    (unsigned long)GetCurrentProcessId());
        if (instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }

    int rc = 0;
    try {
        ScreenTelegramApp app;
        rc = app.run();
    } catch (const std::exception& e) {
        Logger::logCrash("치명적 오류: 예외 발생 - %s", e.what());
        rc = 2;
    } catch (...) {
        Logger::logCrash("치명적 오류: 알 수 없는 예외 발생");
        rc = 2;
    }
    Logger::log("프로그램 종료 (종료 코드 %d)", rc);
    if (instanceMutex) CloseHandle(instanceMutex);
    return rc;
}
