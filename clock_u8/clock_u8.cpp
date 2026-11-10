// 텍스트 시계 - Windows 11 GUI (WinMain), 클래스 구조, 전부 UTF-8, 저메모리
// rc/manifest 불필요. 문자열은 UTF-8 char 로만 다루고, 화면 출력 직전에만 UTF-8 -> UTF-16 변환(char16_t 버퍼)
// 빌드: g++ -Os -s -static -mwindows -Wl,--gc-sections -o clock_u8.exe clock_u8.cpp -lcomdlg32 -lole32 -lcomctl32
//
// 구조
//   Utf8            : UTF-8 변환 도우미
//   SingleInstance  : 중복 실행 방지 (RAII 뮤텍스)
//   Scroller        : 좌우 스크롤 상태 (오프셋/방향, 끝에서 방향 전환)
//   DesktopPin      : 모든 가상 데스크톱에 표시 (셸 COM, 필요할 때만 초기화)
//   CITIES / Zone   : Global 도시 목록 / 도시별 현재 시각 (Windows 시간대 + DST)
//   Settings        : clock_u8.ini 읽기/쓰기 (타이머, 스타일, 옵션, 기준 도시, 창 위치)
//   TimeDialog      : 알람 시각(시:분:초) 입력 대화상자 (코드로 만든 대화상자 + 시간 선택 컨트롤)
//   ClockWindow     : 시계 창 1개. 본 창(메뉴 있음) + Global 도시 창(소유 창, 본 창 아래에 계속 추가)
//   WinMain         : 조립
#include <windows.h>
#include <objbase.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <propsys.h>
#include <stdlib.h>

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

private:
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
    static const char16_t* AppId() { return u"GarpsuKim.TextClock.U8"; }
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

struct Alarm {
    bool on = false;                            // 사용 여부
    int  h = 0, m = 0, s = 0;                   // 알람 시각 (24시간제, 이 PC 로컬 시각)
    char file[784] = "";                        // 재생할 동영상/음악 파일 경로 (UTF-8)
    int  closeMode = 0;                         // 플레이어 자동 닫기: 0=끔, 1=재생이 끝나면(파일 길이), 2=지정 시간 후
    char memo[320] = "";                       // 알람 메모 (UTF-8, 최대 100자). 알람 시각에 모니터 중앙에 30초 표시
    int  closeSec  = 60;                        // 모드 2 의 닫기까지 시간(초). 모드 1 에서 길이를 못 읽을 때도 사용 (0=닫지 않음)
};

