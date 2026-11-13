// gsHomeManagerLauncher.cpp - gsHomeManager 런처
// 1) 서버(GitHub)의 version.txt 를 받아 PC 의 exe 파일 버전과 비교
// 2) PC 쪽이 낮으면 서버 exe 를 내려받아 교체
// 3) LocalExe 실행
// 서버에 접속하지 못하거나 다운로드/교체에 실패해도 기존 exe 는 그대로 실행한다.
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdio>

static std::wstring g_dir;      // gsHomeManagerLauncher.exe 가 있는 폴더 (끝에 \ 포함)

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

// ---------- 로그 (gsHomeManagerLauncher.log, 256KB 넘으면 새로 시작) ----------
static void Log(const std::wstring& msg)
{
    HANDLE h = CreateFileW((g_dir + L"gsHomeManagerLauncher.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    if (GetFileSize(h, nullptr) > 262144) { SetFilePointer(h, 0, nullptr, FILE_BEGIN); SetEndOfFile(h); }
    SetFilePointer(h, 0, nullptr, FILE_END);
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t ts[40];
    _snwprintf(ts, 40, L"[%04d-%02d-%02d %02d:%02d:%02d] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string line = W2U(std::wstring(ts) + msg) + "\r\n";
    DWORD w; WriteFile(h, line.data(), (DWORD)line.size(), &w, nullptr);
    CloseHandle(h);
}

static std::wstring Err(const wchar_t* what) { return std::wstring(what) + L" (오류 " + std::to_wstring(GetLastError()) + L")"; }

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

// 상대 경로면 gsHomeManagerLauncher.exe 폴더 기준으로 만든다 (X:\... 와 \\서버\... 는 그대로)
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
static bool HttpGet(const std::wstring& url, std::string& out)
{
    out.clear();
    wchar_t host[256], path[2048];
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    bool ok = false;
    HINTERNET s = WinHttpOpen(L"gsHomeManager-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 5000, 5000, 10000, 30000);            // 이름 확인, 연결, 송신, 수신 (ms)
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &code, &sz, WINHTTP_NO_HEADER_INDEX);
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
static void Update(const std::wstring& local, const std::wstring& verUrl, const std::wstring& exeUrl)
{
    std::string vt;
    if (!HttpGet(verUrl, vt)) { Log(Err(L"서버 version.txt 확인 실패 - 갱신 건너뜀")); return; }
    ULONGLONG sv = ParseVer(vt);
    if (!sv) { Log(L"서버 version.txt 형식 오류 - 갱신 건너뜀"); return; }

    ULONGLONG lv = GetFileVer(local);                           // exe 가 없으면 0 -> 내려받음
    if (lv >= sv) return;

    std::string data;
    if (!HttpGet(exeUrl, data)) { Log(Err(L"서버 exe 다운로드 실패")); return; }

    std::wstring tmp = local + L".new", old = local + L".old";
    if (!WriteFileAll(tmp, data)) { DeleteFileW(tmp.c_str()); Log(Err(L".new 파일 저장 실패")); return; }
    ULONGLONG tv = GetFileVer(tmp);                             // 받은 파일 자체의 버전으로 검증 (깨진 파일/HTML 오류 페이지/CDN 지연 방지)
    if (tv <= lv) {
        DeleteFileW(tmp.c_str());
        Log(L"받은 exe 의 버전을 읽을 수 없거나 PC 보다 높지 않음 (" + VerStr(tv) + L") - 갱신 취소");
        return;
    }

    // 실행 중인 exe 는 덮어쓸 수 없지만 이름 변경은 가능하다
    bool had = Exists(local);
    if (had && !MoveFileExW(local.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str()); Log(Err(L"기존 exe 이름 변경 실패 - 갱신 취소")); return;
    }
    if (!MoveFileExW(tmp.c_str(), local.c_str(), 0)) {
        Log(Err(L"새 exe 교체 실패 - 원래대로 복구"));
        if (had) MoveFileExW(old.c_str(), local.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(tmp.c_str());
        return;
    }
    Log(L"갱신 완료: " + VerStr(lv) + L" -> " + VerStr(tv));
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_dir = self;
    g_dir.resize(g_dir.find_last_of(L'\\') + 1);

    std::string ini = ReadAll(g_dir + L"gsHomeManagerLauncher.ini");
    std::wstring le = IniGet(ini, "LocalExe");
    std::wstring local = Resolve(le.empty() ? L"gsHomeManager.exe" : le);
    std::wstring verUrl = IniGet(ini, "ServerVersionUrl");
    std::wstring exeUrl = IniGet(ini, "ServerExeUrl");

    DeleteFileW((local + L".old").c_str());                     // 지난 갱신 때 남은 파일 (아직 실행 중이면 실패, 무시)
    if (!verUrl.empty() && !exeUrl.empty()) Update(local, verUrl, exeUrl);

    if (!Exists(local)) {
        Log(L"실행할 exe 가 없음: " + local);
        MessageBoxW(nullptr, (L"실행할 파일이 없습니다.\n" + local).c_str(), L"gsHomeManagerLauncher", MB_OK | MB_ICONERROR);
        return 2;
    }

    std::wstring cmd = L"\"" + local + L"\"";
    std::wstring dir = local.substr(0, local.find_last_of(L'\\'));
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(local.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        Log(Err(L"실행 실패"));
        MessageBoxW(nullptr, (L"실행하지 못했습니다.\n" + local).c_str(), L"gsHomeManagerLauncher", MB_OK | MB_ICONERROR);
        return 3;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
