// gsHomeManager - 텍스트 시계 + 집 안팎 감시(블루투스/Wi-Fi 스캔) - Windows 11 GUI (WinMain), 클래스 구조, 전부 UTF-8
// (gsClock 과 WiFiScan 을 하나로 합친 앱. screen_telegram 도 이 파일에 합쳤다: namespace tg)
// rc/manifest 불필요. 문자열은 UTF-8 char 로만 다루고, 화면 출력 직전에만 UTF-8 -> UTF-16 변환(char16_t 버퍼)
// 빌드: g++ -Os -s -static -mwindows -Wl,--gc-sections -o gsHomeManager.exe gsHomeManager.cpp gsHomeManager_res.o -lcomdlg32 -lole32 -loleaut32 -lcomctl32 -liphlpapi -lws2_32 -lbthprops -lgdiplus -lgdi32 -lwinhttp -lmfuuid   (_build.bat)
//
// 구조
//   Utf8            : UTF-8 변환 도우미
//   SingleInstance  : 중복 실행 방지 (RAII 뮤텍스)
//   Scroller        : 좌우 스크롤 상태 (오프셋/방향, 끝에서 방향 전환)
//   DesktopPin      : 모든 가상 데스크톱에 표시 (셸 COM, 필요할 때만 초기화)
//   CITIES / Zone   : Global 도시 목록 / 도시별 현재 시각 (Windows 시간대 + DST)
//   Settings        : gsHomeManager.ini 읽기/쓰기 (타이머, 스타일, 옵션, 기준 도시, 창 위치, WiFiScan)
//   TimeDialog      : 알람 시각(시:분:초) 입력 대화상자 (코드로 만든 대화상자 + 시간 선택 컨트롤)
//   WiFiScan        : (WiFiScan.cpp 에서 가져옴) StrUtil / AppDir / Logger / NameResolver / OuiDb / Subnet / Arp / PrevResult /
//                     ScanView / Bluetooth / ResultFile / WifiScanner / TriggerFile / HomeWatcher / ScanEngine / ScanDialog
//   namespace tg    : (screen_telegram.cpp 에서 가져옴) 주기 화면/웹캠 캡처 -> 텔레그램 전송, 봇 명령 수신, 침입 탐지(카메라 모션), 폰 <-> PC 파일 전송.
//                     Logger / StrUtil / SysUtil / LocalTrigger / StopSignal / Config / ImageEncoder / TelegramClient / CommandListener /
//                     MonitorCapture / CameraCapture / MotionDetector / ImageWindow / ScreenTelegramApp. 전용 스레드로 돌고 설정은 ini [Telegram]
//   namespace rdp   : (rdp_server.cpp / rdp_client.cpp 에서 가져옴) 다른 PC 원격 제어. Server(화면 JPEG 전송 + 마우스 입력 주입) / Client(화면 보기 + 마우스 전달)
//                     + RdpServerDialog / RdpClientDialog (메뉴 F8). 전용 스레드로 돌고 설정은 ini [RdpServer] / [RdpClient]
//   ClockWindow     : 시계 창 1개. 본 창(메뉴 있음) + Global 도시 창(소유 창, 본 창 아래에 계속 추가)
//   WinMain         : 조립
// WinLibs(GCC 16, -Os, 정적 링크)에서 std::string 이동 생성자가 libstdc++.a 에 없어 링크 오류가 나는 것을 피한다:
// basic_string 의 extern template 만 끄면 필요한 함수를 이 파일 안에서 직접 만든다.
#if defined(__has_include)
#  if __has_include(<bits/c++config.h>)
#    include <bits/c++config.h>
#    undef _GLIBCXX_EXTERN_TEMPLATE
#    define _GLIBCXX_EXTERN_TEMPLATE -1
#  endif
#endif
#ifndef NOMINMAX
#define NOMINMAX                         // std::max 등과 windows.h 의 min/max 매크로 충돌 방지
#endif
#include <winsock2.h>                    // windows.h 보다 먼저 (WiFiScan 의 소켓/ICMP/블루투스)
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <objbase.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include "version.h"                     // APP_NAME / APP_VERSION_STR (빌드할 때 불러옴. exe 속성의 버전 정보와 같은 값)
#include <winhttp.h>                     // Telegram 모듈의 WinHTTP (-lwinhttp). wininet.h 와는 타입이 충돌해 함께 include 할 수 없다 (HttpGet 의 WinINet 은 동적 로딩 + 상수 직접 정의)
#include <propsys.h>
#include <objidl.h>                         // 여기부터 screen_telegram 에서 쓰는 헤더 (Media Foundation 카메라)
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#undef FCC                                  // mfapi.h 의 FCC 매크로가 ipcam 녹화기의 FCC() 멤버 함수와 충돌
#include <stdlib.h>
#include <stdarg.h>
#include <cstdio>                        // 여기부터 WiFiScan 에서 쓰는 표준 라이브러리
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cmath>                          // 아날로그 시계 바늘 좌표 (sin/cos)
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <functional>
#include <cstdarg>
#include <cwctype>
#include <exception>
#include <utility>
using std::min; using std::max;           // gdiplus.h 가 min/max 를 그냥 쓴다 (NOMINMAX 와 함께 필요)
#include <gdiplus.h>                      // IP cam (ipcam.cpp 통합)

// ---------------------------------------------------------------- Utf8
class Utf8 {
public:
    // UTF-8 문자열 -> UTF-16 (출력용 임시 버퍼), 변환된 길이 반환 (n=-1 이면 널 포함 길이)
    static int ToUtf16(const char* s, int n, char16_t* out, int cap) {
        return MultiByteToWideChar(CP_UTF8, 0, s, n, (LPWSTR)out, cap);
    }

    // 유효한 UTF-8 이 아니면 ANSI(CP949 등)로 보고 UTF-8 로 바꿔 줌 (ini 를 ANSI 로 저장한 경우 대비)
    static void Normalize(char* s, int cap) {
        char16_t w[1024];
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, (LPWSTR)w, 1024)) return;
        if (MultiByteToWideChar(CP_ACP, 0, s, -1, (LPWSTR)w, 1024))
            WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)w, -1, s, cap, NULL, NULL);
    }
};

// ---------------------------------------------------------------- Log
// 동작 로그: 실행 파일과 같은 폴더의 gsHomeManager.log (UTF-8, 한 줄에 한 동작, 시각 포함).
// 기록할 때만 파일을 열고 바로 닫음(상주 메모리/핸들 없음). 1MB 를 넘으면 gsHomeManager.log.old 로 넘기고 새로 시작.
class Log {
public:
    static void Init() {
        GetModuleFileNameA(NULL, path_, MAX_PATH);
        int n = lstrlenA(path_);
        while (n > 0 && path_[n - 1] != '.' && path_[n - 1] != '\\') n--;
        if (n > 0 && path_[n - 1] == '.') lstrcpyA(path_ + n, "log");
        else lstrcatA(path_, ".log");
    }

    static void Put(const char* fmt, ...) {
        if (!path_[0]) return;
        char msg[800];
        va_list ap;
        va_start(ap, fmt);
        wvsprintfA(msg, fmt, ap);
        va_end(ap);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[900];
        int n = wsprintfA(line, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\r\n",
                          t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        HANDLE h = Open();
        if (h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart > 1024 * 1024) {      // 로그 순환
            CloseHandle(h);
            char old[MAX_PATH + 8];
            lstrcpyA(old, path_);
            lstrcatA(old, ".old");
            MoveFileExA(path_, old, MOVEFILE_REPLACE_EXISTING);
            h = Open();
            if (h == INVALID_HANDLE_VALUE) return;
        }
        DWORD w;
        WriteFile(h, line, (DWORD)n, &w, NULL);
        CloseHandle(h);
    }

private:
    static HANDLE Open() {
        return CreateFileA(path_, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    static inline char path_[MAX_PATH + 8] = "";
};

// ---------------------------------------------------------------- SingleInstance
class SingleInstance {
public:
    explicit SingleInstance(const char* name) {
        mutex_ = CreateMutexA(NULL, FALSE, name);
        first_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }
    ~SingleInstance() { if (mutex_) CloseHandle(mutex_); }
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool IsFirst() const { return first_; }

    // 뮤텍스를 미리 풀어 준다 (텔레그램 /restart: 새 프로세스를 띄우기 전에 호출). 소멸자는 이미 풀렸으면 아무것도 하지 않는다.
    void Release() { if (mutex_) { CloseHandle(mutex_); mutex_ = NULL; } }

    // 이미 실행 중인 같은 앱을 종료시키고 이 프로세스가 대신 실행되게 함.
    // 1) 기존 본 창에 종료 메시지를 보내 정상 종료(위치/도시 목록 저장, 트레이 아이콘 제거)를 기다리고,
    // 2) 응답이 없거나 남아 있으면 같은 이름의 다른 프로세스를 강제 종료.
    // 반환: 기존 프로세스가 모두 사라졌으면 true
    bool TakeOver() {
        EnumWindows(FindOld, 0);                // 정상 종료 요청 (종료를 기다림)
        KillSameName();                         // 남은 프로세스는 강제 종료
        first_ = true;
        return true;
    }

    static const UINT WM_TAKEOVER = WM_APP + 3;     // 새로 시작한 프로세스가 기존 본 창에 보내는 종료 요청

private:
    // 같은 클래스("gsHomeManager")의 소유자 없는 최상위 창 = 다른 프로세스의 본 창
    static BOOL CALLBACK FindOld(HWND h, LPARAM) {
        char cls[32];
        if (GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "gsHomeManager") == 0 && !GetWindow(h, GW_OWNER)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid && pid != GetCurrentProcessId()) {
                HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
                PostMessageA(h, WM_TAKEOVER, 0, 0);
                if (p) {
                    WaitForSingleObject(p, 4000);   // 정상 종료 대기 (최대 4초)
                    CloseHandle(p);
                }
            }
        }
        return TRUE;
    }

    static void KillSameName() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        const char* me = path;
        for (const char* c = path; *c; c++) if (*c == '\\' || *c == '/') me = c + 1;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32 e = {};
        e.dwSize = sizeof(e);
        for (BOOL ok = Process32First(snap, &e); ok; ok = Process32Next(snap, &e)) {
            if (e.th32ProcessID == GetCurrentProcessId() || lstrcmpiA(e.szExeFile, me) != 0) continue;
            HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, e.th32ProcessID);
            if (p) {
                TerminateProcess(p, 0);
                WaitForSingleObject(p, 3000);
                CloseHandle(p);
            }
        }
        CloseHandle(snap);
    }

    HANDLE mutex_;
    bool   first_;
};

// ---------------------------------------------------------------- Scroller
class Scroller {
public:
    explicit Scroller(int step) : step_(step) {}

    void SetTextWidth(int w) { tw_ = w; }
    int  Offset() const      { return off_; }

    // 한 칸 이동, 창 끝에 닿으면 방향 전환
    void Advance(int clientWidth) {
        int range = (clientWidth - tw_) / 2;
        if (range < 0) range = 0;
        off_ += dir_ * step_;
        if (off_ >=  range) { off_ =  range; dir_ = -1; }
        if (off_ <= -range) { off_ = -range; dir_ =  1; }
    }

private:
    int step_;
    int tw_  = 0;
    int off_ = 0;
    int dir_ = 1;
};

// ---------------------------------------------------------------- DesktopPin
// 모든 가상 데스크톱에 창 표시 (고정). 문서화되지 않은 셸 COM 인터페이스(IVirtualDesktopPinnedApps)를 사용.
// 프로세스에 고유 AppUserModelID 를 지정하고, 그 AppID 를 고정/해제한다.
// Windows 버전에 따라 동작하지 않을 수 있으며, 실패하면 false 를 반환한다.

struct IVdServiceProvider : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryService(REFGUID svc, REFIID riid, void** ppv) = 0;
};
struct IVdPinnedApps : public IUnknown {        // 메서드 순서 중요 (vtable)
    virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(LPCWSTR appId, BOOL* pinned) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinAppID(LPCWSTR appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinAppID(LPCWSTR appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsViewPinned(void* view, BOOL* pinned) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinView(void* view) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinView(void* view) = 0;
};

class DesktopPin {
public:
    static void SetProcessAppId() { SetCurrentProcessExplicitAppUserModelID((PCWSTR)AppId()); }

    // COM 은 필요할 때만 초기화하고 바로 해제 (상주 메모리 최소화)
    static bool Set(bool pin) {
        static const GUID CLSID_ImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
        static const GUID IID_ServiceProvider  = {0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};
        static const GUID SID_PinnedApps       = {0xB5A399E7, 0x1C87, 0x46B8, {0x88, 0xE9, 0xFC, 0x57, 0x47, 0xB1, 0x71, 0xBD}};
        static const GUID IID_PinnedApps       = {0x4CE81583, 0x1E4C, 0x4632, {0xA6, 0x21, 0x07, 0xA5, 0x35, 0x43, 0x14, 0x8F}};

        HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool ok = false;
        IVdServiceProvider* sp = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER,
                                       IID_ServiceProvider, (void**)&sp)) && sp) {
            IVdPinnedApps* pa = NULL;
            if (SUCCEEDED(sp->QueryService(SID_PinnedApps, IID_PinnedApps, (void**)&pa)) && pa) {
                HRESULT hr = pin ? pa->PinAppID((LPCWSTR)AppId()) : pa->UnpinAppID((LPCWSTR)AppId());
                ok = SUCCEEDED(hr);
                pa->Release();
            }
            sp->Release();
        }
        if (SUCCEEDED(co)) CoUninitialize();
        return ok;
    }

private:
    static const char16_t* AppId() { return u"GarpsuKim.gsHomeManager"; }
};

// ---------------------------------------------------------------- CITIES / Zone
// Global 메뉴의 주요 도시. zone 은 Windows 시간대 키 이름 (레지스트리 Time Zones 아래)
struct City { const char* name; const char* zone; };

static const City CITIES[] = {
    {"서울",         "Korea Standard Time"},
    {"도쿄",         "Tokyo Standard Time"},
    {"베이징",       "China Standard Time"},
    {"싱가포르",     "Singapore Standard Time"},
    {"방콕",         "SE Asia Standard Time"},
    {"델리",         "India Standard Time"},
    {"두바이",       "Arabian Standard Time"},
    {"이스탄불",     "Turkey Standard Time"},
    {"모스크바",     "Russian Standard Time"},
    {"카이로",       "Egypt Standard Time"},
    {"요하네스버그", "South Africa Standard Time"},
    {"런던",         "GMT Standard Time"},
    {"파리",         "Romance Standard Time"},
    {"베를린",       "W. Europe Standard Time"},
    {"시드니",       "AUS Eastern Standard Time"},
    {"오클랜드",     "New Zealand Standard Time"},
    {"뉴욕",         "Eastern Standard Time"},
    {"시카고",       "Central Standard Time"},
    {"덴버",         "Mountain Standard Time"},
    {"로스앤젤레스", "Pacific Standard Time"},
    {"호놀룰루",     "Hawaiian Standard Time"},
    {"상파울루",     "E. South America Standard Time"},
};
static const int CITY_COUNT = (int)(sizeof(CITIES) / sizeof(CITIES[0]));

// 도시의 현재 시각 (시간대 + 서머타임 규칙 반영). Load 하지 않았거나 실패하면 이 PC 의 로컬 시각
class Zone {
public:
    bool Load(const char* key) {
        char path[160];
        wsprintfA(path, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%s", key);
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
        struct Tzi { LONG bias, stdBias, dstBias; SYSTEMTIME stdDate, dstDate; } tz;   // REG_TZI_FORMAT (44바이트)
        DWORD sz = sizeof(tz), type = 0;
        LONG rc = RegQueryValueExA(k, "TZI", NULL, &type, (LPBYTE)&tz, &sz);
        RegCloseKey(k);
        if (rc != ERROR_SUCCESS || sz != sizeof(tz)) return false;
        tzi_.Bias         = tz.bias;
        tzi_.StandardBias = tz.stdBias;
        tzi_.DaylightBias = tz.dstBias;
        tzi_.StandardDate = tz.stdDate;
        tzi_.DaylightDate = tz.dstDate;
        valid_ = true;
        return true;
    }

    void Now(SYSTEMTIME& t) {
        if (valid_) {
            SYSTEMTIME u;
            GetSystemTime(&u);
            if (SystemTimeToTzSpecificLocalTime(&tzi_, &u, &t)) return;
        }
        GetLocalTime(&t);
    }

private:
    TIME_ZONE_INFORMATION tzi_ = {};
    bool valid_ = false;
};

// ---------------------------------------------------------------- Alarm
static const int ALARM_COUNT = 10;              // 알람1 ~ 알람10
static const int ALARM_SCAN_ON  = ALARM_COUNT;      // 칸 10 = ScanOn 시각 (scan 시작 예약, ini [WiFiScan] ScanOn*)
static const int ALARM_SCAN_OFF = ALARM_COUNT + 1;  // 칸 11 = ScanOff 시각 (scan 종료 예약, ini [WiFiScan] ScanOff*)
static const int ALARM_TOTAL    = ALARM_COUNT + 2;

struct Alarm {
    bool on = false;                            // 사용 여부
    int  h = 0, m = 0, s = 0;                   // 알람 시각 (24시간제, 이 PC 로컬 시각)
    char file[784] = "";                        // 재생할 동영상/음악 파일 경로 (UTF-8)
    int  closeMode = 0;                         // 플레이어 자동 닫기: 0=끔, 1=재생이 끝나면(파일 길이), 2=지정 시간 후
    char memo[320] = "";                       // 알람 메모 (UTF-8, 최대 100자). 알람 시각에 모니터 중앙에 30초 표시
    int  closeSec  = 60;                        // 모드 2 의 닫기까지 시간(초). 모드 1 에서 길이를 못 읽을 때도 사용 (0=닫지 않음)
};

// ---------------------------------------------------------------- 아날로그 시계 테마
struct AnalogTheme {
    const char* name;                           // 메뉴에 표시할 이름
    COLORREF bezel, face, tickMin, tickHour, text, hand, sec;   // 테두리 / 문자판 / 분 눈금 / 시 눈금 / 요일·날짜 글자 / 시침·분침 / 초침
    bool rainbow;                               // true: 테두리와 눈금을 무지개 색으로
};
static const AnalogTheme ATHEMES[] = {
    {"블랙 테마",     RGB( 70,  70,  70), RGB( 24,  24,  24), RGB(120, 120, 120), RGB(225, 225, 225), RGB(205, 205, 205), RGB(235, 235, 235), RGB(230,  60,  50), false},
    {"화이트 테마",   RGB(150, 150, 150), RGB(246, 246, 246), RGB(150, 150, 150), RGB( 80,  80,  80), RGB( 85,  85,  85), RGB( 70,  70,  70), RGB(205,  40,  40), false},
    {"푸른색 테마",   RGB( 30,  90, 170), RGB(225, 240, 255), RGB(110, 150, 200), RGB( 25,  70, 140), RGB( 30,  80, 150), RGB( 25,  65, 130), RGB(245, 130,  20), false},
    {"빨간색 테마",   RGB(170,  30,  35), RGB(255, 235, 232), RGB(205, 110, 110), RGB(150,  25,  30), RGB(150,  30,  35), RGB(130,  20,  25), RGB( 40,  40,  40), false},
    {"무지개 테마",   RGB(150, 150, 150), RGB(250, 250, 250), RGB(150, 150, 150), RGB( 80,  80,  80), RGB( 85,  85,  85), RGB( 70,  70,  70), RGB(205,  40,  40), true},
    {"초록색 테마",   RGB( 35, 120,  65), RGB(230, 247, 234), RGB(120, 170, 135), RGB( 30, 100,  55), RGB( 30, 100,  55), RGB( 25,  85,  45), RGB(220,  50,  40), false},
    {"금색 테마",     RGB(190, 150,  50), RGB(255, 248, 225), RGB(200, 170,  90), RGB(140, 100,  20), RGB(130,  95,  20), RGB(110,  80,  15), RGB(190,  35,  35), false},
    {"보라색 테마",   RGB(110,  60, 160), RGB(244, 236, 252), RGB(160, 125, 190), RGB( 90,  45, 140), RGB( 95,  50, 140), RGB( 80,  40, 120), RGB(230,  80, 140), false},
    {"분홍색 테마",   RGB(225, 110, 150), RGB(255, 240, 245), RGB(230, 160, 185), RGB(200,  80, 120), RGB(190,  70, 110), RGB(170,  60, 100), RGB( 60,  60,  60), false},
    {"청록색 테마",   RGB( 20, 130, 140), RGB(228, 248, 248), RGB(110, 175, 180), RGB( 15, 105, 115), RGB( 15, 105, 115), RGB( 10,  85,  95), RGB(235,  95,  40), false},
    {"세피아 테마",   RGB(120,  85,  55), RGB(245, 235, 210), RGB(170, 140, 100), RGB( 95,  65,  40), RGB(100,  70,  45), RGB( 80,  55,  35), RGB(170,  50,  30), false},
};
static const int ATHEME_COUNT = (int)(sizeof(ATHEMES) / sizeof(ATHEMES[0]));
static const COLORREF ANALOG_KEY = RGB(1, 0, 1);    // 아날로그 모드에서 원 바깥을 완전 투명으로 만드는 색 (창 배경에만 사용)

// ---------------------------------------------------------------- Settings
// gsHomeManager.ini (exe 와 같은 폴더) 관리. 값은 시작할 때 읽고, 변경할 때마다 저장한다.
class Settings {
public:
    // [Timer] (ms)
    int      moveMs       = 30000;      // 창 위치 이동 간격
    int      timeScrollMs = 100;        // 시분초 스크롤 간격
    int      dateScrollMs = 200;        // 년월일 스크롤 간격
    // [Style]
    COLORREF bgColor      = RGB(0, 0, 0);
    int      opacity      = 100;        // 10~100 (%)
    char     timeFont[64] = "Consolas";
    int      timeSize     = 56;         // 픽셀 높이
    COLORREF timeColor    = RGB(0, 255, 120);
    char     dateFont[64] = "Segoe UI";
    int      dateSize     = 12;
    COLORREF dateColor    = RGB(180, 180, 180);
    // [Option]
    bool     topmost      = false;      // 항상 다른 앱보다 앞에
    bool     allMon       = false;      // 랜덤 이동을 모든 모니터로
    bool     allDesk      = false;      // 모든 가상 데스크톱에 표시
    bool     visible      = true;       // 시계 보이기 (false = 숨기고 시스템 트레이에서만 동작 표시)
    bool     backFollow   = false;      // 바탕색을 모니터 현재 바탕화면(단색)에 맞춤
    bool     showLunar    = false;      // 음력 날짜 표시 (년월일 다음 줄)
    char     lunarUrl[300] = "";        // [Lunar] ApiUrl: 천문연구원 getLunCalInfo 주소 (ini 에서만 가져옴)
    char     lunarKey[200] = "";        // [Lunar] ApiKey: 공공API 인증키 (ini 에서만 가져옴)
    bool LunarReady() const { return lunarUrl[0] && lunarKey[0]; }   // 주소와 키가 모두 있어야 음력 사용 가능
    bool LunarOn() const    { return showLunar && LunarReady(); }
    bool     showDate     = true;       // 년월일(도시 이름 포함) 표시
    bool     trayStart    = false;      // [Option] tray_start=TRUE 이면 시작할 때 화면에 보이지 않고 바로 트레이로
    static const int COMPANION_MAX = 5;     // 동반 실행 앱 최대 개수
    char     companionApp[COMPANION_MAX][784] = {};   // [Option] companion_app1 ~ companion_app5: 시작할 때 함께 실행할 BAT/CMD/EXE 전체 경로 (UTF-8)
    bool     companionInvoker = false;  // [Option] companion_run_as_invoker=TRUE: 동반 실행 앱의 관리자 권한 요청을 무시 (RunAsInvoker, 일반 권한으로 실행)
    bool     analogNoSec  = false;      // [Analog] HideSecond: 초침 생략
    int      analogSize   = 200;        // [Analog] Size: 아날로그 시계 한 변(px). 마우스 휠로 확대/축소하며 마지막 크기를 보관
    bool     analogMatch  = false;      // [Analog] MatchBg: 시계 원 안쪽 바탕색을 모니터 바탕색(단색일 때)과 일치
    bool     deskSolid    = false;      // (실행 중 값) 모니터 바탕이 단색인지
    COLORREF deskColor    = 0;          // (실행 중 값) 모니터 바탕 단색
    bool     analogFull   = false;      // [Analog] Full: 아날로그 전체화면 (모니터 중앙 고정 + 크기 = 모니터 높이)
    bool     analogNeon   = false;      // [Analog] Neon: 네온사인 효과 (어두운 문자판 + 빛나는 선)
    int      analogTheme  = 1;          // [Analog] Theme: ATHEMES 인덱스 (0 블랙, 1 화이트 ...)
    int      clockMode    = 0;          // [Option] ClockMode: 0=[타이틀 & 디지털] 1=[디지털] 2=[아날로그] (마우스 클릭 때마다 순환, 시작할 때 이 모드로 시작)
    bool     bootingAlert = false;      // [Option] booting_alert=TRUE 이면 시작할 때 .\booting_alert\booting_Alert_RUN.BAT 실행
    int      timeFmt      = 0;          // 시분초 표시 형식 (0~9)
    int      dateFmt      = 0;          // 년월일 표시 형식 (0~9)
    // [Global]
    char     baseCity[32] = "서울";     // 본 창의 기준 도시 이름 (시간은 이 PC 시간대)
    char     cities[1024] = "";         // Global 도시 창 목록 (도시 이름, 쉼표 구분). 시작할 때 모두 복원
    // [Alarm]
    Alarm    alarms[ALARM_TOTAL];       // 0~9 알람, 10 ScanOn, 11 ScanOff
    bool     pgOn = false;              // 프로그램 실행 알람 사용
    int      pgH = 0, pgM = 0, pgS = 0; // 프로그램 실행 시각
    char     pgFile[784] = "";          // 실행할 BAT/CMD/EXE 경로 (UTF-8)
    char     pgMemo[320] = "";          // 실행할 때 모니터 중앙에 30초 표시할 메모
    struct HomeProg { bool on = false; char file[784] = ""; char memo[320] = ""; };
    HomeProg homeProg[2];               // [0] HomeBack 프로그램, [1] NotHome 프로그램 (/HomeBack, /NotHome 판정이 날 때 실행)
    bool     rbOn = false;              // 시스템 재시작 사용
    int      rbH = 0, rbM = 0, rbS = 0; // 시스템 재시작 시각
    // [IPCam] 폰 카메라 앱(DroidCam, IP Webcam 등)의 주소
    char     ipHost[64] = "";           // IP address (없으면 카메라 영상 창에서 IP Cam 을 실행하기 전에 등록)
    int      ipPort     = 4747;         // port 번호 (DroidCam 4747, IP Webcam 8080)
    // [PhoneFtp] 폰 탐색기: 폰의 FTP 서버(CX파일탐색기 등) 주소. 비밀번호는 저장하지 않음
    char     phHost[32] = "";           // IP address (a.b.c.d)
    int      phPort     = 2121;         // port 번호
    char     phUser[64] = "";           // 사용자명
    // [RdpServer] 다른 PC(서버): 이 PC 를 원격으로 제어하게 함 (예전 rdp_server.ini 의 [server]). 비밀번호는 ini 에 평문으로 저장됨
    int      rsPort       = 7071;       // port
    char     rsPass[200]  = "";         // password
    char     rsAllow[40]  = "any";      // allowed_ip: any = 모든 IP 허용
    int      rsAccept     = 0;          // accept: 0 once(클라이언트 IP 마다 처음 한 번만 묻기) / 1 auto(묻지 않음) / 2 always(접속할 때마다 묻기)
    bool     rsAuto       = false;      // autostart: 프로그램을 시작할 때 서버를 자동으로 시작
    bool     rsOsk        = false;      // osk: 클라이언트가 연결되면 화상 키보드(osk.exe) 실행
    // [RdpClient] 다른 PC(클라이언트) (예전 rdp_client.ini 의 [client])
    char     rcIp[40]     = "";         // 서버 IP address
    int      rcPort       = 7071;
    char     rcPass[200]  = "";
    // [WiFiScan] 집 안팎 감시 (WiFiScan.cpp 가 screen_telegram.ini 에서 읽던 키를 여기서 읽음)
    bool     scanBt       = true;       // ScanBluetooth: 블루투스를 scan 대상으로
    bool     scanWifi     = true;       // ScanWifi: Wi-Fi(LAN)를 scan 대상으로
    char     homeBt[32]   = "";         // HomeBluetooth: 폰 블루투스 주소 (AA:BB:CC:DD:EE:FF)
    char     homeMac[32]  = "";         // HomeMac: 폰 Wi-Fi(LAN 카드) MAC
    char     homeIp[24]   = "";         // HomeIp: 폰 고정 IP (선택)
    int      scanSec      = 15;         // WiFiScanWatchInterval: scan(감시) 주기 (초, 최소 5)
    int      notHomeSec   = 60;         // NotHome: 외출 판정 (초)

    void Init() {
        InitPath();
        CreateDefaultIni();
        Load();
    }

    // 기본값으로 되돌림 (기준 도시와 창 위치는 유지). 기본값은 위 멤버 초기값 한 곳에서만 정의
    void ResetDefaults() {
        Settings d;
        moveMs = d.moveMs;  timeScrollMs = d.timeScrollMs;  dateScrollMs = d.dateScrollMs;
        bgColor = d.bgColor;  backFollow = d.backFollow;  opacity = d.opacity;
        lstrcpyA(timeFont, d.timeFont);  timeSize = d.timeSize;  timeColor = d.timeColor;
        lstrcpyA(dateFont, d.dateFont);  dateSize = d.dateSize;  dateColor = d.dateColor;
        analogTheme = d.analogTheme;  analogNeon = d.analogNeon;  analogMatch = d.analogMatch;  analogSize = d.analogSize;  analogNoSec = d.analogNoSec;
        topmost = d.topmost;  allMon = d.allMon;  allDesk = d.allDesk;  timeFmt = d.timeFmt;  dateFmt = d.dateFmt;
    }

    // 음력 API 키 변경: 메모리와 ini [Lunar] ApiKey 에 저장
    void SetLunarKey(const char* key) {
        lstrcpynA(lunarKey, key, sizeof(lunarKey));
        WritePrivateProfileStringA("Lunar", "ApiKey", lunarKey, ini_);
    }

    void WriteInt(const char* section, const char* key, int v) {
        char b[16];
        wsprintfA(b, "%d", v);
        WritePrivateProfileStringA(section, key, b, ini_);
    }

    void SaveStyle() {
        WriteColor("Style", "BackColor", bgColor);
        WriteInt("Style", "BackFollowDesktop", backFollow);
        WriteInt("Style", "Opacity", opacity);
        WritePrivateProfileStringA("Style", "TimeFont", timeFont, ini_);
        WriteInt("Style", "TimeSize", timeSize);
        WriteColor("Style", "TimeColor", timeColor);
        WritePrivateProfileStringA("Style", "DateFont", dateFont, ini_);
        WriteInt("Style", "DateSize", dateSize);
        WriteColor("Style", "DateColor", dateColor);
    }

    void SaveAll() {
        WriteInt("Timer", "MoveMs", moveMs);
        WriteInt("Timer", "TimeScrollMs", timeScrollMs);
        WriteInt("Timer", "DateScrollMs", dateScrollMs);
        SaveStyle();
        WriteInt("Option", "AlwaysOnTop", topmost);
        WriteInt("Option", "AllMonitors", allMon);
        WriteInt("Option", "AllDesktops", allDesk);
        WriteInt("Option", "TimeFormat", timeFmt);
        WriteInt("Option", "DateFormat", dateFmt);
    }

    // 저장된 위치가 있으면 x,y 에 채움 (가상 화면 안으로 보정)
    void LoadPos(int& x, int& y, int w, int h) {
        const int NONE = 0x7FFFFFFF;
        int px = (int)GetPrivateProfileIntA("Window", "X", NONE, ini_);
        int py = (int)GetPrivateProfileIntA("Window", "Y", NONE, ini_);
        if (px == NONE || py == NONE) return;

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (px > vx + vw - w) px = vx + vw - w;
        if (py > vy + vh - h) py = vy + vh - h;
        if (px < vx) px = vx;
        if (py < vy) py = vy;
        x = px;
        y = py;
        lastX_ = px;
        lastY_ = py;
    }

    // 창 위치 저장 (이전과 같으면 디스크에 쓰지 않음)
    void SavePos(int x, int y) {
        if (x == lastX_ && y == lastY_) return;
        lastX_ = x;
        lastY_ = y;
        WriteInt("Window", "X", x);
        WriteInt("Window", "Y", y);
    }

    // 프로그램 실행 알람 저장: ProgOn / ProgTime / ProgFile / ProgMemo
    void SaveProg() {
        char t[16];
        WriteInt("Alarm", "ProgOn", pgOn);
        wsprintfA(t, "%02d:%02d:%02d", pgH, pgM, pgS);
        WritePrivateProfileStringA("Alarm", "ProgTime", t, ini_);
        WritePrivateProfileStringA("Alarm", "ProgFile", pgFile, ini_);
        WritePrivateProfileStringA("Alarm", "ProgMemo", pgMemo, ini_);
    }

    // HomeBack / NotHome 프로그램 저장: [WiFiScan] HomeBackProgOn / HomeBackProgFile / HomeBackProgMemo, NotHomeProg... 도 같음
    void SaveHomeProg(int i) {
        const char* p = i == 0 ? "HomeBackProg" : "NotHomeProg";
        char k[40];
        wsprintfA(k, "%sOn", p);
        WriteInt("WiFiScan", k, homeProg[i].on);
        wsprintfA(k, "%sFile", p);
        WritePrivateProfileStringA("WiFiScan", k, homeProg[i].file, ini_);
        wsprintfA(k, "%sMemo", p);
        WritePrivateProfileStringA("WiFiScan", k, homeProg[i].memo, ini_);
    }

    // IP cam 등록 저장: [IPCam] Host / Port
    void SaveIpCam() {
        WritePrivateProfileStringA("IPCam", "Host", ipHost, ini_);
        WriteInt("IPCam", "Port", ipPort);
    }

    // 동반 실행 앱 저장: [Option] companion_app1 ~ companion_app5 = 전체 경로, companion_run_as_invoker = TRUE/FALSE
    void SaveCompanionApps() {
        WritePrivateProfileStringA("Option", "companion_run_as_invoker", companionInvoker ? "TRUE" : "FALSE", ini_);
        for (int i = 0; i < COMPANION_MAX; i++) {
            char k[24];
            wsprintfA(k, "companion_app%d", i + 1);
            WritePrivateProfileStringA("Option", k, companionApp[i], ini_);
        }
    }
    bool HasCompanionApp() const {
        for (int i = 0; i < COMPANION_MAX; i++) if (companionApp[i][0]) return true;
        return false;
    }

    // 시계 모드 저장: [Option] ClockMode=0/1/2
    void SaveClockMode() { WriteInt("Option", "ClockMode", clockMode); }
    void SaveAnalogTheme() { WriteInt("Analog", "Theme", analogTheme); }
    void SaveAnalogNeon() { WriteInt("Analog", "Neon", analogNeon); }
    void SaveAnalogFull() { WriteInt("Analog", "Full", analogFull); }
    void SaveAnalogMatch() { WriteInt("Analog", "MatchBg", analogMatch); }
    void SaveAnalogSize() { WriteInt("Analog", "Size", analogSize); }
    void SaveAnalogNoSec() { WriteInt("Analog", "HideSecond", analogNoSec); }

    // 트레이로 시작 저장: [Option] tray_start=TRUE/FALSE ('시계 보이기' 메뉴를 바꿀 때 함께 저장: 숨김=TRUE, 보임=FALSE)
    void SaveTrayStart(bool on) {
        trayStart = on;
        WritePrivateProfileStringA("Option", "tray_start", on ? "TRUE" : "FALSE", ini_);
    }

    // 다른 PC(서버) 저장: [RdpServer] port / password / allowed_ip / accept / autostart
    void SaveRdpServer() {
        WriteInt("RdpServer", "port", rsPort);
        WritePrivateProfileStringA("RdpServer", "password", rsPass, ini_);
        WritePrivateProfileStringA("RdpServer", "allowed_ip", rsAllow, ini_);
        WritePrivateProfileStringA("RdpServer", "accept", rsAccept == 1 ? "auto" : rsAccept == 2 ? "always" : "once", ini_);
        WriteInt("RdpServer", "autostart", rsAuto);
        WriteInt("RdpServer", "osk", rsOsk);
    }
    // 다른 PC(클라이언트) 저장: [RdpClient] ip / port / password
    void SaveRdpClient() {
        WritePrivateProfileStringA("RdpClient", "ip", rcIp, ini_);
        WriteInt("RdpClient", "port", rcPort);
        WritePrivateProfileStringA("RdpClient", "password", rcPass, ini_);
    }

    // 폰 탐색기 저장: [PhoneFtp] Host / Port / User (비밀번호는 저장하지 않음)
    void SavePhoneFtp() {
        WritePrivateProfileStringA("PhoneFtp", "Host", phHost, ini_);
        WriteInt("PhoneFtp", "Port", phPort);
        WritePrivateProfileStringA("PhoneFtp", "User", phUser, ini_);
    }

    // 시스템 재시작 시각 저장: RebootOn / RebootTime
    void SaveReboot() {
        char t[16];
        WriteInt("Alarm", "RebootOn", rbOn);
        wsprintfA(t, "%02d:%02d:%02d", rbH, rbM, rbS);
        WritePrivateProfileStringA("Alarm", "RebootTime", t, ini_);
    }

    // 알람 i (0~9) 저장: AlarmNOn / AlarmNTime / AlarmNFile
    // ScanOn / ScanOff 예약 저장 ([WiFiScan] ScanOnUse / ScanOnTime / ScanOnFile / ScanOnMemo / ScanOnClose / ScanOnCloseSec, ScanOff 도 같음)
    void SaveScanSched(int i) {
        const Alarm& a = alarms[i];
        const char* p = i == ALARM_SCAN_ON ? "ScanOn" : "ScanOff";
        char k[40], t[16];
        wsprintfA(k, "%sUse", p);
        WriteInt("WiFiScan", k, a.on);
        wsprintfA(k, "%sTime", p);
        wsprintfA(t, "%02d:%02d:%02d", a.h, a.m, a.s);
        WritePrivateProfileStringA("WiFiScan", k, t, ini_);
        wsprintfA(k, "%sFile", p);
        WritePrivateProfileStringA("WiFiScan", k, a.file, ini_);
        wsprintfA(k, "%sMemo", p);
        WritePrivateProfileStringA("WiFiScan", k, a.memo, ini_);
        wsprintfA(k, "%sClose", p);
        WriteInt("WiFiScan", k, a.closeMode);
        wsprintfA(k, "%sCloseSec", p);
        WriteInt("WiFiScan", k, a.closeSec);
    }

    void SaveAlarm(int i) {
        if (i >= ALARM_COUNT) { SaveScanSched(i); return; }
        const Alarm& a = alarms[i];
        char k[24], t[16];
        wsprintfA(k, "Alarm%dOn", i + 1);
        WriteInt("Alarm", k, a.on);
        wsprintfA(k, "Alarm%dTime", i + 1);
        wsprintfA(t, "%02d:%02d:%02d", a.h, a.m, a.s);
        WritePrivateProfileStringA("Alarm", k, t, ini_);
        wsprintfA(k, "Alarm%dFile", i + 1);
        WritePrivateProfileStringA("Alarm", k, a.file, ini_);
        wsprintfA(k, "Alarm%dMemo", i + 1);
        WritePrivateProfileStringA("Alarm", k, a.memo, ini_);
        wsprintfA(k, "Alarm%dClose", i + 1);
        WriteInt("Alarm", k, a.closeMode);
        wsprintfA(k, "Alarm%dCloseSec", i + 1);
        WriteInt("Alarm", k, a.closeSec);
    }

    // 현재 표시 중인 도시 창 목록 저장 (csv: "런던,뉴욕")
    void SaveCities(const char* csv) {
        WritePrivateProfileStringA("Global", "Cities", csv, ini_);
    }

    // [WiFiScan] 저장 (WiFiScan 대화상자의 저장 / scan 재시작)
    void SaveScan() {
        WriteInt("WiFiScan", "ScanBluetooth", scanBt);
        WriteInt("WiFiScan", "ScanWifi", scanWifi);
        WritePrivateProfileStringA("WiFiScan", "HomeBluetooth", homeBt, ini_);
        WritePrivateProfileStringA("WiFiScan", "HomeMac", homeMac, ini_);
        WritePrivateProfileStringA("WiFiScan", "HomeIp", homeIp, ini_);
        WriteInt("WiFiScan", "WiFiScanWatchInterval", scanSec);
        WriteInt("WiFiScan", "NotHome", notHomeSec);
    }

private:
    // exe 와 같은 폴더, 같은 이름의 .ini
    void InitPath() {
        GetModuleFileNameA(NULL, ini_, MAX_PATH);
        int i = lstrlenA(ini_);
        while (i > 0 && ini_[i - 1] != '.' && ini_[i - 1] != '\\') i--;
        if (i > 0 && ini_[i - 1] == '.') lstrcpyA(ini_ + i, "ini");
        else                             lstrcatA(ini_, ".ini");
    }

    // ini 파일이 없을 때만 주석이 달린 기본 ini 를 만든다 (UTF-8, BOM 없음, CRLF)
    // 주석(;)은 키 위의 별도 줄에 둔다 (ini API 는 값 뒤 주석을 지원하지 않음)
    void CreateDefaultIni() {
        if (GetFileAttributesA(ini_) != INVALID_FILE_ATTRIBUTES) return;
        static const char text[] =
            "; ===== gsHomeManager 설정 =====\r\n"
            "; 앱 시작 시 한 번 읽습니다. 수정 후 앱을 다시 시작하세요.\r\n"
            "; 주석은 반드시 별도 줄에 쓰세요 (값 뒤에 붙이면 값으로 읽힙니다).\r\n"
            "; 파일은 UTF-8 (BOM 없음) 또는 ANSI 로 저장하세요.\r\n"
            "\r\n"
            "[Timer]\r\n"
            "; 윈도우 위치를 랜덤으로 옮기는 시간 간격 (밀리초, 0=이동 안 함/고정, 그 외 최소 10)\r\n"
            "MoveMs=30000\r\n"
            "; 시분초 글자가 좌우로 스크롤되는 시간 간격 (밀리초, 최소 10)\r\n"
            "TimeScrollMs=100\r\n"
            "; 년월일 글자가 좌우로 스크롤되는 시간 간격 (밀리초, 최소 10)\r\n"
            "DateScrollMs=200\r\n"
            "\r\n"
            "[Style]\r\n"
            "; 윈도우 바탕색 (RRGGBB 16진수, 예: 000000=검정, FFFFFF=흰색)\r\n"
            "BackColor=000000\r\n"
            "; 1=바탕색을 모니터의 현재 바탕화면 단색에 맞춤 (바탕화면이 단색일 때만 적용), 0=BackColor 사용\r\n"
            "BackFollowDesktop=0\r\n"
            "; 윈도우 투명도 (10~100, 100=불투명)\r\n"
            "Opacity=100\r\n"
            "; 시분초 폰트 이름 (예: Consolas, 맑은 고딕)\r\n"
            "TimeFont=Consolas\r\n"
            "; 시분초 폰트 크기 (픽셀 높이, 6~400)\r\n"
            "TimeSize=56\r\n"
            "; 시분초 폰트 색깔 (RRGGBB 16진수)\r\n"
            "TimeColor=00FF78\r\n"
            "; 년월일 폰트 이름\r\n"
            "DateFont=Segoe UI\r\n"
            "; 년월일 폰트 크기 (픽셀 높이, 6~400)\r\n"
            "DateSize=12\r\n"
            "; 년월일 폰트 색깔 (RRGGBB 16진수)\r\n"
            "DateColor=B4B4B4\r\n"
            "\r\n"
            "[Option]\r\n"
            "; 항상 다른 앱보다 앞에 보이기 (1=On, 0=Off)\r\n"
            "AlwaysOnTop=0\r\n"
            "; 랜덤 이동을 모든 모니터에 적용 (1=On, 0=Off, Off 이면 기본 모니터에서만 이동)\r\n"
            "AllMonitors=0\r\n"
            "; 모든 가상 데스크톱에 표시 (1=On, 0=Off)\r\n"
            "AllDesktops=0\r\n"
            "; 시각(시분초) 표시 방식 (0=24시간제, 1=12시간제 AM/PM)\r\n"
            "; 시분초 표시 형식 (0~9): 0=HH:mm:ss 1=hh:mm:ss tt 2=H:mm:ss 3=h:mm:ss tt 4=tt hh:mm:ss 5=오전/오후 h:mm:ss 6=HH:mm 7=HH시 mm분 ss초 8=HH.mm.ss 9=HHmmss\r\n"
            "TimeFormat=0\r\n"
            "; 년월일 표시 형식 (0~9): 0=yyyy-MM-dd (요일) 1=yyyy-MM-dd 2=yyyy.MM.dd 3=yyyy/MM/dd 4=yyyy년 M월 d일 (요일) 5=yy-MM-dd 6=MM/dd/yyyy 7=dd/MM/yyyy 8=yyyyMMdd 9=ddd, MMM d, yyyy\r\n"
            "DateFormat=0\r\n"
            "; ShowClock: 1=시계 보이기, 0=숨기기 (시스템 트레이 아이콘만 표시). 메뉴 앱 동작 설정 > 시계 보이기 로 바꿀 수 있고, 앱을 시작할 때는 항상 1(보이기)로 시작합니다.\r\n"
            "ShowClock=1\r\n"
            "; ShowDate: 1=년월일 보이기, 0=숨기기 (시분초만 표시). 메뉴 앱 동작 설정 > 년월일 보이기 로 바꿀 수 있습니다.\r\n"
            "ShowDate=1\r\n"
            "; ShowLunar: 1=년월일 다음 줄에 음력 날짜 표시 (천문연구원 공공API 조회, 인터넷 필요), 0=표시 안 함. 메뉴 앱 동작 설정 > 음력 보이기\r\n"
            "ShowLunar=0\r\n"
            "; ClockMode: 시계 모양. 0=타이틀바 + 디지털, 1=디지털, 2=아날로그. 시계를 마우스로 한 번 클릭할 때마다 0 -> 1 -> 2 -> 0 으로 바뀌고, 앱을 시작할 때는 이 값의 모양으로 바로 시작합니다.\r\n"
            "ClockMode=0\r\n"
            "; booting_alert: TRUE 이면 앱을 시작할 때마다 앱 폴더의 booting_alert\\booting_Alert_RUN.BAT 를 실행합니다. FALSE 이면 실행하지 않습니다.\r\n"
            "booting_alert=FALSE\r\n"
            "; tray_start: TRUE 이면 앱을 시작할 때 화면에 보이지 않고 바로 시스템 트레이로 보냅니다 (5초 동안 안내 메시지 표시). 트레이 아이콘을 누르면 다시 볼 수 있습니다.\r\n"
            "tray_start=FALSE\r\n"
            "; companion_app1 ~ companion_app5: 앱을 시작할 때 함께 실행할 BAT/CMD/EXE/LNK 의 전체 경로 (최대 5개, 번호 순서대로 실행). 메뉴 앱 동작 설정 > 동반 실행 앱 등록 에서 바꿀 수 있습니다. 비워 두면 실행하지 않습니다.\r\n"
            "companion_app1=\r\n"
            "companion_app2=\r\n"
            "companion_app3=\r\n"
            "companion_app4=\r\n"
            "companion_app5=\r\n"
            "; companion_run_as_invoker: TRUE 이면 동반 실행 앱이 관리자 권한을 요구해도 확인창 없이 일반 권한으로 실행합니다 (RunAsInvoker). 관리자 전용 기능은 동작하지 않을 수 있습니다.\r\n"
            "companion_run_as_invoker=FALSE\r\n"
            "\r\n"
            "[Analog]\r\n"
            "; Theme: 아날로그 시계 테마. 0=블랙 1=화이트 2=푸른색 3=빨간색 4=무지개 5=초록색 6=금색 7=보라색 8=분홍색 9=청록색 10=세피아. 메뉴 F4) 아날로그 시계 디자인 에서 바꿀 수 있습니다.\r\n"
            "Theme=1\r\n"
            "; Neon: 1=네온사인 효과 (어두운 바탕에 테마 색으로 빛나는 테두리/눈금/바늘), 0=끔. 메뉴 F4) 아나로그 시계 디자인 > 네온사인 on/off\r\n"
            "Neon=0\r\n"
            "; Full: 1=아날로그 모드에서 전체화면 (모니터 중앙에 고정, 크기를 모니터 수직 높이로 확대). 시계 모양이 아날로그일 때만 적용됩니다. 메뉴 F4) 아날로그 시계 디자인 > 전체화면 on/off\r\n"
            "Full=0\r\n"
            "; MatchBg: 1=시계 원 안쪽 바탕색을 모니터 바탕색과 일치시킴 (모니터 바탕이 단색일 때만. 그림이면 테마 색 사용). 메뉴 F4) 아날로그 시계 디자인 > 배경색 일치 on/off\r\n"
            "MatchBg=0\r\n"
            "; Size: 아날로그 시계 한 변의 크기(픽셀). 아날로그 모드에서 마우스 휠을 굴리면 확대/축소되고, 마지막 크기가 여기에 저장됩니다. (60 ~ 모니터 높이)\r\n"
            "Size=200\r\n"
            "; HideSecond: 1=초침을 그리지 않음, 0=초침 표시. 메뉴 F4) 아날로그 시계 디자인 > 초침 생략 on/off\r\n"
            "HideSecond=0\r\n"
            "\r\n"
            "[Lunar]\r\n"
            "; 음력 조회용 천문연구원 공공API. 주소와 인증키가 둘 다 있어야 메뉴 '음력 보이기' 를 쓸 수 있습니다 (없으면 메뉴 비활성).\r\n"
            "ApiUrl=http://apis.data.go.kr/B090041/openapi/service/LrsrCldInfoService/getLunCalInfo\r\n"
            "; 공공데이터포털에서 발급받은 인증키(Decoding 키 또는 Encoding 키)를 넣으세요.\r\n"
            "ApiKey=\r\n"
            "\r\n"
            "[Global]\r\n"
            "; 기준 도시 이름 (본 창의 날짜 앞에 표시. 시간은 이 PC 의 시간대 기준)\r\n"
            "BaseCity=서울\r\n"
            "; 표시 중인 Global 도시 창 목록 (쉼표로 구분, 예: 런던,뉴욕). 도시를 추가/닫을 때와 종료할 때 자동 저장되고, 시작할 때 모두 다시 열립니다.\r\n"
            "Cities=\r\n"
            "\r\n"
            "[Alarm]\r\n"
            "; 알람 1~10. 메뉴 '알람' 에서 설정하면 자동 저장됩니다. 직접 쓸 때는 N 에 1~10 을 넣으세요.\r\n"
            "; AlarmNOn=1(사용)/0(해제), AlarmNTime=HH:MM:SS (24시간제, 이 PC 시각), AlarmNFile=재생할 동영상/음악 파일 경로\r\n"
            "; 알람 시각이 되면 시스템 기본 플레이어로 파일을 엽니다. 사용 중인 알람은 매일 반복됩니다.\r\n"
            "; AlarmNClose=플레이어 자동 닫기 (0=끔, 1=재생이 끝나면: 파일 길이를 못 읽으면 AlarmNCloseSec 초 후, 2=AlarmNCloseSec 초 후)\r\n"
            "; AlarmNCloseSec=닫기까지 시간(초). 0 이면 닫지 않음\r\n"
            "; AlarmNMemo=알람 시각에 모니터 중앙에 30초 동안 표시할 메모 (최대 100자, 파일 없이 메모만 써도 됨)\r\n"
            "; ProgOn=1 이면 매일 ProgTime(HH:MM:SS) 에 ProgFile(BAT/CMD/EXE) 을 실행하고, ProgMemo 가 있으면 모니터 중앙에 30초 표시합니다\r\n"
            "; RebootOn=1 이면 매일 RebootTime(HH:MM:SS) 에 재시작 확인창(300초)을 띄웁니다. Yes 또는 무응답이면 재시작 (shutdown /r /f /t 0, 앱 강제 종료), No 이면 재시작하지 않습니다\r\n"
            "\r\n"
            "[WiFiScan]\r\n"
            "; 집 안팎 감시 (블루투스 / Wi-Fi 스캔). 앱을 시작하면 자동으로 scan 을 시작합니다. 메뉴 'WiFiScan' 대화상자에서도 바꿀 수 있습니다.\r\n"
            "; 스캔 대상 (1=On, 0=Off). Off 이면 그 스캔과 감시를 하지 않습니다 (결과 파일에는 이전 장치가 OFF 로 남습니다)\r\n"
            "ScanBluetooth=1\r\n"
            "ScanWifi=1\r\n"
            "; 폰 블루투스 주소 (AA:BB:CC:DD:EE:FF, 먼저 PC 와 페어링 필요. 주소는 WiFiScan_result.txt 의 [Bluetooth] 구역에서 확인)\r\n"
            "HomeBluetooth=\r\n"
            "; 폰 Wi-Fi(LAN 카드) MAC 주소 (AA:BB:CC:DD:EE:FF)\r\n"
            "HomeMac=\r\n"
            "; 폰 고정 IP (선택. 있으면 먼저 직접 확인)\r\n"
            "HomeIp=\r\n"
            "; scan(감시) 주기 (초, 최소 5, 0 이면 15)\r\n"
            "WiFiScanWatchInterval=15\r\n"
            "; 외출 판정 시간 (초): 블루투스, Wi-Fi 둘 다 이 시간 동안 연속으로 확인되지 않으면 /NotHome (0 이면 60)\r\n"
            "NotHome=60\r\n"
            "; --- scan 예약 (메뉴 WiFiScan > Wifi공유기 자동감시 On / Off). 아래 키가 없으면 scan 에 아무 영향이 없습니다. 매일 반복됩니다.\r\n"
            "; ScanOnUse / ScanOffUse = 1(사용), 0(해제).  ScanOnTime / ScanOffTime = HH:MM:SS (24시간제, 이 PC 시각)\r\n"
            "; ScanOnFile / ScanOffFile = 그 시각에 시스템 기본 플레이어로 재생할 동영상/음악 파일 경로,  ScanOnMemo / ScanOffMemo = 모니터 중앙에 30초 표시할 메모\r\n"
            "; ScanOnClose / ScanOffClose = 플레이어 자동 닫기 (0=끔, 1=재생이 끝나면, 2=지정 시간 후),  ScanOnCloseSec / ScanOffCloseSec = 닫기까지 시간(초)\r\n"
            "; --- HomeBack / NotHome 프로그램 (메뉴 WiFiScan > HomeBack 프로그램 / NotHome 프로그램). /HomeBack, /NotHome 판정이 날 때마다 실행합니다.\r\n"
            "; HomeBackProgOn / NotHomeProgOn = 1(사용), 0(해제).  HomeBackProgFile / NotHomeProgFile = 실행할 BAT/CMD/EXE 경로,  HomeBackProgMemo / NotHomeProgMemo = 모니터 중앙에 30초 표시할 메모\r\n"
            "\r\n"
            "[IPCam]\r\n"
            "; 폰 카메라 앱(DroidCam, IP Webcam 등)의 IP address 와 port 번호. 메뉴 'IP cam > IP cam 등록' 으로 입력합니다. (DroidCam 4747, IP Webcam 8080)\r\n"
            "Host=\r\n"
            "Port=4747\r\n"
            "\r\n"
            "[RdpServer]\r\n"
            "; 다른 PC(서버): 이 PC 를 원격으로 제어하게 합니다. 메뉴 F8) IP cam / 폰 연결 > 다른 PC(서버) 에서 바꾸고 시작/중지합니다.\r\n"
            "; port = 접속 port,  password = 클라이언트가 입력할 비밀번호 (평문으로 저장되므로 이 ini 를 공유/업로드하지 마세요)\r\n"
            "; allowed_ip = 이 IP 에서 오는 접속만 허용 (any = 제한 없음),  accept = once(클라이언트 IP 마다 처음 한 번만 허용 여부를 묻기) / auto(묻지 않음) / always(접속할 때마다 묻기)\r\n"
            "; autostart = 1 이면 프로그램을 시작할 때 서버를 자동으로 시작 (비밀번호가 있어야 함)\r\n"
            "port=7071\r\n"
            "password=\r\n"
            "allowed_ip=any\r\n"
            "accept=once\r\n"
            "autostart=0\r\n"
            "; osk = 1 이면 클라이언트가 연결될 때 화상 키보드(osk.exe)를 실행\r\n"
            "osk=0\r\n"
            "\r\n"
            "[RdpClient]\r\n"
            "; 다른 PC(클라이언트): 다른 PC 를 원격으로 제어합니다. 메뉴 F8) IP cam / 폰 연결 > 다른 PC(클라이언트) 에서 바꾸고 [연결] 을 누릅니다.\r\n"
            "ip=\r\n"
            "port=7071\r\n"
            "password=\r\n"
            "\r\n"
            "[Window]\r\n"
            "; 윈도우 왼쪽 위 화면 좌표 X, Y (px) 는 앱이 창을 옮기거나 종료할 때 자동 저장됩니다.\r\n"
            "; 직접 고칠 때는 앱을 끈 상태에서 수정하세요.\r\n";
        HANDLE f = CreateFileA(ini_, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD wrote;
        WriteFile(f, text, sizeof(text) - 1, &wrote, NULL);
        CloseHandle(f);
    }

    // 읽기: 키가 없으면 기본값을 ini 에 써 넣어 편집할 수 있게 함
    void Load() {
        moveMs       = ReadInt("Timer", "MoveMs",       moveMs,       0, 0x7FFFFFFF);   // 0 = 이동 안 함
        if (moveMs > 0 && moveMs < 10) moveMs = 10;
        timeScrollMs = ReadInt("Timer", "TimeScrollMs", timeScrollMs, 10, 0x7FFFFFFF);
        dateScrollMs = ReadInt("Timer", "DateScrollMs", dateScrollMs, 10, 0x7FFFFFFF);

        bgColor   = ReadColor("Style", "BackColor", bgColor);
        backFollow = ReadBool("Style", "BackFollowDesktop", backFollow);
        opacity   = ReadInt("Style", "Opacity", opacity, 10, 100);
        ReadStr("Style", "TimeFont", timeFont, sizeof(timeFont));
        timeSize  = ReadInt("Style", "TimeSize", timeSize, 6, 400);
        timeColor = ReadColor("Style", "TimeColor", timeColor);
        ReadStr("Style", "DateFont", dateFont, sizeof(dateFont));
        dateSize  = ReadInt("Style", "DateSize", dateSize, 6, 400);
        dateColor = ReadColor("Style", "DateColor", dateColor);

        topmost = ReadBool("Option", "AlwaysOnTop", topmost);
        allMon  = ReadBool("Option", "AllMonitors", allMon);
        allDesk = ReadBool("Option", "AllDesktops", allDesk);
        {   // 시분초 형식: 예전 ini 의 Hour12 값은 처음 한 번만 이어받음 (0 -> 형식0, 1 -> 형식1)
            int tf = (int)GetPrivateProfileIntA("Option", "TimeFormat", -1, ini_);
            if (tf < 0) tf = GetPrivateProfileIntA("Option", "Hour12", 0, ini_) ? 1 : 0;
            timeFmt = tf > 9 ? 0 : tf;
            WriteInt("Option", "TimeFormat", timeFmt);
            dateFmt = ReadInt("Option", "DateFormat", dateFmt, 0, 9);
        }
        visible = true;                         // 시작할 때는 저장값/트레이 상태와 상관없이 항상 화면에 표시
        WriteInt("Option", "ShowClock", 1);
        showDate = ReadBool("Option", "ShowDate", showDate);
        showLunar = ReadBool("Option", "ShowLunar", showLunar);
        clockMode = ReadInt("Option", "ClockMode", clockMode, 0, 2);
        analogTheme = ReadInt("Analog", "Theme", analogTheme, 0, ATHEME_COUNT - 1);
        analogNeon = ReadBool("Analog", "Neon", analogNeon);
        analogFull = ReadBool("Analog", "Full", analogFull);
        analogMatch = ReadBool("Analog", "MatchBg", analogMatch);
        analogSize = ReadInt("Analog", "Size", analogSize, 60, 4000);
        analogNoSec = ReadBool("Analog", "HideSecond", analogNoSec);
        {                                       // booting_alert: TRUE / FALSE (없으면 FALSE 를 ini 에 써 넣음)
            char v[16];
            GetPrivateProfileStringA("Option", "booting_alert", "", v, sizeof(v), ini_);
            if (!v[0]) WritePrivateProfileStringA("Option", "booting_alert", "FALSE", ini_);
            bootingAlert = lstrcmpiA(v, "TRUE") == 0 || lstrcmpA(v, "1") == 0;
            GetPrivateProfileStringA("Option", "tray_start", "", v, sizeof(v), ini_);     // tray_start: TRUE 이면 트레이로 시작
            if (!v[0]) WritePrivateProfileStringA("Option", "tray_start", "FALSE", ini_);
            trayStart = lstrcmpiA(v, "TRUE") == 0 || lstrcmpiA(v, "ON") == 0 || lstrcmpA(v, "1") == 0;
            if (trayStart) visible = false;                 // 숨김 상태로 시작 -> 창 생성 때 트레이 아이콘만 표시
            GetPrivateProfileStringA("Option", "companion_run_as_invoker", "", v, sizeof(v), ini_);   // 동반 실행 앱 권한 요청 무시 (TRUE / FALSE)
            if (!v[0]) WritePrivateProfileStringA("Option", "companion_run_as_invoker", "FALSE", ini_);
            companionInvoker = lstrcmpiA(v, "TRUE") == 0 || lstrcmpA(v, "1") == 0;
            for (int i = 0; i < COMPANION_MAX; i++) {                                   // 동반 실행 앱 1~5 (없으면 빈 값으로 키를 만들어 둠)
                char k[24];
                wsprintfA(k, "companion_app%d", i + 1);
                GetPrivateProfileStringA("Option", k, "\x01", companionApp[i], sizeof(companionApp[i]), ini_);
                if (companionApp[i][0] == '\x01') {
                    companionApp[i][0] = 0;
                    if (i == 0) {                                                       // 예전 단일 키 companion_app 값은 1번으로 이어받음
                        GetPrivateProfileStringA("Option", "companion_app", "", companionApp[0], sizeof(companionApp[0]), ini_);
                        WritePrivateProfileStringA("Option", "companion_app", NULL, ini_);
                    }
                    WritePrivateProfileStringA("Option", k, companionApp[i], ini_);
                }
                Utf8::Normalize(companionApp[i], sizeof(companionApp[i]));
            }
        }
        {   // 음력 API 주소/키: ini 에서만 가져옴 (없는 키는 빈 항목을 만들어 위치를 알려 줌)
            static const char NONE[] = "\x01";
            GetPrivateProfileStringA("Lunar", "ApiUrl", NONE, lunarUrl, sizeof(lunarUrl), ini_);
            if (lunarUrl[0] == 1) {
                lstrcpyA(lunarUrl, "http://apis.data.go.kr/B090041/openapi/service/LrsrCldInfoService/getLunCalInfo");
                WritePrivateProfileStringA("Lunar", "ApiUrl", lunarUrl, ini_);
            }
            GetPrivateProfileStringA("Lunar", "ApiKey", NONE, lunarKey, sizeof(lunarKey), ini_);
            if (lunarKey[0] == 1) {
                lunarKey[0] = 0;
                WritePrivateProfileStringA("Lunar", "ApiKey", "", ini_);
            }
        }

        ReadStr("Global", "BaseCity", baseCity, sizeof(baseCity));
        Utf8::Normalize(baseCity, sizeof(baseCity));

        GetPrivateProfileStringA("Global", "Cities", "", cities, sizeof(cities), ini_);
        Utf8::Normalize(cities, sizeof(cities));

        for (int i = 0; i < ALARM_COUNT; i++) {  // 알람 (설정한 것만 ini 에 있음)
            Alarm& a = alarms[i];
            char k[24], t[16];
            wsprintfA(k, "Alarm%dOn", i + 1);
            a.on = GetPrivateProfileIntA("Alarm", k, 0, ini_) != 0;
            wsprintfA(k, "Alarm%dTime", i + 1);
            GetPrivateProfileStringA("Alarm", k, "00:00:00", t, sizeof(t), ini_);
            ParseTime(t, a.h, a.m, a.s);
            wsprintfA(k, "Alarm%dFile", i + 1);
            GetPrivateProfileStringA("Alarm", k, "", a.file, sizeof(a.file), ini_);
            Utf8::Normalize(a.file, sizeof(a.file));
            wsprintfA(k, "Alarm%dMemo", i + 1);
            GetPrivateProfileStringA("Alarm", k, "", a.memo, sizeof(a.memo), ini_);
            Utf8::Normalize(a.memo, sizeof(a.memo));
            wsprintfA(k, "Alarm%dClose", i + 1);
            a.closeMode = (int)GetPrivateProfileIntA("Alarm", k, 0, ini_);
            if (a.closeMode < 0 || a.closeMode > 2) a.closeMode = 0;
            wsprintfA(k, "Alarm%dCloseSec", i + 1);
            a.closeSec = (int)GetPrivateProfileIntA("Alarm", k, 60, ini_);
            if (a.closeSec < 0 || a.closeSec > 86399) a.closeSec = 60;
        }
        for (int i = ALARM_COUNT; i < ALARM_TOTAL; i++) {    // scan 예약: ini 에 시각이 없으면 사용 안 함 (scan 에 영향 없음)
            Alarm& a = alarms[i];
            const char* p = i == ALARM_SCAN_ON ? "ScanOn" : "ScanOff";
            char k[40], t[16];
            wsprintfA(k, "%sTime", p);
            GetPrivateProfileStringA("WiFiScan", k, "", t, sizeof(t), ini_);
            const bool has = t[0] != 0;
            if (has) ParseTime(t, a.h, a.m, a.s);
            wsprintfA(k, "%sUse", p);
            a.on = has && GetPrivateProfileIntA("WiFiScan", k, 1, ini_) != 0;
            wsprintfA(k, "%sFile", p);
            GetPrivateProfileStringA("WiFiScan", k, "", a.file, sizeof(a.file), ini_);
            Utf8::Normalize(a.file, sizeof(a.file));
            wsprintfA(k, "%sMemo", p);
            GetPrivateProfileStringA("WiFiScan", k, "", a.memo, sizeof(a.memo), ini_);
            Utf8::Normalize(a.memo, sizeof(a.memo));
            wsprintfA(k, "%sClose", p);
            a.closeMode = (int)GetPrivateProfileIntA("WiFiScan", k, 0, ini_);
            if (a.closeMode < 0 || a.closeMode > 2) a.closeMode = 0;
            wsprintfA(k, "%sCloseSec", p);
            a.closeSec = (int)GetPrivateProfileIntA("WiFiScan", k, 60, ini_);
            if (a.closeSec < 0 || a.closeSec > 86399) a.closeSec = 60;
        }
        {
            char t[16];
            pgOn = GetPrivateProfileIntA("Alarm", "ProgOn", 0, ini_) != 0;
            GetPrivateProfileStringA("Alarm", "ProgTime", "00:00:00", t, sizeof(t), ini_);
            ParseTime(t, pgH, pgM, pgS);
            GetPrivateProfileStringA("Alarm", "ProgFile", "", pgFile, sizeof(pgFile), ini_);
            Utf8::Normalize(pgFile, sizeof(pgFile));
            GetPrivateProfileStringA("Alarm", "ProgMemo", "", pgMemo, sizeof(pgMemo), ini_);
            Utf8::Normalize(pgMemo, sizeof(pgMemo));
            rbOn = GetPrivateProfileIntA("Alarm", "RebootOn", 0, ini_) != 0;
            GetPrivateProfileStringA("Alarm", "RebootTime", "00:00:00", t, sizeof(t), ini_);
            ParseTime(t, rbH, rbM, rbS);
        }

        for (int i = 0; i < 2; i++) {               // HomeBack / NotHome 프로그램 (없으면 사용 안 함)
            const char* p = i == 0 ? "HomeBackProg" : "NotHomeProg";
            char k[40];
            wsprintfA(k, "%sOn", p);
            homeProg[i].on = GetPrivateProfileIntA("WiFiScan", k, 0, ini_) != 0;
            wsprintfA(k, "%sFile", p);
            GetPrivateProfileStringA("WiFiScan", k, "", homeProg[i].file, sizeof(homeProg[i].file), ini_);
            Utf8::Normalize(homeProg[i].file, sizeof(homeProg[i].file));
            wsprintfA(k, "%sMemo", p);
            GetPrivateProfileStringA("WiFiScan", k, "", homeProg[i].memo, sizeof(homeProg[i].memo), ini_);
            Utf8::Normalize(homeProg[i].memo, sizeof(homeProg[i].memo));
        }
        ReadStr("IPCam", "Host", ipHost, sizeof(ipHost));                 // IP cam (없는 키는 기본값을 ini 에 써 넣음)
        ipPort = ReadSec("IPCam", "Port", ipPort, 1);
        if (ipPort > 65535) ipPort = 4747;
        ReadStr("PhoneFtp", "Host", phHost, sizeof(phHost));              // 폰 탐색기
        phPort = ReadSec("PhoneFtp", "Port", phPort, 1);
        if (phPort > 65535) phPort = 2121;
        ReadStr("PhoneFtp", "User", phUser, sizeof(phUser));
        scanBt   = ReadBool("WiFiScan", "ScanBluetooth", scanBt);        // 집 안팎 감시 (없는 키는 기본값을 ini 에 써 넣음)
        scanWifi = ReadBool("WiFiScan", "ScanWifi", scanWifi);
        ReadStr("WiFiScan", "HomeBluetooth", homeBt, sizeof(homeBt));
        ReadStr("WiFiScan", "HomeMac", homeMac, sizeof(homeMac));
        ReadStr("WiFiScan", "HomeIp", homeIp, sizeof(homeIp));
        scanSec    = ReadSec("WiFiScan", "WiFiScanWatchInterval", scanSec, 5);
        notHomeSec = ReadSec("WiFiScan", "NotHome", notHomeSec, 1);
        LoadRdp();
    }

    bool HasKey(const char* sec, const char* key) {
        char t[4];
        GetPrivateProfileStringA(sec, key, "\x01", t, sizeof(t), ini_);
        return t[0] != '\x01';
    }

    // 예전 rdp_server.ini / rdp_client.ini (exe 폴더) 의 값을 새 섹션으로 가져옴. 새 섹션에 첫 키가 이미 있으면 하지 않음 (원본 파일은 그대로 둠)
    void ImportRdpIni(const char* oldName, const char* oldSec, const char* newSec, const char* const* keys, int n) {
        if (HasKey(newSec, keys[0])) return;
        char path[MAX_PATH + 16];
        lstrcpynA(path, ini_, MAX_PATH);
        char* sl = strrchr(path, '\\');
        if (!sl || (sl - path) + 1 + lstrlenA(oldName) >= (int)sizeof(path)) return;
        lstrcpyA(sl + 1, oldName);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
        for (int i = 0; i < n; i++) {
            char v[400];
            GetPrivateProfileStringA(oldSec, keys[i], "", v, sizeof(v), path);
            if (v[0]) WritePrivateProfileStringA(newSec, keys[i], v, ini_);
        }
        Log::Put("%s 의 설정을 ini [%s] 로 가져옴 (원본은 그대로 둠)", oldName, newSec);       // 비밀번호는 기록하지 않음
    }

    // 다른 PC(서버 / 클라이언트). 비밀번호가 UTF-8 로 63바이트를 넘을 수 있어 ReadStr(64바이트) 대신 넉넉한 버퍼로 읽음
    void LoadRdp() {
        static const char* const SRV[] = {"port", "password", "allowed_ip", "accept"};
        static const char* const CLI[] = {"ip", "port", "password"};
        ImportRdpIni("rdp_server.ini", "server", "RdpServer", SRV, 4);
        ImportRdpIni("rdp_client.ini", "client", "RdpClient", CLI, 3);
        const bool hadSrv = HasKey("RdpServer", "autostart") && HasKey("RdpServer", "accept") && HasKey("RdpServer", "osk");
        const bool hadCli = HasKey("RdpClient", "ip") && HasKey("RdpClient", "password");
        auto S = [&](const char* sec, const char* key, char* buf, int cap) {      // 값이 있으면 buf 에 (ANSI 로 저장된 한글은 UTF-8 로 바꿈)
            char t[400];
            GetPrivateProfileStringA(sec, key, "", t, sizeof(t), ini_);
            Utf8::Normalize(t, sizeof(t));
            if (t[0]) lstrcpynA(buf, t, cap);
        };
        rsPort = (int)GetPrivateProfileIntA("RdpServer", "port", rsPort, ini_);
        if (rsPort < 1 || rsPort > 65535) rsPort = 7071;
        S("RdpServer", "password", rsPass, sizeof(rsPass));
        S("RdpServer", "allowed_ip", rsAllow, sizeof(rsAllow));
        char m[16];
        GetPrivateProfileStringA("RdpServer", "accept", "once", m, sizeof(m), ini_);
        rsAccept = !lstrcmpiA(m, "auto") ? 1 : !lstrcmpiA(m, "always") ? 2 : 0;
        rsAuto = GetPrivateProfileIntA("RdpServer", "autostart", 0, ini_) != 0;
        rsOsk = GetPrivateProfileIntA("RdpServer", "osk", 0, ini_) != 0;
        S("RdpClient", "ip", rcIp, sizeof(rcIp));
        rcPort = (int)GetPrivateProfileIntA("RdpClient", "port", rcPort, ini_);
        if (rcPort < 1 || rcPort > 65535) rcPort = 7071;
        S("RdpClient", "password", rcPass, sizeof(rcPass));
        if (!hadSrv) SaveRdpServer();                                             // 없는 키는 기본값을 ini 에 써 넣음
        if (!hadCli) SaveRdpClient();
    }

    // 초 단위 값: 0 이면 기본값, lo 보다 작으면 lo (WiFiScan.cpp 와 같은 규칙: 주기 0 -> 15, 최소 5 / 외출 0 -> 60)
    int ReadSec(const char* sec, const char* key, int def, int lo) {
        int v = (int)GetPrivateProfileIntA(sec, key, -1, ini_);
        if (v < 0) {
            WriteInt(sec, key, def);
            return def;
        }
        return v == 0 ? def : (v < lo ? lo : v);
    }

    // "HH:MM:SS" -> 시/분/초 (범위를 벗어나면 보정)
    static void ParseTime(const char* t, int& h, int& m, int& s) {
        char* e;
        long v[3] = {0, 0, 0};
        for (int i = 0; i < 3 && *t; i++) {
            v[i] = strtol(t, &e, 10);
            t = (*e == ':') ? e + 1 : e;
            if (e == t && i < 2 && *e != ':') break;
        }
        h = (int)(v[0] < 0 ? 0 : (v[0] > 23 ? 23 : v[0]));
        m = (int)(v[1] < 0 ? 0 : (v[1] > 59 ? 59 : v[1]));
        s = (int)(v[2] < 0 ? 0 : (v[2] > 59 ? 59 : v[2]));
    }

    int ReadInt(const char* sec, const char* key, int def, int lo, int hi) {
        int v = (int)GetPrivateProfileIntA(sec, key, -1, ini_);
        if (v < 0) {
            WriteInt(sec, key, def);
            return def;
        }
        return v < lo ? lo : (v > hi ? hi : v);
    }

    bool ReadBool(const char* sec, const char* key, bool def) {
        int v = (int)GetPrivateProfileIntA(sec, key, -1, ini_);
        if (v < 0) {
            WriteInt(sec, key, def);
            return def;
        }
        return v != 0;
    }

    // 문자열 (buf 에 기본값이 들어 있고, ini 에 값이 있으면 덮어씀)
    void ReadStr(const char* sec, const char* key, char* buf, int cap) {
        char tmp[64];
        GetPrivateProfileStringA(sec, key, "", tmp, sizeof(tmp), ini_);
        if (!tmp[0]) {
            WritePrivateProfileStringA(sec, key, buf, ini_);
            return;
        }
        lstrcpynA(buf, tmp, cap);
    }

    // 색상: RRGGBB 16진수 (예: 00FF78, #00FF78)
    COLORREF ReadColor(const char* sec, const char* key, COLORREF def) {
        char b[16];
        GetPrivateProfileStringA(sec, key, "", b, sizeof(b), ini_);
        if (!b[0]) {
            WriteColor(sec, key, def);
            return def;
        }
        unsigned long v = strtoul(b[0] == '#' ? b + 1 : b, NULL, 16);
        return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }

    void WriteColor(const char* sec, const char* key, COLORREF c) {
        char b[16];
        wsprintfA(b, "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        WritePrivateProfileStringA(sec, key, b, ini_);
    }

    char ini_[MAX_PATH] = {};
    int  lastX_ = 0x7FFFFFFF;               // 마지막으로 저장한 위치 (중복 쓰기 방지)
    int  lastY_ = 0x7FFFFFFF;
};

// ---------------------------------------------------------------- TimeDialog
// 메모리 상의 대화상자 템플릿 생성기 (WORD 단위 스트림, 문자열은 UTF-8 -> UTF-16 변환)
class DlgBuilder {
public:
    void W(int v)   { buf_[n_++] = (WORD)v; }
    void D(DWORD v) { W((int)(v & 0xFFFF)); W((int)(v >> 16)); }
    void Align()    { if (n_ & 1) W(0); }                   // 항목은 DWORD 경계에 맞춤
    void S(const char* utf8) { n_ += Utf8::ToUtf16(utf8, -1, (char16_t*)(buf_ + n_), CAP - n_); }

    // 자식 컨트롤: 클래스 이름이 문자열인 경우 / 버튼(예약 클래스 0x0080)인 경우
    void Control(DWORD style, int x, int y, int cx, int cy, int id, const char* cls, const char* title) {
        Align(); D(style); D(0); W(x); W(y); W(cx); W(cy); W(id); S(cls); S(title); W(0);
    }
    void Button(DWORD style, int x, int y, int cx, int cy, int id, const char* title) {
        Align(); D(style); D(0); W(x); W(y); W(cx); W(cy); W(id); W(0xFFFF); W(0x0080); S(title); W(0);
    }
    const void* Data() const { return buf_; }

private:
    enum { CAP = 2048 };
    alignas(4) WORD buf_[CAP];
    int n_ = 0;
};

// 알람 시각(시:분:초) 선택 대화상자 (시간 선택 컨트롤, 화살표 키/스핀으로 조절)
class TimeDialog {
public:
    // use != NULL 이면 '사용' 체크박스를 함께 보여 주고 값을 입출력
    static bool Show(HINSTANCE hi, HWND owner, const char* title, int& h, int& m, int& s, bool* use = NULL) {
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_DATE_CLASSES};
        InitCommonControlsEx(&ic);

        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(use ? 4 : 3); b.W(0); b.W(0); b.W(150); b.W(use ? 78 : 62);     // 항목 수, x, y, cx, cy (대화상자 단위)
        b.W(0); b.W(0);                                     // 메뉴 없음, 기본 클래스
        b.S(title);
        b.W(9); b.S("Segoe UI");                            // DS_SETFONT: 글꼴 크기, 이름
        b.Control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN,
                  15, 12, 120, 14, IDC_TIME, "SysDateTimePick32", "");
        if (use) b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 15, 34, 120, 12, IDC_USE, "사용");
        const int by = use ? 54 : 38;
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 25, by, 45, 14, IDOK, "확인");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,    80, by, 45, 14, IDCANCEL, "취소");

        int t[4] = {h, m, s, use && *use ? 1 : 0};
        INT_PTR ok = DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)t);
        if (ok != 1) return false;
        h = t[0];
        m = t[1];
        s = t[2];
        if (use) *use = t[3] != 0;
        return true;
    }

private:
    enum { IDC_TIME = 100, IDC_USE = 101 };

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            int* t = (int*)l;
            SYSTEMTIME st = {};
            GetLocalTime(&st);                              // 날짜 부분은 오늘 (시각만 사용)
            st.wHour = (WORD)t[0];
            st.wMinute = (WORD)t[1];
            st.wSecond = (WORD)t[2];
            SendDlgItemMessageW(d, IDC_TIME, DTM_SETFORMATW, 0, (LPARAM)u"HH:mm:ss");
            SendDlgItemMessageW(d, IDC_TIME, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
            CheckDlgButton(d, IDC_USE, t[3] ? BST_CHECKED : BST_UNCHECKED);      // 사용 체크박스가 없으면 아무 일도 안 함
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) {
                SYSTEMTIME st;
                SendDlgItemMessageW(d, IDC_TIME, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
                int* t = (int*)GetWindowLongPtrA(d, GWLP_USERDATA);
                t[0] = st.wHour;
                t[1] = st.wMinute;
                t[2] = st.wSecond;
                t[3] = IsDlgButtonChecked(d, IDC_USE) == BST_CHECKED ? 1 : 0;
                EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
};

// Yes/No 확인 대화상자 + 남은 대기 시간 프로그레스 바(초 단위). 시간이 다 되면 defYes 가 true 면 Yes, false 면 No 로 처리 (기본 버튼도 같음)
class ConfirmDialog {
public:
    // 반환: IDYES / IDNO (시간 초과 시 defYes ? IDYES : IDNO)
    static int Show(HINSTANCE hi, int waitSec, const char* caption, const char* msg, const char* note, bool defYes) {
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&ic);

        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | DS_SETFOREGROUND | WS_POPUP | WS_CAPTION);   // 닫기(X) 버튼 없음
        b.D(0);
        b.W(5); b.W(0); b.W(0); b.W(270); b.W(94);
        b.W(0); b.W(0);
        b.S(caption);
        b.W(9); b.S("Segoe UI");
        b.Control(WS_CHILD | WS_VISIBLE, 10, 8, 250, 24, IDC_MSG, "STATIC", msg);
        b.Control(WS_CHILD | WS_VISIBLE, 10, 38, 250, 10, IDC_LEFT, "STATIC", "");
        b.Control(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, 52, 250, 10, IDC_BAR, "msctls_progress32", "");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | (defYes ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 80, 72, 45, 14, IDYES, "Yes");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | (defYes ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON), 145, 72, 45, 14, IDNO, "No");

        Ctx c = {GetTickCount(), waitSec, note, defYes};
        return (int)DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), NULL, Proc, (LPARAM)&c);
    }

private:
    enum { IDC_MSG = 100, IDC_LEFT = 101, IDC_BAR = 102 };
    struct Ctx { DWORD start; int sec; const char* note; bool defYes; };

    static void Update(HWND d, Ctx* c) {
        int left = c->sec - (int)((GetTickCount() - c->start) / 1000);
        if (left < 0) left = 0;
        SendDlgItemMessageW(d, IDC_BAR, PBM_SETPOS, (WPARAM)left, 0);
        char t[128];
        char16_t u[128];
        wsprintfA(t, "남은 시간: %d초 (%s)", left, c->note);
        Utf8::ToUtf16(t, -1, u, 128);
        SetDlgItemTextW(d, IDC_LEFT, (LPCWSTR)u);
        if (left == 0) EndDialog(d, c->defYes ? IDYES : IDNO);   // 시간 초과 = 기본 응답
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            Ctx* c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SendDlgItemMessageW(d, IDC_BAR, PBM_SETRANGE32, 0, c->sec);
            Update(d, c);
            SetTimer(d, 1, 250, NULL);
            return TRUE;
        }
        case WM_TIMER:
            Update(d, (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA));
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(w) == IDYES || LOWORD(w) == IDNO) {
                KillTimer(d, 1);
                EndDialog(d, LOWORD(w));
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
};

// 안내 대화상자 (About / 홈페이지): 여러 줄 문구 + (선택) 링크 + 남은 시간 프로그레스 바. 시간이 다 되면 자동으로 닫힘
// 링크를 누르면 시스템 기본 브라우저로 연다 (SysLink 는 매니페스트가 필요해서 파란 밑줄 STATIC 으로 처리)
class InfoDialog {
public:
    static void Show(HINSTANCE hi, const char* caption, const char* msg, const char* link, int waitSec) {
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&ic);

        int lines = 0;                          // 줄 수 (긴 줄은 창 폭에서 줄바꿈되므로 글자 수로 추정해 더함)
        for (const char* p = msg; ; ) {
            int n = 0;
            while (*p && *p != '\n') { if (((unsigned char)*p & 0xC0) != 0x80) n++; p++; }
            lines += 1 + (n > 0 ? (n - 1) / 34 : 0);
            if (!*p) break;
            p++;
        }
        int y = 8, mh = lines * 10 + 2;

        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | DS_SETFOREGROUND | WS_POPUP | WS_CAPTION);
        b.D(0);
        b.W(link ? 5 : 4); b.W(0); b.W(0); b.W(300); b.W(mh + (link ? 12 : 0) + 60);   // 컨트롤 개수: 링크 없으면 4
        b.W(0); b.W(0);
        b.S(caption);
        b.W(9); b.S("Segoe UI");
        b.Control(WS_CHILD | WS_VISIBLE, 10, y, 280, mh, IDC_MSG, "STATIC", msg);
        y += mh;
        if (link) { b.Control(WS_CHILD | WS_VISIBLE | SS_NOTIFY, 10, y, 280, 10, IDC_LINK, "STATIC", link); y += 12; }
        b.Control(WS_CHILD | WS_VISIBLE, 10, y + 4, 280, 10, IDC_LEFT, "STATIC", "");
        b.Control(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, y + 18, 280, 10, IDC_BAR, "msctls_progress32", "");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 127, y + 34, 46, 14, IDOK, "OK");

        Ctx c = {GetTickCount(), waitSec, link, NULL};
        DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), NULL, Proc, (LPARAM)&c);
    }

private:
    enum { IDC_MSG = 100, IDC_LEFT = 101, IDC_BAR = 102, IDC_LINK = 103 };
    struct Ctx { DWORD start; int sec; const char* link; HFONT font; };

    static void Update(HWND d, Ctx* c) {
        int left = c->sec - (int)((GetTickCount() - c->start) / 1000);
        if (left < 0) left = 0;
        SendDlgItemMessageW(d, IDC_BAR, PBM_SETPOS, (WPARAM)left, 0);
        char t[64];
        char16_t u[64];
        wsprintfA(t, "남은 시간: %d초 (끝나면 자동으로 닫힘)", left);
        Utf8::ToUtf16(t, -1, u, 64);
        SetDlgItemTextW(d, IDC_LEFT, (LPCWSTR)u);
        if (left == 0) EndDialog(d, IDOK);
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
        switch (msg) {
        case WM_INITDIALOG: {
            c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SendDlgItemMessageW(d, IDC_BAR, PBM_SETRANGE32, 0, c->sec);
            HWND hl = GetDlgItem(d, IDC_LINK);
            if (hl) {                                            // 링크: 밑줄 글꼴
                LOGFONTW lf;
                HFONT f = (HFONT)SendMessageW(hl, WM_GETFONT, 0, 0);
                if (f && GetObjectW(f, sizeof(lf), &lf)) {
                    lf.lfUnderline = TRUE;
                    c->font = CreateFontIndirectW(&lf);
                    SendMessageW(hl, WM_SETFONT, (WPARAM)c->font, TRUE);
                }
            }
            Update(d, c);
            SetTimer(d, 1, 250, NULL);
            return TRUE;
        }
        case WM_TIMER:
            Update(d, c);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(d, IDC_LINK)) {
                SetTextColor((HDC)w, RGB(0, 0, 255));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_SETCURSOR:
            if ((HWND)w == GetDlgItem(d, IDC_LINK)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                SetWindowLongPtrA(d, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(w) == IDC_LINK && c && c->link) {         // 링크 클릭 -> 기본 브라우저
                char16_t u[300];
                Utf8::ToUtf16(c->link, -1, u, 300);
                HINSTANCE r = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)u, NULL, NULL, SW_SHOWNORMAL);
                Log::Put("브라우저로 열기: %s (결과코드 %d)", c->link, (int)(INT_PTR)r);
                return TRUE;
            }
            if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
                EndDialog(d, IDOK);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            KillTimer(d, 1);
            if (c && c->font) DeleteObject(c->font);
            break;
        }
        return FALSE;
    }
};

// 음력 API 키값 등록 대화상자: 설명문 + 발급 주소 링크(기본 브라우저로 열림) + 키 입력 칸
class LunarKeyDialog {
public:
    static bool Show(HINSTANCE hi, HWND owner, char* key, int cap) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(13); b.W(0); b.W(0); b.W(340); b.W(208);
        b.W(0); b.W(0);
        b.S("음력 API 키값 등록");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        b.Control(T, 10, 8,  320, 12, 110, "STATIC", "1)  음력 날짜를 구하려면 천문대 서버에 API로 접속하여야 합니다.");
        b.Control(T, 10, 22, 320, 22, 111, "STATIC", "2) 천문대 서버 API 키값은, [정부 공공 데이타 포탈]에서 무료로 발급합니다.");
        b.Control(T, 10, 46, 320, 22, 112, "STATIC", "3) [정부 공공 데이타 포탈] 에 회원 가입을 하고, 공공API 키값 발급 신청을 하면, 즉시 무료로 발급됩니다.");
        b.Control(T, 10, 70, 320, 12, 113, "STATIC", "4) [정부 공공 데이타 포탈] API 키 발급 주소 :");
        b.Control(T | SS_NOTIFY, 22, 82, 308, 12, IDC_LINK, "STATIC", URLS[0]);
        b.Control(T, 10, 98, 320, 12, 114, "STATIC", "5) 발급 받은 키 값을, 아래 칸에 붙여넣기 하세요");
        b.Control(T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 10, 112, 320, 14, IDC_EDIT, "EDIT", "");
        b.Control(T, 10, 132, 320, 12, 115, "STATIC", "6) 참고사항1 : 한국천문연구원_음양력 정보 api 해설");
        b.Control(T | SS_NOTIFY, 22, 144, 308, 12, IDC_LINK + 1, "STATIC", URLS[1]);
        b.Control(T, 10, 160, 320, 12, 116, "STATIC", "7) 참고사항2 : 한국천문연구원 음양력 변환, 일반용 홈페이지");
        b.Control(T | SS_NOTIFY, 22, 172, 308, 12, IDC_LINK + 2, "STATIC", URLS[2]);
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON, 100, 190, 45, 14, IDOK, "확인");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,    195, 190, 45, 14, IDCANCEL, "취소");
        Ctx c = {key, cap, NULL};
        bool ok = DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c) == 1;
        return ok;
    }

private:
    enum { IDC_EDIT = 100, IDC_LINK = 103 };
    static constexpr const char* URLS[3] = {
        "https://www.data.go.kr/iim/api/selectApiKeyList.do",
        "https://www.data.go.kr/data/15012679/openapi.do",
        "https://astro.kasi.re.kr/kor/life/pageView/8"};
    struct Ctx { char* s; int cap; HFONT font; };

    static int LinkIdx(HWND d, HWND h) {                         // 링크 컨트롤이면 0..2, 아니면 -1
        for (int i = 0; i < 3; i++) if (h && h == GetDlgItem(d, IDC_LINK + i)) return i;
        return -1;
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
        switch (msg) {
        case WM_INITDIALOG: {
            c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            char16_t u[320];
            u[0] = 0;
            Utf8::ToUtf16(c->s, -1, u, 320);
            SendDlgItemMessageW(d, IDC_EDIT, EM_LIMITTEXT, 190, 0);
            SetDlgItemTextW(d, IDC_EDIT, (LPCWSTR)u);
            LOGFONTW lf;                                         // 링크: 밑줄 글꼴 (3개가 공유)
            HFONT f = (HFONT)SendDlgItemMessageW(d, IDC_LINK, WM_GETFONT, 0, 0);
            if (f && GetObjectW(f, sizeof(lf), &lf)) {
                lf.lfUnderline = TRUE;
                c->font = CreateFontIndirectW(&lf);
                for (int i = 0; i < 3; i++) SendDlgItemMessageW(d, IDC_LINK + i, WM_SETFONT, (WPARAM)c->font, TRUE);
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            if (LinkIdx(d, (HWND)l) >= 0) {
                SetTextColor((HDC)w, RGB(0, 0, 255));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_SETCURSOR:
            if (LinkIdx(d, (HWND)w) >= 0) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                SetWindowLongPtrA(d, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(w) >= IDC_LINK && LOWORD(w) <= IDC_LINK + 2) {   // 링크 클릭 -> 기본 브라우저
                const char* url = URLS[LOWORD(w) - IDC_LINK];
                char16_t u[300];
                Utf8::ToUtf16(url, -1, u, 300);
                HINSTANCE r = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)u, NULL, NULL, SW_SHOWNORMAL);
                Log::Put("브라우저로 열기: %s (결과코드 %d)", url, (int)(INT_PTR)r);
                return TRUE;
            }
            if (LOWORD(w) == IDOK) {
                char16_t u[200];
                GetDlgItemTextW(d, IDC_EDIT, (LPWSTR)u, 200);
                char k[400];
                WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, k, sizeof(k), NULL, NULL);
                char* p = k;                                      // 앞뒤 공백 제거
                while (*p == ' ' || *p == '\t') p++;
                int n = lstrlenA(p);
                while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = 0;
                lstrcpynA(c->s, p, c->cap);
                EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (c && c->font) DeleteObject(c->font);
            break;
        }
        return FALSE;
    }
};

// 알람 메모 입력 대화상자 (한 줄, 최대 100자)
class MemoDialog {
public:
    static bool Show(HINSTANCE hi, HWND owner, char* utf8, int cap) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(3); b.W(0); b.W(0); b.W(220); b.W(56);
        b.W(0); b.W(0);
        b.S("알람 메모 (알람 시각에 30초 동안 표시)");
        b.W(9); b.S("Segoe UI");
        b.Control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 10, 8, 200, 14, IDC_EDIT, "EDIT", "");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 60, 32, 45, 14, IDOK, "확인");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,   115, 32, 45, 14, IDCANCEL, "취소");
        Ctx c = {utf8, cap};
        return DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c) == 1;
    }

private:
    enum { IDC_EDIT = 100 };
    struct Ctx { char* s; int cap; };

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            Ctx* c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            char16_t u[320];
            u[0] = 0;
            Utf8::ToUtf16(c->s, -1, u, 320);
            SendDlgItemMessageW(d, IDC_EDIT, EM_LIMITTEXT, 100, 0);
            SetDlgItemTextW(d, IDC_EDIT, (LPCWSTR)u);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) {
                Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
                char16_t u[128];
                GetDlgItemTextW(d, IDC_EDIT, (LPWSTR)u, 128);
                WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, c->s, c->cap, NULL, NULL);
                EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
};

// 알람 메모 표시 창: 모니터 중앙, 항상 위, 지정한 초 후 자동으로 닫힘 (클릭하면 즉시 닫힘). 창마다 독립적으로 동작
class MemoWindow {
public:
    static void Show(HINSTANCE hi, const char* utf8, int sec) {
        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = Proc;
            wc.hInstance = hi;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = (LPCWSTR)u"gsHomeManagerMemo";
            RegisterClassW(&wc);
            reg = true;
        }
        Data* d = (Data*)malloc(sizeof(Data));
        if (!d) return;
        Log::Put("메모 표시 (%d초): %s", sec, utf8);
        d->text[0] = 0;
        Utf8::ToUtf16(utf8, -1, d->text, 320);
        d->start = GetTickCount();
        d->sec = sec;
        const int W = 640, H = 220, off = live_ * 30;       // 동시에 여러 개면 조금씩 어긋나게
        int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2 + off;
        int y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2 + off;
        HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, (LPCWSTR)u"gsHomeManagerMemo", (LPCWSTR)u"",
                                 WS_POPUP | WS_BORDER, x, y, W, H, NULL, NULL, hi, d);
        if (!h) { free(d); return; }
        live_++;
        ShowWindow(h, SW_SHOWNOACTIVATE);
        SetTimer(h, 1, 1000, NULL);
    }

private:
    struct Data { char16_t text[320]; DWORD start; int sec; };
    static inline int live_ = 0;

    static void Paint(HWND h, HDC dc) {
        Data* d = (Data*)GetWindowLongPtrW(h, GWLP_USERDATA);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);

        HFONT big = CreateFontW(-34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, big);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT t = {20, 16, rc.right - 20, rc.bottom - 44};
        RECT c = t;
        DrawTextW(dc, (LPCWSTR)d->text, -1, &c, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        int th = c.bottom - c.top;
        t.top += ((t.bottom - t.top) - th) / 2;             // 세로 가운데 맞춤
        if (t.top < 16) t.top = 16;
        DrawTextW(dc, (LPCWSTR)d->text, -1, &t, DT_CENTER | DT_WORDBREAK);
        SelectObject(dc, old);
        DeleteObject(big);

        int left = d->sec - (int)((GetTickCount() - d->start) / 1000);
        if (left < 0) left = 0;
        char s[48];
        char16_t u[48];
        wsprintfA(s, "%d초 후 자동으로 닫힙니다 (클릭하면 닫기)", left);
        Utf8::ToUtf16(s, -1, u, 48);
        HFONT sm = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        old = SelectObject(dc, sm);
        SetTextColor(dc, RGB(160, 160, 160));
        RECT b = {0, rc.bottom - 36, rc.right, rc.bottom - 8};
        DrawTextW(dc, (LPCWSTR)u, -1, &b, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, old);
        DeleteObject(sm);
    }

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
            return 0;
        case WM_TIMER: {
            Data* d = (Data*)GetWindowLongPtrW(h, GWLP_USERDATA);
            if ((int)((GetTickCount() - d->start) / 1000) >= d->sec) DestroyWindow(h);
            else InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            Paint(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            DestroyWindow(h);
            return 0;
        case WM_NCDESTROY:
            free((void*)GetWindowLongPtrW(h, GWLP_USERDATA));
            live_--;
            break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
};

// ---------------------------------------------------------------- DesktopColor
// 모니터 바탕화면이 단색인지 확인하고 그 색을 읽음 (IDesktopWallpaper, 필요할 때만 COM 사용)
// IDesktopWallpaper 의 앞부분 (MinGW 헤더에 정의가 없을 수 있어 직접 선언. 메서드 순서가 vtable 순서)
struct IDeskWall : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetWallpaper(LPCWSTR monitorId, LPCWSTR wallpaper) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaper(LPCWSTR monitorId, LPWSTR* wallpaper) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitorDevicePathAt(UINT index, LPWSTR* path) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitorDevicePathCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMonitorRECT(LPCWSTR monitorId, RECT* rect) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(COLORREF color) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(COLORREF* color) = 0;
};

class DesktopColor {
public:
    // 단색(배경 그림 없음)이면 true + color. 그림/슬라이드쇼면 false
    static bool GetSolid(COLORREF& color) {
        static const GUID CLSID_DW = {0xC2CF3110, 0x460E, 0x4FC1, {0xB9, 0xD0, 0x8A, 0x1C, 0x0C, 0x9C, 0xC4, 0xBD}};
        static const GUID IID_DW   = {0xB92B56A9, 0x8B55, 0x4E14, {0x9A, 0x89, 0x01, 0x99, 0xBB, 0xB6, 0xF9, 0x3B}};
        bool solid = false;
        HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IDeskWall* dw = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_DW, NULL, CLSCTX_ALL, IID_DW, (void**)&dw)) && dw) {
            LPWSTR path = NULL;
            if (SUCCEEDED(dw->GetWallpaper(NULL, &path)) && path) {   // NULL = 모든 모니터 공통
                solid = (path[0] == 0);                                // 빈 경로 = 단색
                CoTaskMemFree(path);
            }
            COLORREF c = 0;
            if (solid && SUCCEEDED(dw->GetBackgroundColor(&c))) color = c;
            else solid = false;
            dw->Release();
        }
        if (SUCCEEDED(co)) CoUninitialize();
        return solid;
    }
};

// ---------------------------------------------------------------- Lunar
// 음력 조회: 천문연구원 공공API(getLunCalInfo, lunar.cpp 와 같은 방식).
// 네트워크는 작업 스레드에서 처리(시계가 멈추지 않음). 결과는 창 메시지로 UI 스레드에 전달되어 캐시에 저장.
// WinINet 은 필요할 때 동적으로 불러와서(LoadLibrary) 링크 옵션을 늘리지 않음.
class Lunar {
public:
    enum { WM_DONE = WM_APP + 2 };

    // API 주소/인증키는 ini 에서 받아 옴 (둘 중 하나라도 없으면 조회하지 않음)
    static void Init(const char* url, const char* key, HWND notify) {
        notify_ = notify;
        lstrcpynA(url_, url, sizeof(url_));
        lstrcpynA(key_, key, sizeof(key_));
    }

    // 키가 바뀌면 이전 조회 결과(특히 실패)를 버리고 다시 조회
    static void Reset() {
        for (int i = 0; i < CACHE; i++) { cache_[i].ymd = 0; cache_[i].state = 0; }
        next_ = 0;
    }

    // ymd(YYYYMMDD) 의 표시 문자열: 정상이면 "음력 YYYY/MM/DD (평달|윤달)", 조회 중이면 안내, 실패면 서버 오류 문구
    static void Text(int ymd, char* out, int cap) {
        Entry* e = Find(ymd);
        if (e && e->state == 2 && GetTickCount() - e->tick > 60000 && !pending_) {   // 실패는 1분 뒤 다시 시도
            e->ymd = 0;
            e = NULL;
        }
        if (e) {
            lstrcpynA(out, e->text, cap);
            return;
        }
        if (!pending_) Start(ymd);
        lstrcpynA(out, "음력 조회 중...", cap);
    }

    // 작업 스레드가 보낸 결과를 캐시에 저장 (UI 스레드)
    static void OnDone(LPARAM l) {
        Res* r = (Res*)l;
        pending_ = 0;
        Entry& e = cache_[next_++ % CACHE];
        e.ymd = r->ymd;
        e.state = r->ok ? 1 : 2;
        e.tick = GetTickCount();
        lstrcpynA(e.text, r->text, sizeof(e.text));
        Log::Put("음력 조회 %d: %s", r->ymd, r->text);
        free(r);
    }

private:
    enum { CACHE = 6 };
    struct Entry { int ymd; int state; DWORD tick; char text[96]; };       // 정적 저장소라 0 으로 시작
    struct Req { HWND hwnd; int ymd; char url[300]; char key[200]; };
    struct Res { int ymd; int ok; char text[96]; };

    static inline Entry cache_[CACHE];
    static inline int   next_ = 0;
    static inline int   pending_ = 0;
    static inline HWND  notify_ = NULL;
    static inline char  url_[300] = "";
    static inline char  key_[200] = "";

    static const char* ErrText() { return "천문 연구원 서버 오류"; }

    static Entry* Find(int ymd) {
        for (int i = 0; i < CACHE; i++) if (cache_[i].ymd == ymd) return &cache_[i];
        return NULL;
    }

    static void Start(int ymd) {
        if (!notify_ || !url_[0] || !key_[0]) return;                   // 결과를 받을 창이 아직 없음 (창 생성 직후 다시 시도됨)
        Req* q = (Req*)malloc(sizeof(Req));
        if (!q) return;
        q->hwnd = notify_;
        q->ymd = ymd;
        lstrcpynA(q->url, url_, sizeof(q->url));
        lstrcpynA(q->key, key_, sizeof(q->key));
        HANDLE h = CreateThread(NULL, 0, Worker, q, 0, NULL);
        if (!h) { free(q); return; }
        CloseHandle(h);
        pending_ = ymd;
    }

    // <tag>값</tag> 추출 (값이 없으면 빈 문자열)
    static bool Tag(const char* x, const char* t, char* out, int cap) {
        char open[40], close[40];
        wsprintfA(open, "<%s>", t);
        wsprintfA(close, "</%s>", t);
        const char* a = strstr(x, open);
        out[0] = 0;
        if (!a) return false;
        a += lstrlenA(open);
        const char* b = strstr(a, close);
        if (!b) return false;
        int n = (int)(b - a);
        if (n >= cap) n = cap - 1;
        memcpy(out, a, n);
        out[n] = 0;
        return true;
    }

    // HTTP GET (WinINet 동적 로딩). 성공하면 응답 본문을 body 에 채움
    static bool HttpGet(const char* url, char* body, int cap) {
        typedef HINTERNET (WINAPI *FnOpen)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
        typedef HINTERNET (WINAPI *FnOpenUrl)(HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
        typedef BOOL (WINAPI *FnRead)(HINTERNET, LPVOID, DWORD, LPDWORD);
        typedef BOOL (WINAPI *FnClose)(HINTERNET);
        typedef BOOL (WINAPI *FnSetOpt)(HINTERNET, DWORD, LPVOID, DWORD);
        const DWORD kOpenPreconfig = 0, kOptConnectTimeout = 2, kOptReceiveTimeout = 6;          // INTERNET_OPEN_TYPE_PRECONFIG, INTERNET_OPTION_CONNECT_TIMEOUT / RECEIVE_TIMEOUT
        const DWORD kFlagReload = 0x80000000, kFlagNoCacheWrite = 0x04000000;                    // INTERNET_FLAG_RELOAD, INTERNET_FLAG_NO_CACHE_WRITE (wininet.h 를 include 하지 않으므로 직접 정의)
        HMODULE m = LoadLibraryA("wininet.dll");
        if (!m) return false;
        FnOpen     fOpen    = (FnOpen)GetProcAddress(m, "InternetOpenA");
        FnOpenUrl  fOpenUrl = (FnOpenUrl)GetProcAddress(m, "InternetOpenUrlA");
        FnRead     fRead    = (FnRead)GetProcAddress(m, "InternetReadFile");
        FnClose    fClose   = (FnClose)GetProcAddress(m, "InternetCloseHandle");
        FnSetOpt   fSetOpt  = (FnSetOpt)GetProcAddress(m, "InternetSetOptionA");
        bool ok = false;
        if (fOpen && fOpenUrl && fRead && fClose) {
            HINTERNET net = fOpen("gsHomeManager/1.0", kOpenPreconfig, NULL, NULL, 0);
            if (net) {
                DWORD to = 8000;                                  // 접속/수신 제한 시간 8초
                if (fSetOpt) {
                    fSetOpt(net, kOptConnectTimeout, &to, sizeof(to));
                    fSetOpt(net, kOptReceiveTimeout, &to, sizeof(to));
                }
                HINTERNET u = fOpenUrl(net, url, NULL, 0, kFlagReload | kFlagNoCacheWrite, 0);
                if (u) {
                    int len = 0;
                    DWORD n = 0;
                    while (len < cap - 1 && fRead(u, body + len, (DWORD)(cap - 1 - len), &n) && n > 0) len += (int)n;
                    body[len] = 0;
                    ok = len > 0;
                    fClose(u);
                }
                fClose(net);
            }
        }
        FreeLibrary(m);
        return ok;
    }

    static DWORD WINAPI Worker(LPVOID p) {
        Req* q = (Req*)p;
        Res* r = (Res*)malloc(sizeof(Res));
        if (!r) { free(q); return 0; }
        r->ymd = q->ymd;
        r->ok = 0;
        lstrcpyA(r->text, ErrText());

        int y = q->ymd / 10000;
        if (y >= 1392 && y <= 2050) {                              // API 계산 한도
            char url[700];
            wsprintfA(url, "%s?serviceKey=%s&solYear=%d&solMonth=%02d&solDay=%02d",
                      q->url, q->key, y, q->ymd / 100 % 100, q->ymd % 100);
            char* body = (char*)malloc(16384);
            if (body) {
                char code[16], ly[16], lm[8], ld[8], leap[16];
                if (HttpGet(url, body, 16384) && Tag(body, "resultCode", code, sizeof(code)) && lstrcmpA(code, "00") == 0 &&
                    Tag(body, "lunYear", ly, sizeof(ly)) && Tag(body, "lunMonth", lm, sizeof(lm)) &&
                    Tag(body, "lunDay", ld, sizeof(ld)) && ly[0] && lm[0] && ld[0]) {
                    Tag(body, "lunLeapmonth", leap, sizeof(leap));
                    wsprintfA(r->text, "음력 %s/%02d/%02d (%s달)", ly, atoi(lm), atoi(ld), leap[0] ? leap : "평");
                    r->ok = 1;
                }
                free(body);
            }
        }
        if (!PostMessageA(q->hwnd, WM_DONE, 0, (LPARAM)r)) free(r);
        free(q);
        return 0;
    }
};

// ================================================================ WiFiScan
// WiFiScan.cpp (블루투스/Wi-Fi 스캔 + 집 안팎 감시)를 합친 부분.
//  - 만드는/읽는 파일은 WiFiScan.cpp 와 같다: WiFiScan_result.txt, WiFiScan_watch.log, oui.txt (exe 와 같은 폴더).
//  - /HomeBack, /NotHome 은 trigger.txt 파일 대신 같은 프로세스의 Telegram 모듈(namespace tg)에 직접 넘긴다 (TriggerFile -> tg::PostTrigger).
//  - ini 키(HomeBluetooth, HomeMac, HomeIp, WiFiScanWatchInterval, NotHome)는 screen_telegram.ini 가 아니라
//    gsHomeManager.ini 의 [WiFiScan] 에서 읽는다.
//  - scan 은 작업 스레드(ScanEngine)에서 돌고, 진행 상황(ScanView)을 WiFiScan 대화상자(ScanDialog)가 읽어서 보여 준다.
// ======================================================================
// 문자열 / 주소 도구
// ======================================================================
class StrUtil {
public:
    static std::string u8(const std::wstring& w) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), 0, 0, 0, 0);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, 0, 0);
        return s;
    }
    static std::string fromOem(const char* p, int len) {
        int n = MultiByteToWideChar(CP_OEMCP, 0, p, len, 0, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_OEMCP, 0, p, len, &w[0], n);
        return u8(w);
    }
    static std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    }
    static std::vector<std::string> split(const std::string& s, char c) {
        std::vector<std::string> v; std::string t; std::stringstream ss(s);
        while (std::getline(ss, t, c)) v.push_back(t);
        return v;
    }
    static std::string ipStr(uint32_t ip) {
        char b[32];
        snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        return b;
    }
    static bool parseIp(const std::string& s, uint32_t& ip) {
        in_addr a;
        if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
        ip = ntohl(a.s_addr);
        return true;
    }
    static std::string normMac(const std::string& s) {      // xx:xx.. / xx-xx.. -> XX-XX-XX-XX-XX-XX
        std::string h;
        for (char c : s) if (isxdigit((unsigned char)c)) h += (char)toupper((unsigned char)c);
        if (h.size() != 12) return "";
        std::string o;
        for (int i = 0; i < 12; i += 2) { if (i) o += '-'; o += h.substr(i, 2); }
        return o;
    }
    static std::string macStr(const unsigned char* p) {
        char t[32];
        snprintf(t, sizeof t, "%02X-%02X-%02X-%02X-%02X-%02X", p[0], p[1], p[2], p[3], p[4], p[5]);
        return t;
    }
    static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
};

// ======================================================================
// exe 폴더 / 각종 파일 경로 (WiFiScan 이 만들고 읽는 파일 - 이름/위치는 WiFiScan.cpp 와 같음)
// ======================================================================
class AppDir {
public:
    static const std::string& dir() {                        // exe 가 있는 폴더 (끝에 \ 포함)
        static const std::string d = [] {
            char exe[MAX_PATH] = {0}; GetModuleFileNameA(0, exe, MAX_PATH);
            std::string s = exe; s.resize(s.find_last_of("\\/") + 1);
            return s;
        }();
        return d;
    }
    static std::string logFile()    { return dir() + "WiFiScan_watch.log"; }
    static std::string resultFile() { return dir() + "WiFiScan_result.txt"; }
    static std::string ouiFile()    { return dir() + "oui.txt"; }
};

// ======================================================================
// 파일 로그 WiFiScan_watch.log (WiFiScan 동작 기록. 앱 전체 동작 로그는 위의 Log 클래스가 gsHomeManager.log 에 남김)
// ======================================================================
class Logger {
public:
    static void log(const std::string& msg) {
        std::lock_guard<std::mutex> g(mu());                 // localtime 은 스레드 안전하지 않으므로 잠근 뒤에 호출
        time_t t = time(0); char ts[32];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
        std::ofstream f(AppDir::logFile(), std::ios::app | std::ios::binary);
        f << ts << " " << msg << "\r\n";
    }
private:
    static std::mutex& mu() { static std::mutex m; return m; }
};

// ======================================================================
// 작업 취소 확인: ScanEngine 이 세대 번호를 바꾸면(scan Off / 재시작 / 앱 종료) 이전 세대의 작업은 취소된 것으로 본다
// ======================================================================
struct Cancel {
    const std::atomic<int>* gen;
    int my;
    bool operator()() const { return gen->load() != my; }
};
// ======================================================================
// 장치 이름 조회: DNS -> NetBIOS(UDP 137) -> mDNS(UDP 5353)
// ======================================================================
class NameResolver {
public:
    static std::string hostName(uint32_t ip) {
        sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(ip);
        wchar_t h[NI_MAXHOST];
        if (GetNameInfoW((sockaddr*)&sa, sizeof sa, h, NI_MAXHOST, 0, 0, NI_NAMEREQD) == 0) {
            std::wstring w = h;
            size_t d = w.find(L'.');
            if (d != std::wstring::npos) w.resize(d);
            if (!w.empty()) return StrUtil::u8(w);
        }
        std::string n = netbiosName(ip);
        return n.empty() ? mdnsName(ip) : n;
    }
private:
    // NetBIOS 노드 상태 조회 (UDP 137)
    static std::string netbiosName(uint32_t ip) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return "";
        DWORD to = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        unsigned char q[50] = {0};
        q[0] = 0x13; q[1] = 0x37; q[5] = 1; q[12] = 0x20; q[13] = 'C'; q[14] = 'K';
        for (int i = 15; i < 45; i++) q[i] = 'A';
        q[47] = 0x21; q[49] = 1;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(137); a.sin_addr.s_addr = htonl(ip);
        std::string out;
        if (sendto(s, (char*)q, sizeof q, 0, (sockaddr*)&a, sizeof a) == sizeof q) {
            unsigned char b[600];
            int r = recvfrom(s, (char*)b, sizeof b, 0, 0, 0);
            if (r > 57) {
                int p = 12;
                while (p < r && b[p]) {
                    if ((b[p] & 0xC0) == 0xC0) { p += 1; break; }
                    p += b[p] + 1;
                }
                p += 1;                 // 이름 종료
                p += 2 + 2 + 4 + 2;     // type, class, ttl, rdlength
                if (p < r) {
                    int cnt = b[p++];
                    for (int i = 0; i < cnt && p + 18 <= r; i++, p += 18) {
                        int flags = (b[p + 16] << 8) | b[p + 17];
                        if (b[p + 15] == 0 && !(flags & 0x8000)) {
                            int len = 15;
                            while (len > 0 && b[p + len - 1] == ' ') len--;
                            out = StrUtil::fromOem((char*)b + p, len);
                            break;
                        }
                    }
                }
            }
        }
        closesocket(s);
        return out;
    }
    // DNS 패킷의 이름 디코딩 (압축 포인터 지원). p 는 이름 다음 위치로 갱신
    static std::string dnsName(const unsigned char* b, int len, int& p) {
        std::string s; int pos = p, jumps = 0; bool jumped = false;
        while (pos < len) {
            int c = b[pos];
            if (c == 0) { pos++; break; }
            if ((c & 0xC0) == 0xC0) {
                if (pos + 1 >= len || ++jumps > 10) break;
                if (!jumped) p = pos + 2;
                jumped = true;
                pos = ((c & 0x3F) << 8) | b[pos + 1];
                continue;
            }
            if (pos + 1 + c > len) break;
            if (!s.empty()) s += '.';
            s.append((const char*)b + pos + 1, c);
            pos += 1 + c;
        }
        if (!jumped) p = pos;
        return s;
    }
    // mDNS 역방향 조회 (UDP 5353 유니캐스트, PTR)
    static std::string mdnsName(uint32_t ip) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return "";
        DWORD to = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        unsigned char q[128] = {0}; int n = 12; q[5] = 1;
        for (int sh = 0; sh <= 24; sh += 8) {
            std::string o = std::to_string((ip >> sh) & 255);
            q[n++] = (unsigned char)o.size(); memcpy(q + n, o.data(), o.size()); n += (int)o.size();
        }
        for (const char* t : {"in-addr", "arpa"}) {
            size_t l = strlen(t); q[n++] = (unsigned char)l; memcpy(q + n, t, l); n += (int)l;
        }
        q[n++] = 0; q[n++] = 0; q[n++] = 12; q[n++] = 0; q[n++] = 1;   // PTR, IN
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(5353); a.sin_addr.s_addr = htonl(ip);
        std::string out;
        if (sendto(s, (char*)q, n, 0, (sockaddr*)&a, sizeof a) == n) {
            unsigned char b[512];
            int r = recvfrom(s, (char*)b, sizeof b, 0, 0, 0);
            if (r > 12) {
                int qd = (b[4] << 8) | b[5], an = (b[6] << 8) | b[7], p = 12;
                for (int i = 0; i < qd && p < r; i++) { dnsName(b, r, p); p += 4; }
                for (int i = 0; i < an && p + 10 <= r; i++) {
                    dnsName(b, r, p);
                    int type = (b[p] << 8) | b[p + 1], rdlen = (b[p + 8] << 8) | b[p + 9];
                    p += 10;
                    if (type == 12) {
                        int pp = p;
                        out = dnsName(b, r, pp);
                        size_t d = out.find('.');
                        if (d != std::string::npos) out.resize(d);
                        break;
                    }
                    p += rdlen;
                }
            }
        }
        closesocket(s);
        return out;
    }
};

// ======================================================================
// 제조사 DB (oui.txt)
// ======================================================================
class OuiDb {
public:
    explicit OuiDb(const std::string& path) {
        std::ifstream f(path);
        std::string l;
        while (std::getline(f, l)) {
            size_t p = l.find("(hex)");
            if (p == std::string::npos || p < 8) continue;
            std::string k = StrUtil::upper(l.substr(0, 8)), v = l.substr(p + 5);
            size_t b = v.find_first_not_of(" \t"), e = v.find_last_not_of(" \t\r\n");
            if (b != std::string::npos) map_[k] = v.substr(b, e - b + 1);
        }
    }
    bool empty() const { return map_.empty(); }
    std::string vendorOf(const std::string& mac) const {     // 랜덤(로컬 관리) MAC 은 제조사 없음
        unsigned first = 0;
        if (mac.size() < 8 || sscanf(mac.c_str(), "%2x", &first) != 1 || (first & 2)) return "";
        auto it = map_.find(mac.substr(0, 8));
        return it == map_.end() ? "" : it->second;
    }
private:
    std::map<std::string, std::string> map_;
};

// ======================================================================
// 서브넷 감지
// ======================================================================
class Subnet {
public:
    uint32_t net = 0, bc = 0;
    bool valid() const { return net != 0; }
    std::string range() const { return StrUtil::ipStr(net + 1) + " ~ " + StrUtil::ipStr(bc - 1); }
    uint32_t localIp = 0;                                     // 내 PC 의 로컬 IP (범위 결정의 기준)
    // 1) 내 PC 의 로컬 IP 확인 -> 2) 로컬 IP 기준으로 scan 범위 결정.
    // 공유기가 211.x.x.x 같은 사설이 아닌 주소 대역을 쓰기도 하므로 192.168/172.16~31/10 으로 고정하지 않고,
    // 모든 어댑터의 IP 중 게이트웨이가 있고 사설 주소인 것을 우선 고른다. 범위는 그 IP 와 서브넷 마스크로 정한다
    // (마스크가 너무 넓거나 좁으면 로컬 IP 가 속한 /24).
    bool detect() {
        ULONG sz = 0; GetAdaptersInfo(0, &sz);
        std::vector<char> buf(sz ? sz : 1);
        auto* ai = (IP_ADAPTER_INFO*)buf.data();
        uint32_t myIp = 0, mask = 0;
        int best = -1;
        if (sz && GetAdaptersInfo(ai, &sz) == NO_ERROR) {
            for (auto* a = ai; a; a = a->Next) {
                const bool gw = strcmp(a->GatewayList.IpAddress.String, "0.0.0.0") != 0 && a->GatewayList.IpAddress.String[0];
                const bool nic = a->Type == MIB_IF_TYPE_ETHERNET || a->Type == IF_TYPE_IEEE80211;
                for (IP_ADDR_STRING* p = &a->IpAddressList; p; p = p->Next) {         // 어댑터 1개에 IP 가 여러 개일 수 있음
                    uint32_t ip, mk;
                    if (!StrUtil::parseIp(p->IpAddress.String, ip) || !StrUtil::parseIp(p->IpMask.String, mk) || !ip || !mk) continue;
                    if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE) continue;           // 127.x 루프백, 169.254.x.x 자동 할당 제외
                    const bool priv = (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8;
                    const int score = (gw ? 4 : 0) + (priv ? 2 : 0) + (nic ? 1 : 0);
                    if (score > best) { best = score; myIp = ip; mask = mk; }
                }
            }
        }
        if (!myIp) return false;
        localIp = myIp;
        net = myIp & mask; bc = net | ~mask;
        if (bc - net > 1022 || bc - net < 3) { net = myIp & 0xFFFFFF00u; bc = net | 255; }
        Logger::log("로컬 IP " + StrUtil::ipStr(myIp) + " -> scan 범위 " + range());
        return true;
    }
};

// ======================================================================
// ARP 확인
// ======================================================================
class Arp {
public:
    static std::string macOf(uint32_t ip) {
        ULONG m[2] = {0}, len = 6;
        if (SendARP(htonl(ip), 0, m, &len) != NO_ERROR || len != 6) return "";
        return StrUtil::macStr((unsigned char*)m);
    }
    // 서브넷 전체에서 mac 을 가진 IP 를 찾는다 (없으면 0). 동시 ARP 가 너무 많으면 느린 기기의 응답을 놓치므로 16 스레드.
    static uint32_t find(const Subnet& sn, const std::string& mac, const Cancel& cancel) {
        std::atomic<uint32_t> next(sn.net + 1), hit(0);
        const uint32_t bc = sn.bc;
        auto w = [&]() {
            for (;;) {
                uint32_t ip = next++;
                if (ip >= bc || hit || cancel()) break;
                if (macOf(ip) == mac) hit = ip;
            }
        };
        std::vector<std::thread> th;
        for (int i = 0; i < 16; i++) th.emplace_back(w);
        for (auto& t : th) t.join();
        return hit;
    }
    // 한 IP 를 직접 확인: ARP 3회 재시도, 안 되면 ping 으로 깨운 뒤 한 번 더 ARP
    static bool check(uint32_t ip, const std::string& mac) {
        for (int t = 0; t < 3; t++) if (macOf(ip) == mac) return true;
        HANDLE ic = IcmpCreateFile();
        if (ic != INVALID_HANDLE_VALUE) {
            char data[4] = {'p', 'i', 'n', 'g'}, reply[sizeof(ICMP_ECHO_REPLY) + 64];
            IcmpSendEcho(ic, htonl(ip), data, 4, 0, reply, sizeof reply, 1500);
            IcmpCloseHandle(ic);
        }
        return macOf(ip) == mac;
    }
};

// ======================================================================
// 이전 결과 / 결과 파일
// ======================================================================
struct BtDev { ULONGLONG addr = 0; std::string name, user; bool on = false, paired = false; };
struct Host { uint32_t ip; bool on; std::string name, mac, vendor, user; };

class PrevResult {
public:
    std::vector<std::vector<std::string>> wifi, bt;           // 이전 결과 파일의 ON/OFF 줄 (탭 분리)
    static PrevResult load(const std::string& path) {
        PrevResult p;
        std::ifstream f(path, std::ios::binary);
        std::string l; bool first = true, bt = false;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (first) { first = false; if (l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3); }
            if (l == "[Bluetooth]") { bt = true; continue; }
            if (l == "[Wi-Fi]") { bt = false; continue; }      // 구역 표시가 없는 예전 파일은 Wi-Fi 로 본다
            auto c = StrUtil::split(l, '\t');
            if (c.size() < 3 || (c[0] != "ON" && c[0] != "OFF")) continue;
            (bt ? p.bt : p.wifi).push_back(c);
        }
        return p;
    }
};

// ======================================================================
// scan 진행 상황 (화면용). scan 을 시작할 때마다 새로 만들고, 작업 스레드가 쓰고 WiFiScan 대화상자가 읽는다.
// 멈추면(freeze) 더 이상 바뀌지 않는다 (이미 취소된 이전 작업이 늦게 써도 무시됨).
// ======================================================================
struct ScanRow {
    int         st = 0;                                // 0=확인 중(노랑) 1=ON(초록) 2=OFF(빨강)
    uint32_t    ip = 0;                                // Wi-Fi 행 정렬용
    std::string c0, name, mac, vendor, user;           // c0 = IP 주소 (블루투스 행은 "<blue tooth>")
};

class ScanView {
public:
    mutable std::mutex mu;
    std::vector<ScanRow> bt, wifi;                     // 화면 순서: 블루투스 행 -> Wi-Fi 행 (IP 순)
    std::string phase;                                 // 현재 단계 문구
    unsigned ver = 0;                                  // 바뀔 때마다 증가 (대화상자가 다시 그릴지 판단)
    bool frozen = false;
    std::atomic<int> done{0}, total{0};                // Wi-Fi 스캔 진행 (처리한 IP 수 / 전체 IP 수)

    // 모든 칸이 비어 있는 행은 표시하지 않는다
    static bool blank(const ScanRow& r) { return r.c0.empty() && r.name.empty() && r.mac.empty() && r.vendor.empty() && r.user.empty(); }
    static void dropBlank(std::vector<ScanRow>& v) { v.erase(std::remove_if(v.begin(), v.end(), blank), v.end()); }
    bool isBt(int i) const { return i < (int)bt.size(); }

    int count() const { return (int)(bt.size() + wifi.size()); }          // 아래 함수들은 mu 를 잡고 호출
    const ScanRow& at(int i) const { return i < (int)bt.size() ? bt[i] : wifi[i - (int)bt.size()]; }

    void setPhase(const std::string& s) {
        std::lock_guard<std::mutex> g(mu);
        if (!frozen && phase != s) { phase = s; ver++; }
    }
    void freeze(const char* s) {
        std::lock_guard<std::mutex> g(mu);
        frozen = true;
        if (s && *s) phase = s;
        total = 0;
        ver++;
    }
    // 이전 결과 파일의 장치를 "확인 중"(노랑)으로 먼저 보여 준다. 스캔 대상이 아닌 구역은 OFF(빨강)
    void initFrom(const PrevResult& p, bool useBt, bool useWifi) {
        std::vector<ScanRow> b, w;
        for (auto& c : p.bt) {                         // 컬럼: Status, Address, Name, Paired, User Text
            ScanRow r; r.st = useBt ? 0 : 2; r.c0 = "<blue tooth>"; r.mac = c[1];
            r.name = c[2] == "<UnKnown>" ? "" : c[2];
            r.vendor = (c.size() > 3 && c[3] == "paired") ? "paired" : "";
            r.user = c.size() > 4 ? c[4] : "";
            if (!blank(r)) b.push_back(r);
        }
        for (auto& c : p.wifi) {                       // 컬럼: Status, IP, MAC, Name, Company, User Text
            uint32_t ip;
            if (c.size() < 5 || !StrUtil::parseIp(c[1], ip)) continue;
            ScanRow r; r.st = useWifi ? 0 : 2; r.ip = ip; r.c0 = c[1]; r.mac = c[2];
            r.name = c[3] == "<UnKnown>" ? "" : c[3];
            r.vendor = c[4]; r.user = c.size() > 5 ? c[5] : "";
            if (!blank(r)) w.push_back(r);
        }
        std::lock_guard<std::mutex> g(mu);
        if (frozen) return;
        bt.swap(b); wifi.swap(w); ver++;
    }
    void setBt(std::vector<ScanRow> v) {
        dropBlank(v);
        std::lock_guard<std::mutex> g(mu);
        if (frozen) return;
        bt.swap(v); ver++;
    }
    void setWifi(std::vector<ScanRow> v) {
        dropBlank(v);
        std::lock_guard<std::mutex> g(mu);
        if (frozen) return;
        wifi.swap(v); ver++;
    }
    // Wi-Fi 행 한 개를 넣거나 바꾼다 (IP 순 위치에)
    void upsertWifi(const ScanRow& r) {
        if (blank(r)) return;
        std::lock_guard<std::mutex> g(mu);
        if (frozen) return;
        auto it = std::lower_bound(wifi.begin(), wifi.end(), r.ip, [](const ScanRow& a, uint32_t ip) { return a.ip < ip; });
        if (it != wifi.end() && it->ip == r.ip) *it = r;
        else wifi.insert(it, r);
        ver++;
    }
};

// ======================================================================
// 블루투스
// ======================================================================
class Bluetooth {
public:
    static ULONGLONG addrOf(const std::string& norm) {        // "AA-BB-CC-DD-EE-FF" -> 0xAABBCCDDEEFF
        ULONGLONG v = 0;
        for (char c : norm) if (isxdigit((unsigned char)c)) v = (v << 4) | (ULONGLONG)(isdigit((unsigned char)c) ? c - '0' : toupper((unsigned char)c) - 'A' + 10);
        return v;
    }
    static std::string str(ULONGLONG a) {
        char t[32];
        snprintf(t, sizeof t, "%02X-%02X-%02X-%02X-%02X-%02X", (unsigned)((a >> 40) & 255), (unsigned)((a >> 32) & 255),
                 (unsigned)((a >> 24) & 255), (unsigned)((a >> 16) & 255), (unsigned)((a >> 8) & 255), (unsigned)(a & 255));
        return t;
    }
    // 1) 이미 연결 상태이면 바로 "있음"  2) 아니면 페어링 여부를 확인한 뒤 직접 불러 본다
    static bool present(ULONGLONG addr, std::string& info, const Cancel& cancel) {
        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof di; di.Address.ullLong = addr;
        DWORD r = BluetoothGetDeviceInfo(nullptr, &di);
        if (r != ERROR_SUCCESS) { info = "BluetoothGetDeviceInfo 오류 " + std::to_string(r) + " (어댑터 없음/꺼짐?)"; return false; }
        if (di.fConnected) { info = "연결 상태"; return true; }
        if (!di.fRemembered && !di.fAuthenticated) { info = "PC 에 페어링되어 있지 않음 (먼저 폰과 페어링 필요)"; return false; }
        return probe(addr, info, cancel);
    }
    // 블루투스 스캔: 페어링된 장치(연결 상태/직접 불러 보기) + 주변의 검색 가능한 장치(탐색). 이전 결과와 병합. 취소되면 빈 목록.
    // 진행 상황은 view 에 단계마다 반영 (확인 전 = 노랑, 응답 = 초록, 끝까지 응답 없음 = 빨강)
    static std::vector<BtDev> scan(const PrevResult& prev, const Cancel& cancel, ScanView& view) {
        std::map<ULONGLONG, BtDev> m;
        for (int pass = 0; pass < 2; pass++) {                // 0: 페어링 목록, 1: 주변 탐색(약 5초)
            BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {};
            sp.dwSize = sizeof sp; sp.fReturnAuthenticated = TRUE; sp.fReturnRemembered = TRUE; sp.fReturnConnected = TRUE;
            sp.fReturnUnknown = pass ? TRUE : FALSE; sp.fIssueInquiry = pass ? TRUE : FALSE;
            sp.cTimeoutMultiplier = pass ? 4 : 0; sp.hRadio = nullptr;
            BLUETOOTH_DEVICE_INFO di = {};
            di.dwSize = sizeof di;
            HBLUETOOTH_DEVICE_FIND h = BluetoothFindFirstDevice(&sp, &di);
            if (h) {
                do {
                    BtDev& d = m[di.Address.ullLong];
                    d.addr = di.Address.ullLong;
                    std::string n = StrUtil::u8(di.szName);
                    if (!n.empty()) d.name = n;
                    const bool paired = di.fRemembered || di.fAuthenticated;
                    if (paired) d.paired = true;
                    if (di.fConnected || (pass && !paired)) d.on = true;   // 연결 중이거나, 탐색에서 새로 발견된 장치
                } while (BluetoothFindNextDevice(h, &di));
                BluetoothFindDeviceClose(h);
            }
            if (cancel()) return {};
            publish(view, m, prev, false);
        }
        std::vector<std::thread> th;                          // 연결 안 된 페어링 장치는 직접 불러 본다 (동시에)
        for (auto& kv : m)
            if (kv.second.paired && !kv.second.on)
                th.emplace_back([d = &kv.second, &cancel]() { std::string info; d->on = present(d->addr, info, cancel); });
        for (auto& t : th) t.join();
        if (cancel()) return {};

        merge(m, prev);                                       // 이전 결과 병합: User Text 유지, 이번에 안 보인 장치는 OFF
        view.setBt(toRows(m, true));
        std::vector<BtDev> v;
        for (auto& kv : m) v.push_back(kv.second);
        return v;
    }
    // 블루투스를 스캔하지 않을 때: 이전 결과만 OFF 로 남긴다
    static std::vector<BtDev> offline(const PrevResult& prev, ScanView& view) {
        std::map<ULONGLONG, BtDev> m;
        merge(m, prev);
        view.setBt(toRows(m, true));
        std::vector<BtDev> v;
        for (auto& kv : m) v.push_back(kv.second);
        return v;
    }
private:
    static constexpr int kProbeMs = 8000;                     // 블루투스 호출(page) 대기 시간
    static void merge(std::map<ULONGLONG, BtDev>& m, const PrevResult& prev) {
        for (auto& c : prev.bt) {
            const std::string nm = StrUtil::normMac(c[1]);
            if (nm.empty()) continue;
            const ULONGLONG a = addrOf(nm);
            auto it = m.find(a);
            const std::string user = c.size() > 4 ? c[4] : "";
            if (it != m.end()) { it->second.user = user; if (it->second.name.empty()) it->second.name = c[2]; }
            else { BtDev d; d.addr = a; d.name = c[2]; d.paired = c.size() > 3 && c[3] == "paired"; d.user = user; m[a] = d; }
        }
    }
    static std::vector<ScanRow> toRows(const std::map<ULONGLONG, BtDev>& m, bool fin) {
        std::vector<ScanRow> v;
        for (auto& kv : m) {
            const BtDev& d = kv.second;
            ScanRow r;
            r.st = d.on ? 1 : (fin ? 2 : 0);
            r.c0 = "<blue tooth>"; r.name = d.name == "<UnKnown>" ? "" : d.name; r.mac = str(d.addr);
            r.vendor = d.paired ? "paired" : ""; r.user = d.user;
            v.push_back(r);
        }
        return v;
    }
    static void publish(ScanView& view, std::map<ULONGLONG, BtDev> m, const PrevResult& prev, bool fin) {
        merge(m, prev);
        view.setBt(toRows(m, fin));
    }
    // 장치를 직접 불러 본다 (RFCOMM 채널 1 연결 시도). 연결되거나 "거부됨" 이면 장치가 응답한 것 -> 근처에 있음.
    // 시간 초과/네트워크 오류면 응답 없음. 페어링된 장치만 시험한다 (미페어링 장치를 부르면 PC 에 페어링 요청이 뜰 수 있음).
    // 기다리는 동안 0.2초마다 취소 여부를 확인한다.
    static bool probe(ULONGLONG addr, std::string& info, const Cancel& cancel) {
        SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (s == INVALID_SOCKET) { info = "블루투스 소켓 생성 실패 (오류 " + std::to_string(WSAGetLastError()) + ", 어댑터 없음/꺼짐?)"; return false; }
        SOCKADDR_BTH sa = {};
        sa.addressFamily = AF_BTH; sa.btAddr = addr; sa.port = 1;
        u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
        bool ok = false;
        if (connect(s, (sockaddr*)&sa, sizeof sa) == 0) {
            ok = true; info = "응답함 (연결됨)";
        } else {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) {
                bool ready = false, writable = false;
                for (int waited = 0; waited < kProbeMs && !ready && !cancel(); waited += 200) {
                    fd_set wf, ef; FD_ZERO(&wf); FD_ZERO(&ef); FD_SET(s, &wf); FD_SET(s, &ef);
                    timeval tv; tv.tv_sec = 0; tv.tv_usec = 200 * 1000;
                    if (select(0, nullptr, &wf, &ef, &tv) > 0) { ready = true; writable = FD_ISSET(s, &wf) != 0; }
                }
                if (ready) {
                    int err = 0, len = sizeof err;
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
                    if (err == 0 && writable) { ok = true; info = "응답함 (연결됨)"; }
                    else if (err == WSAECONNREFUSED) { ok = true; info = "응답함 (서비스 거부 = 장치는 근처에 있음)"; }
                    else info = "응답 없음 (오류 " + std::to_string(err) + ")";
                } else info = cancel() ? "취소됨" : "응답 없음 (" + std::to_string(kProbeMs / 1000) + "초 시간 초과)";
            } else if (e == WSAECONNREFUSED) {
                ok = true; info = "응답함 (서비스 거부 = 장치는 근처에 있음)";
            } else {
                info = "응답 없음 (오류 " + std::to_string(e) + ")";
            }
        }
        closesocket(s);
        return ok;
    }
};

// ======================================================================
// 결과 파일 WiFiScan_result.txt ([Bluetooth] 구역 + [Wi-Fi] 구역. 내용/형식은 WiFiScan.cpp 와 같음)
// 두 구역을 한꺼번에 임시 파일에 쓴 뒤 바꿔치기한다 -> scan 이 도중에 멈춰도 기존 파일(User Text)은 그대로 남는다.
// ======================================================================
class ResultFile {
public:
    explicit ResultFile(std::string path) : path_(std::move(path)) {}
    const std::string& path() const { return path_; }
    bool write(const std::vector<BtDev>& bt, const std::vector<Host>& wifi) const {
        std::string s = "\xEF\xBB\xBF";
        s += "[Bluetooth]\r\n";
        s += "Status\tBluetooth Address\tDevice Name\tPaired\tUser Text\r\n";
        for (auto& d : bt)
            s += std::string(d.on ? "ON" : "OFF") + '\t' + Bluetooth::str(d.addr) + '\t' + (d.name.empty() ? "<UnKnown>" : d.name) + '\t' +
                 (d.paired ? "paired" : "-") + '\t' + d.user + "\r\n";
        s += "\r\n";
        s += "[Wi-Fi]\r\n";
        s += "Status\tIP Address\tMAC Address\tComputer Name\tNetwork Adapter Company\tUser Text\r\n";
        for (auto& h : wifi)
            s += std::string(h.on ? "ON" : "OFF") + '\t' + StrUtil::ipStr(h.ip) + '\t' + h.mac + '\t' +
                 (h.name.empty() ? "<UnKnown>" : h.name) + '\t' + h.vendor + '\t' + h.user + "\r\n";
        const std::string tmp = path_ + ".tmp";
        bool ok = false;
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            if (o) { o << s; o.flush(); ok = o.good(); }
        }
        if (ok) ok = MoveFileExA(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) { DWORD e = GetLastError(); DeleteFileA(tmp.c_str()); Logger::log("결과 파일 쓰기 실패 (오류 " + std::to_string(e) + "): " + path_); }
        return ok;
    }
private:
    std::string path_;
};

// ======================================================================
// Wi-Fi 전체 스캔 (ping + ARP, 병렬)
// 이전 결과에 있던 IP 는 절전 기기일 수 있으므로 최대 3회 재시도, 이번에 응답 없는 기존 장치는 OFF 로 남기고 User Text 는 유지.
// 찾는 즉시 view 에 반영 (이전 결과에 있던 장치는 처음엔 노랑, 찾으면 초록, 끝까지 못 찾으면 빨강). 취소되면 빈 목록.
// ======================================================================
class WifiScanner {
public:
    WifiScanner(const Subnet& sn, const PrevResult& prev, ScanView& view) : sn_(sn), prev_(prev), view_(view) {}
    std::vector<Host> run(const Cancel& cancel) {
        OuiDb oui(AppDir::ouiFile());
        if (oui.empty()) Logger::log("(oui.txt 없음: 제조사명은 비어 있습니다)");
        std::set<uint32_t> known;
        std::map<uint32_t, std::string> users;                // 이전 결과의 User Text (찾는 즉시 화면에 같이 보여 주기 위함)
        for (auto& c : prev_.wifi) {
            uint32_t kip;
            if (c.size() > 1 && StrUtil::parseIp(c[1], kip)) { known.insert(kip); if (c.size() > 5) users[kip] = c[5]; }
        }
        std::vector<Host> found; std::mutex mu; std::atomic<uint32_t> next(sn_.net + 1);
        const uint32_t bc = sn_.bc;
        view_.done = 0; view_.total = (int)(bc - sn_.net - 1);
        auto worker = [&]() {
            HANDLE ic = IcmpCreateFile();
            char data[4] = {'p', 'i', 'n', 'g'}, reply[sizeof(ICMP_ECHO_REPLY) + 64];
            for (;;) {
                uint32_t ip = next++;
                if (ip >= bc || cancel()) break;
                bool on = false, arp = false;
                ULONG m[2] = {0};
                for (int t = known.count(ip) ? 3 : 1; t > 0 && !on && !arp && !cancel(); t--) {
                    on = IcmpSendEcho(ic, htonl(ip), data, 4, 0, reply, sizeof reply, 1000) > 0 &&
                         ((ICMP_ECHO_REPLY*)reply)->Status == IP_SUCCESS;
                    ULONG len = 6;
                    arp = SendARP(htonl(ip), 0, m, &len) == NO_ERROR && len == 6;
                }
                if (!on && !arp) { view_.done++; continue; }
                Host h; h.ip = ip; h.on = true;
                if (arp) { h.mac = StrUtil::macStr((unsigned char*)m); h.vendor = oui.vendorOf(h.mac); }
                auto u = users.find(ip);
                if (u != users.end()) h.user = u->second;
                view_.upsertWifi(rowOf(h));                   // 찾는 즉시 표시 (이름은 아직 없음)
                h.name = NameResolver::hostName(ip);
                view_.upsertWifi(rowOf(h));
                { std::lock_guard<std::mutex> g(mu); found.push_back(h); }
                view_.done++;
            }
            IcmpCloseHandle(ic);
        };
        std::vector<std::thread> th;
        for (int i = 0; i < 64; i++) th.emplace_back(worker);
        for (auto& t : th) t.join();
        if (cancel()) return {};
        return finish(found);
    }
    // 네트워크가 없거나 Wi-Fi 를 스캔하지 않을 때: 이전 결과만 OFF 로 남긴다
    std::vector<Host> runOffline() {
        std::vector<Host> found;
        return finish(found);
    }
private:
    std::vector<Host> finish(std::vector<Host>& found) {
        mergePrevious(found);
        std::sort(found.begin(), found.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
        int on = 0;
        std::vector<ScanRow> rows;
        for (auto& h : found) { on += h.on; rows.push_back(rowOf(h)); }
        Logger::log("Wi-Fi 스캔 완료: " + std::to_string(found.size()) + "개 장치 (ON " + std::to_string(on) + " / OFF " +
                    std::to_string(found.size() - on) + ") -> " + AppDir::resultFile());
        view_.setWifi(rows);
        return found;
    }
    static ScanRow rowOf(const Host& h) {
        ScanRow r;
        r.st = h.on ? 1 : 2; r.ip = h.ip; r.c0 = StrUtil::ipStr(h.ip);
        r.name = h.name == "<UnKnown>" ? "" : h.name; r.mac = h.mac; r.vendor = h.vendor; r.user = h.user;
        return r;
    }
    void mergePrevious(std::vector<Host>& found) const {      // 컬럼 순서: Status, IP, MAC, Name, Company, User Text
        for (auto& c : prev_.wifi) {
            uint32_t ip;
            if (c.size() < 5 || !StrUtil::parseIp(c[1], ip)) continue;
            std::string user = c.size() > 5 ? c[5] : "";
            std::string pname = (c[3] == "<UnKnown>") ? "" : c[3];
            auto it = std::find_if(found.begin(), found.end(), [&](const Host& h) { return h.ip == ip; });
            if (it != found.end()) { it->user = user; if (it->name.empty()) it->name = pname; }
            else found.push_back({ip, false, pname, c[2], c[4], user});
        }
    }
    const Subnet& sn_;
    const PrevResult& prev_;
    ScanView& view_;
};

// ======================================================================
// 명령 전달 (HomeWatcher -> Telegram 모듈): 같은 프로세스 안에서 tg::PostTrigger 로 넘긴다
// ======================================================================
// 작업 스레드 -> 본 창(UI 스레드) 알림: 하단 상태줄 문구, 모니터 중앙 팝업 (텔레그램 명령 전달 등), 앱 종료 요청
class ScanEvents {
public:
    enum { MSG = WM_APP + 4 };
    struct Item { int kind; std::string text; };      // kind: 0=상태줄, 1=팝업(5초), 2=HomeBack 프로그램 실행, 3=NotHome 프로그램 실행, 4=앱 종료
    static void SetWindow(HWND h) {
        bool any;
        { std::lock_guard<std::mutex> g(mu_); wnd_ = h; any = !q_.empty(); }
        if (h && any) PostMessageW(h, MSG, 0, 0);
    }
    static void Status(const std::string& what) {          // "HH:MM:SS  내용"
        SYSTEMTIME t;
        GetLocalTime(&t);
        char b[32];
        snprintf(b, sizeof b, "%02d:%02d:%02d  ", t.wHour, t.wMinute, t.wSecond);
        push(Item{0, std::string(b) + what});
    }
    static void Popup(const std::string& text) { push(Item{1, text}); }
    static void Program(int home) { push(Item{home ? 3 : 2, std::string()}); }   // home: 0=HomeBack, 1=NotHome
    static void Quit() { push(Item{4, std::string()}); }                         // 텔레그램 /stop, /restart: 본 창이 앱을 종료함 (확인창 없이)
    static std::vector<Item> Take() {
        std::lock_guard<std::mutex> g(mu_);
        std::vector<Item> v;
        v.swap(q_);
        return v;
    }
private:
    static void push(Item it) {
        HWND h;
        {
            std::lock_guard<std::mutex> g(mu_);
            q_.push_back(std::move(it));
            if (q_.size() > 50) q_.erase(q_.begin());
            h = wnd_;
        }
        if (h) PostMessageW(h, MSG, 0, 0);
    }
    static inline std::mutex mu_;
    static inline std::vector<Item> q_;
    static inline HWND wnd_ = NULL;
};

namespace tg { void PostTrigger(const std::string& cmd); }      // Telegram 모듈 (아래쪽 namespace tg)

class TriggerFile {
public:
    // /HomeBack, /NotHome 을 같은 프로세스의 Telegram 모듈에 넘긴다.
    // (예전에는 screen_telegram 폴더의 trigger.txt 에 써서 다른 프로세스(screen_telegram.exe)가 1초마다 읽었다)
    void send(const std::string& cmd) const {
        tg::PostTrigger(cmd);
        Logger::log("텔레그램 모듈에 명령 전달: " + cmd);
        ScanEvents::Popup("텔레그램 명령 전달: " + cmd);        // 모니터 중앙에 5초 팝업 (본 창이 표시)
        if (cmd == "/HomeBack") ScanEvents::Program(0);
        else if (cmd == "/NotHome") ScanEvents::Program(1);
    }
};


// ======================================================================
// scan 설정 (ini [WiFiScan] 값. 대화상자에서 저장하면 실행 중인 감시에도 바로 반영됨)
// ======================================================================
struct ScanCfg {
    bool bt = true, wifi = true;                              // scan 대상 (체크박스)
    std::string btMac, mac, ip;                               // 폰 블루투스 주소 / 폰 Wi-Fi(LAN 카드) MAC / 폰 고정 IP (선택)
    int interval = 15, awaySec = 60;                          // 감시 주기(초), 외출 판정(초)
};

class ScanCfgBox {
public:
    void set(const ScanCfg& c) { std::lock_guard<std::mutex> g(mu_); c_ = c; }
    ScanCfg get() const { std::lock_guard<std::mutex> g(mu_); return c_; }
private:
    mutable std::mutex mu_;
    ScanCfg c_;
};

// ======================================================================
// 집 안팎 감시
//   판정 순서: 1) 블루투스(HomeBluetooth) 로 확인  2) 확인되지 않으면 Wi-Fi 스캔(HomeMac) 으로 확인
//   집에 있음으로 바뀌면 -> /HomeBack, NotHome 초 연속 둘 다 미확인 -> /NotHome (WiFiScan_watch.log)
//   전달 방식: 같은 프로세스의 Telegram 모듈(namespace tg)에 직접 넘긴다 (tg::PostTrigger)
// ======================================================================
class HomeWatcher {
public:
    HomeWatcher(Subnet sn, ScanCfgBox& cfg, ScanView& view, const Cancel& cancel)
        : sn_(sn), cfg_(cfg), view_(view), cancel_(cancel) {}

    // 취소될 때까지 감시한다 (scan Off / 재시작 / 앱 종료 때 끝남)
    void run() {
        apply(cfg_.get());
        Logger::log("설정: gsHomeManager.ini [WiFiScan] / /HomeBack, /NotHome 은 같은 프로세스의 Telegram 모듈로 전달");
        Logger::log("감시 시작: 블루투스 " + (haveBt() ? btMac_ : std::string("(없음)")) + " -> Wi-Fi " + (haveWifi() ? mac_ : std::string("(없음)")) +
                    " (간격 " + std::to_string(interval_) + "초, 외출 판정 " + std::to_string(awaySec_) + "초)");
        for (;;) try {
            if (cancel_()) return;
            if (apply(cfg_.get()))
                Logger::log("설정 변경: 블루투스 " + (haveBt() ? btMac_ : std::string("(없음)")) + " / Wi-Fi " + (haveWifi() ? mac_ : std::string("(없음)")) +
                            " (간격 " + std::to_string(interval_) + "초, 외출 판정 " + std::to_string(awaySec_) + "초)");
            if (!haveBt() && !haveWifi()) {            // 확인할 대상이 없어도 멈추지 않고, 설정이 저장되면 자동으로 시작
                if (!warned_) { Logger::log("HomeBluetooth 와 HomeMac 이 없거나 스캔 대상이 아닙니다. 설정을 기다리는 중"); warned_ = true; }
                view_.setPhase("감시 대기 중: 블루투스 / LAN 카드 MAC 주소를 입력하고 저장하세요");
                if (sleepOrStop(interval_)) return;
                continue;
            }
            warned_ = false;
            if (state_ == -1) view_.setPhase("감시 중 - 블루투스 / Wi-Fi 확인 중...");
            std::string via;
            const bool present = checkBluetooth(via) || checkWifi(via);   // 1) 블루투스 우선  2) 확인되지 않으면 Wi-Fi
            if (cancel_()) return;
            update(present, via);
            if (sleepOrStop(interval_)) return;
        } catch (const std::exception& e) {
            Logger::log(std::string("오류: ") + e.what());
            if (sleepOrStop(interval_)) return;
        }
    }

private:
    // interval 초 동안 쉬면서 취소를 본다. 멈춰야 하면 true
    bool sleepOrStop(int seconds) {
        for (int i = 0; i < seconds * 10; i++) {
            if (cancel_()) return true;
            Sleep(100);
        }
        return cancel_();
    }

    // 설정(ini/대화상자 값)을 읽어 반영. 바뀐 것이 있으면 true
    bool apply(const ScanCfg& c) {
        const std::string bm = StrUtil::normMac(c.btMac), m = StrUtil::normMac(c.mac);
        uint32_t ip = 0;
        if (!StrUtil::parseIp(c.ip, ip)) ip = 0;                                       // 선택: 폰 고정 IP
        const int iv = c.interval > 0 ? std::max(5, c.interval) : 15;
        const int as = c.awaySec > 0 ? c.awaySec : 60;
        const bool changed = bm != btMac_ || m != mac_ || ip != homeIp_ || c.bt != useBt_ || c.wifi != useWifi_ || iv != interval_ || as != awaySec_;
        if (!changed) return false;
        btMac_ = bm; mac_ = m; homeIp_ = ip; useBt_ = c.bt; useWifi_ = c.wifi; interval_ = iv; awaySec_ = as;
        btAddr_ = btMac_.empty() ? 0 : Bluetooth::addrOf(btMac_);
        ip_ = 0; lastBtInfo_.clear(); lastWifiInfo_.clear();
        return true;
    }
    bool haveBt() const { return useBt_ && btAddr_ != 0; }
    bool haveWifi() const { return useWifi_ && !mac_.empty(); }

    bool checkBluetooth(std::string& via) {
        if (!haveBt()) return false;
        std::string info;
        const bool ok = Bluetooth::present(btAddr_, info, cancel_);
        if (ok) via = "Bluetooth";
        if (info != lastBtInfo_) { Logger::log("Bluetooth: " + info); lastBtInfo_ = info; }
        return ok;
    }

    bool checkWifi(std::string& via) {
        if (!haveWifi()) return false;
        if (!sn_.valid() && sn_.detect())     // 시작할 때 네트워크가 없었다면 다시 찾는다
            Logger::log("네트워크 어댑터 감지: Wi-Fi 확인 시작 (" + sn_.range() + ")");
        if (!sn_.valid()) return false;
        bool ok = false;
        if (homeIp_ && Arp::check(homeIp_, mac_)) { ip_ = homeIp_; ok = true; }
        if (!ok && ip_ && ip_ != homeIp_) ok = Arp::check(ip_, mac_);
        if (!ok) { ip_ = Arp::find(sn_, mac_, cancel_); ok = (ip_ != 0); }
        if (ok) via = "Wi-Fi " + StrUtil::ipStr(ip_);
        const std::string wi = ok ? "확인됨 (" + StrUtil::ipStr(ip_) + ")"
                                  : "응답 없음 (HomeMac " + mac_ + ", 범위 " + sn_.range() + ")";
        if (wi != lastWifiInfo_) { Logger::log("Wi-Fi: " + wi); lastWifiInfo_ = wi; }
        return ok;
    }

    // 상태 판정: -1 미정 -> 알림, 0(AWAY) -> 1(HOME) 이면 /HomeBack, 1 -> 0 은 awaySec_ 초 연속 미확인일 때 /NotHome
    void update(bool present, const std::string& via) {
        const time_t now = time(0);
        if (present) lastSeen_ = now;
        if (state_ == -1) {
            state_ = present ? 1 : 0;
            Logger::log(std::string("초기 상태: ") + (present ? "HOME (" + via + ")" : "AWAY"));
            trigger_.send(present ? "/HomeBack" : "/NotHome");     // 시작 시 현재 상태도 전송
        } else if (state_ == 0 && present) {
            state_ = 1;
            Logger::log("HOME (" + via + ")");
            trigger_.send("/HomeBack");
        } else if (state_ == 1 && !present && now - lastSeen_ >= awaySec_) {
            state_ = 0;
            Logger::log("AWAY (" + std::to_string(awaySec_) + "초 연속 미확인: 블루투스, Wi-Fi)");
            trigger_.send("/NotHome");
        }
        if (state_ == 1) view_.setPhase("감시 중 - 집에 있음 (" + (present ? via : "미확인 " + std::to_string((long long)(now - lastSeen_)) + "초 / 외출 판정 " + std::to_string(awaySec_) + "초") + ")");
        else             view_.setPhase("감시 중 - 외출 중 (집에서 확인되지 않음)");
    }

    Subnet sn_;
    ScanCfgBox& cfg_;
    ScanView& view_;
    const Cancel& cancel_;
    TriggerFile trigger_;
    std::string mac_, btMac_, lastBtInfo_, lastWifiInfo_;
    ULONGLONG btAddr_ = 0;
    uint32_t homeIp_ = 0, ip_ = 0;
    bool useBt_ = false, useWifi_ = false, warned_ = false;
    int interval_ = 15, awaySec_ = 60, state_ = -1;           // state_: -1 미정, 1 HOME, 0 AWAY
    time_t lastSeen_ = 0;
};

// ======================================================================
// scan 엔진: 작업 스레드 1개가 (블루투스 스캔 -> Wi-Fi 스캔 -> 결과 파일 -> 감시) 를 수행.
// Start() = scan On / 재시작 (이전 작업이 있으면 취소하고 끝나길 기다렸다가 새로 시작), Stop() = scan Off (즉시 반환)
// ======================================================================
class ScanEngine {
public:
    ~ScanEngine() { Shutdown(); }

    static ScanCfg FromSettings(const Settings& s) {
        ScanCfg c;
        c.bt = s.scanBt; c.wifi = s.scanWifi;
        c.btMac = s.homeBt; c.mac = s.homeMac; c.ip = s.homeIp;
        c.interval = s.scanSec; c.awaySec = s.notHomeSec;
        return c;
    }
    void SetConfig(const ScanCfg& c) { cfg_.set(c); }          // 실행 중인 감시도 다음 확인부터 이 값을 쓴다
    std::shared_ptr<ScanView> View() const { std::lock_guard<std::mutex> g(vmu_); return view_; }
    bool IsOn() const { return on_; }

    void Start(const char* why = "") {
        std::lock_guard<std::mutex> g(ctl_);
        ScanEvents::Status(std::string("scan On") + (why && *why ? std::string(" - ") + why : std::string()));
        auto nv = std::make_shared<ScanView>();
        nv->phase = "scan 시작 중...";
        {
            std::lock_guard<std::mutex> vg(vmu_);
            if (view_) view_->freeze(nullptr);                 // 이전 화면 데이터는 더 이상 바뀌지 않게
            view_ = nv;
        }
        const int my = ++gen_;                                 // 이전 작업은 취소된 것으로 본다
        on_ = true;
        std::thread prev = std::move(th_);
        th_ = std::thread([this, my, nv, p = std::move(prev)]() mutable {
            if (p.joinable()) p.join();                        // 이전 작업이 완전히 끝난 뒤에 시작 (결과 파일을 동시에 쓰지 않도록)
            Cancel cancel{&gen_, my};
            if (cancel()) return;
            running_++;
            try { Work(*nv, cancel); }
            catch (const std::exception& e) { Logger::log(std::string("scan 오류: ") + e.what()); }
            catch (...) { Logger::log("scan 오류 (알 수 없음)"); }
            running_--;
        });
    }

    void Stop(const char* why = "") {
        std::lock_guard<std::mutex> g(ctl_);
        ScanEvents::Status(std::string("scan Off") + (why && *why ? std::string(" - ") + why : std::string()));
        ++gen_;
        on_ = false;
        auto v = View();
        if (v) v->freeze("scan Off (정지)");
        Logger::log("scan Off (정지 요청)");
    }

    // 앱 종료: 작업을 취소하고 최대 2초 기다린다. 모두 끝났으면 true, 아직 도는 스레드가 있으면 떼어 놓고 false
    bool Shutdown() {
        std::lock_guard<std::mutex> g(ctl_);
        ++gen_;
        on_ = false;
        for (int i = 0; i < 40 && running_ > 0; i++) Sleep(50);
        const bool clean = running_ == 0;
        if (th_.joinable()) { if (clean) th_.join(); else th_.detach(); }
        return clean;
    }

private:
    static void SleepMs(int ms, const Cancel& cancel) { for (int t = 0; t < ms && !cancel(); t += 100) Sleep(100); }

    void Work(ScanView& view, const Cancel& cancel) {
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        struct WsaGuard { ~WsaGuard() { WSACleanup(); } } wsaGuard;
        const ScanCfg c = cfg_.get();
        Logger::log("WiFiScan 시작: 1) 블루투스 스캔 2) Wi-Fi 스캔 3) 감시");

        // 부팅 직후에는 네트워크가 아직 준비되지 않았을 수 있으므로 최대 60초 기다린다 (없으면 블루투스만으로 진행, 감시 중 다시 찾음)
        Subnet sn;
        if (c.wifi) {
            for (int i = 0; i < 12 && !sn.detect(); i++) {
                if (i == 0) { Logger::log("네트워크 어댑터 대기 중 (최대 60초)"); view.setPhase("네트워크 어댑터 대기 중 (최대 60초)"); }
                SleepMs(5000, cancel);
                if (cancel()) return;
            }
            if (sn.valid()) Logger::log("스캔 범위: " + sn.range());
            else Logger::log("네트워크 어댑터를 찾지 못했습니다: Wi-Fi 스캔은 건너뛰고 블루투스로 감시합니다 (어댑터가 생기면 자동으로 Wi-Fi 확인 시작)");
        }

        ResultFile out(AppDir::resultFile());
        PrevResult prev = PrevResult::load(out.path());       // 새 파일을 만들기 전에 이전 결과를 읽어 둔다
        view.initFrom(prev, c.bt, c.wifi);

        // 1) 블루투스 스캔
        std::vector<BtDev> bt;
        if (c.bt) {
            Logger::log("블루투스 스캔 중 (주변 탐색 약 5초 + 페어링 장치 확인)");
            view.setPhase("블루투스 스캔 중 (주변 탐색 약 5초 + 페어링 장치 확인)");
            bt = Bluetooth::scan(prev, cancel, view);
            if (cancel()) return;
            int on = 0;
            for (auto& d : bt) on += d.on;
            Logger::log("블루투스 스캔 완료: " + std::to_string(bt.size()) + "개 장치 (ON " + std::to_string(on) + " / OFF " +
                        std::to_string(bt.size() - on) + ")" + (bt.empty() ? " - 어댑터 없음/꺼짐?" : "") + " -> " + out.path());
        } else {
            bt = Bluetooth::offline(prev, view);
            Logger::log("블루투스 스캔 안 함 (스캔 대상 아님): 이전 결과만 OFF 로 남김");
        }

        // 2) Wi-Fi 전체 스캔 (네트워크가 없거나 스캔 대상이 아니면 이전 결과만 OFF 로 남긴다)
        std::vector<Host> wifi;
        WifiScanner ws(sn, prev, view);
        if (c.wifi && sn.valid()) {
            Logger::log("Wi-Fi 스캔 중");
            view.setPhase("Wi-Fi 스캔 중 (ping + ARP)");
            wifi = ws.run(cancel);
            if (cancel()) return;
        } else {
            wifi = ws.runOffline();
            if (!c.wifi) Logger::log("Wi-Fi 스캔 안 함 (스캔 대상 아님)");
        }

        // 결과 파일: 두 구역을 한꺼번에 쓴다 (취소되면 쓰지 않고 기존 파일을 그대로 둠)
        if (cancel()) return;
        out.write(bt, wifi);

        // 3) 감시
        view.setPhase("감시 시작");
        HomeWatcher w(sn, cfg_, view, cancel);
        w.run();
    }

    ScanCfgBox cfg_;
    mutable std::mutex ctl_, vmu_;
    std::shared_ptr<ScanView> view_;
    std::thread th_;
    std::atomic<int> gen_{0}, running_{0};
    std::atomic<bool> on_{false};
};


// ---------------------------------------------------------------- ScanDialog
// WiFiScan 대화상자 (코드로 만든 대화상자): 블루투스 MAC / LAN 카드 MAC / scan 주기 입력 3칸, 스캔 대상 체크 2개(Wi-Fi, 블루투스),
// 저장 / 취소 / scan 재시작 / scan Off 버튼, 아래쪽에 scan 진행 상황 표 (상태 점: 노랑=확인 중, 초록=ON, 빨강=OFF).
// 저장 = ini 에 저장 + 실행 중인 감시에 반영하고 닫기 / scan 재시작 = 저장 + 처음부터 다시 scan (창은 유지) / scan Off = scan 정지
// 취소 = 저장하지 않고 닫기. 창을 닫아도 scan 은 계속 동작한다.
class ScanDialog {
public:
    static void Show(HINSTANCE hi, HWND owner, Settings& cfg, ScanEngine& eng, const char* notice = NULL) {
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&ic);

        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(15); b.W(0); b.W(0); b.W(430); b.W(248);        // 항목 수, x, y, cx, cy (대화상자 단위)
        b.W(0); b.W(0);
        b.S(notice ? "내 폰 등록" : "WiFiScan (블루투스 / Wi-Fi 스캔, 집 안팎 감시)");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        const DWORD E = T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        const DWORD C = T | WS_TABSTOP | BS_AUTOCHECKBOX;
        b.Control(T,                 10,  10,  82, 10, 110, "STATIC", "블루투스 MAC 주소");
        b.Control(E,                 96,   8,  96, 13, IDC_BTMAC, "EDIT", "");
        b.Control(T,                204,  10,  82, 10, 111, "STATIC", "LAN 카드 MAC 주소");
        b.Control(E,                290,   8,  96, 13, IDC_LANMAC, "EDIT", "");
        b.Control(T,                 10,  28,  82, 10, 112, "STATIC", "scan 주기 (초)");
        b.Control(E | ES_NUMBER,     96,  26,  40, 13, IDC_SEC, "EDIT", "");
        b.Button(C,                 204,  27,  76, 12, IDC_CHKWIFI, "Wi-Fi 스캔");
        b.Button(C,                 290,  27,  80, 12, IDC_CHKBT, "블루투스 스캔");
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON,  10, 46, 50, 14, IDOK, "저장");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,     64, 46, 50, 14, IDCANCEL, "취소");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,    204, 46, 76, 14, IDC_RESTART, "scan 재시작");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,    290, 46, 60, 14, IDC_OFF, "scan Off");
        b.Control(T | SS_ENDELLIPSIS,                10, 65, 410, 10, IDC_NOTICE, "STATIC", "");      // 안내 문구 (내 폰 등록)
        b.Control(T | SS_ENDELLIPSIS,                10, 76, 410, 10, IDC_STATUS, "STATIC", "");
        b.Control(T | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                  10, 90, 410, 148, IDC_LIST, "SysListView32", "");

        Ctx c = {&cfg, &eng, nullptr, 0, "", "", "", notice};
        DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c);
    }

private:
    enum { IDC_BTMAC = 101, IDC_LANMAC = 102, IDC_SEC = 103, IDC_CHKWIFI = 104, IDC_CHKBT = 105,
           IDC_RESTART = 106, IDC_OFF = 107, IDC_STATUS = 108, IDC_LIST = 109, IDC_NOTICE = 114 };
    struct Ctx { Settings* cfg; ScanEngine* eng; std::shared_ptr<ScanView> view; unsigned ver; std::string status; std::string btNorm, lanNorm; const char* notice; };

    static void SetText(HWND d, int id, const char* utf8) {
        char16_t u[400];
        u[0] = 0;
        Utf8::ToUtf16(utf8, -1, u, 400);
        SetDlgItemTextW(d, id, (LPCWSTR)u);
    }

    // 입력 칸의 글자를 UTF-8 로 (앞뒤 공백 제거)
    static void GetText(HWND d, int id, char* out, int cap) {
        char16_t u[200];
        GetDlgItemTextW(d, id, (LPWSTR)u, 200);
        char k[600];
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, k, sizeof(k), NULL, NULL);
        char* p = k;
        while (*p == ' ' || *p == '\t') p++;
        int n = lstrlenA(p);
        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = 0;
        lstrcpynA(out, p, cap);
    }

    static void Warn(HWND d, const char* msg, int focusId) {
        char16_t m[200], t[32];
        Utf8::ToUtf16(msg, -1, m, 200);
        Utf8::ToUtf16("WiFiScan", -1, t, 32);
        MessageBoxW(d, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(d, focusId));
    }

    // 상태 점 이미지 목록: 0 노랑(확인 중), 1 초록(ON), 2 빨강(OFF)
    static HIMAGELIST MakeDots() {
        static const COLORREF col[3] = {RGB(255, 200, 0), RGB(40, 190, 40), RGB(225, 30, 30)};
        HDC sdc = GetDC(NULL);
        HDC dc = CreateCompatibleDC(sdc);
        HBITMAP bm = CreateCompatibleBitmap(sdc, 48, 16);
        HGDIOBJ obm = SelectObject(dc, bm);
        HBRUSH bg = CreateSolidBrush(RGB(255, 0, 255));                    // 마스크 색 (투명)
        RECT rc = {0, 0, 48, 16};
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
        HGDIOBJ op = SelectObject(dc, pen);
        for (int i = 0; i < 3; i++) {
            HBRUSH br = CreateSolidBrush(col[i]);
            HGDIOBJ ob = SelectObject(dc, br);
            Ellipse(dc, i * 16 + 2, 2, i * 16 + 14, 14);
            SelectObject(dc, ob);
            DeleteObject(br);
        }
        SelectObject(dc, op);
        DeleteObject(pen);
        SelectObject(dc, obm);
        HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 3, 0);
        ImageList_AddMasked(il, bm, RGB(255, 0, 255));
        DeleteObject(bm);
        DeleteDC(dc);
        ReleaseDC(NULL, sdc);
        return il;
    }

    // 행 맨 앞 체크박스 이미지 (상태 이미지 번호 1=해제, 3=체크. 앞 두 칸은 번호 계산 방식이 달라도 되도록 같은 그림으로 채움)
    static HIMAGELIST MakeChecks() {
        HDC sdc = GetDC(NULL);
        HDC dc = CreateCompatibleDC(sdc);
        HBITMAP bm = CreateCompatibleBitmap(sdc, 64, 16);
        HGDIOBJ obm = SelectObject(dc, bm);
        RECT all = {0, 0, 64, 16};
        FillRect(dc, &all, GetSysColorBrush(COLOR_WINDOW));
        for (int i = 0; i < 4; i++) {
            RECT r = {i * 16 + 1, 1, i * 16 + 15, 15};
            DrawFrameControl(dc, &r, DFC_BUTTON, DFCS_BUTTONCHECK | (i >= 2 ? DFCS_CHECKED : 0));
        }
        SelectObject(dc, obm);
        HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR24, 4, 0);
        ImageList_Add(il, bm, NULL);
        DeleteObject(bm);
        DeleteDC(dc);
        ReleaseDC(NULL, sdc);
        return il;
    }

    // 행의 체크 상태 = 그 행의 MAC 이 해당 입력 칸(블루투스 / LAN 카드)의 MAC 과 같은지
    static int CheckState(const Ctx* c, bool bt, const std::string& mac) {
        const std::string nm = StrUtil::normMac(mac);
        if (nm.empty()) return 0;                                           // MAC 이 없는 행은 체크박스 없음
        return nm == (bt ? c->btNorm : c->lanNorm) ? 3 : 1;
    }

    // 체크박스 클릭/스페이스: 같은 종류(블루투스 / Wi-Fi) 에서 한 행만 선택. 해당 MAC 입력 칸에 넣거나 비운다 (ini 에는 저장/재시작 때 기록)
    static void ToggleRow(HWND d, Ctx* c, int item) {
        if (!c->notice || !c->view || item < 0) return;                     // 내 폰 등록일 때만 목록 체크박스 동작
        bool bt;
        std::string nm;
        {
            std::lock_guard<std::mutex> g(c->view->mu);
            if (item >= c->view->count()) return;
            bt = c->view->isBt(item);
            nm = StrUtil::normMac(c->view->at(item).mac);
        }
        if (nm.empty()) return;
        if (nm == (bt ? c->btNorm : c->lanNorm)) { SetText(d, bt ? IDC_BTMAC : IDC_LANMAC, ""); return; }
        for (char& ch : nm) if (ch == '-') ch = ':';
        SetText(d, bt ? IDC_BTMAC : IDC_LANMAC, nm.c_str());
    }

    // 250ms 마다: 진행 상황 표/상태 문구/버튼 갱신
    static void Refresh(HWND d, Ctx* c, bool force) {
        std::shared_ptr<ScanView> v = c->eng->View();
        if (!v) return;
        int n, done, total;
        unsigned ver;
        std::string s;
        {
            std::lock_guard<std::mutex> g(v->mu);
            n = v->count();
            ver = v->ver;
            s = v->phase;
        }
        done = v->done;
        total = v->total;
        if (force || v != c->view || ver != c->ver) {
            c->view = v;
            c->ver = ver;
            SendDlgItemMessageW(d, IDC_LIST, LVM_SETITEMCOUNT, n, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
            InvalidateRect(GetDlgItem(d, IDC_LIST), NULL, FALSE);
        }
        if (total > 0) s += "  (" + std::to_string(done) + " / " + std::to_string(total) + ")";
        if (s != c->status) {
            c->status = s;
            SetText(d, IDC_STATUS, s.c_str());
        }
        HWND off = GetDlgItem(d, IDC_OFF);                                  // scan Off 상태에서는 Off 버튼 비활성
        if (!c->eng->IsOn() && GetFocus() == off) SendMessageW(d, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(d, IDC_RESTART), TRUE);
        EnableWindow(off, c->eng->IsOn());
    }

    // 입력값 검사 -> Settings 에 반영 -> ini 저장 -> 실행 중인 감시에 전달. 잘못된 값이면 안내하고 false
    static bool Apply(HWND d, Ctx* c) {
        char bt[64], mac[64];
        GetText(d, IDC_BTMAC, bt, sizeof(bt));
        GetText(d, IDC_LANMAC, mac, sizeof(mac));
        std::string nb = bt[0] ? StrUtil::normMac(bt) : "", nm = mac[0] ? StrUtil::normMac(mac) : "";
        if (bt[0] && nb.empty()) { Warn(d, "블루투스 MAC 주소 형식이 올바르지 않습니다.\n예: AA:BB:CC:DD:EE:FF", IDC_BTMAC); return false; }
        if (mac[0] && nm.empty()) { Warn(d, "LAN 카드 MAC 주소 형식이 올바르지 않습니다.\n예: AA:BB:CC:DD:EE:FF", IDC_LANMAC); return false; }
        BOOL ok = FALSE;
        UINT sec = GetDlgItemInt(d, IDC_SEC, &ok, FALSE);
        if (!ok || sec < 5) { Warn(d, "scan 주기는 5 이상의 숫자(초)로 입력하세요.", IDC_SEC); return false; }

        Settings& s = *c->cfg;
        for (char& ch : nb) if (ch == '-') ch = ':';                        // ini 에는 AA:BB:CC:DD:EE:FF 형식으로
        for (char& ch : nm) if (ch == '-') ch = ':';
        lstrcpynA(s.homeBt, nb.c_str(), sizeof(s.homeBt));
        lstrcpynA(s.homeMac, nm.c_str(), sizeof(s.homeMac));
        s.scanSec = (int)sec;
        s.scanWifi = IsDlgButtonChecked(d, IDC_CHKWIFI) == BST_CHECKED;
        s.scanBt = IsDlgButtonChecked(d, IDC_CHKBT) == BST_CHECKED;
        s.SaveScan();
        c->eng->SetConfig(ScanEngine::FromSettings(s));
        Log::Put("WiFiScan 설정 저장: 블루투스=%s LAN=%s 주기=%d초 Wi-Fi스캔=%d 블루투스스캔=%d",
                 s.homeBt[0] ? s.homeBt : "(없음)", s.homeMac[0] ? s.homeMac : "(없음)", s.scanSec, (int)s.scanWifi, (int)s.scanBt);
        return true;
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
        switch (msg) {
        case WM_INITDIALOG: {
            c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            const Settings& s = *c->cfg;
            SetText(d, IDC_BTMAC, s.homeBt);
            SetText(d, IDC_LANMAC, s.homeMac);
            char sec[16];
            wsprintfA(sec, "%d", s.scanSec);
            SetText(d, IDC_SEC, sec);
            EnableWindow(GetDlgItem(d, IDC_BTMAC), FALSE);                                    // MAC 칸은 직접 입력/수정 불가 (목록의 체크박스로만 지정)
            EnableWindow(GetDlgItem(d, IDC_LANMAC), FALSE);
            if (c->notice) SetText(d, IDC_NOTICE, c->notice);
            else {                                                                            // WifiScan모니터링: 모든 체크박스 비활성 (내 폰 등록에서만 목록 체크박스 사용)
                EnableWindow(GetDlgItem(d, IDC_CHKWIFI), FALSE);
                EnableWindow(GetDlgItem(d, IDC_CHKBT), FALSE);
            }
            SendDlgItemMessageW(d, IDC_BTMAC, EM_LIMITTEXT, 30, 0);
            SendDlgItemMessageW(d, IDC_LANMAC, EM_LIMITTEXT, 30, 0);
            SendDlgItemMessageW(d, IDC_SEC, EM_LIMITTEXT, 5, 0);
            CheckDlgButton(d, IDC_CHKWIFI, s.scanWifi ? BST_CHECKED : BST_UNCHECKED);        // 기본값은 ini
            CheckDlgButton(d, IDC_CHKBT, s.scanBt ? BST_CHECKED : BST_UNCHECKED);

            HWND hl = GetDlgItem(d, IDC_LIST);                                                // 진행 상황 표
            SendMessageW(hl, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            SendMessageW(hl, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)MakeDots());              // 목록이 닫힐 때 함께 해제됨
            SendMessageW(hl, LVM_SETIMAGELIST, LVSIL_STATE, (LPARAM)MakeChecks());            // 행 맨 앞 체크박스
            SendMessageW(hl, LVM_SETCALLBACKMASK, LVIS_STATEIMAGEMASK, 0);
            RECT rc;
            GetClientRect(hl, &rc);
            int tw = rc.right - GetSystemMetrics(SM_CXVSCROLL);
            // 0번 칸은 체크박스 + 상태 점 전용(글자 없음), 1번 칸부터 IP Address ...
            static const char* const H[6] = {"", "IP Address", "Computer Name", "MAC Address", "Network Adapter Company", "User Text"};
            static const int P[6] = {0, 16, 22, 26, 22, 14};
            const int w0 = 56;
            for (int i = 0; i < 6; i++) {
                char16_t u[40];
                Utf8::ToUtf16(H[i], -1, u, 40);
                LVCOLUMNW col = {};
                col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
                col.fmt = LVCFMT_LEFT;
                col.iSubItem = i;
                col.cx = i == 0 ? w0 : (tw - w0) * P[i] / 100;
                col.pszText = (LPWSTR)u;
                SendMessageW(hl, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
            }
            c->btNorm = s.homeBt[0] ? StrUtil::normMac(s.homeBt) : "";
            c->lanNorm = s.homeMac[0] ? StrUtil::normMac(s.homeMac) : "";
            Refresh(d, c, true);
            SetTimer(d, 1, 250, NULL);
            return TRUE;
        }
        case WM_TIMER:
            if (c) Refresh(d, c, false);
            return TRUE;
        case WM_CTLCOLORSTATIC:                                                           // 안내 문구는 빨간 글씨
            if (GetDlgCtrlID((HWND)l) == IDC_NOTICE) {
                SetTextColor((HDC)w, RGB(200, 0, 0));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
            }
            break;
        case WM_NOTIFY: {
            NMHDR* h = (NMHDR*)l;
            if (c && c->view && h->idFrom == IDC_LIST && h->code == LVN_GETDISPINFOW) {     // 목록 칸 내용 요청 (가상 목록)
                NMLVDISPINFOW* di = (NMLVDISPINFOW*)l;
                static char16_t buf[300];
                buf[0] = 0;
                std::lock_guard<std::mutex> g(c->view->mu);
                if (di->item.iItem >= 0 && di->item.iItem < c->view->count()) {
                    const ScanRow& r = c->view->at(di->item.iItem);
                    if (di->item.mask & LVIF_IMAGE) di->item.iImage = r.st;
                    if (di->item.mask & LVIF_STATE) {                                            // 체크박스
                        di->item.stateMask |= LVIS_STATEIMAGEMASK;
                        di->item.state = (di->item.state & ~(UINT)LVIS_STATEIMAGEMASK) |
                                         INDEXTOSTATEIMAGEMASK(CheckState(c, c->view->isBt(di->item.iItem), r.mac));
                    }
                    if (di->item.mask & LVIF_TEXT) {
                        const std::string* t = NULL;
                        switch (di->item.iSubItem) {
                        case 1: t = &r.c0; break;
                        case 2: t = &r.name; break;
                        case 3: t = &r.mac; break;
                        case 4: t = &r.vendor; break;
                        case 5: t = &r.user; break;
                        }
                        if (!t || !Utf8::ToUtf16(t->c_str(), -1, buf, 300)) buf[0] = 0;
                        di->item.pszText = (LPWSTR)buf;
                    }
                }
                return TRUE;
            }
            if (c && h->idFrom == IDC_LIST && h->code == NM_CLICK) {                         // 체크박스 클릭
                NMITEMACTIVATE* ia = (NMITEMACTIVATE*)l;
                LVHITTESTINFO hi = {};
                hi.pt = ia->ptAction;
                SendMessageW(h->hwndFrom, LVM_HITTEST, 0, (LPARAM)&hi);
                if (hi.iItem >= 0 && (hi.flags & LVHT_ONITEMSTATEICON)) ToggleRow(d, c, hi.iItem);
                return TRUE;
            }
            if (c && h->idFrom == IDC_LIST && h->code == LVN_KEYDOWN) {                      // 스페이스 = 체크 토글
                if (((NMLVKEYDOWN*)l)->wVKey == VK_SPACE)
                    ToggleRow(d, c, (int)SendMessageW(h->hwndFrom, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED));
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (c && HIWORD(w) == EN_CHANGE && (LOWORD(w) == IDC_BTMAC || LOWORD(w) == IDC_LANMAC)) {   // MAC 입력 칸이 바뀌면 체크 표시 갱신
                char t[64];
                GetText(d, LOWORD(w), t, sizeof(t));
                (LOWORD(w) == IDC_BTMAC ? c->btNorm : c->lanNorm) = t[0] ? StrUtil::normMac(t) : "";
                InvalidateRect(GetDlgItem(d, IDC_LIST), NULL, FALSE);
                return TRUE;
            }
            if (LOWORD(w) == IDOK) {                                  // 저장
                if (c && Apply(d, c)) { Log::Put("WiFiScan 대화상자: 저장"); EndDialog(d, 1); }
                return TRUE;
            }
            if (LOWORD(w) == IDC_RESTART) {                           // scan 재시작 (입력값 저장 + 처음부터 다시)
                if (c && Apply(d, c)) {
                    Log::Put("WiFiScan 대화상자: scan 재시작");
                    c->eng->Start("대화상자 재시작");
                    Refresh(d, c, true);
                }
                return TRUE;
            }
            if (LOWORD(w) == IDC_OFF) {                               // scan Off
                if (c) {
                    Log::Put("WiFiScan 대화상자: scan Off");
                    c->eng->Stop("대화상자");
                    Refresh(d, c, true);
                }
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {                              // 취소 (저장하지 않음)
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            KillTimer(d, 1);
            break;
        }
        return FALSE;
    }
};


// ======================================================================
// IP cam (ipcam.cpp 통합): 안드로이드 폰 카메라 앱(DroidCam, IP Webcam 등)의 MJPEG 영상을 별도 창에 실시간 표시.
// 폰 앱 서버(IP:port)의 GET /video 를 Winsock 으로 받아 JPEG 프레임을 잘라 GDI+ 로 디코딩한다 (수신은 별도 스레드).
// 클래스 이름이 scan 쪽(Logger 등)과 겹치지 않도록 namespace ipcam 안에 둔다. 로그 파일은 예전 그대로 exe 폴더의 ipcam.log.
// ======================================================================
namespace ipcam {

// ============================================================ CritSec
class CritSec {
public:
    CritSec()  { InitializeCriticalSection(&cs_); }
    ~CritSec() { DeleteCriticalSection(&cs_); }
    CritSec(const CritSec&) = delete;
    CritSec& operator=(const CritSec&) = delete;
    void Enter() { EnterCriticalSection(&cs_); }
    void Leave() { LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
};

class ScopedLock {
public:
    explicit ScopedLock(CritSec& c) : c_(c) { c_.Enter(); }
    ~ScopedLock() { c_.Leave(); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
private:
    CritSec& c_;
};

// ============================================================ Logger (ipcam.log)
class Logger {
public:
    static Logger& Instance() { static Logger inst; return inst; }

    bool Open(const std::string& path) {
        ScopedLock lk(cs_);
        if (fp_) { fclose(fp_); fp_ = nullptr; }
        fp_ = fopen(path.c_str(), "w");                 // 실행할 때마다 새로 작성 (예전 ipcam 과 같음)
        return fp_ != nullptr;
    }
    void Close() {
        ScopedLock lk(cs_);
        if (fp_) { fclose(fp_); fp_ = nullptr; }
    }
    void Write(const char* fmt, ...) {
        char msg[1024];
        va_list ap; va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        SYSTEMTIME t; GetLocalTime(&t);
        ScopedLock lk(cs_);
        if (!fp_) return;
        fprintf(fp_, "%04d-%02d-%02d %02d:%02d:%02d.%03d [T%lu] %s\n", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), msg);
        fflush(fp_);
    }
    static std::string Printable(const std::string& s, size_t maxLen) {
        std::string o;
        for (size_t i = 0; i < s.size() && i < maxLen; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == '\r') o += "\\r";
            else if (c == '\n') o += "\\n";
            else if (c >= 32 && c < 127) o += (char)c;
            else { char b[8]; snprintf(b, sizeof b, "\\x%02X", c); o += b; }
        }
        return o;
    }
private:
    Logger() = default;
    FILE* fp_ = nullptr;
    CritSec cs_;
};
#define IPLOG(...) ::ipcam::Logger::Instance().Write(__VA_ARGS__)

// ============================================================ Config
struct Config {
    std::string host = "192.168.0.70";
    std::string port = "4747";
    std::string path = "/video";
};

// ============================================================ RAII sessions
class WinsockSession {
public:
    WinsockSession() {
        WSADATA wd;
        rc_ = WSAStartup(MAKEWORD(2, 2), &wd);
        IPLOG("WSAStartup -> %d (version %d.%d)", rc_, LOBYTE(wd.wVersion), HIBYTE(wd.wVersion));
    }
    ~WinsockSession() { if (rc_ == 0) WSACleanup(); }
    bool Ok() const { return rc_ == 0; }
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;
private:
    int rc_ = -1;
};

class GdiplusSession {
public:
    GdiplusSession() {
        Gdiplus::GdiplusStartupInput in;
        st_ = Gdiplus::GdiplusStartup(&tok_, &in, nullptr);
        IPLOG("GdiplusStartup -> status %d", (int)st_);
    }
    ~GdiplusSession() { if (st_ == Gdiplus::Ok) Gdiplus::GdiplusShutdown(tok_); }
    bool Ok() const { return st_ == Gdiplus::Ok; }
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;
private:
    ULONG_PTR tok_ = 0;
    Gdiplus::Status st_ = Gdiplus::GenericError;
};

// ============================================================ JpegDecoder
class JpegDecoder {
public:
    // Returns a self-contained 32bppPARGB bitmap (caller owns), or nullptr on failure.
    static Gdiplus::Bitmap* Decode(const char* data, size_t n) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
        if (!h) { IPLOG("Decode: GlobalAlloc(%llu) failed err=%lu", (unsigned long long)n, GetLastError()); return nullptr; }
        memcpy(GlobalLock(h), data, n);
        GlobalUnlock(h);
        IStream* s = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(h, TRUE, &s);
        if (hr != S_OK) { IPLOG("Decode: CreateStreamOnHGlobal hr=0x%08lX", (unsigned long)hr); GlobalFree(h); return nullptr; }
        Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromStream(s);
        Gdiplus::Bitmap* out = nullptr;
        if (!src) IPLOG("Decode: FromStream returned null (size=%llu)", (unsigned long long)n);
        else if (src->GetLastStatus() != Gdiplus::Ok) IPLOG("Decode: GDI+ status=%d (size=%llu)", (int)src->GetLastStatus(), (unsigned long long)n);
        else out = src->Clone(0, 0, src->GetWidth(), src->GetHeight(), PixelFormat32bppPARGB);
        delete src;
        s->Release();
        return out;
    }
};

// ============================================================ FrameStore
class FrameStore {
public:
    FrameStore() = default;
    ~FrameStore() { delete bmp_; }
    FrameStore(const FrameStore&) = delete;
    FrameStore& operator=(const FrameStore&) = delete;

    // Takes ownership of bmp. Returns total frame count so far.
    LONG Publish(Gdiplus::Bitmap* bmp) {
        {
            ScopedLock lk(cs_);
            delete bmp_;
            bmp_ = bmp;
        }
        InterlockedIncrement(&frames_);
        return InterlockedIncrement(&total_);
    }
    void SetStatus(const std::string& s) { ScopedLock lk(cs_); status_ = s; }
    // 현재 프레임을 지우고 안내 문구만 보이게 함 (카메라를 바꾸거나 종료할 때)
    void Clear(const std::string& s) {
        { ScopedLock lk(cs_); delete bmp_; bmp_ = nullptr; status_ = s; }
        InterlockedExchange(&frames_, 0);
        InterlockedExchange(&total_, 0);
    }
    int  TakeFps()  { return (int)InterlockedExchange(&frames_, 0); }
    LONG Total() const { return total_; }

    // 현재 프레임의 복사본 (사진 저장용, 호출한 쪽이 delete). 없으면 nullptr
    Gdiplus::Bitmap* CopyFrame() {
        ScopedLock lk(cs_);
        return bmp_ ? bmp_->Clone(0, 0, bmp_->GetWidth(), bmp_->GetHeight(), PixelFormat32bppPARGB) : nullptr;
    }

    // Draws the latest frame (aspect-fit, letterboxed, rot = 시계 방향 90도 단위 회전) or the status text.
    void Paint(HDC dc, const RECT& rc, int rot = 0) {
        ScopedLock lk(cs_);
        int W = rc.right - rc.left, H = rc.bottom - rc.top;
        if (bmp_) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
            double bw = bmp_->GetWidth(), bh = bmp_->GetHeight();
            const double rw = (rot & 1) ? bh : bw, rh = (rot & 1) ? bw : bh;          // 회전 후 크기
            double sc = std::min(W / rw, H / rh);
            double dw = bw * sc, dh = bh * sc;
            g.TranslateTransform((Gdiplus::REAL)(rc.left + W / 2.0), (Gdiplus::REAL)(rc.top + H / 2.0));
            g.RotateTransform((Gdiplus::REAL)(90 * (rot & 3)));
            g.DrawImage(bmp_, (Gdiplus::REAL)(-dw / 2), (Gdiplus::REAL)(-dh / 2), (Gdiplus::REAL)dw, (Gdiplus::REAL)dh);
        } else {
            RECT r = rc;
            SetTextColor(dc, RGB(200, 200, 200));
            SetBkMode(dc, TRANSPARENT);
            char16_t u[300];                                                    // 안내 문구는 UTF-8 (한글 가능)
            if (Utf8::ToUtf16(status_.c_str(), -1, u, 300)) {
                HGDIOBJ of = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(dc, (LPCWSTR)u, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, of);
            }
        }
    }
private:
    CritSec cs_;
    Gdiplus::Bitmap* bmp_ = nullptr;
    std::string status_ = "Connecting...";
    volatile LONG frames_ = 0;
    volatile LONG total_ = 0;
};

// ============================================================ MjpegClient
class MjpegClient {
public:
    using FrameCallback = std::function<void()>;
    using JpegSink = std::function<void(const char*, size_t, Gdiplus::Bitmap*)>;     // 프레임 원본 JPEG + 디코딩된 비트맵 (녹화용)

    MjpegClient(const Config& cfg, FrameStore& store, FrameCallback onFrame, JpegSink sink = nullptr)
        : cfg_(cfg), store_(store), onFrame_(std::move(onFrame)), sink_(std::move(sink)) {
        stopEvt_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }
    ~MjpegClient() { Stop(); if (stopEvt_) CloseHandle(stopEvt_); }
    MjpegClient(const MjpegClient&) = delete;
    MjpegClient& operator=(const MjpegClient&) = delete;

    void Start() {
        if (thread_) return;
        ResetEvent(stopEvt_);
        thread_ = CreateThread(nullptr, 0, &MjpegClient::ThreadProc, this, 0, nullptr);
        if (!thread_) IPLOG("CreateThread failed err=%lu", GetLastError());
    }
    void Stop() {
        if (!thread_) return;
        SetEvent(stopEvt_);
        {   // unblock a pending recv()
            ScopedLock lk(sockCs_);
            if (sock_ != INVALID_SOCKET) shutdown(sock_, SD_BOTH);
        }
        if (WaitForSingleObject(thread_, 3000) == WAIT_TIMEOUT) IPLOG("NetThread did not stop within 3s");
        CloseHandle(thread_);
        thread_ = nullptr;
    }

private:
    static DWORD WINAPI ThreadProc(LPVOID p) { static_cast<MjpegClient*>(p)->Run(); return 0; }

    bool Stopping() const { return WaitForSingleObject(stopEvt_, 0) == WAIT_OBJECT_0; }
    // Interruptible sleep; returns true if stop was requested meanwhile.
    bool SleepOrStop(DWORD ms) const { return WaitForSingleObject(stopEvt_, ms) == WAIT_OBJECT_0; }

    void Run() {
        IPLOG("NetThread started");
        while (!Stopping()) {
            SOCKET s = ConnectSocket();
            if (s == INVALID_SOCKET) {
                store_.SetStatus("Connect failed - retrying...");
                if (SleepOrStop(2000)) break;
                continue;
            }
            { ScopedLock lk(sockCs_); sock_ = s; }
            bool httpError = Stream(s);
            { ScopedLock lk(sockCs_); sock_ = INVALID_SOCKET; }
            closesocket(s);
            if (Stopping()) break;
            if (!httpError) store_.SetStatus("Disconnected - retrying...");
            if (SleepOrStop(2000)) break;
        }
        IPLOG("NetThread exit");
    }

    SOCKET ConnectSocket() {
        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int gr = getaddrinfo(cfg_.host.c_str(), cfg_.port.c_str(), &hints, &res);
        if (gr != 0) { IPLOG("getaddrinfo(%s:%s) failed: %d", cfg_.host.c_str(), cfg_.port.c_str(), gr); return INVALID_SOCKET; }
        SOCKET s = INVALID_SOCKET;
        for (addrinfo* p = res; p; p = p->ai_next) {
            char ip[64] = "?";
            getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, ip, sizeof ip, nullptr, 0, NI_NUMERICHOST);
            s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s == INVALID_SOCKET) { IPLOG("socket() failed wsaerr=%d", WSAGetLastError()); continue; }
            DWORD to = 5000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
            IPLOG("connecting to %s:%s ...", ip, cfg_.port.c_str());
            // 비차단 connect: 최대 5초, 100ms 마다 종료 요청 확인 (잘못된 IP 에서도 [종료] 가 바로 먹도록)
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);
            bool ok = (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0);
            if (!ok && WSAGetLastError() == WSAEWOULDBLOCK) {
                for (int i = 0; i < 50 && !Stopping(); i++) {
                    fd_set wf, ef;
                    FD_ZERO(&wf); FD_ZERO(&ef);
                    FD_SET(s, &wf); FD_SET(s, &ef);
                    timeval tv = {0, 100000};
                    int sr = select(0, nullptr, &wf, &ef, &tv);
                    if (sr > 0) { ok = FD_ISSET(s, &wf) != 0; break; }
                    if (sr < 0) break;
                }
            }
            nb = 0;
            ioctlsocket(s, FIONBIO, &nb);
            if (ok) { IPLOG("connected"); break; }
            IPLOG("connect() failed wsaerr=%d", WSAGetLastError());
            closesocket(s);
            s = INVALID_SOCKET;
        }
        freeaddrinfo(res);
        return s;
    }

    // Reads the stream until it ends. Returns true if the server answered with a non-200 status.
    bool Stream(SOCKET s) {
        std::string req = "GET " + cfg_.path + " HTTP/1.0\r\nHost: " + cfg_.host + "\r\nConnection: close\r\n\r\n";
        int sr = send(s, req.c_str(), (int)req.size(), 0);
        IPLOG("send request (%d bytes) -> %d", (int)req.size(), sr);

        std::string buf;
        bool headChecked = false;
        unsigned long long totalRecv = 0;
        static thread_local char tmp[65536];
        while (!Stopping()) {
            int r = recv(s, tmp, sizeof tmp, 0);
            if (r <= 0) {
                IPLOG("recv returned %d wsaerr=%d (total %llu bytes)", r, r < 0 ? WSAGetLastError() : 0, totalRecv);
                return false;
            }
            totalRecv += (unsigned)r;
            buf.append(tmp, r);
            if (!headChecked && buf.size() >= 12) {
                headChecked = true;
                IPLOG("response head: %s", Logger::Printable(buf, 300).c_str());
                if (buf.compare(0, 5, "HTTP/") == 0 && buf.find(" 200") != 8) {
                    std::string line = buf.substr(0, buf.find("\r\n"));
                    store_.SetStatus("HTTP error: " + line);
                    IPLOG("non-200 response -> abort: %s", line.c_str());
                    return true;
                }
            }
            ExtractFrames(buf);
        }
        return false;
    }

    // Pulls every complete JPEG (SOI..EOI) out of buf and publishes it.
    void ExtractFrames(std::string& buf) {
        for (;;) {
            size_t a = buf.find("\xFF\xD8");
            if (a == std::string::npos) { if (buf.size() > 4) buf.erase(0, buf.size() - 1); return; }
            size_t e = buf.find("\xFF\xD9", a + 2);
            if (e == std::string::npos) {
                if (a > 0) buf.erase(0, a);
                if (buf.size() > (16u << 20)) { IPLOG("buffer > 16MB without EOI, cleared"); buf.clear(); }
                return;
            }
            Gdiplus::Bitmap* bmp = JpegDecoder::Decode(buf.data() + a, e + 2 - a);
            if (bmp && sink_) sink_(buf.data() + a, e + 2 - a, bmp);
            buf.erase(0, e + 2);
            if (!bmp) continue;
            UINT w = bmp->GetWidth(), h = bmp->GetHeight();
            LONG tot = store_.Publish(bmp);
            if (tot == 1) IPLOG("first frame decoded: %ux%u", w, h);
            else if (tot % 300 == 0) IPLOG("frames so far: %ld", tot);
            if (onFrame_) onFrame_();
        }
    }

    Config cfg_;
    FrameStore& store_;
    FrameCallback onFrame_;
    JpegSink sink_;
    HANDLE thread_ = nullptr;
    HANDLE stopEvt_ = nullptr;
    CritSec sockCs_;
    SOCKET sock_ = INVALID_SOCKET;
};

// ============================================================ WebcamClient
// PC 에 연결된 웹캠(WebCam1 = 장치 #0, WebCam2 = 장치 #1, Media Foundation 열거 순서)을 수신 스레드에서 읽어 FrameStore 에 올린다.
// Telegram 의 CameraCapture 와 같은 방식(MF 함수는 실행 중에 동적 로딩 -> N/KN 에디션에서도 앱은 시작됨)이지만 독립 객체이다.
// 침입 탐지(/detectOn) 등 다른 곳이 카메라를 쓰는 중이면 열기에 실패하므로 2초마다 다시 시도한다.
class WebcamClient {
public:
    using FrameCallback = std::function<void()>;
    using BitmapSink = std::function<void(Gdiplus::Bitmap*)>;       // 녹화용: 디코딩된 프레임 (Publish 전에 호출)

    WebcamClient(int index, FrameStore& store, FrameCallback onFrame, BitmapSink sink = nullptr)
        : index_(index), store_(store), onFrame_(std::move(onFrame)), sink_(std::move(sink)) {
        stopEvt_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }
    ~WebcamClient() { Stop(); if (stopEvt_) CloseHandle(stopEvt_); }
    WebcamClient(const WebcamClient&) = delete;
    WebcamClient& operator=(const WebcamClient&) = delete;

    void Start() {
        if (thread_) return;
        ResetEvent(stopEvt_);
        thread_ = CreateThread(nullptr, 0, &WebcamClient::ThreadProc, this, 0, nullptr);
        if (!thread_) IPLOG("CreateThread failed err=%lu", GetLastError());
    }
    void Stop() {
        if (!thread_) return;
        SetEvent(stopEvt_);
        if (WaitForSingleObject(thread_, 5000) == WAIT_TIMEOUT) IPLOG("WebcamThread did not stop within 5s");
        CloseHandle(thread_);
        thread_ = nullptr;
    }

private:
    typedef HRESULT(WINAPI* PFN_MFStartup)(ULONG, DWORD);
    typedef HRESULT(WINAPI* PFN_MFShutdown)(void);
    typedef HRESULT(WINAPI* PFN_MFCreateAttributes)(IMFAttributes**, UINT32);
    typedef HRESULT(WINAPI* PFN_MFEnumDeviceSources)(IMFAttributes*, IMFActivate***, UINT32*);
    typedef HRESULT(WINAPI* PFN_MFCreateMediaType)(IMFMediaType**);
    typedef HRESULT(WINAPI* PFN_MFCreateSourceReaderFromMediaSource)(IMFMediaSource*, IMFAttributes*, IMFSourceReader**);

    static DWORD WINAPI ThreadProc(LPVOID p) { static_cast<WebcamClient*>(p)->Run(); return 0; }

    bool Stopping() const { return WaitForSingleObject(stopEvt_, 0) == WAIT_OBJECT_0; }
    bool SleepOrStop(DWORD ms) const { return WaitForSingleObject(stopEvt_, ms) == WAIT_OBJECT_0; }

    void Run() {
        IPLOG("WebcamThread started (device #%d)", index_);
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        if (LoadApi() && SUCCEEDED(pMFStartup_(MF_VERSION, 0))) {
            while (!Stopping()) {
                std::string err;
                if (!OpenReader(err)) {
                    store_.SetStatus(err + " - retrying...");
                    IPLOG("open failed: %s", err.c_str());
                    if (SleepOrStop(2000)) break;
                    continue;
                }
                ReadLoop();
                CloseReader();
                if (Stopping()) break;
                store_.SetStatus("Camera disconnected - retrying...");
                if (SleepOrStop(2000)) break;
            }
            CloseReader();
            pMFShutdown_();
        } else {
            store_.SetStatus("Media Foundation is not available");
            IPLOG("Media Foundation load/startup failed");
        }
        if (com) CoUninitialize();
        IPLOG("WebcamThread exit");
    }

    bool LoadApi() {
        HMODULE hPlat = LoadLibraryW(L"mfplat.dll");
        HMODULE hMf = LoadLibraryW(L"mf.dll");
        HMODULE hRw = LoadLibraryW(L"mfreadwrite.dll");
        if (!hPlat || !hMf || !hRw) return false;
        pMFStartup_ = (PFN_MFStartup)GetProcAddress(hPlat, "MFStartup");
        pMFShutdown_ = (PFN_MFShutdown)GetProcAddress(hPlat, "MFShutdown");
        pMFCreateAttributes_ = (PFN_MFCreateAttributes)GetProcAddress(hPlat, "MFCreateAttributes");
        pMFCreateMediaType_ = (PFN_MFCreateMediaType)GetProcAddress(hPlat, "MFCreateMediaType");
        pMFEnumDeviceSources_ = (PFN_MFEnumDeviceSources)GetProcAddress(hMf, "MFEnumDeviceSources");
        pMFCreateReader_ = (PFN_MFCreateSourceReaderFromMediaSource)GetProcAddress(hRw, "MFCreateSourceReaderFromMediaSource");
        return pMFStartup_ && pMFShutdown_ && pMFCreateAttributes_ && pMFCreateMediaType_ && pMFEnumDeviceSources_ && pMFCreateReader_;
    }

    void CloseReader() {
        if (reader_) { reader_->Release(); reader_ = nullptr; }
        if (source_) { source_->Shutdown(); source_->Release(); source_ = nullptr; }
    }

    // index_ 번째 웹캠을 열어 reader_ 를 RGB32 출력으로 준비한다. 실패하면 err 에 사유 (열린 것은 모두 정리).
    bool OpenReader(std::string& err) {
        IMFAttributes* pAttr = nullptr;
        IMFActivate** devs = nullptr;
        UINT32 count = 0;
        IMFAttributes* pRdAttr = nullptr;
        IMFMediaType* pType = nullptr;

        HRESULT hr = pMFCreateAttributes_(&pAttr, 1);
        if (SUCCEEDED(hr)) hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) hr = pMFEnumDeviceSources_(pAttr, &devs, &count);
        if (SUCCEEDED(hr) && (UINT32)index_ >= count) {
            char b[64];
            snprintf(b, sizeof b, "WebCam%d not found (%u camera%s)", index_ + 1, (unsigned)count, count == 1 ? "" : "s");
            err = b;
            hr = E_FAIL;
        }
        if (SUCCEEDED(hr)) {
            WCHAR* nm = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(devs[index_]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nm, &len)) && nm) {
                char u8[256];
                WideCharToMultiByte(CP_UTF8, 0, nm, -1, u8, sizeof u8, nullptr, nullptr);
                IPLOG("device #%d: %s (%u found)", index_, u8, (unsigned)count);
                CoTaskMemFree(nm);
            }
            hr = devs[index_]->ActivateObject(IID_PPV_ARGS(&source_));
        }
        if (SUCCEEDED(hr)) hr = pMFCreateAttributes_(&pRdAttr, 1);
        if (SUCCEEDED(hr)) hr = pRdAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // MJPEG/YUY2/NV12 -> RGB32 자동 변환
        if (SUCCEEDED(hr)) hr = pMFCreateReader_(source_, pRdAttr, &reader_);
        if (SUCCEEDED(hr)) hr = pMFCreateMediaType_(&pType);
        if (SUCCEEDED(hr)) hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);

        w_ = h_ = 0;
        bottomUp_ = true;                                            // MF RGB 규약: stride 가 음수이면 bottom-up (속성 없을 때 기본)
        if (SUCCEEDED(hr)) {
            IMFMediaType* pCur = nullptr;
            hr = reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCur);
            if (SUCCEEDED(hr)) {
                MFGetAttributeSize(pCur, MF_MT_FRAME_SIZE, &w_, &h_);
                UINT32 ds = 0;
                if (SUCCEEDED(pCur->GetUINT32(MF_MT_DEFAULT_STRIDE, &ds))) bottomUp_ = ((LONG)ds < 0);
                pCur->Release();
            }
        }
        if (SUCCEEDED(hr) && (w_ == 0 || h_ == 0)) { err = "Camera frame size unknown"; hr = E_FAIL; }
        if (FAILED(hr) && err.empty()) {
            char b[96];
            snprintf(b, sizeof b, "Camera open failed (0x%08lX, in use by another app?)", (unsigned long)hr);
            err = b;
        }

        if (pType) pType->Release();
        if (pRdAttr) pRdAttr->Release();
        if (devs) {
            for (UINT32 i = 0; i < count; ++i) devs[i]->Release();
            CoTaskMemFree(devs);
        }
        if (pAttr) pAttr->Release();
        if (FAILED(hr)) { CloseReader(); return false; }
        IPLOG("camera opened: %ux%u bottomUp=%d", (unsigned)w_, (unsigned)h_, (int)bottomUp_);
        return true;
    }

    // 프레임을 계속 읽어 올린다. 끊기거나 오류가 나면 반환 (바깥 루프가 다시 연다)
    void ReadLoop() {
        while (!Stopping()) {
            DWORD flags = 0;
            IMFSample* smp = nullptr;
            HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &smp);
            if (FAILED(hr)) { IPLOG("ReadSample failed hr=0x%08lX", (unsigned long)hr); return; }
            if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) {
                if (smp) smp->Release();
                IPLOG("ReadSample flags=0x%lX -> reopen", (unsigned long)flags);
                return;
            }
            if (!smp) continue;
            Gdiplus::Bitmap* bmp = ToBitmap(smp);
            smp->Release();
            if (!bmp) continue;
            if (sink_) sink_(bmp);
            LONG tot = store_.Publish(bmp);
            if (tot == 1) IPLOG("first frame: %ux%u", (unsigned)w_, (unsigned)h_);
            else if (tot % 300 == 0) IPLOG("frames so far: %ld", tot);
            if (onFrame_) onFrame_();
        }
    }

    // MF RGB32(BGRX) 샘플 -> 32bppPARGB 비트맵 (알파는 불투명으로 채움, bottom-up 이면 행을 뒤집어 복사)
    Gdiplus::Bitmap* ToBitmap(IMFSample* smp) {
        IMFMediaBuffer* buf = nullptr;
        if (FAILED(smp->ConvertToContiguousBuffer(&buf)) || !buf) return nullptr;
        Gdiplus::Bitmap* out = nullptr;
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
            LONG stride = (LONG)(curLen / h_);
            if (stride < (LONG)w_ * 4) stride = (LONG)w_ * 4;
            if ((size_t)stride * h_ <= curLen) {                     // 버퍼가 프레임보다 작으면 범위 밖 읽기 방지
                out = new Gdiplus::Bitmap((INT)w_, (INT)h_, PixelFormat32bppPARGB);
                Gdiplus::BitmapData bd;
                Gdiplus::Rect rc(0, 0, (INT)w_, (INT)h_);
                if (out->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
                    for (UINT y = 0; y < h_; y++) {
                        const BYTE* s = data + (size_t)(bottomUp_ ? (h_ - 1 - y) : y) * stride;
                        BYTE* d = (BYTE*)bd.Scan0 + (ptrdiff_t)y * bd.Stride;
                        memcpy(d, s, (size_t)w_ * 4);
                        for (UINT x = 3; x < w_ * 4; x += 4) d[x] = 0xFF;
                    }
                    out->UnlockBits(&bd);
                } else { delete out; out = nullptr; }
            }
            buf->Unlock();
        }
        buf->Release();
        return out;
    }

    int index_;
    FrameStore& store_;
    FrameCallback onFrame_;
    BitmapSink sink_;
    HANDLE thread_ = nullptr;
    HANDLE stopEvt_ = nullptr;
    IMFMediaSource* source_ = nullptr;
    IMFSourceReader* reader_ = nullptr;
    UINT32 w_ = 0, h_ = 0;
    bool bottomUp_ = true;
    PFN_MFStartup pMFStartup_ = nullptr;
    PFN_MFShutdown pMFShutdown_ = nullptr;
    PFN_MFCreateAttributes pMFCreateAttributes_ = nullptr;
    PFN_MFCreateMediaType pMFCreateMediaType_ = nullptr;
    PFN_MFEnumDeviceSources pMFEnumDeviceSources_ = nullptr;
    PFN_MFCreateSourceReaderFromMediaSource pMFCreateReader_ = nullptr;
};

// ============================================================ Jpeg 인코딩 / 회전 도우미
// 비트맵을 (시계 방향) 90도 단위로 돌려 JPEG 바이트로 만든다. rot: 0~3
static bool JpegClsid(CLSID* out) {
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (!sz) return false;
    std::vector<char> buf(sz);
    Gdiplus::ImageCodecInfo* e = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, sz, e);
    for (UINT i = 0; i < n; i++)
        if (wcscmp(e[i].MimeType, L"image/jpeg") == 0) { *out = e[i].Clsid; return true; }
    return false;
}

static bool EncodeJpeg(Gdiplus::Bitmap* src, int rot, std::string& out) {
    CLSID jpg;
    if (!src || !JpegClsid(&jpg)) return false;
    Gdiplus::Bitmap* c = src->Clone(0, 0, src->GetWidth(), src->GetHeight(), PixelFormat24bppRGB);
    if (!c) return false;
    if (rot == 1) c->RotateFlip(Gdiplus::Rotate90FlipNone);
    else if (rot == 2) c->RotateFlip(Gdiplus::Rotate180FlipNone);
    else if (rot == 3) c->RotateFlip(Gdiplus::Rotate270FlipNone);
    ULONG quality = 90;
    Gdiplus::EncoderParameters ep;
    ep.Count = 1;
    ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
    ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues = 1;
    ep.Parameter[0].Value = &quality;
    IStream* s = nullptr;
    bool ok = false;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &s) == S_OK) {
        if (c->Save(s, &jpg, &ep) == Gdiplus::Ok) {
            STATSTG st;
            HGLOBAL h = nullptr;
            if (s->Stat(&st, STATFLAG_NONAME) == S_OK && GetHGlobalFromStream(s, &h) == S_OK && h) {
                const void* p = GlobalLock(h);
                if (p) { out.assign((const char*)p, (size_t)st.cbSize.QuadPart); GlobalUnlock(h); ok = !out.empty(); }
            }
        }
        s->Release();
    }
    delete c;
    return ok;
}

// 저장 폴더: exe 와 같은 폴더의 IPCam\ (없으면 만듦)
static std::string SaveDir() {
    char exe[MAX_PATH] = "";
    std::string dir;
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
        dir = exe;
        size_t sl = dir.find_last_of("\\/");
        dir = sl == std::string::npos ? std::string() : dir.substr(0, sl + 1);
    }
    dir += "IPCam\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// 같은 이름의 파일이 이미 있으면 _1, _2 ... 를 붙여 덮어쓰지 않게 함 (영상 창이 여러 개일 때 같은 초에 저장해도 안전)
static std::string UniquePath(const std::string& path) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return path;
    const size_t dot = path.find_last_of('.');
    const std::string base = dot == std::string::npos ? path : path.substr(0, dot);
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
    for (int i = 1; i < 1000; i++) {
        std::string p = base + "_" + std::to_string(i) + ext;
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return p;
    }
    return path;
}

static std::string Stamp(const char* ext) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[64];
    snprintf(b, sizeof b, "ipcam_%04d%02d%02d_%02d%02d%02d%s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, ext);
    return b;
}

// ============================================================ Recorder
// 동영상 촬영: 받은 MJPEG 프레임을 그대로 AVI(MJPG) 파일에 담는다 (외부 라이브러리 없음). 회전 중이면 시작할 때의 회전을 적용해 다시 인코딩.
// 종료할 때 실제 평균 fps 를 헤더에 기록한다. 프레임은 수신 스레드에서(Add), 시작/종료는 UI 스레드에서 호출.
class Recorder {
public:
    Recorder() = default;
    ~Recorder() { std::string a, b; Stop(a, b); }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool Active() { ScopedLock lk(cs_); return fp_ != nullptr; }
    bool Full()   { ScopedLock lk(cs_); return full_; }
    int  Seconds() { ScopedLock lk(cs_); return fp_ ? (int)((GetTickCount() - startTick_) / 1000) : 0; }

    bool Start(int rot, std::string& path) {
        ScopedLock lk(cs_);
        if (fp_) return false;
        path = UniquePath(SaveDir() + Stamp(".avi"));
        fp_ = fopen(path.c_str(), "wb");
        if (!fp_) { IPLOG("record: cannot open %s", path.c_str()); return false; }
        path_ = path;
        rot_ = rot;
        frames_ = 0; headerDone_ = false; full_ = false; skipped_ = 0;
        idx_.clear();
        startTick_ = GetTickCount();
        IPLOG("record start: %s rot=%d", path.c_str(), rot);
        return true;
    }

    // 종료 + 파일 마무리. info = 사용자에게 보여 줄 결과. 반환: 저장된 프레임이 있으면 true
    bool Stop(std::string& path, std::string& info) {
        ScopedLock lk(cs_);
        if (!fp_) return false;
        path = path_;
        bool ok = false;
        if (frames_ > 0 && headerDone_) {
            Finish();
            ok = true;
            char b[128];
            snprintf(b, sizeof b, "%u 프레임, %.1f fps", (unsigned)frames_, fps_);
            info = b;
            IPLOG("record stop: %s (%u frames, %.2f fps, %u skipped)", path_.c_str(), (unsigned)frames_, fps_, (unsigned)skipped_);
        } else {
            info = "녹화된 프레임이 없어 파일을 만들지 않았습니다";
            IPLOG("record stop: no frames");
        }
        fclose(fp_);
        fp_ = nullptr;
        if (!ok) DeleteFileA(path_.c_str());
        return ok;
    }

    // 수신 스레드: 새 프레임 (jpg = 받은 JPEG 바이트, bmp = 디코딩된 비트맵)
    void Add(const char* jpg, size_t n, Gdiplus::Bitmap* bmp) {
        ScopedLock lk(cs_);
        if (!fp_ || full_ || !bmp) return;
        const int bw = (int)bmp->GetWidth(), bh = (int)bmp->GetHeight();
        const int w = (rot_ & 1) ? bh : bw, h = (rot_ & 1) ? bw : bh;
        std::string enc;
        const char* data = jpg;
        size_t len = n;
        if (rot_ != 0) {
            if (!EncodeJpeg(bmp, rot_, enc)) return;
            data = enc.data();
            len = enc.size();
        }
        if (!headerDone_) { w_ = w; h_ = h; WriteHeaders(); headerDone_ = true; }
        if (w != w_ || h != h_) { skipped_++; return; }            // 해상도가 바뀐 프레임은 건너뜀
        long pos = ftell(fp_);
        if (pos < 0 || pos > 1900000000L) { full_ = true; IPLOG("record: file size limit reached"); return; }
        FCC("00dc");
        W32((uint32_t)len);
        fwrite(data, 1, len, fp_);
        if (len & 1) fputc(0, fp_);
        idx_.push_back({(uint32_t)(pos - moviPos_), (uint32_t)len});
        if (frames_ == 0) firstTick_ = GetTickCount();
        lastTick_ = GetTickCount();
        frames_++;
        if (len > maxFrame_) maxFrame_ = (uint32_t)len;
    }

private:
    struct Idx { uint32_t off, size; };

    void W32(uint32_t v) { fwrite(&v, 4, 1, fp_); }
    void W16(uint16_t v) { fwrite(&v, 2, 1, fp_); }
    void FCC(const char* s) { fwrite(s, 1, 4, fp_); }
    void Patch32(long pos, uint32_t v) { long cur = ftell(fp_); fseek(fp_, pos, SEEK_SET); W32(v); fseek(fp_, cur, SEEK_SET); }

    void WriteHeaders() {
        FCC("RIFF"); W32(0); FCC("AVI ");                           // RIFF 크기는 마지막에 기록
        FCC("LIST"); W32(192); FCC("hdrl");
        FCC("avih"); W32(56);
        usecPos_ = ftell(fp_);  W32(66667);                         // dwMicroSecPerFrame (종료 때 실제 값으로)
        W32(0); W32(0); W32(0x10);                                  // MaxBytesPerSec, Padding, Flags(HASINDEX)
        totalPos_ = ftell(fp_); W32(0);                             // dwTotalFrames
        W32(0); W32(1); W32(1 << 20); W32((uint32_t)w_); W32((uint32_t)h_);
        W32(0); W32(0); W32(0); W32(0);
        FCC("LIST"); W32(116); FCC("strl");
        FCC("strh"); W32(56);
        FCC("vids"); FCC("MJPG"); W32(0); W16(0); W16(0); W32(0);
        scalePos_ = ftell(fp_); W32(1000); W32(15000);              // dwScale, dwRate (종료 때 실제 fps 로)
        W32(0);
        lengthPos_ = ftell(fp_); W32(0);                            // dwLength
        W32(1 << 20); W32(0xFFFFFFFFu); W32(0);
        W16(0); W16(0); W16((uint16_t)w_); W16((uint16_t)h_);
        FCC("strf"); W32(40);
        W32(40); W32((uint32_t)w_); W32((uint32_t)h_); W16(1); W16(24); FCC("MJPG");
        W32((uint32_t)(w_ * h_ * 3)); W32(0); W32(0); W32(0); W32(0);
        FCC("LIST"); moviSizePos_ = ftell(fp_); W32(0);
        moviPos_ = ftell(fp_); FCC("movi");                         // idx1 의 offset 은 이 'movi' 위치 기준
    }

    void Finish() {
        const long moviEnd = ftell(fp_);
        Patch32(moviSizePos_, (uint32_t)(moviEnd - (moviSizePos_ + 4)));
        FCC("idx1"); W32((uint32_t)idx_.size() * 16);
        for (const Idx& e : idx_) { FCC("00dc"); W32(0x10); W32(e.off); W32(e.size); }
        const long end = ftell(fp_);
        Patch32(4, (uint32_t)(end - 8));
        double secs = frames_ > 1 ? (lastTick_ - firstTick_) / 1000.0 : 0.0;       // 첫 프레임 ~ 마지막 프레임
        fps_ = secs > 0.2 ? (frames_ - 1) / secs : 15.0;
        if (fps_ < 0.5) fps_ = 0.5;
        if (fps_ > 120.0) fps_ = 120.0;
        Patch32(usecPos_, (uint32_t)(1000000.0 / fps_ + 0.5));
        Patch32(totalPos_, (uint32_t)frames_);
        Patch32(scalePos_, 1000);
        Patch32(scalePos_ + 4, (uint32_t)(fps_ * 1000.0 + 0.5));
        Patch32(lengthPos_, (uint32_t)frames_);
    }

    CritSec cs_;
    FILE* fp_ = nullptr;
    std::string path_;
    std::vector<Idx> idx_;
    long usecPos_ = 0, totalPos_ = 0, scalePos_ = 0, lengthPos_ = 0, moviSizePos_ = 0, moviPos_ = 0;
    int rot_ = 0, w_ = 0, h_ = 0;
    uint32_t frames_ = 0, skipped_ = 0, maxFrame_ = 0;
    DWORD startTick_ = 0, firstTick_ = 0, lastTick_ = 0;
    double fps_ = 0;
    bool headerDone_ = false, full_ = false;
};

// ============================================================ ViewerWindow
// 영상 창: 위쪽 첫 줄(라디오 WebCam1 / WebCam2 / IP Cam + 실행 / 종료 버튼), 둘째 줄(우로 90도 회전 / 좌로 90도 회전 / 사진 저장 / 동영상 촬영 / 저장 폴더 열기), 셋째 줄(연속 사진 간격 목록 + 연속 사진 개시 / 종료 / 연속 10회) + 영상.
// [실행] = 선택한 카메라 영상 재생 (재생 중이면 다시 시작/전환), [종료] = 영상 재생만 멈춤 (창은 그대로).
// 창이 닫히면 본 창(notify)에 MSG_CLOSED 를 보내 세션을 정리하게 한다 (앱은 계속 실행).
class ViewerWindow {
public:
    static const UINT MSG_CLOSED = WM_APP + 5;
    using RunHandler = std::function<bool(int, std::string&)>;      // (카메라 0=WebCam1 1=WebCam2 2=IP Cam, 성공 시 제목용 이름 / 실패 시 사유)
    using StopHandler = std::function<void()>;

    ViewerWindow(HINSTANCE inst, const Config& cfg, FrameStore& store, Recorder& rec, HWND notify)
        : inst_(inst), cfg_(cfg), store_(store), rec_(rec), notify_(notify) {}
    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Create() {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = &ViewerWindow::StaticProc;
        wc.hInstance = inst_;
        wc.lpszClassName = "IPCam";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(inst_, MAKEINTRESOURCE(1));          // exe 아이콘
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { IPLOG("RegisterClass failed err=%lu", GetLastError()); return false; }
        hwnd_ = CreateWindowA("IPCam", "Camera", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr, nullptr, inst_, this);
        if (!hwnd_) { IPLOG("CreateWindow failed err=%lu", GetLastError()); return false; }
        SetTimer(hwnd_, 1, 1000, nullptr);
        IPLOG("window created hwnd=%p", (void*)hwnd_);
        return true;
    }

    HWND Hwnd() const { return hwnd_; }
    void Destroy() { HWND h = hwnd_; if (h && IsWindow(h)) DestroyWindow(h); }
    void SetHandlers(RunHandler run, StopHandler stop) { onRun_ = std::move(run); onStop_ = std::move(stop); }

    // Safe to call from any thread.
    void NotifyFrame() { HWND h = hwnd_; if (h) PostMessage(h, WM_APP, 0, 0); }

private:
    enum { ID_RIGHT = 1, ID_LEFT = 2, ID_PHOTO = 3, ID_REC = 4, ID_FOLDER = 5,
           ID_CAM0 = 11, ID_CAM1 = 12, ID_CAM2 = 13, ID_RUN = 14, ID_STOP = 15,
           ID_INTERVAL = 16, ID_BURST = 17, ID_BURSTSTOP = 18, ID_BURST10 = 19, STRIP_H = 102, STATUS_H = 24 };

    static LRESULT CALLBACK StaticProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        ViewerWindow* self = nullptr;
        if (m == WM_NCCREATE) {
            self = static_cast<ViewerWindow*>(reinterpret_cast<CREATESTRUCTA*>(l)->lpCreateParams);
            SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd_ = h;
        } else {
            self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtr(h, GWLP_USERDATA));
        }
        return self ? self->HandleMessage(h, m, w, l) : DefWindowProc(h, m, w, l);
    }

    LRESULT HandleMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_CREATE:     MakeButtons(h); return 0;
        case WM_APP:        InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_TIMER:      if (w == 2) OnBurstTimer(); else OnTimer(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:      OnPaint(); return 0;
        case WM_COMMAND:    OnCommand(LOWORD(w)); if (LOWORD(w) != ID_INTERVAL) SetFocus(h); return 0;       // (목록은 포커스를 빼앗으면 펼침이 닫힘)       // 버튼을 눌러도 Esc 가 창에 전달되도록
        case WM_KEYDOWN:    if (w == VK_ESCAPE) DestroyWindow(h); return 0;
        case WM_CLOSE:      IPLOG("WM_CLOSE"); break;
        case WM_DESTROY:
            IPLOG("WM_DESTROY");
            KillTimer(h, 1);
            KillTimer(h, 2);
            hwnd_ = nullptr;
            if (notify_) PostMessage(notify_, MSG_CLOSED, 0, 0);     // 앱은 종료하지 않고 본 창이 세션만 정리
            return 0;
        }
        return DefWindowProc(h, m, w, l);
    }

    void MakeButtons(HWND h) {
        struct B { int id; const char16_t* text; int w; DWORD style; };
        // 첫 줄: 카메라 선택(라디오 3개, 첫 번째에 WS_GROUP) + 실행 / 종료
        static const B top[5] = {{ID_CAM0, u"WebCam1", 84, BS_AUTORADIOBUTTON | WS_GROUP}, {ID_CAM1, u"WebCam2", 84, BS_AUTORADIOBUTTON},
                                 {ID_CAM2, u"IP Cam", 76, BS_AUTORADIOBUTTON}, {ID_RUN, u"실행", 80, BS_PUSHBUTTON | WS_GROUP}, {ID_STOP, u"종료", 80, BS_PUSHBUTTON}};
        // 둘째 줄: 회전 / 사진 / 동영상 / 폴더
        static const B bs[5] = {{ID_RIGHT, u"우로 90도 회전", 112, BS_PUSHBUTTON}, {ID_LEFT, u"좌로 90도 회전", 112, BS_PUSHBUTTON},
                                {ID_PHOTO, u"사진 저장", 92, BS_PUSHBUTTON}, {ID_REC, u"동영상 촬영", 124, BS_PUSHBUTTON},
                                {ID_FOLDER, u"저장 폴더 열기", 112, BS_PUSHBUTTON}};
        for (int row = 0; row < 2; row++) {
            const B* a = row ? bs : top;
            int x = 6;
            for (int i = 0; i < 5; i++) {
                HWND b = CreateWindowExW(0, L"BUTTON", (LPCWSTR)a[i].text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | a[i].style,
                                         x, 5 + row * 32, a[i].w, 28, h, (HMENU)(INT_PTR)a[i].id, inst_, nullptr);
                SendMessage(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                x += a[i].w + 6;
            }
        }
        SendDlgItemMessage(h, ID_CAM2, BM_SETCHECK, BST_CHECKED, 0);      // 기본은 IP Cam (예전 동작과 같게)
        // 셋째 줄: 연속 사진 간격(초) 목록 + 개시 / 종료 / 연속 10회
        HWND lb = CreateWindowExW(0, L"STATIC", (LPCWSTR)u"연속 사진 간격:", WS_CHILD | WS_VISIBLE, 6, 74 + 3, 100, 18, h, nullptr, inst_, nullptr);
        SendMessage(lb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        HWND cb = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                  110, 74, 84, 300, h, (HMENU)(INT_PTR)ID_INTERVAL, inst_, nullptr);
        SendMessage(cb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        for (int i = 0; i < 15; i++) {
            wchar_t t[16];
            wsprintfW(t, L"%d초", IntervalSec(i));
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t);
        }
        SendMessage(cb, CB_SETCURSEL, 3, 0);                              // 기본 5초
        static const B tb[3] = {{ID_BURST, u"연속 사진 개시", 124, BS_PUSHBUTTON}, {ID_BURSTSTOP, u"연속 사진 종료", 124, BS_PUSHBUTTON},
                                {ID_BURST10, u"연속 10회", 100, BS_PUSHBUTTON}};
        int x = 200;
        for (int i = 0; i < 3; i++) {
            HWND b = CreateWindowExW(0, L"BUTTON", (LPCWSTR)tb[i].text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | tb[i].style,
                                     x, 74, tb[i].w, 28, h, (HMENU)(INT_PTR)tb[i].id, inst_, nullptr);
            SendMessage(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            x += tb[i].w + 6;
        }
        UpdateButtons();
    }

    static int IntervalSec(int idx) {                                     // 목록 순서대로의 간격(초)
        static const int v[15] = {1, 2, 3, 5, 10, 15, 30, 60, 120, 180, 300, 600, 900, 1800, 3600};
        return v[(idx < 0 || idx > 14) ? 3 : idx];
    }

    // 연속 사진: mode 1 = 개시(종료를 누를 때까지), 2 = 연속 10회. 첫 장은 바로 저장하고 이후 목록의 간격마다 저장
    void StartBurst(int mode) {
        if (burst_ || !running_) return;
        burstSec_ = IntervalSec((int)SendDlgItemMessage(hwnd_, ID_INTERVAL, CB_GETCURSEL, 0, 0));
        burst_ = mode;
        burstDone_ = 0;
        IPLOG("burst start: mode=%d interval=%ds", mode, burstSec_);
        BurstShot();
        if (burst_) SetTimer(hwnd_, 2, (UINT)burstSec_ * 1000, nullptr);
        UpdateButtons();
    }

    void BurstShot() {
        if (SavePhoto()) burstDone_++;                                    // 아직 영상이 없어 저장 못 한 것은 횟수에 넣지 않음
        if (burst_ == 2 && burstDone_ >= 10) StopBurst();
    }

    void OnBurstTimer() { if (burst_) BurstShot(); }

    void StopBurst() {
        if (!burst_) return;
        KillTimer(hwnd_, 2);
        char b[96];
        snprintf(b, sizeof b, burst_ == 2 && burstDone_ >= 10 ? "연속 10회 저장 완료 (%d장)" : "연속 사진 종료: %d장 저장", burstDone_);
        IPLOG("burst stop: %d saved", burstDone_);
        burst_ = 0;
        SetMsg(b);
        UpdateButtons();
    }

    int SelectedCamera() const {
        for (int i = 0; i < 3; i++)
            if (SendDlgItemMessage(hwnd_, ID_CAM0 + i, BM_GETCHECK, 0, 0) == BST_CHECKED) return i;
        return 2;
    }

    // [실행]: 녹화 중이면 먼저 저장하고, 선택한 카메라로 (다시) 시작
    void RunCamera() {
        StopBurst();
        if (rec_.Active()) StopRecord();
        std::string res;
        const int src = SelectedCamera();
        if (onRun_ && onRun_(src, res)) {
            running_ = true;
            label_ = res;
            IPLOG("run: camera %d (%s)", src, res.c_str());
        } else {
            running_ = false;
            SetMsg(res.empty() ? "카메라를 시작하지 못했습니다" : res);
            IPLOG("run failed: camera %d (%s)", src, res.c_str());
        }
        UpdateButtons();
        OnTimer();
    }

    // [종료]: 영상 재생만 멈춤 (녹화 중이면 저장). 창은 닫지 않음
    void StopCamera() {
        StopBurst();
        if (rec_.Active()) StopRecord();
        if (onStop_) onStop_();
        running_ = false;
        IPLOG("stop camera");
        UpdateButtons();
        OnTimer();
    }

    // 저장/녹화/오류 안내는 창 맨 아래 상태줄에 "HH:MM:SS  내용" 으로 표시 (다음 안내가 올 때까지 유지)
    void SetMsg(const std::string& s) {
        SYSTEMTIME t; GetLocalTime(&t);
        char h[16];
        snprintf(h, sizeof h, "%02d:%02d:%02d  ", t.wHour, t.wMinute, t.wSecond);
        msg_ = h + s;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void OnCommand(int id) {
        switch (id) {
        case ID_RIGHT: rot_ = (rot_ + 1) & 3; IPLOG("rotate right -> %d", rot_ * 90); InvalidateRect(hwnd_, nullptr, FALSE); break;
        case ID_LEFT:  rot_ = (rot_ + 3) & 3; IPLOG("rotate left -> %d", rot_ * 90);  InvalidateRect(hwnd_, nullptr, FALSE); break;
        case ID_PHOTO: SavePhoto(); break;
        case ID_REC:   ToggleRecord(); break;
        case ID_FOLDER: OpenFolder(); break;
        case ID_RUN:   RunCamera(); break;
        case ID_STOP:  StopCamera(); break;
        case ID_BURST:     StartBurst(1); break;
        case ID_BURST10:   StartBurst(2); break;
        case ID_BURSTSTOP: StopBurst(); break;
        }
    }

    void OpenFolder() {                                         // 저장 폴더(IPCam\)를 탐색기로 연다
        const std::string dir = SaveDir();
        ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);       // SaveDir() 는 ANSI 경로 (한글 경로도 그대로 동작)
    }

    bool SavePhoto() {
        Gdiplus::Bitmap* b = store_.CopyFrame();
        std::string jpg;
        if (!b) { SetMsg("저장할 영상이 아직 없습니다"); return false; }
        bool ok = EncodeJpeg(b, rot_, jpg);
        delete b;
        if (!ok) { SetMsg("사진 저장 실패 (인코딩)"); IPLOG("photo: encode failed"); return false; }
        const std::string path = UniquePath(SaveDir() + Stamp(".jpg"));
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { SetMsg("사진 저장 실패: " + path); IPLOG("photo: cannot open %s", path.c_str()); return false; }
        fwrite(jpg.data(), 1, jpg.size(), f);
        fclose(f);
        IPLOG("photo saved: %s (%u bytes)", path.c_str(), (unsigned)jpg.size());
        SetMsg("사진 저장: " + path);
        return true;
    }

    void ToggleRecord() {
        if (!rec_.Active()) {                                   // 처음 누르면 녹화 시작
            std::string path;
            if (!rec_.Start(rot_, path)) { SetMsg("동영상 녹화를 시작하지 못했습니다"); return; }
            SetMsg("녹화 시작: " + path);
        } else {                                                // 두 번째 누르면 녹화 종료 및 저장
            StopRecord();
        }
        UpdateButtons();
    }

    void StopRecord() {
        std::string path, info;
        bool ok = rec_.Stop(path, info);
        SetMsg(ok ? "동영상 저장: " + path + " (" + info + ")" : info);
        UpdateButtons();
    }

    void UpdateButtons() {
        const bool rec = rec_.Active();
        SetWindowTextW(GetDlgItem(hwnd_, ID_REC), rec ? (LPCWSTR)u"촬영 종료 (저장)" : (LPCWSTR)u"동영상 촬영");
        EnableWindow(GetDlgItem(hwnd_, ID_RIGHT), !rec);        // 녹화 중에는 회전 고정
        EnableWindow(GetDlgItem(hwnd_, ID_LEFT), !rec);
        EnableWindow(GetDlgItem(hwnd_, ID_STOP), running_);     // 재생 중일 때만 [종료] / 사진 / 동영상
        EnableWindow(GetDlgItem(hwnd_, ID_PHOTO), running_);
        EnableWindow(GetDlgItem(hwnd_, ID_REC), running_ || rec);
        const bool bs = burst_ != 0;                            // 연속 사진: 진행 중에는 개시/10회/간격 고정, 종료만 가능
        EnableWindow(GetDlgItem(hwnd_, ID_BURST), running_ && !bs);
        EnableWindow(GetDlgItem(hwnd_, ID_BURST10), running_ && !bs);
        EnableWindow(GetDlgItem(hwnd_, ID_BURSTSTOP), bs);
        EnableWindow(GetDlgItem(hwnd_, ID_INTERVAL), !bs);
    }

    void OnTimer() {
        if (rec_.Full() && rec_.Active()) { StopRecord(); SetMsg("파일 크기 한도에 도달해 녹화를 종료했습니다"); }
        char t[256];
        int n = running_ ? snprintf(t, sizeof t, "%s  -  %d fps", label_.c_str(), store_.TakeFps())
                         : snprintf(t, sizeof t, "Camera  -  stopped");
        if (rec_.Active()) { int s = rec_.Seconds(); snprintf(t + n, sizeof t - n, "   REC %02d:%02d", s / 60, s % 60); }
        SetWindowTextA(hwnd_, t);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT rc; GetClientRect(hwnd_, &rc);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP mb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mdc, mb);
        FillRect(mdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        RECT strip = {0, 0, rc.right, STRIP_H};
        FillRect(mdc, &strip, GetSysColorBrush(COLOR_BTNFACE));
        RECT vr = {0, STRIP_H, rc.right, rc.bottom - STATUS_H};
        if (vr.bottom > vr.top) store_.Paint(mdc, vr, rot_);
        RECT sb = {0, rc.bottom - STATUS_H, rc.right, rc.bottom};          // 상태줄 (메시지 표시)
        if (sb.top > STRIP_H) {
            FillRect(mdc, &sb, GetSysColorBrush(COLOR_BTNFACE));
            RECT ln = {0, sb.top, rc.right, sb.top + 1};
            FillRect(mdc, &ln, GetSysColorBrush(COLOR_3DSHADOW));
            char16_t u[600];
            if (!msg_.empty() && Utf8::ToUtf16(msg_.c_str(), -1, u, 600)) {
                SetBkMode(mdc, TRANSPARENT);
                SetTextColor(mdc, GetSysColor(COLOR_BTNTEXT));
                HGDIOBJ of = SelectObject(mdc, GetStockObject(DEFAULT_GUI_FONT));
                RECT tr = {8, sb.top + 1, rc.right - 8, rc.bottom};
                DrawTextW(mdc, (LPCWSTR)u, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                SelectObject(mdc, of);
            }
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(mb);
        DeleteDC(mdc);
        EndPaint(hwnd_, &ps);
    }

    HINSTANCE inst_;
    Config cfg_;
    FrameStore& store_;
    Recorder& rec_;
    HWND notify_;
    int rot_ = 0;                      // 시계 방향 90도 단위 (0~3)
    int burst_ = 0, burstDone_ = 0, burstSec_ = 5;     // 연속 사진: 0=안 함 1=개시 2=연속 10회, 저장한 장수, 간격(초)
    bool running_ = false;             // 영상 재생 중 ([실행] 후 ~ [종료] 전)
    std::string label_;                // 제목줄에 표시할 카메라 이름
    RunHandler onRun_;
    StopHandler onStop_;
    std::string msg_;
    volatile HWND hwnd_ = nullptr;
};

// ============================================================ Session
// '카메라 영상' 한 번의 실행: 로그 열기 -> Winsock/GDI+ -> 영상 창. 창이 열려도 영상은 바로 재생하지 않고, 창의 [실행] 버튼을 누르면
// 선택한 카메라(WebCam1 / WebCam2 / IP Cam)의 수신 스레드를 시작한다 ([종료] 는 그 스레드만 정지).
// 소멸 순서는 생성의 반대 (수신 스레드 -> 창 -> 프레임 -> GDI+ -> Winsock -> 로그)
class Session {
public:
    static const UINT MSG_CLOSED = ViewerWindow::MSG_CLOSED;

    Session(HINSTANCE inst, HWND notify, const std::string& host, const std::string& port)
        : host_(host), port_(port), cfg_(Make(host, port)), window_(inst, cfg_, store_, rec_, notify) {
        window_.SetHandlers([this](int src, std::string& res) { return Run(src, res); }, [this] { Halt(); });
    }
    ~Session() { window_.Destroy(); StopCam(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool Open() {
        IPLOG("ip target=%s:%s%s", host_.c_str(), port_.c_str(), cfg_.path.c_str());
        if (!ws_.Ok())  return Fail("WSAStartup failed");
        if (!gdi_.Ok()) return Fail("GdiplusStartup failed");
        store_.SetStatus(kIdle);
        if (!window_.Create()) return Fail("Window creation failed (see ipcam.log)");
        SetForegroundWindow(window_.Hwnd());
        return true;
    }
    HWND Hwnd() const { return window_.Hwnd(); }
    void SetIp(const std::string& host, const std::string& port) { host_ = host; port_ = port; }   // 'IP cam 등록' 으로 바뀐 주소 (다음 [실행] 부터 적용)

private:
    static constexpr const char* kIdle = "카메라를 선택하고 [실행] 을 누르세요";

    void StopCam() {
        if (ip_)  { ip_->Stop();  ip_.reset(); }
        if (cam_) { cam_->Stop(); cam_.reset(); }
    }

    // [종료]: 수신 스레드 정지 + 화면 지움
    void Halt() {
        StopCam();
        store_.Clear(kIdle);
    }

    // [실행]: 이전 카메라를 멈추고 src (0=WebCam1, 1=WebCam2, 2=IP Cam) 를 시작. 성공하면 res = 제목줄 이름, 실패하면 res = 사유
    bool Run(int src, std::string& res) {
        StopCam();
        if (src == 2) {
            if (host_.empty()) {
                store_.Clear(kIdle);
                res = "IP cam 주소가 없습니다. 메뉴 'IP cam 등록' 으로 먼저 등록하세요";
                return false;
            }
            store_.Clear("Connecting...");
            ip_.reset(new MjpegClient(Make(host_, port_), store_, [this] { window_.NotifyFrame(); },
                                      [this](const char* d, size_t n, Gdiplus::Bitmap* b) { rec_.Add(d, n, b); }));
            ip_->Start();
            res = "IP Cam " + host_ + ":" + port_;
        } else {
            store_.Clear("Opening camera...");
            cam_.reset(new WebcamClient(src, store_, [this] { window_.NotifyFrame(); },
                                        [this](Gdiplus::Bitmap* b) {             // 녹화 중일 때만 JPEG 로 인코딩해 AVI 에 담음
                                            if (!rec_.Active()) return;
                                            std::string j;
                                            if (EncodeJpeg(b, 0, j)) rec_.Add(j.data(), j.size(), b);
                                        }));
            cam_->Start();
            res = src == 0 ? "WebCam1" : "WebCam2";
        }
        return true;
    }

    // 로그 파일: exe 와 같은 폴더의 ipcam.log (이름은 예전 그대로), 시작할 때마다 새로 작성. 크래시 시 예외 코드를 기록
    struct LogScope {
        LONG (WINAPI *prev)(EXCEPTION_POINTERS*) = nullptr;
        static inline int refs = 0;                       // 열려 있는 영상 창(세션) 수. 첫 세션이 로그를 열고 마지막 세션이 닫음 (UI 스레드에서만 증감)
        LogScope() {
            if (refs++ > 0) { IPLOG("---- another camera window opened (%d)", refs); return; }
            char exe[MAX_PATH] = "";
            std::string path = "ipcam.log";
            if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
                path = exe;
                size_t sl = path.find_last_of("\\/");
                path = (sl == std::string::npos ? std::string() : path.substr(0, sl + 1)) + "ipcam.log";
            }
            if (!Logger::Instance().Open(path))
                MessageBoxA(nullptr, ("Cannot open log file: " + path).c_str(), "ipcam", MB_ICONERROR);
            prev = SetUnhandledExceptionFilter(&LogScope::CrashHandler);
            IPLOG("exe=%s", exe);
            IPLOG("==== ipcam start ====");
            IPLOG("cmdline=%s", GetCommandLineA());
            IPLOG("pid=%lu", GetCurrentProcessId());
        }
        ~LogScope() {
            if (--refs > 0) { IPLOG("---- a camera window closed (%d left)", refs); return; }
            IPLOG("==== ipcam exit ====");
            SetUnhandledExceptionFilter(prev);
            Logger::Instance().Close();
        }
        static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
            IPLOG("!!! CRASH: exception code=0x%08lX address=%p", ep->ExceptionRecord->ExceptionCode,
                  ep->ExceptionRecord->ExceptionAddress);
            Logger::Instance().Close();
            return EXCEPTION_EXECUTE_HANDLER;
        }
    };

    static Config Make(const std::string& host, const std::string& port) {
        Config c;
        c.host = host;
        c.port = port;
        return c;
    }
    bool Fail(const char* msg) {
        IPLOG("FATAL: %s", msg);
        MessageBoxA(nullptr, msg, "ipcam", MB_ICONERROR);
        return false;
    }

    LogScope        log_;                 // 가장 먼저 만들어지고 가장 나중에 없어짐
    std::string     host_, port_;         // IP Cam 주소 (등록값)
    Config          cfg_;
    WinsockSession  ws_;
    GdiplusSession  gdi_;
    FrameStore      store_;
    Recorder        rec_;                 // 동영상 촬영 (창/수신 스레드보다 나중에 없어짐 -> 파일을 마무리)
    ViewerWindow    window_;
    std::unique_ptr<MjpegClient>  ip_;    // IP Cam 수신 스레드 ([실행] 때 만들고 [종료] 때 없앰)
    std::unique_ptr<WebcamClient> cam_;   // WebCam1 / WebCam2 수신 스레드
};

}  // namespace ipcam

// IP cam 등록 대화상자: IP address 와 port 번호 입력 (host 칸에 "주소:포트" 로 써도 됨)
class IpCamDialog {
public:
    static bool Show(HINSTANCE hi, HWND owner, char* host, int cap, int& port) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(9); b.W(0); b.W(0); b.W(330); b.W(122);
        b.W(0); b.W(0);
        b.S("IP cam 등록");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        b.Control(T,                                   10, 12, 60, 10, 120, "STATIC", "IP address");
        b.Control(T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 76, 10, 140, 13, IDC_HOST, "EDIT", "");
        b.Control(T,                                   10, 30, 60, 10, 121, "STATIC", "port 번호");
        b.Control(T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 76, 28, 50, 13, IDC_PORT, "EDIT", "");
        b.Control(T,                                   10, 48, 210, 10, 122, "STATIC", "예) DroidCam 4747 / IP Webcam 8080");
        b.Control(T,                                   10, 66, 310, 10, 123, "STATIC", "스마트폰에 DroidCam 을 설치하세요  (아래 주소를 누르면 브라우저로 열립니다)");
        b.Control(T | SS_NOTIFY,                       10, 78, 310, 10, IDC_LINK, "STATIC", DROIDCAM_URL);
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON,    110, 98, 50, 14, IDOK, "저장");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,       170, 98, 50, 14, IDCANCEL, "취소");
        Ctx c = {host, cap, &port, NULL};
        return DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c) == 1;
    }

private:
    enum { IDC_HOST = 101, IDC_PORT = 102, IDC_LINK = 103 };
    static constexpr const char* DROIDCAM_URL = "https://play.google.com/store/apps/details?id=com.dev47apps.obsdroidcam";
    struct Ctx { char* host; int cap; int* port; HFONT font; };

    static void Warn(HWND d, const char* msg, int focusId) {
        char16_t m[200], t[32];
        Utf8::ToUtf16(msg, -1, m, 200);
        Utf8::ToUtf16("IP cam 등록", -1, t, 32);
        MessageBoxW(d, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(d, focusId));
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            Ctx* c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            char16_t u[96];
            u[0] = 0;
            Utf8::ToUtf16(c->host, -1, u, 96);
            SetDlgItemTextW(d, IDC_HOST, (LPCWSTR)u);
            SendDlgItemMessageW(d, IDC_HOST, EM_LIMITTEXT, 63, 0);
            SendDlgItemMessageW(d, IDC_PORT, EM_LIMITTEXT, 5, 0);
            SetDlgItemInt(d, IDC_PORT, (UINT)*c->port, FALSE);
            HWND hl = GetDlgItem(d, IDC_LINK);                       // 링크: 파란 밑줄 글꼴
            LOGFONTW lf;
            HFONT f = (HFONT)SendMessageW(hl, WM_GETFONT, 0, 0);
            if (f && GetObjectW(f, sizeof(lf), &lf)) {
                lf.lfUnderline = TRUE;
                c->font = CreateFontIndirectW(&lf);
                SendMessageW(hl, WM_SETFONT, (WPARAM)c->font, TRUE);
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(d, IDC_LINK)) {
                SetTextColor((HDC)w, RGB(0, 0, 255));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_SETCURSOR:
            if ((HWND)w == GetDlgItem(d, IDC_LINK)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                SetWindowLongPtrA(d, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            break;
        case WM_DESTROY: {
            Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
            if (c && c->font) DeleteObject(c->font);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDC_LINK) {                             // 주소 클릭 -> 시스템 기본 브라우저
                ShellExecuteA(NULL, "open", DROIDCAM_URL, NULL, NULL, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(w) == IDOK) {
                Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
                char16_t u[100];
                char k[400];
                GetDlgItemTextW(d, IDC_HOST, (LPWSTR)u, 100);
                WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, k, sizeof(k), NULL, NULL);
                std::string h = k;
                while (!h.empty() && (h.back() == ' ' || h.back() == '\t')) h.pop_back();
                size_t st = 0;
                while (st < h.size() && (h[st] == ' ' || h[st] == '\t')) st++;
                h = h.substr(st);
                BOOL ok = FALSE;
                UINT port = GetDlgItemInt(d, IDC_PORT, &ok, FALSE);
                const size_t colon = h.find(':');
                if (colon != std::string::npos) {                       // "192.168.0.70:4747" 형태
                    const int p = atoi(h.c_str() + colon + 1);
                    if (p > 0) { port = (UINT)p; ok = TRUE; }
                    h = h.substr(0, colon);
                }
                if (h.empty() || h.find(' ') != std::string::npos) { Warn(d, "IP address 를 입력하세요. 예: 192.168.0.70", IDC_HOST); return TRUE; }
                if (!ok || port < 1 || port > 65535) { Warn(d, "port 번호는 1 ~ 65535 사이의 숫자로 입력하세요.", IDC_PORT); return TRUE; }
                lstrcpynA(c->host, h.c_str(), c->cap);
                *c->port = (int)port;
                EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
};


// 폰 탐색기 대화상자: ftp:// IP address(4칸) : port, 사용자명, 암호 입력 + CX파일탐색기 안내 링크
class PhoneDialog {
public:
    // host: "a.b.c.d" (입출력), port/user: 입출력, pass: 출력 전용 (저장하지 않음)
    static bool Show(HINSTANCE hi, HWND owner, char* host, int& port, char* user, int ucap, char* pass, int pcap) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(20); b.W(0); b.W(0); b.W(380); b.W(158);        // 컨트롤 개수 20
        b.W(0); b.W(0);
        b.S("폰 탐색기");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        const DWORD E = T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        const DWORD N = E | ES_NUMBER | ES_CENTER;
        b.Control(T, 12,  6, 360, 10, 138, "STATIC", "1)  스마트폰에서 \"CX파일 탐색기\"를 먼저 실행하세요.");
        b.Control(T, 12, 18, 360, 10, 139, "STATIC", "2)  [CX파일 탐색기] 화면에서, [네트워크] -->  [네트워크 액세스]  -->  [시작]  을 차례대로 누르세요");
        b.Control(T,       12, 44, 34, 10, 130, "STATIC", "ftp://");
        b.Control(N,       50, 42, 30, 13, IDC_O1, "EDIT", "");
        b.Control(T | SS_CENTER, 80, 44, 10, 10, 131, "STATIC", ".");
        b.Control(N,       90, 42, 30, 13, IDC_O1 + 1, "EDIT", "");
        b.Control(T | SS_CENTER, 120, 44, 10, 10, 132, "STATIC", ".");
        b.Control(N,      130, 42, 30, 13, IDC_O1 + 2, "EDIT", "");
        b.Control(T | SS_CENTER, 160, 44, 10, 10, 133, "STATIC", ".");
        b.Control(N,      170, 42, 30, 13, IDC_O1 + 3, "EDIT", "");
        b.Control(T | SS_CENTER, 200, 44, 10, 10, 134, "STATIC", ":");
        b.Control(N,      210, 42, 40, 13, IDC_PORT, "EDIT", "");
        b.Control(T,        4, 66, 46, 10, 135, "STATIC", "사용자 이름");
        b.Control(E,       50, 64, 200, 13, IDC_USER, "EDIT", "");
        b.Control(T,       12, 86, 36, 10, 136, "STATIC", "비밀번호");
        b.Control(E | ES_PASSWORD, 50, 84, 200, 13, IDC_PASS, "EDIT", "");
        b.Control(T | SS_NOPREFIX, 12, 108, 356, 10, 137, "STATIC", "스마트폰에서 미리 설치&실행 하여야 할 어플 [ CX파일탐색기 ] :");
        b.Control(T | SS_NOPREFIX | SS_NOTIFY, 12, 119, 360, 10, IDC_LINK, "STATIC", URL);
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON, 130, 137, 50, 14, IDOK, "실행");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,    200, 137, 50, 14, IDCANCEL, "취소");
        Ctx c = {host, &port, user, ucap, pass, pcap, NULL};
        return DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c) == 1;
    }

private:
    enum { IDC_O1 = 101, IDC_PORT = 105, IDC_USER = 106, IDC_PASS = 107, IDC_LINK = 108, IDC_NOTICE = 137 };
    static constexpr const char* URL = "https://play.google.com/store/search?q=cx%ED%8C%8C%EC%9D%BC%ED%83%90%EC%83%89%EA%B8%B0&c=apps";
    struct Ctx { char* host; int* port; char* user; int ucap; char* pass; int pcap; HFONT font; };

    static void Warn(HWND d, const char* msg, int focusId) {
        char16_t m[200], t[32];
        Utf8::ToUtf16(msg, -1, m, 200);
        Utf8::ToUtf16("폰 탐색기", -1, t, 32);
        MessageBoxW(d, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(d, focusId));
    }

    static void GetUtf8(HWND d, int id, char* out, int cap) {        // 입력 칸 -> UTF-8 (앞뒤 공백 제거는 하지 않음: 암호에 공백이 있을 수 있음)
        char16_t u[100];
        char k[400];
        GetDlgItemTextW(d, id, (LPWSTR)u, 100);
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, k, sizeof(k), NULL, NULL);
        lstrcpynA(out, k, cap);
        SecureZeroMemory(u, sizeof(u));
        SecureZeroMemory(k, sizeof(k));
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            Ctx* c = (Ctx*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            char tmp[32];                                            // "a.b.c.d" -> 4칸
            lstrcpynA(tmp, c->host, sizeof(tmp));
            char* p = tmp;
            for (int i = 0; i < 4; i++) {
                SendDlgItemMessageW(d, IDC_O1 + i, EM_LIMITTEXT, 3, 0);
                char* q = p;
                while (*q && *q != '.') q++;
                const char sv = *q;
                *q = 0;
                SetDlgItemTextA(d, IDC_O1 + i, p);
                if (!sv) break;
                p = q + 1;
            }
            SendDlgItemMessageW(d, IDC_PORT, EM_LIMITTEXT, 5, 0);
            SetDlgItemInt(d, IDC_PORT, (UINT)*c->port, FALSE);
            SendDlgItemMessageW(d, IDC_USER, EM_LIMITTEXT, 60, 0);
            SendDlgItemMessageW(d, IDC_PASS, EM_LIMITTEXT, 60, 0);
            char16_t u[100];
            u[0] = 0;
            Utf8::ToUtf16(c->user, -1, u, 100);
            SetDlgItemTextW(d, IDC_USER, (LPCWSTR)u);
            HWND hl = GetDlgItem(d, IDC_LINK);                       // 링크: 파란 밑줄 글꼴
            LOGFONTW lf;
            HFONT f = (HFONT)SendMessageW(hl, WM_GETFONT, 0, 0);
            if (f && GetObjectW(f, sizeof(lf), &lf)) {
                lf.lfUnderline = TRUE;
                c->font = CreateFontIndirectW(&lf);
                SendMessageW(hl, WM_SETFONT, (WPARAM)c->font, TRUE);
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(d, IDC_LINK) || (HWND)l == GetDlgItem(d, IDC_NOTICE)) {   // 링크: 파랑, 안내 문구: 빨강
                SetTextColor((HDC)w, (HWND)l == GetDlgItem(d, IDC_LINK) ? RGB(0, 0, 255) : RGB(255, 0, 0));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_SETCURSOR:
            if ((HWND)w == GetDlgItem(d, IDC_LINK)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                SetWindowLongPtrA(d, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            break;
        case WM_DESTROY: {
            Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
            if (c && c->font) DeleteObject(c->font);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDC_LINK) {                             // 주소 클릭 -> 시스템 기본 브라우저
                ShellExecuteA(NULL, "open", URL, NULL, NULL, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(w) == IDOK) {
                Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
                UINT o[4];
                for (int i = 0; i < 4; i++) {
                    BOOL ok = FALSE;
                    o[i] = GetDlgItemInt(d, IDC_O1 + i, &ok, FALSE);
                    if (!ok || o[i] > 255) { Warn(d, "IP address 의 각 칸은 0 ~ 255 사이의 숫자로 입력하세요. 예: 192.168.0.70", IDC_O1 + i); return TRUE; }
                }
                BOOL ok = FALSE;
                UINT port = GetDlgItemInt(d, IDC_PORT, &ok, FALSE);
                if (!ok || port < 1 || port > 65535) { Warn(d, "port 번호는 1 ~ 65535 사이의 숫자로 입력하세요.", IDC_PORT); return TRUE; }
                wsprintfA(c->host, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
                *c->port = (int)port;
                GetUtf8(d, IDC_USER, c->user, c->ucap);
                GetUtf8(d, IDC_PASS, c->pass, c->pcap);
                EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) {
                EndDialog(d, 0);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
};


// 동반 실행 앱 등록 대화상자: 최대 5개의 BAT/CMD/EXE 전체 경로 (현재 등록된 값을 미리 표시, 찾기... 로 검색)
class CompanionDialog {
public:
    struct Ctx { char (*apps)[784]; bool* invoker; };
    static bool Show(HINSTANCE hi, HWND owner, char (*apps)[784], bool& invoker) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(19); b.W(0); b.W(0); b.W(400); b.W(172);        // 컨트롤 개수 19 (안내 1 + 5줄 x 3 + 체크박스 1 + 버튼 2)
        b.W(0); b.W(0);
        b.S("동반 실행 앱 등록");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        b.Control(T, 10, 6, 380, 20, 300, "STATIC", "프로그램을 시작할 때 함께 실행할 BAT / CMD / EXE / LNK (최대 5개, 번호 순서대로 실행). 비우면 실행하지 않습니다.");
        for (int i = 0; i < 5; i++) {
            const int y = 32 + i * 20;
            char n[8];
            wsprintfA(n, "%d)", i + 1);
            b.Control(T, 10, y + 2, 14, 10, 310 + i, "STATIC", n);
            b.Control(T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 26, y, 310, 13, IDC_E + i, "EDIT", "");
            b.Button(T | WS_TABSTOP | BS_PUSHBUTTON, 342, y - 1, 48, 14, IDC_B + i, "찾기...");
        }
        b.Button(T | WS_TABSTOP | BS_AUTOCHECKBOX, 26, 136, 364, 12, IDC_INV, "관리자 권한 요청 무시 (RunAsInvoker): 권한 확인창 없이 일반 권한으로 실행");
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON, 140, 152, 50, 14, IDOK, "확인");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,    210, 152, 50, 14, IDCANCEL, "취소");
        Ctx c = {apps, &invoker};
        return DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&c) == 1;
    }

private:
    enum { IDC_E = 201, IDC_B = 211, IDC_INV = 230 };

    static void Warn(HWND d, const char* msg, int focusId) {
        char16_t m[200], t[32];
        Utf8::ToUtf16(msg, -1, m, 200);
        Utf8::ToUtf16("동반 실행 앱 등록", -1, t, 32);
        MessageBoxW(d, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(d, focusId));
    }

    static void Browse(HWND d, int idx) {                            // 찾기...: 시작 메뉴 폴더(모든 사용자 / 내 계정)를 고른 뒤 BAT / CMD / EXE / LNK 파일 선택
        HMENU pm = CreatePopupMenu();
        AppendMenuW(pm, MF_STRING, 1, (LPCWSTR)u"모든 사용자 시작 메뉴 (ProgramData)");
        AppendMenuW(pm, MF_STRING, 2, (LPCWSTR)u"내 계정 시작 메뉴 (AppData)");
        RECT br;
        GetWindowRect(GetDlgItem(d, IDC_B + idx), &br);
        int pick = (int)TrackPopupMenu(pm, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, br.left, br.bottom, 0, d, NULL);
        DestroyMenu(pm);
        if (!pick) return;                                           // 메뉴에서 취소
        static const char16_t filter[] =
            u"실행 파일 (BAT, CMD, EXE, LNK)\0*.bat;*.cmd;*.exe;*.lnk\0"
            u"모든 파일 (*.*)\0*.*\0";
        char16_t file[MAX_PATH] = {};
        wchar_t startDir[MAX_PATH] = L"";
        ExpandEnvironmentStringsW(pick == 1 ? L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs"      // 모든 사용자용 앱 바로가기
                                            : L"%AppData%\\Microsoft\\Windows\\Start Menu\\Programs",         // 내 계정에만 설치된 앱 바로가기
                                  startDir, MAX_PATH);
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = d;
        ofn.lpstrFilter = (LPCWSTR)filter;
        ofn.lpstrFile   = (LPWSTR)file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrInitialDir = startDir;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_NODEREFERENCELINKS;   // LNK 는 바로가기 자체를 등록 (대상 파일로 바꾸지 않음)
        if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(d, IDC_E + idx, (LPCWSTR)file);
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            Ctx* c = (Ctx*)l;
            char (*apps)[784] = c->apps;
            CheckDlgButton(d, IDC_INV, *c->invoker ? BST_CHECKED : BST_UNCHECKED);
            for (int i = 0; i < 5; i++) {                            // 이미 등록된 값을 full path 로 미리 표시
                char16_t u[MAX_PATH];
                u[0] = 0;
                Utf8::ToUtf16(apps[i], -1, u, MAX_PATH);
                SendDlgItemMessageW(d, IDC_E + i, EM_LIMITTEXT, MAX_PATH - 1, 0);
                SetDlgItemTextW(d, IDC_E + i, (LPCWSTR)u);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (id >= IDC_B && id < IDC_B + 5) { Browse(d, id - IDC_B); return TRUE; }
            if (id == IDCANCEL) { EndDialog(d, 0); return TRUE; }
            if (id == IDOK) {
                Ctx* c = (Ctx*)GetWindowLongPtrA(d, GWLP_USERDATA);
                char (*apps)[784] = c->apps;
                wchar_t v[5][MAX_PATH];
                for (int i = 0; i < 5; i++) {
                    GetDlgItemTextW(d, IDC_E + i, v[i], MAX_PATH);
                    wchar_t* p = v[i];                               // 앞뒤 공백과 따옴표 제거 (경로를 "..." 로 붙여넣은 경우)
                    while (*p == L' ' || *p == L'"') p++;
                    int n = lstrlenW(p);
                    while (n > 0 && (p[n - 1] == L' ' || p[n - 1] == L'"')) p[--n] = 0;
                    if (p != v[i]) memmove(v[i], p, (n + 1) * sizeof(wchar_t));
                    if (n > 0 && GetFileAttributesW(v[i]) == INVALID_FILE_ATTRIBUTES) {
                        Warn(d, "파일을 찾을 수 없습니다. 전체 경로를 확인하거나 [찾기...] 로 선택하세요.", IDC_E + i);
                        return TRUE;
                    }
                }
                for (int i = 0; i < 5; i++) {
                    apps[i][0] = 0;
                    if (v[i][0]) WideCharToMultiByte(CP_UTF8, 0, v[i], -1, apps[i], 784, NULL, NULL);
                }
                *c->invoker = IsDlgButtonChecked(d, IDC_INV) == BST_CHECKED;
                EndDialog(d, 1);
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }
};


// ================================================================ Telegram
// screen_telegram.cpp 를 합친 부분 (namespace tg). 주기적으로 모니터별 화면(및 설정 시 웹캠)을 캡처하여 텔레그램으로 전송하고,
// 텔레그램 봇 명령으로 PC 를 제어한다. 별도 스레드로 동작하며(Start/RequestStop/WaitStopped), 시계 창/WiFiScan 과는 독립적이다.
//  - 설정은 screen_telegram.ini 가 아니라 gsHomeManager.ini 의 [Telegram] 섹션에서 읽는다. 섹션이 없으면 첫 실행 때 만든다
//    (예전 screen_telegram.ini 가 있으면 내용을 가져옴). 만드는 파일은 예전과 같다: log.txt, image\\, upload\\, detect.state, pinned.id (exe 폴더).
//  - WiFiScan 감시의 /HomeBack, /NotHome 은 trigger.txt 파일 대신 LocalTrigger 큐로 받는다 (tg::PostTrigger).
//  - 텔레그램 /stop, /restart 는 이 모듈만이 아니라 프로그램 전체(본 창 종료 -> WinMain)를 종료/재시작한다 (ScanEvents::Quit).
//  - 처리되지 않은 예외는 log.txt 에 남기고 종료한다 (InstallCrashHandlers).
//  - 고DPI 캡처를 위해 이 모듈의 스레드만 DPI 인식으로 만든다 (프로세스 전체는 그대로).
// ----------------------------------------------------------------
// gsHomeManager.ini [Telegram] 섹션의 설정: (TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 없으면 텔레그램 기능을 사용하지 않음)
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
//                           /stop 은 바로 종료하지 않고 Yes/No 버튼으로 확인한다 (Yes 이면 시계와 WiFiScan 감시까지 프로그램 전체를 종료. /restart 도 전체 재시작).
//                           TELEGRAM_CHAT_ID 에서 온 명령만 처리. SAVE_ONLY=TRUE 여도 동작)
//   TELEGRAM_ALLOWED_USER_IDS=111,222  (명령을 내릴 수 있는 텔레그램 사용자 ID 목록. 미기재 시 개인 chat 만 허용.
//                           TELEGRAM_CHAT_ID 가 그룹(음수)이면 이 목록이 없을 때 명령을 모두 거부한다)
//   KEEP_DAYS=7            (image 폴더에서 N일 지난 이미지 자동 삭제. 0 또는 미기재 시 삭제 안 함)
//   KEEP_MAX_FILES=5000    (image 폴더 이미지 최대 개수. 초과분은 오래된 것부터 삭제. 0 또는 미기재 시 제한 없음)
//   PREVIOUS_SESSION_CLEAR=TRUE  (TRUE 이면 프로그램 시작할 때 log.txt 와 image 폴더의 모든 파일을 삭제. 기본 FALSE)
//   LOCAL_TRIGGER=TRUE (같은 프로세스의 WiFiScan 감시(HomeWatcher)가 넘기는 "/HomeBack" 또는 "/NotHome" 을 1초 안에 받아 그 명령을 처리. 기본 TRUE.
//                       이 두 명령만 허용하고 300초 지난 요청은 무시. 시작 전에 이미 넘겨진 것도 300초 안이면 처리한다 -
//                       부팅 직후 WiFiScan 감시가 먼저 낸 첫 판정을 놓치지 않도록. 예전의 trigger.txt 파일은 쓰지 않는다)
//   TELEGRAM_MENU=TRUE (시작할 때 setMyCommands 로 텔레그램 입력창의 "메뉴"(/ 명령 목록)를 등록. 기본 TRUE. BotFather 에서 직접 만든 목록을 쓰려면 FALSE)
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
//   (텔레그램 /github 는 PC 의 기본 브라우저로 GitHub 의 ReadMe 페이지(kGithubUrl)를 연다.)
//   (텔레그램 /HomeBack 은 귀가 인사 "[집에 돌아 오신 것을 환영합니다. 푹 쉬세요]" 를 보낸다. 인사 뒤에 /detectOff 와 /pause 를 자동 처리한다.)
//   (텔레그램 /NotHome 은 외출 인사 "안녕히 다녀오세요. 제가 집을 잘 지키고 있겠습니다." 를 보낸다. 인사 뒤에 /detectOn 과 /resume 을 자동 처리한다.)
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
//
// ---- 클래스 구성 ----
//   Logger          시각이 붙은 로그 출력 (log.txt)
//   StrUtil         문자열 유틸 (trim, bool 파싱, UTF-8 <-> UTF-16)
//   SysUtil         시스템 유틸 (파일 존재, exe 폴더, 호스트명, 시각 문자열, INI [Telegram] 섹션 읽기/쓰기)
//   LocalTrigger    WiFiScan 감시 -> 텔레그램 모듈 명령 큐 (/HomeBack, /NotHome)
//   StopSignal      종료 요청 이벤트
//   ComScope        COM 초기화/해제 (RAII)
//   ScopedBitmap    HBITMAP 소유권 관리 (RAII)
//   Config          gsHomeManager.ini [Telegram] 설정
//   ImageEncoder    GDI+ 세션 + PNG/JPEG 인코딩
//   TelegramClient  텔레그램 sendPhoto / sendMessage / getUpdates (WinHTTP, 연결 재사용) + 재시도
//   CommandListener 텔레그램 봇 명령 수신 (getUpdates, 허용된 chat 만)
//   ImageWindow     폰에서 받은 이미지를 보여 주는 창 (이미지 1장 = 창 1개 = 스레드 1개)
//   Shot            캡처 결과 1장 (라벨 + 비트맵)
//   ICaptureSource  캡처 소스 인터페이스
//   MonitorCapture  모니터 화면 캡처 (ICaptureSource)
//   CameraCapture   웹캠 캡처, Media Foundation (ICaptureSource)
//   MotionDetector  카메라 침입(움직임) 탐지
//   ScreenTelegramApp  전체 흐름 (설정 로드 -> 주기 반복 캡처 -> image 폴더 저장 -> 전송, 명령/트리거/침입 탐지 스레드)
//
// 주의: mfplat.dll / mfreadwrite.dll (카메라용) 은 빌드 시점에 링크하지 않고,
// 실행 중 LoadLibrary 로 동적으로 찾는다. 해당 DLL이 없는 환경(Windows N/KN 등)
// 에서도 exe 자체는 정상적으로 시작되며, 카메라 기능만 자동으로 비활성화된다.
// -lmfuuid 는 GUID 상수만 담긴 순수 정적 라이브러리이므로 DLL 의존성이 없다. (빌드: -lwinhttp -lmfuuid -loleaut32 필요)
namespace tg {

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

    // INI 의 [name] 섹션 본문 범위 [b, e) 를 찾는다 (헤더 줄 다음 줄 ~ 다음 [섹션] 헤더 직전 / 파일 끝). name 은 대문자로 전달, 대소문자 무시.
    // 섹션이 없으면 false.
    static bool findSection(const std::string& content, const std::string& name, size_t& b, size_t& e)
    {
        bool inSec = false;
        size_t pos = content.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0; // UTF-8 BOM
        while (pos < content.size()) {
            const size_t nl = content.find('\n', pos);
            const size_t lineEnd = nl == std::string::npos ? content.size() : nl + 1;
            const std::string t = StrUtil::trim(content.substr(pos, lineEnd - pos));
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                if (inSec) { e = pos; return true; }
                if (StrUtil::upper(StrUtil::trim(t.substr(1, t.size() - 2))) == name) { inSec = true; b = lineEnd; }
            }
            pos = lineEnd;
        }
        if (inSec) e = content.size();
        return inSec;
    }

    enum class IniAdd { Added, Exists, Error };

    // INI 의 [Telegram] 섹션에서 key(대문자로 전달, 비교는 대소문자 무시)를 처리한다.
    //  replace=true : 있으면 값만 newVal 로 바꾸고(값 뒤 인라인 주석은 유지), 없으면 섹션 끝에 추가한다 -> Added (TELEGRAM_INTERVAL 갱신용)
    //  replace=false: 이미 있으면 아무것도 바꾸지 않고 Exists, 없으면 섹션 끝에 추가한다 -> Added (/ini 용)
    // 다른 줄, 줄바꿈(CRLF/LF), BOM 은 그대로 유지한다. [Telegram] 섹션이 없으면 파일 끝에 만든다.
    // 임시 파일에 먼저 쓴 뒤 교체하므로 쓰다 실패해도 원본은 보존된다.
    static IniAdd setIniKey(const std::string& path, const std::string& key, const std::string& newVal, bool replace)
    {
        std::string content;
        if (!readFile(path, content)) return IniAdd::Error;
        const std::string eol = (content.empty() || content.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
        size_t b = 0, e = 0;
        if (!findSection(content, "TELEGRAM", b, e)) {
            if (!content.empty() && content.back() != '\n') content += eol;
            content += "[Telegram]" + eol;
            b = e = content.size();
        }
        const std::string sec = content.substr(b, e - b);

        std::string out;
        out.reserve(sec.size() + 64);
        bool found = false;
        size_t pos = 0;
        while (pos < sec.size()) {
            const size_t nl = sec.find('\n', pos);
            const size_t lineEnd = nl == std::string::npos ? sec.size() : nl + 1;
            std::string line = sec.substr(pos, lineEnd - pos);
            pos = lineEnd;

            if (!found) {
                size_t bodyLen = line.size();
                while (bodyLen > 0 && (line[bodyLen - 1] == '\n' || line[bodyLen - 1] == '\r')) --bodyLen;
                const std::string text = line.substr(0, bodyLen);
                const std::string term = line.substr(bodyLen);

                size_t s = 0;
                while (s < text.size() && (unsigned char)text[s] <= ' ') ++s;
                if (s < text.size() && text[s] != ';' && text[s] != '#' && text[s] != '[') {
                    const size_t eq = text.find('=', s);
                    if (eq != std::string::npos &&
                        StrUtil::upper(StrUtil::trim(text.substr(s, eq - s))) == key) {
                        if (!replace) return IniAdd::Exists;
                        // 값 뒤 인라인 주석("공백 + ; 또는 #")은 유지 (Config::load 와 같은 규칙)
                        size_t cmt = std::string::npos;
                        for (size_t i = eq + 2; i < text.size(); ++i) {
                            if ((text[i] == ';' || text[i] == '#') && (unsigned char)text[i - 1] <= ' ') {
                                cmt = i;
                                break;
                            }
                        }
                        std::string tail;
                        if (cmt != std::string::npos) {
                            size_t ws = cmt;
                            while (ws > eq + 1 && (unsigned char)text[ws - 1] <= ' ') --ws;
                            tail = text.substr(ws);
                        }
                        line = text.substr(0, eq + 1) + newVal + tail + term;
                        found = true;
                    }
                }
            }
            out += line;
        }
        if (!found) { // 섹션의 마지막 내용 줄 바로 뒤(뒤쪽 빈 줄 앞)에 추가
            size_t keep = out.size();
            while (keep > 0 && (unsigned char)out[keep - 1] <= ' ') --keep;
            const size_t nl = out.find('\n', keep);
            const size_t ins = nl == std::string::npos ? out.size() : nl + 1;
            std::string head = out.substr(0, ins);
            const std::string rest = out.substr(ins);
            if (!head.empty() && head.back() != '\n') head += eol;
            out = head + key + "=" + newVal + eol + rest;
        }

        return replaceFileAtomic(path, content.substr(0, b) + out + content.substr(e)) ? IniAdd::Added : IniAdd::Error;
    }

    // [Telegram] 섹션의 key(대문자) 값을 바꾼다 (없으면 추가). /INTERVAL=nnn 이 TELEGRAM_INTERVAL 을 저장할 때 사용.
    static bool updateIniValue(const std::string& path, const std::string& key, const std::string& newVal)
    {
        return setIniKey(path, key, newVal, true) == IniAdd::Added;
    }

    // [Telegram] 섹션 끝에 "KEY=값" 줄을 추가한다. 같은 키가 이미 있으면 아무것도 바꾸지 않고 Exists 를 돌려준다. (/ini 용)
    static IniAdd addIniKey(const std::string& path, const std::string& key, const std::string& val)
    {
        return setIniKey(path, key, val, false);
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

// ================================================================ 통합용 도우미
// 설정 파일: exe 와 같은 폴더의 gsHomeManager.ini 의 [Telegram] 섹션
static std::string iniFilePath()
{
    return SysUtil::exeDir() + "\\gsHomeManager.ini";
}

// 고DPI 환경에서도 실제 해상도로 캡처하도록 "이 스레드만" DPI 인식으로 만든다 (Windows 10 1607+).
// 프로세스 전체를 바꾸면 시계 창의 크기/좌표 계산이 달라지므로 스레드 단위로만 적용한다.
static void enableThreadDpiAwareness()
{
    typedef HANDLE(WINAPI * PFN_SetThreadDpiAwarenessContext)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN_SetThreadDpiAwarenessContext f =
        u ? (PFN_SetThreadDpiAwarenessContext)GetProcAddress(u, "SetThreadDpiAwarenessContext") : nullptr;
    if (f) f((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

// 같은 프로세스의 WiFiScan 감시(HomeWatcher)가 /HomeBack, /NotHome 을 넘기는 큐.
// (예전에는 WiFiScan.exe 가 trigger.txt 에 쓰고 screen_telegram.exe 가 1초마다 읽었다. 텔레그램을 거치지 않는 이유:
//  봇이 sendMessage 로 보낸 글은 봇 자신이 getUpdates 로 받지 못한다.)
// 시작 직후 감시가 먼저 넘긴 첫 판정도 큐에 남아 있다가 처리된다 (오래된 것은 처리할 때 나이 검사로 버려진다).
class LocalTrigger {
public:
    struct Item {
        std::string cmd;   // 소문자, '/' 포함 (예: "/homeback")
        ULONGLONG tick;    // 넣은 시각 (GetTickCount64)
    };

    static void post(const std::string& cmd)
    {
        std::string c = StrUtil::trim(cmd);
        for (auto& ch : c) ch = (char)tolower((unsigned char)ch);
        if (!c.empty() && c[0] != '/') c = "/" + c;
        std::lock_guard<std::mutex> g(mu());
        q().push_back(Item{c, GetTickCount64()});
        if (q().size() > 20) q().erase(q().begin()); // 소비하는 쪽이 없어도 무한히 쌓이지 않게
    }

    // 가장 오래된 것부터 하나 꺼낸다. 없으면 false.
    static bool take(Item& out)
    {
        std::lock_guard<std::mutex> g(mu());
        if (q().empty()) return false;
        out = q().front();
        q().erase(q().begin());
        return true;
    }

    // 지우지 않고 가장 최근 것을 본다. 없으면 false.
    static bool latest(Item& out)
    {
        std::lock_guard<std::mutex> g(mu());
        if (q().empty()) return false;
        out = q().back();
        return true;
    }

private:
    static std::mutex& mu() { static std::mutex m; return m; }
    static std::vector<Item>& q() { static std::vector<Item> v; return v; }
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

    bool localTrigger = true;         // LOCAL_TRIGGER=TRUE (WiFiScan 감시의 /HomeBack /NotHome 받기)
    bool menu = true;                 // TELEGRAM_MENU=TRUE (시작할 때 텔레그램 명령 메뉴 등록)
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

    // gsHomeManager.ini 의 [Telegram] 섹션을 읽는다. 파일을 열 수 없으면 false. (섹션이 없으면 기본값 = 토큰 없음)
    bool load(const std::string& path)
    {
        std::string content;
        if (!SysUtil::readFile(path, content)) return false;
        size_t secB = 0, secE = 0;
        if (SysUtil::findSection(content, "TELEGRAM", secB, secE)) content = content.substr(secB, secE - secB); // gsHomeManager.ini 의 [Telegram] 섹션만 읽는다
        else content.clear();
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
            "TELEGRAM_ALLOWED_USER_IDS", "TELEGRAM_GREETING", "TELEGRAM_MENU", "LOCAL_TRIGGER", "SAVE_ONLY", "MAX_LOOP",
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
        else if (key == "LOCAL_TRIGGER") {
            localTrigger = StrUtil::parseBool(val, true);
        }
        else if (key == "TELEGRAM_MENU") {
            menu = StrUtil::parseBool(val, true);
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

    // 명령 메뉴 등록 (setMyCommands). json 은 {"commands":[{"command":"..","description":".."},..]} (UTF-8)
    bool setMyCommands(const std::string& json, std::string& resp) const
    {
        return post(L"setMyCommands", "application/json", json, resp);
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

    // 메시지를 채팅창 상단에 고정한다 (알림 없음)
    bool pinMessage(const std::string& messageId, std::string& resp) const
    {
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";
        std::string body;
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "message_id", messageId);
        addField(body, boundary, "disable_notification", "true");
        body += "--" + boundary + "--\r\n";
        return post(L"pinChatMessage", "multipart/form-data; boundary=" + boundary, body, resp);
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

// /restart 요청 플래그: WinMain 이 app.run() 이 끝난 뒤 같은 exe 를 다시 실행한다.
static volatile LONG g_restartRequested = 0;

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

    // 종료 요청 (바로 반환). run() 이 끝나길 기다리는 것은 호출한 쪽 몫.
    void requestStop() { stop_.request(); }

    int run()
    {
        enableThreadDpiAwareness(); // 고DPI 환경에서도 실제 해상도로 캡처 (이 스레드만)

        Logger::log("==================== 프로그램 시작 ====================");
        Logger::log("Telegram 모듈 시작: %s v%s (빌드 %s %s), PID %lu", APP_NAME, APP_VERSION_STR, __DATE__, __TIME__,
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
            Logger::log("%s v%s, PID %lu", APP_NAME, APP_VERSION_STR, (unsigned long)GetCurrentProcessId());
            Logger::log("PREVIOUS_SESSION_CLEAR=TRUE: log.txt %s, image 폴더 파일 %zu개 삭제%s",
                        logOk ? "삭제" : "삭제 실패", n,
                        failed ? (", 삭제 실패 " + std::to_string(failed) + "개").c_str() : "");
        }
        if (cfg_.token.empty() || cfg_.chatId.empty()) {
            if (!cfg_.saveOnly) {
                Logger::log("gsHomeManager.ini [Telegram] 에 TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 없어 텔레그램 기능은 사용하지 않습니다.");
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
            Logger::log("텔레그램 명령 수신 활성: /shot /screen /photo /pause /resume /status /detectOn /detectOff /INTERVAL=nnn /ini /log /CMD /version /restart /stop /help");
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
        if (cfg_.localTrigger && !cfg_.token.empty() && !cfg_.chatId.empty()) startTrigger(telegram); // WiFiScan 감시의 /HomeBack /NotHome 수신
        if (listener) registerMenu(telegram); // 텔레그램 입력창 "메뉴" 등록 (TELEGRAM_MENU=FALSE 로 끌 수 있음)
        if (listener) updateDetectBar(telegram, initialDetectState()); // 채팅창 상단 고정: 현재 상태(/detectOn 또는 /detectOff)
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
        stopTrigger();
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

    // ---- 로컬 트리거 (LocalTrigger 큐) ----
    // 같은 프로세스의 WiFiScan 감시(HomeWatcher)가 텔레그램을 거치지 않고 /HomeBack, /NotHome 을 전달하는 통로.
    // (봇이 sendMessage 로 보낸 글은 봇 자신이 getUpdates 로 받지 못해서 텔레그램 경유로는 동작하지 않는다.)
    // 허용 명령은 /homeback, /nothome 두 개뿐이다 (다른 명령은 이 통로로 실행되지 않는다).
    struct TriggerCtx {
        ScreenTelegramApp* app = nullptr;
        const TelegramClient* telegram = nullptr;
    };

    static DWORD WINAPI triggerThunk(LPVOID p)
    {
        auto* c = static_cast<TriggerCtx*>(p);
        c->app->triggerLoop(*c->telegram);
        return 0;
    }

    void startTrigger(const TelegramClient& telegram)
    {
        tctx_.app = this;
        tctx_.telegram = &telegram;
        trigger_ = CreateThread(nullptr, 0, triggerThunk, &tctx_, 0, nullptr);
        if (!trigger_) Logger::log("로컬 트리거 스레드 생성 실패 - WiFiScan 감시의 /HomeBack /NotHome 을 처리할 수 없습니다.");
    }

    void stopTrigger()
    {
        if (!trigger_) return;
        if (WaitForSingleObject(trigger_, 20000) == WAIT_TIMEOUT) { // 명령 처리(네트워크) 중이면 끝날 때까지 잠시 대기
            Logger::log("로컬 트리거 스레드 종료 지연 - 강제 종료");
            TerminateThread(trigger_, 0);
        }
        CloseHandle(trigger_);
        trigger_ = nullptr;
    }

    void triggerLoop(const TelegramClient& telegram)
    {
        // 시작할 때 이미 큐에 있는 트리거(부팅 직후 WiFiScan 이 먼저 넘긴 첫 판정)도 처리한다. 오래된 것은 아래 나이 검사에서 버려진다.
        Logger::log("로컬 트리거 수신 시작 (WiFiScan 감시 -> 텔레그램 명령, 허용: /HomeBack /NotHome, %lld초 안의 것만)", kTriggerMaxAgeSec);
        while (!stop_.wait(1000)) {
            LocalTrigger::Item it;
            while (!stop_.requested() && LocalTrigger::take(it)) {
                const long long ageSec = (long long)((GetTickCount64() - it.tick) / 1000);
                if (ageSec > kTriggerMaxAgeSec) {
                    Logger::log("로컬 트리거 무시: 오래된 요청 (%lld초 전) %s", ageSec, it.cmd.c_str());
                    continue;
                }
                if (it.cmd != "/homeback" && it.cmd != "/nothome") {
                    Logger::log("로컬 트리거 무시: 허용되지 않은 내용 (%.40s)", it.cmd.c_str());
                    continue;
                }
                Logger::log("로컬 트리거 수신: %s", it.cmd.c_str());
                CsLock lk(cmdCs_);
                handleCommand(it.cmd, "", telegram);
            }
        }
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

    // 설정 파일: gsHomeManager.ini 의 [Telegram] 섹션 (명령줄 인자는 사용하지 않는다)
    static std::string resolveIniPath()
    {
        return iniFilePath();
    }

    // 시작 준비가 끝났음을 텔레그램으로 알린다 (프로그램 시작당 1회). 부팅 직후 네트워크가 늦을 수 있어 최대 3회 시도.
    // 텔레그램 입력창의 "메뉴"(/ 명령 목록)에 표시할 명령을 등록한다 (시작할 때 1회).
    // 명령 이름은 소문자/숫자/_ 만 허용되므로 소문자로 등록한다. 이 프로그램은 명령을 대소문자 구분 없이 처리하므로
    // 메뉴에서 누른 /homeback 도 /HomeBack 과 똑같이 동작한다. 실패해도 프로그램 동작에는 영향 없음.
    // 채팅창 상단 고정 메시지: 현재 침입 탐지 상태 하나만 보여 준다. 켜짐 "❤️ /detectOn", 꺼짐 "💙 /detectOff".
    // 상태가 바뀔 때마다 같은 메시지를 고쳐 쓴다. 메시지 id 는 exe 폴더의 pinned.id 에 저장한다.
    // 메시지가 없으면(지웠거나 처음) 새로 보내서 고정한다. 안 나오면 pinned.id 를 지우고 재시작.
    void updateDetectBar(const TelegramClient& telegram, bool on)
    {
        if (cfg_.token.empty() || cfg_.chatId.empty()) return;
        CsLock lk(pinCs_);
        // 카메라가 없거나 CAMERA=FALSE 이면 침입 탐지를 쓸 수 없으므로 상태 대신 안내를 보여 준다.
        const std::string text = !camera_ ? "\xE2\x9D\xA4\xEF\xB8\x8F \xEC\xB9\xB4\xEB\xA9\x94\xEB\x9D\xBC\xEA\xB0\x80 \xEC\x9E\x91\xEB\x8F\x99\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"
                                          : on ? "\xE2\x9D\xA4\xEF\xB8\x8F /detectOn" : "\xF0\x9F\x92\x99 /detectOff";
        const std::string idFile = SysUtil::exeDir() + "\\pinned.id";
        std::string oldId;
        {
            std::ifstream f(idFile, std::ios::binary);
            std::getline(f, oldId);
            while (!oldId.empty() && !isdigit((unsigned char)oldId.back())) oldId.pop_back();
        }
        std::string resp;
        if (!oldId.empty()) {      // 고쳐 쓰기: 성공 또는 "not modified"(이미 같은 내용) 이면 메시지가 남아 있다는 뜻
            const bool ok = telegram.editMessageText(oldId, text, resp);
            if (ok || resp.find("not modified") != std::string::npos) {
                Logger::log("상단 고정 메시지: %s", !camera_ ? "카메라 없음" : on ? "detectOn" : "detectOff");
                return;
            }
        }
        if (!telegram.sendMessage(text, resp)) { Logger::log("상단 고정 메시지 전송 실패: %s", resp.substr(0, 120).c_str()); return; }
        const std::string id = TelegramClient::extractMessageId(resp);
        if (id.empty()) return;
        std::string r2;
        if (!telegram.pinMessage(id, r2)) {
            Logger::log("상단 고정 실패 (그룹이면 봇에게 '메시지 고정' 권한 필요): %s", r2.substr(0, 120).c_str());
            return;
        }
        SysUtil::writeFile(idFile, std::vector<BYTE>(id.begin(), id.end()));
        Logger::log("상단 고정 메시지 생성: %s (id %s)", !camera_ ? "카메라 없음" : on ? "detectOn" : "detectOff", id.c_str());
    }

    // 시작할 때 처음 보여 줄 상태: 큐에 최근 트리거가 있으면 그 내용(/NotHome -> 켜짐, /HomeBack -> 꺼짐)을 따르고,
    // 없으면 실제 침입 탐지 상태를 따른다. (큐는 비우지 않는다. 꺼내서 처리하는 건 트리거 스레드)
    bool initialDetectState() const
    {
        LocalTrigger::Item it;
        if (LocalTrigger::latest(it) && (long long)((GetTickCount64() - it.tick) / 1000) <= kTriggerMaxAgeSec) {
            if (it.cmd == "/nothome") return true;
            if (it.cmd == "/homeback") return false;
        }
        return detectOn_ != 0;
    }

    void registerMenu(const TelegramClient& telegram)
    {
        if (!cfg_.menu || cfg_.token.empty()) return;
        static const char* const kMenu[][2] = {
            {"shot", "📸 지금 화면+카메라 캡처해서 전송"},
            {"screen", "🖥️ 지금 화면만 캡처해서 전송"},
            {"photo", "📷 지금 카메라만 캡처해서 전송"},
            {"pause", "⏸️ 이미지 전송 중단 (저장은 계속)"},
            {"resume", "▶️ 이미지 전송 재개"},
            {"status", "ℹ️ 상태 확인"},
            {"detecton", "🚨 침입 탐지 켜기"},
            {"detectoff", "🔕 침입 탐지 끄기"},
            {"interval", "⏱️ 캡처 간격 확인 (변경: /INTERVAL=초)"},
            {"ini", "📝 INI 에 새 키 추가 (/ini 키=값)"},
            {"log", "📜 실행 로그 전송 (/log [n|all])"},
            {"cmd", "💻 PC 에서 명령 실행 (/CMD 명령)"},
            {"download", "📥 PC 에서 파일을 골라 전송"},
            {"github", "🌐 PC 브라우저로 GitHub ReadMe 열기"},
            {"homeback", "🏠 귀가 인사 + 침입 탐지 끄기"},
            {"nothome", "🚪 외출 인사 + 침입 탐지 켜기"},
            {"version", "🔖 프로그램 버전 표시"},
            {"restart", "🔄 프로그램 재시작 (Yes/No 확인)"},
            {"stop", "🛑 프로그램 종료 (Yes/No 확인)"},
            {"help", "❓ 도움말"},
        };
        std::string json = "{\"commands\":[";
        int n = 0;
        for (const auto& m : kMenu) {
            if (n++) json += ",";
            json += std::string("{\"command\":\"") + m[0] + "\",\"description\":\"" + m[1] + "\"}";
        }
        json += "]}";
        std::string resp;
        if (telegram.setMyCommands(json, resp)) Logger::log("텔레그램 명령 메뉴 등록: %d개", n);
        else Logger::log("텔레그램 명령 메뉴 등록 실패: %s", resp.substr(0, 150).c_str());
    }

    void sendGreeting(const TelegramClient& telegram)
    {
        if (!cfg_.greeting || cfg_.token.empty() || cfg_.chatId.empty()) return;

        const std::string host = SysUtil::hostName();
        std::string text = "안녕하세요! [" + host + "] " + APP_NAME + " v" + APP_VERSION_STR + " 준비가 완료되었습니다.\n" +
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
                CsLock lk(cmdCs_); // 로컬 트리거 스레드의 명령 처리와 겹치지 않게 순서대로 처리
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
            // 텔레그램 글자는 색을 지정할 수 없어 색깔 원 이모지로 표시: [detectOn] 빨강, [detectOff] 파랑
            const std::string tag = on ? "\xF0\x9F\x94\xB4 [detectOn]\n" : "\xF0\x9F\x94\xB5 [detectOff]\n";
            if (!camera_) {
                telegram.sendMessage(tag + "[" + host + "] 카메라를 사용할 수 없어 침입 탐지를 켜거나 끌 수 없습니다. "
                                     "(CAMERA=TRUE 설정과 카메라 연결을 확인하세요)", resp);
            } else if ((detectOn_ != 0) == on) {
                telegram.sendMessage(tag + "[" + host + "] 침입 탐지는 이미 " + (on ? "켜져" : "꺼져") + " 있습니다.", resp);
            } else {
                const bool saved = applyDetect(on);
                updateDetectBar(telegram, on); // 상단 고정 메시지도 현재 상태로
                std::string msg = tag + "[" + host + "] 침입 탐지를 " + (on ? "켰습니다." : "껐습니다.");
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
            telegram.sendMessage("[" + host + "] " + APP_NAME + " v" + APP_VERSION_STR +
                                 "\n빌드: " + __DATE__ + " " + __TIME__, resp);
        } else if (cmd == "/log") {
            handleLogCmd(arg, host, telegram);
        } else if (cmd == "/homeback") { // 명령은 소문자로 정규화되어 들어온다 (/HomeBack -> /homeback)
            telegram.sendMessage("\xF0\x9F\x92\x99\xF0\x9F\x92\x99 [집에 돌아 오신 것을 환영합니다. 푹 쉬세요] \xF0\x9F\x92\x99\xF0\x9F\x92\x99", resp);   // 파란 하트 2개씩
            Logger::log("/HomeBack: 귀가 인사 전송 -> /detectOff 자동 처리");
            handleCommand("/detectoff", "", telegram); // 인사말 뒤에 침입 탐지 끄기 (카메라 없음/이미 꺼짐이면 그 안내 메시지가 나감)
            handleCommand("/pause", "", telegram);     // 집에 왔으니 이미지 전송도 중단
        } else if (cmd == "/nothome") { // /NotHome -> /nothome
            telegram.sendMessage("\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x9D\xA4\xEF\xB8\x8F 안녕히 다녀오세요. 제가 집을 잘 지키고 있겠습니다. \xE2\x9D\xA4\xEF\xB8\x8F\xE2\x9D\xA4\xEF\xB8\x8F", resp);   // 빨간 하트 2개씩
            Logger::log("/NotHome: 외출 인사 전송 -> /detectOn 자동 처리");
            handleCommand("/detecton", "", telegram); // 인사말 뒤에 침입 탐지 켜기 (카메라 없음/이미 켜짐이면 그 안내 메시지가 나감)
            handleCommand("/resume", "", telegram);   // 외출하니 이미지 전송도 재개
        } else if (cmd == "/github") {
            handleGithub(host, telegram);
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
        } else if (cmd == "/restart") {
            // /stop 처럼 Yes/No 버튼으로 확인한 뒤에만 재시작한다. 버튼 처리는 handleCallback().
            const std::string keyboard =
                "{\"inline_keyboard\":[[{\"text\":\"Yes\",\"callback_data\":\"restart_yes\"},"
                "{\"text\":\"No\",\"callback_data\":\"restart_no\"}]]}";
            if (telegram.sendMessageWithKeyboard("[" + host + "] 재시작하시겠습니까?(y/n)", keyboard, resp)) {
                restartMsgId_ = TelegramClient::extractMessageId(resp);
                restartAskTick_ = GetTickCount64();
            } else {
                Logger::log("재시작 확인 메시지 전송 실패 - 재시작하지 않음: %s", resp.substr(0, 120).c_str());
            }
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
                    "/github - PC 기본 브라우저로 GitHub ReadMe 페이지 열기\n"
                    "/download - PC 에 파일 선택 창을 띄워 고른 파일(여러 개)을 전송\n"
                    "(파일 첨부) - 봇 채팅에 파일을 보내면 PC 의 upload 폴더에 저장 (20MB 까지), 이미지는 창으로 표시, mp4/mp3 는 기본 플레이어로 실행\n"
                    "/version - 버전 표시\n"
                    "/restart - 프로그램 재시작 (Yes/No 확인)\n/stop - 프로그램 종료 (Yes/No 확인)\n/help 또는 // - 도움말";
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

    // /github : PC 의 기본 브라우저로 kGithubUrl 을 연다. (명령 스레드에서 호출)
    void handleGithub(const std::string& host, const TelegramClient& telegram)
    {
        std::string resp;
        const std::string pre = "[" + host + "] ";
        ComScope com; // ShellExecute 는 COM 초기화된 스레드에서 호출하도록 권장
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", StrUtil::toWide(kGithubUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r > 32) {
            Logger::log("/github: 기본 브라우저로 열기 성공");
            telegram.sendMessage(pre + "PC 기본 브라우저로 GitHub 페이지를 열었습니다.\n" + kGithubUrl, resp);
        } else {
            Logger::log("/github: 브라우저 열기 실패 (코드 %lld)", (long long)(INT_PTR)r);
            telegram.sendMessage(pre + "브라우저를 열지 못했습니다 (코드 " + std::to_string((long long)(INT_PTR)r) + "). 잠금 화면이거나 기본 브라우저가 없는지 확인하세요.", resp);
        }
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

    // 프로그램 전체(gsHomeManager) 종료 요청: 본 창(UI 스레드)에 알려 시계 창과 WiFiScan 감시까지 정상 종료하게 한다.
    // (/restart 는 g_restartRequested 를 함께 세워 두면 WinMain 이 종료 뒤 같은 exe 를 다시 실행한다)
    static void requestAppExit()
    {
        ::ScanEvents::Quit();
        Logger::log("프로그램 종료 요청: 본 창에 종료를 알림");
    }

    // 인라인 버튼 클릭 처리 (명령 스레드에서 호출). /stop, /restart 의 Yes/No 확인 버튼이 있다.
    // 현재 대기 중인 확인 메시지의 버튼만 유효하다. 재시작 전의 오래된 메시지나 이미 처리된 메시지,
    // 5분이 지난 메시지의 버튼은 종료로 이어지지 않는다.
    void handleCallback(const BotEvent& ev, const TelegramClient& telegram)
    {
        Logger::log("버튼 수신: %s", ev.cbData.c_str());
        const std::string host = SysUtil::hostName();
        std::string resp;

        if (ev.cbData == "restart_yes" || ev.cbData == "restart_no") { // /restart 확인 버튼
            const bool ryes = (ev.cbData == "restart_yes");
            const bool rvalid = !restartMsgId_.empty() && ev.messageId == restartMsgId_ &&
                                GetTickCount64() - restartAskTick_ < 300000ULL;
            if (!rvalid) {
                telegram.answerCallbackQuery(ev.cbId, "만료된 요청입니다. /restart 를 다시 보내세요.", resp);
                if (!ev.messageId.empty())
                    telegram.editMessageText(ev.messageId, "[" + host + "] 재시작 확인이 만료되었습니다.", resp);
                return;
            }
            restartMsgId_.clear();
            if (ryes) {
                telegram.answerCallbackQuery(ev.cbId, "재시작합니다.", resp);
                telegram.editMessageText(ev.messageId, "[" + host + "] 재시작합니다. 잠시 후 시작 인사가 오면 완료입니다.", resp);
                Logger::log("재시작 확인(Yes) - 재시작합니다.");
                InterlockedExchange(&g_restartRequested, 1);
                stop_.request();   // 텔레그램 모듈 종료
                requestAppExit();  // 프로그램 전체 종료 -> WinMain 이 같은 exe 를 다시 실행
            } else {
                telegram.answerCallbackQuery(ev.cbId, "재시작을 취소했습니다.", resp);
                telegram.editMessageText(ev.messageId, "[" + host + "] 재시작을 취소했습니다.", resp);
                Logger::log("재시작 확인(No) - 재시작 취소");
            }
            return;
        }

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
            Logger::log("종료 확인(Yes) - 프로그램을 종료합니다.");
            stop_.request();   // 텔레그램 모듈 종료
            requestAppExit();  // 프로그램 전체(시계, WiFiScan 감시 포함) 종료
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
    std::string restartMsgId_;       // 대기 중인 /restart 확인 메시지의 id (명령 스레드 전용)
    ULONGLONG restartAskTick_ = 0;   // 그 확인 메시지를 보낸 시각
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
    static constexpr const char* kGithubUrl = "https://github.com/GarpsuKim/gsTop/blob/main/gsHomeManager/README.md"; // /github 가 여는 주소 (통합 후 gsHomeManager 의 README)
    static constexpr long long kTriggerMaxAgeSec = 300;            // 이보다 오래된 트리거는 무시
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
    // 명령 처리 직렬화 (명령 스레드 + 로컬 트리거 스레드). CRITICAL_SECTION 은 같은 스레드에서 다시 잡아도 안전.
    struct CsGuard {
        CRITICAL_SECTION cs;
        CsGuard() { InitializeCriticalSection(&cs); }
        ~CsGuard() { DeleteCriticalSection(&cs); }
        NO_COPY(CsGuard);
    };
    struct CsLock {
        CRITICAL_SECTION* p;
        explicit CsLock(CsGuard& g) : p(&g.cs) { EnterCriticalSection(p); }
        ~CsLock() { LeaveCriticalSection(p); }
        NO_COPY(CsLock);
    };
    CsGuard cmdCs_;
    CsGuard pinCs_;   // 상단 고정 메시지 갱신 직렬화
    HANDLE trigger_ = nullptr;
    TriggerCtx tctx_;
    HANDLE worker_ = nullptr;
    WorkerCtx ctx_;
    CRITICAL_SECTION ioCs_;          // image 폴더 정리 직렬화
    HANDLE intrusionThread_ = nullptr;
    const ImageEncoder* encoder_ = nullptr;      // 침입 탐지 스레드가 사용 (run() 의 지역 객체, 스레드 종료 후 해제)
    const TelegramClient* telegram_ = nullptr;
    const std::string imageDir_ = SysUtil::exeDir() + "\\image";
    const std::string stateFile_ = SysUtil::exeDir() + "\\detect.state"; // 침입 탐지 켜짐/꺼짐 상태 저장
};

// ================================================================ 모듈 창구
// gsHomeManager 의 WinMain 이 쓰는 시작/종료 함수. Telegram 모듈은 전용 스레드 1개(여기서 ScreenTelegramApp::run())로 돌고,
// 그 안에서 다시 명령 수신 / 로컬 트리거 / 침입 탐지 스레드를 만든다. 서로 다른 스레드이므로 UI 와 독립적으로 동작한다.
static ScreenTelegramApp* g_app = nullptr;
static HANDLE g_thread = nullptr;

// WiFiScan 감시(HomeWatcher)가 /HomeBack, /NotHome 을 넘길 때 호출 (어느 스레드에서든 안전)
void PostTrigger(const std::string& cmd)
{
    LocalTrigger::post(cmd);
}

static DWORD WINAPI moduleThread(LPVOID)
{
    try {
        g_app->run();
    } catch (const std::exception& e) {
        Logger::logCrash("치명적 오류: Telegram 모듈 예외 발생 - %s", e.what());
    } catch (...) {
        Logger::logCrash("치명적 오류: Telegram 모듈 알 수 없는 예외 발생");
    }
    return 0;
}

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

// 프로세스 전체에 적용된다 (WinMain 맨 앞에서 한 번 호출)
void InstallCrashHandlers()
{
    SetUnhandledExceptionFilter(crashFilter);
    std::set_terminate(terminateHandler);
}

// gsHomeManager.ini 에 [Telegram] 섹션이 없으면 만든다.
//  - 예전 screen_telegram.ini 가 있으면(exe 폴더, 또는 ..\screen_telegram 폴더) 그 내용을 그대로 가져온다 (원본 파일은 건드리지 않음).
//  - 없으면 빈 토큰과 기본값 견본을 써 넣는다 (토큰을 채우면 다음 실행부터 동작).
static void ensureIniSection(const std::string& ini)
{
    std::string content;
    if (!SysUtil::readFile(ini, content)) return; // gsHomeManager.ini 는 앱이 먼저 만든다
    size_t b = 0, e = 0;
    if (SysUtil::findSection(content, "TELEGRAM", b, e)) return;

    static const char kTemplate[] =
    "; --- 텔레그램 (screen_telegram.cpp 통합) ---------------------------------------------------------\r\n"
    "; 주기적으로 모니터(+웹캠) 화면을 캡처해 텔레그램으로 보내고, 텔레그램 봇 명령(/help 로 목록)으로 PC 를 제어합니다.\r\n"
    "; TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 비어 있으면 텔레그램 기능은 사용하지 않습니다 (SAVE_ONLY=TRUE 이면 저장만 함).\r\n"
    "; 값 뒤에 ' ; 메모' 처럼 공백 + ; 로 주석을 붙일 수 있습니다. 시작할 때만 읽으며, 텔레그램 /INTERVAL=nnn 과 /ini 는 이 섹션을 고칩니다.\r\n"
    "TELEGRAM_BOT_TOKEN=\r\n"
    "TELEGRAM_CHAT_ID=\r\n"
    "; 캡처/전송 간격 (초, 5~86400). 텔레그램 /INTERVAL=nnn 으로도 바꿀 수 있습니다.\r\n"
    "TELEGRAM_INTERVAL=60\r\n"
    "; 캡처할 모니터 (MONITOR1, MONITOR2, ... 미기재 시 TRUE)\r\n"
    "MONITOR1=TRUE\r\n"
    "MONITOR2=TRUE\r\n"
    "; 웹캠 캡처 (기본 FALSE) / 사용할 카메라 번호 (0부터, 목록은 log.txt 에 출력)\r\n"
    "CAMERA=FALSE\r\n"
    "CAMERA_INDEX=0\r\n"
    "; 캡처/전송 반복 횟수 (0 = 무제한)\r\n"
    "MAX_LOOP=0\r\n"
    "; TRUE 이면 이미지를 텔레그램으로 보내지 않고 image 폴더에 저장만 합니다 (/shot 은 전송)\r\n"
    "SAVE_ONLY=FALSE\r\n"
    "; 텔레그램 봇 명령 수신 (/shot /screen /photo /pause /resume /status /detectOn /detectOff /INTERVAL /ini /log /CMD /download /github /HomeBack /NotHome /version /restart /stop /help)\r\n"
    "TELEGRAM_COMMANDS=TRUE\r\n"
    "; 명령을 내릴 수 있는 텔레그램 사용자 ID (쉼표로 구분). 비우면 개인 chat 만 허용, TELEGRAM_CHAT_ID 가 그룹이면 반드시 설정\r\n"
    "TELEGRAM_ALLOWED_USER_IDS=\r\n"
    "; image 폴더 자동 정리: N일 지난 이미지 삭제 / 최대 파일 수 (0 = 하지 않음)\r\n"
    "KEEP_DAYS=7\r\n"
    "KEEP_MAX_FILES=5000\r\n"
    "; TRUE 이면 시작할 때 log.txt 와 image 폴더의 모든 파일을 삭제\r\n"
    "PREVIOUS_SESSION_CLEAR=FALSE\r\n"
    "; 폰 -> PC 파일 수신 저장 폴더 (상대 경로는 exe 폴더 기준) / 받은 이미지를 창으로 표시 / 받은 mp4, mp3 를 기본 플레이어로 실행\r\n"
    "UPLOAD_DIR=upload\r\n"
    "UPLOAD_SHOW_IMAGE=TRUE\r\n"
    "UPLOAD_PLAY_MEDIA=TRUE\r\n"
    "; WiFiScan 감시의 /HomeBack, /NotHome 판정을 텔레그램 명령으로 처리 (인사말 + 침입 탐지 끄기/켜기 + 이미지 전송 중단/재개)\r\n"
    "LOCAL_TRIGGER=TRUE\r\n"
    "; 시작할 때 텔레그램 입력창 메뉴(/ 명령 목록) 등록 / 시작 인사말 전송\r\n"
    "TELEGRAM_MENU=TRUE\r\n"
    "TELEGRAM_GREETING=TRUE\r\n"
    "; TRUE 이면 시작할 때 침입 탐지(/detectOn)를 자동으로 켭니다 (CAMERA=TRUE 필요). FALSE 이면 detect.state 의 마지막 상태를 따릅니다.\r\n"
    "AUTO_DETECT=FALSE\r\n"
    "; 침입 탐지(카메라 모션 감지) 조정: 픽셀 밝기 변화 임계값(1~255) / 변화 면적 %(화면의 몇 % 이상) / 연속 확인 프레임 / 재탐지 대기(초) / 검사 간격(ms, 200~10000)\r\n"
    "INTRUSION_PIXEL_THRESHOLD=25\r\n"
    "INTRUSION_AREA_PERCENT=1.0\r\n"
    "INTRUSION_CONFIRM_FRAMES=2\r\n"
    "INTRUSION_COOLDOWN=60\r\n"
    "INTRUSION_CHECK_MS=1000\r\n"
    "; TRUE 이면 카메라를 계속 열어 둡니다 (지연/LED 깜빡임은 줄지만 다른 앱이 카메라를 쓸 수 없음)\r\n"
    "CAMERA_KEEP_OPEN=FALSE\r\n";

    std::string add, from;
    const std::string dir = SysUtil::exeDir();
    const std::string cands[] = { dir + "\\screen_telegram.ini", dir + "\\..\\screen_telegram\\screen_telegram.ini" };
    std::string legacy;
    for (const auto& c : cands) {
        if (SysUtil::readFile(c, legacy) && !legacy.empty()) { from = c; break; }
    }
    if (!from.empty()) {
        std::istringstream f(legacy);
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first) { // UTF-8 BOM
                first = false;
                if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            }
            const std::string t = StrUtil::trim(line);
            if (!t.empty() && t[0] == '[') continue; // 옛 INI 는 섹션이 없었다. 섹션 줄은 가져오지 않는다
            add += line + "\r\n";
        }
    } else {
        add = kTemplate;
    }

    if (!content.empty() && content.back() != '\n') content += "\r\n";
    content += "\r\n[Telegram]\r\n" + add;
    if (SysUtil::replaceFileAtomic(ini, content)) {
        if (from.empty()) Logger::log("gsHomeManager.ini 에 [Telegram] 섹션을 만들었습니다. TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 를 채우고 다시 시작하세요.");
        else Logger::log("%s 의 설정을 gsHomeManager.ini [Telegram] 섹션으로 가져왔습니다. (원본은 그대로 둠)", from.c_str());
    } else {
        Logger::log("gsHomeManager.ini 에 [Telegram] 섹션을 쓰지 못했습니다.");
    }
}

// Telegram 모듈 시작 (스레드 생성 후 바로 반환). 토큰/chat id 가 없으면 모듈이 스스로 종료하고 나머지 앱에는 영향이 없다.
void Start()
{
    ensureIniSection(iniFilePath());
    g_app = new ScreenTelegramApp();
    g_thread = CreateThread(nullptr, 0, moduleThread, nullptr, 0, nullptr);
    if (!g_thread) {
        Logger::log("Telegram 모듈 스레드 생성 실패 - 텔레그램 기능을 사용할 수 없습니다.");
        delete g_app;
        g_app = nullptr;
    }
}

// 종료 요청 (바로 반환)
void RequestStop()
{
    if (g_app) g_app->requestStop();
}

// 모듈 스레드가 끝나길 최대 ms 기다린다. 끝났으면 true (정리 완료), 아직 도는 중이면 false.
bool WaitStopped(DWORD ms)
{
    if (!g_thread) return true;
    if (WaitForSingleObject(g_thread, ms) != WAIT_OBJECT_0) return false;
    CloseHandle(g_thread);
    g_thread = nullptr;
    delete g_app;
    g_app = nullptr;
    return true;
}

// 텔레그램 /restart 로 재시작이 요청되었는지
bool RestartRequested()
{
    return g_restartRequested != 0;
}

}  // namespace tg

// ---------------------------------------------------------------- 원격 제어 (rdp_server.cpp / rdp_client.cpp 통합)
// 다른 PC 의 화면(모니터마다 JPEG)을 받아 보고 마우스를 조작하는 LAN 용 원격 제어. 서버(조작당하는 PC)와 클라이언트(조작하는 PC)가 모두 이 앱 안에 있다.
// 프로토콜은 예전 rdp_mini 와 같다 (헤더 5바이트 = 종류 1 + 길이 4, 인증 -> 모니터 정보 -> 화면 프레임 / 마우스 입력).
// 설정은 gsHomeManager.ini 의 [RdpServer] / [RdpClient]. 서버는 전용 스레드, 클라이언트는 연결마다 전용 스레드(자기 창 + 메시지 루프).
// TLS 없음: 신뢰하는 내부망에서만 사용할 것.
namespace rdp {

#pragma pack(push, 1)
struct Hdr { uint8_t type; uint32_t len; };
#pragma pack(pop)
struct Ev { int32_t t, a, b, m; };                       // m = 모니터 번호 (마우스 이동)
enum { RM_AUTH = 1, RM_FRAME = 2, RM_INPUT = 3, RM_INFO = 4 };
enum { EV_MOVE = 1, EV_BTN = 2, EV_WHEEL = 3 };

static bool sendAll(SOCKET s, const void* p, int n) {
    const char* c = (const char*)p;
    while (n > 0) { int r = send(s, c, n, 0); if (r <= 0) return false; c += r; n -= r; }
    return true;
}
static bool recvAll(SOCKET s, void* p, int n) {
    char* c = (char*)p;
    while (n > 0) { int r = recv(s, c, n, 0); if (r <= 0) return false; c += r; n -= r; }
    return true;
}
static bool sendMsg(SOCKET s, uint8_t type, const void* p, uint32_t len) {
    Hdr h{type, len};
    return sendAll(s, &h, sizeof h) && (len == 0 || sendAll(s, p, (int)len));
}

// UTF-8 메시지 상자 (어느 스레드에서나 호출 가능)
static int Box(const char* text, const char* title, UINT flags) {
    char16_t t[600], c[100];
    t[0] = c[0] = 0;
    Utf8::ToUtf16(text, -1, t, 600);
    Utf8::ToUtf16(title, -1, c, 100);
    return MessageBoxW(NULL, (LPCWSTR)t, (LPCWSTR)c, flags | MB_SETFOREGROUND | MB_TOPMOST);
}

// 이 PC 의 IPv4 주소들 ("a.b.c.d,  e.f.g.h")
static std::string LocalIps() {
    std::string out;
    ULONG sz = 0;
    GetAdaptersInfo(NULL, &sz);
    if (!sz) return out;
    std::vector<char> buf(sz);
    IP_ADAPTER_INFO* ai = (IP_ADAPTER_INFO*)buf.data();
    if (GetAdaptersInfo(ai, &sz) != NO_ERROR) return out;
    for (; ai; ai = ai->Next)
        for (IP_ADDR_STRING* a = &ai->IpAddressList; a; a = a->Next) {
            const char* ip = a->IpAddress.String;
            if (!ip[0] || !lstrcmpA(ip, "0.0.0.0")) continue;
            if (!out.empty()) out += ",  ";
            out += ip;
        }
    return out;
}

// ================================================================ 서버 (조작당하는 PC)
class Server {
public:
    enum { ST_STOPPED, ST_LISTEN, ST_ASK, ST_CONN };
    struct Cfg {
        int port = 7071;
        std::string pw;
        std::string allowIp;        // 빈 문자열 = 모든 IP 허용
        bool osk = false;           // 연결되면 화상 키보드(osk.exe) 실행
        int accept = 0;             // 0 = 클라이언트 IP 마다 처음 한 번만 묻기, 1 = 묻지 않음(자동 수락), 2 = 접속할 때마다 묻기
    };

    static Server& Get() { static Server s; return s; }
    ~Server() { Stop(); }

    bool Running() const { return th_.joinable(); }

    // 포트를 열고 대기 스레드를 시작. 실패하면 err 에 사유
    bool Start(const Cfg& c, std::string& err) {
        if (th_.joinable()) Stop();
        if (c.port < 1 || c.port > 65535) { err = "port 번호는 1 ~ 65535 사이여야 합니다"; return false; }
        if (c.pw.empty()) { err = "비밀번호가 비어 있습니다"; return false; }
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((u_short)c.port);
        if (ls != INVALID_SOCKET) setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof one);
        if (ls == INVALID_SOCKET || bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 1) != 0) {
            char m[200];
            wsprintfA(m, "port %d 를 열 수 없습니다 (WSA 오류 %d). 이미 사용 중인 port 인지 확인하세요", c.port, WSAGetLastError());
            if (ls != INVALID_SOCKET) closesocket(ls);
            WSACleanup();
            err = m;
            Log::Put("원격 서버: %s", m);
            return false;
        }
        { std::lock_guard<std::mutex> g(mtx_); cfg_ = c; }
        ls_ = ls;
        stop_ = false;
        done_ = false;
        frames_ = 0;
        trusted_.clear();
        SetState(ST_LISTEN, "");
        th_ = std::thread([this] { Run(); });
        Log::Put("원격 서버 시작: port %d, 허용 IP %s, 연결 확인 %s", c.port, c.allowIp.empty() ? "any" : c.allowIp.c_str(),
                 c.accept == 1 ? "auto" : c.accept == 2 ? "always" : "once");
        return true;
    }

    void Stop() {
        if (!th_.joinable()) return;
        stop_ = true;
        SOCKET ls = ls_.exchange(INVALID_SOCKET);
        if (ls != INVALID_SOCKET) closesocket(ls);                      // accept 를 깨움
        SOCKET c = cur_;
        if (c != INVALID_SOCKET) shutdown(c, SD_BOTH);                  // 진행 중인 세션/인증 대기를 깨움
        for (int i = 0; i < 300 && !done_; i++) {                       // 허용 여부 질문창이 떠 있으면 No 로 닫음
            HWND w = FindWindowW((LPCWSTR)u"#32770", (LPCWSTR)kAskTitle16);
            if (w) PostMessageW(w, WM_COMMAND, IDNO, 0);
            Sleep(30);
        }
        th_.join();
        WSACleanup();
        SetState(ST_STOPPED, "");
        Log::Put("원격 서버 중지");
    }

    // 대화상자에 보여 줄 현재 상태
    std::string Status() {
        std::lock_guard<std::mutex> g(mtx_);
        char b[240];
        switch (state_) {
        case ST_LISTEN: wsprintfA(b, "대기 중 (port %d) - 클라이언트의 연결을 기다립니다", cfg_.port); break;
        case ST_ASK:    wsprintfA(b, "%s 에서 연결 요청 - 허용 여부를 묻는 중", peer_.c_str()); break;
        case ST_CONN:   wsprintfA(b, "연결됨: %s  (보낸 화면 %lu 장)", peer_.c_str(), (unsigned long)frames_); break;
        default:        return "중지됨";
        }
        return b;
    }
    // 실행 중인 서버에 설정 반영 (비밀번호, 허용 IP, 연결 확인, 화상 키보드). port 는 다시 시작해야 바뀜. 다음 접속부터 적용
    void Update(const Cfg& c) {
        std::lock_guard<std::mutex> g(mtx_);
        const int port = cfg_.port;
        cfg_ = c;
        cfg_.port = port;
    }
    int Port() { std::lock_guard<std::mutex> g(mtx_); return cfg_.port; }
    bool Connected() { std::lock_guard<std::mutex> g(mtx_); return state_ == ST_CONN; }

private:
    Server() = default;
    static constexpr char16_t kAskTitle16[] = u"gsHomeManager - 원격 연결 요청";
    static constexpr const char* kAskTitle = "gsHomeManager - 원격 연결 요청";

    Cfg Snap() { std::lock_guard<std::mutex> g(mtx_); return cfg_; }

    void SetState(int st, const std::string& peer) {
        std::lock_guard<std::mutex> g(mtx_);
        state_ = st;
        peer_ = peer;
    }

    static bool JpegClsid(CLSID* out) {
        UINT n = 0, sz = 0;
        Gdiplus::GetImageEncodersSize(&n, &sz);
        if (!sz) return false;
        std::vector<char> buf(sz);
        auto* ci = (Gdiplus::ImageCodecInfo*)buf.data();
        Gdiplus::GetImageEncoders(n, sz, ci);
        for (UINT i = 0; i < n; i++)
            if (!wcscmp(ci[i].MimeType, L"image/jpeg")) { *out = ci[i].Clsid; return true; }
        return false;
    }

    // 화상 키보드 실행 (C:\Windows\System32\osk.exe). 32비트 프로세스는 System32 가 SysWOW64 로 바뀌므로 Sysnative 로 접근
    static void RunOsk() {
        wchar_t win[MAX_PATH], dir[MAX_PATH + 16], exe[MAX_PATH + 32];
        if (!GetWindowsDirectoryW(win, MAX_PATH)) return;
        BOOL wow = FALSE;
        IsWow64Process(GetCurrentProcess(), &wow);
        lstrcpyW(dir, win);
        lstrcatW(dir, wow ? L"\\Sysnative" : L"\\System32");
        lstrcpyW(exe, dir);
        lstrcatW(exe, L"\\osk.exe");
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            Log::Put("원격 서버: 화상 키보드(osk.exe) 실행");
            return;
        }
        const int e1 = (int)GetLastError();
        HINSTANCE r = ShellExecuteW(NULL, L"open", exe, NULL, dir, SW_SHOWNORMAL);       // CreateProcess 가 안 되면 셸로 다시 시도
        Log::Put("원격 서버: 화상 키보드(osk.exe) 실행 %s (CreateProcess 오류 %d, ShellExecute 결과 %d)", (INT_PTR)r > 32 ? "성공" : "실패", e1, (int)(INT_PTR)r);
    }

    static BOOL CALLBACK EnumProc(HMONITOR, HDC, LPRECT r, LPARAM lp) {
        ((std::vector<RECT>*)lp)->push_back(*r);
        return TRUE;
    }

    // 클라이언트의 마우스 입력을 이 PC 에 넣음 (a, b = 모니터 m 안의 0..65535 좌표 -> 가상 데스크톱 좌표)
    static void Inject(const Ev& e, const std::vector<RECT>& mons) {
        INPUT in = {};
        switch (e.t) {
        case EV_MOVE: {
            if (e.m < 0 || e.m >= (int)mons.size()) return;
            const RECT& r = mons[e.m];
            int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if (vw < 2 || vh < 2) return;
            int px = r.left + (int)((int64_t)std::clamp(e.a, 0, 65535) * (r.right - r.left - 1) / 65535);
            int py = r.top + (int)((int64_t)std::clamp(e.b, 0, 65535) * (r.bottom - r.top - 1) / 65535);
            in.type = INPUT_MOUSE;
            in.mi.dx = (LONG)((int64_t)(px - vx) * 65535 / (vw - 1));
            in.mi.dy = (LONG)((int64_t)(py - vy) * 65535 / (vh - 1));
            in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            break; }
        case EV_BTN: {
            DWORD f = (DWORD)e.a;
            if (f != MOUSEEVENTF_LEFTDOWN && f != MOUSEEVENTF_LEFTUP && f != MOUSEEVENTF_RIGHTDOWN &&
                f != MOUSEEVENTF_RIGHTUP && f != MOUSEEVENTF_MIDDLEDOWN && f != MOUSEEVENTF_MIDDLEUP) return;
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = f;
            break; }
        case EV_WHEEL:
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = (DWORD)e.a;
            break;
        default: return;
        }
        SendInput(1, &in, sizeof(INPUT));
    }

    // 대기 스레드: 한 번에 클라이언트 한 명
    void Run() {
        tg::enableThreadDpiAwareness();         // 실제 해상도로 캡처 / 좌표 계산 (이 스레드만 DPI 인식. 시계 창에는 영향 없음)
        Gdiplus::GdiplusStartupInput gi;
        ULONG_PTR tok = 0;
        Gdiplus::GdiplusStartup(&tok, &gi, NULL);
        for (;;) {
            SOCKET ls = ls_;
            if (stop_ || ls == INVALID_SOCKET) break;
            sockaddr_in ca = {};
            int cl = sizeof ca;
            SOCKET c = accept(ls, (sockaddr*)&ca, &cl);
            if (stop_) { if (c != INVALID_SOCKET) closesocket(c); break; }
            if (c == INVALID_SOCKET) { Sleep(50); continue; }
            std::string ips = inet_ntoa(ca.sin_addr);
            if (const Cfg cur = Snap(); !cur.allowIp.empty() && cur.allowIp != ips) {
                Log::Put("원격 서버: 허용 IP 가 아니어서 거부: %s", ips.c_str());
                closesocket(c);
                continue;
            }
            int nd = 1;
            setsockopt(c, IPPROTO_TCP, 1 /*TCP_NODELAY*/, (char*)&nd, sizeof nd);
            cur_ = c;
            Serve(c, ips);
            cur_ = INVALID_SOCKET;
            closesocket(c);
            SetState(stop_ ? ST_STOPPED : ST_LISTEN, "");
        }
        Gdiplus::GdiplusShutdown(tok);
        done_ = true;
    }

    void Serve(SOCKET c, const std::string& ips) {
        DWORD to = 5000;                        // 인증 메시지는 5초 안에 와야 함 (연결만 하고 가만히 있는 상대가 서버를 붙잡지 못하게)
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        Hdr h;
        if (!recvAll(c, &h, sizeof h) || h.type != RM_AUTH || h.len > 256) { Log::Put("원격 서버: 잘못된 인증 메시지 (%s)", ips.c_str()); return; }
        std::string got(h.len, '\0');
        if (h.len && !recvAll(c, &got[0], (int)h.len)) { Log::Put("원격 서버: 인증 메시지가 끝까지 오지 않음 (%s)", ips.c_str()); return; }
        to = 0;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);

        const Cfg cfg = Snap();                 // 접속할 때의 설정 (대화상자에서 [저장] 한 최신 값)
        uint8_t ok = (got == cfg.pw);
        SecureZeroMemory(&got[0], got.size());
        if (!ok) {
            Log::Put("원격 서버: 비밀번호가 틀림 (%s)", ips.c_str());
            Sleep(1000);                        // 비밀번호 대입 공격을 늦춤
        } else if (cfg.accept == 1) {
            Log::Put("원격 서버: 비밀번호 확인됨 (%s, 묻지 않고 수락)", ips.c_str());
        } else if (cfg.accept == 2 || !trusted_.count(ips)) {
            SetState(ST_ASK, ips);
            Log::Put("원격 서버: 비밀번호 확인됨 (%s), 사용자에게 허용 여부를 묻는 중", ips.c_str());
            std::string msg = "원격 연결 요청: " + ips + "\n\n허용하시겠습니까?";
            ok = (Box(msg.c_str(), kAskTitle, MB_YESNO | MB_ICONQUESTION) == IDYES) && !stop_;
            Log::Put("원격 서버: 사용자가 %s (%s)", ok ? "허용" : "거부", ips.c_str());
            if (ok) trusted_.insert(ips);
        } else {
            Log::Put("원격 서버: 비밀번호 확인됨 (%s, 이미 허용한 IP)", ips.c_str());
        }
        sendMsg(c, RM_AUTH, &ok, 1);
        if (!ok) return;

        CLSID jpg;
        if (!JpegClsid(&jpg)) { Log::Put("원격 서버: JPEG 인코더를 찾을 수 없어 연결을 끊음"); return; }
        SetState(ST_CONN, ips);
        frames_ = 0;
        Log::Put("원격 서버: 클라이언트 연결 수락: %s", ips.c_str());
        if (cfg.osk) RunOsk();

        std::vector<RECT> gMons;                // 모니터 (왼쪽 -> 오른쪽)
        EnumDisplayMonitors(NULL, NULL, EnumProc, (LPARAM)&gMons);
        std::sort(gMons.begin(), gMons.end(), [](const RECT& a, const RECT& b) {
            return a.left != b.left ? a.left < b.left : a.top < b.top;
        });
        Log::Put("원격 서버: 모니터 %d 개 감지", (int)gMons.size());
        for (size_t i = 0; i < gMons.size(); i++)
            Log::Put("원격 서버:   모니터 %d: %dx%d (%d,%d)", (int)i + 1, (int)(gMons[i].right - gMons[i].left),
                     (int)(gMons[i].bottom - gMons[i].top), (int)gMons[i].left, (int)gMons[i].top);
        {   // 클라이언트에 모니터 개수와 크기를 알림
            std::vector<int32_t> info;
            info.push_back((int32_t)gMons.size());
            for (auto& r : gMons) { info.push_back(r.right - r.left); info.push_back(r.bottom - r.top); }
            sendMsg(c, RM_INFO, info.data(), (uint32_t)(info.size() * 4));
        }

        std::atomic<bool> alive(true);
        std::thread rx([&] {                    // 클라이언트 -> 서버: 마우스 입력
            tg::enableThreadDpiAwareness();
            Hdr h;
            while (recvAll(c, &h, sizeof h)) {
                if (h.len > 1024) break;
                Ev e;
                if (h.type == RM_INPUT && h.len == sizeof e) {
                    if (!recvAll(c, &e, sizeof e)) break;
                    Inject(e, gMons);
                } else {
                    std::vector<char> skip(h.len);
                    if (h.len && !recvAll(c, skip.data(), (int)h.len)) break;
                }
            }
            alive = false;
        });

        struct Mon { int W, H; HDC mem; HBITMAP dib; HGDIOBJ old; void* bits; size_t bytes; std::vector<uint8_t> prev; };
        std::vector<Mon> mons(gMons.size());
        HDC scr = GetDC(NULL);
        for (size_t i = 0; i < mons.size(); i++) {
            Mon& m = mons[i];
            m.W = gMons[i].right - gMons[i].left;
            m.H = gMons[i].bottom - gMons[i].top;
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = m.W;
            bi.bmiHeader.biHeight = -m.H;       // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            m.bits = nullptr;
            m.mem = CreateCompatibleDC(scr);
            m.dib = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &m.bits, NULL, 0);
            m.old = SelectObject(m.mem, m.dib);
            m.bytes = (size_t)m.W * m.H * 4;
            m.prev.assign(m.bytes, 0);
        }
        ULONG quality = 60;
        unsigned long sentFrames = 0, sentBytes = 0;
        while (alive && !stop_) {               // 서버 -> 클라이언트: 바뀐 모니터의 화면만 JPEG 로 전송
            bool sentOk = true;
            for (size_t i = 0; i < mons.size() && sentOk; i++) {
                Mon& m = mons[i];
                if (!m.bits) continue;
                BitBlt(m.mem, 0, 0, m.W, m.H, scr, gMons[i].left, gMons[i].top, SRCCOPY | CAPTUREBLT);
                if (memcmp(m.prev.data(), m.bits, m.bytes) == 0) continue;     // 이 모니터는 그대로
                memcpy(m.prev.data(), m.bits, m.bytes);
                Gdiplus::Bitmap bmp(m.W, m.H, m.W * 4, PixelFormat32bppRGB, (BYTE*)m.bits);
                IStream* st = nullptr;
                CreateStreamOnHGlobal(NULL, TRUE, &st);
                Gdiplus::EncoderParameters ep;
                ep.Count = 1;
                ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                ep.Parameter[0].NumberOfValues = 1;
                ep.Parameter[0].Value = &quality;
                if (bmp.Save(st, &jpg, &ep) == Gdiplus::Ok) {
                    STATSTG ss;
                    st->Stat(&ss, STATFLAG_NONAME);
                    HGLOBAL hg;
                    GetHGlobalFromStream(st, &hg);
                    void* p = GlobalLock(hg);
                    Hdr fh{RM_FRAME, ss.cbSize.LowPart + 4};         // 내용 = 모니터 번호 + JPEG
                    int32_t idx = (int32_t)i;
                    sentOk = sendAll(c, &fh, sizeof fh) && sendAll(c, &idx, 4) && sendAll(c, p, (int)ss.cbSize.LowPart);
                    GlobalUnlock(hg);
                    if (sentOk) { sentFrames++; sentBytes += ss.cbSize.LowPart; frames_++; }
                } else {
                    Log::Put("원격 서버: JPEG 인코딩 실패 (모니터 %d)", (int)i + 1);
                }
                st->Release();
            }
            if (!sentOk) { Log::Put("원격 서버: 전송 실패, 세션을 끝냄"); break; }
            Sleep(60);
        }
        shutdown(c, SD_BOTH);
        rx.join();
        for (Mon& m : mons) {
            SelectObject(m.mem, m.old);
            DeleteObject(m.dib);
            DeleteDC(m.mem);
        }
        ReleaseDC(NULL, scr);
        Log::Put("원격 서버: 클라이언트 연결 종료: %s (보낸 화면 %lu 장, %lu KB)", ips.c_str(), sentFrames, sentBytes / 1024);
    }

    Cfg                     cfg_;
    std::thread             th_;
    std::atomic<SOCKET>     ls_{INVALID_SOCKET};     // 대기 소켓
    std::atomic<SOCKET>     cur_{INVALID_SOCKET};    // 지금 처리 중인 클라이언트 소켓
    std::atomic<bool>       stop_{false};
    std::atomic<bool>       done_{false};            // Run() 이 끝남
    std::atomic<unsigned long> frames_{0};
    std::set<std::string>   trusted_;                // 한 번 허용한 클라이언트 IP (accept=once. 서버를 다시 시작하면 비움)
    std::mutex              mtx_;
    int                     state_ = ST_STOPPED;
    std::string             peer_;
};

// ================================================================ 클라이언트 (조작하는 PC)
class Client {
public:
    struct Cfg { std::string ip; int port = 7071; std::string pw; };

    static constexpr UINT WM_PROGRESS = WM_APP + 10;    // 대화상자로: wParam = 남은 ms, lParam = 단계 (0 서버 접속, 1 서버 승인 대기)
    static constexpr UINT WM_DONE     = WM_APP + 11;    // 대화상자로: wParam = 1 이면 연결 성공 (대화상자를 닫음), 0 이면 실패/취소 (오류는 TakeError)
    static constexpr int CONNECT_SEC = 10, APPROVE_SEC = 60;

    // 연결 시작: 전용 스레드가 접속 -> 인증(진행 상황을 dlg 에 알림) -> 서버 모니터마다 창을 만들고 닫힐 때까지 동작. 이미 연결 중이면 false
    static bool Start(const Cfg& c, HWND dlg) {
        if (busy_) return false;
        if (th_.joinable()) th_.join();
        cancel_ = false;
        dlg_ = dlg;
        { std::lock_guard<std::mutex> g(emtx_); err_.clear(); }
        busy_ = true;
        th_ = std::thread([c] { Session(c); busy_ = false; });
        return true;
    }
    static bool Busy() { return busy_; }
    static void Cancel() { cancel_ = true; }                 // 접속/승인 대기 중인 연결 시도를 취소
    static void Detach() { dlg_ = NULL; }                    // 대화상자가 닫힘: 더 이상 알리지 않음
    static std::string TakeError() {                         // 실패 사유 (한 번 읽으면 비움)
        std::lock_guard<std::mutex> g(emtx_);
        std::string e;
        e.swap(err_);
        return e;
    }

    // 앱 종료: 진행 중인 연결을 닫고 스레드를 기다림
    static void Shutdown() {
        if (!th_.joinable()) return;
        cancel_ = true;
        DWORD t = tid_;
        if (busy_ && t) PostThreadMessageW(t, WM_QUIT, 0, 0);
        th_.join();
    }

private:
    static inline std::thread th_;
    static inline std::atomic<bool> busy_{false};
    static inline std::atomic<bool> cancel_{false};
    static inline std::atomic<DWORD> tid_{0};
    static inline std::atomic<HWND> dlg_{NULL};     // 진행 상황을 알릴 대화상자
    static inline std::mutex emtx_;
    static inline std::string err_;                 // 마지막 실패 사유
    static inline SOCKET sock_ = INVALID_SOCKET;
    static inline std::vector<HWND> wnd_;           // 서버 모니터마다 창 1개
    static inline std::vector<HBITMAP> bmp_;        // 모니터마다 마지막 화면
    static inline std::mutex mtx_;

    static const char* ErrText(int e) {
        switch (e) {
        case WSAETIMEDOUT:     return "응답 없음 (시간 초과)";
        case WSAECONNREFUSED:  return "서버가 실행 중이 아니거나 port 가 틀림";
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH:  return "서버에 도달할 수 없음 (IP 주소 / 네트워크 확인)";
        default:               return "";
        }
    }

    static void Post(UINT m, WPARAM w, LPARAM l = 0) {
        HWND d = dlg_;
        if (d) PostMessageW(d, m, w, l);
    }

    // 실패: 로그에 남기고 사유를 대화상자에 알림 (오류 창은 대화상자가 띄움)
    static void Fail(const char* msg) {
        Log::Put("원격 클라이언트: %s", msg);
        { std::lock_guard<std::mutex> g(emtx_); err_ = msg; }
        Post(WM_DONE, 0);
    }

    // 소켓이 쓰기(연결 완료) / 읽기(서버 응답) 가능해질 때까지 기다리며 대화상자의 진행 표시를 갱신.
    // 실패하면 err = WSA 오류 (사용자가 취소하면 WSAECANCELLED)
    static bool Wait(SOCKET s, bool write, int sec, int phase, int& err) {
        DWORD t0 = GetTickCount();
        for (;;) {
            int left = sec * 1000 - (int)(GetTickCount() - t0);
            if (left <= 0) { err = WSAETIMEDOUT; return false; }
            if (cancel_) { err = WSAECANCELLED; return false; }
            Post(WM_PROGRESS, (WPARAM)left, (LPARAM)phase);
            fd_set rf, wf, ef;
            FD_ZERO(&rf); FD_ZERO(&wf); FD_ZERO(&ef);
            if (write) FD_SET(s, &wf); else FD_SET(s, &rf);
            FD_SET(s, &ef);
            timeval tv = {0, 100000};
            int n = select(0, write ? NULL : &rf, write ? &wf : NULL, &ef, &tv);
            if (n < 0) { err = WSAGetLastError(); return false; }
            if (n > 0) {
                if (FD_ISSET(s, &ef)) {
                    int e = 0, l = sizeof e;
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &l);
                    err = e ? e : WSAECONNREFUSED;
                    return false;
                }
                return true;
            }
        }
    }

    static void Session(const Cfg& c) {
        tid_ = GetCurrentThreadId();
        tg::enableThreadDpiAwareness();
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        Gdiplus::GdiplusStartupInput gi;
        ULONG_PTR tok = 0;
        Gdiplus::GdiplusStartup(&tok, &gi, NULL);
        Run(c);
        Gdiplus::GdiplusShutdown(tok);
        WSACleanup();
        tid_ = 0;
    }

    static void Run(const Cfg& c) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = inet_addr(c.ip.c_str());
        a.sin_port = htons((u_short)c.port);
        Log::Put("원격 클라이언트: %s:%d 에 연결 요청", c.ip.c_str(), c.port);
        char m[300];
        int err = 0;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        bool ok = false;
        if (connect(s, (const sockaddr*)&a, sizeof a) == 0) ok = true;
        else if (WSAGetLastError() != WSAEWOULDBLOCK) err = WSAGetLastError();
        else ok = Wait(s, true, CONNECT_SEC, 0, err);
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        if (!ok) {
            closesocket(s);
            if (err == WSAECANCELLED) { Log::Put("원격 클라이언트: 연결 취소"); Post(WM_DONE, 0); return; }
            wsprintfA(m, "%s:%d 에 연결할 수 없습니다.\n%s (WSA 오류 %d)", c.ip.c_str(), c.port, ErrText(err), err);
            Fail(m);
            return;
        }
        Log::Put("원격 클라이언트: 연결됨, 비밀번호 전송");
        int nd = 1;
        setsockopt(s, IPPROTO_TCP, 1 /*TCP_NODELAY*/, (char*)&nd, sizeof nd);
        sendMsg(s, RM_AUTH, c.pw.data(), (uint32_t)c.pw.size());
        // 서버 사용자가 허용 여부를 누를 때까지 기다림 (최대 APPROVE_SEC 초)
        if (!Wait(s, false, APPROVE_SEC, 1, err)) {
            closesocket(s);
            if (err == WSAECANCELLED) { Log::Put("원격 클라이언트: 승인 대기 취소"); Post(WM_DONE, 0); return; }
            Fail("서버가 60초 안에 응답하지 않았습니다. 서버 PC 에서 연결 요청을 확인하세요.");
            return;
        }
        DWORD to = 10000;                       // 이후 핸드셰이크는 10초 안에
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        Hdr h;
        uint8_t acc = 0;
        if (!recvAll(s, &h, sizeof h) || h.len != 1 || !recvAll(s, &acc, 1) || !acc) {
            closesocket(s);
            Fail("인증에 실패했거나 서버가 연결을 거부했습니다.\n비밀번호와 서버의 허용 여부를 확인하세요.");
            return;
        }
        Log::Put("원격 클라이언트: 인증됨");
        if (!recvAll(s, &h, sizeof h) || h.type != RM_INFO || h.len < 12 || h.len > 4096 || h.len % 4) {
            closesocket(s);
            Fail("서버의 모니터 정보가 올바르지 않습니다.");
            return;
        }
        std::vector<int32_t> info(h.len / 4);
        bool bad = !recvAll(s, info.data(), (int)h.len) || info[0] < 1 || info[0] > 16 || (size_t)info[0] * 2 + 1 > info.size();
        for (int i = 0; !bad && i < info[0]; i++) bad = info[1 + 2 * i] < 1 || info[2 + 2 * i] < 1;
        if (bad) {
            closesocket(s);
            Fail("서버의 모니터 정보가 올바르지 않습니다.");
            return;
        }
        to = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        if (cancel_) { closesocket(s); Post(WM_DONE, 0); return; }
        sock_ = s;
        Post(WM_DONE, 1);                       // 대화상자를 닫고 원격 제어 창을 연다
        View(info);
        shutdown(sock_, SD_BOTH);
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    static int MonOf(HWND w) {
        for (size_t i = 0; i < wnd_.size(); i++) if (wnd_[i] == w) return (int)i;
        return -1;
    }

    static void SendEv(int t, int a, int b = 0, int m = 0) {
        Ev e{t, a, b, m};
        sendMsg(sock_, RM_INPUT, &e, sizeof e);
    }

    // 서버 -> 클라이언트: 화면 프레임 수신
    static void NetThread() {
        Hdr h;
        unsigned long frames = 0;
        while (recvAll(sock_, &h, sizeof h)) {
            if (h.len > (64u << 20)) { Log::Put("원격 클라이언트: 프레임이 너무 큼 (%u 바이트), 연결을 닫음", (unsigned)h.len); break; }
            std::vector<char> buf(h.len);
            if (h.len && !recvAll(sock_, buf.data(), (int)h.len)) break;
            if (h.type != RM_FRAME || h.len <= 4) continue;
            int32_t idx;
            memcpy(&idx, buf.data(), 4);
            if (idx < 0 || idx >= (int)wnd_.size()) continue;
            uint32_t n = h.len - 4;
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n);
            if (!hg) continue;
            memcpy(GlobalLock(hg), buf.data() + 4, n);
            GlobalUnlock(hg);
            IStream* st = nullptr;
            CreateStreamOnHGlobal(hg, TRUE, &st);
            HBITMAP hb = NULL;
            {
                Gdiplus::Bitmap bmp(st);
                bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0), &hb);
            }
            st->Release();
            if (!hb) { Log::Put("원격 클라이언트: 프레임 디코딩 실패 (모니터 %d)", (int)idx + 1); continue; }
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (bmp_[idx]) DeleteObject(bmp_[idx]);
                bmp_[idx] = hb;
            }
            InvalidateRect(wnd_[idx], NULL, FALSE);
            frames++;
        }
        Log::Put("원격 클라이언트: 서버 또는 네트워크가 연결을 끊음 (받은 화면 %lu 장)", frames);
        PostMessageW(wnd_[0], WM_CLOSE, 0, 0);
    }

    static LRESULT CALLBACK ViewProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
        switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            std::lock_guard<std::mutex> lk(mtx_);
            int mi = MonOf(w);
            HBITMAP cur = mi >= 0 ? bmp_[mi] : NULL;
            if (cur) {
                BITMAP bm;
                GetObjectW(cur, sizeof bm, &bm);
                RECT rc;
                GetClientRect(w, &rc);
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ o = SelectObject(mem, cur);
                SetStretchBltMode(dc, HALFTONE);
                StretchBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SelectObject(mem, o);
                DeleteDC(mem);
            }
            EndPaint(w, &ps);
            return 0; }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            RECT rc;
            GetClientRect(w, &rc);
            if (rc.right > 1 && rc.bottom > 1) {
                int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
                SendEv(EV_MOVE, (int)((int64_t)x * 65535 / (rc.right - 1)), (int)((int64_t)y * 65535 / (rc.bottom - 1)), MonOf(w));
            }
            return 0; }
        case WM_LBUTTONDOWN: SetCapture(w); SendEv(EV_BTN, MOUSEEVENTF_LEFTDOWN); return 0;
        case WM_LBUTTONUP:   ReleaseCapture(); SendEv(EV_BTN, MOUSEEVENTF_LEFTUP); return 0;
        case WM_RBUTTONDOWN: SetCapture(w); SendEv(EV_BTN, MOUSEEVENTF_RIGHTDOWN); return 0;
        case WM_RBUTTONUP:   ReleaseCapture(); SendEv(EV_BTN, MOUSEEVENTF_RIGHTUP); return 0;
        case WM_MBUTTONDOWN: SetCapture(w); SendEv(EV_BTN, MOUSEEVENTF_MIDDLEDOWN); return 0;
        case WM_MBUTTONUP:   ReleaseCapture(); SendEv(EV_BTN, MOUSEEVENTF_MIDDLEUP); return 0;
        case WM_MOUSEWHEEL:
            SendEv(EV_WHEEL, (short)HIWORD(wp));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(w, m, wp, lp);
    }

    // 서버 모니터마다 창을 만들고 닫힐 때까지 메시지 루프 (info = 모니터 개수, (너비, 높이) ...)
    static void View(const std::vector<int32_t>& info) {
        int nMon = info[0];
        Log::Put("원격 클라이언트: 서버 모니터 %d 개", nMon);
        for (int i = 0; i < nMon; i++) Log::Put("원격 클라이언트:   모니터 %d: %dx%d", i + 1, (int)info[1 + 2 * i], (int)info[2 + 2 * i]);
        HINSTANCE hi = GetModuleHandleW(NULL);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ViewProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));            // 앱 아이콘 (rc 의 1 ICON)
        wc.lpszClassName = (LPCWSTR)u"gsHomeManagerRdpView";
        RegisterClassW(&wc);
        wnd_.assign(nMon, NULL);
        bmp_.assign(nMon, NULL);
        for (int i = 0; i < nMon; i++) {
            int mw = info[1 + 2 * i], mh = info[2 + 2 * i];
            int cw = std::min(mw, 1280), ch = (int)((int64_t)mh * cw / mw);
            RECT rc = {0, 0, cw, ch};
            AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
            char t[80];
            wsprintfA(t, "원격 제어 - 모니터 %d (%dx%d)", i + 1, mw, mh);
            char16_t title[80];
            title[0] = 0;
            Utf8::ToUtf16(t, -1, title, 80);
            wnd_[i] = CreateWindowW((LPCWSTR)u"gsHomeManagerRdpView", (LPCWSTR)title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hi, NULL);
        }
        for (HWND w : wnd_) if (w) ShowWindow(w, SW_SHOW);
        std::thread net(NetThread);
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Log::Put("원격 클라이언트: 창을 닫음, 연결 종료");
        shutdown(sock_, SD_BOTH);
        net.join();                             // NetThread 는 recv 가 끝나면 빠져나옴 (소켓은 호출한 쪽에서 닫음)
        for (HWND w : wnd_) if (w && IsWindow(w)) DestroyWindow(w);
        for (HBITMAP b : bmp_) if (b) DeleteObject(b);
        wnd_.clear();
        bmp_.clear();
    }
};

}  // namespace rdp

// 서버 설정 (Settings -> rdp::Server::Cfg)
static rdp::Server::Cfg RdpServerCfg(const Settings& s) {
    rdp::Server::Cfg c;
    c.port = s.rsPort;
    c.pw = s.rsPass;
    c.allowIp = (!lstrcmpiA(s.rsAllow, "any") || !lstrcmpA(s.rsAllow, "*")) ? "" : s.rsAllow;
    c.accept = s.rsAccept;
    c.osk = s.rsOsk;
    return c;
}

// 두 원격 제어 대화상자가 함께 쓰는 도우미 (입력 칸 <-> UTF-8, 경고창, 비밀번호 보이기)
struct RdpDlg {
    static std::string Get(HWND d, int id) {
        char16_t u[256];
        char k[1024];
        u[0] = 0;
        GetDlgItemTextW(d, id, (LPWSTR)u, 256);
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)u, -1, k, sizeof(k), NULL, NULL);
        std::string s = k;
        SecureZeroMemory(u, sizeof(u));
        SecureZeroMemory(k, sizeof(k));
        return s;
    }
    static bool Set(HWND d, int id, const std::string& s) {      // 글자가 바뀌었을 때만 쓰고 true (깜빡임 방지)
        char16_t u[512], cur[512];
        u[0] = cur[0] = 0;
        Utf8::ToUtf16(s.c_str(), -1, u, 512);
        GetDlgItemTextW(d, id, (LPWSTR)cur, 512);
        if (lstrcmpW((LPCWSTR)u, (LPCWSTR)cur) == 0) return false;
        SetDlgItemTextW(d, id, (LPCWSTR)u);
        return true;
    }
    static void Warn(HWND d, const char* title, const char* msg, int focusId) {
        char16_t m[300], t[64];
        m[0] = t[0] = 0;
        Utf8::ToUtf16(msg, -1, m, 300);
        Utf8::ToUtf16(title, -1, t, 64);
        MessageBoxW(d, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONWARNING);
        if (focusId) SetFocus(GetDlgItem(d, focusId));
    }
    static void ShowPw(HWND d, int pwId, bool show) {
        SendDlgItemMessageW(d, pwId, EM_SETPASSWORDCHAR, show ? 0 : (WPARAM)'*', 0);
        InvalidateRect(GetDlgItem(d, pwId), NULL, TRUE);
    }
    // ini 에 안전하게 저장할 수 있는 비밀번호인지 (앞뒤 공백과 양끝 따옴표는 ini 를 읽을 때 사라진다)
    static bool PwOk(const std::string& p) {
        if (p.empty()) return false;
        if (p.front() == ' ' || p.back() == ' ' || p.front() == '\t' || p.back() == '\t') return false;
        if (p.size() > 1 && p.front() == p.back() && (p.front() == '"' || p.front() == '\'')) return false;
        return true;
    }
    static bool Port(HWND d, int id, int& port) {
        BOOL ok = FALSE;
        UINT p = GetDlgItemInt(d, id, &ok, FALSE);
        if (!ok || p < 1 || p > 65535) return false;
        port = (int)p;
        return true;
    }
};

// 다른 PC(서버): ini [RdpServer] 설정 + 서버 시작/중지 + 내 IP 주소와 현재 상태. 대화상자를 닫아도 서버는 계속 동작한다.
class RdpServerDialog {
public:
    static void Show(HINSTANCE hi, HWND owner, Settings& cfg) {
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(24); b.W(0); b.W(0); b.W(300); b.W(210);
        b.W(0); b.W(0);
        b.S("다른 PC(서버) - 이 PC 를 원격으로 제어하게 함");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        const DWORD E = T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        b.Control(T,                                              10,  10,  70, 10, 0xFFFF, "STATIC", "내 IP 주소");
        b.Control(T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 84, 8, 206, 13, IDC_IP, "EDIT", "");
        b.Control(T,                                              10,  28,  70, 10, 0xFFFF, "STATIC", "현재 상태");
        b.Control(T | SS_NOPREFIX,                                84,  28, 206, 10, IDC_STATE, "STATIC", "");
        b.Control(T | SS_ETCHEDHORZ,                              10,  44, 280,  1, 0xFFFF, "STATIC", "");
        b.Control(T,                                              10,  54,  70, 10, 0xFFFF, "STATIC", "port 번호");
        b.Control(E | ES_NUMBER,                                  84,  52,  50, 13, IDC_PORT, "EDIT", "");
        b.Control(T,                                              10,  72,  70, 10, 0xFFFF, "STATIC", "비밀번호");
        b.Control(E | ES_PASSWORD,                                84,  70, 120, 13, IDC_PASS, "EDIT", "");
        b.Button(T | WS_TABSTOP | BS_AUTOCHECKBOX,               212,  71,  70, 12, IDC_SHOWPW, "보이기");
        b.Control(T,                                              10,  90,  70, 10, 0xFFFF, "STATIC", "허용 IP");
        b.Control(E,                                              84,  88,  90, 13, IDC_ALLOW, "EDIT", "");
        b.Control(T,                                             180,  90, 110, 10, 0xFFFF, "STATIC", "any = 모든 IP 허용");
        b.Control(T,                                              10, 108,  70, 10, 0xFFFF, "STATIC", "연결 요청 확인");
        b.Button(T | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 84, 106, 206, 12, IDC_ACC0,     "클라이언트 IP 마다 처음 한 번만 묻기");
        b.Button(T | BS_AUTORADIOBUTTON,                          84, 119, 206, 12, IDC_ACC0 + 1, "묻지 않고 자동 수락");
        b.Button(T | BS_AUTORADIOBUTTON,                          84, 132, 206, 12, IDC_ACC0 + 2, "접속할 때마다 묻기");
        b.Button(T | WS_TABSTOP | WS_GROUP | BS_AUTOCHECKBOX,     84, 150, 206, 12, IDC_AUTO, "프로그램을 시작할 때 서버를 자동으로 시작");
        b.Button(T | WS_TABSTOP | BS_AUTOCHECKBOX,               84, 163, 206, 12, IDC_OSK, "연결되면 화상 키보드(OSK) 실행");
        b.Control(T | SS_NOPREFIX,                                10, 180, 280, 10, IDC_WARN, "STATIC", "※ 암호화(TLS)가 없습니다. 신뢰하는 내부망에서만 사용하세요.");
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON,               20, 192,  55, 14, IDC_SAVE, "저장");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,                  85, 192,  55, 14, IDC_START, "시작");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,                 150, 192,  55, 14, IDC_STOP, "서버 종료");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,                 215, 192,  55, 14, IDCANCEL, "닫기");
        DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&cfg);
    }

private:
    enum { IDC_IP = 101, IDC_STATE = 102, IDC_PORT = 103, IDC_PASS = 104, IDC_SHOWPW = 105, IDC_ALLOW = 106,
           IDC_ACC0 = 107 /* 107~109 */, IDC_AUTO = 110, IDC_WARN = 111, IDC_SAVE = 112, IDC_START = 113, IDC_STOP = 114, IDC_OSK = 115,
           TID_REFRESH = 1 };
    static constexpr const char* TITLE = "다른 PC(서버)";

    // 입력 칸 -> cfg (검사 실패하면 경고 후 false)
    static bool Collect(HWND d, Settings& cfg) {
        int port;
        if (!RdpDlg::Port(d, IDC_PORT, port)) { RdpDlg::Warn(d, TITLE, "port 번호는 1 ~ 65535 사이의 숫자로 입력하세요.", IDC_PORT); return false; }
        std::string pw = RdpDlg::Get(d, IDC_PASS);
        if (!RdpDlg::PwOk(pw)) {
            RdpDlg::Warn(d, TITLE, "비밀번호를 입력하세요. (앞뒤 공백, 양끝 따옴표는 사용할 수 없습니다)", IDC_PASS);
            SecureZeroMemory(&pw[0], pw.size());
            return false;
        }
        std::string ip = RdpDlg::Get(d, IDC_ALLOW);
        while (!ip.empty() && (ip.back() == ' ' || ip.back() == '\t')) ip.pop_back();
        size_t st = 0;
        while (st < ip.size() && (ip[st] == ' ' || ip[st] == '\t')) st++;
        ip = ip.substr(st);
        if (ip.empty() || ip == "*" || !lstrcmpiA(ip.c_str(), "any")) ip = "any";
        else if (inet_addr(ip.c_str()) == INADDR_NONE) {
            RdpDlg::Warn(d, TITLE, "허용 IP 는 any 또는 IP address (예: 192.168.0.20) 로 입력하세요.", IDC_ALLOW);
            SecureZeroMemory(&pw[0], pw.size());
            return false;
        }
        cfg.rsPort = port;
        lstrcpynA(cfg.rsPass, pw.c_str(), sizeof(cfg.rsPass));
        lstrcpynA(cfg.rsAllow, ip.c_str(), sizeof(cfg.rsAllow));
        cfg.rsAccept = IsDlgButtonChecked(d, IDC_ACC0 + 1) == BST_CHECKED ? 1 : IsDlgButtonChecked(d, IDC_ACC0 + 2) == BST_CHECKED ? 2 : 0;
        cfg.rsAuto = IsDlgButtonChecked(d, IDC_AUTO) == BST_CHECKED;
        cfg.rsOsk = IsDlgButtonChecked(d, IDC_OSK) == BST_CHECKED;
        SecureZeroMemory(&pw[0], pw.size());
        return true;
    }

    static void Refresh(HWND d) {
        rdp::Server& sv = rdp::Server::Get();
        if (RdpDlg::Set(d, IDC_STATE, sv.Status())) InvalidateRect(GetDlgItem(d, IDC_STATE), NULL, TRUE);       // 글자가 바뀌면 색도 다시 그림
        RdpDlg::Set(d, IDC_START, sv.Running() ? "재시작" : "시작");
        EnableWindow(GetDlgItem(d, IDC_STOP), sv.Running());
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_INITDIALOG: {
            Settings* c = (Settings*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            SendDlgItemMessageW(d, IDC_PORT, EM_LIMITTEXT, 5, 0);
            SendDlgItemMessageW(d, IDC_PASS, EM_LIMITTEXT, 60, 0);
            SendDlgItemMessageW(d, IDC_ALLOW, EM_LIMITTEXT, 15, 0);
            SetDlgItemInt(d, IDC_PORT, (UINT)c->rsPort, FALSE);
            RdpDlg::Set(d, IDC_PASS, c->rsPass);
            RdpDlg::Set(d, IDC_ALLOW, c->rsAllow);
            CheckRadioButton(d, IDC_ACC0, IDC_ACC0 + 2, IDC_ACC0 + (c->rsAccept >= 0 && c->rsAccept <= 2 ? c->rsAccept : 0));
            CheckDlgButton(d, IDC_AUTO, c->rsAuto ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(d, IDC_OSK, c->rsOsk ? BST_CHECKED : BST_UNCHECKED);
            std::string ips = rdp::LocalIps();
            RdpDlg::Set(d, IDC_IP, ips.empty() ? "(네트워크 연결 없음)" : ips);
            Refresh(d);
            SetTimer(d, TID_REFRESH, 500, NULL);
            return TRUE;
        }
        case WM_TIMER:
            if (w == TID_REFRESH) Refresh(d);
            return TRUE;
        case WM_DESTROY:
            KillTimer(d, TID_REFRESH);
            break;
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(d, IDC_STATE) || (HWND)l == GetDlgItem(d, IDC_WARN)) {
                const bool run = rdp::Server::Get().Running();
                SetTextColor((HDC)w, (HWND)l == GetDlgItem(d, IDC_WARN) ? RGB(200, 0, 0) : (run ? RGB(0, 130, 0) : RGB(200, 0, 0)));
                SetBkColor((HDC)w, GetSysColor(COLOR_3DFACE));
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_COMMAND: {
            Settings* c = (Settings*)GetWindowLongPtrA(d, GWLP_USERDATA);
            switch (LOWORD(w)) {
            case IDC_SHOWPW:
                RdpDlg::ShowPw(d, IDC_PASS, IsDlgButtonChecked(d, IDC_SHOWPW) == BST_CHECKED);
                return TRUE;
            case IDC_SAVE:
                if (!Collect(d, *c)) return TRUE;
                c->SaveRdpServer();
                Log::Put("원격 서버 설정 저장: port %d, 허용 IP %s, 연결 확인 %d, 자동 시작 %d, 화상 키보드 %d", c->rsPort, c->rsAllow, c->rsAccept, (int)c->rsAuto, (int)c->rsOsk);
                if (rdp::Server::Get().Running()) {                // 실행 중인 서버에도 바로 반영 (다음 접속부터)
                    const int runPort = rdp::Server::Get().Port();
                    rdp::Server::Get().Update(RdpServerCfg(*c));
                    if (runPort != c->rsPort) RdpDlg::Warn(d, TITLE, "port 번호는 [시작](재시작)을 눌러야 적용됩니다. 나머지 설정은 다음 접속부터 적용됩니다.", 0);
                }
                EndDialog(d, 1);
                return TRUE;
            case IDC_START: {
                if (!Collect(d, *c)) return TRUE;
                c->SaveRdpServer();
                std::string err;
                if (!rdp::Server::Get().Start(RdpServerCfg(*c), err)) RdpDlg::Warn(d, TITLE, err.c_str(), 0);
                Refresh(d);
                return TRUE; }
            case IDC_STOP:
                rdp::Server::Get().Stop();
                Refresh(d);
                return TRUE;
            case IDCANCEL:
                EndDialog(d, 0);
                return TRUE;
            }
            break; }
        }
        return FALSE;
    }
};

// 다른 PC(클라이언트): ini [RdpClient] 설정 + [연결] 버튼으로 서버에 연결 요청
class RdpClientDialog {
public:
    static void Show(HINSTANCE hi, HWND owner, Settings& cfg) {
        InitCommonControls();                                   // 진행 막대
        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(14); b.W(0); b.W(0); b.W(300); b.W(148);
        b.W(0); b.W(0);
        b.S("다른 PC(클라이언트) - 다른 PC 를 원격으로 제어");
        b.W(9); b.S("Segoe UI");
        const DWORD T = WS_CHILD | WS_VISIBLE;
        const DWORD E = T | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        b.Control(T,                                              10,  12,  70, 10, 0xFFFF, "STATIC", "서버 IP address");
        b.Control(E,                                              84,  10, 110, 13, IDC_IP, "EDIT", "");
        b.Control(T,                                              10,  30,  70, 10, 0xFFFF, "STATIC", "port 번호");
        b.Control(E | ES_NUMBER,                                  84,  28,  50, 13, IDC_PORT, "EDIT", "");
        b.Control(T,                                              10,  48,  70, 10, 0xFFFF, "STATIC", "비밀번호");
        b.Control(E | ES_PASSWORD,                                84,  46, 120, 13, IDC_PASS, "EDIT", "");
        b.Button(T | WS_TABSTOP | BS_AUTOCHECKBOX,               212,  47,  70, 12, IDC_SHOWPW, "보이기");
        b.Control(T,                                              10,  70, 280, 10, 0xFFFF, "STATIC", "서버 PC 의 [다른 PC(서버)] 에서 서버를 시작한 뒤 [연결] 을 누르세요.");
        b.Control(T | SS_NOPREFIX,                                10,  82, 280, 10, IDC_WARN, "STATIC", "※ 암호화(TLS)가 없습니다. 신뢰하는 내부망에서만 사용하세요.");
        b.Control(WS_CHILD | SS_NOPREFIX,                         10,  98, 280, 10, IDC_PSTAT, "STATIC", "");                 // 연결하는 동안만 보임
        b.Control(WS_CHILD | PBS_SMOOTH,                          10, 109, 280,  9, IDC_PBAR, "msctls_progress32", "");
        b.Button(T | WS_TABSTOP | BS_DEFPUSHBUTTON,               50, 126,  55, 14, IDC_CONNECT, "연결");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,                 115, 126,  55, 14, IDC_SAVE, "저장");
        b.Button(T | WS_TABSTOP | BS_PUSHBUTTON,                 180, 126,  55, 14, IDCANCEL, "닫기");
        DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)&cfg);
    }

private:
    enum { IDC_IP = 101, IDC_PORT = 102, IDC_PASS = 103, IDC_SHOWPW = 104, IDC_WARN = 105, IDC_CONNECT = 106, IDC_SAVE = 107, IDC_PSTAT = 108, IDC_PBAR = 109 };
    static constexpr const char* TITLE = "다른 PC(클라이언트)";

    static bool Collect(HWND d, Settings& cfg) {
        std::string ip = RdpDlg::Get(d, IDC_IP);
        while (!ip.empty() && (ip.back() == ' ' || ip.back() == '\t')) ip.pop_back();
        size_t st = 0;
        while (st < ip.size() && (ip[st] == ' ' || ip[st] == '\t')) st++;
        ip = ip.substr(st);
        if (ip.empty() || inet_addr(ip.c_str()) == INADDR_NONE) {
            RdpDlg::Warn(d, TITLE, "서버의 IP address 를 입력하세요. 예: 192.168.0.118", IDC_IP);
            return false;
        }
        int port;
        if (!RdpDlg::Port(d, IDC_PORT, port)) { RdpDlg::Warn(d, TITLE, "port 번호는 1 ~ 65535 사이의 숫자로 입력하세요.", IDC_PORT); return false; }
        std::string pw = RdpDlg::Get(d, IDC_PASS);
        if (!RdpDlg::PwOk(pw)) {
            RdpDlg::Warn(d, TITLE, "비밀번호를 입력하세요. (앞뒤 공백, 양끝 따옴표는 사용할 수 없습니다)", IDC_PASS);
            SecureZeroMemory(&pw[0], pw.size());
            return false;
        }
        lstrcpynA(cfg.rcIp, ip.c_str(), sizeof(cfg.rcIp));
        cfg.rcPort = port;
        lstrcpynA(cfg.rcPass, pw.c_str(), sizeof(cfg.rcPass));
        SecureZeroMemory(&pw[0], pw.size());
        return true;
    }

    // 연결하는 동안: 입력/연결/저장을 막고 진행 표시를 보임, [닫기] 는 [취소] 로
    static void SetBusy(HWND d, bool busy) {
        static const int ids[] = {IDC_IP, IDC_PORT, IDC_PASS, IDC_SHOWPW, IDC_CONNECT, IDC_SAVE};
        for (int id : ids) EnableWindow(GetDlgItem(d, id), !busy);
        ShowWindow(GetDlgItem(d, IDC_PSTAT), busy ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(d, IDC_PBAR), busy ? SW_SHOW : SW_HIDE);
        RdpDlg::Set(d, IDCANCEL, busy ? "취소" : "닫기");
        if (busy) SetFocus(GetDlgItem(d, IDCANCEL));
        else SetFocus(GetDlgItem(d, IDC_CONNECT));
    }

    static INT_PTR CALLBACK Proc(HWND d, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case rdp::Client::WM_PROGRESS: {
            const int total = (l == 1 ? rdp::Client::APPROVE_SEC : rdp::Client::CONNECT_SEC) * 1000;
            SendDlgItemMessageW(d, IDC_PBAR, PBM_SETRANGE32, 0, total);
            SendDlgItemMessageW(d, IDC_PBAR, PBM_SETPOS, w, 0);
            char t[120];
            wsprintfA(t, l == 1 ? "서버의 승인을 기다리는 중...  %d 초 남음" : "서버에 연결하는 중...  %d 초 남음", ((int)w + 999) / 1000);
            RdpDlg::Set(d, IDC_PSTAT, t);
            return TRUE; }
        case rdp::Client::WM_DONE: {
            if (w == 1) { EndDialog(d, 2); return TRUE; }       // 연결 성공: 원격 제어 창이 열림
            SetBusy(d, false);
            std::string e = rdp::Client::TakeError();
            if (!e.empty()) RdpDlg::Warn(d, TITLE, e.c_str(), 0);
            return TRUE; }
        case WM_DESTROY:
            rdp::Client::Detach();
            break;
        case WM_INITDIALOG: {
            Settings* c = (Settings*)l;
            SetWindowLongPtrA(d, GWLP_USERDATA, l);
            SendDlgItemMessageW(d, IDC_IP, EM_LIMITTEXT, 15, 0);
            SendDlgItemMessageW(d, IDC_PORT, EM_LIMITTEXT, 5, 0);
            SendDlgItemMessageW(d, IDC_PASS, EM_LIMITTEXT, 60, 0);
            RdpDlg::Set(d, IDC_IP, c->rcIp);
            SetDlgItemInt(d, IDC_PORT, (UINT)c->rcPort, FALSE);
            RdpDlg::Set(d, IDC_PASS, c->rcPass);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(d, IDC_WARN)) {
                SetTextColor((HDC)w, RGB(200, 0, 0));
                SetBkMode((HDC)w, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
            }
            break;
        case WM_COMMAND: {
            Settings* c = (Settings*)GetWindowLongPtrA(d, GWLP_USERDATA);
            switch (LOWORD(w)) {
            case IDC_SHOWPW:
                RdpDlg::ShowPw(d, IDC_PASS, IsDlgButtonChecked(d, IDC_SHOWPW) == BST_CHECKED);
                return TRUE;
            case IDC_SAVE:
                if (!Collect(d, *c)) return TRUE;
                c->SaveRdpClient();
                Log::Put("원격 클라이언트 설정 저장: %s:%d", c->rcIp, c->rcPort);
                EndDialog(d, 1);
                return TRUE;
            case IDC_CONNECT: {
                if (!Collect(d, *c)) return TRUE;
                c->SaveRdpClient();
                rdp::Client::Cfg cc;
                cc.ip = c->rcIp;
                cc.port = c->rcPort;
                cc.pw = c->rcPass;
                if (!rdp::Client::Start(cc, d)) { RdpDlg::Warn(d, TITLE, "이미 원격 연결 중입니다. 열려 있는 원격 제어 창을 닫거나 잠시 뒤에 다시 시도하세요.", 0); return TRUE; }
                SetBusy(d, true);
                RdpDlg::Set(d, IDC_PSTAT, "서버에 연결하는 중...");
                return TRUE; }
            case IDCANCEL:
                if (!IsWindowEnabled(GetDlgItem(d, IDC_CONNECT))) rdp::Client::Cancel();     // 연결하는 중이면 취소
                EndDialog(d, 0);
                return TRUE;
            }
            break; }
        }
        return FALSE;
    }
};

// ---------------------------------------------------------------- ClockWindow
// 시계 창 1개. owner 가 없으면 본 창(메뉴/ini 위치 저장/랜덤 이동 담당),
// 있으면 Global 도시 창(본 창의 소유 창). 도시 창은 본 창 아래에 계속 추가되고 본 창과 함께 이동한다.
class ClockWindow {
public:
    explicit ClockWindow(Settings& cfg, ClockWindow* owner = NULL,
                         const char* city = NULL, const char* zoneKey = NULL)
        : cfg_(cfg), owner_(owner), city_(city ? city : cfg.baseCity),
          timeScroll_(2), dateScroll_(1), lunarScroll_(1) {
        if (zoneKey) zone_.Load(zoneKey);
    }
    ~ClockWindow() {
        if (font_)     DeleteObject(font_);
        if (dateFont_) DeleteObject(dateFont_);
        if (bgBrush_)  DeleteObject(bgBrush_);
        if (statusFont_) DeleteObject(statusFont_);
    }
    ClockWindow(const ClockWindow&) = delete;
    ClockWindow& operator=(const ClockWindow&) = delete;

    bool Create(HINSTANCE hi, int show, int x, int y) {
        hi_ = hi;
        Register(hi);
        if (!owner_) {                          // 본 창만 메뉴를 가짐
            menu_ = BuildMenu();
            CheckOpacity();
            CheckMoveInterval();
            CheckFormats();
            UpdateOptionChecks();
            UpdateCityChecks();                 // 기준 도시 체크
        }
        const int mode = cfg_.clockMode;
        if (!owner_ && mode != 2) cfg_.analogFull = false;      // 전체화면은 아날로그일 때만
        RECT r0 = {0, 0, 336, ClientH(ShowLunarHere()) + StatusH()};   // [타이틀 & 디지털] 모드의 크기
        AdjustWindowRect(&r0, NormalStyle(), FALSE);          // 고정 메뉴바 없음 (메뉴는 더블클릭 팝업)
        int w = r0.right - r0.left, ht = r0.bottom - r0.top;
        if (!owner_) cfg_.LoadPos(x, y, w, ht); // ini 에 저장된 마지막 위치로 시작 (저장 좌표 = [타이틀 & 디지털] 모드의 창 좌표)
        DWORD style = NormalStyle();
        hidden_ = (mode != 0);
        if (mode != 0) {                        // [디지털] / [아날로그]: 타이틀바 없이, 그 모드의 크기로 바로 생성
            if (!owner_) { x -= r0.left; y -= r0.top; }       // 저장 좌표 -> 클라이언트 영역의 왼쪽 위
            RECT r = {0, 0, mode == 2 ? AnalogSide() : 336, ClientH()};
            style = WS_POPUP;
            AdjustWindowRect(&r, style, FALSE);
            w = r.right - r.left;
            ht = r.bottom - r.top;
        }

        HWND h = CreateWindowExA(0, "gsHomeManager", "gsHomeManager", style, x, y, w, ht,
                                 owner_ ? owner_->hwnd_ : NULL, NULL, hi, this);
        if (!h) return false;

        char16_t tu[64];
        Utf8::ToUtf16(owner_ ? city_ : "gsHomeManager", -1, tu, 64);   // UTF-8 제목
        SetWindowTextW(h, (LPCWSTR)tu);
        ShowWindow(h, cfg_.visible ? show : SW_HIDE);   // 숨김 상태로 시작하면 트레이 아이콘만 표시
        if (!owner_ && cfg_.analogFull) ApplyAnalogFull();      // ini Full=1: 전체화면으로 바로 시작 (모니터 중앙)
        if (!owner_) {
            Lunar::Init(cfg_.lunarUrl, cfg_.lunarKey, h);     // 음력 조회 결과를 받을 창
            if (!cfg_.LunarReady()) Log::Put("음력: ini [Lunar] ApiUrl/ApiKey 가 없어 '음력 보이기' 비활성");
        }
        if (!owner_ && cfg_.backFollow) ApplyDesktopColor();   // 시작할 때 현재 바탕색 적용
        if (!owner_ && cfg_.analogMatch) RefreshDeskColor(false);   // 아날로그 '배경색 일치': 현재 모니터 바탕색 읽기
        if (!owner_ && !cfg_.visible) TrayAdd();
        if (!owner_) RestoreCities();           // 저장된 도시 창을 모두 다시 열기
        return true;
    }

    void AttachScan(ScanEngine* s) { scan_ = s; }   // 본 창만: WiFiScan 대화상자가 쓰는 scan 엔진 (WinMain 이 만든 것)

    int Run() {
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {   // 메시지 대기 중 CPU 0
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return (int)msg.wParam;
    }

private:
    enum {
        TID_SECOND      = 1,        // 초 경계: 시계 갱신
        TID_MOVE        = 2,        // 창 위치 이동 (본 창만)
        TID_SCROLL_TIME = 3,        // 시분초 스크롤
        TID_SCROLL_DATE = 4,        // 년월일 스크롤
        TID_CLOSE       = 5,        // 알람 플레이어 자동 닫기 감시 (1초 간격, 필요할 때만)
        TID_CLICK       = 6,        // 한 번 클릭 확정 대기 (더블클릭이 아니면 시계 모드 변경)

        CMD_BACKCOLOR   = 101,      // 메뉴 명령
        CMD_TIMEFONT    = 102,
        CMD_DATEFONT    = 103,
        CMD_EXIT        = 104,
        CMD_RESET       = 105,
        CMD_TOPMOST     = 106,
        CMD_ALLMON      = 107,
        CMD_ALLDESK     = 108,
        CMD_COMPANION   = 116,      // 앱 동작 설정 > 동반 실행 앱 등록 (대화상자: 최대 5개 -> ini companion_app1~5)
        CMD_AUTORUN     = 115,      // 앱 동작 설정 > 자동 실행 on/off (Windows 시작 시 자동 실행: 레지스트리 Run)
        CMD_HELP        = 730,      // 730 About, 731 ReadMe.MD, 732 홈페이지, 733 최신 버전 다운로드, 734 음력 API 키값 등록
        CMD_SCAN        = 740,      // WiFiScan 대화상자
        CMD_MYPHONE     = 741,      // 앱 동작 설정 > 내 폰 등록 (WiFiScan 대화상자 + 안내 문구)
        CMD_IPREG       = 742,      // IP cam > IP cam 등록
        CMD_IPVIEW      = 743,      // IP cam > 카메라 영상 (WebCam1 / WebCam2 / IP Cam)
        CMD_PHONE       = 744,      // IP cam / 폰 연결 > 폰 탐색기
        CMD_RDPSRV      = 745,      // IP cam / 폰 연결 > 다른 PC(서버)
        CMD_RDPCLI      = 746,      // IP cam / 폰 연결 > 다른 PC(클라이언트)
        CMD_TFMT        = 700,      // 시분초 표시 형식 700~709
        CMD_DFMT        = 720,      // 년월일 표시 형식 720~729
        FMT_COUNT       = 10,
        CMD_BACKDESK    = 113,      // 시계 바탕색과 모니터 바탕색을 동일하게 토글
        CMD_SHOWLUNAR   = 114,      // 음력 보이기/숨기기 토글
        CMD_SHOWDATE    = 112,      // 년월일 보이기/숨기기 토글
        CMD_SHOWCLOCK   = 111,      // 시계 보이기/숨기기 토글 (트레이 메뉴에서도 사용)
        WM_TRAY         = WM_APP + 1,   // 트레이 아이콘 콜백
        CMD_OPACITY     = 200,      // 200 + (투명도/10) : 201~210
        CMD_ASEC        = 853,      // F4) 아나로그 시계 디자인 > 초침 생략 on/off
        CMD_AMATCH      = 852,      // F4) 아나로그 시계 디자인 > 배경색 일치 on/off
        CMD_AFULL       = 851,      // F4) 아나로그 시계 디자인 > 전체화면 on/off
        CMD_ANEON       = 850,      // F4) 아나로그 시계 디자인 > 네온사인 on/off
        CMD_ATHEME      = 800,      // 800 + 테마 인덱스: F4) 아날로그 시계 디자인
        CMD_CITY        = 300,      // 300 + 도시 인덱스
        CMD_MOVE        = 400,      // 400 + 이동 간격 선택지 인덱스
        CMD_PROG        = 610,      // 610 사용, 611 시각, 612 파일, 613 메모, 614 지우기, 615 테스트
        CMD_REBOOT      = 600,      // 시스템 재시작 시각 (대화상자: 시각 + 사용)
        CMD_HOMEPROG    = 660,      // 660~679: HomeBack 프로그램(660+동작) / NotHome 프로그램(670+동작). 동작 0 사용, 2 파일, 3 메모, 4 지우기, 5 테스트
        CMD_SCANSCHED   = 640,      // 640~659: ScanOn(640+동작) / ScanOff(650+동작) 예약
        CMD_ALARM       = 500,      // 500 + 알람번호(0~9)*ALARM_ACTS + 동작
        ALARM_ACTS      = 10        // 0 사용, 1 시각, 2 파일, 3 지우기, 4~6 자동 닫기(끔/재생 후/지정 시간 후), 7 닫기 시간, 8 테스트, 9 메모
    };

    // 위치 이동 간격 선택지 (ms). 0 = 이동하지 않고 고정
    static const int MOVE_COUNT = 8;
    static int MoveChoice(int i) {
        static const int v[MOVE_COUNT] = {0, 5000, 10000, 30000, 60000, 300000, 600000, 1800000};
        return v[i];
    }

    static DWORD NormalStyle() { return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX; }

    static void Register(HINSTANCE hi) {
        static bool done = false;
        if (done) return;
        done = true;
        WNDCLASSA wc = {};
        wc.style         = CS_DBLCLKS;          // 더블클릭 메시지 수신
        wc.lpfnWndProc   = &ClockWindow::StaticProc;
        wc.hInstance     = hi;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = LoadIcon(hi, MAKEINTRESOURCE(1));   // exe 리소스의 아이콘 (작업 표시줄/ALT+TAB/제목 표시줄)
        wc.lpszClassName = "gsHomeManager";
        RegisterClassA(&wc);
    }

    // ---- 메시지 라우팅: static 프로시저 -> 인스턴스 메서드
    static LRESULT CALLBACK StaticProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        ClockWindow* self;
        if (m == WM_NCCREATE) {
            self = (ClockWindow*)((CREATESTRUCTA*)l)->lpCreateParams;
            SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = h;
        } else {
            self = (ClockWindow*)GetWindowLongPtrA(h, GWLP_USERDATA);
        }
        if (!self) return DefWindowProcA(h, m, w, l);

        LRESULT r = self->Proc(m, w, l);
        if (m == WM_NCDESTROY) {
            SetWindowLongPtrA(h, GWLP_USERDATA, 0);
            if (self->owner_) delete self;      // 도시 창은 스스로 해제 (본 창은 WinMain 스택 객체)
        }
        return r;
    }

    LRESULT Proc(UINT m, WPARAM w, LPARAM l) {
        static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (m == taskbarCreated && !owner_) {                     // 로그온 직후 작업 표시줄이 아직 없어 트레이 추가가 실패했거나, 탐색기가 다시 시작된 경우
            trayOn_ = false;
            if (!cfg_.visible) TrayAdd();
            return 0;
        }
        switch (m) {
        case WM_WINDOWPOSCHANGING:                                // 트레이 상태(시계 숨김)에서는 어떤 경로로도 창이 화면에 나타나지 않게 함
            if (!cfg_.visible) ((WINDOWPOS*)l)->flags &= ~SWP_SHOWWINDOW;
            break;
        case WM_CREATE:     OnCreate();                          return 0;
        case WM_TIMER:      OnTimer((UINT_PTR)w);                return 0;
        case WM_ERASEBKGND: return 1;                            // 배경은 OnPaint에서 채움
        case WM_PAINT:      OnPaint();                           return 0;
        case ScanEvents::MSG: if (!owner_) OnScanEvents();       return 0;
        case ipcam::Session::MSG_CLOSED: if (!owner_) IpCamClosed(); return 0;
        case WM_CLOSE:                                            // 본 창을 닫으면(종료 확인 후) 앱 종료
            if (owner_) { DestroyWindow(hwnd_); return 0; }   // 도시 창: 이 도시 시계만 닫음 (앱은 계속 실행)
            ConfirmExit();                                    // 본 창 X: '프로그램 종료' 와 동일한 확인창 (Yes 일 때만 종료)
            return 0;
        case WM_SYSKEYDOWN:                                       // ALT+F4: '프로그램 종료' 와 동일 (도시 창에서도 앱 종료 확인)
            if (w == VK_F4) { ConfirmExit(); return 0; }
            break;
        case WM_EXITSIZEMOVE: SavePos();                         return 0;   // 사용자가 드래그를 끝냈을 때
        case WM_COMMAND:    OnCommand((int)LOWORD(w));           return 0;
        case WM_TRAY:       OnTray((UINT)l);                     return 0;
        case SingleInstance::WM_TAKEOVER:                         // 새로 시작한 같은 앱이 요청: 확인 없이 정상 종료
            if (!owner_) {
                Log::Put("새로 시작한 앱의 요청으로 종료");
                RequestExit();
            }
            return 0;
        case Lunar::WM_DONE:                                      // 음력 조회 완료: 캐시에 저장하고 모든 창 다시 그림
            Lunar::OnDone(l);
            ForAll(&ClockWindow::ApplyFormat);
            return 0;
        case WM_QUERYENDSESSION:
            if (!owner_) Log::Put("Windows 종료/재시작/로그오프 요청 수신 (lParam=0x%x)", (int)l);
            break;
        case WM_ENDSESSION:
            if (!owner_) Log::Put("세션 종료 %s", w ? "확정: 앱이 곧 종료됨" : "취소됨");
            if (w && !owner_) TrayRemove();                       // 로그오프/종료: 트레이 아이콘이 남지 않게 정리
            break;
        case WM_KEYDOWN:                                          // F1: 최상위 메뉴 전체 표시 / F2 ~ F9: 최상위 메뉴 8개를 순서대로 바로 열기
            if (!owner_ && w == VK_F1) { ShowPopupMenu(); return 0; }
            if (!owner_ && w >= VK_F2 && w < VK_F2 + TOP_MENU_COUNT) {
                const int k = (int)(w - VK_F2);
                if ((k == TOP_DIGITAL && cfg_.clockMode == 2) || (k == TOP_ANALOG && cfg_.clockMode != 2)) return 0;   // 비활성 메뉴는 열지 않음
                ShowPopupMenu(menuTop_[k]);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:                                      // 한 번 클릭: 시계 모드 순환 [타이틀 & 디지털] -> [디지털] -> [아날로그] (더블클릭이 아님이 확인되면)
            pressed_ = true;
            downPt_.x = (short)LOWORD(l);
            downPt_.y = (short)HIWORD(l);
            SetCapture(hwnd_);
            return 0;
        case WM_MOUSEWHEEL:                                       // 아날로그: 휠로 시계 크기 확대/축소 (전체화면 제외)
            if (cfg_.clockMode == 2 && !cfg_.analogFull) { WheelAnalog((int)(short)HIWORD(w)); return 0; }
            break;
        case WM_MOUSEMOVE:                                        // 클릭한 채 끌면 창 이동 (타이틀바 유무와 무관)
            if (pressed_ && (w & MK_LBUTTON)) {
                int dx = (short)LOWORD(l) - downPt_.x, dy = (short)HIWORD(l) - downPt_.y;
                if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG)) {
                    pressed_ = false;
                    ReleaseCapture();
                    if (!cfg_.analogFull) SendMessageA(hwnd_, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);   // 전체화면: 끌어도 이동 안 함
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (pressed_) {
                pressed_ = false;
                ReleaseCapture();
                SetTimer(hwnd_, TID_CLICK, GetDoubleClickTime(), NULL);   // 시간 안에 더블클릭이 오면 취소
            }
            return 0;
        case WM_LBUTTONDBLCLK:                                    // 더블클릭: 팝업 메뉴 표시 (다른 곳을 한 번 더 클릭하면 닫힘)
            KillTimer(hwnd_, TID_CLICK);
            ShowPopupMenu();
            return 0;
        case WM_NCLBUTTONDBLCLK:
            if (owner_ && w == HTCAPTION) { owner_->ArrangeCities(); return 0; }   // 도시 창 타이틀바 더블클릭: 본 창 밑으로 정렬
            break;
        case WM_INITMENUPOPUP:                                    // 메뉴가 열릴 때 단색 여부 다시 확인
            if (!owner_ && (HMENU)w == menuBack_) UpdateBackMenu();
            if (!owner_ && (HMENU)w == menuATheme_) RefreshDeskColor(true);
            break;
        case WM_SETTINGCHANGE:                                    // 바탕화면이 바뀌면 (단색일 때) 바탕색 다시 맞춤
            if (!owner_ && cfg_.backFollow && w == SPI_SETDESKWALLPAPER) ApplyDesktopColor();
            if (!owner_ && cfg_.analogMatch && w == SPI_SETDESKWALLPAPER) RefreshDeskColor(true);
            break;
        case WM_ENTERMENULOOP: modal_ = true;                    return 0;   // 메뉴/대화상자 중에는
        case WM_EXITMENULOOP:  modal_ = false;                   return 0;   // 창 자동 이동 멈춤
        case WM_DESTROY:    OnDestroy();                         return 0;
        }
        return DefWindowProcA(hwnd_, m, w, l);
    }

    // ---- 핸들러
    void OnCreate() {
        if (!owner_) ScanEvents::SetWindow(hwnd_);      // 작업 스레드의 상태/팝업 알림을 받음
        ApplyStyle();
        RestartTimers();
        ScheduleSecondTick();
        if (!owner_ && cfg_.allDesk && !DesktopPin::Set(true)) {   // 시작 시 ini 값 적용 (실패하면 Off)
            cfg_.allDesk = false;
            UpdateOptionChecks();
        }
    }

    void OnTimer(UINT_PTR id) {
        switch (id) {
        case TID_SECOND:
            ScheduleSecondTick();
            if (!owner_) {
                CheckAlarms();                  // 알람 확인은 본 창만
                if (trayOn_) TrayUpdate();      // 숨김 상태: 트레이 툴팁에 현재 시각 표시
            }
            if (MinuteOnly()) {                 // 아날로그 + 초침 생략: 분이 바뀔 때만 다시 그림 (알람 확인은 위에서 매초 계속)
                SYSTEMTIME nt;
                zone_.Now(nt);
                if (nt.wMinute == lastPaintMin_) return;
            }
            break;
        case TID_MOVE:
            if (!modal_ && !cfg_.analogFull) MoveGroup();       // 전체화면: 모니터 중앙에 고정
            return;
        case TID_CLOSE:
            ProcessPending();
            return;
        case TID_CLICK:
            KillTimer(hwnd_, TID_CLICK);
            CycleClockMode();
            return;
        case TID_SCROLL_TIME:
            if (cfg_.clockMode == 2) return;    // 아날로그: 글자 스크롤 없음 -> 다시 그리지 않음
            timeScroll_.Advance(ClientWidth());
            break;
        case TID_SCROLL_DATE:
            if (cfg_.clockMode == 2) return;
            dateScroll_.Advance(ClientWidth());
            lunarScroll_.Advance(ClientWidth());
            break;
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    // 작업 스레드에서 온 알림: 상태줄 문구 갱신 / 명령 전달 팝업 (5초) / 앱 종료 요청
    void OnScanEvents() {
        for (const ScanEvents::Item& it : ScanEvents::Take()) {
            if (it.kind == 4) { Log::Put("텔레그램 명령으로 앱 종료"); RequestExit(); return; }   // /stop, /restart (텔레그램에서 이미 Yes 확인함)
            if (it.kind == 1) MemoWindow::Show(hi_, it.text.c_str(), 5);
            else if (it.kind >= 2) RunHomeProg(it.kind - 2);
            else statusText_ = it.text;
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void DrawStatus(HDC dc, const RECT& rc) {
        RECT line = {0, rc.bottom - STATUS_H, rc.right, rc.bottom - STATUS_H + 1};
        HBRUSH lb = CreateSolidBrush(RGB(70, 70, 70));
        FillRect(dc, &line, lb);
        DeleteObject(lb);
        if (statusText_.empty()) return;
        if (!statusFont_) statusFont_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        char16_t u[200];
        if (!Utf8::ToUtf16(statusText_.c_str(), -1, u, 200)) return;
        const bool on = statusText_.find("scan On") != std::string::npos;
        const bool off = statusText_.find("scan Off") != std::string::npos;
        HGDIOBJ old = SelectObject(dc, statusFont_);
        SetTextColor(dc, on ? RGB(90, 220, 120) : (off ? RGB(255, 170, 60) : RGB(190, 190, 190)));
        RECT t = {6, rc.bottom - STATUS_H + 1, rc.right - 6, rc.bottom};
        DrawTextW(dc, (LPCWSTR)u, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }

    void OnPaint() {
        PAINTSTRUCT ps;
        HDC sdc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);

        HDC dc = CreateCompatibleDC(sdc);       // 더블버퍼링 (깜박임 방지)
        HBITMAP bm = CreateCompatibleBitmap(sdc, rc.right, rc.bottom);
        HGDIOBJ obm = SelectObject(dc, bm);
        FillRect(dc, &rc, bgBrush_);
        SetBkMode(dc, TRANSPARENT);

        SYSTEMTIME t;
        zone_.Now(t);                           // 도시 창: 그 도시의 현재 시각
        lastPaintMin_ = t.wMinute;
        char buf[96];
        int n;

        if (cfg_.clockMode == 2) {                  // [아날로그]
            PaintAnalog(dc, rc, t);
        } else {                                    // [타이틀 & 디지털] / [디지털]
            // 시분초 (큰 글자)
            FormatTime(t, cfg_.timeFmt, buf);
            n = lstrlenA(buf);
            RECT cr = rc;
            cr.bottom -= StatusH();                 // 본 창: 맨 아래 상태줄 영역 제외
            RECT r1 = cr;
            if (cfg_.showDate || ShowLunarHere()) r1.bottom = 90;  // 시분초 영역 90px (년월일/음력 줄이 없으면 창 전체 가운데에 표시)
            DrawLine(dc, font_, cfg_.timeColor, buf, n, r1, timeScroll_.Offset(),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // 기준 도시 이름 + 년월일 (작은 글자)
            if (cfg_.showDate) {
            char ds[64];
            FormatDate(t, cfg_.dateFmt, ds);
            n = wsprintfA(buf, "%s %s", city_, ds);
            RECT r2 = cr;
            r2.top = 90;
            DrawLine(dc, dateFont_, cfg_.dateColor, buf, n, r2,
                     dateScroll_.Offset(), DT_CENTER | DT_TOP | DT_SINGLELINE);
            }

            // 음력 (본 시계 전용. 년월일 다음 줄. 년월일을 숨기면 그 자리에)
            if (ShowLunarHere()) {
                HGDIOBJ of = SelectObject(dc, dateFont_);
                TEXTMETRICA tm;
                GetTextMetricsA(dc, &tm);
                SelectObject(dc, of);
                RECT r3 = cr;
                r3.top = 90 + (cfg_.showDate ? tm.tmHeight + 6 : 0);
                char lt[96];
                Lunar::Text(t.wYear * 10000 + t.wMonth * 100 + t.wDay, lt, sizeof(lt));
                DrawLine(dc, dateFont_, cfg_.dateColor, lt, lstrlenA(lt), r3,
                         lunarScroll_.Offset(), DT_CENTER | DT_TOP | DT_SINGLELINE);
            }

            if (!owner_) DrawStatus(dc, rc);
        }
        BitBlt(sdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, obm);
        DeleteObject(bm);                       // 그리자마자 해제 -> 상주 메모리 증가 없음
        DeleteDC(dc);
        EndPaint(hwnd_, &ps);
    }

    void OnDestroy() {
        if (!owner_) { for (ipcam::Session* c : cams_) delete c; cams_.clear(); }      // 앱 종료: 카메라 영상 창/스레드 모두 정리
        KillTimer(hwnd_, TID_SECOND);
        KillTimer(hwnd_, TID_MOVE);
        KillTimer(hwnd_, TID_SCROLL_TIME);
        KillTimer(hwnd_, TID_SCROLL_DATE);
        if (owner_) {                           // 도시 창: 본 창의 목록에서 제외 (앱은 계속 실행)
            Log::Put("Global 도시 창 닫힘: %s", city_);
            owner_->Unlink(this);
            if (!owner_->quitting_) {           // 앱 종료 중이 아닐 때만 (종료 시에는 이미 저장됨)
                owner_->SaveCities();
                owner_->UpdateCityChecks();     // Global 메뉴 체크 해제
            }
            return;
        }
        TrayRemove();
        SavePos();                              // 본 창 종료 = 앱 종료: 마지막 위치 저장
        if (cfg_.allDesk) DesktopPin::Set(false);   // 고정 해제 (ini 값은 유지)
        if (menu_) DestroyMenu(menu_);          // 창에 붙이지 않은 메뉴는 직접 해제
        PostQuitMessage(0);                     // 소유 창(도시 창)은 Windows 가 함께 닫음
    }

    // ---- 스타일 적용 (설정이 바뀌면 모든 창에 적용)
    void ApplyStyle() {
        if (font_)     DeleteObject(font_);
        if (dateFont_) DeleteObject(dateFont_);
        if (bgBrush_)  DeleteObject(bgBrush_);
        font_     = MakeFont(cfg_.timeFont, cfg_.timeSize);
        dateFont_ = MakeFont(cfg_.dateFont, cfg_.dateSize);
        bgBrush_  = CreateSolidBrush(cfg_.bgColor);
        MeasureText();
        ApplyOpacity();
        ApplyTopmost();
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void ApplyOpacity() {
        LONG_PTR ex = GetWindowLongPtrA(hwnd_, GWL_EXSTYLE);
        const bool analog = (cfg_.clockMode == 2);          // 아날로그: 원 바깥(ANALOG_KEY 색)을 완전 투명하게
        if (analog || cfg_.opacity < 100) {
            SetWindowLongPtrA(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd_, ANALOG_KEY, (BYTE)(cfg_.opacity * 255 / 100),
                                       (analog ? LWA_COLORKEY : 0) | (cfg_.opacity < 100 ? LWA_ALPHA : 0));
        } else {
            SetWindowLongPtrA(hwnd_, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        }
        RedrawWindow(hwnd_, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    void ApplyTopmost() {
        SetWindowPos(hwnd_, cfg_.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // 스크롤/이동 타이머 (간격은 ini 값). 같은 ID 로 다시 설정하면 간격이 교체됨
    void RestartTimers() {
        if (!owner_) {
            if (cfg_.moveMs > 0) SetTimer(hwnd_, TID_MOVE, cfg_.moveMs, NULL);
            else                 KillTimer(hwnd_, TID_MOVE);          // 0 = 위치 이동 안 함 (고정)
        }
        SetTimer(hwnd_, TID_SCROLL_TIME, cfg_.timeScrollMs, NULL);
        SetTimer(hwnd_, TID_SCROLL_DATE, cfg_.dateScrollMs, NULL);
    }

    // 본 창 + 모든 도시 창에 같은 동작 적용
    void ForAll(void (ClockWindow::*fn)()) {
        (this->*fn)();
        for (ClockWindow* p = head_; p; p = p->next_) (p->*fn)();
    }

    // ---- 메뉴: 설정 / Global
    static void AddMenu(HMENU m, UINT flags, UINT_PTR id, const char* utf8) {
        char16_t u[64];
        Utf8::ToUtf16(utf8, -1, u, 64);
        AppendMenuW(m, flags | MF_STRING, id, (LPCWSTR)u);
    }

    HMENU BuildMenu() {
        HMENU bar = CreatePopupMenu();          // 최상위 항목들을 담는 팝업 메뉴 (창에 붙이지 않음)
        HMENU set = menuSet_ = CreatePopupMenu();       // 앱 동작 설정
        HMENU dsn = menuDsn_ = CreatePopupMenu();       // 디지탈 시계 디자인: 바탕색 / 투명도 / 년월일·음력 보이기 / 표시 형식 / 글꼴
        menuOpacity_ = CreatePopupMenu();
        for (int p = 100; p >= 10; p -= 10) {
            char b[8];
            wsprintfA(b, "%d", p);
            lstrcatA(b, "%");
            AppendMenuA(menuOpacity_, MF_STRING, CMD_OPACITY + p / 10, b);
        }
        // (아래 menuMove_ 는 투명도 항목 바로 뒤에 추가됨)
        AddMenu(set, 0,        CMD_RESET,     "설정 초기화(&R)");
        AddMenu(set, 0,        CMD_MYPHONE,   "내 폰 등록(&P)...");      // WiFiScan 대화상자 재사용
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        menuBack_ = CreatePopupMenu();          // 바탕색 변경: 시계 바탕색과 모니터 바탕색을 동일하게(토글) / 바탕색 선택 대화상자
        AddMenu(menuBack_, 0, CMD_BACKDESK,  "시계 바탕색과 모니터 바탕색을 동일하게(&M)");
        AddMenu(menuBack_, 0, CMD_BACKCOLOR, "바탕색 선택(&C)...");
        AddMenu(dsn, MF_POPUP, (UINT_PTR)menuBack_, "바탕색 변경(&B)");
        AddMenu(dsn, MF_POPUP, (UINT_PTR)menuOpacity_, "투명도 변경(&O)");

        menuMove_ = CreatePopupMenu();          // 위치 이동 간격 (0 = 이동 안 함)
        static const char* const moveName[MOVE_COUNT] = {
            "0 (이동 안 함, 위치 고정)", "5초", "10초", "30초", "1분", "5분", "10분", "30분"};
        for (int i = 0; i < MOVE_COUNT; i++) AddMenu(menuMove_, 0, CMD_MOVE + i, moveName[i]);
        AddMenu(set, MF_POPUP, (UINT_PTR)menuMove_, "위치 이동 간격 변경(&I)");   // 디지탈/아날로그 모두 적용되므로 앱 동작 설정(F1)에 둠

        static const char* const tfName[FMT_COUNT] = {
            "HH:mm:ss", "hh:mm:ss tt", "H:mm:ss", "h:mm:ss tt", "tt hh:mm:ss",
            "tt h:mm:ss (오전/오후)", "HH:mm", "HH시 mm분 ss초", "HH.mm.ss", "HHmmss"};
        static const char* const dfName[FMT_COUNT] = {
            "yyyy-MM-dd (요일)", "yyyy-MM-dd", "yyyy.MM.dd", "yyyy/MM/dd", "yyyy년 M월 d일 (요일)",
            "yy-MM-dd", "MM/dd/yyyy", "dd/MM/yyyy", "yyyyMMdd", "ddd, MMM d, yyyy"};
        static const SYSTEMTIME sample = {2026, 11, 3, 11, 14, 5, 9, 0};     // 예시용: 2026-11-11 (수) 14:05:09
        menuTimeFmt_ = CreatePopupMenu();       // 시분초 표시 형식 10종
        menuDateFmt_ = CreatePopupMenu();       // 년월일 표시 형식 10종
        for (int i = 0; i < FMT_COUNT; i++) {
            char l[96], ex[48];
            FormatTime(sample, i, ex);
            wsprintfA(l, "%s    →  %s", tfName[i], ex);
            AddMenu(menuTimeFmt_, 0, CMD_TFMT + i, l);
            FormatDate(sample, i, ex);
            wsprintfA(l, "%s    →  %s", dfName[i], ex);
            AddMenu(menuDateFmt_, 0, CMD_DFMT + i, l);
        }
        AddMenu(dsn, MF_POPUP, (UINT_PTR)menuTimeFmt_, "시분초 표시 형식(&F)");
        AddMenu(dsn, MF_POPUP, (UINT_PTR)menuDateFmt_, "년월일 표시 형식(&N)");
        AddMenu(dsn, 0,        CMD_TIMEFONT,  "시분초 글꼴 변경(&T)...");
        AddMenu(dsn, 0,        CMD_DATEFONT,  "년월일 글꼴 변경(&D)...");
        AddMenu(set, 0,        CMD_TOPMOST,   "항상 다른 앱보다 앞에 보이기(&A)");
        AddMenu(set, 0,        CMD_ALLMON,    "모든 모니터에 적용(&M)");
        AddMenu(set, 0,        CMD_ALLDESK,   "모든 가상 데스크톱에 적용(&V)");
        AddMenu(dsn, 0,        CMD_SHOWDATE,  "년월일 보이기(&Y)");
        AddMenu(dsn, 0,        CMD_SHOWLUNAR, "음력 보이기(&U)");
        AddMenu(set, 0,        CMD_SHOWCLOCK, "시계 보이기(&H)  (해제하면 트레이에서만 동작)");
        AddMenu(set, 0,        CMD_AUTORUN,   "부팅(또는 로그온) 자동실행 on/off(&G)");
        AddMenu(set, 0,        CMD_COMPANION, "동반 실행 앱 등록(&C)...");
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        AddMenu(set, 0,        CMD_EXIT,      "프로그램 종료 (&Exit)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)set, "F2) 앱 동작 설정(&S)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)dsn, "F3) 디지탈 시계 디자인(&D)");

        menuATheme_ = CreatePopupMenu();        // 아날로그 시계 디자인: 테마
        AddMenu(menuATheme_, 0, CMD_AFULL, "전체화면 on/off(&F)");
        AddMenu(menuATheme_, 0, CMD_ANEON, "네온사인 on/off(&O)");
        AddMenu(menuATheme_, 0, CMD_ASEC, "초침 생략 on/off(&S)");
        AddMenu(menuATheme_, 0, CMD_AMATCH, "배경색 일치 on/off(&B)  (모니터 바탕색 = 시계 원 안쪽)");
        AppendMenuA(menuATheme_, MF_SEPARATOR, 0, NULL);
        for (int i = 0; i < ATHEME_COUNT; i++) AddMenu(menuATheme_, 0, CMD_ATHEME + i, ATHEMES[i].name);
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuATheme_, "F4) 아나로그 시계 디자인(&N)");

        HMENU glob = menuGlobal_ = CreatePopupMenu();   // Global: 도시를 고르면 그 도시 시계 창이 하나 더 추가됨
        for (int i = 0; i < CITY_COUNT; i++) AddMenu(glob, 0, CMD_CITY + i, CITIES[i].name);
        AddMenu(bar, MF_POPUP, (UINT_PTR)glob, "F5) Global(&G)");

        menuAlarm_ = CreatePopupMenu();         // 알람1 ~ 알람10 (각각 사용/시각/파일/지우기 서브메뉴)
        for (int i = 0; i < ALARM_COUNT; i++) MakeAlarmSub(i);
        AppendMenuA(menuAlarm_, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menuAlarm_, MF_STRING, CMD_REBOOT, "-");     // 시스템 재시작 시각 (알람 10개 + 구분선 다음, 위치 ALARM_COUNT+1). 누르면 시각/사용 대화상자
        UpdateRebootMenu();
        menuProg_ = CreatePopupMenu();          // 프로그램 실행 (위치 ALARM_COUNT+2)
        AddMenu(menuProg_, 0, CMD_PROG,     "사용(&E)");
        AddMenu(menuProg_, 0, CMD_PROG + 1, "시각 설정(&T)...");
        AddMenu(menuProg_, 0, CMD_PROG + 2, "BAT / CMD / EXE 파일 선택(&F)...");
        AddMenu(menuProg_, 0, CMD_PROG + 3, "메모 설정(&M)...");
        AppendMenuA(menuProg_, MF_SEPARATOR, 0, NULL);
        AddMenu(menuProg_, 0, CMD_PROG + 4, "알람 지우기(&C)");
        AddMenu(menuProg_, 0, CMD_PROG + 5, "알람 테스트(&R)");
        AppendMenuA(menuAlarm_, MF_POPUP | MF_STRING, (UINT_PTR)menuProg_, "-");
        UpdateProgMenu();
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuAlarm_, "F6) 알람(&A)");

        menuScan_ = CreatePopupMenu();          // WiFiScan: 모니터링 대화상자 / 자동감시 On·Off 예약 / HomeBack·NotHome 프로그램
        AddMenu(menuScan_, 0, CMD_SCAN, "WifiScan모니터링(&M)...");   // 블루투스/Wi-Fi 스캔 설정 + 진행 상황 대화상자
        AppendMenuA(menuScan_, MF_SEPARATOR, 0, NULL);
        MakeAlarmSub(ALARM_SCAN_ON);            // 위치 2, 3
        MakeAlarmSub(ALARM_SCAN_OFF);
        MakeHomeProgMenu(0);                    // 위치 4, 5
        MakeHomeProgMenu(1);
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuScan_, "F7) WiFiScan(&W)");

        HMENU ipc = CreatePopupMenu();          // IP cam / 폰 연결: IP cam 등록 / 영상 / 폰 탐색기 / 다른 PC(서버) / 다른 PC(클라이언트)
        AddMenu(ipc, 0, CMD_IPREG,  "IP cam 등록(&R)...");
        AddMenu(ipc, 0, CMD_IPVIEW, "카메라 영상(&V)");
        AddMenu(ipc, 0, CMD_PHONE,  "폰 탐색기(&E)...");
        AppendMenuA(ipc, MF_SEPARATOR, 0, NULL);
        AddMenu(ipc, 0, CMD_RDPSRV, "다른 PC(서버)(&S)...");           // 이 PC 를 원격 제어하게 함 (rdp_server)
        AddMenu(ipc, 0, CMD_RDPCLI, "다른 PC(클라이언트)(&C)...");     // 다른 PC 를 원격 제어 (rdp_client)
        AddMenu(bar, MF_POPUP, (UINT_PTR)ipc, "F8) IP cam / 폰 연결(&I)");

        HMENU help = CreatePopupMenu();         // Help
        AddMenu(help, 0, CMD_HELP,     "About(&A)");
        AddMenu(help, 0, CMD_HELP + 1, "ReadMe.MD(&R)");
        AddMenu(help, 0, CMD_HELP + 2, "홈페이지(&H)");
        AddMenu(help, 0, CMD_HELP + 3, "최신 버전 다운로드(&D)");
        AddMenu(help, 0, CMD_HELP + 4, "음력 API 키값 등록(&K)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)help, "F9) Help(&H)");
        for (int i = 0; i < TOP_MENU_COUNT; i++) menuTop_[i] = GetSubMenu(bar, i);     // F2 ~ F9 단축키용
        return bar;
    }

    // 프로그램 실행 메뉴 이름/체크 갱신
    // ---- HomeBack / NotHome 프로그램 (알람 메뉴, '프로그램 실행' 과 같은 방식에서 시각 설정만 없음)
    static const char* HomeProgTitle(int i) { return i == 0 ? "HomeBack 프로그램" : "NotHome 프로그램"; }
    static int HomeProgId(int i, int act) { return CMD_HOMEPROG + i * 10 + act; }

    void MakeHomeProgMenu(int i) {
        HMENU m = menuHomeProg_[i] = CreatePopupMenu();
        AddMenu(m, 0, HomeProgId(i, 0), "사용(&E)");
        AddMenu(m, 0, HomeProgId(i, 2), "BAT / CMD / EXE 파일 선택(&F)...");
        AddMenu(m, 0, HomeProgId(i, 3), "메모 설정(&M)...");
        AppendMenuA(m, MF_SEPARATOR, 0, NULL);
        AddMenu(m, 0, HomeProgId(i, 4), "지우기(&C)");
        AddMenu(m, 0, HomeProgId(i, 5), "테스트(&R)");
        AppendMenuA(menuScan_, MF_POPUP | MF_STRING, (UINT_PTR)m, "-");
        UpdateHomeProgMenu(i);
    }

    void UpdateHomeProgMenu(int i) {
        const Settings::HomeProg& h = cfg_.homeProg[i];
        const char* name = h.file;
        for (const char* p = h.file; *p; p++)
            if (*p == '\\' || *p == '/') name = p + 1;
        char nm[48];
        lstrcpynA(nm, name, sizeof(nm));
        char s[160];
        wsprintfA(s, "%s   %s", HomeProgTitle(i), h.file[0] ? nm : "__");
        char16_t u[160];
        Utf8::ToUtf16(s, -1, u, 160);
        const int pos = 4 + i;                      // WiFiScan 메뉴 안의 위치
        ModifyMenuW(menuScan_, pos, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)menuHomeProg_[i], (LPCWSTR)u);
        UINT chk = h.on ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(menuScan_, pos, MF_BYPOSITION | chk);
        CheckMenuItem(menuHomeProg_[i], HomeProgId(i, 0), MF_BYCOMMAND | chk);
        CheckMenuItem(menuHomeProg_[i], HomeProgId(i, 3), MF_BYCOMMAND | (h.memo[0] ? MF_CHECKED : MF_UNCHECKED));
    }

    // /HomeBack (i=0) 또는 /NotHome (i=1) 판정이 날 때 호출
    void RunHomeProg(int i) {
        const Settings::HomeProg& h = cfg_.homeProg[i];
        if (!h.on || (!h.file[0] && !h.memo[0])) return;
        RunProgFile(HomeProgTitle(i), h.file, h.memo);
    }

    void HomeProgCommand(int i, int act) {
        Settings::HomeProg& h = cfg_.homeProg[i];
        if (act == 5) { RunProgFile(HomeProgTitle(i), h.file, h.memo); return; }     // 테스트: 1회 실행 (설정 변경 없음)
        if (act == 0) {
            if (!h.on && !h.file[0] && !h.memo[0] && !PickProgFile(h.file, (int)sizeof(h.file))) return;   // 파일/메모가 없으면 먼저 고르게 함
            h.on = !h.on;
        } else if (act == 2) {
            if (!PickProgFile(h.file, (int)sizeof(h.file))) return;
        } else if (act == 3) {
            char m[sizeof(h.memo)];
            lstrcpynA(m, h.memo, sizeof(m));
            modal_ = true;
            bool ok = MemoDialog::Show(hi_, hwnd_, m, sizeof(m));
            modal_ = false;
            if (!ok) return;
            lstrcpynA(h.memo, m, sizeof(h.memo));
        } else {                                // 4: 지우기
            h.on = false;
            h.file[0] = 0;
            h.memo[0] = 0;
        }
        cfg_.SaveHomeProg(i);
        UpdateHomeProgMenu(i);
    }

    void UpdateProgMenu() {
        const char* name = cfg_.pgFile;
        for (const char* p = cfg_.pgFile; *p; p++)
            if (*p == '\\' || *p == '/') name = p + 1;
        char nm[48];
        lstrcpynA(nm, name, sizeof(nm));
        char s[160];
        wsprintfA(s, "다른 앱 호출   %02d:%02d:%02d   %s", cfg_.pgH, cfg_.pgM, cfg_.pgS, cfg_.pgFile[0] ? nm : "__");
        char16_t u[160];
        Utf8::ToUtf16(s, -1, u, 160);
        ModifyMenuW(menuAlarm_, ALARM_COUNT + 2, MF_BYPOSITION | MF_POPUP | MF_STRING,
                    (UINT_PTR)menuProg_, (LPCWSTR)u);
        UINT chk = cfg_.pgOn ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(menuAlarm_, ALARM_COUNT + 2, MF_BYPOSITION | chk);
        CheckMenuItem(menuProg_, CMD_PROG, MF_BYCOMMAND | chk);
        CheckMenuItem(menuProg_, CMD_PROG + 3, MF_BYCOMMAND | (cfg_.pgMemo[0] ? MF_CHECKED : MF_UNCHECKED));
    }

    // BAT / CMD / EXE 파일 선택
    bool PickProgFile() { return PickProgFile(cfg_.pgFile, (int)sizeof(cfg_.pgFile)); }
    bool PickProgFile(char* out, int cap) {
        static const char16_t filter[] =
            u"실행 파일 (BAT, CMD, EXE)\0*.bat;*.cmd;*.exe\0"
            u"모든 파일 (*.*)\0*.*\0";
        char16_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd_;
        ofn.lpstrFilter = (LPCWSTR)filter;
        ofn.lpstrFile   = (LPWSTR)file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        modal_ = true;
        BOOL ok = GetOpenFileNameW(&ofn);
        modal_ = false;
        if (!ok) return false;
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)file, -1, out, cap, NULL, NULL);
        return true;
    }

    // 메모 표시 + 파일 실행 (작업 폴더는 파일이 있는 폴더)
    void RunProg() { RunProgFile("다른 앱 호출", cfg_.pgFile, cfg_.pgMemo); }

    void RunProgFile(const char* title, const char* file, const char* memo) {
        Log::Put("%s: 파일=%s 메모=%s", title, file[0] ? file : "(없음)", memo[0] ? memo : "(없음)");
        if (memo[0]) MemoWindow::Show(hi_, memo, 30);
        if (!file[0]) return;
        char16_t path[MAX_PATH], dir[MAX_PATH];
        Utf8::ToUtf16(file, -1, path, MAX_PATH);
        lstrcpynW((LPWSTR)dir, (LPCWSTR)path, MAX_PATH);
        int n = lstrlenW((LPCWSTR)dir);
        while (n > 0 && dir[n - 1] != u'\\' && dir[n - 1] != u'/') n--;
        dir[n > 0 ? n - 1 : 0] = 0;
        HINSTANCE x = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)path, NULL, dir[0] ? (LPCWSTR)dir : NULL, SW_SHOWNORMAL);
        Log::Put("%s 결과: %s (결과코드 %d)", title, (INT_PTR)x > 32 ? "성공" : "실패", (int)(INT_PTR)x);
    }

    void ProgCommand(int act) {
        if (act == 5) { RunProg(); return; }    // 알람 테스트: 1회 실행 (설정 변경 없음)
        if (act == 0) {
            if (!cfg_.pgOn && !cfg_.pgFile[0] && !PickProgFile()) return;   // 파일이 없으면 먼저 고르게 함
            cfg_.pgOn = !cfg_.pgOn;
        } else if (act == 1) {
            modal_ = true;
            bool ok = TimeDialog::Show(hi_, hwnd_, "다른 앱 호출 시각 (시:분:초)", cfg_.pgH, cfg_.pgM, cfg_.pgS);
            modal_ = false;
            if (!ok) return;
        } else if (act == 2) {
            if (!PickProgFile()) return;
        } else if (act == 3) {
            char m[sizeof(cfg_.pgMemo)];
            lstrcpynA(m, cfg_.pgMemo, sizeof(m));
            modal_ = true;
            bool ok = MemoDialog::Show(hi_, hwnd_, m, sizeof(m));
            modal_ = false;
            if (!ok) return;
            lstrcpynA(cfg_.pgMemo, m, sizeof(cfg_.pgMemo));
        } else {                                // 4: 알람 지우기
            cfg_.pgOn = false;
            cfg_.pgFile[0] = 0;
            cfg_.pgMemo[0] = 0;
        }
        pgFiredYmd_ = 0;
        cfg_.SaveProg();
        UpdateProgMenu();
    }

    void UpdateRebootMenu() {
        char s[96];
        wsprintfA(s, "시스템 재시작 시각   %02d:%02d:%02d", cfg_.rbH, cfg_.rbM, cfg_.rbS);
        char16_t u[96];
        Utf8::ToUtf16(s, -1, u, 96);
        ModifyMenuW(menuAlarm_, ALARM_COUNT + 1, MF_BYPOSITION | MF_STRING | (cfg_.rbOn ? MF_CHECKED : MF_UNCHECKED),
                    CMD_REBOOT, (LPCWSTR)u);                     // '사용' 이면 메뉴 이름 앞에 체크 표시
    }

    // 시스템 재시작 시각 메뉴: 시각 + '사용' 체크박스를 한 대화상자에서 설정
    void RebootCommand() {
        modal_ = true;
        bool use = cfg_.rbOn;
        bool ok = TimeDialog::Show(hi_, hwnd_, "시스템 재시작 시각 (시:분:초)", cfg_.rbH, cfg_.rbM, cfg_.rbS, &use);
        modal_ = false;
        if (!ok) return;
        cfg_.rbOn = use;
        rbFiredYmd_ = 0;
        cfg_.SaveReboot();
        UpdateRebootMenu();
    }

    // 알람 메뉴 항목 이름/체크 갱신 (체크 = 사용 중)
    // 알람 / ScanOn / ScanOff 한 칸의 서브메뉴 만들기 (사용, 시각, 파일, 메모, 자동 닫기, 지우기, 테스트)
    void MakeAlarmSub(int i) {
        const bool sc = i >= ALARM_COUNT;
        HMENU sub = menuAlarmSub_[i] = CreatePopupMenu();
        AddMenu(sub, 0, AlarmId(i, 0), "사용(&E)");
        AddMenu(sub, 0, AlarmId(i, 1), "시각 설정(&T)...");
        AddMenu(sub, 0, AlarmId(i, 2), "동영상/음악 파일 선택(&F)...");
        AddMenu(sub, 0, AlarmId(i, 9), "메모 설정(&M)...");
        AppendMenuA(sub, MF_SEPARATOR, 0, NULL);
        AddMenu(sub, 0, AlarmId(i, 4), "플레이어 자동 닫기: 끔(&0)");
        AddMenu(sub, 0, AlarmId(i, 5), "플레이어 자동 닫기: 재생이 끝나면(&1)");
        AddMenu(sub, 0, AlarmId(i, 6), "플레이어 자동 닫기: 지정 시간 후(&2)");
        AddMenu(sub, 0, AlarmId(i, 7), "닫기 시간 설정...(&S)");
        AppendMenuA(sub, MF_SEPARATOR, 0, NULL);
        AddMenu(sub, 0, AlarmId(i, 3), sc ? "예약 지우기(&C)" : "알람 지우기(&C)");
        AddMenu(sub, 0, AlarmId(i, 8), sc ? "파일/메모 테스트(&R)  (scan 은 바꾸지 않음)" : "알람 테스트(&R)");
        AppendMenuA(i < ALARM_COUNT ? menuAlarm_ : menuScan_, MF_POPUP | MF_STRING, (UINT_PTR)sub, "-");
        UpdateAlarmMenu(i);                     // 항목 이름: 알람N  시각  파일명
    }

    static int AlarmId(int i, int act) {
        return i < ALARM_COUNT ? CMD_ALARM + i * ALARM_ACTS + act : CMD_SCANSCHED + (i - ALARM_COUNT) * ALARM_ACTS + act;
    }
    static bool DecodeAlarm(int id, int& i, int& act) {
        if (id >= CMD_ALARM && id < CMD_ALARM + ALARM_COUNT * ALARM_ACTS) { i = (id - CMD_ALARM) / ALARM_ACTS; act = (id - CMD_ALARM) % ALARM_ACTS; return true; }
        if (id >= CMD_SCANSCHED && id < CMD_SCANSCHED + 2 * ALARM_ACTS) { i = ALARM_COUNT + (id - CMD_SCANSCHED) / ALARM_ACTS; act = (id - CMD_SCANSCHED) % ALARM_ACTS; return true; }
        return false;
    }
    static const char* AlarmName(int i) {                       // 로그/메뉴용 이름: 알람N / ScanOn / ScanOff
        static char b[ALARM_TOTAL][16];
        if (!b[i][0]) {
            if (i < ALARM_COUNT) wsprintfA(b[i], "알람%d", i + 1);
            else lstrcpyA(b[i], i == ALARM_SCAN_ON ? "ScanOn" : "ScanOff");
        }
        return b[i];
    }
    static const char* AlarmTitle(int i) {                      // 메뉴에 보이는 이름
        return i < ALARM_COUNT ? AlarmName(i) : (i == ALARM_SCAN_ON ? "Wifi공유기 자동감시 On" : "Wifi공유기 자동감시 Off");
    }
    static int AlarmPos(int i) { return i < ALARM_COUNT ? i : 2 + (i - ALARM_COUNT); }     // 알람 메뉴(0~9) / WiFiScan 메뉴(모니터링, 구분선 다음 2, 3) 안의 위치

    void UpdateAlarmMenu(int i) {
        const Alarm& a = cfg_.alarms[i];
        const char* name = a.file;
        for (const char* p = a.file; *p; p++)
            if (*p == '\\' || *p == '/') name = p + 1;
        char nm[48];
        lstrcpynA(nm, name, sizeof(nm));
        char s[160];
        if (i < ALARM_COUNT) wsprintfA(s, "알람%d   %02d:%02d:%02d   %s", i + 1, a.h, a.m, a.s, a.file[0] ? nm : "__");
        else wsprintfA(s, "%s   %02d:%02d:%02d   %s", AlarmTitle(i), a.h, a.m, a.s, a.file[0] ? nm : "__");
        char16_t u[160];
        Utf8::ToUtf16(s, -1, u, 160);
        HMENU pm = i < ALARM_COUNT ? menuAlarm_ : menuScan_;
        ModifyMenuW(pm, AlarmPos(i), MF_BYPOSITION | MF_POPUP | MF_STRING,
                    (UINT_PTR)menuAlarmSub_[i], (LPCWSTR)u);
        UINT chk = a.on ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(pm, AlarmPos(i), MF_BYPOSITION | chk);
        HMENU sub = menuAlarmSub_[i];
        CheckMenuItem(sub, AlarmId(i, 0), MF_BYCOMMAND | chk);
        CheckMenuItem(sub, AlarmId(i, 9), MF_BYCOMMAND | (a.memo[0] ? MF_CHECKED : MF_UNCHECKED));   // 체크 = 메모 있음
        CheckMenuRadioItem(sub, AlarmId(i, 4), AlarmId(i, 6),
                           AlarmId(i, 4) + a.closeMode, MF_BYCOMMAND);
        wsprintfA(s, "닫기 시간 설정 (%02d:%02d:%02d)...(&S)", a.closeSec / 3600, a.closeSec % 3600 / 60, a.closeSec % 60);
        Utf8::ToUtf16(s, -1, u, 160);
        ModifyMenuW(sub, AlarmId(i, 7), MF_BYCOMMAND | MF_STRING,
                    AlarmId(i, 7), (LPCWSTR)u);
    }

    void AlarmCommand(int i, int act) {
        Alarm& a = cfg_.alarms[i];
        if (act == 8) {                         // 알람 테스트: 메모 표시 + 선택된 파일 1회 실행 (설정/사용 여부 변경 없음)
            Log::Put("%s 테스트 실행 파일=%s", AlarmName(i), a.file[0] ? a.file : "(없음)");
            if (a.memo[0]) MemoWindow::Show(hi_, a.memo, 30);
            if (a.file[0]) LaunchAlarm(i);
            return;
        }
        if (act == 9) {                         // 메모 설정
            char m[sizeof(a.memo)];
            lstrcpynA(m, a.memo, sizeof(m));
            modal_ = true;
            bool ok = MemoDialog::Show(hi_, hwnd_, m, sizeof(m));
            modal_ = false;
            if (!ok) return;
            lstrcpynA(a.memo, m, sizeof(a.memo));
            cfg_.SaveAlarm(i);
            UpdateAlarmMenu(i);
            return;
        }
        if (act == 0) {                         // 사용 On/Off (파일/메모가 없으면 먼저 파일을 고르게 함)
            if (i < ALARM_COUNT && !a.on && !a.file[0] && !a.memo[0] && !PickAlarmFile(a)) return;
            a.on = !a.on;
        } else if (act == 1) {                  // 시각 설정
            modal_ = true;
            char ttl[64];
            wsprintfA(ttl, i < ALARM_COUNT ? "알람 시각 (시:분:초)" : "%s 시각 (시:분:초)", AlarmTitle(i));
            bool ok = TimeDialog::Show(hi_, hwnd_, ttl, a.h, a.m, a.s);
            modal_ = false;
            if (!ok) return;
            if (i >= ALARM_COUNT) a.on = true;      // scan 예약은 시각을 정하면 바로 사용
        } else if (act == 2) {                  // 파일 선택
            if (!PickAlarmFile(a)) return;
        } else if (act == 3) {                  // 알람 지우기
            a.on = false;
            a.file[0] = 0;
            a.memo[0] = 0;
            if (i >= ALARM_COUNT) a.h = a.m = a.s = 0;
        } else if (act >= 4 && act <= 6) {      // 플레이어 자동 닫기: 끔 / 재생이 끝나면 / 지정 시간 후
            a.closeMode = act - 4;
        } else {                                // 7: 닫기까지 시간 (모드 2, 모드 1 에서 길이를 못 읽을 때)
            int h = a.closeSec / 3600, m = a.closeSec % 3600 / 60, s = a.closeSec % 60;
            modal_ = true;
            bool ok = TimeDialog::Show(hi_, hwnd_, "닫기까지 시간 (시:분:초)", h, m, s);
            modal_ = false;
            if (!ok) return;
            a.closeSec = h * 3600 + m * 60 + s;
        }
        firedYmd_[i] = 0;                       // 오늘 울릴 기회를 다시 허용
        cfg_.SaveAlarm(i);
        UpdateAlarmMenu(i);
    }

    // 동영상/음악 파일 선택 (경로는 UTF-8 로 보관)
    bool PickAlarmFile(Alarm& a) {
        static const char16_t filter[] =
            u"동영상/음악 파일\0*.mp3;*.wav;*.wma;*.flac;*.m4a;*.aac;*.ogg;*.mp4;*.mkv;*.avi;*.wmv;*.mov;*.webm;*.mpg;*.mpeg\0"
            u"모든 파일 (*.*)\0*.*\0";
        char16_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd_;
        ofn.lpstrFilter = (LPCWSTR)filter;
        ofn.lpstrFile   = (LPWSTR)file;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        modal_ = true;
        BOOL ok = GetOpenFileNameW(&ofn);
        modal_ = false;
        if (!ok) return false;
        WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)file, -1, a.file, sizeof(a.file), NULL, NULL);
        return true;
    }

    // 매초 호출: 시각이 된 알람은 시스템 기본 플레이어로 파일 실행 (하루 한 번, 사용 중이면 매일 반복)
    void CheckAlarms() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        int now = t.wHour * 3600 + t.wMinute * 60 + t.wSecond;
        int ymd = t.wYear * 10000 + t.wMonth * 100 + t.wDay;
        for (int i = 0; i < ALARM_TOTAL; i++) {
            const Alarm& a = cfg_.alarms[i];
            const bool sc = i >= ALARM_COUNT;   // ScanOn / ScanOff 는 파일/메모 없이 시각만 있어도 동작
            if (!a.on || (!sc && !a.file[0] && !a.memo[0]) || firedYmd_[i] == ymd) continue;
            int d = now - (a.h * 3600 + a.m * 60 + a.s);
            if (d < 0 || d > 5) continue;       // 타이머가 조금 늦어도 놓치지 않도록 5초 여유
            firedYmd_[i] = ymd;
            Log::Put("%s 시각 도달 %02d:%02d:%02d 파일=%s 메모=%s", AlarmName(i), a.h, a.m, a.s,
                     a.file[0] ? a.file : "(없음)", a.memo[0] ? a.memo : "(없음)");
            if (sc) ScanSchedule(i);            // scan On/Off 먼저, 이어서 메모/음악·동영상
            if (a.memo[0]) MemoWindow::Show(hi_, a.memo, 30);
            if (a.file[0]) LaunchAlarm(i);
        }
        if (cfg_.pgOn && (cfg_.pgFile[0] || cfg_.pgMemo[0]) && pgFiredYmd_ != ymd) {   // 프로그램 실행
            int d = now - (cfg_.pgH * 3600 + cfg_.pgM * 60 + cfg_.pgS);
            if (d >= 0 && d <= 5) {
                pgFiredYmd_ = ymd;
                RunProg();
            }
        }
        if (cfg_.rbOn && rbFiredYmd_ != ymd) {  // 시스템 재시작 (재부팅 직후 재실행되어도 5초 여유가 지나 다시 실행되지 않음)
            int d = now - (cfg_.rbH * 3600 + cfg_.rbM * 60 + cfg_.rbS);
            if (d >= 0 && d <= 5) {
                rbFiredYmd_ = ymd;
                ConfirmReboot();
            }
        }
    }

    // 예약 시각이 된 scan On / Off 처리 (이미 그 상태면 그대로 두고 상태줄에만 표시)
    void ScanSchedule(int i) {
        if (!scan_) return;
        const Alarm& a = cfg_.alarms[i];
        char why[48];
        wsprintfA(why, "예약 %02d:%02d:%02d", a.h, a.m, a.s);
        if (i == ALARM_SCAN_ON) {
            if (scan_->IsOn()) ScanEvents::Status("scan On 예약 - 이미 실행 중");
            else scan_->Start(why);
        } else {
            if (!scan_->IsOn()) ScanEvents::Status("scan Off 예약 - 이미 정지 상태");
            else scan_->Stop(why);
        }
    }

    // 재시작 확인창: 300초 동안 Yes/No 대기(남은 시간 프로그레스 바). Yes 또는 무응답(시간 초과) = 즉시 재시작, No = 재시작 안 함
    void ConfirmReboot() {
        modal_ = true;
        Log::Put("시스템 재시작 시각 도달: 확인창 표시 (300초 대기)");
        int r = ConfirmDialog::Show(hi_, 300, "시스템 재시작", "시스템을 재시작 하겠습니까? (Yes/No)", "무응답 시 재시작", true);
        modal_ = false;
        Log::Put("시스템 재시작 확인 결과: %s", r == IDNO ? "No -> 재시작 안 함" : "Yes 또는 시간 초과 -> 재시작 실행");
        if (r != IDNO) {
            HINSTANCE x = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)u"shutdown.exe", (LPCWSTR)u"/r /f /t 0", NULL, SW_HIDE);
            Log::Put("shutdown /r /f /t 0 실행 (결과코드 %d)", (int)(INT_PTR)x);
        }
    }

    // ---- 알람 실행 + 플레이어 자동 닫기
    // 닫기 시각 = (모드 1) 파일 길이 + 여유 3초, 길이를 못 읽으면 closeSec / (모드 2) closeSec.
    // 플레이어 창은 실행 전 보이던 창과 비교해 "새로 생긴 창"을 실행 후 8초 동안 찾아 기억하고, 때가 되면 WM_CLOSE 를 보낸다.
    // (이미 열려 있던 플레이어 창을 재사용하면 새 창이 없으므로 닫지 않는다)
    struct Pending {
        bool  active = false;
        DWORD launchTick = 0;
        DWORD closeAfterMs = 0;
        HWND  wins[8];
        int   nWins = 0;
        HWND* before = NULL;                    // 실행 전 보이던 최상위 창 (탐지가 끝나면 해제)
        int   nBefore = 0;
    };

    struct EnumCtx { HWND* out; int n; int cap; };
    static BOOL CALLBACK EnumVisible(HWND h, LPARAM l) {
        EnumCtx* c = (EnumCtx*)l;
        if (IsWindowVisible(h) && c->n < c->cap) c->out[c->n++] = h;
        return TRUE;
    }

    static bool InList(const HWND* a, int n, HWND h) {
        for (int i = 0; i < n; i++) if (a[i] == h) return true;
        return false;
    }

    static void FreePending(Pending& p) {
        if (p.before) free(p.before);
        p.before = NULL;
        p.nBefore = 0;
        p.active = false;
    }

    // 파일 재생 시간(ms). 셸 속성(System.Media.Duration)에서 읽음. 못 읽으면 0
    static DWORD DurationMs(const char* utf8) {
        static const GUID IID_PropStore = {0x886D8EEB, 0x8CF2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};
        PROPERTYKEY key = {{0x64440490, 0x4C8B, 0x11D1, {0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03}}, 3};
        char16_t w[MAX_PATH];
        Utf8::ToUtf16(utf8, -1, w, MAX_PATH);
        DWORD ms = 0;
        HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IPropertyStore* ps = NULL;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName((PCWSTR)w, NULL, GPS_DEFAULT, IID_PropStore, (void**)&ps)) && ps) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(key, &pv)) && pv.vt == VT_UI8) {
                unsigned long long t = pv.uhVal.QuadPart / 10000ULL;     // 100ns -> ms
                if (t > 0 && t < 0x7FFFFFFFULL) ms = (DWORD)t;
            }
            PropVariantClear(&pv);
            ps->Release();
        }
        if (SUCCEEDED(co)) CoUninitialize();
        return ms;
    }

    void LaunchAlarm(int i) {
        const Alarm& a = cfg_.alarms[i];
        Pending& p = pend_[i];
        FreePending(p);

        if (a.closeMode != 0) {                 // 실행 전에 보이던 창 목록을 저장
            p.before = (HWND*)malloc(sizeof(HWND) * 512);
            if (p.before) {
                EnumCtx c = {p.before, 0, 512};
                EnumWindows(EnumVisible, (LPARAM)&c);
                p.nBefore = c.n;
            }
        }

        char16_t path[MAX_PATH];
        Utf8::ToUtf16(a.file, -1, path, MAX_PATH);
        HINSTANCE r = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)path, NULL, NULL, SW_SHOWNORMAL);
        Log::Put("%s 파일 실행: %s (결과코드 %d)", AlarmName(i), (INT_PTR)r > 32 ? "성공" : "실패", (int)(INT_PTR)r);
        if ((INT_PTR)r <= 32 || a.closeMode == 0 || !p.before) {   // 실행 실패 / 자동 닫기 끔
            FreePending(p);
            return;
        }

        DWORD after = 0;
        if (a.closeMode == 1) {
            DWORD dur = DurationMs(a.file);
            after = dur ? dur + 3000 : (DWORD)a.closeSec * 1000;   // 길이를 못 읽으면 지정 시간으로 대체
        } else {
            after = (DWORD)a.closeSec * 1000;
        }
        if (after == 0) {                       // 닫을 시각을 정할 수 없음
            FreePending(p);
            return;
        }
        Log::Put("%s 플레이어 자동 닫기 예약: %d초 후 (모드 %d)", AlarmName(i), (int)(after / 1000), a.closeMode);
        p.active = true;
        p.launchTick = GetTickCount();
        p.closeAfterMs = after;
        p.nWins = 0;
        SetTimer(hwnd_, TID_CLOSE, 1000, NULL);
    }

    // 실행 후 새로 생긴 플레이어 창 찾기: 보이는 최상위 창, 소유 창/도구 창 아님, 제목 있음, 이 앱의 창 아님
    void DetectWindows(Pending& p) {
        HWND cur[512];
        EnumCtx c = {cur, 0, 512};
        EnumWindows(EnumVisible, (LPARAM)&c);
        DWORD me = GetCurrentProcessId();
        for (int k = 0; k < c.n && p.nWins < 8; k++) {
            HWND w = cur[k];
            if (InList(p.before, p.nBefore, w) || InList(p.wins, p.nWins, w)) continue;
            if (GetWindow(w, GW_OWNER)) continue;
            if (GetWindowLongPtrA(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) continue;
            if (GetWindowTextLengthW(w) == 0) continue;
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == me) continue;
            p.wins[p.nWins++] = w;
        }
    }

    // 1초마다: 새 창 탐지(실행 후 8초 동안) / 닫을 시각이 된 알람의 플레이어 창에 WM_CLOSE
    void ProcessPending() {
        bool any = false;
        for (int i = 0; i < ALARM_TOTAL; i++) {
            Pending& p = pend_[i];
            if (!p.active) continue;
            DWORD el = GetTickCount() - p.launchTick;
            if (p.before) {
                if (el <= 8000) DetectWindows(p);
                else { free(p.before); p.before = NULL; p.nBefore = 0; }   // 탐지 종료
            }
            if (el >= p.closeAfterMs) {
                int sent = 0;
                for (int k = 0; k < p.nWins; k++)
                    if (IsWindow(p.wins[k])) { PostMessage(p.wins[k], WM_CLOSE, 0, 0); sent++; }
                Log::Put("%s 플레이어 창 닫기: 대상 %d개 중 %d개에 WM_CLOSE 전송", AlarmName(i), p.nWins, sent);
                FreePending(p);
                continue;
            }
            any = true;
        }
        if (!any) KillTimer(hwnd_, TID_CLOSE);
    }

    void CheckOpacity() {                       // 현재 투명도에 가장 가까운 항목에 표시
        if (!menuOpacity_) return;
        int n = (cfg_.opacity + 5) / 10;
        if (n < 1) n = 1;
        if (n > 10) n = 10;
        CheckMenuRadioItem(menuOpacity_, CMD_OPACITY + 1, CMD_OPACITY + 10,
                           CMD_OPACITY + n, MF_BYCOMMAND);
    }

    // 이미 표시 중인 도시인가 (기준 도시 포함)
    bool HasCity(int idx) const {
        if (lstrcmpA(cfg_.baseCity, CITIES[idx].name) == 0) return true;
        for (const ClockWindow* p = head_; p; p = p->next_)
            if (lstrcmpA(p->city_, CITIES[idx].name) == 0) return true;
        return false;
    }

    // Global 메뉴: 표시 중인 도시 앞에 체크 표시
    void UpdateCityChecks() {
        if (!menuGlobal_) return;
        for (int i = 0; i < CITY_COUNT; i++)
            CheckMenuItem(menuGlobal_, CMD_CITY + i,
                          MF_BYCOMMAND | (HasCity(i) ? MF_CHECKED : MF_UNCHECKED));
    }

    void CheckFormats() {
        if (!menuTimeFmt_) return;
        CheckMenuRadioItem(menuTimeFmt_, CMD_TFMT, CMD_TFMT + FMT_COUNT - 1, CMD_TFMT + cfg_.timeFmt, MF_BYCOMMAND);
        CheckMenuRadioItem(menuDateFmt_, CMD_DFMT, CMD_DFMT + FMT_COUNT - 1, CMD_DFMT + cfg_.dateFmt, MF_BYCOMMAND);
    }

    // 시분초 표시 형식 10종 (t 의 시각을 UTF-8 문자열로)
    static void FormatTime(const SYSTEMTIME& t, int fmt, char* b) {
        int h12 = t.wHour % 12;
        if (h12 == 0) h12 = 12;                 // 0시 -> 12 AM, 12시 -> 12 PM
        const char* ap = t.wHour < 12 ? "AM" : "PM";
        const char* kr = t.wHour < 12 ? "오전" : "오후";
        int H = t.wHour, M = t.wMinute, S = t.wSecond;
        switch (fmt) {
        case 1:  wsprintfA(b, "%02d:%02d:%02d %s", h12, M, S, ap); break;
        case 2:  wsprintfA(b, "%d:%02d:%02d", H, M, S); break;
        case 3:  wsprintfA(b, "%d:%02d:%02d %s", h12, M, S, ap); break;
        case 4:  wsprintfA(b, "%s %02d:%02d:%02d", ap, h12, M, S); break;
        case 5:  wsprintfA(b, "%s %d:%02d:%02d", kr, h12, M, S); break;
        case 6:  wsprintfA(b, "%02d:%02d", H, M); break;
        case 7:  wsprintfA(b, "%02d시 %02d분 %02d초", H, M, S); break;
        case 8:  wsprintfA(b, "%02d.%02d.%02d", H, M, S); break;
        case 9:  wsprintfA(b, "%02d%02d%02d", H, M, S); break;
        default: wsprintfA(b, "%02d:%02d:%02d", H, M, S); break;
        }
    }

    // 년월일 표시 형식 10종 (도시 이름은 그리는 쪽에서 앞에 붙임)
    static void FormatDate(const SYSTEMTIME& t, int fmt, char* b) {
        static const char* const WD[] = {"일", "월", "화", "수", "목", "금", "토"};
        static const char* const WE[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* const ME[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int Y = t.wYear, M = t.wMonth, D = t.wDay, w = t.wDayOfWeek % 7;
        switch (fmt) {
        case 1:  wsprintfA(b, "%04d-%02d-%02d", Y, M, D); break;
        case 2:  wsprintfA(b, "%04d.%02d.%02d", Y, M, D); break;
        case 3:  wsprintfA(b, "%04d/%02d/%02d", Y, M, D); break;
        case 4:  wsprintfA(b, "%04d년 %d월 %d일 (%s)", Y, M, D, WD[w]); break;
        case 5:  wsprintfA(b, "%02d-%02d-%02d", Y % 100, M, D); break;
        case 6:  wsprintfA(b, "%02d/%02d/%04d", M, D, Y); break;
        case 7:  wsprintfA(b, "%02d/%02d/%04d", D, M, Y); break;
        case 8:  wsprintfA(b, "%04d%02d%02d", Y, M, D); break;
        case 9:  wsprintfA(b, "%s, %s %d, %04d", WE[w], ME[M % 13], D, Y); break;
        default: wsprintfA(b, "%04d-%02d-%02d (%s)", Y, M, D, WD[w]); break;
        }
    }

    // 시각 표시 방식이 바뀌면 글자 폭(스크롤 범위)을 다시 재고 다시 그림
    void ApplyFormat() {
        MeasureText();
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void CheckMoveInterval() {                // 현재 이동 간격과 같은 항목에 표시 (없으면 모두 해제)
        if (!menuMove_) return;
        int sel = -1;
        for (int i = 0; i < MOVE_COUNT; i++) {
            bool on = (MoveChoice(i) == cfg_.moveMs);
            CheckMenuItem(menuMove_, CMD_MOVE + i, MF_BYCOMMAND | MF_UNCHECKED);
            if (on) sel = i;
        }
        if (sel >= 0)
            CheckMenuRadioItem(menuMove_, CMD_MOVE, CMD_MOVE + MOVE_COUNT - 1,
                               CMD_MOVE + sel, MF_BYCOMMAND);
    }

    void UpdateOptionChecks() {
        if (!menuSet_) return;
        if (menuATheme_) CheckMenuItem(menuATheme_, CMD_ASEC, MF_BYCOMMAND | (cfg_.analogNoSec ? MF_CHECKED : MF_UNCHECKED));
        if (menuATheme_) {
            EnableMenuItem(menuATheme_, CMD_AMATCH, MF_BYCOMMAND | (cfg_.deskSolid || cfg_.analogMatch ? MF_ENABLED : MF_GRAYED));   // 단색이 아니면 켤 수 없음
            CheckMenuItem(menuATheme_, CMD_AMATCH, MF_BYCOMMAND | (cfg_.analogMatch ? MF_CHECKED : MF_UNCHECKED));
        }
        if (menuATheme_) CheckMenuItem(menuATheme_, CMD_AFULL, MF_BYCOMMAND | (cfg_.analogFull ? MF_CHECKED : MF_UNCHECKED));
        if (menuATheme_) CheckMenuItem(menuATheme_, CMD_ANEON, MF_BYCOMMAND | (cfg_.analogNeon ? MF_CHECKED : MF_UNCHECKED));
        if (menuATheme_) CheckMenuRadioItem(menuATheme_, CMD_ATHEME, CMD_ATHEME + ATHEME_COUNT - 1,
                                            CMD_ATHEME + cfg_.analogTheme, MF_BYCOMMAND);
        if (menu_) {                            // 아날로그 모드: 디지탈 시계 디자인 비활성 / 디지탈 모드: 아나로그 시계 디자인 비활성
            EnableMenuItem(menu_, TOP_DIGITAL, MF_BYPOSITION | (cfg_.clockMode == 2 ? MF_GRAYED : MF_ENABLED));
            EnableMenuItem(menu_, TOP_ANALOG,  MF_BYPOSITION | (cfg_.clockMode == 2 ? MF_ENABLED : MF_GRAYED));
        }
        CheckMenuItem(menuSet_, CMD_TOPMOST, MF_BYCOMMAND | (cfg_.topmost ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_ALLMON,  MF_BYCOMMAND | (cfg_.allMon  ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_ALLDESK, MF_BYCOMMAND | (cfg_.allDesk ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menuDsn_, CMD_SHOWLUNAR, MF_BYCOMMAND | (cfg_.LunarReady() ? MF_ENABLED : MF_GRAYED));   // 주소/키가 없으면 비활성
        CheckMenuItem(menuDsn_, CMD_SHOWLUNAR, MF_BYCOMMAND | (cfg_.LunarOn() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuDsn_, CMD_SHOWDATE, MF_BYCOMMAND | (cfg_.showDate ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_SHOWCLOCK, MF_BYCOMMAND | (cfg_.visible ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_COMPANION, MF_BYCOMMAND | (cfg_.HasCompanionApp() ? MF_CHECKED : MF_UNCHECKED));   // 등록되어 있으면 체크
        CheckMenuItem(menuSet_, CMD_AUTORUN, MF_BYCOMMAND | (IsAutoRun() ? MF_CHECKED : MF_UNCHECKED));   // 레지스트리 기준 (기본 off)
    }

    // 메뉴 명령을 사람이 읽을 수 있는 문장으로 로그에 기록
    static void LogCommand(int id) {
        const char* n = NULL;
        int ai = 0, aact = 0;
        switch (id) {
        case CMD_EXIT:      n = "앱 동작 설정 > 프로그램 종료"; break;
        case CMD_SHOWCLOCK: n = "시계 보이기/숨기기 토글"; break;
        case CMD_SCAN:      n = "WiFiScan 대화상자"; break;
        case CMD_MYPHONE:   n = "앱 동작 설정 > 내 폰 등록"; break;
        case CMD_IPREG:     n = "IP cam > IP cam 등록"; break;
        case CMD_IPVIEW:    n = "IP cam / 폰 연결 > 카메라 영상"; break;
        case CMD_PHONE:     n = "IP cam / 폰 연결 > 폰 탐색기"; break;
        case CMD_RDPSRV:    n = "IP cam / 폰 연결 > 다른 PC(서버)"; break;
        case CMD_RDPCLI:    n = "IP cam / 폰 연결 > 다른 PC(클라이언트)"; break;
        case CMD_SHOWDATE:  n = "년월일 보이기/숨기기 토글"; break;
        case CMD_SHOWLUNAR: n = "음력 보이기/숨기기 토글"; break;
        case CMD_RESET:     n = "설정 초기화"; break;
        case CMD_ASEC:      n = "F4) 아나로그 시계 디자인 > 초침 생략 on/off"; break;
        case CMD_AMATCH:    n = "F4) 아나로그 시계 디자인 > 배경색 일치 on/off"; break;
        case CMD_AFULL:     n = "F4) 아나로그 시계 디자인 > 전체화면 on/off"; break;
        case CMD_ANEON:     n = "F4) 아나로그 시계 디자인 > 네온사인 on/off"; break;
        case CMD_BACKCOLOR: n = "바탕색 선택"; break;
        case CMD_BACKDESK:  n = "시계 바탕색과 모니터 바탕색을 동일하게 토글"; break;
        case CMD_TIMEFONT:  n = "시분초 글꼴 변경"; break;
        case CMD_DATEFONT:  n = "년월일 글꼴 변경"; break;
        case CMD_TOPMOST:   n = "항상 앞에 보이기 토글"; break;
        case CMD_ALLMON:    n = "모든 모니터 적용 토글"; break;
        case CMD_ALLDESK:   n = "모든 가상 데스크톱 적용 토글"; break;
        case CMD_AUTORUN:   n = "자동 실행 on/off 토글"; break;
        case CMD_COMPANION: n = "동반 실행 앱 등록"; break;
        }
        if (n) Log::Put("메뉴: %s", n);
        else if (id > CMD_OPACITY && id <= CMD_OPACITY + 10) Log::Put("메뉴: 투명도 %d%%", (id - CMD_OPACITY) * 10);
        else if (id >= CMD_HELP && id <= CMD_HELP + 4) {
            static const char* const A[] = {"About", "ReadMe.MD", "홈페이지", "최신 버전 다운로드", "음력 API 키값 등록"};
            Log::Put("메뉴: Help > %s", A[id - CMD_HELP]);
        }
        else if (id >= CMD_TFMT && id < CMD_TFMT + FMT_COUNT) Log::Put("메뉴: 시분초 표시 형식 %d", id - CMD_TFMT);
        else if (id >= CMD_DFMT && id < CMD_DFMT + FMT_COUNT) Log::Put("메뉴: 년월일 표시 형식 %d", id - CMD_DFMT);
        else if (id >= CMD_CITY && id < CMD_CITY + CITY_COUNT) Log::Put("메뉴: Global 도시 선택 %s", CITIES[id - CMD_CITY].name);
        else if (id >= CMD_ATHEME && id < CMD_ATHEME + ATHEME_COUNT) Log::Put("메뉴: 아나로그 시계 디자인 > %s", ATHEMES[id - CMD_ATHEME].name);
        else if (id >= CMD_MOVE && id < CMD_MOVE + MOVE_COUNT) Log::Put("메뉴: 위치 이동 간격 %dms", MoveChoice(id - CMD_MOVE));
        else if (id >= CMD_PROG && id <= CMD_PROG + 5) {
            static const char* const A[] = {"사용 토글", "시각 설정", "파일 선택", "메모 설정", "알람 지우기", "알람 테스트"};
            Log::Put("메뉴: 알람 / 다른 앱 호출 > %s", A[id - CMD_PROG]);
        } else if (id == CMD_REBOOT)
            Log::Put("메뉴: 시스템 재시작 시각 (시각 설정 + 사용)");
        else if (id >= CMD_HOMEPROG && id < CMD_HOMEPROG + 20) {
            static const char* const A[6] = {"사용 토글", "", "파일 선택", "메모 설정", "지우기", "테스트"};
            Log::Put("메뉴: %s > %s", HomeProgTitle((id - CMD_HOMEPROG) / 10), A[id % 10 < 6 ? id % 10 : 1]);
        }
        else if (DecodeAlarm(id, ai, aact)) {
            static const char* const A[ALARM_ACTS] = {"사용 토글", "시각 설정", "파일 선택", "알람 지우기",
                "자동 닫기 끔", "자동 닫기 재생이 끝나면", "자동 닫기 지정 시간 후", "닫기 시간 설정", "알람 테스트", "메모 설정"};
            Log::Put("메뉴: %s > %s", AlarmName(ai), A[aact]);
        }
    }

    void OnCommand(int id) {
        int ai = 0, aact = 0;
        LogCommand(id);
        if (id == CMD_EXIT)            ConfirmExit();
        else if (id == CMD_SHOWCLOCK)  SetClockVisible(!cfg_.visible);
        else if (id == CMD_SCAN)       ScanCommand();
        else if (id == CMD_MYPHONE)    ScanCommand(true);
        else if (id == CMD_IPREG)      IpCamRegister();
        else if (id == CMD_IPVIEW)     IpCamView();
        else if (id == CMD_PHONE)      PhoneExplorer();
        else if (id == CMD_RDPSRV)     RdpServerCommand();
        else if (id == CMD_RDPCLI)     RdpClientCommand();
        else if (id >= CMD_HELP && id <= CMD_HELP + 4) HelpCommand(id - CMD_HELP);
        else if (id == CMD_SHOWLUNAR) {
            if (!cfg_.LunarReady()) return;
            cfg_.showLunar = !cfg_.showLunar;
            cfg_.WriteInt("Option", "ShowLunar", cfg_.showLunar);
            ResizeAll();                        // 음력 줄만큼 높이 조정
            ForAll(&ClockWindow::ApplyFormat);
            UpdateOptionChecks();
        }
        else if (id == CMD_SHOWDATE) {
            cfg_.showDate = !cfg_.showDate;
            cfg_.WriteInt("Option", "ShowDate", cfg_.showDate);
            ResizeAll();                        // 숨기면 년월일 영역만큼 높이 축소
            ForAll(&ClockWindow::ApplyFormat);  // 모든 창 다시 그리기
            UpdateOptionChecks();
        }
        else if (id == CMD_RESET)      ResetSettings();
        else if (id == CMD_BACKCOLOR)  PickBackColor();
        else if (id == CMD_BACKDESK)   ToggleBackFollow();
        else if (id == CMD_TIMEFONT)   PickFont(cfg_.timeFont, (int)sizeof(cfg_.timeFont), cfg_.timeSize, cfg_.timeColor);
        else if (id == CMD_DATEFONT)   PickFont(cfg_.dateFont, (int)sizeof(cfg_.dateFont), cfg_.dateSize, cfg_.dateColor);
        else if (id > CMD_OPACITY && id <= CMD_OPACITY + 10) SetOpacity((id - CMD_OPACITY) * 10);
        else if (id == CMD_TOPMOST)    {
            cfg_.topmost = !cfg_.topmost;
            ForAll(&ClockWindow::ApplyTopmost);
            UpdateOptionChecks();
            cfg_.WriteInt("Option", "AlwaysOnTop", cfg_.topmost);
        }
        else if (id == CMD_ALLMON)     {
            cfg_.allMon = !cfg_.allMon;
            UpdateOptionChecks();
            cfg_.WriteInt("Option", "AllMonitors", cfg_.allMon);
        }
        else if (id == CMD_ALLDESK)    ToggleAllDesktops();
        else if (id == CMD_AUTORUN)    ToggleAutoRun();
        else if (id == CMD_COMPANION)  RegisterCompanion();
        else if (id >= CMD_CITY && id < CMD_CITY + CITY_COUNT) AddCity(id - CMD_CITY, true);
        else if (id == CMD_ASEC) {
            cfg_.analogNoSec = !cfg_.analogNoSec;
            cfg_.SaveAnalogNoSec();
            UpdateOptionChecks();
            ForAll(&ClockWindow::ScheduleSecondTick);   // 분 단위 <-> 초 단위 갱신 간격 즉시 반영
            ForAll(&ClockWindow::ApplyFormat);
        }
        else if (id == CMD_AMATCH) {
            RefreshDeskColor(false);
            if (!cfg_.analogMatch && !cfg_.deskSolid) { UpdateOptionChecks(); return; }      // 모니터 바탕이 그림이면 켜지 않음
            cfg_.analogMatch = !cfg_.analogMatch;
            cfg_.SaveAnalogMatch();
            UpdateOptionChecks();
            ForAll(&ClockWindow::ApplyFormat);
        }
        else if (id == CMD_AFULL) {
            cfg_.analogFull = !cfg_.analogFull;
            cfg_.SaveAnalogFull();
            ApplyAnalogFull();
            UpdateOptionChecks();
        }
        else if (id == CMD_ANEON) {
            cfg_.analogNeon = !cfg_.analogNeon;
            cfg_.SaveAnalogNeon();
            UpdateOptionChecks();
            ForAll(&ClockWindow::ApplyFormat);
        }
        else if (id >= CMD_ATHEME && id < CMD_ATHEME + ATHEME_COUNT) {
            cfg_.analogTheme = id - CMD_ATHEME;
            cfg_.SaveAnalogTheme();
            UpdateOptionChecks();
            ForAll(&ClockWindow::ApplyFormat);
        }
        else if (id >= CMD_TFMT && id < CMD_TFMT + FMT_COUNT) {
            cfg_.timeFmt = id - CMD_TFMT;
            ForAll(&ClockWindow::ApplyFormat);
            CheckFormats();
            cfg_.WriteInt("Option", "TimeFormat", cfg_.timeFmt);
        }
        else if (id >= CMD_DFMT && id < CMD_DFMT + FMT_COUNT) {
            cfg_.dateFmt = id - CMD_DFMT;
            ForAll(&ClockWindow::ApplyFormat);
            CheckFormats();
            cfg_.WriteInt("Option", "DateFormat", cfg_.dateFmt);
        }
        else if (id >= CMD_PROG && id <= CMD_PROG + 5)
            ProgCommand(id - CMD_PROG);
        else if (id == CMD_REBOOT)
            RebootCommand();
        else if (DecodeAlarm(id, ai, aact))
            AlarmCommand(ai, aact);
        else if (id >= CMD_HOMEPROG && id < CMD_HOMEPROG + 20 && id % 10 != 1 && id % 10 < 6)
            HomeProgCommand((id - CMD_HOMEPROG) / 10, id % 10);
        else if (id >= CMD_MOVE && id < CMD_MOVE + MOVE_COUNT) {
            cfg_.moveMs = MoveChoice(id - CMD_MOVE);
            RestartTimers();                    // 0 이면 타이머 중지 = 위치 고정
            CheckMoveInterval();
            cfg_.WriteInt("Timer", "MoveMs", cfg_.moveMs);
        }
    }

    // 클라이언트 높이: 시분초 영역 90 + 년월일/음력 줄 (없으면 90, 한 줄 150, 두 줄 180)
    // (년월일 + 음력) 표시 줄 수에 따라 150 / 180 / 90
    bool ShowLunarHere() const { return !owner_ && cfg_.LunarOn(); }   // 음력은 본 시계에만 (Global 도시 시계 제외)
    enum { STATUS_H = 22, ANALOG_SIZE = 200 };   // STATUS_H: 본 창 맨 아래 상태줄 높이 / ANALOG_SIZE: 아날로그 모드의 정사각형 한 변
    int StatusH() const { return owner_ ? 0 : STATUS_H; }
    int ClientH() const { return cfg_.clockMode == 2 ? AnalogSide() : ClientH(ShowLunarHere()) + StatusH(); }

    // 아날로그 한 변: 보통 ANALOG_SIZE, 전체화면이면(본 창만) 창이 있는 모니터의 수직 높이
    int AnalogSide() const {
        if (owner_ || !cfg_.analogFull) return cfg_.analogSize;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(hwnd_ ? MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        return (int)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    }

    // 휠 한 칸(120)마다 한 변을 약 8% 키우거나 줄임. 범위 80px ~ 모니터 높이. 모든 아날로그 창이 같은 크기, 각 창의 중심은 유지
    void WheelAnalog(int delta) {
        ClockWindow* root = owner_ ? owner_ : this;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        const int maxSz = (int)(mi.rcMonitor.bottom - mi.rcMonitor.top);
        int sz = cfg_.analogSize;
        const int notch = delta / 120 != 0 ? delta / 120 : (delta > 0 ? 1 : -1);
        int nsz = sz * (100 + 8 * notch) / 100;
        if (nsz == sz) nsz += notch > 0 ? 1 : -1;
        nsz = nsz < 80 ? 80 : (nsz > maxSz ? maxSz : nsz);
        if (nsz == sz) return;
        cfg_.analogSize = nsz;
        cfg_.SaveAnalogSize();

        ClockWindow* w[CITY_COUNT + 1];
        RECT old[CITY_COUNT + 1];
        bool adj[CITY_COUNT + 1] = {};
        int n = 0;
        w[n++] = root;
        for (ClockWindow* p = root->head_; p && n < CITY_COUNT + 1; p = p->next_) w[n++] = p;
        for (int i = 0; i < n; i++) GetWindowRect(w[i]->hwnd_, &old[i]);
        for (int i = 1; i < n; i++) adj[i] = old[i].left == old[i - 1].left && old[i].top == old[i - 1].bottom;
        int prevBottom = 0, prevLeft = 0;
        for (int i = 0; i < n; i++) {
            int nl = (old[i].left + old[i].right) / 2 - nsz / 2, nt = (old[i].top + old[i].bottom) / 2 - nsz / 2;
            if (i > 0 && adj[i]) { nl = prevLeft; nt = prevBottom; }       // 아래로 쌓인 도시 창은 바로 밑에 다시 붙임
            SetWindowPos(w[i]->hwnd_, NULL, nl, nt, nsz, nsz, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(w[i]->hwnd_, NULL, FALSE);
            prevLeft = nl;
            prevBottom = nt + nsz;
        }
        root->SavePos();
        Log::Put("아나로그 크기 %d px", nsz);
    }

    // 전체화면 on: 모니터 중앙(가로)에 고정하고 한 변을 모니터 높이로 / off: 이전 위치와 기본 크기로 복원
    void ApplyAnalogFull() {
        if (cfg_.analogFull) {
            RECT wr;
            GetWindowRect(hwnd_, &wr);
            preFull_.x = wr.left;
            preFull_.y = wr.top;
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
            const int side = (int)(mi.rcMonitor.bottom - mi.rcMonitor.top);
            SetWindowPos(hwnd_, NULL, mi.rcMonitor.left + ((int)(mi.rcMonitor.right - mi.rcMonitor.left) - side) / 2, mi.rcMonitor.top,
                         side, side, SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hwnd_, NULL, preFull_.x, preFull_.y, cfg_.analogSize, cfg_.analogSize, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(hwnd_, NULL, FALSE);
        Log::Put("아나로그 전체화면 %s", cfg_.analogFull ? "on" : "off");
    }
    int ClientH(bool lunar) const {
        int lines = (cfg_.showDate ? 1 : 0) + (lunar ? 1 : 0);
        return lines == 0 ? 90 : (lines == 1 ? 150 : 180);
    }

    // 모든 창의 높이를 현재 ClientH() 로 바꾸고, 아래로 쌓인 도시 창은 바로 밑에 다시 붙임 (새 열의 시작 위치는 유지)
    void ResizeAll() {
        ClockWindow* w[CITY_COUNT + 1];
        RECT old[CITY_COUNT + 1];
        int n = 0;
        w[n++] = this;
        for (ClockWindow* p = head_; p && n < CITY_COUNT + 1; p = p->next_) w[n++] = p;
        for (int i = 0; i < n; i++) GetWindowRect(w[i]->hwnd_, &old[i]);

        int prevBottom = 0;
        for (int i = 0; i < n; i++) {
            RECT wr, cr;
            GetWindowRect(w[i]->hwnd_, &wr);
            GetClientRect(w[i]->hwnd_, &cr);
            int h = (wr.bottom - wr.top) - cr.bottom + w[i]->ClientH();      // 비클라이언트(캡션/메뉴) 높이는 유지
            int top = wr.top;
            if (i > 0 && old[i].left == old[i - 1].left && old[i].top == old[i - 1].bottom)
                top = prevBottom;               // 바로 위 창 밑에 붙어 있던 창은 새 위치로
            SetWindowPos(w[i]->hwnd_, NULL, wr.left, top, wr.right - wr.left, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            prevBottom = top + h;
        }
        SavePos();
    }

    // 시계 보이기/숨기기 (본 창 + 도시 창 모두). 숨기면 트레이 아이콘만 남음
    void ApplyShow() { ShowWindow(hwnd_, cfg_.visible ? SW_SHOWNA : SW_HIDE); }

    void SetClockVisible(bool v) {
        ClockWindow* root = owner_ ? owner_ : this;
        Log::Put("시계 %s", v ? "보이기 (트레이 아이콘 제거)" : "숨기기 (트레이 아이콘만 표시)");
        root->cfg_.visible = v;
        root->cfg_.WriteInt("Option", "ShowClock", v);
        root->cfg_.SaveTrayStart(!v);           // 시계 숨김 = 다음 시작 때도 트레이로 시작 (tray_start=TRUE)
        root->ForAll(&ClockWindow::ApplyShow);
        if (v) root->TrayRemove(); else root->TrayAdd();
        root->UpdateOptionChecks();
    }

    // ---- 시스템 트레이 (본 창만, 숨김 상태에서만 표시)
    void TrayFill(NOTIFYICONDATAW& n) {
        n = NOTIFYICONDATAW();
        n.cbSize = sizeof(n);
        n.hWnd = hwnd_;
        n.uID = 1;
        SYSTEMTIME t;
        GetLocalTime(&t);
        char s[64];
        wsprintfA(s, "gsHomeManager  %02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
        Utf8::ToUtf16(s, -1, (char16_t*)n.szTip, 64);
    }

    void TrayAdd() {
        if (trayOn_) return;
        NOTIFYICONDATAW n;
        TrayFill(n);
        n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        n.uCallbackMessage = WM_TRAY;
        if (!trayIcon_)
            trayIcon_ = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);   // 트레이: 리소스 아이콘 (TrayRemove 에서 해제)
        n.hIcon = trayIcon_ ? trayIcon_ : LoadIcon(NULL, IDI_APPLICATION);    // 실패하면 공용 기본 아이콘 (해제 불필요)
        trayOn_ = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
        Log::Put("트레이 아이콘 추가 %s", trayOn_ ? "성공" : "실패");
        if (!trayOn_ && trayIcon_) { DestroyIcon(trayIcon_); trayIcon_ = NULL; }
    }

    void TrayUpdate() {
        NOTIFYICONDATAW n;
        TrayFill(n);
        n.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }

    void TrayRemove() {
        if (!trayOn_) return;
        NOTIFYICONDATAW n = NOTIFYICONDATAW();
        n.cbSize = sizeof(n);
        n.hWnd = hwnd_;
        n.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &n);
        trayOn_ = false;
        if (trayIcon_) { DestroyIcon(trayIcon_); trayIcon_ = NULL; }     // 아이콘 핸들 누수 방지
    }

    void OnTray(UINT ev) {
        if (ev == WM_LBUTTONUP) {               // 클릭: 시계 다시 보이기
            SetClockVisible(true);
        } else if (ev == WM_RBUTTONUP) {        // 우클릭: 보이기 / 종료 메뉴
            HMENU m = CreatePopupMenu();
            AddMenu(m, 0, CMD_SHOWCLOCK, "시계 보이기(&H)");
            AddMenu(m, 0, CMD_EXIT, "프로그램 종료 (&Exit)");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd_);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, NULL);
            PostMessage(hwnd_, WM_NULL, 0, 0);
            DestroyMenu(m);
        }
    }

    // 종료 확인: 30초 동안 Yes/No. Yes 일 때만 종료, No 또는 무응답이면 계속 동작
    void ConfirmExit() {
        static bool asking = false;
        if (asking) return;
        asking = true;
        ClockWindow* root = owner_ ? owner_ : this;
        root->modal_ = true;
        Log::Put("종료 확인창 표시 (30초 대기)");
        int r = ConfirmDialog::Show(hi_, 30, "홈 관리센터", "이 앱을 종료하면 [자동 침입탐지] 기능이 동작하지 않습니다.\n종료하겠습니까? (Yes/No)", "무응답 시 종료하지 않음", false);
        root->modal_ = false;
        asking = false;
        Log::Put("종료 확인 결과: %s", r == IDYES ? "Yes -> 종료" : "No 또는 시간 초과 -> 계속 동작");
        if (r == IDYES) RequestExit();
    }

    // 앱 종료: 도시 목록을 먼저 저장한 뒤 본 창을 닫음 (소유 창인 도시 창도 함께 닫힘)
    void RequestExit() {
        Log::Put("앱 종료 처리 시작 (도시 목록 저장)");
        ClockWindow* root = owner_ ? owner_ : this;
        root->quitting_ = true;
        root->SaveCities();
        DestroyWindow(root->hwnd_);
    }

    // 현재 도시 창 목록(만든 순서)을 ini 에 저장
    void SaveCities() {
        char csv[1024];
        int len = 0;
        csv[0] = 0;
        for (ClockWindow* p = head_; p; p = p->next_) {
            int n = lstrlenA(p->city_);
            if (len + n + 2 > (int)sizeof(csv)) break;
            if (len) csv[len++] = ',';
            lstrcpyA(csv + len, p->city_);
            len += n;
        }
        cfg_.SaveCities(csv);
    }

    // ini 에 저장된 도시 목록을 모두 다시 열기 (알 수 없는 이름은 무시)
    void RestoreCities() {
        char s[1024];
        lstrcpynA(s, cfg_.cities, sizeof(s));
        char* p = s;
        while (*p) {
            char* e = p;
            while (*e && *e != ',') e++;
            char more = *e;
            *e = 0;
            char* a = p;
            while (*a == ' ') a++;
            char* z = a + lstrlenA(a);
            while (z > a && z[-1] == ' ') *--z = 0;
            for (int i = 0; i < CITY_COUNT; i++) {
                if (lstrcmpA(a, CITIES[i].name) == 0) { AddCity(i, false); break; }
            }
            if (!more) break;
            p = e + 1;
        }
        SaveCities();                           // 정리된(중복/알 수 없는 이름 제거) 목록으로 다시 저장
    }

    // ---- Global 도시 창: 가장 아래쪽 창 바로 밑에 추가 (화면 아래를 넘으면 오른쪽 새 열)
    void AddCity(int idx, bool save) {
        if (HasCity(idx)) {                     // 이미 표시 중인 도시는 건너뜀 (중복 방지)
            Log::Put("Global 도시 추가 건너뜀(이미 표시 중): %s", CITIES[idx].name);
            return;
        }
        ClockWindow* e = new ClockWindow(cfg_, this, CITIES[idx].name, CITIES[idx].zone);

        RECT low;
        GetWindowRect(hwnd_, &low);
        for (ClockWindow* p = head_; p; p = p->next_) {
            RECT r;
            GetWindowRect(p->hwnd_, &r);
            if (r.bottom > low.bottom) low = r;
        }
        const int cmode = cfg_.clockMode;
        RECT c = {0, 0, cmode == 2 ? cfg_.analogSize : 336, cmode == 2 ? cfg_.analogSize : ClientH(false)};   // 새 도시 창: 음력 줄 없음
        AdjustWindowRect(&c, cmode == 0 ? NormalStyle() : (DWORD)WS_POPUP, FALSE);
        int w = c.right - c.left, ht = c.bottom - c.top;

        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        int x = low.left, y = low.bottom;
        if (y + ht > mi.rcWork.bottom) { x = low.right; y = mi.rcWork.top; }
        if (x + w > mi.rcWork.right)  x = mi.rcWork.right - w;
        if (x < mi.rcWork.left)       x = mi.rcWork.left;
        if (y < mi.rcWork.top)        y = mi.rcWork.top;

        if (!e->Create(hi_, SW_SHOWNOACTIVATE, x, y)) {
            delete e;
            return;
        }
        Log::Put("Global 도시 창 추가: %s", CITIES[idx].name);
        e->next_ = NULL;                        // 목록 끝에 추가 (만든 순서 유지)
        ClockWindow** pp = &head_;
        while (*pp) pp = &(*pp)->next_;
        *pp = e;
        UpdateCityChecks();
        if (save) SaveCities();
    }

    void Unlink(ClockWindow* e) {
        for (ClockWindow** pp = &head_; *pp; pp = &(*pp)->next_) {
            if (*pp == e) {
                *pp = e->next_;
                return;
            }
        }
    }

    // ---- 시계 모드: 0=[타이틀 & 디지털] 1=[디지털] 2=[아날로그]
    // 현재 클라이언트 영역의 왼쪽 위를 유지한 채 타이틀바 유무와 창 크기를 모드에 맞춤 (메뉴바가 없으므로 계산이 정확함)
    void ApplyClockMode() {
        const int mode = cfg_.clockMode;
        const bool bar = (mode == 0);
        POINT tl = {0, 0};
        ClientToScreen(hwnd_, &tl);
        hidden_ = !bar;
        const DWORD base = bar ? NormalStyle() : (DWORD)WS_POPUP;
        SetWindowLongPtrA(hwnd_, GWL_STYLE, base | (DWORD)(GetWindowLongPtrA(hwnd_, GWL_STYLE) & WS_VISIBLE));   // 보이는 상태는 그대로 (트레이 숨김 중이면 숨김 유지)
        RECT r = {0, 0, mode == 2 ? AnalogSide() : 336, ClientH()};
        AdjustWindowRect(&r, base, FALSE);
        SetWindowPos(hwnd_, NULL, tl.x + r.left, tl.y + r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ScheduleSecondTick();                   // 모드에 맞는 갱신 간격
        ApplyOpacity();                         // 아날로그 <-> 디지탈: 원 바깥 투명 처리(컬러키) 적용/해제
        if (!owner_) UpdateOptionChecks();      // 디자인 메뉴 활성/비활성
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    // 마우스 한 번 클릭: 모드를 다음으로 (모든 창에 적용, ini 에 저장). 아래로 쌓인 도시 창은 바로 밑에 다시 붙임
    void CycleClockMode() {
        ClockWindow* root = owner_ ? owner_ : this;
        if (root->cfg_.analogFull) { root->cfg_.analogFull = false; root->cfg_.SaveAnalogFull(); root->ApplyAnalogFull(); }   // 아날로그를 벗어나기 전에 전체화면 해제
        ClockWindow* w[CITY_COUNT + 1];
        RECT old[CITY_COUNT + 1];
        bool adj[CITY_COUNT + 1] = {};
        int n = 0;
        w[n++] = root;
        for (ClockWindow* p = root->head_; p && n < CITY_COUNT + 1; p = p->next_) w[n++] = p;
        for (int i = 0; i < n; i++) GetWindowRect(w[i]->hwnd_, &old[i]);
        for (int i = 1; i < n; i++) adj[i] = old[i].left == old[i - 1].left && old[i].top == old[i - 1].bottom;

        root->cfg_.clockMode = (root->cfg_.clockMode + 1) % 3;
        root->cfg_.SaveClockMode();
        static const char* const name[3] = {"타이틀 & 디지털", "디지털", "아날로그"};
        Log::Put("시계 모드: %s", name[root->cfg_.clockMode]);

        int prevBottom = 0, prevLeft = 0;
        for (int i = 0; i < n; i++) {
            w[i]->ApplyClockMode();
            RECT r;
            GetWindowRect(w[i]->hwnd_, &r);
            if (i > 0 && adj[i]) {
                SetWindowPos(w[i]->hwnd_, NULL, prevLeft, prevBottom, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                GetWindowRect(w[i]->hwnd_, &r);
            }
            prevBottom = r.bottom;
            prevLeft = r.left;
        }
        root->SavePos();
    }

    // ---- 아날로그 시계 그리기 (GDI): 테마 색의 테두리 + 문자판 + 눈금 + 요일/날짜 + 시침/분침/초침. 원 바깥은 ANALOG_KEY 색(= 투명)
    static int Rnd(double v) { return (int)(v < 0 ? v - 0.5 : v + 0.5); }

    static COLORREF Hsv(double h, double sat, double v) {                    // h: 0~360
        h = fmod(h, 360.0) / 60.0;
        const int k = (int)h;
        const double f = h - k, p = v * (1 - sat), q = v * (1 - sat * f), u = v * (1 - sat * (1 - f));
        double r = v, g = u, b = p;
        switch (k) {
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = u; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = u; g = p; b = v; break;
            case 5: r = v; g = p; b = q; break;
        }
        return RGB(Rnd(r * 255), Rnd(g * 255), Rnd(b * 255));
    }

    void PaintAnalog(HDC dc, const RECT& rc, const SYSTEMTIME& t) {
        const double PI = 3.14159265358979323846;
        AnalogTheme th = ATHEMES[cfg_.analogTheme];
        if (MatchDesk()) {                                                        // 원 안쪽 = 모니터 바탕색. 어두우면 눈금/글자/바늘을 밝게
            th.face = cfg_.deskColor;
            if (Lum(th.face) < 128) {
                th.tickMin = RGB(150, 150, 150);  th.tickHour = RGB(225, 225, 225);
                th.text = RGB(210, 210, 210);     th.hand = RGB(235, 235, 235);
            }
        }
        const bool full = !owner_ && cfg_.analogFull;                             // 전체화면: 원 테두리 생략, 눈금은 굵고 선명하게
        if (full) {
            if (Lum(th.face) < 128) { th.tickMin = RGB(235, 235, 235);  th.tickHour = RGB(255, 255, 255); }
            else                    { th.tickMin = RGB(60, 60, 60);     th.tickHour = RGB(0, 0, 0); }
        }
        HBRUSH kb = CreateSolidBrush(ANALOG_KEY);
        FillRect(dc, &rc, kb);                                                    // 원 바깥: 투명 처리용 색
        DeleteObject(kb);
        const int cx = rc.right / 2, cy = rc.bottom / 2;
        const int R = (rc.right < rc.bottom ? rc.right : rc.bottom) / 2 - 4;      // 바깥 반지름
        if (R < 20) return;
        if (cfg_.analogNeon) { PaintNeon(dc, cx, cy, R, t); return; }
        const int bz = R / 20 < 5 ? 5 : R / 20;                                   // 테두리 두께 (크기에 비례)

        HGDIOBJ np = GetStockObject(NULL_PEN);
        HGDIOBJ op = SelectObject(dc, np);
        if (full) {
        } else if (th.rainbow) {                                                  // 무지개 테두리: 3도씩 색을 바꾼 부채꼴 120개
            for (int d = 0; d < 360; d += 3) {
                const double a0 = d * PI / 180, a1 = (d + 4) * PI / 180, rr = R + 1;
                POINT pt[3] = {{cx, cy}, {cx + Rnd(rr * sin(a0)), cy - Rnd(rr * cos(a0))},
                               {cx + Rnd(rr * sin(a1)), cy - Rnd(rr * cos(a1))}};
                HBRUSH wb = CreateSolidBrush(Hsv(d, 0.85, 0.95));
                HGDIOBJ o = SelectObject(dc, wb);
                Polygon(dc, pt, 3);
                SelectObject(dc, o);
                DeleteObject(wb);
            }
        } else {
            HBRUSH bezel = CreateSolidBrush(th.bezel);
            HGDIOBJ ob = SelectObject(dc, bezel);
            Ellipse(dc, cx - R, cy - R, cx + R + 1, cy + R + 1);
            SelectObject(dc, ob);
            DeleteObject(bezel);
        }
        HBRUSH face = CreateSolidBrush(th.face);
        HGDIOBJ ob = SelectObject(dc, face);
        if (full) Ellipse(dc, cx - R, cy - R, cx + R + 1, cy + R + 1);
        else       Ellipse(dc, cx - R + bz, cy - R + bz, cx + R - bz + 1, cy + R - bz + 1);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(face);

        const double rOut = full ? R - 4 : R - (bz + 4);                                // 눈금 60개 (5개마다 굵고 길게)
        for (int i = 0; i < 60; i++) {
            const bool hr = (i % 5 == 0);
            COLORREF col = hr ? th.tickHour : th.tickMin;
            if (th.rainbow) col = Hsv(i * 6, full ? 0.9 : 0.85, full ? 1.0 : (hr ? 0.75 : 0.95));
            HPEN pn = CreatePen(PS_SOLID, hr ? (full ? (R / 40 < 7 ? 7 : R / 40) : (R / 50 < 2 ? 2 : R / 50)) : (full ? (R / 130 < 2 ? 2 : R / 130) : (R / 100 < 1 ? 1 : R / 100)), col);
            op = SelectObject(dc, pn);
            const double a = i * 6 * PI / 180, rIn = rOut - (hr ? R * (full ? 0.16 : 0.10) : R * (full ? 0.055 : 0.045));
            MoveToEx(dc, cx + Rnd(rOut * sin(a)), cy - Rnd(rOut * cos(a)), NULL);
            LineTo(dc, cx + Rnd(rIn * sin(a)), cy - Rnd(rIn * cos(a)));
            SelectObject(dc, op);
            DeleteObject(pn);
        }

        static const char* const WD[7] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
        char ds[64];
        FormatDate(t, 1, ds);                                                     // yyyy-MM-dd
        char top[96];                                                             // 도시 창은 요일 대신 도시 이름 + (AM)/(PM)
        wsprintfA(top, "%s (%s)", owner_ ? city_ : WD[t.wDayOfWeek % 7], t.wHour < 12 ? "AM" : "PM");
        HFONT f = MakeFont(cfg_.dateFont, R * 14 / 100 < 10 ? 10 : R * 14 / 100);
        HGDIOBJ of = SelectObject(dc, f);
        SetTextColor(dc, th.text);
        const char* txt[2] = {top, ds};
        const int ty[2] = {cy - R * 34 / 100, cy + R * 34 / 100};
        for (int k = 0; k < (!owner_ && cfg_.analogFull ? 0 : 2); k++) {   // 전체화면: 요일/날짜 생략
            char16_t u[64];
            Utf8::ToUtf16(txt[k], -1, u, 64);
            const int fh = (R * 14 / 100 < 10 ? 10 : R * 14 / 100) / 2 + 6;     // 글자 높이에 맞춘 영역
            RECT tr = {cx - R + 10, ty[k] - fh, cx + R - 10, ty[k] + fh};
            DrawTextW(dc, (LPCWSTR)u, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, of);
        DeleteObject(f);

        // 바늘: 12시 방향이 0도, 시계 방향. (along: 중심에서 바늘 방향 거리, side: 좌우)
        auto hand = [&](double deg, double len, double hw, double tail, COLORREF col) {
            const double a = deg * PI / 180, dx = sin(a), dy = -cos(a), px = -dy, py = dx;
            POINT p[4];
            const double al[4] = {-tail, len * 0.18, len, len * 0.18}, sd[4] = {0, -hw, 0, hw};
            for (int k = 0; k < 4; k++) {
                p[k].x = cx + Rnd(dx * al[k] + px * sd[k]);
                p[k].y = cy + Rnd(dy * al[k] + py * sd[k]);
            }
            HBRUSH hb = CreateSolidBrush(col);
            HPEN hp = CreatePen(PS_SOLID, 1, col);
            HGDIOBJ b0 = SelectObject(dc, hb), p0 = SelectObject(dc, hp);
            Polygon(dc, p, 4);
            SelectObject(dc, b0);
            SelectObject(dc, p0);
            DeleteObject(hb);
            DeleteObject(hp);
        };
        const double hh = ((t.wHour % 12) + t.wMinute / 60.0 + t.wSecond / 3600.0) * 30;
        const double mm = (t.wMinute + t.wSecond / 60.0) * 6;
        const double ss = t.wSecond * 6;
        hand(hh, R * 0.52, R * 0.050 < 2 ? 2 : R * 0.050, R * 0.10, th.hand);
        hand(mm, R * 0.80, R * 0.036 < 1.5 ? 1.5 : R * 0.036, R * 0.10, th.hand);

        const double sa = ss * PI / 180;                                          // 초침 (뒤쪽 꼬리 포함) + 가운데 점
        const COLORREF capC = cfg_.analogNoSec ? th.hand : th.sec;                // 초침 생략: 가운데 점만 바늘 색으로
        HPEN ps = CreatePen(PS_SOLID, R / 48 < 1 ? 1 : R / 48, capC);
        HBRUSH rb = CreateSolidBrush(capC);
        op = SelectObject(dc, ps);
        ob = SelectObject(dc, rb);
        if (!cfg_.analogNoSec) {
            MoveToEx(dc, cx - Rnd(R * 0.20 * sin(sa)), cy + Rnd(R * 0.20 * cos(sa)), NULL);
            LineTo(dc, cx + Rnd(R * 0.86 * sin(sa)), cy - Rnd(R * 0.86 * cos(sa)));
        }
        const int cap = R * 5 / 100 < 3 ? 3 : R * 5 / 100;
        Ellipse(dc, cx - cap, cy - cap, cx + cap + 1, cy + cap + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(ps);
        DeleteObject(rb);
    }

    // ---- 네온사인 효과: 어두운 문자판 위에 테마 색 선을 (넓고 어두운 번짐 -> 중간 번짐 -> 밝은 심) 3겹으로 그림
    static COLORREF Mix(COLORREF a, COLORREF b, double k) {                    // a*(1-k) + b*k
        return RGB(Rnd(GetRValue(a) * (1 - k) + GetRValue(b) * k), Rnd(GetGValue(a) * (1 - k) + GetGValue(b) * k),
                   Rnd(GetBValue(a) * (1 - k) + GetBValue(b) * k));
    }
    static COLORREF NeonOf(COLORREF c) {                                       // 가장 밝은 채널을 255 로. 무채색이면 시안
        const int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        if (mx - mn < 40) return RGB(0, 230, 255);
        const double k = 255.0 / mx;
        return RGB(Rnd(r * k), Rnd(g * k), Rnd(b * k));
    }

    void PaintNeon(HDC dc, int cx, int cy, int R, const SYSTEMTIME& t) {
        const double PI = 3.14159265358979323846;
        const AnalogTheme& th = ATHEMES[cfg_.analogTheme];
        const int u = (!owner_ && cfg_.analogFull) ? 1 : (R / 96 < 1 ? 1 : R / 96);   // 글로우/선 굵기 배율 (크기에 비례)
        const COLORREF bg = (MatchDesk() && Lum(cfg_.deskColor) < 110) ? cfg_.deskColor : RGB(8, 8, 14), mainC = th.rainbow ? RGB(235, 235, 255) : NeonOf(th.bezel), secC = NeonOf(th.sec);

        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        HBRUSH face = CreateSolidBrush(bg);
        HGDIOBJ ob = SelectObject(dc, face);
        Ellipse(dc, cx - R, cy - R, cx + R + 1, cy + R + 1);                  // 어두운 문자판 (원 바깥은 투명 유지)
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(face);

        auto mkpen = [&](int w, COLORREF c) {
            LOGBRUSH lb = {BS_SOLID, c, 0};
            return ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, w, &lb, 0, NULL);
        };
        auto passCol = [&](int pass, COLORREF c) {                              // 0,1: 어두운 번짐 / 2: 밝은 심
            return pass == 0 ? Mix(bg, c, 0.15) : pass == 1 ? Mix(bg, c, 0.40) : Mix(c, RGB(255, 255, 255), 0.45);
        };
        auto line = [&](double x1, double y1, double x2, double y2, int w, COLORREF c) {
            for (int pass = 0; pass < 3; pass++) {
                HPEN pn = mkpen(pass == 0 ? w + 6 * u : pass == 1 ? w + 3 * u : w, passCol(pass, c));
                HGDIOBJ o = SelectObject(dc, pn);
                MoveToEx(dc, Rnd(x1), Rnd(y1), NULL);
                LineTo(dc, Rnd(x2) + (Rnd(x1) == Rnd(x2) && Rnd(y1) == Rnd(y2) ? 1 : 0), Rnd(y2));
                SelectObject(dc, o);
                DeleteObject(pn);
            }
        };

        const int rr = R - 7 * u;                                                  // 테두리 링 (번짐이 원 밖으로 나가지 않게 안쪽에)
        const bool noRing = !owner_ && cfg_.analogFull;                        // 전체화면: 링 생략, 눈금은 굵고 선명하게
        if (noRing) {
        } else if (th.rainbow) {
            for (int d = 0; d < 360; d += 3) {
                const double a0 = d * PI / 180, a1 = (d + 3.5) * PI / 180;
                line(cx + rr * sin(a0), cy - rr * cos(a0), cx + rr * sin(a1), cy - rr * cos(a1), 3 * u, Hsv(d, 0.9, 1.0));
            }
        } else {
            HGDIOBJ nb = SelectObject(dc, GetStockObject(NULL_BRUSH));
            for (int pass = 0; pass < 3; pass++) {
                HPEN pn = mkpen(pass == 0 ? 9 * u : pass == 1 ? 6 * u : 3 * u, passCol(pass, mainC));
                HGDIOBJ o = SelectObject(dc, pn);
                Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
                SelectObject(dc, o);
                DeleteObject(pn);
            }
            SelectObject(dc, nb);
        }

        const double rOut = noRing ? R - 9 : R - 18 * u;                           // 눈금: 시 눈금 12개는 네온, 분 눈금은 은은하게
        for (int i = 0; i < 60; i++) {
            const bool hr = (i % 5 == 0);
            const double a = i * 6 * PI / 180, rIn = rOut - (hr ? R * 0.10 : R * 0.04);
            const double x1 = cx + rOut * sin(a), y1 = cy - rOut * cos(a), x2 = cx + rIn * sin(a), y2 = cy - rIn * cos(a);
            const COLORREF c = th.rainbow ? Hsv(i * 6, 0.9, 1.0) : mainC;
            if (hr) line(x1, y1, x2, y2, noRing ? (R / 35 < 8 ? 8 : R / 35) : 3 * u, c);
            else {
                HPEN pn = mkpen(noRing ? (R / 130 < 2 ? 2 : R / 130) : u, Mix(bg, c, noRing ? 1.0 : 0.6));
                HGDIOBJ o = SelectObject(dc, pn);
                MoveToEx(dc, Rnd(x1), Rnd(y1), NULL);
                LineTo(dc, Rnd(x2), Rnd(y2));
                SelectObject(dc, o);
                DeleteObject(pn);
            }
        }

        static const char* const WD[7] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
        char ds[64];
        FormatDate(t, 1, ds);
        char top[96];                                                          // 요일(도시 이름) + (AM)/(PM)
        wsprintfA(top, "%s (%s)", owner_ ? city_ : WD[t.wDayOfWeek % 7], t.wHour < 12 ? "AM" : "PM");
        const char* txt[2] = {top, ds};
        HFONT f = MakeFont(cfg_.dateFont, R * 14 / 100 < 10 ? 10 : R * 14 / 100);
        HGDIOBJ of = SelectObject(dc, f);
        SetTextColor(dc, Mix(mainC, RGB(255, 255, 255), 0.4));
        const int ty[2] = {cy - R * 34 / 100, cy + R * 34 / 100};
        for (int k = 0; k < (!owner_ && cfg_.analogFull ? 0 : 2); k++) {   // 전체화면: 요일/날짜 생략
            char16_t u[64];
            Utf8::ToUtf16(txt[k], -1, u, 64);
            const int fh = (R * 14 / 100 < 10 ? 10 : R * 14 / 100) / 2 + 6;     // 글자 높이에 맞춘 영역
            RECT tr = {cx - R + 10, ty[k] - fh, cx + R - 10, ty[k] + fh};
            DrawTextW(dc, (LPCWSTR)u, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, of);
        DeleteObject(f);

        auto hand = [&](double deg, double len, double tail, int w, COLORREF c) {   // 12시 방향이 0도, 시계 방향
            const double a = deg * PI / 180, dx = sin(a), dy = -cos(a);
            line(cx - dx * tail, cy - dy * tail, cx + dx * len, cy + dy * len, w, c);
        };
        hand(((t.wHour % 12) + t.wMinute / 60.0 + t.wSecond / 3600.0) * 30, R * 0.50, 0, 5 * u, mainC);
        hand((t.wMinute + t.wSecond / 60.0) * 6, R * 0.75, 0, 4 * u, mainC);
        if (!cfg_.analogNoSec) hand(t.wSecond * 6, R * 0.80, R * 0.18, 2 * u, secC);
        HBRUSH cb = CreateSolidBrush(passCol(2, cfg_.analogNoSec ? mainC : secC));
        op = SelectObject(dc, GetStockObject(NULL_PEN));
        ob = SelectObject(dc, cb);
        const int cap = R * 5 / 100 < 3 ? 3 : R * 5 / 100;
        Ellipse(dc, cx - cap, cy - cap, cx + cap + 1, cy + cap + 1);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(cb);
    }

    // ---- 더블클릭 팝업 메뉴 (본 창만). 메뉴 밖을 클릭하면 Windows 가 알아서 닫음
    void ShowPopupMenu(HMENU m = NULL) {            // m: 열 메뉴 (없으면 전체 메뉴. F1). F2 ~ F9 는 해당 최상위 메뉴만 연다
        if (!m) m = menu_;
        if (owner_ || !m) return;
        POINT pt;
        GetCursorPos(&pt);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        if (m != menu_ && !PtInRect(&wr, pt)) { pt.x = wr.left; pt.y = wr.bottom; }   // 마우스가 창 밖이면 창 아래쪽에 표시
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, NULL);
        PostMessageA(hwnd_, WM_NULL, 0, 0);
    }

    // ---- 설정 초기화: 바탕색/투명도/폰트/타이머/옵션을 기본값으로 (창 위치, 기준 도시는 유지)
    // Global 도시 창을 본 창 바로 밑에 가지런히 정렬 (만든 순서, 화면 아래를 넘으면 오른쪽 새 열)
    void ArrangeCities() {
        RECT mr;
        GetWindowRect(hwnd_, &mr);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& wa = mi.rcWork;
        int x = mr.left, y = mr.bottom, n = 0;
        for (ClockWindow* p = head_; p; p = p->next_) {
            RECT r;
            GetWindowRect(p->hwnd_, &r);
            int w = r.right - r.left, h = r.bottom - r.top;
            if (y + h > wa.bottom) {            // 화면 아래를 넘으면 오른쪽 새 열
                x += w;
                y = wa.top;
            }
            int cx = x;
            if (cx + w > wa.right) cx = wa.right - w;
            if (cx < wa.left) cx = wa.left;
            SetWindowPos(p->hwnd_, NULL, cx, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            y += h;
            n++;
        }
        Log::Put("Global 도시 창 %d개를 본 창 밑으로 정렬 (타이틀바 더블클릭)", n);
    }

    // ---- WiFiScan 메뉴: 대화상자 (모달. 열려 있는 동안 창 자동 이동만 멈추고 scan 은 계속 동작)
    // ---- IP cam 메뉴: 등록(대화상자 -> ini) / 카메라 영상(별도 창에서 WebCam1 / WebCam2 / IP Cam 선택 후 [실행], 앱은 계속 실행)
    bool IpCamRegister() {
        char h[64];
        lstrcpynA(h, cfg_.ipHost, sizeof(h));
        int port = cfg_.ipPort;
        modal_ = true;
        bool ok = IpCamDialog::Show(hi_, hwnd_, h, sizeof(h), port);
        modal_ = false;
        if (!ok) return false;
        lstrcpynA(cfg_.ipHost, h, sizeof(cfg_.ipHost));
        cfg_.ipPort = port;
        cfg_.SaveIpCam();
        Log::Put("IP cam 등록: %s:%d", cfg_.ipHost, cfg_.ipPort);
        for (ipcam::Session* c : cams_) c->SetIp(cfg_.ipHost, std::to_string(cfg_.ipPort));      // 열려 있는 카메라 영상 창들에도 반영 (다음 [실행] 부터)
        return true;
    }

    // ---- 폰 탐색기: 대화상자 -> (host/port/user 만 ini 에 저장) -> explorer.exe ftp://사용자명:비밀번호@IP:port 실행
    static void PctEncode(const char* s, std::string& out) {        // URL 에 넣을 수 있게 특수문자(@ : / % 한글 등)를 %XX 로
        for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
            if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '-' || *p == '.' || *p == '_' || *p == '~') out += (char)*p;
            else { char t[4]; wsprintfA(t, "%%%02X", (unsigned)*p); out += t; }
        }
    }

    void PhoneExplorer() {
        if (owner_) return;
        char host[32], user[64], pass[64];
        lstrcpynA(host, cfg_.phHost, sizeof(host));
        lstrcpynA(user, cfg_.phUser, sizeof(user));
        pass[0] = 0;
        int port = cfg_.phPort;
        modal_ = true;
        bool ok = PhoneDialog::Show(hi_, hwnd_, host, port, user, sizeof(user), pass, sizeof(pass));
        modal_ = false;
        if (!ok) { SecureZeroMemory(pass, sizeof(pass)); return; }
        lstrcpynA(cfg_.phHost, host, sizeof(cfg_.phHost));
        lstrcpynA(cfg_.phUser, user, sizeof(cfg_.phUser));
        cfg_.phPort = port;
        cfg_.SavePhoneFtp();

        std::string url = "ftp://";
        if (user[0]) {
            PctEncode(user, url);
            if (pass[0]) { url += ':'; PctEncode(pass, url); }
            url += '@';
        }
        url += host;
        url += ':';
        url += std::to_string(port);
        SecureZeroMemory(pass, sizeof(pass));

        char exe[MAX_PATH + 16];
        GetWindowsDirectoryA(exe, MAX_PATH);
        lstrcatA(exe, "\\explorer.exe");                           // C:\Windows\explorer.exe
        HINSTANCE r = ShellExecuteA(NULL, "open", exe, url.c_str(), NULL, SW_SHOWNORMAL);
        Log::Put("폰 탐색기 실행: ftp://%s%s%s:%d (결과코드 %d)", user, user[0] ? "@" : "", host, port, (int)(INT_PTR)r);   // 비밀번호는 기록하지 않음
    }

    void IpCamView() {                           // 누를 때마다 새 영상 창 (웹캠 / IP cam 을 각각 다른 창에서 동시에 실행 가능)
        if (owner_) return;
        Log::Put("카메라 영상 열기 (IP cam 등록값: %s:%d, 열린 창 %d개)", cfg_.ipHost, cfg_.ipPort, (int)cams_.size());      // IP cam 이 미등록이어도 창은 열림 (웹캠만 쓸 수 있음)
        ipcam::Session* s = new ipcam::Session(hi_, hwnd_, cfg_.ipHost, std::to_string(cfg_.ipPort));
        if (!s->Open()) { delete s; return; }
        cams_.push_back(s);
    }

    void IpCamClosed() {                         // 영상 창이 닫힘: 창이 없어진 세션의 수신 스레드 정지 + 정리 (마지막 창이 닫히면 ipcam.log 도 닫힘)
        for (size_t i = cams_.size(); i-- > 0; ) {
            HWND w = cams_[i]->Hwnd();
            if (w && IsWindow(w)) continue;
            Log::Put("카메라 영상 닫힘");
            delete cams_[i];
            cams_.erase(cams_.begin() + i);
        }
    }

    // ---- 다른 PC(서버 / 클라이언트): 대화상자 (서버는 전용 스레드라 대화상자를 닫아도 계속 동작, 클라이언트는 [연결] 후 자기 창에서 동작)
    void RdpServerCommand() {
        if (owner_) return;
        modal_ = true;
        RdpServerDialog::Show(hi_, hwnd_, cfg_);
        modal_ = false;
    }

    void RdpClientCommand() {
        if (owner_) return;
        modal_ = true;
        RdpClientDialog::Show(hi_, hwnd_, cfg_);
        modal_ = false;
    }

    void ScanCommand(bool myPhone = false) {
        if (!scan_) return;
        modal_ = true;
        ScanDialog::Show(hi_, hwnd_, cfg_, *scan_, myPhone ? "본인 스마트폰 항목에 체크하고, 저장 버튼을 누르세요" : NULL);
        modal_ = false;
    }

    // ---- Help 메뉴
    static const char* HomeUrl()    { return "https://github.com/GarpsuKim/gsTop/tree/main/gsHomeManager"; }
    static const char* ReadmeUrl()   { return "https://github.com/GarpsuKim/gsTop/blob/main/gsHomeManager/README.md"; }
    static const char* DownloadUrl() { return "https://downgit.github.io/#/home?url=https://github.com/GarpsuKim/gsTop/tree/main/gsHomeManager"; }

    // 시스템 기본 브라우저로 주소 열기
    void OpenUrl(const char* url) {
        char16_t u[300];
        Utf8::ToUtf16(url, -1, u, 300);
        HINSTANCE r = ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)u, NULL, NULL, SW_SHOWNORMAL);
        Log::Put("브라우저로 열기: %s (결과코드 %d)", url, (int)(INT_PTR)r);
    }

    void ShowInfo(const char* title, const char* text, const char* link = NULL) {
        modal_ = true;
        InfoDialog::Show(hi_, title, text, link, 30);          // 30초 프로그레스 바 (끝나면 자동으로 닫힘)
        modal_ = false;
    }

    void LunarKeyCommand() {
        char key[200];
        lstrcpynA(key, cfg_.lunarKey, sizeof(key));
        modal_ = true;
        bool ok = LunarKeyDialog::Show(hi_, hwnd_, key, sizeof(key));
        modal_ = false;
        if (!ok) { Log::Put("음력 API 키값 등록 취소"); return; }
        cfg_.SetLunarKey(key);
        Lunar::Init(cfg_.lunarUrl, cfg_.lunarKey, hwnd_);
        Lunar::Reset();
        Log::Put("음력 API 키값 등록: %s", key[0] ? "저장함" : "비움 (음력 사용 불가)");
        ResizeAll();                            // 키를 비우면 음력 줄이 사라지므로 높이 조정
        ForAll(&ClockWindow::ApplyFormat);
        UpdateOptionChecks();                   // '음력 보이기' 활성/비활성 갱신
    }

    void HelpCommand(int act) {
        if (act == 0) {                         // About
            char m[600];
            wsprintfA(m, "프로그램 이름 : %s\r\n버전 : %s\r\n개발자 : 김갑수 ( koreastaaa@gmail.com )\r\n\r\n%s", APP_NAME, APP_VERSION_STR, APP_COPYRIGHT);
            ShowInfo("About", m);
        } else if (act == 1) {                  // ReadMe.MD -> 기본 브라우저
            OpenUrl(ReadmeUrl());
        } else if (act == 2) {                  // 홈페이지 주소 표시
            ShowInfo("홈페이지", "홈페이지 :", HomeUrl());     // 링크를 누르면 기본 브라우저로 열기
        } else if (act == 4) {                  // 음력 API 키값 등록
            LunarKeyCommand();
        } else {                                // 최신 버전 다운로드 (GitHub 폴더를 zip 으로 내려받는 링크)
            OpenUrl(DownloadUrl());
        }
    }

    int CountCities() const {
        int n = 0;
        for (ClockWindow* p = head_; p; p = p->next_) n++;
        return n;
    }

    void ResetSettings() {
        Log::Put("설정 초기화 확인창 표시");
        char16_t msg[96], cap[32];
        Utf8::ToUtf16("모든 설정을 기본값으로 되돌릴까요?\n(창 위치는 유지됩니다)", -1, msg, 96);
        Utf8::ToUtf16("설정 초기화", -1, cap, 32);
        modal_ = true;
        int ans = MessageBoxW(hwnd_, (LPCWSTR)msg, (LPCWSTR)cap,
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        modal_ = false;
        Log::Put("설정 초기화 확인 결과: %s", ans == IDYES ? "Yes -> 기본값 복원" : "No -> 취소");
        if (ans != IDYES) return;

        if (cfg_.allDesk) DesktopPin::Set(false);
        cfg_.ResetDefaults();
        cfg_.SaveAll();
        ForAll(&ClockWindow::RestartTimers);
        ForAll(&ClockWindow::ApplyStyle);
        CheckOpacity();
        CheckMoveInterval();
        CheckFormats();
        UpdateOptionChecks();
    }

    // 모든 가상 데스크톱에 표시 On/Off (실패하면 상태를 바꾸지 않고 알림)
    // ---- 자동 실행: HKCU\Software\Microsoft\Windows\CurrentVersion\Run 에 현재 exe 경로를 등록 (관리자 권한 불필요)
    static constexpr const wchar_t* RUN_KEY  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t* RUN_NAME = L"gsHomeManager";

    static bool IsAutoRun() {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
        LONG r = RegQueryValueExW(k, RUN_NAME, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
        return r == ERROR_SUCCESS;
    }

    static bool SetAutoRun(bool on) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return false;
        LONG r;
        if (on) {
            wchar_t exe[MAX_PATH], val[MAX_PATH + 4];
            GetModuleFileNameW(NULL, exe, MAX_PATH);
            {   // 같은 폴더에 gsHomeManagerLauncher.exe 가 있으면 그것을 등록 (부팅 때도 서버 버전 확인/갱신)
                wchar_t* sl = wcsrchr(exe, L'\\');
                if (sl) {
                    wchar_t ln[MAX_PATH + 16];
                    lstrcpynW(ln, exe, (int)(sl - exe) + 2);
                    lstrcatW(ln, L"gsHomeManagerLauncher.exe");
                    if (GetFileAttributesW(ln) != INVALID_FILE_ATTRIBUTES) lstrcpyW(exe, ln);
                }
            }
            wsprintfW(val, L"\"%s\"", exe);                       // 경로에 공백이 있어도 되도록 따옴표로 감쌈
            r = RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (const BYTE*)val, (lstrlenW(val) + 1) * sizeof(wchar_t));
        } else {
            r = RegDeleteValueW(k, RUN_NAME);
            if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
        }
        RegCloseKey(k);
        return r == ERROR_SUCCESS;
    }

    // ---- 동반 실행 앱 등록: 대화상자(최대 5개, 현재 값 미리 표시) -> ini [Option] companion_app1 ~ companion_app5
    void RegisterCompanion() {
        char tmp[Settings::COMPANION_MAX][784];
        memcpy(tmp, cfg_.companionApp, sizeof(tmp));
        modal_ = true;
        bool inv = cfg_.companionInvoker;
        bool ok = CompanionDialog::Show(hi_, hwnd_, tmp, inv);
        modal_ = false;
        if (!ok) return;                                            // 취소하면 그대로 유지
        memcpy(cfg_.companionApp, tmp, sizeof(tmp));
        cfg_.companionInvoker = inv;
        cfg_.SaveCompanionApps();
        for (int k = 0; k < Settings::COMPANION_MAX; k++)
            if (cfg_.companionApp[k][0]) Log::Put("동반 실행 앱 등록 %d: %s (권한 요청 무시 %s)", k + 1, cfg_.companionApp[k], inv ? "ON" : "OFF");
        UpdateOptionChecks();
    }

    void ToggleAutoRun() {                                          // on/off 모두 먼저 Yes/No 확인
        const bool on = IsAutoRun();
        char16_t msg[64], cap[32];
        Utf8::ToUtf16(on ? "자동 실행 해제합니까?" : "부팅 (또는 로그온)하면서 자동 실행 합니까? (Yes/No)", -1, msg, 64);
        Utf8::ToUtf16("자동 실행", -1, cap, 32);
        modal_ = true;
        int a = MessageBoxW(hwnd_, (LPCWSTR)msg, (LPCWSTR)cap, MB_YESNO | MB_ICONQUESTION);
        modal_ = false;
        if (a == IDYES) {
            bool ok = SetAutoRun(!on);
            Log::Put("자동 실행 %s: %s", on ? "해제" : "등록", ok ? "성공" : "실패");
            if (!ok) {
                Utf8::ToUtf16(on ? "자동 실행 해제에 실패했습니다." : "자동 실행 등록에 실패했습니다.", -1, msg, 64);
                MessageBoxW(hwnd_, (LPCWSTR)msg, (LPCWSTR)cap, MB_OK | MB_ICONWARNING);
            }
        } else {
            Log::Put("자동 실행 %s 취소", on ? "해제" : "등록");
        }
        UpdateOptionChecks();
    }

    void ToggleAllDesktops() {
        bool want = !cfg_.allDesk;
        bool okPin = DesktopPin::Set(want);
        Log::Put("모든 가상 데스크톱 적용 %s 요청: %s", want ? "켜기" : "끄기", okPin ? "성공" : "실패");
        if (!okPin) {
            char16_t msg[96], cap[32];
            Utf8::ToUtf16("이 Windows 버전에서는 가상 데스크톱 적용을 사용할 수 없습니다.", -1, msg, 96);
            Utf8::ToUtf16("모든 가상 데스크톱", -1, cap, 32);
            modal_ = true;
            MessageBoxW(hwnd_, (LPCWSTR)msg, (LPCWSTR)cap, MB_OK | MB_ICONWARNING);
            modal_ = false;
            return;
        }
        cfg_.allDesk = want;
        UpdateOptionChecks();
        cfg_.WriteInt("Option", "AllDesktops", want);
    }

    // 바탕화면이 단색일 때만 '시계 바탕색과 모니터 바탕색을 동일하게' 사용 가능 (아니면 비활성)
    void UpdateBackMenu() {
        if (!menuBack_) return;
        COLORREF c;
        bool solid = DesktopColor::GetSolid(c);
        EnableMenuItem(menuBack_, CMD_BACKDESK, MF_BYCOMMAND | (solid ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menuBack_, CMD_BACKDESK, MF_BYCOMMAND | (cfg_.backFollow && solid ? MF_CHECKED : MF_UNCHECKED));
    }

    // 바탕색을 현재 바탕화면 단색으로 (단색이 아니면 아무것도 하지 않음). 성공하면 true
    bool ApplyDesktopColor() {
        COLORREF c;
        if (!DesktopColor::GetSolid(c)) return false;
        if (c != cfg_.bgColor) {
            Log::Put("시계 바탕색을 모니터 바탕색으로 변경: %02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
            cfg_.bgColor = c;
            ForAll(&ClockWindow::ApplyStyle);
            cfg_.SaveStyle();
        }
        return true;
    }

    // 모니터 바탕색(단색일 때)을 읽어 둠 (그릴 때마다 COM 을 쓰지 않도록 cfg_ 에 보관). redraw: 바뀌었으면 다시 그림
    void RefreshDeskColor(bool redraw) {
        COLORREF c = 0;
        const bool solid = DesktopColor::GetSolid(c);
        const bool chg = (solid != cfg_.deskSolid) || (solid && c != cfg_.deskColor);
        cfg_.deskSolid = solid;
        if (solid) cfg_.deskColor = c;
        if (redraw) {
            UpdateOptionChecks();
            if (chg && cfg_.analogMatch) ForAll(&ClockWindow::ApplyFormat);
        }
    }

    bool MatchDesk() const { return cfg_.analogMatch && cfg_.deskSolid; }
    static int Lum(COLORREF c) { return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000; }

    void ToggleBackFollow() {
        if (!cfg_.backFollow) {
            if (!ApplyDesktopColor()) { UpdateBackMenu(); return; }   // 단색이 아니면 켜지 않음
            cfg_.backFollow = true;
        } else {
            cfg_.backFollow = false;
        }
        cfg_.SaveStyle();
        UpdateBackMenu();
    }

    void PickBackColor() {
        static COLORREF custom[16];
        CHOOSECOLORW cc = {};
        cc.lStructSize  = sizeof(cc);
        cc.hwndOwner    = hwnd_;
        cc.rgbResult    = cfg_.bgColor;
        cc.lpCustColors = custom;
        cc.Flags        = CC_RGBINIT | CC_FULLOPEN;
        modal_ = true;
        BOOL ok = ChooseColorW(&cc);
        modal_ = false;
        if (!ok) return;
        cfg_.bgColor = cc.rgbResult;
        Log::Put("바탕색 직접 선택: %02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
        cfg_.backFollow = false;                // 직접 고른 색을 쓰므로 '현재 바탕색 적용'은 해제
        ForAll(&ClockWindow::ApplyStyle);
        cfg_.SaveStyle();
        UpdateBackMenu();
    }

    void SetOpacity(int pct) {
        cfg_.opacity = pct;
        ForAll(&ClockWindow::ApplyOpacity);
        CheckOpacity();
        cfg_.SaveStyle();
    }

    // 글꼴 대화상자: 이름/크기/색깔을 한 번에 선택 (굵게/기울임 등 스타일은 무시)
    void PickFont(char* name, int cap, int& size, COLORREF& color) {
        LOGFONTW lf = {};
        FaceToWide(name, lf.lfFaceName);
        lf.lfHeight  = -size;
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;

        CHOOSEFONTW cf = {};
        cf.lStructSize = sizeof(cf);
        cf.hwndOwner   = hwnd_;
        cf.lpLogFont   = &lf;
        cf.rgbColors   = color;
        cf.Flags       = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
        modal_ = true;
        BOOL ok = ChooseFontW(&cf);
        modal_ = false;
        if (!ok) return;

        WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, name, cap, NULL, NULL);
        int px = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
        size  = px < 6 ? 6 : (px > 400 ? 400 : px);
        color = cf.rgbColors;
        Log::Put("글꼴 변경: %s %dpx 색=%02X%02X%02X", name, size, GetRValue(color), GetGValue(color), GetBValue(color));
        ForAll(&ClockWindow::ApplyStyle);       // 폰트 재생성 + 스크롤 범위 재계산
        cfg_.SaveStyle();
    }

    // ---- 도우미
    int ClientWidth() const {
        RECT c;
        GetClientRect(hwnd_, &c);
        return c.right;
    }

    // 다음 초 경계에 맞춰 타이머 재설정
    void ScheduleSecondTick() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        UINT ms = 1000 - t.wMilliseconds;
        if (owner_ && MinuteOnly()) ms += (59 - t.wSecond) * 1000;      // 도시 창: 알람과 무관하므로 다음 분 경계에만 깨움
        SetTimer(hwnd_, TID_SECOND, ms, NULL);
    }

    bool MinuteOnly() const { return cfg_.clockMode == 2 && cfg_.analogNoSec; }   // 아날로그 + 초침 생략: 분 단위 갱신

    // 스크롤 범위 계산용 텍스트 폭 측정
    void MeasureText() {
        HDC dc = GetDC(hwnd_);
        SIZE sz;
        HGDIOBJ old = SelectObject(dc, font_);
        static const SYSTEMTIME sample = {2026, 9, 1, 28, 20, 8, 8, 0};     // 폭 측정용 예시 (2026-09-28 20:08:08)
        char s[96];
        char16_t u[96];
        FormatTime(sample, cfg_.timeFmt, s);
        int n = Utf8::ToUtf16(s, -1, u, 96) - 1;
        GetTextExtentPoint32W(dc, (LPCWSTR)u, n, &sz);
        timeScroll_.SetTextWidth(sz.cx);

        SelectObject(dc, dateFont_);
        char ds[64];
        FormatDate(sample, cfg_.dateFmt, ds);
        wsprintfA(s, "%s %s", city_, ds);
        n = Utf8::ToUtf16(s, -1, u, 96) - 1;
        GetTextExtentPoint32W(dc, (LPCWSTR)u, n, &sz);
        dateScroll_.SetTextWidth(sz.cx);

        if (ShowLunarHere()) {                  // 음력 줄 폭 (현재 날짜의 조회 결과 기준)
            SYSTEMTIME now;
            zone_.Now(now);
            Lunar::Text(now.wYear * 10000 + now.wMonth * 100 + now.wDay, s, sizeof(s));
            n = Utf8::ToUtf16(s, -1, u, 96) - 1;
            GetTextExtentPoint32W(dc, (LPCWSTR)u, n, &sz);
            lunarScroll_.SetTextWidth(sz.cx);
        }

        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
    }

    // 모든 모니터의 작업영역 수집 (랜덤 이동 대상)
    struct MonList { RECT r[16]; int n; };
    static BOOL CALLBACK MonProc(HMONITOR m, HDC, LPRECT, LPARAM l) {
        MonList* L = (MonList*)l;
        if (L->n < 16) {
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoA(m, &mi);
            L->r[L->n++] = mi.rcWork;
        }
        return TRUE;
    }

    // 본 창 + 도시 창 전체를 한 덩어리로 작업영역 내 랜덤 위치로 이동 (상대 위치 유지)
    void MoveGroup() {
        RECT wa;
        MonList L = {};
        if (cfg_.allMon) EnumDisplayMonitors(NULL, NULL, MonProc, (LPARAM)&L);
        if (L.n > 0) wa = L.r[rand() % L.n];    // 모든 모니터 On: 임의의 모니터 선택
        else         SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);   // Off: 기본 모니터

        RECT g;
        GetWindowRect(hwnd_, &g);
        for (ClockWindow* p = head_; p; p = p->next_) {
            RECT r;
            GetWindowRect(p->hwnd_, &r);
            UnionRect(&g, &g, &r);
        }
        int mx = wa.right - wa.left - (g.right - g.left);
        int my = wa.bottom - wa.top - (g.bottom - g.top);
        int dx = wa.left + (mx > 0 ? rand() % (mx + 1) : 0) - g.left;
        int dy = wa.top  + (my > 0 ? rand() % (my + 1) : 0) - g.top;

        Shift(dx, dy);
        for (ClockWindow* p = head_; p; p = p->next_) p->Shift(dx, dy);
        SavePos();                              // 이동할 때마다 저장
        { RECT mr; GetWindowRect(hwnd_, &mr); Log::Put("창 그룹 이동 -> (%d,%d) 창 %d개", (int)mr.left, (int)mr.top, 1 + CountCities()); }
    }

    void Shift(int dx, int dy) {
        RECT r;
        GetWindowRect(hwnd_, &r);
        SetWindowPos(hwnd_, NULL, r.left + dx, r.top + dy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // 본 창 위치를 ini 에 저장 (최소화 상태면 건너뜀)
    void SavePos() {
        if (owner_ || IsIconic(hwnd_) || cfg_.analogFull) return;   // 전체화면 위치는 저장하지 않음
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        if (hidden_) {                          // 타이틀바 숨김 상태면 보임 상태의 창 좌표로 환산해서 저장
            RECT r = {0, 0, wr.right - wr.left, wr.bottom - wr.top};
            AdjustWindowRect(&r, NormalStyle(), FALSE);
            OffsetRect(&wr, r.left, r.top);
        }
        cfg_.SavePos(wr.left, wr.top);
    }

    // 폰트 이름(UTF-8 또는 ANSI) -> UTF-16 (LF_FACESIZE 이하)
    static void FaceToWide(const char* name, LPWSTR out) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, out, LF_FACESIZE))
            MultiByteToWideChar(CP_ACP, 0, name, -1, out, LF_FACESIZE);
    }

    // 폰트 이름은 UTF-8(또는 ANSI) 문자열 -> UTF-16 변환 후 생성. 크기는 픽셀 높이
    static HFONT MakeFont(const char* name, int px) {
        char16_t face[LF_FACESIZE] = {};
        FaceToWide(name, (LPWSTR)face);
        return CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, (LPCWSTR)face);
    }

    // UTF-8 한 줄을 오프셋만큼 옮겨서 그림
    static void DrawLine(HDC dc, HFONT font, COLORREF color, const char* utf8, int len,
                         RECT r, int offset, UINT fmt) {
        char16_t u[128];
        int n = Utf8::ToUtf16(utf8, len, u, 128);
        r.left += offset;
        r.right += offset;
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, (LPCWSTR)u, n, &r, fmt);
        SelectObject(dc, old);
    }

    Settings&    cfg_;                  // 모든 창이 공유하는 설정
    ClockWindow* owner_;                // NULL = 본 창, 아니면 본 창 (도시 창)
    const char*  city_;                 // 표시할 도시 이름 (UTF-8)
    Zone         zone_;                 // 도시 시각 (본 창은 로컬 시각)
    HINSTANCE    hi_           = NULL;
    HWND         hwnd_         = NULL;
    HFONT        font_         = NULL;
    HFONT        dateFont_     = NULL;
    HBRUSH       bgBrush_      = NULL;
    HMENU        menu_         = NULL;  // 본 창만 (더블클릭 팝업 메뉴)
    enum { TOP_DIGITAL = 1, TOP_ANALOG = 2 };           // 최상위 메뉴 위치: F3 디지탈 시계 디자인 / F4 아나로그 시계 디자인
    static constexpr int TOP_MENU_COUNT = 8;            // 최상위 메뉴 개수 (F2 ~ F9. F1 은 전체 메뉴)
    HMENU        menuTop_[TOP_MENU_COUNT] = {};         // 최상위 메뉴의 하위 팝업 (순서대로 F2 ~ F9)
    HMENU        menuSet_      = NULL;
    HMENU        menuDsn_      = NULL;  // F3) 디지탈 시계 디자인
    HMENU        menuATheme_   = NULL;  // F4) 아날로그 시계 디자인 (테마 목록)
    HMENU        menuOpacity_  = NULL;
    HMENU        menuMove_     = NULL;
    HMENU        menuGlobal_   = NULL;
    HMENU        menuTimeFmt_  = NULL;
    HMENU        menuDateFmt_  = NULL;
    HMENU        menuAlarm_    = NULL;
    HMENU        menuScan_     = NULL;
    HMENU        menuBack_     = NULL;
    HMENU        menuProg_     = NULL;
    int          pgFiredYmd_   = 0;     // 프로그램 실행을 마지막으로 한 날짜
    int          rbFiredYmd_   = 0;     // 시스템 재시작을 마지막으로 실행한 날짜
    HMENU        menuAlarmSub_[ALARM_TOTAL] = {};
    HMENU        menuHomeProg_[2] = {};
    int          firedYmd_[ALARM_TOTAL]     = {};   // 알람이 마지막으로 울린 날짜 (YYYYMMDD)
    Pending      pend_[ALARM_TOTAL];                // 자동 닫기 대기 중인 플레이어 창 정보
    bool         quitting_     = false; // 본 창: 앱 종료 중 (도시 창 닫힘을 목록 변경으로 저장하지 않음)
    bool         trayOn_       = false; // 트레이 아이콘 표시 중 (본 창)
    HICON        trayIcon_     = NULL;  // 트레이 아이콘 (LoadImage 로 만든 것: 제거할 때 DestroyIcon)
    bool         hidden_       = false; // 타이틀바 숨김 상태
    bool         pressed_      = false; // 클라이언트 영역에서 왼쪽 버튼을 누른 상태 (클릭/끌기 판별)
    POINT        downPt_       = {0, 0};
    int          lastPaintMin_ = -1;    // 마지막으로 그린 분 (아날로그 초침 생략: 분이 바뀔 때만 다시 그림)
    POINT        preFull_      = {0, 0};  // 전체화면 켜기 직전의 창 위치
    bool         modal_        = false; // 메뉴/대화상자 사용 중 (자동 이동 일시 정지)
    ScanEngine*  scan_         = NULL;  // 본 창: scan 엔진 (WinMain 소유)
    std::vector<ipcam::Session*> cams_;  // 본 창: 열려 있는 카메라 영상 세션들 (메뉴를 누를 때마다 새 창, 중복 실행 가능)
    std::string  statusText_;           // 본 창: 맨 아래 상태줄 문구 (scan On/Off 등)
    HFONT        statusFont_   = NULL;
    ClockWindow* head_         = NULL;  // 본 창: 도시 창 목록
    ClockWindow* next_         = NULL;
    Scroller     timeScroll_;           // 시분초: 0.1초마다 2px
    Scroller     dateScroll_;           // 년월일: 0.2초마다 1px
    Scroller     lunarScroll_;          // 음력: 년월일과 같은 속도
};

// ---------------------------------------------------------------- WinMain
// 실행할 때마다 exe 와 같은 폴더에 version.txt 를 새로 만듦 (내용: version.h 의 APP_VERSION_STR)
static void WriteVersionFile() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    int n = lstrlenA(path);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    lstrcpyA(path + n, "version.txt");
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log::Put("version.txt 생성 실패 (오류 %d)", (int)GetLastError());
        return;
    }
    DWORD w;
    WriteFile(h, APP_VERSION_STR "\r\n", (DWORD)lstrlenA(APP_VERSION_STR) + 2, &w, NULL);
    CloseHandle(h);
    Log::Put("version.txt 생성: %s", APP_VERSION_STR);
}

// ini [Option] booting_alert=TRUE 일 때: 앱 폴더의 booting_alert\booting_Alert_RUN.BAT 실행 (창 숨김, 작업 폴더는 그 BAT 가 있는 폴더)
static void RunBootingAlert() {
    wchar_t dir[MAX_PATH], bat[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* sl = wcsrchr(dir, L'\\');
    if (sl) *sl = 0;
    lstrcatW(dir, L"\\booting_alert");
    lstrcpyW(bat, dir);
    lstrcatW(bat, L"\\booting_Alert_RUN.BAT");
    if (GetFileAttributesW(bat) == INVALID_FILE_ATTRIBUTES) {
        Log::Put("booting_alert=TRUE 이지만 .\\booting_alert\\booting_Alert_RUN.BAT 파일이 없어 실행하지 않음");
        return;
    }
    HINSTANCE r = ShellExecuteW(NULL, L"open", bat, NULL, dir, SW_HIDE);       // BAT 창 숨김
    Log::Put("booting_alert: .\\booting_alert\\booting_Alert_RUN.BAT 실행 %s (결과코드 %d)", (INT_PTR)r > 32 ? "성공" : "실패", (int)(INT_PTR)r);
}

// 텔레그램 /restart: 같은 exe 를 같은 인자로 다시 실행한다. 호출하기 전에 SingleInstance 뮤텍스를 풀어 두어야
// 새 프로세스가 "이미 실행 중" 으로 취급되어 (자기 자신 대신) 이 프로세스를 강제 종료하는 일이 없다.
static void RestartSelf() {
    wchar_t exe[MAX_PATH * 2] = {0};
    GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
    std::wstring cmdLine = L"\"" + std::wstring(exe) + L"\"";
    const wchar_t* args = GetCommandLineW();                  // 원래 인자를 유지 (첫 토큰은 exe 경로)
    if (args) {
        if (*args == L'"') { ++args; while (*args && *args != L'"') ++args; if (*args) ++args; }
        else while (*args && *args != L' ') ++args;
        cmdLine += args;
    }
    std::wstring dir = exe;
    dir.resize(dir.find_last_of(L"\\/") + 1);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log::Put("재시작: 새 프로세스를 실행했습니다 (pid=%d)", (int)pi.dwProcessId);
    } else {
        Log::Put("재시작 실패: CreateProcess 오류 %d", (int)GetLastError());
    }
}

// ini [Option] companion_app1~5 가 있으면 각각: cmd /c start "" "<경로>" 로 실행 (작업 폴더는 그 파일이 있는 폴더, cmd 창은 숨김)
static void RunCompanionApp(const char* utf8Path, bool asInvoker) {
    if (!utf8Path[0]) return;
    wchar_t path[MAX_PATH], dir[MAX_PATH], args[MAX_PATH + 24];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, path, MAX_PATH)) return;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        Log::Put("동반 실행 앱이 없어 실행하지 않음: %s", utf8Path);
        return;
    }
    lstrcpyW(dir, path);
    wchar_t* sl = wcsrchr(dir, L'\\');
    if (sl) *sl = 0;
    wsprintfW(args, L"/c start \"\" \"%s\"", path);
    if (asInvoker) SetEnvironmentVariableW(L"__COMPAT_LAYER", L"RunAsInvoker");   // 자식 프로세스가 상속: 관리자 권한 요청을 무시하고 일반 권한으로 실행
    HINSTANCE r = ShellExecuteW(NULL, L"open", L"cmd.exe", args, sl ? dir : NULL, SW_HIDE);
    if (asInvoker) SetEnvironmentVariableW(L"__COMPAT_LAYER", NULL);             // 이 앱의 다른 실행에는 영향 없게 원복
    Log::Put("동반 실행 앱 실행 %s (결과코드 %d, 권한 요청 무시 %s): %s", (INT_PTR)r > 32 ? "성공" : "실패", (int)(INT_PTR)r, asInvoker ? "ON" : "OFF", utf8Path);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    Log::Init();
    tg::InstallCrashHandlers();                 // 처리되지 않은 예외는 원인을 log.txt 에 남기고 종료
    SingleInstance guard("gsHomeManager_SingleInstance");   // 중복 실행 방지
    if (!guard.IsFirst()) {                     // 이미 실행 중이면 기존 앱을 종료시키고 이 프로세스가 대신 실행
        Log::Put("기존 실행 중인 앱 감지: 기존 프로세스를 종료하고 새로 시작");
        guard.TakeOver();
    }
    Log::Put("===== 앱 시작 %s %s (pid=%d)", APP_NAME, APP_VERSION_STR, (int)GetCurrentProcessId());
    WriteVersionFile();

    Settings cfg;
    cfg.Init();                                 // ini 읽기 (없으면 주석 포함 기본 파일 생성)
    Log::Put("설정 로드: 이동간격=%dms 투명도=%d%% 시계보이기=%d 년월일=%d 표시형식(시분초*10+년월일)=%d 항상위=%d 모든모니터=%d 모든데스크톱=%d 기준도시=%s",
             cfg.moveMs, cfg.opacity, (int)cfg.visible, (int)cfg.showDate, cfg.timeFmt * 10 + cfg.dateFmt,
             (int)cfg.topmost, (int)cfg.allMon, (int)cfg.allDesk, cfg.baseCity);

    Log::Put("WiFiScan 설정: 블루투스=%s LAN=%s 주기=%d초 외출판정=%d초 Wi-Fi스캔=%d 블루투스스캔=%d",
             cfg.homeBt[0] ? cfg.homeBt : "(없음)", cfg.homeMac[0] ? cfg.homeMac : "(없음)", cfg.scanSec, cfg.notHomeSec,
             (int)cfg.scanWifi, (int)cfg.scanBt);

    DesktopPin::SetProcessAppId();              // 가상 데스크톱 고정에 쓰는 AppID (창 생성 전에 지정)
    ClockWindow win(cfg);
    ScanEngine scan;                            // 블루투스/Wi-Fi scan 엔진 (작업 스레드)
    scan.SetConfig(ScanEngine::FromSettings(cfg));
    win.AttachScan(&scan);
    show = SW_SHOWNORMAL;                       // 시작할 때는 최소화/숨김 지정과 상관없이 항상 화면에 표시
    if (!win.Create(hi, show, CW_USEDEFAULT, CW_USEDEFAULT)) {
        Log::Put("창 생성 실패 (오류 %d)", (int)GetLastError());
        return 1;
    }
    scan.Start("프로그램 시작");                               // 시작할 때 자동으로 scan On
    Log::Put("WiFiScan: 시작할 때 자동으로 scan On");
    tg::Start();                                // Telegram 모듈 (전용 스레드). ini [Telegram] 에 토큰/chat id 가 없으면 스스로 사용 안 함
    Log::Put("Telegram 모듈 시작 (ini [Telegram], 로그 log.txt)");
    if (cfg.rsAuto) {                           // ini [RdpServer] autostart=1: 다른 PC(서버) 자동 시작
        std::string err;
        if (!rdp::Server::Get().Start(RdpServerCfg(cfg), err)) Log::Put("원격 서버 자동 시작 실패: %s", err.c_str());
    }
    if (cfg.trayStart) MemoWindow::Show(hi, "gsHomeManager 를 트레이로 보냅니다", 5);     // ini tray_start=TRUE: 트레이로 시작 안내 (5초)
    for (int i = 0; i < Settings::COMPANION_MAX; i++) RunCompanionApp(cfg.companionApp[i], cfg.companionInvoker);   // ini companion_app1~5: 동반 실행 앱 (start "" 경로)
    if (cfg.bootingAlert) RunBootingAlert();    // ini booting_alert=TRUE: 시작 알림 BAT 실행
    int rc = win.Run();
    rdp::Client::Shutdown();                    // 원격 제어 창/연결 정리
    rdp::Server::Get().Stop();                  // 원격 서버 중지
    tg::RequestStop();                          // Telegram 모듈에 종료 요청 (바로 반환)
    bool clean = scan.Shutdown();               // scan 작업을 취소하고 최대 2초 기다림
    const bool restart = tg::RestartRequested();   // 텔레그램 /restart: 종료한 뒤 같은 exe 를 다시 실행
    // 명령 수신(롱폴링) 중이면 최대 10초쯤 걸린다. 일반 종료는 3초만 기다리고, 재시작은 새 프로세스와 겹치지 않게 충분히 기다린다.
    if (!tg::WaitStopped(restart ? 12000 : 3000)) clean = false;
    Log::Put("===== 앱 종료%s", clean ? "" : " (scan / Telegram 작업이 아직 끝나지 않아 바로 종료)");
    if (restart) { guard.Release(); RestartSelf(); }
    if (!clean) ExitProcess((UINT)rc);          // 도는 스레드가 남아 있으면 정리 없이 종료 (결과 파일은 임시 파일 교체 방식이라 안전)
    return rc;
}
