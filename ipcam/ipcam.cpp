// IP Webcam (Android) MJPEG viewer for Windows 11 - single file, no external libs
// Build: g++ -O2 -std=c++17 -o ipcam.exe ipcam.cpp -lws2_32 -lgdiplus -lgdi32 -lole32 -mwindows -static
// Run  : ipcam.exe 192.168.0.70:4747      (log file: ipcam.log next to the exe)
// Phone App : https://play.google.com/store/apps/details?id=com.dev47apps.obsdroidcam

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
using std::min; using std::max;
#include <gdiplus.h>
#include <cstdio>
#include <cstdarg>
#include <string>

static HWND g_hwnd;
static CRITICAL_SECTION g_cs, g_lcs;
static Gdiplus::Bitmap* g_bmp = nullptr;
static volatile LONG g_run = 1;
static volatile LONG g_frames = 0;
static volatile LONG g_total = 0;
static std::string g_host = "192.168.0.70", g_port = "4747", g_status = "Connecting...";
static const char* g_path = "/video";
static FILE* g_log = nullptr;
static char g_logpath[MAX_PATH] = "ipcam.log";

// ---------- logging ----------
static void Log(const char* fmt, ...) {
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SYSTEMTIME t; GetLocalTime(&t);
    EnterCriticalSection(&g_lcs);
    if (g_log) {
        fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d.%03d [T%lu] %s\n", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), msg);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_lcs);
}

static std::string Printable(const std::string& s, size_t n) {
    std::string o;
    for (size_t i = 0; i < s.size() && i < n; i++) {
        unsigned char c = s[i];
        if (c == '\r') o += "\\r"; else if (c == '\n') o += "\\n";
        else if (c >= 32 && c < 127) o += (char)c; else { char b[8]; snprintf(b, sizeof b, "\\x%02X", c); o += b; }
    }
    return o;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    Log("!!! CRASH: exception code=0x%08lX address=%p", ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------- image decode ----------
static Gdiplus::Bitmap* Decode(const char* d, size_t n) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!h) { Log("Decode: GlobalAlloc(%zu) failed err=%lu", n, GetLastError()); return nullptr; }
    memcpy(GlobalLock(h), d, n);
    GlobalUnlock(h);
    IStream* s = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(h, TRUE, &s);
    if (hr != S_OK) { Log("Decode: CreateStreamOnHGlobal hr=0x%08lX", (unsigned long)hr); GlobalFree(h); return nullptr; }
    Gdiplus::Bitmap* b = Gdiplus::Bitmap::FromStream(s);
    Gdiplus::Bitmap* c = nullptr;
    if (!b) Log("Decode: FromStream returned null (size=%zu)", n);
    else if (b->GetLastStatus() != Gdiplus::Ok) Log("Decode: GDI+ status=%d (size=%zu)", (int)b->GetLastStatus(), n);
    else c = b->Clone(0, 0, b->GetWidth(), b->GetHeight(), PixelFormat32bppPARGB);
    delete b;
    s->Release();
    return c;
}

// ---------- network ----------
static SOCKET Connect() {
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    int gr = getaddrinfo(g_host.c_str(), g_port.c_str(), &hints, &res);
    if (gr != 0) { Log("getaddrinfo(%s:%s) failed: %d", g_host.c_str(), g_port.c_str(), gr); return INVALID_SOCKET; }
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        char ip[64] = "?";
        getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, ip, sizeof ip, nullptr, 0, NI_NUMERICHOST);
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) { Log("socket() failed wsaerr=%d", WSAGetLastError()); continue; }
        DWORD to = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        Log("connecting to %s:%s ...", ip, g_port.c_str());
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) { Log("connected"); break; }
        Log("connect() failed wsaerr=%d", WSAGetLastError());
        closesocket(s); s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

static DWORD WINAPI NetThread(LPVOID) {
    Log("NetThread started");
    static char tmp[65536];
    while (g_run) {
        SOCKET s = Connect();
        if (s == INVALID_SOCKET) { g_status = "Connect failed - retrying..."; Sleep(2000); continue; }
        std::string req = std::string("GET ") + g_path + " HTTP/1.0\r\nHost: " + g_host +
                          "\r\nConnection: close\r\n\r\n";
        int sr = send(s, req.c_str(), (int)req.size(), 0);
        Log("send request (%d bytes) -> %d", (int)req.size(), sr);
        std::string buf;
        bool checked = false;
        size_t totalRecv = 0;
        while (g_run) {
            int r = recv(s, tmp, sizeof tmp, 0);
            if (r <= 0) { Log("recv returned %d wsaerr=%d (total %zu bytes)", r, r < 0 ? WSAGetLastError() : 0, totalRecv); break; }
            totalRecv += r;
            buf.append(tmp, r);
            if (!checked && buf.size() >= 12) {
                checked = true;
                Log("response head: %s", Printable(buf, 300).c_str());
                if (buf.compare(0, 5, "HTTP/") == 0 && buf.find(" 200") != 8) {
                    g_status = "HTTP error: " + buf.substr(0, buf.find("\r\n"));
                    Log("non-200 response -> abort: %s", g_status.c_str());
                    break;
                }
            }
            for (;;) {  // extract complete JPEGs (SOI..EOI)
                size_t a = buf.find("\xFF\xD8");
                if (a == std::string::npos) { if (buf.size() > 4) buf.erase(0, buf.size() - 1); break; }
                size_t e = buf.find("\xFF\xD9", a + 2);
                if (e == std::string::npos) {
                    if (a > 0) buf.erase(0, a);
                    if (buf.size() > (16u << 20)) { Log("buffer > 16MB without EOI, cleared"); buf.clear(); }
                    break;
                }
                Gdiplus::Bitmap* nb = Decode(buf.data() + a, e + 2 - a);
                buf.erase(0, e + 2);
                if (!nb) continue;
                EnterCriticalSection(&g_cs);
                delete g_bmp; g_bmp = nb;
                LeaveCriticalSection(&g_cs);
                InterlockedIncrement(&g_frames);
                LONG tot = InterlockedIncrement(&g_total);
                if (tot == 1) Log("first frame decoded: %ux%u", nb->GetWidth(), nb->GetHeight());
                else if (tot % 300 == 0) Log("frames so far: %ld", tot);
                PostMessage(g_hwnd, WM_APP, 0, 0);
            }
        }
        closesocket(s);
        if (g_run) { if (g_status.rfind("HTTP", 0) != 0) g_status = "Disconnected - retrying..."; Sleep(2000); }
    }
    Log("NetThread exit");
    return 0;
}

