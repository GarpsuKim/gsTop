// IP Webcam (Android) MJPEG viewer for Windows 11 - single file, no external libs
// Build: g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static
// Run  : ipcam.exe 192.168.0.10:8080      (log file: ipcam.log next to the exe)
//
// Class overview
//   CritSec / ScopedLock  - thin Win32 critical-section wrappers
//   Logger                - thread-safe file logger (singleton)
//   Config                - target host/port/path parsed from the command line
//   WinsockSession        - RAII WSAStartup/WSACleanup
//   GdiplusSession        - RAII GdiplusStartup/GdiplusShutdown
//   JpegDecoder           - JPEG bytes -> Gdiplus::Bitmap
//   FrameStore            - latest frame + status text + fps counters (thread-safe)
//   MjpegClient           - network thread: connect, read MJPEG, decode, publish
//   ViewerWindow          - Win32 window that paints the FrameStore
//   App                   - wires everything together

//   폰 앱 [ DroidCam Webcam & OBS Camera ]  설치 주소 
//   https://play.google.com/store/apps/details?id=com.dev47apps.obsdroidcam

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
using std::min; using std::max;
#include <gdiplus.h>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>

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

// ============================================================ Logger
class Logger {
public:
    static Logger& Instance() { static Logger inst; return inst; }

