// screen_telegram.cpp
// Windows 11 / MinGW-w64 g++
//
// booting_Alert.ini 의 설정:
//   TELEGRAM_BOT_TOKEN=xxxxxxxxxx
//   TELEGRAM_CHAT_ID=yyyyyyy
//   TELEGRAM_INTERVAL=60
//   MONITOR1=TRUE          (모니터1 캡처 여부, 미기재 시 기본 TRUE)
//   MONITOR2=TRUE
//   MONITOR3=FALSE
//   CAMERA=TRUE            (웹캠 캡처 여부, 기본 FALSE)
//   CAMERA_INDEX=0         (사용할 카메라 번호, 0부터. 기본 0. 목록은 로그에 출력)
//   MAX_LOOP=9             (캡처/전송 반복 횟수. 이 횟수만큼 실행 후 종료. 0 또는 미기재 시 무제한)
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
//   Config          booting_Alert.ini 설정
//   ImageEncoder    GDI+ 세션 + PNG/JPEG 인코딩
//   TelegramClient  텔레그램 sendPhoto (WinHTTP) + 재시도
//   Shot            캡처 결과 1장 (라벨 + 비트맵)
//   ICaptureSource  캡처 소스 인터페이스
//   MonitorCapture  모니터 화면 캡처 (ICaptureSource)
//   CameraCapture   웹캠 캡처, Media Foundation (ICaptureSource)
//   ScreenTelegramApp  전체 흐름 (설정 로드 -> 주기 반복 캡처/전송)
//
// 빌드:
//   g++ -O2 -std=c++17 -o screen_telegram.exe screen_telegram.cpp ^
//       -lgdiplus -lgdi32 -luser32 -lole32 -loleaut32 -lwinhttp -lmfuuid ^
//       -static -static-libgcc -static-libstdc++
//   (콘솔 창 없이 실행하려면 -mwindows 추가)
//
// 주의: mfplat.dll / mfreadwrite.dll (카메라용) 은 빌드 시점에 링크하지 않고,
// 실행 중 LoadLibrary 로 동적으로 찾는다. 해당 DLL이 없는 환경(Windows N/KN 등)
// 에서도 exe 자체는 정상적으로 시작되며, 카메라 기능만 자동으로 비활성화된다.
// -lmfuuid 는 GUID 상수만 담긴 순수 정적 라이브러리이므로 DLL 의존성이 없다.
//
// 실행:
//   screen_telegram.exe [ini파일경로]
//   (인자가 없으면 exe 폴더 -> 현재 폴더의 booting_Alert.ini 를 찾음)

