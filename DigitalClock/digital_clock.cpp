// digital_clock.cpp  (UTF-8 소스 / UTF-8 콘솔 출력)
// CLI 디지털 시계 (1초 단위 갱신)
//   - 표시 형식을 명령줄 인수로 지정 가능 (기본: "%Y/%m/%d %I:%M:%S %p %A")
//   - 첫째 줄: 시계 (1초마다 갱신)            ← 쓰레드 1
//   - 둘째 줄: 화살표가 윗줄과 같은 폭 안에서 0.5초에 한 칸씩 좌우로 핑퐁   ← 쓰레드 2
//                 오른쪽으로 갈 때 "-->", 왼쪽으로 갈 때 "<--"
//
// 시간 계산은 C++ 표준(<chrono>, <thread>, <ctime>)만 사용합니다.
// Windows API 는 콘솔 설정과 인수(UTF-8) 변환에만 쓰며 _WIN32 에서만 컴파일됩니다.
//
// 이 파일은 반드시 UTF-8 로 저장하세요.
//
// 빌드 (Visual Studio Developer Command Prompt):
//   cl /EHsc /std:c++17 /utf-8 digital_clock.cpp
// 빌드 (MinGW-w64, 기본이 UTF-8, posix 스레드 모델):
//   g++ -std=c++17 -pthread -o digital_clock.exe digital_clock.cpp
// 빌드 (Linux/macOS):
//   g++ -std=c++17 -pthread -o digital_clock digital_clock.cpp
//
// 사용:  digital_clock [-f FORMAT]      (도움말: -h)
// 종료:  Ctrl+C

#define _CRT_SECURE_NO_WARNINGS   // MSVC 에서 std::localtime 경고 방지 (반드시 include 보다 먼저)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#endif

static const char* kDefaultFormat = "%Y/%m/%d %I:%M:%S %p %A";

// 일(0) ~ 토(6)  (std::tm::tm_wday 와 동일)
static const char* kWeekdayLong[7]  = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
static const char* kWeekdayShort[7] = {"일", "월", "화", "수", "목", "금", "토"};

// ---------------------------------------------------------------
// 콘솔 설정 (표준 C++ 에는 없는 부분: UTF-8 출력, ANSI 활성화)
// ---------------------------------------------------------------
#ifdef _WIN32
static UINT g_oldCP = 0;
#endif

static void InitConsole()
{
#ifdef _WIN32
    g_oldCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

static void RestoreCodePage()
{
#ifdef _WIN32
    if (g_oldCP) SetConsoleOutputCP(g_oldCP);
#endif
}

// 종료 요청 플래그 (Ctrl+C 로 설정, 모든 쓰레드가 확인)
static std::atomic<bool> g_quit{false};

static void OnSignal(int)
{
    g_quit = true;   // 시그널 핸들러에서는 플래그만 설정, 정리는 main 에서
}

// ---------------------------------------------------------------
// 명령줄 인수 (Windows 는 argv 가 ANSI 코드페이지라서 UTF-8 로 다시 얻음)
// ---------------------------------------------------------------
static std::vector<std::string> GetArgs(int argc, char** argv)
{
    std::vector<std::string> args;
#ifdef _WIN32
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (w) {
        for (int i = 0; i < n; ++i) {
            int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 1 ? len - 1 : 0, '\0');
            if (len > 1)
                WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            args.push_back(s);
        }
        LocalFree(w);
        return args;
    }
#endif
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    return args;
}

static void PrintUsage()
{
    std::printf(
        "사용법: digital_clock [-f FORMAT]\n"
        "\n"
        "  -f, --format FORMAT   표시 형식 (기본: \"%s\")\n"
        "  -h, --help            도움말\n"
        "\n"
        "형식 문자:\n"
        "  %%Y  연도 4자리      %%y  연도 2자리\n"
        "  %%m  월 (01-12)      %%d  일 (01-31)\n"
        "  %%H  시 (00-23)      %%I  시 (01-12)\n"
        "  %%M  분 (00-59)      %%S  초 (00-59)\n"
        "  %%p  am / pm         %%A  요일 (화요일)\n"
        "  %%a  요일 (화)       %%%%  %% 문자\n"
        "\n"
        "예)\n"
        "  digital_clock -f \"%%H:%%M:%%S\"\n"
        "  digital_clock -f \"%%Y-%%m-%%d (%%a) %%I:%%M:%%S %%p\"\n"
        "  digital_clock -f \"%%Y년 %%m월 %%d일 %%A\"\n",
        kDefaultFormat);
}