    bool Open(const std::string& path) {
        ScopedLock lk(cs_);
        fp_ = fopen(path.c_str(), "w");
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
#define LOG(...) Logger::Instance().Write(__VA_ARGS__)

// ============================================================ Config
struct Config {
    std::string host = "192.168.0.70";
    std::string port = "4747";
    std::string path = "/video";

    static Config FromArgs(int argc, char** argv) {
        Config c;
        if (argc > 1) {
            std::string a = argv[1];
            size_t p = a.find(':');
            c.host = a.substr(0, p);
            if (p != std::string::npos) c.port = a.substr(p + 1);
        }
        return c;
    }
};

// ============================================================ RAII sessions
class WinsockSession {
public:
    WinsockSession() {
        WSADATA wd;
        rc_ = WSAStartup(MAKEWORD(2, 2), &wd);
        LOG("WSAStartup -> %d (version %d.%d)", rc_, LOBYTE(wd.wVersion), HIBYTE(wd.wVersion));
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
        LOG("GdiplusStartup -> status %d", (int)st_);
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
        if (!h) { LOG("Decode: GlobalAlloc(%llu) failed err=%lu", (unsigned long long)n, GetLastError()); return nullptr; }
        memcpy(GlobalLock(h), data, n);
        GlobalUnlock(h);
        IStream* s = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(h, TRUE, &s);
        if (hr != S_OK) { LOG("Decode: CreateStreamOnHGlobal hr=0x%08lX", (unsigned long)hr); GlobalFree(h); return nullptr; }
        Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromStream(s);
        Gdiplus::Bitmap* out = nullptr;
        if (!src) LOG("Decode: FromStream returned null (size=%llu)", (unsigned long long)n);
        else if (src->GetLastStatus() != Gdiplus::Ok) LOG("Decode: GDI+ status=%d (size=%llu)", (int)src->GetLastStatus(), (unsigned long long)n);
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

    // Draws the latest frame (aspect-fit, letterboxed) or the status text.
    void Paint(HDC dc, const RECT& rc) {
        ScopedLock lk(cs_);
        int W = rc.right - rc.left, H = rc.bottom - rc.top;
        if (bmp_) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
            double bw = bmp_->GetWidth(), bh = bmp_->GetHeight();
            double sc = min(W / bw, H / bh);
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            g.DrawImage(bmp_, (W - dw) / 2, (H - dh) / 2, dw, dh);
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

    MjpegClient(const Config& cfg, FrameStore& store, FrameCallback onFrame)
        : cfg_(cfg), store_(store), onFrame_(std::move(onFrame)) {
        stopEvt_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }
    ~MjpegClient() { Stop(); if (stopEvt_) CloseHandle(stopEvt_); }
    MjpegClient(const MjpegClient&) = delete;
    MjpegClient& operator=(const MjpegClient&) = delete;

    void Start() {
        if (thread_) return;
        ResetEvent(stopEvt_);
        thread_ = CreateThread(nullptr, 0, &MjpegClient::ThreadProc, this, 0, nullptr);
        if (!thread_) LOG("CreateThread failed err=%lu", GetLastError());
    }
    void Stop() {
        if (!thread_) return;
        SetEvent(stopEvt_);
        {   // unblock a pending recv()
            ScopedLock lk(sockCs_);
            if (sock_ != INVALID_SOCKET) shutdown(sock_, SD_BOTH);
        }
        if (WaitForSingleObject(thread_, 3000) == WAIT_TIMEOUT) LOG("NetThread did not stop within 3s");
        CloseHandle(thread_);
        thread_ = nullptr;
    }

private:
    static DWORD WINAPI ThreadProc(LPVOID p) { static_cast<MjpegClient*>(p)->Run(); return 0; }

    bool Stopping() const { return WaitForSingleObject(stopEvt_, 0) == WAIT_OBJECT_0; }
    // Interruptible sleep; returns true if stop was requested meanwhile.
    bool SleepOrStop(DWORD ms) const { return WaitForSingleObject(stopEvt_, ms) == WAIT_OBJECT_0; }

    void Run() {
        LOG("NetThread started");
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
        LOG("NetThread exit");
    }

    SOCKET ConnectSocket() {
        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int gr = getaddrinfo(cfg_.host.c_str(), cfg_.port.c_str(), &hints, &res);
        if (gr != 0) { LOG("getaddrinfo(%s:%s) failed: %d", cfg_.host.c_str(), cfg_.port.c_str(), gr); return INVALID_SOCKET; }
        SOCKET s = INVALID_SOCKET;
        for (addrinfo* p = res; p; p = p->ai_next) {
            char ip[64] = "?";
            getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, ip, sizeof ip, nullptr, 0, NI_NUMERICHOST);
            s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s == INVALID_SOCKET) { LOG("socket() failed wsaerr=%d", WSAGetLastError()); continue; }
            DWORD to = 5000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
            LOG("connecting to %s:%s ...", ip, cfg_.port.c_str());
            if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) { LOG("connected"); break; }
            LOG("connect() failed wsaerr=%d", WSAGetLastError());
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
        LOG("send request (%d bytes) -> %d", (int)req.size(), sr);

        std::string buf;
        bool headChecked = false;
        unsigned long long totalRecv = 0;
        static thread_local char tmp[65536];
        while (!Stopping()) {
            int r = recv(s, tmp, sizeof tmp, 0);
            if (r <= 0) {
                LOG("recv returned %d wsaerr=%d (total %llu bytes)", r, r < 0 ? WSAGetLastError() : 0, totalRecv);
                return false;
            }
            totalRecv += (unsigned)r;
            buf.append(tmp, r);
            if (!headChecked && buf.size() >= 12) {
                headChecked = true;
                LOG("response head: %s", Logger::Printable(buf, 300).c_str());
                if (buf.compare(0, 5, "HTTP/") == 0 && buf.find(" 200") != 8) {
                    std::string line = buf.substr(0, buf.find("\r\n"));
                    store_.SetStatus("HTTP error: " + line);
                    LOG("non-200 response -> abort: %s", line.c_str());
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
                if (buf.size() > (16u << 20)) { LOG("buffer > 16MB without EOI, cleared"); buf.clear(); }
                return;
            }
            Gdiplus::Bitmap* bmp = JpegDecoder::Decode(buf.data() + a, e + 2 - a);
            buf.erase(0, e + 2);
            if (!bmp) continue;
            UINT w = bmp->GetWidth(), h = bmp->GetHeight();
            LONG tot = store_.Publish(bmp);
            if (tot == 1) LOG("first frame decoded: %ux%u", w, h);
            else if (tot % 300 == 0) LOG("frames so far: %ld", tot);
            if (onFrame_) onFrame_();
        }
    }

    Config cfg_;
    FrameStore& store_;
    FrameCallback onFrame_;
    HANDLE thread_ = nullptr;
    HANDLE stopEvt_ = nullptr;
    CritSec sockCs_;
    SOCKET sock_ = INVALID_SOCKET;
};

// ============================================================ ViewerWindow
class ViewerWindow {
public:
    ViewerWindow(HINSTANCE inst, const Config& cfg, FrameStore& store)
        : inst_(inst), cfg_(cfg), store_(store) {}
    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Create() {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = &ViewerWindow::StaticProc;
        wc.hInstance = inst_;
        wc.lpszClassName = "IPCam";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassA(&wc)) { LOG("RegisterClass failed err=%lu", GetLastError()); return false; }
        hwnd_ = CreateWindowA("IPCam", "IP Webcam", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, inst_, this);
        if (!hwnd_) { LOG("CreateWindow failed err=%lu", GetLastError()); return false; }
        SetTimer(hwnd_, 1, 1000, nullptr);
        LOG("window created hwnd=%p", (void*)hwnd_);
        return true;
    }

    // Safe to call from any thread.
    void NotifyFrame() { if (hwnd_) PostMessage(hwnd_, WM_APP, 0, 0); }

    int RunMessageLoop() {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
        return (int)msg.wParam;
    }

private:
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
        case WM_APP:        InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_TIMER:      OnTimer(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:      OnPaint(); return 0;
        case WM_KEYDOWN:    if (w == VK_ESCAPE) DestroyWindow(h); return 0;
        case WM_CLOSE:      LOG("WM_CLOSE"); break;
        case WM_DESTROY:    LOG("WM_DESTROY"); PostQuitMessage(0); return 0;
        }
        return DefWindowProc(h, m, w, l);
    }

    void OnTimer() {
        char t[256];
        snprintf(t, sizeof t, "IP Webcam %s:%s  -  %d fps", cfg_.host.c_str(), cfg_.port.c_str(), store_.TakeFps());
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
        store_.Paint(mdc, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(mb);
        DeleteDC(mdc);
        EndPaint(hwnd_, &ps);
    }

    HINSTANCE inst_;
    Config cfg_;
    FrameStore& store_;
    HWND hwnd_ = nullptr;
};

// ============================================================ App
class App {
public:
    explicit App(HINSTANCE inst) : inst_(inst) {}

    int Run(int argc, char** argv) {
        OpenLog();
        SetUnhandledExceptionFilter(&App::CrashHandler);
        LOG("==== ipcam start ====");
        LOG("cmdline=%s", GetCommandLineA());
        LOG("pid=%lu", GetCurrentProcessId());

        Config cfg = Config::FromArgs(argc, argv);
        LOG("target=%s:%s%s", cfg.host.c_str(), cfg.port.c_str(), cfg.path.c_str());

        int rc = 0;
        {   // scope: destruction order = client, window, store, gdi+, winsock
            WinsockSession ws;
            if (!ws.Ok()) return Fail("WSAStartup failed");
            GdiplusSession gdi;
            if (!gdi.Ok()) return Fail("GdiplusStartup failed");

            FrameStore store;
            ViewerWindow window(inst_, cfg, store);
            if (!window.Create()) return Fail("Window creation failed (see ipcam.log)");

            MjpegClient client(cfg, store, [&window] { window.NotifyFrame(); });
            client.Start();
            rc = window.RunMessageLoop();
            LOG("message loop ended, total frames=%ld", store.Total());
            client.Stop();
        }
        LOG("==== ipcam exit ====");
        Logger::Instance().Close();
        return rc;
    }

private:
    void OpenLog() {
        char exe[MAX_PATH] = "";
        std::string path = "ipcam.log";
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
            path = exe;
            size_t dot = path.find_last_of('.');
            path = (dot == std::string::npos ? path : path.substr(0, dot)) + ".log";
        }
        if (!Logger::Instance().Open(path))
            MessageBoxA(nullptr, ("Cannot open log file: " + path).c_str(), "ipcam", MB_ICONERROR);
        LOG("exe=%s", exe);
    }

    int Fail(const char* msg) {
        LOG("FATAL: %s", msg);
        MessageBoxA(nullptr, msg, "ipcam", MB_ICONERROR);
        Logger::Instance().Close();
        return 1;
    }

    static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
        LOG("!!! CRASH: exception code=0x%08lX address=%p", ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
        Logger::Instance().Close();
        return EXCEPTION_EXECUTE_HANDLER;
    }

    HINSTANCE inst_;
};

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    return App(inst).Run(__argc, __argv);
}