#include <windows.h>
#include <objidl.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <winhttp.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 복사 금지 선언용 매크로
#define NO_COPY(T)               \
    T(const T&) = delete;        \
    T& operator=(const T&) = delete

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
        DWORD a = GetFileAttributesA(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    static std::string exeDir()
    {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf, n);
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

    // 해당 모니터(1부터 시작하는 순번)가 캡처 대상인지 확인. 미지정 시 기본 true.
    bool isMonitorEnabled(int oneBasedIndex) const
    {
        auto it = monitorEnabled.find(oneBasedIndex);
        return it == monitorEnabled.end() ? true : it->second;
    }

    // INI 파일을 읽는다. 파일을 열 수 없으면 false.
    bool load(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

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

    bool encode(HBITMAP hBmp, bool jpeg, std::vector<BYTE>& out) const
    {
        out.clear();
        CLSID clsid;
        if (!getEncoderClsid(jpeg ? L"image/jpeg" : L"image/png", &clsid))
            return false;

        bool ok = false;
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;

        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
            Gdiplus::Status st;
            if (jpeg) {
                ULONG quality = 85;
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

    // JPEG(품질 85) 우선, 실패하거나 9.5MB 초과 시 PNG 시도 (텔레그램 사진 한도 10MB)
    bool encodeForTelegram(HBITMAP hBmp, std::vector<BYTE>& out, bool& jpeg) const
    {
        jpeg = true;
        if (encode(hBmp, true, out) && out.size() <= 9500000) return true;
        jpeg = false;
        return encode(hBmp, false, out);
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
    bool sendPhoto(const std::string& caption, const std::vector<BYTE>& img,
                   bool jpeg, std::string& resp) const
    {
        resp.clear();
        const std::string boundary = "----ScreenTelegramBoundary7MA4YWxkTrZu0gW";

        std::string body;
        body.reserve(img.size() + 1024);
        addField(body, boundary, "chat_id", chatId_);
        addField(body, boundary, "caption", caption);

        body += "--" + boundary + "\r\n";
        body += std::string("Content-Disposition: form-data; name=\"photo\"; filename=\"screen") +
                (jpeg ? ".jpg" : ".png") + "\"\r\n";
        body += std::string("Content-Type: ") + (jpeg ? "image/jpeg" : "image/png") + "\r\n\r\n";
        body.append((const char*)img.data(), img.size());
        body += "\r\n--" + boundary + "--\r\n";

        HINTERNET hSession = WinHttpOpen(L"ScreenTelegram/1.0",
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 10000, 10000, 60000, 120000);

        bool ok = false;
        HINTERNET hConn = WinHttpConnect(hSession, L"api.telegram.org",
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET hReq = nullptr;
        if (hConn) {
            std::wstring path = L"/bot" + StrUtil::toWide(token_) + L"/sendPhoto";
            hReq = WinHttpOpenRequest(hConn, L"POST", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE);
        }
        if (hReq) {
            std::wstring hdr = L"Content-Type: multipart/form-data; boundary=" +
                               StrUtil::toWide(boundary);
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
        }
        if (hReq) WinHttpCloseHandle(hReq);
        if (hConn) WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
        return ok;
    }

    // 전송 실패 시 재시도 (네트워크 오류, 5xx, 429 retry_after). 최대 3회.
    bool sendPhotoWithRetry(const std::string& caption, const std::vector<BYTE>& img,
                            bool jpeg, std::string& resp) const
    {
        const int maxTry = 3;
        for (int attempt = 1; ; ++attempt) {
            if (sendPhoto(caption, img, jpeg, resp)) return true;
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

// ================================================================ 캡처 소스
// 캡처 결과 1장: 라벨(예: "Monitor 1/2 (1920x1080)", "Camera") + 비트맵
struct Shot {
    std::string label;
    ScopedBitmap bmp;

    Shot(std::string l, HBITMAP h) : label(std::move(l)), bmp(h) {}
    Shot(Shot&&) noexcept = default;
    Shot& operator=(Shot&&) noexcept = default;
};

// 캡처 소스 인터페이스. 캡처한 이미지를 out 에 추가한다. (실패 시 로그만 남기고 추가 안 함)
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual void capture(std::vector<Shot>& out) = 0;
};

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
            out.emplace_back(label, hBmp);
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
};

// ---------------------------------------------------------------- CameraCapture
// Media Foundation 함수를 런타임에 동적으로 로드한다.
// MF 함수를 직접(암시적 링크) 호출하면, mfplat.dll / mfreadwrite.dll 을 로드하지
// 못하는 환경(Windows N/KN 등)에서 exe 가 "시작조차" 못 한다. 이를 막기 위해
// LoadLibrary/GetProcAddress 로 실행 중에만 찾고, 실패하면 카메라만 비활성화한다.
class CameraCapture : public ICaptureSource {
public:
    explicit CameraCapture(const Config& cfg) : cfg_(cfg) {}
    ~CameraCapture() override
    {
        if (started_ && api_.pMFShutdown) api_.pMFShutdown();
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

    void capture(std::vector<Shot>& out) override
    {
        std::string err;
        HBITMAP h = captureFrame(err);
        if (!h) {
            Logger::log("카메라 캡처 실패: %s", err.c_str());
            return;
        }
        out.emplace_back("Camera", h);
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

    // cfg_.cameraIndex 번째 웹캠에서 한 프레임을 캡처 (실패 시 nullptr)
    HBITMAP captureFrame(std::string& errMsg)
    {
        if (!loadApi(errMsg))
            return nullptr;

        HBITMAP result = nullptr;
        IMFAttributes* pAttr = nullptr;
        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        IMFMediaSource* pSource = nullptr;
        IMFSourceReader* pReader = nullptr;
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
            hr = ppDevices[idx]->ActivateObject(IID_PPV_ARGS(&pSource));
        }

        // 리더가 카메라의 원본 포맷(MJPEG/YUY2/NV12 등)을 RGB32 로 자동
        // 변환할 수 있도록 허용한다. 이게 없으면 원본 포맷과 RGB32 가 바로
        // 맞지 않는 경우 MF_E_INVALIDMEDIATYPE(0xC00D36B4) 로 실패한다.
        IMFAttributes* pReaderAttr = nullptr;
        if (SUCCEEDED(hr))
            hr = api_.pMFCreateAttributes(&pReaderAttr, 1);
        if (SUCCEEDED(hr))
            hr = pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        if (SUCCEEDED(hr))
            hr = api_.pMFCreateSourceReaderFromMediaSource(pSource, pReaderAttr, &pReader);
        if (pReaderAttr) pReaderAttr->Release();

        // 출력 포맷을 RGB32(BGRA) 로 강제 설정
        if (SUCCEEDED(hr))
            hr = api_.pMFCreateMediaType(&pType);
        if (SUCCEEDED(hr))
            hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr))
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr))
            hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr, pType);

        UINT32 w = 0, h = 0;
        bool bottomUp = true; // MF RGB 규약: stride 가 음수이면 bottom-up (속성 없을 때 기본)
        if (SUCCEEDED(hr)) {
            IMFMediaType* pCur = nullptr;
            hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCur);
            if (SUCCEEDED(hr)) {
                MFGetAttributeSize(pCur, MF_MT_FRAME_SIZE, &w, &h);
                UINT32 ds = 0;
                if (SUCCEEDED(pCur->GetUINT32(MF_MT_DEFAULT_STRIDE, &ds)))
                    bottomUp = ((LONG)ds < 0);
                pCur->Release();
            }
        }

        if (SUCCEEDED(hr) && (w == 0 || h == 0)) {
            errMsg = "카메라 프레임 크기를 확인할 수 없습니다.";
            hr = E_FAIL;
        }

        // 자동 노출/화이트밸런스 안정화를 위해 몇 프레임 버린 뒤 캡처
        if (SUCCEEDED(hr)) {
            const int warmupFrames = 5;
            for (int attempt = 0; attempt < warmupFrames + 3 && SUCCEEDED(hr); ++attempt) {
                DWORD flags = 0;
                IMFSample* pSample = nullptr;
                hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
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
                        LONG stride = (LONG)(curLen / h);
                        if (stride < (LONG)w * 4) stride = (LONG)w * 4;
                        result = frameToBitmap(data, stride, w, h, bottomUp);
                        pBuf->Unlock();
                    }
                    pBuf->Release();
                }
                pSample->Release();
                break;
            }
        }

        if (!result && errMsg.empty()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "카메라 캡처 실패 (0x%08lX)", (unsigned long)hr);
            errMsg = buf;
        }

        if (pType) pType->Release();
        if (pReader) pReader->Release();
        if (pSource) { pSource->Shutdown(); pSource->Release(); }
        if (ppDevices) {
            for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
            CoTaskMemFree(ppDevices);
        }
        if (pAttr) pAttr->Release();

        return result;
    }

    const Config& cfg_;
    Api api_;
    bool started_ = false;
    bool listed_ = false;
};