// ---------- window ----------
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_APP: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_TIMER: {
        char t[256];
        snprintf(t, sizeof t, "IP Webcam %s:%s  -  %d fps", g_host.c_str(), g_port.c_str(), (int)InterlockedExchange(&g_frames, 0));
        SetWindowTextA(h, t);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN: if (w == VK_ESCAPE) DestroyWindow(h); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP mb = CreateCompatibleBitmap(hdc, W, H);
        HGDIOBJ old = SelectObject(mdc, mb);
        FillRect(mdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EnterCriticalSection(&g_cs);
        if (g_bmp) {
            Gdiplus::Graphics g(mdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
            double bw = g_bmp->GetWidth(), bh = g_bmp->GetHeight();
            double sc = min(W / bw, H / bh);
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            g.DrawImage(g_bmp, (W - dw) / 2, (H - dh) / 2, dw, dh);
        } else {
            SetTextColor(mdc, RGB(200, 200, 200)); SetBkMode(mdc, TRANSPARENT);
            DrawTextA(mdc, g_status.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        LeaveCriticalSection(&g_cs);
        BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old); DeleteObject(mb); DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CLOSE: Log("WM_CLOSE"); break;
    case WM_DESTROY: Log("WM_DESTROY"); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_lcs);

    // log file next to the exe
    char exe[MAX_PATH] = "";
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
        strncpy(g_logpath, exe, MAX_PATH - 5);
        char* dot = strrchr(g_logpath, '.');
        if (dot) strcpy(dot, ".log"); else strcat(g_logpath, ".log");
    }
    g_log = fopen(g_logpath, "w");
    if (!g_log) MessageBoxA(nullptr, (std::string("Cannot open log file: ") + g_logpath).c_str(), "ipcam", MB_ICONERROR);
    SetUnhandledExceptionFilter(CrashHandler);

    Log("==== ipcam start ====");
    Log("exe=%s", exe);
    Log("cmdline=%s", GetCommandLineA());
    OSVERSIONINFOA dummy; (void)dummy;
    Log("pid=%lu", GetCurrentProcessId());

    if (__argc > 1) {
        std::string a = __argv[1];
        size_t c = a.find(':');
        g_host = a.substr(0, c);
        if (c != std::string::npos) g_port = a.substr(c + 1);
    }
    Log("target=%s:%s%s", g_host.c_str(), g_port.c_str(), g_path);

    WSADATA wd;
    int wr = WSAStartup(MAKEWORD(2, 2), &wd);
    Log("WSAStartup -> %d (version %d.%d)", wr, LOBYTE(wd.wVersion), HIBYTE(wd.wVersion));
    if (wr != 0) { MessageBoxA(nullptr, "WSAStartup failed", "ipcam", MB_ICONERROR); return 1; }

    ULONG_PTR tok = 0; Gdiplus::GdiplusStartupInput gi;
    Gdiplus::Status gs = Gdiplus::GdiplusStartup(&tok, &gi, nullptr);
    Log("GdiplusStartup -> status %d", (int)gs);
    if (gs != Gdiplus::Ok) { MessageBoxA(nullptr, "GdiplusStartup failed", "ipcam", MB_ICONERROR); return 1; }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "IPCam";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc)) { Log("RegisterClass failed err=%lu", GetLastError()); MessageBoxA(nullptr, "RegisterClass failed", "ipcam", MB_ICONERROR); return 1; }
    g_hwnd = CreateWindowA("IPCam", "IP Webcam", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) { Log("CreateWindow failed err=%lu", GetLastError()); MessageBoxA(nullptr, "CreateWindow failed", "ipcam", MB_ICONERROR); return 1; }
    Log("window created hwnd=%p", (void*)g_hwnd);
    SetTimer(g_hwnd, 1, 1000, nullptr);

    HANDLE th = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr);
    if (!th) Log("CreateThread failed err=%lu", GetLastError());
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    Log("message loop ended, total frames=%ld", g_total);

    g_run = 0;
    Log("==== ipcam exit ====");
    if (g_log) fclose(g_log);
    ExitProcess(0);
    return 0;
}
