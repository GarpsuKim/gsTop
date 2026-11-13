// make_home.cpp - screen_telegram.ini 를 읽어 Home.txt 를 만든다. (Windows 11 / MinGW-w64 g++, CLI)
//
// Home.txt 에는 [HomeBack] / [NotHome] 두 항목의 sendMessage 호출 URL 이 들어간다:
//   https://api.telegram.org/bot<TELEGRAM_BOT_TOKEN>/sendMessage?chat_id=<TELEGRAM_CHAT_ID>&text=/HomeBack
//
// 사용법:  make_home.exe [screen_telegram.ini 경로]
//   인자가 없으면 exe 폴더 -> 현재 폴더의 screen_telegram.ini 를 찾는다. (screen_telegram.exe 와 같은 규칙)
//   Home.txt 는 INI 파일이 있는 폴더에 만든다. 이미 있으면 덮어쓴다.
//
// 빌드:
//   g++ -O2 -std=c++17 -municode -o make_home.exe make_home.cpp -static -static-libgcc -static-libstdc++
//
// 주의: Home.txt 에는 봇 토큰이 평문으로 들어간다. 공유/업로드 금지 (노출 시 BotFather 에서 /revoke).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

static std::string upper(std::string s)
{
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return s;
}

// 값 뒤의 "공백 + ; 또는 #" 는 주석 (screen_telegram.ini 규칙)
static std::string stripComment(const std::string& v)
{
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t'))
            return trim(v.substr(0, i));
    return trim(v);
}

// URL 에 안전하지 않은 바이트만 %XX 로 바꾼다. (정상적인 토큰/chat id 는 그대로 나온다)
static std::string urlEncode(const std::string& s)
{
    static const char* const hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~' || c == ':' || c == '@';
        if (safe) {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

// INI 에서 두 키를 읽는다. 같은 키가 여러 번 있으면 마지막 값. 파일을 못 열면 false.
static bool readIni(const fs::path& path, std::string& token, std::string& chatId)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        data.erase(0, 3); // UTF-8 BOM

    size_t pos = 0;
    while (pos <= data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        const std::string t = trim(data.substr(pos, nl - pos)); // \r 도 trim 에서 제거됨
        pos = nl + 1;
        if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = upper(trim(t.substr(0, eq)));
        const std::string val = stripComment(t.substr(eq + 1));
        if (key == "TELEGRAM_BOT_TOKEN") token = val;
        else if (key == "TELEGRAM_CHAT_ID") chatId = val;
    }
    return true;
}

static std::string u8(const fs::path& p)
{
    return p.u8string();
}

static int run(const fs::path& arg, const fs::path& exeDir)
{
    fs::path ini;
    if (!arg.empty()) {
        ini = arg;
    } else {
        const fs::path p1 = exeDir / "screen_telegram.ini";
        std::error_code ec;
        ini = fs::exists(p1, ec) ? p1 : fs::path("screen_telegram.ini");
    }

    std::string token, chatId;
    if (!readIni(ini, token, chatId)) {
        printf("INI 파일을 열 수 없습니다: %s\n사용법: make_home.exe [screen_telegram.ini 경로]\n", u8(ini).c_str());
        return 1;
    }
    if (token.empty() || chatId.empty()) {
        printf("INI 에 %s%s%s 값이 없습니다: %s\n", token.empty() ? "TELEGRAM_BOT_TOKEN" : "",
               token.empty() && chatId.empty() ? ", " : "", chatId.empty() ? "TELEGRAM_CHAT_ID" : "",
               u8(ini).c_str());
        return 1;
    }

    const std::string base = "https://api.telegram.org/bot" + urlEncode(token) +
                             "/sendMessage?chat_id=" + urlEncode(chatId) + "&text=";
    const std::string out = "[HomeBack]\r\n" + base + "/HomeBack\r\n"
                            "\r\n\r\n"
                            "[NotHome]\r\n" + base + "/NotHome\r\n";

    fs::path dir = ini.parent_path();
    const fs::path outPath = (dir.empty() ? fs::path(".") : dir) / "Home.txt";
    std::ofstream o(outPath, std::ios::binary | std::ios::trunc);
    if (!o || !o.write(out.data(), (std::streamsize)out.size())) {
        printf("Home.txt 를 쓸 수 없습니다: %s\n", u8(outPath).c_str());
        return 1;
    }
    o.close();
    printf("Home.txt 를 만들었습니다: %s\n", u8(outPath).c_str());
    printf("주의: 이 파일에는 봇 토큰이 들어 있으니 공유/업로드하지 마세요.\n");
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8); // 한글 메시지/경로 출력
    wchar_t exe[MAX_PATH * 4] = {};
    GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    return run(argc >= 2 ? fs::path(argv[1]) : fs::path(), fs::path(exe).parent_path());
}
#else
int main(int argc, char** argv) // 리눅스에서 로직 시험용
{
    return run(argc >= 2 ? fs::path(argv[1]) : fs::path(), fs::absolute(argv[0]).parent_path());
}
#endif
