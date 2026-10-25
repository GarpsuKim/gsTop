// Gmail 단건 발송 (Windows / g++ / C++17 / libcurl) - UTF-8 전용
//
// 사용법: gmail_send <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]
//
// 메일 파일 형식:
//   1줄 : 수신자 ID (예: hong 또는 hong@gmail.com)
//   2줄 : 제목
//   3줄~: 본문
//
// 첨부폴더: 지정하면 그 폴더 바로 아래의 모든 파일을 첨부 (하위 폴더는 제외)
//           첨부 총량 약 18MB 까지 (Gmail 한도 25MB, Base64 증가분 고려)
//
// 빌드  : build.bat 참고 (g++ ... -lcurl)
// 명령줄 인자(비밀번호, 파일/폴더 경로)는 영문/ASCII 만 사용 (폴더 안 파일명은 한글 가능)
// 발신자: 환경변수 GMAIL_USER (없으면 아래 kSender)

#include <windows.h>
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char* kSender = "analogclockseoul@gmail.com";
static const uint64_t kMaxAttachBytes = 18ull * 1024 * 1024;

// ---- 문자열 유틸 ----
static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static bool isAscii(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return c >= 0; });
}

// ---- Base64 ----
static std::string b64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    uint32_t val = 0;
    int bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) { out += T[(val >> bits) & 63]; bits -= 6; }
    }
    if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}

// Base64 를 76자마다 CRLF 로 줄바꿈
static std::string wrap76(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 76 * 2 + 2);
    for (size_t i = 0; i < s.size(); i += 76) out += s.substr(i, 76) + "\r\n";
    return out;
}

// ---- 제목: 비ASCII 면 =?UTF-8?B?...?= (UTF-8 문자 경계에서 분할) ----
static std::string encodeSubject(const std::string& s) {
    if (isAscii(s)) return s;
    std::string out;
    for (size_t pos = 0; pos < s.size();) {
        size_t end = std::min(pos + 45, s.size());
        while (end < s.size() && (s[end] & 0xC0) == 0x80) --end;
        if (!out.empty()) out += ' ';
        out += "=?UTF-8?B?" + b64(s.substr(pos, end - pos)) + "?=";
        pos = end;
    }
    return out;
}

// ---- 첨부 파일명 인코딩 (RFC 2231: filename*=UTF-8''%XX...) ----
static std::string pctEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  c == '.' || c == '-' || c == '_';
        if (ok) out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

static std::string asciiFallback(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c < 32 || c > 126 || c == '"' || c == '\\') ? '_' : (char)c;
    return out;
}

// ---- 파일 읽기 ----
static bool readBin(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// 메일 파일: UTF-8, BOM 제거
static bool readMailFile(const char* path, std::string& out) {
    if (!readBin(wide(path), out)) return false;
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

// pos 위치에서 한 줄을 꺼냄 (CR/LF 제거). 더 읽을 줄이 없으면 false
static bool nextLine(const std::string& s, size_t& pos, std::string& line) {
    if (pos >= s.size()) return false;
    size_t e = s.find('\n', pos);
    if (e == std::string::npos) e = s.size();
    line = s.substr(pos, e - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pos = (e < s.size()) ? e + 1 : e;
    return true;
}

// 쉼표로 구분된 수신자 목록 파싱 (@ 없으면 @gmail.com 추가)
static std::vector<std::string> parseRecipients(const std::string& line) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= line.size()) {
        size_t e = line.find(',', p);
        if (e == std::string::npos) e = line.size();
        std::string r = trim(line.substr(p, e - p));
        if (!r.empty()) {
            if (r.find('@') == std::string::npos) r += "@gmail.com";
            v.push_back(r);
        }
        p = e + 1;
    }
    return v;
}

// 줄바꿈을 CRLF 로 통일
static std::string toCrlf(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') ++i; out += "\r\n"; }
        else if (s[i] == '\n') out += "\r\n";
        else out += s[i];
    }
    return out;
}

// ---- 첨부 폴더 읽기 (하위 폴더 제외) ----
struct Attach { std::string name; std::string data; };