// ---------------------------------------------------------------- Settings
// clock_u8.ini (exe 와 같은 폴더) 관리. 값은 시작할 때 읽고, 변경할 때마다 저장한다.
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
    bool     showDate     = true;       // 년월일(도시 이름 포함) 표시
    bool     hour12       = false;      // 시각 표시: false=24시간제, true=12시간제(AM/PM)
    // [Global]
    char     baseCity[32] = "서울";     // 본 창의 기준 도시 이름 (시간은 이 PC 시간대)
    char     cities[1024] = "";         // Global 도시 창 목록 (도시 이름, 쉼표 구분). 시작할 때 모두 복원
    // [Alarm]
    Alarm    alarms[ALARM_COUNT];
    bool     rbOn = false;              // 시스템 재시작 사용
    int      rbH = 0, rbM = 0, rbS = 0; // 시스템 재시작 시각

    void Init() {
        InitPath();
        CreateDefaultIni();
        Load();
    }

    // 기본값으로 되돌림 (기준 도시와 창 위치는 유지). 기본값은 위 멤버 초기값 한 곳에서만 정의
    void ResetDefaults() {
        Settings d;
        moveMs = d.moveMs;  timeScrollMs = d.timeScrollMs;  dateScrollMs = d.dateScrollMs;
        bgColor = d.bgColor;  opacity = d.opacity;
        lstrcpyA(timeFont, d.timeFont);  timeSize = d.timeSize;  timeColor = d.timeColor;
        lstrcpyA(dateFont, d.dateFont);  dateSize = d.dateSize;  dateColor = d.dateColor;
        topmost = d.topmost;  allMon = d.allMon;  allDesk = d.allDesk;  hour12 = d.hour12;
    }

    void WriteInt(const char* section, const char* key, int v) {
        char b[16];
        wsprintfA(b, "%d", v);
        WritePrivateProfileStringA(section, key, b, ini_);
    }

    void SaveStyle() {
        WriteColor("Style", "BackColor", bgColor);
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
        WriteInt("Option", "Hour12", hour12);
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

    // 시스템 재시작 시각 저장: RebootOn / RebootTime
    void SaveReboot() {
        char t[16];
        WriteInt("Alarm", "RebootOn", rbOn);
        wsprintfA(t, "%02d:%02d:%02d", rbH, rbM, rbS);
        WritePrivateProfileStringA("Alarm", "RebootTime", t, ini_);
    }

    // 알람 i (0~9) 저장: AlarmNOn / AlarmNTime / AlarmNFile
    void SaveAlarm(int i) {
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
            "; ===== Text Clock 설정 =====\r\n"
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
            "Hour12=0\r\n"
            "; ShowClock: 1=시계 보이기, 0=숨기기 (시스템 트레이 아이콘만 표시). 메뉴 설정 > 시계 보이기 로 바꿀 수 있습니다.\r\n"
            "ShowClock=1\r\n"
            "; ShowDate: 1=년월일 보이기, 0=숨기기 (시분초만 표시). 메뉴 설정 > 년월일 보이기 로 바꿀 수 있습니다.\r\n"
            "ShowDate=1\r\n"
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
            "; RebootOn=1 이면 매일 RebootTime(HH:MM:SS) 에 재시작 확인창(300초)을 띄웁니다. Yes 또는 무응답이면 재시작 (shutdown /r /f /t 0, 앱 강제 종료), No 이면 재시작하지 않습니다\r\n"
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
        hour12  = ReadBool("Option", "Hour12", hour12);
        visible = ReadBool("Option", "ShowClock", visible);
        showDate = ReadBool("Option", "ShowDate", showDate);

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
        {
            char t[16];
            rbOn = GetPrivateProfileIntA("Alarm", "RebootOn", 0, ini_) != 0;
            GetPrivateProfileStringA("Alarm", "RebootTime", "00:00:00", t, sizeof(t), ini_);
            ParseTime(t, rbH, rbM, rbS);
        }
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
    enum { CAP = 512 };
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
        b.W(5); b.W(0); b.W(0); b.W(190); b.W(80);
        b.W(0); b.W(0);
        b.S(caption);
        b.W(9); b.S("Segoe UI");
        b.Control(WS_CHILD | WS_VISIBLE, 10, 8, 170, 12, IDC_MSG, "STATIC", msg);
        b.Control(WS_CHILD | WS_VISIBLE, 10, 24, 170, 10, IDC_LEFT, "STATIC", "");
        b.Control(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, 38, 170, 10, IDC_BAR, "msctls_progress32", "");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | (defYes ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 40, 58, 45, 14, IDYES, "Yes");
        b.Button(WS_CHILD | WS_VISIBLE | WS_TABSTOP | (defYes ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON), 105, 58, 45, 14, IDNO, "No");

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
            wc.lpszClassName = (LPCWSTR)u"TextClockMemo";
            RegisterClassW(&wc);
            reg = true;
        }
        Data* d = (Data*)malloc(sizeof(Data));
        if (!d) return;
        d->text[0] = 0;
        Utf8::ToUtf16(utf8, -1, d->text, 320);
        d->start = GetTickCount();
        d->sec = sec;
        const int W = 640, H = 220, off = live_ * 30;       // 동시에 여러 개면 조금씩 어긋나게
        int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2 + off;
        int y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2 + off;
        HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, (LPCWSTR)u"TextClockMemo", (LPCWSTR)u"",
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

// ---------------------------------------------------------------- ClockWindow
// 시계 창 1개. owner 가 없으면 본 창(메뉴/ini 위치 저장/랜덤 이동 담당),
// 있으면 Global 도시 창(본 창의 소유 창). 도시 창은 본 창 아래에 계속 추가되고 본 창과 함께 이동한다.
class ClockWindow {
public:
    explicit ClockWindow(Settings& cfg, ClockWindow* owner = NULL,
                         const char* city = NULL, const char* zoneKey = NULL)
        : cfg_(cfg), owner_(owner), city_(city ? city : cfg.baseCity),
          timeScroll_(2), dateScroll_(1) {
        if (zoneKey) zone_.Load(zoneKey);
    }
    ~ClockWindow() {
        if (font_)     DeleteObject(font_);
        if (dateFont_) DeleteObject(dateFont_);
        if (bgBrush_)  DeleteObject(bgBrush_);
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
            CheckHourFormat();
            UpdateOptionChecks();
            UpdateCityChecks();                 // 기준 도시 체크
        }
        RECT r = {0, 0, 336, ClientH()};
        AdjustWindowRect(&r, NormalStyle(), menu_ != NULL);   // 메뉴바 높이 포함
        int w = r.right - r.left, ht = r.bottom - r.top;
        if (!owner_) cfg_.LoadPos(x, y, w, ht); // ini 에 저장된 마지막 위치로 시작

        HWND h = CreateWindowExA(0, "TextClock", "Text Clock", NormalStyle(), x, y, w, ht,
                                 owner_ ? owner_->hwnd_ : NULL, menu_, hi, this);
        if (!h) return false;

        char16_t tu[64];
        Utf8::ToUtf16(owner_ ? city_ : "Text Clock", -1, tu, 64);   // UTF-8 제목
        SetWindowTextW(h, (LPCWSTR)tu);
        ShowWindow(h, cfg_.visible ? show : SW_HIDE);   // 숨김 상태로 시작하면 트레이 아이콘만 표시
        if (!owner_ && !cfg_.visible) TrayAdd();
        if (!owner_) RestoreCities();           // 저장된 도시 창을 모두 다시 열기
        return true;
    }

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

        CMD_BACKCOLOR   = 101,      // 메뉴 명령
        CMD_TIMEFONT    = 102,
        CMD_DATEFONT    = 103,
        CMD_EXIT        = 104,
        CMD_RESET       = 105,
        CMD_TOPMOST     = 106,
        CMD_ALLMON      = 107,
        CMD_ALLDESK     = 108,
        CMD_H24         = 109,      // 24시간제 / 12시간제 (연속 ID)
        CMD_H12         = 110,
        CMD_SHOWDATE    = 112,      // 년월일 보이기/숨기기 토글
        CMD_SHOWCLOCK   = 111,      // 시계 보이기/숨기기 토글 (트레이 메뉴에서도 사용)
        WM_TRAY         = WM_APP + 1,   // 트레이 아이콘 콜백
        CMD_OPACITY     = 200,      // 200 + (투명도/10) : 201~210
        CMD_CITY        = 300,      // 300 + 도시 인덱스
        CMD_MOVE        = 400,      // 400 + 이동 간격 선택지 인덱스
        CMD_REBOOT      = 600,      // 600 사용, 601 시각 (CMD_ALARM + 10*ALARM_ACTS 이후)
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
        wc.lpszClassName = "TextClock";
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
        case WM_LBUTTONDBLCLK: ToggleFrame();                    return 0;   // 더블클릭: 타이틀/메뉴바 숨김<->보임
        case WM_NCLBUTTONDBLCLK:
            if (hidden_) { ToggleFrame(); return 0; }            // 숨김 상태에서는 전체가 캡션 영역
            break;
        case WM_NCHITTEST:
            if (hidden_) return HTCAPTION;                       // 숨김 상태에서도 드래그로 이동 가능
            break;
        case WM_ENTERMENULOOP: modal_ = true;                    return 0;   // 메뉴/대화상자 중에는
        case WM_EXITMENULOOP:  modal_ = false;                   return 0;   // 창 자동 이동 멈춤
        case WM_DESTROY:    OnDestroy();                         return 0;
        }
        return DefWindowProcA(hwnd_, m, w, l);
    }

    // ---- 핸들러
    void OnCreate() {
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
        case TID_SCROLL_TIME:
            timeScroll_.Advance(ClientWidth());
            break;
        case TID_SCROLL_DATE:
            dateScroll_.Advance(ClientWidth());
            break;
        }
        InvalidateRect(hwnd_, NULL, FALSE);
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

        static const char* const WD[] = {"일", "월", "화", "수", "목", "금", "토"};
        SYSTEMTIME t;
        zone_.Now(t);                           // 도시 창: 그 도시의 현재 시각
        char buf[96];
        int n;

        // 시분초 (큰 글자)
        if (cfg_.hour12) {                      // 12시간제: 0시 -> 12 AM, 12시 -> 12 PM
            int h12 = t.wHour % 12;
            if (h12 == 0) h12 = 12;
            n = wsprintfA(buf, "%02d:%02d:%02d %s", h12, t.wMinute, t.wSecond, t.wHour < 12 ? "AM" : "PM");
        } else {
            n = wsprintfA(buf, "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
        }
        RECT r1 = rc;
        if (cfg_.showDate) r1.bottom = rc.bottom * 3 / 5;     // 년월일을 숨기면 시분초를 창 전체 가운데에 표시
        DrawLine(dc, font_, cfg_.timeColor, buf, n, r1, timeScroll_.Offset(),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 기준 도시 이름 + 년월일 (작은 글자)
        if (cfg_.showDate) {
        n = wsprintfA(buf, "%s %04d-%02d-%02d (%s)", city_, t.wYear, t.wMonth, t.wDay, WD[t.wDayOfWeek]);
        RECT r2 = rc;
        r2.top = rc.bottom * 3 / 5;
        DrawLine(dc, dateFont_, cfg_.dateColor, buf, n, r2,
                 dateScroll_.Offset(), DT_CENTER | DT_TOP | DT_SINGLELINE);
        }

        BitBlt(sdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, obm);
        DeleteObject(bm);                       // 그리자마자 해제 -> 상주 메모리 증가 없음
        DeleteDC(dc);
        EndPaint(hwnd_, &ps);
    }

    void OnDestroy() {
        KillTimer(hwnd_, TID_SECOND);
        KillTimer(hwnd_, TID_MOVE);
        KillTimer(hwnd_, TID_SCROLL_TIME);
        KillTimer(hwnd_, TID_SCROLL_DATE);
        if (owner_) {                           // 도시 창: 본 창의 목록에서 제외 (앱은 계속 실행)
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
        if (hidden_ && menu_) DestroyMenu(menu_);   // 창에서 떼어낸 메뉴는 직접 해제
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
        HMENU bar = CreateMenu();
        HMENU set = menuSet_ = CreatePopupMenu();
        menuOpacity_ = CreatePopupMenu();
        for (int p = 100; p >= 10; p -= 10) {
            char b[8];
            wsprintfA(b, "%d", p);
            lstrcatA(b, "%");
            AppendMenuA(menuOpacity_, MF_STRING, CMD_OPACITY + p / 10, b);
        }
        // (아래 menuMove_ 는 투명도 항목 바로 뒤에 추가됨)
        AddMenu(set, 0,        CMD_RESET,     "설정 초기화(&R)");
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        AddMenu(set, 0,        CMD_BACKCOLOR, "바탕색 변경(&B)...");
        AddMenu(set, MF_POPUP, (UINT_PTR)menuOpacity_, "투명도 변경(&O)");

        menuMove_ = CreatePopupMenu();          // 위치 이동 간격 (0 = 이동 안 함)
        static const char* const moveName[MOVE_COUNT] = {
            "0 (이동 안 함, 위치 고정)", "5초", "10초", "30초", "1분", "5분", "10분", "30분"};
        for (int i = 0; i < MOVE_COUNT; i++) AddMenu(menuMove_, 0, CMD_MOVE + i, moveName[i]);
        AddMenu(set, MF_POPUP, (UINT_PTR)menuMove_, "위치 이동 간격 변경(&I)");

        menuHour_ = CreatePopupMenu();          // 시각 표시 방식
        AddMenu(menuHour_, 0, CMD_H24, "24시간제 (24h)");
        AddMenu(menuHour_, 0, CMD_H12, "12시간제 (12h, AM/PM)");
        AddMenu(set, MF_POPUP, (UINT_PTR)menuHour_, "시각 표시 방식 24h / 12h(&H)");
        AddMenu(set, 0,        CMD_TIMEFONT,  "시분초 글꼴 변경(&T)...");
        AddMenu(set, 0,        CMD_DATEFONT,  "년월일 글꼴 변경(&D)...");
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        AddMenu(set, 0,        CMD_TOPMOST,   "항상 다른 앱보다 앞에 보이기(&A)");
        AddMenu(set, 0,        CMD_ALLMON,    "모든 모니터에 적용(&M)");
        AddMenu(set, 0,        CMD_ALLDESK,   "모든 가상 데스크톱에 적용(&V)");
        AddMenu(set, 0,        CMD_SHOWDATE,  "년월일 보이기(&Y)");
        AddMenu(set, 0,        CMD_SHOWCLOCK, "시계 보이기(&H)  (해제하면 트레이에서만 동작)");
        AppendMenuA(set, MF_SEPARATOR, 0, NULL);
        AddMenu(set, 0,        CMD_EXIT,      "프로그램 종료 (&Exit)");
        AddMenu(bar, MF_POPUP, (UINT_PTR)set, "설정(&S)");

        HMENU glob = menuGlobal_ = CreatePopupMenu();   // Global: 도시를 고르면 그 도시 시계 창이 하나 더 추가됨
        for (int i = 0; i < CITY_COUNT; i++) AddMenu(glob, 0, CMD_CITY + i, CITIES[i].name);
        AddMenu(bar, MF_POPUP, (UINT_PTR)glob, "Global(&G)");

        menuAlarm_ = CreatePopupMenu();         // 알람1 ~ 알람10 (각각 사용/시각/파일/지우기 서브메뉴)
        for (int i = 0; i < ALARM_COUNT; i++) {
            HMENU sub = menuAlarmSub_[i] = CreatePopupMenu();
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 0, "사용(&E)");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 1, "시각 설정(&T)...");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 2, "동영상/음악 파일 선택(&F)...");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 9, "메모 설정(&M)...");
            AppendMenuA(sub, MF_SEPARATOR, 0, NULL);
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 4, "플레이어 자동 닫기: 끔(&0)");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 5, "플레이어 자동 닫기: 재생이 끝나면(&1)");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 6, "플레이어 자동 닫기: 지정 시간 후(&2)");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 7, "닫기 시간 설정...(&S)");
            AppendMenuA(sub, MF_SEPARATOR, 0, NULL);
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 3, "알람 지우기(&C)");
            AddMenu(sub, 0, CMD_ALARM + i * ALARM_ACTS + 8, "알람 테스트(&R)");
            AppendMenuA(menuAlarm_, MF_POPUP | MF_STRING, (UINT_PTR)sub, "-");
            UpdateAlarmMenu(i);                 // 항목 이름: 알람N  시각  파일명
        }
        AppendMenuA(menuAlarm_, MF_SEPARATOR, 0, NULL);
        menuReboot_ = CreatePopupMenu();        // 시스템 재시작 시각 (알람 10개 + 구분선 다음, 위치 ALARM_COUNT+1)
        AddMenu(menuReboot_, 0, CMD_REBOOT,     "사용(&E)");
        AddMenu(menuReboot_, 0, CMD_REBOOT + 1, "시각 설정(&T)...");
        AppendMenuA(menuAlarm_, MF_POPUP | MF_STRING, (UINT_PTR)menuReboot_, "-");
        UpdateRebootMenu();
        AddMenu(bar, MF_POPUP, (UINT_PTR)menuAlarm_, "알람(&A)");
        return bar;
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
    void UpdateAlarmMenu(int i) {
        const Alarm& a = cfg_.alarms[i];
        const char* name = a.file;
        for (const char* p = a.file; *p; p++)
            if (*p == '\\' || *p == '/') name = p + 1;
        char nm[48];
        lstrcpynA(nm, name, sizeof(nm));
        char s[160];
        wsprintfA(s, "알람%d   %02d:%02d:%02d   %s", i + 1, a.h, a.m, a.s, a.file[0] ? nm : "(파일 없음)");
        char16_t u[160];
        Utf8::ToUtf16(s, -1, u, 160);
        ModifyMenuW(menuAlarm_, i, MF_BYPOSITION | MF_POPUP | MF_STRING,
                    (UINT_PTR)menuAlarmSub_[i], (LPCWSTR)u);
        UINT chk = a.on ? MF_CHECKED : MF_UNCHECKED;
        CheckMenuItem(menuAlarm_, i, MF_BYPOSITION | chk);
        HMENU sub = menuAlarmSub_[i];
        CheckMenuItem(sub, CMD_ALARM + i * ALARM_ACTS, MF_BYCOMMAND | chk);
        CheckMenuItem(sub, CMD_ALARM + i * ALARM_ACTS + 9, MF_BYCOMMAND | (a.memo[0] ? MF_CHECKED : MF_UNCHECKED));   // 체크 = 메모 있음
        CheckMenuRadioItem(sub, CMD_ALARM + i * ALARM_ACTS + 4, CMD_ALARM + i * ALARM_ACTS + 6,
                           CMD_ALARM + i * ALARM_ACTS + 4 + a.closeMode, MF_BYCOMMAND);
        wsprintfA(s, "닫기 시간 설정 (%02d:%02d:%02d)...(&S)", a.closeSec / 3600, a.closeSec % 3600 / 60, a.closeSec % 60);
        Utf8::ToUtf16(s, -1, u, 160);
        ModifyMenuW(sub, CMD_ALARM + i * ALARM_ACTS + 7, MF_BYCOMMAND | MF_STRING,
                    CMD_ALARM + i * ALARM_ACTS + 7, (LPCWSTR)u);
    }

    void AlarmCommand(int i, int act) {
        Alarm& a = cfg_.alarms[i];
        if (act == 8) {                         // 알람 테스트: 메모 표시 + 선택된 파일 1회 실행 (설정/사용 여부 변경 없음)
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
            if (!a.on && !a.file[0] && !a.memo[0] && !PickAlarmFile(a)) return;
            a.on = !a.on;
        } else if (act == 1) {                  // 시각 설정
            modal_ = true;
            bool ok = TimeDialog::Show(hi_, hwnd_, "알람 시각 (시:분:초)", a.h, a.m, a.s);
            modal_ = false;
            if (!ok) return;
        } else if (act == 2) {                  // 파일 선택
            if (!PickAlarmFile(a)) return;
        } else if (act == 3) {                  // 알람 지우기
            a.on = false;
            a.file[0] = 0;
            a.memo[0] = 0;
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
        for (int i = 0; i < ALARM_COUNT; i++) {
            const Alarm& a = cfg_.alarms[i];
            if (!a.on || (!a.file[0] && !a.memo[0]) || firedYmd_[i] == ymd) continue;
            int d = now - (a.h * 3600 + a.m * 60 + a.s);
            if (d < 0 || d > 5) continue;       // 타이머가 조금 늦어도 놓치지 않도록 5초 여유
            firedYmd_[i] = ymd;
            if (a.memo[0]) MemoWindow::Show(hi_, a.memo, 30);
            if (a.file[0]) LaunchAlarm(i);
        }
        if (cfg_.rbOn && rbFiredYmd_ != ymd) {  // 시스템 재시작 (재부팅 직후 재실행되어도 5초 여유가 지나 다시 실행되지 않음)
            int d = now - (cfg_.rbH * 3600 + cfg_.rbM * 60 + cfg_.rbS);
            if (d >= 0 && d <= 5) {
                rbFiredYmd_ = ymd;
                ConfirmReboot();
            }
        }
    }

    // 재시작 확인창: 300초 동안 Yes/No 대기(남은 시간 프로그레스 바). Yes 또는 무응답(시간 초과) = 즉시 재시작, No = 재시작 안 함
    void ConfirmReboot() {
        modal_ = true;
        int r = ConfirmDialog::Show(hi_, 300, "시스템 재시작", "시스템을 재시작 하겠습니까? (Yes/No)", "무응답 시 재시작", true);
        modal_ = false;
        if (r != IDNO)
            ShellExecuteW(NULL, (LPCWSTR)u"open", (LPCWSTR)u"shutdown.exe", (LPCWSTR)u"/r /f /t 0", NULL, SW_HIDE);
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
        for (int i = 0; i < ALARM_COUNT; i++) {
            Pending& p = pend_[i];
            if (!p.active) continue;
            DWORD el = GetTickCount() - p.launchTick;
            if (p.before) {
                if (el <= 8000) DetectWindows(p);
                else { free(p.before); p.before = NULL; p.nBefore = 0; }   // 탐지 종료
            }
            if (el >= p.closeAfterMs) {
                for (int k = 0; k < p.nWins; k++)
                    if (IsWindow(p.wins[k])) PostMessage(p.wins[k], WM_CLOSE, 0, 0);
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

    void CheckHourFormat() {
        if (!menuHour_) return;
        CheckMenuRadioItem(menuHour_, CMD_H24, CMD_H12, cfg_.hour12 ? CMD_H12 : CMD_H24, MF_BYCOMMAND);
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
        CheckMenuItem(menuSet_, CMD_SHOWDATE, MF_BYCOMMAND | (cfg_.showDate ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menuSet_, CMD_SHOWCLOCK, MF_BYCOMMAND | (cfg_.visible ? MF_CHECKED : MF_UNCHECKED));
    }

    void OnCommand(int id) {
        if (id == CMD_EXIT)            ConfirmExit();
        else if (id == CMD_SHOWCLOCK)  SetClockVisible(!cfg_.visible);
        else if (id == CMD_SHOWDATE) {
            cfg_.showDate = !cfg_.showDate;
            cfg_.WriteInt("Option", "ShowDate", cfg_.showDate);
            ResizeAll();                        // 숨기면 년월일 영역만큼 높이 축소
            ForAll(&ClockWindow::ApplyFormat);  // 모든 창 다시 그리기
            UpdateOptionChecks();
        }
        else if (id == CMD_RESET)      ResetSettings();
        else if (id == CMD_BACKCOLOR)  PickBackColor();
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
        else if (id == CMD_H24 || id == CMD_H12) {
            cfg_.hour12 = (id == CMD_H12);
            ForAll(&ClockWindow::ApplyFormat);
            CheckHourFormat();
            cfg_.WriteInt("Option", "Hour12", cfg_.hour12);
        }
        else if (id == CMD_REBOOT || id == CMD_REBOOT + 1)
            RebootCommand(id - CMD_REBOOT);
        else if (id >= CMD_ALARM && id < CMD_ALARM + ALARM_COUNT * ALARM_ACTS)
            AlarmCommand((id - CMD_ALARM) / ALARM_ACTS, (id - CMD_ALARM) % ALARM_ACTS);
        else if (id >= CMD_MOVE && id < CMD_MOVE + MOVE_COUNT) {
            cfg_.moveMs = MoveChoice(id - CMD_MOVE);
            RestartTimers();                    // 0 이면 타이머 중지 = 위치 고정
            CheckMoveInterval();
            cfg_.WriteInt("Timer", "MoveMs", cfg_.moveMs);
        }
    }

    // 클라이언트 높이: 년월일 표시 150, 숨김 90 (시분초 영역 = 3/5)
    int ClientH() const { return cfg_.showDate ? 150 : 90; }

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
            int h = (wr.bottom - wr.top) - cr.bottom + ClientH();      // 비클라이언트(캡션/메뉴) 높이는 유지
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
        wsprintfA(s, "Text Clock  %02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
        Utf8::ToUtf16(s, -1, (char16_t*)n.szTip, 64);
    }

    void TrayAdd() {
        if (trayOn_) return;
        NOTIFYICONDATAW n;
        TrayFill(n);
        n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        n.uCallbackMessage = WM_TRAY;
        n.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        trayOn_ = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
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
        int r = ConfirmDialog::Show(hi_, 30, "시계 종료", "시계 앱을 종료하겠습니까? (Yes/No)", "무응답 시 종료하지 않음", false);
        root->modal_ = false;
        asking = false;
        if (r == IDYES) RequestExit();
    }

    // 앱 종료: 도시 목록을 먼저 저장한 뒤 본 창을 닫음 (소유 창인 도시 창도 함께 닫힘)
    void RequestExit() {
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
        if (HasCity(idx)) return;               // 이미 표시 중인 도시는 건너뜀 (중복 방지)
        ClockWindow* e = new ClockWindow(cfg_, this, CITIES[idx].name, CITIES[idx].zone);

        RECT low;
        GetWindowRect(hwnd_, &low);
        for (ClockWindow* p = head_; p; p = p->next_) {
            RECT r;
            GetWindowRect(p->hwnd_, &r);
            if (r.bottom > low.bottom) low = r;
        }
        RECT c = {0, 0, 336, ClientH()};
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

    // ---- 타이틀바/메뉴바 숨김 <-> 보임 (글자가 그려지는 클라이언트 영역의 위치/크기는 유지)
    void ToggleFrame() {
        RECT c;
        GetClientRect(hwnd_, &c);
        POINT tl = {0, 0};
        ClientToScreen(hwnd_, &tl);
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        hidden_ = !hidden_;
        if (hidden_) {
            SetMenu(hwnd_, NULL);
            SetWindowLongPtrA(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd_, NULL, tl.x, tl.y, c.right, c.bottom, flags);
        } else {
            SetMenu(hwnd_, menu_);
            SetWindowLongPtrA(hwnd_, GWL_STYLE, NormalStyle() | WS_VISIBLE);
            RECT r = {0, 0, c.right, c.bottom};
            AdjustWindowRect(&r, NormalStyle(), menu_ != NULL);
            SetWindowPos(hwnd_, NULL, tl.x + r.left, tl.y + r.top,
                         r.right - r.left, r.bottom - r.top, flags);
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    // ---- 설정 초기화: 바탕색/투명도/폰트/타이머/옵션을 기본값으로 (창 위치, 기준 도시는 유지)
    void ResetSettings() {
        char16_t msg[96], cap[32];
        Utf8::ToUtf16("모든 설정을 기본값으로 되돌릴까요?\n(창 위치는 유지됩니다)", -1, msg, 96);
        Utf8::ToUtf16("설정 초기화", -1, cap, 32);
        modal_ = true;
        int ans = MessageBoxW(hwnd_, (LPCWSTR)msg, (LPCWSTR)cap,
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        modal_ = false;
        if (ans != IDYES) return;

        if (cfg_.allDesk) DesktopPin::Set(false);
        cfg_.ResetDefaults();
        cfg_.SaveAll();
        ForAll(&ClockWindow::RestartTimers);
        ForAll(&ClockWindow::ApplyStyle);
        CheckOpacity();
        CheckMoveInterval();
        CheckHourFormat();
        UpdateOptionChecks();
    }

    // 모든 가상 데스크톱에 표시 On/Off (실패하면 상태를 바꾸지 않고 알림)
    void ToggleAllDesktops() {
        bool want = !cfg_.allDesk;
        if (!DesktopPin::Set(want)) {
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
        ForAll(&ClockWindow::ApplyStyle);
        cfg_.SaveStyle();
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
        if (cfg_.hour12) GetTextExtentPoint32A(dc, "00:00:00 PM", 11, &sz);
        else             GetTextExtentPoint32A(dc, "00:00:00", 8, &sz);
        timeScroll_.SetTextWidth(sz.cx);

        SelectObject(dc, dateFont_);
        char s[96];
        char16_t u[96];
        wsprintfA(s, "%s 0000-00-00 (수)", city_);
        int n = Utf8::ToUtf16(s, -1, u, 96) - 1;
        GetTextExtentPoint32W(dc, (LPCWSTR)u, n, &sz);
        dateScroll_.SetTextWidth(sz.cx);

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
        if (hidden_) {                          // 숨김 상태면 보임 상태의 창 좌표로 환산해서 저장
            RECT r = {0, 0, wr.right - wr.left, wr.bottom - wr.top};
            AdjustWindowRect(&r, NormalStyle(), menu_ != NULL);
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
    HMENU        menu_         = NULL;  // 본 창만
    HMENU        menuSet_      = NULL;
    HMENU        menuOpacity_  = NULL;
    HMENU        menuMove_     = NULL;
    HMENU        menuGlobal_   = NULL;
    HMENU        menuHour_     = NULL;
    HMENU        menuAlarm_    = NULL;
    HMENU        menuReboot_   = NULL;
    int          rbFiredYmd_   = 0;     // 시스템 재시작을 마지막으로 실행한 날짜
    HMENU        menuAlarmSub_[ALARM_COUNT] = {};
    int          firedYmd_[ALARM_COUNT]     = {};   // 알람이 마지막으로 울린 날짜 (YYYYMMDD)
    Pending      pend_[ALARM_COUNT];                // 자동 닫기 대기 중인 플레이어 창 정보
    bool         quitting_     = false; // 본 창: 앱 종료 중 (도시 창 닫힘을 목록 변경으로 저장하지 않음)
    bool         trayOn_       = false; // 트레이 아이콘 표시 중 (본 창)
    bool         hidden_       = false; // 타이틀바/메뉴바 숨김 상태
    bool         modal_        = false; // 메뉴/대화상자 사용 중 (자동 이동 일시 정지)
    ClockWindow* head_         = NULL;  // 본 창: 도시 창 목록
    ClockWindow* next_         = NULL;
    Scroller     timeScroll_;           // 시분초: 0.1초마다 2px
    Scroller     dateScroll_;           // 년월일: 0.2초마다 1px
};

// ---------------------------------------------------------------- WinMain
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    SingleInstance guard("TextClock_U8_SingleInstance");   // 중복 실행 방지
    if (!guard.IsFirst()) return 0;

    Settings cfg;
    cfg.Init();                                 // ini 읽기 (없으면 주석 포함 기본 파일 생성)

    DesktopPin::SetProcessAppId();              // 가상 데스크톱 고정에 쓰는 AppID (창 생성 전에 지정)
    ClockWindow win(cfg);
    if (!win.Create(hi, show, CW_USEDEFAULT, CW_USEDEFAULT)) return 1;
    return win.Run();
}