// ---------------------------------------------------------------
// 형식 문자열 -> 시간 문자열
// (std::put_time/strftime 은 %p, %A 가 로케일 의존(영문 AM/PM, 요일)이라
//  요청하신 "am/pm", "화요일" 을 위해 직접 처리)
// ---------------------------------------------------------------
static std::string FormatTime(const std::string& fmt, const std::tm& tm)
{
    std::string out;
    char buf[16];

    int hour12 = tm.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    for (size_t i = 0; i < fmt.size(); ++i) {
        const char c = fmt[i];
        if (c != '%' || i + 1 >= fmt.size()) {
            out += c;
            continue;
        }
        const char t = fmt[++i];
        switch (t) {
            case 'Y': std::snprintf(buf, sizeof(buf), "%04d", tm.tm_year + 1900);       out += buf; break;
            case 'y': std::snprintf(buf, sizeof(buf), "%02d", (tm.tm_year + 1900) % 100); out += buf; break;
            case 'm': std::snprintf(buf, sizeof(buf), "%02d", tm.tm_mon + 1);           out += buf; break;
            case 'd': std::snprintf(buf, sizeof(buf), "%02d", tm.tm_mday);              out += buf; break;
            case 'H': std::snprintf(buf, sizeof(buf), "%02d", tm.tm_hour);             out += buf; break;
            case 'I': std::snprintf(buf, sizeof(buf), "%02d", hour12);                  out += buf; break;
            case 'M': std::snprintf(buf, sizeof(buf), "%02d", tm.tm_min);               out += buf; break;
            case 'S': std::snprintf(buf, sizeof(buf), "%02d", tm.tm_sec);               out += buf; break;
            case 'p': out += (tm.tm_hour < 12) ? "am" : "pm";                           break;
            case 'A': out += kWeekdayLong[tm.tm_wday];                                  break;
            case 'a': out += kWeekdayShort[tm.tm_wday];                                 break;
            case '%': out += '%';                                                       break;
            default:  out += '%'; out += t;                                             break; // 모르는 문자는 그대로
        }
    }
    return out;
}

// ---------------------------------------------------------------
// UTF-8 문자열의 화면 폭 (한글/한자/전각 = 2칸, 그 외 = 1칸)
// ---------------------------------------------------------------
static int DisplayWidth(const std::string& s)
{
    int width = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        size_t len = 1;
        if      ((c >> 5) == 0x6)  { len = 2; cp = c & 0x1F; }
        else if ((c >> 4) == 0xE)  { len = 3; cp = c & 0x0F; }
        else if ((c >> 3) == 0x1E) { len = 4; cp = c & 0x07; }
        for (size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;

        const bool wide =
            (cp >= 0x1100  && cp <= 0x115F)  ||
            (cp >= 0x2E80  && cp <= 0xA4CF)  ||
            (cp >= 0xAC00  && cp <= 0xD7A3)  ||
            (cp >= 0xF900  && cp <= 0xFAFF)  ||
            (cp >= 0xFE30  && cp <= 0xFE6F)  ||
            (cp >= 0xFF00  && cp <= 0xFF60)  ||
            (cp >= 0xFFE0  && cp <= 0xFFE6)  ||
            (cp >= 0x1F300 && cp <= 0x1F64F) ||
            (cp >= 0x1F900 && cp <= 0x1F9FF);
        width += wide ? 2 : 1;
    }
    return width;
}

// 한 틱에 한 칸씩 0 -> N-1 -> 0 ... 핑퐁하는 위치 (epoch 기준 틱이라 상태 불필요)
//   count      : 움직일 수 있는 위치의 개수 (N)
//   goingRight : 현재 오른쪽으로 가는 중이면 true
static int PingPongPos(long long tick, int count, bool& goingRight)
{
    if (count < 2) { goingRight = true; return 0; }
    const int period = 2 * (count - 1);
    const int p = static_cast<int>(tick % period);
    goingRight = (p >= 1 && p < count);   // 양 끝에서는 벽 쪽을 가리킴 (왼쪽 끝 "<--", 오른쪽 끝 "-->")
    return (p < count) ? p : period - p;
}

// ---------------------------------------------------------------
// 두 쓰레드가 공유하는 상태
// ---------------------------------------------------------------
using Clock = std::chrono::system_clock;

static std::mutex       g_outMutex;   // 콘솔 출력 보호 (두 쓰레드가 같은 화면에 씀)
static std::atomic<int> g_width{1};   // 첫째 줄의 화면 폭 -> 둘째 줄이 사용

// 지정 시각까지 대기. 종료 요청이 오면 바로 깨어남 (최대 50ms 단위로 확인)
template <class TimePoint>
static void SleepUntil(const TimePoint& tp)
{
    const std::chrono::milliseconds slice(50);
    while (!g_quit) {
        const auto remain = tp - Clock::now();
        if (remain <= std::chrono::milliseconds(0)) return;
        if (remain > slice) std::this_thread::sleep_for(slice);
        else { std::this_thread::sleep_until(tp); return; }
    }
}

