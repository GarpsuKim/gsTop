// gsHomeManager - 텍스트 시계 + 집 안팎 감시(블루투스/Wi-Fi 스캔) - Windows 11 GUI (WinMain), 클래스 구조, 전부 UTF-8
// (gsClock 과 WiFiScan 을 하나로 합친 앱)
// rc/manifest 불필요. 문자열은 UTF-8 char 로만 다루고, 화면 출력 직전에만 UTF-8 -> UTF-16 변환(char16_t 버퍼)
// 빌드: g++ -Os -s -static -mwindows -Wl,--gc-sections -o gsHomeManager.exe gsHomeManager.cpp gsHomeManager_res.o -lcomdlg32 -lole32 -lcomctl32 -liphlpapi -lws2_32 -lbthprops -lgdiplus -lgdi32   (_build.bat)
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
//                     ScanView / Bluetooth / ResultFile / WifiScanner / EndFile / TriggerFile / HomeWatcher / ScanEngine / ScanDialog
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
#include <wininet.h>                     // 선언만 사용 (wininet.dll 은 동적 로딩이라 링크 옵션 불필요)
#include <propsys.h>
#include <stdlib.h>
#include <stdarg.h>
#include <cstdio>                        // 여기부터 WiFiScan 에서 쓰는 표준 라이브러리
#include <cstdint>
#include <cstring>
#include <cctype>
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
    HomeProg homeProg[2];               // [0] HomeBack 프로그램, [1] NotHome 프로그램 (trigger.txt 에 /HomeBack, /NotHome 을 쓸 때 실행)
    bool     rbOn = false;              // 시스템 재시작 사용
    int      rbH = 0, rbM = 0, rbS = 0; // 시스템 재시작 시각
    // [IPCam] 폰 카메라 앱(DroidCam, IP Webcam 등)의 주소
    char     ipHost[64] = "";           // IP address (없으면 IP cam 영상을 열기 전에 등록)
    int      ipPort     = 4747;         // port 번호 (DroidCam 4747, IP Webcam 8080)
    // [PhoneFtp] 폰 탐색기: 폰의 FTP 서버(CX파일탐색기 등) 주소. 비밀번호는 저장하지 않음
    char     phHost[32] = "";           // IP address (a.b.c.d)
    int      phPort     = 2121;         // port 번호
    char     phUser[64] = "";           // 사용자명
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
            "; booting_alert: TRUE 이면 앱을 시작할 때마다 앱 폴더의 booting_alert\\booting_Alert_RUN.BAT 를 실행합니다. FALSE 이면 실행하지 않습니다.\r\n"
            "booting_alert=FALSE\r\n"
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
            "; --- HomeBack / NotHome 프로그램 (메뉴 WiFiScan > HomeBack 프로그램 / NotHome 프로그램). trigger.txt 에 /HomeBack, /NotHome 을 쓸 때마다 실행합니다.\r\n"
            "; HomeBackProgOn / NotHomeProgOn = 1(사용), 0(해제).  HomeBackProgFile / NotHomeProgFile = 실행할 BAT/CMD/EXE 경로,  HomeBackProgMemo / NotHomeProgMemo = 모니터 중앙에 30초 표시할 메모\r\n"
            "\r\n"
            "[IPCam]\r\n"
            "; 폰 카메라 앱(DroidCam, IP Webcam 등)의 IP address 와 port 번호. 메뉴 'IP cam > IP cam 등록' 으로 입력합니다. (DroidCam 4747, IP Webcam 8080)\r\n"
            "Host=\r\n"
            "Port=4747\r\n"
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
        {                                       // booting_alert: TRUE / FALSE (없으면 FALSE 를 ini 에 써 넣음)
            char v[16];
            GetPrivateProfileStringA("Option", "booting_alert", "", v, sizeof(v), ini_);
            if (!v[0]) WritePrivateProfileStringA("Option", "booting_alert", "FALSE", ini_);
            bootingAlert = lstrcmpiA(v, "TRUE") == 0 || lstrcmpA(v, "1") == 0;
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
    static bool Show(HINSTANCE hi, HWND owner, const char* title, int& h, int& m, int& s) {
        INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_DATE_CLASSES};
        InitCommonControlsEx(&ic);

        DlgBuilder b;
        b.D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        b.D(0);
        b.W(3); b.W(0); b.W(0); b.W(150); b.W(62);          // 항목 수, x, y, cx, cy (대화상자 단위)
        b.W(0); b.W(0);                                     // 메뉴 없음, 기본 클래스
        b.S(title);
        b.W(9); b.S("Segoe UI");                            // DS_SETFONT: 글꼴 크기, 이름
        b.Control(WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN,
                  15, 12, 120, 14, IDC_TIME, "SysDateTimePick32", "");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 25, 38, 45, 14, IDOK, "확인");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,    80, 38, 45, 14, IDCANCEL, "취소");

        int t[3] = {h, m, s};
        INT_PTR ok = DialogBoxIndirectParamW(hi, (LPCDLGTEMPLATEW)b.Data(), owner, Proc, (LPARAM)t);
        if (ok != 1) return false;
        h = t[0];
        m = t[1];
        s = t[2];
        return true;
    }

