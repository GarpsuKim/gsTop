// rdp_common.h - shared by rdp_server.cpp and rdp_client.cpp
#pragma once

#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma pack(push, 1)
struct Hdr { uint8_t type; uint32_t len; };
#pragma pack(pop)
struct Ev { int32_t t, a, b, m; }; // m = monitor index (mouse move)
enum { MSG_AUTH = 1, MSG_FRAME = 2, MSG_INPUT = 3, MSG_INFO = 4 };
enum { EV_MOVE = 1, EV_BTN = 2, EV_WHEEL = 3 };

static inline bool sendAll(SOCKET s, const void* p, int n) {
    const char* c = (const char*)p;
    while (n > 0) { int r = send(s, c, n, 0); if (r <= 0) return false; c += r; n -= r; }
    return true;
}
static inline bool recvAll(SOCKET s, void* p, int n) {
    char* c = (char*)p;
    while (n > 0) { int r = recv(s, c, n, 0); if (r <= 0) return false; c += r; n -= r; }
    return true;
}
static inline bool sendMsg(SOCKET s, uint8_t type, const void* p, uint32_t len) {
    Hdr h{type, len};
    return sendAll(s, &h, sizeof h) && (len == 0 || sendAll(s, p, (int)len));
}

// ini helpers: the ini file lives in the same folder as the exe
static inline std::string iniPath(const char* name) {
    char p[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, p, MAX_PATH);
    std::string s(p, n);
    size_t pos = s.find_last_of("\\/");
    return (pos == std::string::npos ? std::string() : s.substr(0, pos + 1)) + name;
}
static inline bool iniExists(const std::string& ini) {
    return GetFileAttributesA(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static inline std::string iniStr(const std::string& ini, const char* sec, const char* key, const char* def = "") {
    char buf[512];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, ini.c_str());
    return buf;
}
static inline int iniInt(const std::string& ini, const char* sec, const char* key, int def = 0) {
    return (int)GetPrivateProfileIntA(sec, key, def, ini.c_str());
}

// log file: <exe folder>\\<name>, one line per event with a timestamp
static std::string gLogPath;
static std::mutex gLogMtx;
static inline void logInit(const char* name) { gLogPath = iniPath(name); }
__attribute__((format(printf, 1, 2)))
static void rlog(const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(gLogMtx);
    if (gLogPath.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(gLogPath.c_str(), GetFileExInfoStandard, &fa) &&
        (fa.nFileSizeHigh || fa.nFileSizeLow > (1u << 20)))
        MoveFileExA(gLogPath.c_str(), (gLogPath + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* f = fopen(gLogPath.c_str(), "ab");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
            t.wSecond, t.wMilliseconds, msg);
    fclose(f);
}

// single instance guard: returns false if another process already holds the named mutex
static inline bool singleInstance(const char* name) {
    HANDLE h = CreateMutexA(NULL, FALSE, name); // kept open until the process exits
    return !h || GetLastError() != ERROR_ALREADY_EXISTS;
}