// ---------------------------------------------------------------
// 쓰레드 1: 첫째 줄 - 시계 (1초마다)
// 커서는 항상 첫째 줄에 두므로 '\r' 만으로 덮어씀
// ---------------------------------------------------------------
static void ClockThread(const std::string& format)
{
    while (!g_quit) {
        const auto now = Clock::now();

        // 현재 시각 -> 로컬 시간 (std::tm)
        // (std::localtime 은 쓰레드 안전하지 않지만 이 쓰레드에서만 호출함)
        const std::time_t t = Clock::to_time_t(now);
        const std::tm* lt = std::localtime(&t);
        if (!lt) break;
        const std::tm tm = *lt;

        const std::string line = FormatTime(format, tm);
        g_width = std::max(1, DisplayWidth(line));

        {
            std::lock_guard<std::mutex> lock(g_outMutex);
            std::printf("\r%s   ", line.c_str());
            std::fflush(stdout);
        }

        // 다음 "초 경계"까지 정확히 대기 (시간이 밀리지 않음)
        SleepUntil(std::chrono::time_point_cast<std::chrono::seconds>(now) + std::chrono::seconds(1));
    }
}

// ---------------------------------------------------------------
// 쓰레드 2: 둘째 줄 - 화살표 핑퐁 (0.5초마다 한 칸): 오른쪽 "-->", 왼쪽 "<--"
// 한 줄 아래로 내려가 그린 뒤 다시 첫째 줄로 올라옴
// ---------------------------------------------------------------
static void BarThread()
{
    using namespace std::chrono;
    const long long kStepMs = 500;

    while (!g_quit) {
        const long long ms   = duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
        const long long tick = ms / kStepMs;          // 0.5초 단위 틱

        const int width = g_width.load();

        // 화살표(3칸)가 윗줄 폭 안에 들어가도록 이동 범위를 제한
        const int arrowLen = 3;
        const int count    = std::max(1, width - arrowLen + 1);

        bool goingRight = true;
        const int pos = PingPongPos(tick, count, goingRight);

        const char* arrow = goingRight ? "-->" : "<--";
        const std::string bar = std::string(pos, ' ') + arrow +
                                std::string(std::max(0, width - arrowLen - pos), ' ');

        {
            std::lock_guard<std::mutex> lock(g_outMutex);
            std::printf("\x1b[B\r%s   \x1b[A\r", bar.c_str());   // 아래로 -> 그리기 -> 위로
            std::fflush(stdout);
        }

        // 다음 0.5초 경계까지 정확히 대기
        SleepUntil(time_point<Clock, milliseconds>(milliseconds((tick + 1) * kStepMs)));
    }
}

int main(int argc, char** argv)
{
    InitConsole();

    // ---- 인수 처리 ----
    std::string format = kDefaultFormat;
    const std::vector<std::string> args = GetArgs(argc, argv);
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help" || a == "/?") {
            PrintUsage();
            RestoreCodePage();
            return 0;
        } else if (a == "-f" || a == "--format") {
            if (i + 1 >= args.size()) {
                std::printf("오류: %s 뒤에 형식 문자열이 필요합니다.\n\n", a.c_str());
                PrintUsage();
                RestoreCodePage();
                return 1;
            }
            format = args[++i];
        } else if (a.rfind("--format=", 0) == 0) {
            format = a.substr(9);
        } else {
            std::printf("오류: 알 수 없는 인수 '%s'\n\n", a.c_str());
            PrintUsage();
            RestoreCodePage();
            return 1;
        }
    }

    std::signal(SIGINT, OnSignal);   // Ctrl+C
    std::signal(SIGTERM, OnSignal);

    // 커서 숨김 + 둘째 줄 자리 확보(화면 맨 아래여도 스크롤되도록 줄바꿈 후 복귀)
    std::printf("\x1b[?25l\n\x1b[A");
    std::fflush(stdout);

    // 둘째 줄이 첫 프레임부터 올바른 폭을 쓰도록 첫째 줄 폭을 미리 계산
    {
        const std::time_t t0 = Clock::to_time_t(Clock::now());
        if (const std::tm* lt0 = std::localtime(&t0))
            g_width = std::max(1, DisplayWidth(FormatTime(format, *lt0)));
    }

    // 두 줄을 각각 별개의 쓰레드로 실행
    std::thread clockThread(ClockThread, std::cref(format));
    std::thread barThread(BarThread);

    // 메인 쓰레드는 종료 요청(Ctrl+C)만 기다림
    while (!g_quit)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    clockThread.join();
    barThread.join();

    std::printf("\x1b[?25h\n\n");   // 커서 표시 + 둘째 줄 아래로 이동
    std::fflush(stdout);
    RestoreCodePage();
    return 0;
}
