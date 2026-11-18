// gsHomeManagerLauncher.cpp - gsHomeManager 런처 (다른 프로젝트에서도 재사용 가능: 이름/경로/횟수/시간은 모두 ini 에서 읽음)
// 설정 파일은 이 exe 와 같은 폴더의 <exe 이름>.ini 의 [Launcher] 섹션, 로그는 기본 이 폴더의 log\<exe 이름>.log (log 폴더가 없으면 만듦)
// (exe 이름을 바꿔 복사하면 ini/log 이름도 따라 바뀜. 키 설명은 ini 참고)
// 1) 서버(GitHub)의 version.txt 를 받아 PC 의 exe 파일 버전과 비교
// 2) PC 쪽이 낮으면 서버 exe 를 내려받아 교체
// 3) LocalExe 실행
// 4) 실행한 본 프로세스를 감시: 종료하면 (정상 종료든 비정상 종료든) 버전 확인 없이 다시 실행 (ini Watch=0 이면 감시 안 함)
//    다시 실행하기 전에 같은 이름의 프로세스가 이미 떠 있으면(사용자가 직접 띄웠거나 /restart 로 새 런처가 먼저 띄운 경우) 감시를 끝낸다.
// 서버에 접속하지 못하거나 다운로드/교체에 실패해도 기존 exe 는 그대로 실행한다.
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdio>

static std::wstring g_dir;      // 이 런처 exe 가 있는 폴더 (끝에 \ 포함)
static std::wstring g_base;     // 이 런처 exe 의 이름 (확장자 제외). ini / log 기본 이름
static std::wstring g_title;    // 메시지 상자 제목 (ini AppName)
static std::wstring g_logPath;  // 로그 파일 전체 경로
static int g_logMaxKB = 256;    // 로그가 이 크기(KB)를 넘으면 새로 시작
static int g_httpConnectMs = 5000;     // 서버 연결 제한 시간 (ms)
static int g_httpRecvMs = 30000;       // 서버 응답 수신 제한 시간 (ms)
static std::wstring g_args;     // 본 프로세스에 넘길 명령줄 인자 (ini Args)
static std::wstring g_workDir;  // 본 프로세스 시작 폴더 (ini WorkDir, 비어 있으면 exe 폴더)

