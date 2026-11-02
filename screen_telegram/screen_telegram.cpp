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
//   TELEGRAM_GREETING=TRUE (시작 준비가 끝나면 텔레그램으로 인사말을 한 번 보냄. 기본 TRUE)
//   AUTO_DETECT=TRUE       (프로그램 시작 때 텔레그램 /detectOn 과 동일하게 침입 탐지를 자동으로 켠다. 기본 FALSE.
//                           FALSE 이면 detect.state 에 저장된 마지막 상태(켜짐/꺼짐)를 따른다. CAMERA=TRUE 필요)
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
//   TelegramClient  텔레그램 sendPhoto / sendMessage / getUpdates (WinHTTP) + 재시도
//   CommandListener 텔레그램 봇 명령 수신 (getUpdates, 허용된 chat 만)
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

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cctype>
#include <cmath>
#include <cwctype>
#include <cstdarg>
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
    static void log(const char* fmt, ...)
    {
        char tbuf[32];
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_s(&tmv, &t);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);

        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);

        printf("[%s] %s\n", tbuf, msg);
        fflush(stdout);
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
    int interval = 60;

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
    bool cameraKeepOpen = false; // CAMERA_KEEP_OPEN=TRUE (카메라를 계속 열어 둠)
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
        else if (key == "KEEP_MAX_FILES") {
            int v = atoi(val.c_str());
            if (v >= 0) keepMax = v;
        }
        else if (key == "CAMERA_KEEP_OPEN") {
            cameraKeepOpen = StrUtil::parseBool(val, false);
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
        : token_(std::move(token)), chatId_(std::move(chatId)), stop_(stop) {}

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
    // POST 1회. 응답 본문을 resp 에 담고, "ok":true 이면 true.
    bool post(const wchar_t* method, const std::string& contentType,
              const std::string& body, std::string& resp) const
    {
        resp.clear();
        HINTERNET hSession = WinHttpOpen(L"ScreenTelegram/1.0",
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            resp = "WinHTTP error (open)";
            return false;
        }
        WinHttpSetTimeouts(hSession, 10000, 10000, 60000, 120000);

        bool ok = false;
        HINTERNET hConn = WinHttpConnect(hSession, L"api.telegram.org",
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
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
        if (hReq) WinHttpCloseHandle(hReq);
        if (hConn) WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
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
};

// ================================================================ CommandListener
// 텔레그램 봇 명령 수신 (getUpdates). 설정된 chat_id 에서 온 명령만 허용한다.
// 주의: 같은 봇 토큰으로 다른 프로그램이 getUpdates/webhook 을 쓰면 409 충돌이 난다.
// 수신 이벤트 1건: 일반 명령(/shot 등) 또는 인라인 버튼 클릭(callback_query)
struct BotEvent {
    std::string cmd;        // 명령(소문자, '/' 포함, @봇이름 제거). 버튼 클릭이면 비어 있음
    std::string cbData;     // 버튼 클릭: callback_data (예: "stop_yes"). 일반 명령이면 비어 있음
    std::string cbId;       // 버튼 클릭: callback_query id (answerCallbackQuery 용)
    std::string messageId;  // 버튼 클릭: 버튼이 붙어 있던 메시지의 id
    bool isCallback() const { return !cbData.empty(); }
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
                ev.cmd = extractCommand(extractText(seg));
                if (ev.cmd.empty()) continue;
                what = ev.cmd;
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

        const std::string ini = resolveIniPath();
        if (!cfg_.load(ini)) {
            Logger::log("INI 파일을 열 수 없습니다: %s", ini.c_str());
            return 1;
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
            Logger::log("텔레그램 명령 수신 활성: /shot /screen /photo /pause /resume /status /stop /help");
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
            if (!idle(wait, sources, encoder, telegram)) break;
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
        // 롱폴링(최대 약 3초) 중이면 끝날 때까지 잠시 기다린다. 너무 길면 강제 종료 (어차피 프로세스 종료 중)
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
        std::string text = "안녕하세요! [" + host + "] screen_telegram 준비가 완료되었습니다.\n" +
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
            if (!listener.poll(3, events)) {
                // 통신 실패: 바로 재시도하면 CPU/네트워크를 낭비하므로 잠시 대기
                if (stop_.wait(5000)) break;
                continue;
            }
            for (const auto& ev : events) {
                if (stop_.requested()) break;
                if (ev.isCallback()) handleCallback(ev, telegram);
                else handleCommand(ev.cmd, telegram);
            }
        }
    }

    // 메인 스레드에서 ms 동안 대기한다. 명령 스레드의 캡처 요청(/shot /screen /photo)이 오면 즉시 처리하고
    // 남은 시간만큼 계속 대기.
    // 종료 요청이 오면 false.
    bool idle(DWORD ms, std::vector<std::unique_ptr<ICaptureSource>>& sources,
              const ImageEncoder& encoder, const TelegramClient& telegram)
    {
        const ULONGLONG end = GetTickCount64() + ms;
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
    void handleCommand(const std::string& cmd, const TelegramClient& telegram)
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
            char buf[640];
            snprintf(buf, sizeof(buf),
                     "[%s] 실행 중\n가동 시간: %llu일 %02llu:%02llu:%02llu\n간격: %d초\n"
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
                    "/stop - 프로그램 종료 (Yes/No 확인)\n/help 또는 // - 도움말";
            telegram.sendMessage(text, resp);
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
        for (auto& shot : shots) {
            if (stop_.requested()) break;

            std::vector<BYTE> img;
            bool jpeg = false, asDoc = false;
            bool enc = encoder.encodeForTelegram(shot.bmp.get(), img, jpeg, asDoc);
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
            if (telegram.sendImageWithRetry(caption, img, jpeg, asDoc, resp))
                Logger::log("%s 전송 성공 (%zu KB, %s%s)", label.c_str(),
                            img.size() / 1024, jpeg ? "JPEG" : "PNG",
                            asDoc ? ", 문서로 전송" : "");
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
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    ScreenTelegramApp app;
    return app.run();
}