// ================================================================ ScreenTelegramApp
class ScreenTelegramApp {
public:
    int run(int argc, char** argv)
    {
        // 고DPI 환경에서도 실제 해상도로 캡처
        SetProcessDPIAware();

        const std::string ini = resolveIniPath(argc, argv);
        if (!cfg_.load(ini)) {
            Logger::log("INI 파일을 열 수 없습니다: %s", ini.c_str());
            return 1;
        }
        if (cfg_.token.empty() || cfg_.chatId.empty()) {
            Logger::log("TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID 가 INI 에 없습니다.");
            return 1;
        }
        Logger::log("설정 로드: %s (간격 %d초, 카메라 %s, 반복 %s)", ini.c_str(), cfg_.interval,
                    cfg_.cameraEnabled ? "사용" : "사용안함",
                    cfg_.maxLoop > 0 ? std::to_string(cfg_.maxLoop).c_str() : "무제한");
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

        std::vector<std::unique_ptr<ICaptureSource>> sources;
        sources.push_back(std::make_unique<MonitorCapture>(cfg_));
        if (cfg_.cameraEnabled) {
            auto cam = std::make_unique<CameraCapture>(cfg_);
            if (cam->start()) sources.push_back(std::move(cam)); // 실패해도 앱은 계속 실행
        }

        stop_.install();

        // 실행 간격 유지 (처리 시간만큼 보정). 종료 요청이 오면 대기 중에도 즉시 탈출.
        int loopCount = 0;
        while (!stop_.requested()) {
            ULONGLONG start = GetTickCount64();

            cycle(sources, encoder, telegram);

            // MAX_LOOP 횟수만큼 실행했으면 마지막 대기 없이 바로 종료
            ++loopCount;
            if (cfg_.maxLoop > 0 && loopCount >= cfg_.maxLoop) {
                Logger::log("MAX_LOOP=%d 회 완료", cfg_.maxLoop);
                break;
            }

            ULONGLONG elapsed = GetTickCount64() - start;
            ULONGLONG period = (ULONGLONG)cfg_.interval * 1000ULL;
            DWORD wait = elapsed < period ? (DWORD)(period - elapsed) : 0;
            if (stop_.wait(wait)) break;
        }
        Logger::log("종료합니다.");
        return 0;
    }

private:
    static std::string resolveIniPath(int argc, char** argv)
    {
        if (argc >= 2) return argv[1];
        std::string p1 = SysUtil::exeDir() + "\\booting_Alert.ini";
        return SysUtil::fileExists(p1) ? p1 : "booting_Alert.ini";
    }