private:
    enum { IDC_TIME = 100 };

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
        HMODULE m = LoadLibraryA("wininet.dll");
        if (!m) return false;
        FnOpen     fOpen    = (FnOpen)GetProcAddress(m, "InternetOpenA");
        FnOpenUrl  fOpenUrl = (FnOpenUrl)GetProcAddress(m, "InternetOpenUrlA");
        FnRead     fRead    = (FnRead)GetProcAddress(m, "InternetReadFile");
        FnClose    fClose   = (FnClose)GetProcAddress(m, "InternetCloseHandle");
        FnSetOpt   fSetOpt  = (FnSetOpt)GetProcAddress(m, "InternetSetOptionA");
        bool ok = false;
        if (fOpen && fOpenUrl && fRead && fClose) {
            HINTERNET net = fOpen("gsHomeManager/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
            if (net) {
                DWORD to = 8000;                                  // 접속/수신 제한 시간 8초
                if (fSetOpt) {
                    fSetOpt(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
                    fSetOpt(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));
                }
                HINTERNET u = fOpenUrl(net, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
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
//  - 만드는/읽는 파일은 WiFiScan.cpp 와 같다: WiFiScan_result.txt, WiFiScan_watch.log, oui.txt (exe 와 같은 폴더),
//    ..\screen_telegram\trigger.txt (명령 전달), ..\screen_telegram\wifiscanTriger.txt (END 를 쓰면 scan Off)
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
    static std::string telegramDir() { return dir() + "..\\screen_telegram\\"; }
    static std::string triggerFile() { return telegramDir() + "trigger.txt"; }
    static std::string endFile()    { return telegramDir() + "wifiscanTriger.txt"; }
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
// 종료 파일 (wifiscanTriger.txt) : scan 을 시작할 때 무조건 지우고, 그 뒤 내용이 END 이면 scan Off (텔레그램 /stop)
// ======================================================================
class EndFile {
public:
    static void remove() { DeleteFileA(AppDir::endFile().c_str()); }
    static bool requested() {
        std::ifstream f(AppDir::endFile(), std::ios::binary);
        if (!f) return false;
        std::string l; std::getline(f, l);
        if (l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3);
        return StrUtil::upper(StrUtil::trim(l)) == "END";
    }
};

// ======================================================================
// 명령 전달 파일 (trigger.txt): screen_telegram.exe 가 1초마다 읽어 처리한다
// ======================================================================
// 작업 스레드 -> 본 창(UI 스레드) 알림: 하단 상태줄 문구, 모니터 중앙 팝업 (trigger.txt 갱신 등)
class ScanEvents {
public:
    enum { MSG = WM_APP + 4 };
    struct Item { int kind; std::string text; };      // kind: 0=상태줄, 1=팝업(5초), 2=HomeBack 프로그램 실행, 3=NotHome 프로그램 실행
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

class TriggerFile {
public:
    explicit TriggerFile(std::string path) : path_(std::move(path)) {}
    const std::string& path() const { return path_; }
    // 임시 파일에 쓴 뒤 이름 바꾸기 -> 반쯤 쓰인 파일을 읽지 않음
    void send(const std::string& cmd) const {
        const std::string tmp = path_ + ".tmp";
        bool ok = false;
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (f) { f << cmd << "\r\n"; f.flush(); ok = f.good(); }
        }
        if (ok) ok = MoveFileExA(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        DWORD err = ok ? 0 : GetLastError();
        if (!ok) DeleteFileA(tmp.c_str());
        Logger::log(std::string("트리거 파일 ") + (ok ? "기록 성공" : "기록 실패 (오류 " + std::to_string(err) + ", screen_telegram 폴더 확인)") + ": " + cmd);
        if (ok) {
            ScanEvents::Popup("trigger.txt 갱신: " + cmd);
            if (cmd == "/HomeBack") ScanEvents::Program(0);
            else if (cmd == "/NotHome") ScanEvents::Program(1);
        }      // 모니터 중앙에 5초 팝업 (본 창이 표시)
    }
private:
    std::string path_;
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
//   전달 방식: screen_telegram 폴더의 trigger.txt 에 명령 한 줄을 쓴다 (screen_telegram.exe 가 1초마다 읽어 처리)
// ======================================================================
class HomeWatcher {
public:
    HomeWatcher(Subnet sn, ScanCfgBox& cfg, ScanView& view, const Cancel& cancel)
        : sn_(sn), cfg_(cfg), view_(view), cancel_(cancel), trigger_(AppDir::triggerFile()) {}

    // 반환: true = wifiscanTriger.txt 의 END 로 끝남, false = 취소됨 (scan Off / 재시작 / 앱 종료)
    bool run() {
        apply(cfg_.get());
        Logger::log("설정: gsHomeManager.ini [WiFiScan] / 트리거 파일: " + trigger_.path());
        Logger::log("감시 시작: 블루투스 " + (haveBt() ? btMac_ : std::string("(없음)")) + " -> Wi-Fi " + (haveWifi() ? mac_ : std::string("(없음)")) +
                    " (간격 " + std::to_string(interval_) + "초, 외출 판정 " + std::to_string(awaySec_) + "초)");
        for (;;) try {
            if (cancel_()) return false;
            if (EndFile::requested()) return endNow();
            if (apply(cfg_.get()))
                Logger::log("설정 변경: 블루투스 " + (haveBt() ? btMac_ : std::string("(없음)")) + " / Wi-Fi " + (haveWifi() ? mac_ : std::string("(없음)")) +
                            " (간격 " + std::to_string(interval_) + "초, 외출 판정 " + std::to_string(awaySec_) + "초)");
            if (!haveBt() && !haveWifi()) {            // 확인할 대상이 없어도 멈추지 않고, 설정이 저장되면 자동으로 시작
                if (!warned_) { Logger::log("HomeBluetooth 와 HomeMac 이 없거나 스캔 대상이 아닙니다. 설정을 기다리는 중"); warned_ = true; }
                view_.setPhase("감시 대기 중: 블루투스 / LAN 카드 MAC 주소를 입력하고 저장하세요");
                if (sleepOrStop(interval_)) return end_;
                continue;
            }
            warned_ = false;
            if (state_ == -1) view_.setPhase("감시 중 - 블루투스 / Wi-Fi 확인 중...");
            std::string via;
            const bool present = checkBluetooth(via) || checkWifi(via);   // 1) 블루투스 우선  2) 확인되지 않으면 Wi-Fi
            if (cancel_()) return false;
            update(present, via);
            if (sleepOrStop(interval_)) return end_;
        } catch (const std::exception& e) {
            Logger::log(std::string("오류: ") + e.what());
            if (sleepOrStop(interval_)) return end_;
        }
    }

private:
    bool endNow() { Logger::log("wifiscanTriger.txt = END: scan 을 끕니다"); end_ = true; return true; }

    // interval 초 동안 쉬면서 취소 / 종료 파일(1초마다)을 본다. 멈춰야 하면 true
    bool sleepOrStop(int seconds) {
        for (int i = 0; i < seconds * 10; i++) {
            if (cancel_()) return true;
            if (i % 10 == 0 && EndFile::requested()) return endNow();
            Sleep(100);
        }
        return cancel_() || (EndFile::requested() && endNow());
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
    bool useBt_ = false, useWifi_ = false, warned_ = false, end_ = false;
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
        EndFile::remove();                                    // 시작할 때 종료 파일은 무조건 지운다 (예전 END 로 바로 멈추지 않도록)
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        struct WsaGuard { ~WsaGuard() { WSACleanup(); } } wsaGuard;
        const ScanCfg c = cfg_.get();
        Logger::log("WiFiScan 시작: 1) 블루투스 스캔 2) Wi-Fi 스캔 3) 감시 (종료: " + AppDir::endFile() + " 에 END)");

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
        const bool end = w.run();
        if (end && !cancel()) { on_ = false; view.freeze("scan Off (wifiscanTriger.txt 의 END)"); ScanEvents::Status("scan Off - END 파일"); }
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
            DrawTextA(dc, status_.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
            if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) { IPLOG("connected"); break; }
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
        path = SaveDir() + Stamp(".avi");
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
// 영상 창: 위쪽 버튼 줄(우로 90도 회전 / 좌로 90도 회전 / 사진 저장 / 동영상 촬영) + 영상.
// 창이 닫히면 본 창(notify)에 MSG_CLOSED 를 보내 세션을 정리하게 한다 (앱은 계속 실행).
class ViewerWindow {
public:
    static const UINT MSG_CLOSED = WM_APP + 5;

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
        hwnd_ = CreateWindowA("IPCam", "IP Webcam", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr, nullptr, inst_, this);
        if (!hwnd_) { IPLOG("CreateWindow failed err=%lu", GetLastError()); return false; }
        SetTimer(hwnd_, 1, 1000, nullptr);
        IPLOG("window created hwnd=%p", (void*)hwnd_);
        return true;
    }

    HWND Hwnd() const { return hwnd_; }
    void Destroy() { HWND h = hwnd_; if (h && IsWindow(h)) DestroyWindow(h); }

    // Safe to call from any thread.
    void NotifyFrame() { HWND h = hwnd_; if (h) PostMessage(h, WM_APP, 0, 0); }

private:
    enum { ID_RIGHT = 1, ID_LEFT = 2, ID_PHOTO = 3, ID_REC = 4, ID_FOLDER = 5, STRIP_H = 38 };

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
        case WM_TIMER:      OnTimer(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:      OnPaint(); return 0;
        case WM_COMMAND:    OnCommand(LOWORD(w)); SetFocus(h); return 0;       // 버튼을 눌러도 Esc 가 창에 전달되도록
        case WM_KEYDOWN:    if (w == VK_ESCAPE) DestroyWindow(h); return 0;
        case WM_CLOSE:      IPLOG("WM_CLOSE"); break;
        case WM_DESTROY:
            IPLOG("WM_DESTROY");
            KillTimer(h, 1);
            hwnd_ = nullptr;
            if (notify_) PostMessage(notify_, MSG_CLOSED, 0, 0);     // 앱은 종료하지 않고 본 창이 세션만 정리
            return 0;
        }
        return DefWindowProc(h, m, w, l);
    }

    void MakeButtons(HWND h) {
        struct B { int id; const char16_t* text; int w; };
        static const B bs[5] = {{ID_RIGHT, u"우로 90도 회전", 112}, {ID_LEFT, u"좌로 90도 회전", 112},
                                {ID_PHOTO, u"사진 저장", 92}, {ID_REC, u"동영상 촬영", 124}, {ID_FOLDER, u"저장 폴더 열기", 112}};
        int x = 6;
        for (int i = 0; i < 5; i++) {
            HWND b = CreateWindowExW(0, L"BUTTON", (LPCWSTR)bs[i].text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     x, 5, bs[i].w, 28, h, (HMENU)(INT_PTR)bs[i].id, inst_, nullptr);
            SendMessage(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            x += bs[i].w + 6;
        }
    }

    void SetMsg(const std::string& s) { msg_ = s; msgUntil_ = GetTickCount() + 6000; InvalidateRect(hwnd_, nullptr, FALSE); }

    void OnCommand(int id) {
        switch (id) {
        case ID_RIGHT: rot_ = (rot_ + 1) & 3; IPLOG("rotate right -> %d", rot_ * 90); InvalidateRect(hwnd_, nullptr, FALSE); break;
        case ID_LEFT:  rot_ = (rot_ + 3) & 3; IPLOG("rotate left -> %d", rot_ * 90);  InvalidateRect(hwnd_, nullptr, FALSE); break;
        case ID_PHOTO: SavePhoto(); break;
        case ID_REC:   ToggleRecord(); break;
        case ID_FOLDER: OpenFolder(); break;
        }
    }

    void OpenFolder() {                                         // 저장 폴더(IPCam\)를 탐색기로 연다
        const std::string dir = SaveDir();
        ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);       // SaveDir() 는 ANSI 경로 (한글 경로도 그대로 동작)
    }

    void SavePhoto() {
        Gdiplus::Bitmap* b = store_.CopyFrame();
        std::string jpg;
        if (!b) { SetMsg("저장할 영상이 아직 없습니다"); return; }
        bool ok = EncodeJpeg(b, rot_, jpg);
        delete b;
        if (!ok) { SetMsg("사진 저장 실패 (인코딩)"); IPLOG("photo: encode failed"); return; }
        const std::string path = SaveDir() + Stamp(".jpg");
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { SetMsg("사진 저장 실패: " + path); IPLOG("photo: cannot open %s", path.c_str()); return; }
        fwrite(jpg.data(), 1, jpg.size(), f);
        fclose(f);
        IPLOG("photo saved: %s (%u bytes)", path.c_str(), (unsigned)jpg.size());
        SetMsg("사진 저장: " + path);
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
    }

    void OnTimer() {
        if (rec_.Full() && rec_.Active()) { StopRecord(); SetMsg("파일 크기 한도에 도달해 녹화를 종료했습니다"); }
        char t[256];
        int n = snprintf(t, sizeof t, "IP Webcam %s:%s  -  %d fps", cfg_.host.c_str(), cfg_.port.c_str(), store_.TakeFps());
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
        RECT vr = {0, STRIP_H, rc.right, rc.bottom};
        if (vr.bottom > vr.top) store_.Paint(mdc, vr, rot_);
        if (!msg_.empty() && (LONG)(msgUntil_ - GetTickCount()) > 0) {      // 저장/녹화 안내 (6초)
            char16_t u[600];
            if (Utf8::ToUtf16(msg_.c_str(), -1, u, 600)) {
                SetBkMode(mdc, TRANSPARENT);
                SetTextColor(mdc, RGB(255, 230, 80));
                HGDIOBJ of = SelectObject(mdc, GetStockObject(DEFAULT_GUI_FONT));
                RECT tr = {8, rc.bottom - 24, rc.right - 8, rc.bottom - 4};
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
    std::string msg_;
    DWORD msgUntil_ = 0;
    volatile HWND hwnd_ = nullptr;
};

// ============================================================ Session
// 'IP cam 영상' 한 번의 실행: 로그 열기 -> Winsock/GDI+ -> 영상 창 -> 수신 스레드. 소멸 순서는 생성의 반대 (수신 스레드 -> 창 -> 프레임 -> GDI+ -> Winsock -> 로그)
class Session {
public:
    static const UINT MSG_CLOSED = ViewerWindow::MSG_CLOSED;

    Session(HINSTANCE inst, HWND notify, const std::string& host, const std::string& port)
        : cfg_(Make(host, port)), window_(inst, cfg_, store_, rec_, notify),
          client_(cfg_, store_, [this] { window_.NotifyFrame(); },
                  [this](const char* d, size_t n, Gdiplus::Bitmap* b) { rec_.Add(d, n, b); }) {}
    ~Session() { window_.Destroy(); client_.Stop(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool Open() {
        IPLOG("target=%s:%s%s", cfg_.host.c_str(), cfg_.port.c_str(), cfg_.path.c_str());
        if (!ws_.Ok())  return Fail("WSAStartup failed");
        if (!gdi_.Ok()) return Fail("GdiplusStartup failed");
        if (!window_.Create()) return Fail("Window creation failed (see ipcam.log)");
        client_.Start();
        SetForegroundWindow(window_.Hwnd());
        return true;
    }
    HWND Hwnd() const { return window_.Hwnd(); }

private:
    // 로그 파일: exe 와 같은 폴더의 ipcam.log (이름은 예전 그대로), 시작할 때마다 새로 작성. 크래시 시 예외 코드를 기록
    struct LogScope {
        LONG (WINAPI *prev)(EXCEPTION_POINTERS*) = nullptr;
        LogScope() {
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
    Config          cfg_;
    WinsockSession  ws_;
    GdiplusSession  gdi_;
    FrameStore      store_;
    Recorder        rec_;                 // 동영상 촬영 (창/수신 스레드보다 나중에 없어짐 -> 파일을 마무리)
    ViewerWindow    window_;
    MjpegClient     client_;
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
        RECT r = {0, 0, 336, ClientH()};
        AdjustWindowRect(&r, NormalStyle(), FALSE);           // 고정 메뉴바 없음 (메뉴는 더블클릭 팝업)
        int w = r.right - r.left, ht = r.bottom - r.top;
        if (!owner_) cfg_.LoadPos(x, y, w, ht); // ini 에 저장된 마지막 위치로 시작

        HWND h = CreateWindowExA(0, "gsHomeManager", "gsHomeManager", NormalStyle(), x, y, w, ht,
                                 owner_ ? owner_->hwnd_ : NULL, NULL, hi, this);
        if (!h) return false;

        char16_t tu[64];
        Utf8::ToUtf16(owner_ ? city_ : "gsHomeManager", -1, tu, 64);   // UTF-8 제목
        SetWindowTextW(h, (LPCWSTR)tu);
        ShowWindow(h, cfg_.visible ? show : SW_HIDE);   // 숨김 상태로 시작하면 트레이 아이콘만 표시
        if (!owner_) {
            Lunar::Init(cfg_.lunarUrl, cfg_.lunarKey, h);     // 음력 조회 결과를 받을 창
            if (!cfg_.LunarReady()) Log::Put("음력: ini [Lunar] ApiUrl/ApiKey 가 없어 '음력 보이기' 비활성");
        }
        if (!owner_ && cfg_.backFollow) ApplyDesktopColor();   // 시작할 때 현재 바탕색 적용
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
        TID_CLICK       = 6,        // 한 번 클릭 확정 대기 (더블클릭이 아니면 타이틀바 토글)

        CMD_BACKCOLOR   = 101,      // 메뉴 명령
        CMD_TIMEFONT    = 102,
        CMD_DATEFONT    = 103,
        CMD_EXIT        = 104,
        CMD_RESET       = 105,
        CMD_TOPMOST     = 106,
        CMD_ALLMON      = 107,
        CMD_ALLDESK     = 108,
        CMD_HELP        = 730,      // 730 About, 731 ReadMe.MD, 732 홈페이지, 733 최신 버전 다운로드, 734 음력 API 키값 등록
        CMD_SCAN        = 740,      // WiFiScan 대화상자
        CMD_MYPHONE     = 741,      // 앱 동작 설정 > 내 폰 등록 (WiFiScan 대화상자 + 안내 문구)
        CMD_IPREG       = 742,      // IP cam > IP cam 등록
        CMD_IPVIEW      = 743,      // IP cam > IP cam 영상
        CMD_PHONE       = 744,      // IP cam / 폰 연결 > 폰 탐색기
        CMD_TFMT        = 700,      // 시분초 표시 형식 700~709
        CMD_DFMT        = 720,      // 년월일 표시 형식 720~729
        FMT_COUNT       = 10,
        CMD_BACKDESK    = 113,      // 시계 바탕색과 모니터 바탕색을 동일하게 토글
        CMD_SHOWLUNAR   = 114,      // 음력 보이기/숨기기 토글
        CMD_SHOWDATE    = 112,      // 년월일 보이기/숨기기 토글
        CMD_SHOWCLOCK   = 111,      // 시계 보이기/숨기기 토글 (트레이 메뉴에서도 사용)
        WM_TRAY         = WM_APP + 1,   // 트레이 아이콘 콜백
        CMD_OPACITY     = 200,      // 200 + (투명도/10) : 201~210
        CMD_CITY        = 300,      // 300 + 도시 인덱스
        CMD_MOVE        = 400,      // 400 + 이동 간격 선택지 인덱스
        CMD_PROG        = 610,      // 610 사용, 611 시각, 612 파일, 613 메모, 614 지우기, 615 테스트
        CMD_REBOOT      = 600,      // 600 사용, 601 시각 (CMD_ALARM + 10*ALARM_ACTS 이후)
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
        switch (m) {
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
            break;
        case WM_LBUTTONDOWN:                                      // 한 번 클릭: 타이틀바 숨김<->보임 (더블클릭이 아님이 확인되면)
            pressed_ = true;
            downPt_.x = (short)LOWORD(l);
            downPt_.y = (short)HIWORD(l);
            SetCapture(hwnd_);
            return 0;
        case WM_MOUSEMOVE:                                        // 클릭한 채 끌면 창 이동 (타이틀바 유무와 무관)
            if (pressed_ && (w & MK_LBUTTON)) {
                int dx = (short)LOWORD(l) - downPt_.x, dy = (short)HIWORD(l) - downPt_.y;
                if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG)) {
                    pressed_ = false;
                    ReleaseCapture();
                    SendMessageA(hwnd_, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
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
            break;
        case WM_SETTINGCHANGE:                                    // 바탕화면이 바뀌면 (단색일 때) 바탕색 다시 맞춤
            if (!owner_ && cfg_.backFollow && w == SPI_SETDESKWALLPAPER) ApplyDesktopColor();
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
            break;
        case TID_MOVE:
            if (!modal_) MoveGroup();
            return;
        case TID_CLOSE:
            ProcessPending();
            return;
        case TID_CLICK:
            KillTimer(hwnd_, TID_CLICK);
            ToggleFrame();
            return;
        case TID_SCROLL_TIME:
            timeScroll_.Advance(ClientWidth());
            break;
        case TID_SCROLL_DATE:
            dateScroll_.Advance(ClientWidth());
            lunarScroll_.Advance(ClientWidth());
            break;
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    // 작업 스레드에서 온 알림: 상태줄 문구 갱신 / trigger.txt 갱신 팝업 (5초)
    void OnScanEvents() {
        for (const ScanEvents::Item& it : ScanEvents::Take()) {
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
        char buf[96];
        int n;

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
        BitBlt(sdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, obm);
        DeleteObject(bm);                       // 그리자마자 해제 -> 상주 메모리 증가 없음
        DeleteDC(dc);
        EndPaint(hwnd_, &ps);
    }

    void OnDestroy() {
        if (!owner_ && cam_) { delete cam_; cam_ = NULL; }      // 앱 종료: IP cam 영상 창/스레드 정리
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
        if (cfg_.opacity < 100) {
            SetWindowLongPtrA(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd_, 0, (BYTE)(cfg_.opacity * 255 / 100), LWA_ALPHA);
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
        HMENU dsn = CreatePopupMenu();                  // 시계 디자인 설정: 바탕색 / 투명도 / 위치 이동 간격 / 표시 형식 / 글꼴
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
        AddMenu(dsn, MF_POPUP, (UINT_PTR)menuMove_, "위치 이동 간격 변경(&I)");

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
        AddMenu(set, 0,        CMD_SHOWDATE,  "년월일 보이기(&Y)");
        AddMenu(set, 0,        CMD_SHOWLUNAR, "음력 보이기(&U)");
        AddMenu(set, 0,        CMD_SHOWCLOCK, "시계 보이기(&H)  (해제하면 트레이에서만 동작)");
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        AddMenu(set, 0,        CMD_EXIT,      "프로그램 종료 (&Exit)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)set, "앱 동작 설정(&S)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)dsn, "시계 디자인 설정(&D)");

        HMENU glob = menuGlobal_ = CreatePopupMenu();   // Global: 도시를 고르면 그 도시 시계 창이 하나 더 추가됨
        for (int i = 0; i < CITY_COUNT; i++) AddMenu(glob, 0, CMD_CITY + i, CITIES[i].name);
        AddMenu(bar, MF_POPUP, (UINT_PTR)glob, "Global(&G)");

        menuAlarm_ = CreatePopupMenu();         // 알람1 ~ 알람10 (각각 사용/시각/파일/지우기 서브메뉴)
        for (int i = 0; i < ALARM_COUNT; i++) MakeAlarmSub(i);
        AppendMenuA(menuAlarm_, MF_SEPARATOR, 0, NULL);
        menuReboot_ = CreatePopupMenu();        // 시스템 재시작 시각 (알람 10개 + 구분선 다음, 위치 ALARM_COUNT+1)
        AddMenu(menuReboot_, 0, CMD_REBOOT,     "사용(&E)");
        AddMenu(menuReboot_, 0, CMD_REBOOT + 1, "시각 설정(&T)...");
        AppendMenuA(menuAlarm_, MF_POPUP | MF_STRING, (UINT_PTR)menuReboot_, "-");
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
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuAlarm_, "알람(&A)");

        menuScan_ = CreatePopupMenu();          // WiFiScan: 모니터링 대화상자 / 자동감시 On·Off 예약 / HomeBack·NotHome 프로그램
        AddMenu(menuScan_, 0, CMD_SCAN, "WifiScan모니터링(&M)...");   // 블루투스/Wi-Fi 스캔 설정 + 진행 상황 대화상자
        AppendMenuA(menuScan_, MF_SEPARATOR, 0, NULL);
        MakeAlarmSub(ALARM_SCAN_ON);            // 위치 2, 3
        MakeAlarmSub(ALARM_SCAN_OFF);
        MakeHomeProgMenu(0);                    // 위치 4, 5
        MakeHomeProgMenu(1);
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuScan_, "WiFiScan(&W)");

        HMENU ipc = CreatePopupMenu();          // IP cam / 폰 연결: IP cam 등록 / 영상 / 폰 탐색기
        AddMenu(ipc, 0, CMD_IPREG,  "IP cam 등록(&R)...");
        AddMenu(ipc, 0, CMD_IPVIEW, "IP cam 영상(&V)");
        AddMenu(ipc, 0, CMD_PHONE,  "폰 탐색기(&E)...");
        AddMenu(bar, MF_POPUP, (UINT_PTR)ipc, "IP cam / 폰 연결(&I)");

        HMENU help = CreatePopupMenu();         // Help
        AddMenu(help, 0, CMD_HELP,     "About(&A)");
        AddMenu(help, 0, CMD_HELP + 1, "ReadMe.MD(&R)");
        AddMenu(help, 0, CMD_HELP + 2, "홈페이지(&H)");
        AddMenu(help, 0, CMD_HELP + 3, "최신 버전 다운로드(&D)");
        AddMenu(help, 0, CMD_HELP + 4, "음력 API 키값 등록(&K)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)help, "Help(&H)");
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

    // trigger.txt 에 /HomeBack (i=0) 또는 /NotHome (i=1) 을 쓸 때 호출
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
        wsprintfA(s, "프로그램 실행   %02d:%02d:%02d   %s", cfg_.pgH, cfg_.pgM, cfg_.pgS, cfg_.pgFile[0] ? nm : "__");
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
    void RunProg() { RunProgFile("프로그램 실행", cfg_.pgFile, cfg_.pgMemo); }

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
            bool ok = TimeDialog::Show(hi_, hwnd_, "프로그램 실행 시각 (시:분:초)", cfg_.pgH, cfg_.pgM, cfg_.pgS);
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
        ModifyMenuW(menuAlarm_, ALARM_COUNT + 1, MF_BYPOSITION | MF_POPUP | MF_STRING,
                    (UINT_PTR)menuReboot_, (LPCWSTR)u);
        UINT chk = cfg_.rbOn ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(menuAlarm_, ALARM_COUNT + 1, MF_BYPOSITION | chk);
        CheckMenuItem(menuReboot_, CMD_REBOOT, MF_BYCOMMAND | chk);
    }

    void RebootCommand(int act) {
        if (act == 0) {
            cfg_.rbOn = !cfg_.rbOn;
        } else {
            modal_ = true;
            bool ok = TimeDialog::Show(hi_, hwnd_, "시스템 재시작 시각 (시:분:초)", cfg_.rbH, cfg_.rbM, cfg_.rbS);
            modal_ = false;
            if (!ok) return;
        }
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
        CheckMenuItem(menuSet_, CMD_TOPMOST, MF_BYCOMMAND | (cfg_.topmost ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_ALLMON,  MF_BYCOMMAND | (cfg_.allMon  ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_ALLDESK, MF_BYCOMMAND | (cfg_.allDesk ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menuSet_, CMD_SHOWLUNAR, MF_BYCOMMAND | (cfg_.LunarReady() ? MF_ENABLED : MF_GRAYED));   // 주소/키가 없으면 비활성
        CheckMenuItem(menuSet_, CMD_SHOWLUNAR, MF_BYCOMMAND | (cfg_.LunarOn() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_SHOWDATE, MF_BYCOMMAND | (cfg_.showDate ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_SHOWCLOCK, MF_BYCOMMAND | (cfg_.visible ? MF_CHECKED : MF_UNCHECKED));
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
        case CMD_IPVIEW:    n = "IP cam > IP cam 영상"; break;
        case CMD_PHONE:     n = "IP cam / 폰 연결 > 폰 탐색기"; break;
        case CMD_SHOWDATE:  n = "년월일 보이기/숨기기 토글"; break;
        case CMD_SHOWLUNAR: n = "음력 보이기/숨기기 토글"; break;
        case CMD_RESET:     n = "설정 초기화"; break;
        case CMD_BACKCOLOR: n = "바탕색 선택"; break;
        case CMD_BACKDESK:  n = "시계 바탕색과 모니터 바탕색을 동일하게 토글"; break;
        case CMD_TIMEFONT:  n = "시분초 글꼴 변경"; break;
        case CMD_DATEFONT:  n = "년월일 글꼴 변경"; break;
        case CMD_TOPMOST:   n = "항상 앞에 보이기 토글"; break;
        case CMD_ALLMON:    n = "모든 모니터 적용 토글"; break;
        case CMD_ALLDESK:   n = "모든 가상 데스크톱 적용 토글"; break;
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
        else if (id >= CMD_MOVE && id < CMD_MOVE + MOVE_COUNT) Log::Put("메뉴: 위치 이동 간격 %dms", MoveChoice(id - CMD_MOVE));
        else if (id >= CMD_PROG && id <= CMD_PROG + 5) {
            static const char* const A[] = {"사용 토글", "시각 설정", "파일 선택", "메모 설정", "알람 지우기", "알람 테스트"};
            Log::Put("메뉴: 프로그램 실행 > %s", A[id - CMD_PROG]);
        } else if (id == CMD_REBOOT || id == CMD_REBOOT + 1)
            Log::Put("메뉴: 시스템 재시작 시각 > %s", id == CMD_REBOOT ? "사용 토글" : "시각 설정");
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
        else if (id >= CMD_CITY && id < CMD_CITY + CITY_COUNT) AddCity(id - CMD_CITY, true);
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
        else if (id == CMD_REBOOT || id == CMD_REBOOT + 1)
            RebootCommand(id - CMD_REBOOT);
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
    enum { STATUS_H = 22 };                // 본 창 맨 아래 상태줄 높이
    int StatusH() const { return owner_ ? 0 : STATUS_H; }
    int ClientH() const { return ClientH(ShowLunarHere()) + StatusH(); }
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
        n.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);   // 트레이: 리소스 아이콘 (실패하면 기본)
        if (!n.hIcon) n.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        trayOn_ = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
        Log::Put("트레이 아이콘 추가 %s", trayOn_ ? "성공" : "실패");
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
        RECT c = {0, 0, 336, ClientH(false)};      // 새 도시 창: 음력 줄 없음
        AdjustWindowRect(&c, NormalStyle(), FALSE);
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

    // ---- 타이틀바 숨김 <-> 보임 (글자가 그려지는 클라이언트 영역의 위치/크기는 유지. 메뉴바가 없으므로 계산이 정확함)
    void ToggleFrame() {
        RECT c;
        GetClientRect(hwnd_, &c);
        POINT tl = {0, 0};
        ClientToScreen(hwnd_, &tl);
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        hidden_ = !hidden_;
        Log::Put("타이틀바 %s (클릭)", hidden_ ? "숨김" : "보임");
        if (hidden_) {
            SetWindowLongPtrA(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd_, NULL, tl.x, tl.y, c.right, c.bottom, flags);
        } else {
            SetWindowLongPtrA(hwnd_, GWL_STYLE, NormalStyle() | WS_VISIBLE);
            RECT r = {0, 0, c.right, c.bottom};
            AdjustWindowRect(&r, NormalStyle(), FALSE);
            SetWindowPos(hwnd_, NULL, tl.x + r.left, tl.y + r.top,
                         r.right - r.left, r.bottom - r.top, flags);
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    // ---- 더블클릭 팝업 메뉴 (본 창만). 메뉴 밖을 클릭하면 Windows 가 알아서 닫음
    void ShowPopupMenu() {
        if (owner_ || !menu_) return;
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu_, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, NULL);
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
    // ---- IP cam 메뉴: 등록(대화상자 -> ini) / 영상(별도 창, 앱은 계속 실행)
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

    void IpCamView() {
        if (owner_) return;
        if (cam_) {                              // 이미 열려 있으면 앞으로
            HWND w = cam_->Hwnd();
            if (w && IsWindow(w)) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); return; }
            delete cam_;
            cam_ = NULL;
        }
        if (!cfg_.ipHost[0]) {                   // 등록된 주소가 없으면 먼저 등록
            char16_t m[160], t[32];
            Utf8::ToUtf16("먼저 IP cam 의 IP address 와 port 번호를 등록하세요.", -1, m, 160);
            Utf8::ToUtf16("IP cam", -1, t, 32);
            modal_ = true;
            MessageBoxW(hwnd_, (LPCWSTR)m, (LPCWSTR)t, MB_OK | MB_ICONINFORMATION);
            modal_ = false;
            if (!IpCamRegister()) return;
        }
        Log::Put("IP cam 영상 열기: %s:%d", cfg_.ipHost, cfg_.ipPort);
        ipcam::Session* s = new ipcam::Session(hi_, hwnd_, cfg_.ipHost, std::to_string(cfg_.ipPort));
        if (!s->Open()) { delete s; return; }
        cam_ = s;
    }

    void IpCamClosed() {                         // 영상 창이 닫힘: 수신 스레드 정지 + 정리 (ipcam.log 도 여기서 닫힘)
        if (!cam_) return;
        HWND w = cam_->Hwnd();
        if (w && IsWindow(w)) return;            // 이미 새 창이 열려 있음
        Log::Put("IP cam 영상 닫힘");
        delete cam_;
        cam_ = NULL;
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
        SetTimer(hwnd_, TID_SECOND, 1000 - t.wMilliseconds, NULL);
    }

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
        if (owner_ || IsIconic(hwnd_)) return;
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
    HMENU        menuSet_      = NULL;
    HMENU        menuOpacity_  = NULL;
    HMENU        menuMove_     = NULL;
    HMENU        menuGlobal_   = NULL;
    HMENU        menuTimeFmt_  = NULL;
    HMENU        menuDateFmt_  = NULL;
    HMENU        menuAlarm_    = NULL;
    HMENU        menuScan_     = NULL;
    HMENU        menuBack_     = NULL;
    HMENU        menuReboot_   = NULL;
    HMENU        menuProg_     = NULL;
    int          pgFiredYmd_   = 0;     // 프로그램 실행을 마지막으로 한 날짜
    int          rbFiredYmd_   = 0;     // 시스템 재시작을 마지막으로 실행한 날짜
    HMENU        menuAlarmSub_[ALARM_TOTAL] = {};
    HMENU        menuHomeProg_[2] = {};
    int          firedYmd_[ALARM_TOTAL]     = {};   // 알람이 마지막으로 울린 날짜 (YYYYMMDD)
    Pending      pend_[ALARM_TOTAL];                // 자동 닫기 대기 중인 플레이어 창 정보
    bool         quitting_     = false; // 본 창: 앱 종료 중 (도시 창 닫힘을 목록 변경으로 저장하지 않음)
    bool         trayOn_       = false; // 트레이 아이콘 표시 중 (본 창)
    bool         hidden_       = false; // 타이틀바 숨김 상태
    bool         pressed_      = false; // 클라이언트 영역에서 왼쪽 버튼을 누른 상태 (클릭/끌기 판별)
    POINT        downPt_       = {0, 0};
    bool         modal_        = false; // 메뉴/대화상자 사용 중 (자동 이동 일시 정지)
    ScanEngine*  scan_         = NULL;  // 본 창: scan 엔진 (WinMain 소유)
    ipcam::Session* cam_       = NULL;  // 본 창: 열려 있는 IP cam 영상 세션
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

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    Log::Init();
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
    if (cfg.bootingAlert) RunBootingAlert();    // ini booting_alert=TRUE: 시작 알림 BAT 실행
    int rc = win.Run();
    bool clean = scan.Shutdown();               // scan 작업을 취소하고 최대 2초 기다림
    Log::Put("===== 앱 종료%s", clean ? "" : " (scan 작업이 아직 끝나지 않아 바로 종료)");
    if (!clean) ExitProcess((UINT)rc);          // 도는 스레드가 남아 있으면 정리 없이 종료 (결과 파일은 임시 파일 교체 방식이라 안전)
    return rc;
}