static bool loadFolder(const char* dir, std::vector<Attach>& out, std::string& err) {
    std::wstring w = wide(dir);
    while (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { err = "첨부 폴더를 열 수 없습니다: " + std::string(dir); return false; }

    uint64_t total = 0;
    bool ok = true;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = utf8(fd.cFileName);
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (total + size > kMaxAttachBytes) { err = "첨부 총량이 18MB 를 넘습니다 (" + name + " 에서 초과)"; ok = false; break; }
        Attach a;
        a.name = name;
        if (!readBin(w + L"\\" + fd.cFileName, a.data)) { err = "파일을 읽을 수 없습니다: " + name; ok = false; break; }
        total += size;
        out.push_back(std::move(a));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// ---- libcurl 업로드 콜백 ----
using Clock = std::chrono::steady_clock;

struct Upload {
    const std::string* data;
    size_t pos;
    int pct;
    bool started;
    Clock::time_point first, last;   // 첫 데이터 요청 시각 / 마지막 데이터 전달 시각
};

static size_t readCb(char* ptr, size_t size, size_t nmemb, void* userp) {
    Upload* u = static_cast<Upload*>(userp);
    if (!u->started) { u->started = true; u->first = Clock::now(); }
    size_t n = std::min(size * nmemb, u->data->size() - u->pos);
    std::memcpy(ptr, u->data->data() + u->pos, n);
    u->pos += n;
    if (n > 0) u->last = Clock::now();
    int pct = u->data->empty() ? 100 : (int)(u->pos * 100 / u->data->size());
    if (pct >= u->pct + 5) { printf("\r  업로드 %d%%", pct); fflush(stdout); u->pct = pct; }
    return n;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    auto t0 = Clock::now();

    if (argc != 3 && argc != 4) {
        puts("사용법: gmail_send <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]");
        puts("메일 파일 형식: 1줄=수신자ID, 2줄=제목, 3줄~=본문");
        return 1;
    }

    std::string pass = argv[1];
    pass.erase(std::remove(pass.begin(), pass.end(), ' '), pass.end());

    const char* env = std::getenv("GMAIL_USER");
    std::string from = env ? env : kSender;
    if (from.find('@') == std::string::npos) {
        puts("오류: 발신자 주소가 없습니다. 환경변수 GMAIL_USER 설정 또는 kSender 수정.");
        return 1;
    }

    // 메일 파일 파싱
    std::string text;
    if (!readMailFile(argv[2], text)) { puts("오류: 메일 파일을 열 수 없습니다."); return 1; }

    size_t pos = 0;
    std::string to, subject;
    nextLine(text, pos, to);
    nextLine(text, pos, subject);
/*
    to = trim(to);
    subject = trim(subject);
    if (to.empty()) { puts("오류: 파일 1줄에 수신자 ID 가 없습니다."); return 1; }
    if (to.find('@') == std::string::npos) to += "@gmail.com";
*/	

	subject = trim(subject);
    std::vector<std::string> rcpts = parseRecipients(to);
    if (rcpts.empty()) { puts("오류: 파일 1줄에 수신자 ID 가 없습니다."); return 1; }
    to.clear();   // 이후 To 헤더 및 로그 출력용: "<a>, <b>"
    for (size_t i = 0; i < rcpts.size(); ++i) to += (i ? ", <" : "<") + rcpts[i] + ">";
	

    if (subject.empty()) subject = "(제목 없음)";
    std::string body = toCrlf(text.substr(pos));   // 3줄째부터 전부 본문

    // 첨부 폴더
    std::vector<Attach> attachments;
    if (argc == 4) {
        std::string err;
        if (!loadFolder(argv[3], attachments, err)) { puts(("오류: " + err).c_str()); return 1; }
    }

    auto t1 = Clock::now();

    // 메시지 조립 (Date, Message-ID 는 Gmail 이 자동으로 채움)
    const std::string B = "----=_gmail_send_8f3c1a7e";   // Base64 문자에 없는 '-', '=', '_' 포함 -> 충돌 없음
    std::string msg =
        "From: <" + from + ">\r\n"
        // "To: <" + to + ">\r\n"
		"To: " + to + "\r\n"
        "Subject: " + encodeSubject(subject) + "\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"" + B + "\"\r\n"
        "\r\n"
        "--" + B + "\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: base64\r\n"
        "\r\n" + wrap76(b64(body));

    for (const Attach& a : attachments) {
        msg += "--" + B + "\r\n"
               "Content-Type: application/octet-stream\r\n"
               "Content-Transfer-Encoding: base64\r\n"
               "Content-Disposition: attachment; filename=\"" + asciiFallback(a.name) + "\"";
        if (!isAscii(a.name)) msg += "; filename*=UTF-8''" + pctEncode(a.name);
        msg += "\r\n\r\n" + wrap76(b64(a.data));
    }
    msg += "--" + B + "--\r\n";

    auto t2 = Clock::now();

    // 전송
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    char err[CURL_ERROR_SIZE] = {0};
    Upload up{&msg, 0, 0, false, {}, {}};
    //  curl_slist* rcpt = curl_slist_append(nullptr, ("<" + to + ">").c_str());
	
    curl_slist* rcpt = nullptr;
    for (const auto& r : rcpts) rcpt = curl_slist_append(rcpt, ("<" + r + ">").c_str());	

    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(curl, CURLOPT_USERNAME, from.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + from + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)msg.size());
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 524288L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);

    printf("%s -> %s 전송 중... (메시지 %.1f MB)\n", from.c_str(), to.c_str(), msg.size() / 1048576.0);
    for (const Attach& a : attachments)
        printf("  첨부: %s (%zu KB)\n", a.name.c_str(), (a.data.size() + 1023) / 1024);

    CURLcode rc = curl_easy_perform(curl);
    auto t3 = Clock::now();
    printf("\n");
    if (rc == CURLE_OK) puts("전송 성공");
    else printf("전송 실패: %s\n", err[0] ? err : curl_easy_strerror(rc));

    auto sec = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
    double speed = 0;
    curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &speed);
    printf("시간: 읽기 %.2f초 / 인코딩 %.2f초 / 전송 %.2f초 (업로드 평균 %.2f MB/s)\n",
           sec(t0, t1), sec(t1, t2), sec(t2, t3), speed / 1048576.0);
    if (up.started)
        printf("전송 세부: 연결·로그인 %.2f초 / 데이터 업로드 %.2f초 / 서버 응답 대기 %.2f초\n",
               sec(t2, up.first), sec(up.first, up.last), sec(up.last, t3));

    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return rc == CURLE_OK ? 0 : 2;
}