    // 1회 주기: 모든 소스 캡처 -> 인코딩 -> 텔레그램 전송
    void cycle(std::vector<std::unique_ptr<ICaptureSource>>& sources,
               const ImageEncoder& encoder, const TelegramClient& telegram)
    {
        const std::string host = SysUtil::hostName();
        const std::string ts = SysUtil::timestamp();

        std::vector<Shot> shots;
        for (auto& src : sources)
            src->capture(shots);

        for (auto& shot : shots) {
            if (stop_.requested()) break;

            std::vector<BYTE> img;
            bool jpeg = false;
            bool enc = encoder.encodeForTelegram(shot.bmp.get(), img, jpeg);
            shot.bmp.reset();

            if (!enc) {
                Logger::log("%s 이미지 인코딩 실패", shot.label.c_str());
                continue;
            }

            const std::string caption = "[" + host + "] " + shot.label + " " + ts;
            std::string resp;
            if (telegram.sendPhotoWithRetry(caption, img, jpeg, resp))
                Logger::log("%s 전송 성공 (%zu KB, %s)", shot.label.c_str(),
                            img.size() / 1024, jpeg ? "JPEG" : "PNG");
            else
                Logger::log("%s 전송 실패: %s", shot.label.c_str(), resp.c_str());
        }
    }

    Config cfg_;
    StopSignal stop_;
};

// ================================================================ main
int main(int argc, char** argv)
{
    ScreenTelegramApp app;
    return app.run(argc, argv);
}