// ---------- UTF-8 / 문자열 ----------
static std::wstring U2W(const std::string& s)
{
    if (s.empty()) return L"";
    UINT cp = CP_UTF8;
    DWORD fl = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), nullptr, 0);
    if (!n) { cp = CP_ACP; fl = 0; n = MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), nullptr, 0); }   // UTF-8 이 아니면 ANSI
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string W2U(const std::wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---------- 로그 (기본 log\<exe 이름>.log, LogMaxKB 넘으면 새로 시작) ----------
// 로그 파일이 들어갈 폴더가 없으면 빠진 폴더를 모두 만든다
static void EnsureLogDir()
{
    size_t last = g_logPath.find_last_of(L'\\');
    if (last == std::wstring::npos) return;
    for (size_t i = g_logPath.find(L'\\', 3); i != std::wstring::npos && i <= last; i = g_logPath.find(L'\\', i + 1))
        CreateDirectoryW(g_logPath.substr(0, i).c_str(), nullptr);      // 이미 있으면 실패하지만 무시
}

static void Log(const std::wstring& msg)
{
    static std::wstring dirFor;                                 // 폴더를 확인한 로그 경로 (경로가 바뀌면 다시 확인)
    if (dirFor != g_logPath) { EnsureLogDir(); dirFor = g_logPath; }
    HANDLE h = CreateFileW(g_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    if (g_logMaxKB > 0 && GetFileSize(h, nullptr) > (DWORD)g_logMaxKB * 1024) { SetFilePointer(h, 0, nullptr, FILE_BEGIN); SetEndOfFile(h); }
    SetFilePointer(h, 0, nullptr, FILE_END);
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t ts[40];
    _snwprintf(ts, 40, L"[%04d-%02d-%02d %02d:%02d:%02d] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string line = W2U(std::wstring(ts) + msg) + "\r\n";
    DWORD w; WriteFile(h, line.data(), (DWORD)line.size(), &w, nullptr);
    CloseHandle(h);
}

// 오류 번호 설명 (WinHTTP 주요 오류 + HTTP 상태 코드)
static std::wstring ErrDesc(DWORD e)
{
    if (e >= 100 && e < 600) return L", HTTP 상태 " + std::to_wstring(e);
    switch (e) {
    case 2:     return L", 파일을 찾을 수 없음";
    case 3:     return L", 경로를 찾을 수 없음";
    case 5:     return L", 접근 거부";
    case 32:    return L", 다른 프로그램이 사용 중";
    case 12002: return L", 서버 응답 시간 초과";
    case 12007: return L", 서버 이름을 찾을 수 없음 (DNS / 인터넷 연결 확인)";
    case 12029: return L", 서버에 연결하지 못함";
    case 12030: return L", 연결이 끊어짐";
    case 12037: case 12038: case 12044: case 12045: return L", 서버 인증서 오류";
    case 12152: return L", 서버 응답이 올바르지 않음";
    case 12175: return L", 보안 연결(SSL) 오류";
    }
    return L"";
}
static std::wstring ErrE(const std::wstring& what, DWORD e) { return what + L" (오류 " + std::to_wstring(e) + ErrDesc(e) + L")"; }
static std::wstring Err(const std::wstring& what) { return ErrE(what, GetLastError()); }

// ---------- ini ([Launcher] 섹션만 읽음, UTF-8 BOM 있어도/없어도 OK) ----------
static std::string ReadAll(const std::wstring& path)
{
    std::string s;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    DWORD sz = GetFileSize(h, nullptr), r = 0;
    if (sz != INVALID_FILE_SIZE) { s.resize(sz); ReadFile(h, &s[0], sz, &r, nullptr); s.resize(r); }
    CloseHandle(h);
    if (s.size() >= 3 && (BYTE)s[0] == 0xEF && (BYTE)s[1] == 0xBB && (BYTE)s[2] == 0xBF) s.erase(0, 3);
    return s;
}

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::wstring IniGet(const std::string& text, const char* key)
{
    bool in = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string ln = Trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (ln.empty() || ln[0] == ';') continue;
        if (ln[0] == '[') { in = (_stricmp(ln.c_str(), "[Launcher]") == 0); continue; }
        if (!in) continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        if (_stricmp(Trim(ln.substr(0, eq)).c_str(), key) == 0) return U2W(Trim(ln.substr(eq + 1)));
    }
    return L"";
}

// 정수 키 (없거나 숫자가 아니면 def, 음수는 0)
static int IniInt(const std::string& text, const char* key, int def)
{
    std::wstring v = IniGet(text, key);
    if (v.empty()) return def;
    wchar_t* e = nullptr;
    long n = wcstol(v.c_str(), &e, 10);
    if (e == v.c_str()) return def;
    return n < 0 ? 0 : (int)n;
}

// 상대 경로면 이 런처 exe 폴더 기준으로 만든다 (X:\... 와 \\서버\... 는 그대로)
static std::wstring Resolve(const std::wstring& p)
{
    if (p.empty()) return p;
    if (p.size() > 1 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'))) return p;
    return g_dir + p;
}

// ---------- 버전 (a.b.c.d -> 64bit), 읽을 수 없으면 0 ----------
static ULONGLONG GetFileVer(const std::wstring& path)
{
    DWORD h = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &h);
    if (!sz) return 0;
    std::vector<BYTE> d(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, d.data())) return 0;
    VS_FIXEDFILEINFO* fi = nullptr; UINT len = 0;
    if (!VerQueryValueW(d.data(), L"\\", (LPVOID*)&fi, &len) || !fi) return 0;
    return ((ULONGLONG)fi->dwFileVersionMS << 32) | fi->dwFileVersionLS;
}

// version.txt 내용 "1.0.0.1" -> 64bit. 형식이 틀리면 0
static ULONGLONG ParseVer(const std::string& s)
{
    size_t i = s.find_first_of("0123456789");
    if (i == std::string::npos) return 0;
    unsigned a, b, c, d;
    if (sscanf(s.c_str() + i, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    return ((ULONGLONG)(a & 0xFFFF) << 48) | ((ULONGLONG)(b & 0xFFFF) << 32) | ((ULONGLONG)(c & 0xFFFF) << 16) | (d & 0xFFFF);
}

static std::wstring VerStr(ULONGLONG v)
{
    wchar_t b[40];
    _snwprintf(b, 40, L"%u.%u.%u.%u", (unsigned)(v >> 48) & 0xFFFF, (unsigned)(v >> 32) & 0xFFFF,
               (unsigned)(v >> 16) & 0xFFFF, (unsigned)v & 0xFFFF);
    return b;
}

// ---------- HTTPS GET (WinHTTP: 캐시 없음, 리다이렉트 자동, 시간 제한 있음) ----------
static DWORD g_httpStatus = 0;     // 마지막 HttpGet 의 HTTP 상태 코드 (응답을 못 받았으면 0)

static bool HttpGet(const std::wstring& url, std::string& out)
{
    out.clear();
    g_httpStatus = 0;
    wchar_t host[256], path[2048];
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    bool ok = false;
    HINTERNET s = WinHttpOpen((g_base + L"-launcher").c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 5000, g_httpConnectMs, 10000, g_httpRecvMs);            // 이름 확인, 연결, 송신, 수신 (ms)
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &code, &sz, WINHTTP_NO_HEADER_INDEX);
        g_httpStatus = code;
        if (code == 200) {
            ok = true;
            static char buf[32768];
            for (;;) {
                DWORD n = 0;
                if (!WinHttpReadData(r, buf, sizeof(buf), &n)) { ok = false; break; }
                if (!n) break;
                out.append(buf, n);
                if (out.size() > 100000000) { ok = false; break; }      // 100MB 넘으면 이상한 응답
            }
        } else SetLastError(code);                              // 404 등 HTTP 상태를 오류 번호로 기록
    }
    DWORD e = GetLastError();
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    SetLastError(e);
    return ok;
}

static bool WriteFileAll(const std::wstring& path, const std::string& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
    CloseHandle(h);
    return ok;
}

// ---------- 갱신 ----------
static std::wstring VerOrNone(ULONGLONG v) { return v ? VerStr(v) : L"(읽을 수 없음)"; }

// 파일 정보 한 줄: 크기, 수정 시각, 파일 버전
static std::wstring FileInfo(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return L"파일 없음";
    ULONGLONG sz = ((ULONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    FILETIME lft; SYSTEMTIME st = {};
    FileTimeToLocalFileTime(&d.ftLastWriteTime, &lft);
    FileTimeToSystemTime(&lft, &st);
    wchar_t b[64];
    _snwprintf(b, 64, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::to_wstring(sz) + L"바이트, 수정 " + b + L", 파일 버전 " + VerOrNone(GetFileVer(path));
}

static std::wstring Ms(ULONGLONG ms) { return std::to_wstring(ms) + L"ms"; }

static void Update(const std::wstring& local, const std::wstring& verUrl, const std::wstring& exeUrl)
{
    Log(L"[갱신] ---- 갱신 확인 시작 ----");
    Log(L"[갱신] PC exe: " + local + L" -> " + FileInfo(local));
    const bool had0 = Exists(local);
    ULONGLONG lv = GetFileVer(local);                           // exe 가 없으면 0 -> 내려받음
    Log(L"[갱신] PC 버전: " + VerOrNone(lv) + (had0 ? L"" : L" (exe 가 없으므로 서버에서 내려받음)"));

    // 1) 서버 version.txt
    Log(L"[갱신] 서버 version.txt 요청: " + verUrl + L" (연결 제한 " + std::to_wstring(g_httpConnectMs / 1000) + L"초, 수신 제한 " + std::to_wstring(g_httpRecvMs / 1000) + L"초)");
    std::string vt;
    ULONGLONG t0 = GetTickCount64();
    bool ok = HttpGet(verUrl, vt);
    DWORD e = GetLastError();
    ULONGLONG ms = GetTickCount64() - t0;
    if (!ok) {
        Log(ErrE(L"[갱신] 서버 version.txt 받기 실패", e) + L", " + Ms(ms) + L" - 갱신을 건너뛰고 PC 의 exe 를 그대로 실행");
        return;
    }
    std::string shown = Trim(vt);
    for (char& ch : shown) if (ch == '\r' || ch == '\n') ch = ' ';
    if (shown.size() > 60) shown = shown.substr(0, 60) + "...";
    Log(L"[갱신] 서버 version.txt 응답: HTTP " + std::to_wstring(g_httpStatus) + L", " + std::to_wstring(vt.size()) + L"바이트, " + Ms(ms) + L", 내용 \"" + U2W(shown) + L"\"");

    ULONGLONG sv = ParseVer(vt);
    if (!sv) { Log(L"[갱신] 서버 version.txt 형식 오류 (a.b.c.d 형식이어야 함) - 갱신을 건너뛰고 PC 의 exe 를 그대로 실행"); return; }
    Log(L"[갱신] 버전 비교: 서버 " + VerStr(sv) + (lv >= sv ? L" <= PC " : L" > PC ") + VerOrNone(lv));
    if (lv >= sv) { Log(L"[갱신] PC 가 최신이므로 갱신하지 않음"); return; }

    // 2) 서버 exe 내려받기
    Log(L"[갱신] 서버 exe 요청: " + exeUrl);
    std::string data;
    t0 = GetTickCount64();
    ok = HttpGet(exeUrl, data);
    e = GetLastError();
    ms = GetTickCount64() - t0;
    if (!ok) {
        Log(ErrE(L"[갱신] 서버 exe 다운로드 실패", e) + L", " + std::to_wstring(data.size()) + L"바이트까지 받음, " + Ms(ms) + L" - 갱신을 건너뛰고 PC 의 exe 를 그대로 실행");
        return;
    }
    Log(L"[갱신] 서버 exe 다운로드 완료: HTTP " + std::to_wstring(g_httpStatus) + L", " + std::to_wstring(data.size()) + L"바이트, " + Ms(ms) +
        (ms > 0 ? L", 약 " + std::to_wstring((ULONGLONG)data.size() / (ms ? ms : 1)) + L" KB/s" : L""));

    // 3) .new 로 저장하고 검증
    std::wstring tmp = local + L".new", old = local + L".old";
    if (!WriteFileAll(tmp, data)) { e = GetLastError(); DeleteFileW(tmp.c_str()); Log(ErrE(L"[갱신] .new 파일 저장 실패: " + tmp, e) + L" - 갱신 취소"); return; }
    Log(L"[갱신] 임시 파일 저장: " + tmp + L" -> " + FileInfo(tmp));
    ULONGLONG tv = GetFileVer(tmp);                             // 받은 파일 자체의 버전으로 검증 (깨진 파일/HTML 오류 페이지/CDN 지연 방지)
    if (tv <= lv) {
        DeleteFileW(tmp.c_str());
        Log(L"[갱신] 받은 exe 의 버전을 읽을 수 없거나 PC 보다 높지 않음 (받은 파일 " + VerOrNone(tv) + L", PC " + VerOrNone(lv) + L", 서버 version.txt " + VerStr(sv) + L") - 임시 파일을 지우고 갱신 취소");
        return;
    }
    Log(L"[갱신] 받은 파일 검증 통과: 파일 버전 " + VerStr(tv) + (tv != sv ? L" (서버 version.txt " + VerStr(sv) + L" 와 다름)" : L""));

    // 4) 교체: 실행 중인 exe 는 덮어쓸 수 없지만 이름 변경은 가능하다
    bool had = Exists(local);
    if (had) {
        if (!MoveFileExW(local.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            e = GetLastError(); DeleteFileW(tmp.c_str());
            Log(ErrE(L"[갱신] 기존 exe 이름 변경 실패", e) + L" - 갱신 취소");
            return;
        }
        Log(L"[갱신] 기존 exe -> " + old + L" 이름 변경 완료");
    }
    if (!MoveFileExW(tmp.c_str(), local.c_str(), 0)) {
        e = GetLastError();
        Log(ErrE(L"[갱신] 새 exe 교체 실패", e) + L" - 원래대로 복구");
        if (had) {
            if (MoveFileExW(old.c_str(), local.c_str(), MOVEFILE_REPLACE_EXISTING)) Log(L"[갱신] 원래 exe 복구 완료");
            else Log(Err(L"[갱신] 원래 exe 복구도 실패 - 수동 확인 필요: " + old));
        }
        DeleteFileW(tmp.c_str());
        return;
    }
    Log(L"[갱신] 갱신 완료: " + VerStr(lv) + L" -> " + VerStr(tv) + L" (" + FileInfo(local) + L")");
}

// ---------- 본 프로세스 실행 / 실행 중인지 확인 ----------
static bool Launch(const std::wstring& local, HANDLE& hProc)
{
    std::wstring cmd = L"\"" + local + L"\"" + (g_args.empty() ? L"" : L" " + g_args);
    std::wstring dir = g_workDir.empty() ? local.substr(0, local.find_last_of(L'\\')) : g_workDir;
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    Log(L"실행: " + cmd + L" (작업 폴더: " + dir + L")");
    if (!CreateProcessW(local.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    hProc = pi.hProcess;
    Log(L"실행됨 (pid=" + std::to_wstring(pi.dwProcessId) + L")");
    return true;
}

static bool IsRunning(const std::wstring& name)                // 이 이름의 프로세스(exe 파일명)가 떠 있는지
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e = {}; e.dwSize = sizeof(e);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, name.c_str()) == 0) { found = true; break; }
    CloseHandle(snap);
    return found;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_dir = self;
    g_dir.resize(g_dir.find_last_of(L'\\') + 1);
    g_base = self + g_dir.size();
    { size_t d = g_base.find_last_of(L'.'); if (d != std::wstring::npos) g_base.resize(d); }
    g_title = g_base;
    g_logPath = g_dir + L"log\\" + g_base + L".log";            // 기본: 런처 exe 폴더\log\<exe 이름>.log

    std::string ini = ReadAll(g_dir + g_base + L".ini");
    std::wstring t = IniGet(ini, "AppName");   if (!t.empty()) g_title = t;
    t = IniGet(ini, "LogFile");                if (!t.empty()) g_logPath = Resolve(t);
    g_logMaxKB = IniInt(ini, "LogMaxKB", 256);
    g_httpConnectMs = IniInt(ini, "HttpConnectSec", 5) * 1000;
    g_httpRecvMs = IniInt(ini, "HttpReceiveSec", 30) * 1000;
    g_args = IniGet(ini, "Args");
    t = IniGet(ini, "WorkDir");                if (!t.empty()) g_workDir = Resolve(t);

    Log(L"===== 런처 시작 (pid=" + std::to_wstring(GetCurrentProcessId()) + L") =====");
    Log(L"런처 exe: " + std::wstring(self));
    Log(L"설정 파일: " + g_dir + g_base + L".ini -> " + (ini.empty() ? std::wstring(L"없거나 비어 있음 (기본값 사용)") : FileInfo(g_dir + g_base + L".ini")));
    Log(L"로그 파일: " + g_logPath + L" (최대 " + std::to_wstring(g_logMaxKB) + L"KB)");

    std::wstring le = IniGet(ini, "LocalExe");
    if (le.empty()) { Log(L"ini [Launcher] LocalExe 가 비어 있음"); MessageBoxW(nullptr, (L"LocalExe 를 " + g_base + L".ini 에 지정하세요.").c_str(), g_title.c_str(), MB_OK | MB_ICONERROR); return 2; }
    std::wstring local = Resolve(le);
    std::wstring verUrl = IniGet(ini, "ServerVersionUrl");
    std::wstring exeUrl = IniGet(ini, "ServerExeUrl");

    Log(L"LocalExe: " + local);
    {   // 지난 갱신 때 남은 파일 (아직 실행 중이면 실패, 무시)
        std::wstring oldp = local + L".old";
        if (Exists(oldp)) {
            if (DeleteFileW(oldp.c_str())) Log(L"지난 갱신 때 남은 .old 파일 삭제: " + oldp);
            else Log(Err(L"지난 갱신 때 남은 .old 파일 삭제 실패 (본 프로세스가 아직 실행 중이면 정상): " + oldp));
        }
    }
    if (!verUrl.empty() && !exeUrl.empty()) Update(local, verUrl, exeUrl);
    else Log(L"[갱신] ServerVersionUrl / ServerExeUrl 이 비어 있어 갱신 확인을 하지 않음");

    if (!Exists(local)) {
        Log(L"실행할 exe 가 없음: " + local);
        MessageBoxW(nullptr, (L"실행할 파일이 없습니다.\n" + local).c_str(), g_title.c_str(), MB_OK | MB_ICONERROR);
        return 2;
    }

    const bool watch = IniInt(ini, "Watch", 1) != 0;                            // 감시 사용
    const bool onNormal = IniInt(ini, "RestartOnNormalExit", 1) != 0;           // 종료 코드 0 (정상 종료) 일 때도 다시 실행
    const DWORD delayAbnormalMs = (DWORD)IniInt(ini, "RestartDelaySec", 5) * 1000;       // 비정상 종료 후 다시 실행까지 대기
    const DWORD delayNormalMs = (DWORD)IniInt(ini, "NormalExitDelaySec", 15) * 1000;     // 정상 종료 후 다시 실행까지 대기
    const size_t maxRestarts = (size_t)IniInt(ini, "MaxRestarts", 5);           // 0 = 제한 없음
    const ULONGLONG windowMs = (ULONGLONG)IniInt(ini, "RestartWindowSec", 600) * 1000;   // 0 = 기간 제한 없이 누적
    const bool skipIfRunning = IniInt(ini, "SkipIfRunning", 1) != 0;            // 다시 실행 전에 같은 프로세스가 이미 있으면 감시 종료
    std::wstring procName = IniGet(ini, "ProcessName");                         // 비어 있으면 LocalExe 의 파일명
    if (procName.empty()) procName = local.substr(local.find_last_of(L'\\') + 1);

    HANDLE hp = nullptr;
    Log(L"감시 설정: Watch=" + std::to_wstring(watch) + L" 대상=" + procName + L" 정상종료시재실행=" + std::to_wstring(onNormal) +
        L" 대기(비정상/정상)=" + std::to_wstring(delayAbnormalMs / 1000) + L"/" + std::to_wstring(delayNormalMs / 1000) + L"초" +
        L" 최대재시작=" + std::to_wstring(maxRestarts) + L"회/" + std::to_wstring((unsigned long long)(windowMs / 1000)) + L"초 실행중이면감시종료=" + std::to_wstring(skipIfRunning));
    if (!Launch(local, hp)) {
        Log(Err(L"실행 실패"));
        MessageBoxW(nullptr, (L"실행하지 못했습니다.\n" + local).c_str(), g_title.c_str(), MB_OK | MB_ICONERROR);
        return 3;
    }
    if (!watch) { CloseHandle(hp); return 0; }

    // ---------- 본 프로세스 감시 ----------
    std::vector<ULONGLONG> tries;                               // 다시 실행한 시각들 (MaxRestarts / RestartWindowSec 로 무한 반복 방지)
    for (;;) {
        WaitForSingleObject(hp, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(hp, &code);
        CloseHandle(hp); hp = nullptr;
        Log(code == 0 ? L"본 프로세스 정상 종료 (종료 코드 0)" : L"본 프로세스 비정상 종료 (종료 코드 " + std::to_wstring(code) + L")");

        if (code == 0 && !onNormal) { Log(L"RestartOnNormalExit=0 - 감시 종료"); return 0; }

        ULONGLONG now = GetTickCount64();
        if (windowMs > 0) for (size_t i = tries.size(); i-- > 0;) if (now - tries[i] > windowMs) tries.erase(tries.begin() + i);
        if (maxRestarts > 0 && tries.size() >= maxRestarts) { Log(L"설정한 기간 안에 반복해서 종료됨 (MaxRestarts) - 감시 포기"); return 4; }
        tries.push_back(now);

        Sleep(code == 0 ? delayNormalMs : delayAbnormalMs);     // 정상 종료는 새 런처가 먼저 띄울 시간을 더 준다
        if (skipIfRunning && IsRunning(procName)) { Log(L"이미 다른 본 프로세스가 실행 중 - 감시 종료"); return 0; }     // 사용자가 직접 띄웠거나 다른 런처가 먼저 살린 경우
        if (!Exists(local)) { Log(L"실행할 exe 가 없음: " + local); return 2; }
        if (!Launch(local, hp)) { Log(Err(L"다시 시작 실패")); return 3; }
        Log(L"다시 시작 (버전 확인 생략): " + procName);
    }
}
