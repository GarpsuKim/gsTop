// Gmail 단건 발송 (Windows / g++ / C++17 / libcurl) - UTF-8 전용
//
// 사용법: booting_Alert <메일파일(UTF-8)> [첨부폴더]              (앱 비밀번호는 booting_Alert.ini 의 GMAIL_APP_PASSWORD)
//         booting_Alert <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]   (기존 방식도 그대로 지원)
//
// 설정 파일: 실행 파일과 같은 폴더의 booting_Alert.ini  ("키=값", [섹션] 과 ; # 주석 허용)
//   각 값은 환경변수로 등록되며, 이미 설정된 환경변수가 있으면 그 값이 우선.
//   키 이름 = 환경변수 이름: GMAIL_USER, GMAIL_APP_PASSWORD, SOLAPI_*, KAKAO_*, TELEGRAM_*, KAKAO_ALERT_PHONE
//   수신자: EMAIL_TO=a@b.com,c@d.com  SOLAPI_TO=010...,010...  KAKAO=TRUE  TELEGRAM=TRUE  (쉼표 구분, @ 없으면 @gmail.com)
//
// 메일 파일 형식:
//   1줄 : 수신자 ID (예: hong 또는 hong@gmail.com)
//   2줄 : 제목
//   3줄~: 본문
//  (booting_Alert.ini 에 EMAIL_TO / SOLAPI_TO / KAKAO / TELEGRAM 이 하나라도 있으면 수신자는 ini 에서 읽고,
//   메일 파일은 1줄=제목, 2줄~=본문)
//
// 첨부폴더: 지정하면 그 폴더 바로 아래의 모든 파일을 첨부 (하위 폴더는 제외)
//           첨부 총량 약 18MB 까지 (Gmail 한도 25MB, Base64 증가분 고려)
//
// 빌드  : build.bat 참고 (g++ ... -lcurl)
// 명령줄 인자(비밀번호, 파일/폴더 경로)는 영문/ASCII 만 사용 (폴더 안 파일명은 한글 가능)
// 발신자: 환경변수 GMAIL_USER (없으면 아래 kSender)
//
// SMS (Solapi): 1줄 수신자에 전화번호(010-1234-5678 또는 01012345678)를 쓰면
//   그 번호로 "[제목] 본문" 을 문자 발송 (이메일과 쉼표로 섞어 쓸 수 있음, 첨부는 SMS 에 안 붙음)
//   한글 기준 45자(90바이트) 초과 시 LMS 로 자동 전환 (제목은 LMS 제목란, 최대 2000바이트)
//   전화번호만 있으면 Gmail 접속은 하지 않으므로 <앱비밀번호> 자리에는 아무 값이나 넣으면 됨
//   환경변수: SOLAPI_API_KEY, SOLAPI_API_SECRET, SOLAPI_FROM(등록된 발신번호, 숫자만)
//   빌드 시 -lbcrypt 추가 (HMAC-SHA256 서명 / 난수 생성용)
//
// 카카오톡 "나에게 보내기": 1줄 수신자에 kakao 라고 쓰면 본인 카카오톡 "나와의 채팅" 으로
//   "[제목] 본문" 발송 (최대 200자, 넘으면 잘림. 전화번호/이메일과 쉼표로 섞어 쓸 수 있음)
//   환경변수: KAKAO_REST_API_KEY, KAKAO_CLIENT_SECRET(클라이언트 시크릿 사용 시), KAKAO_LINK_URL(선택)
//   실행 파일과 같은 폴더의 kakao_token.txt 에 refresh_token 을 저장 (없거나 만료/권한 부족이면 자동 인증)
//   자동 인증: 브라우저가 열리고 동의하면 로컬 포트(KAKAO_REDIRECT_URI)로 code 를 받아 토큰 발급/저장
//     KAKAO_REDIRECT_URI (선택, 기본 http://localhost:8080/callback) 를 카카오 콘솔 Redirect URI 에 등록해야 함
//   인증만 따로 실행: booting_Alert --kakao-auth
//
// 텔레그램: 1줄 수신자에 telegram (또는 tg) 라고 쓰면 "[제목] 본문" 을 텔레그램 봇으로 발송
//   (이메일/전화번호/kakao 와 쉼표로 섞어 쓸 수 있음. 4096자 초과 시 나눠서 전송)
//   환경변수: TELEGRAM_BOT_TOKEN (BotFather 발급). chat id 는 봇에게 메시지를 한 번 보내 두면 자동 탐색/저장
//   (직접 지정하려면 TELEGRAM_CHAT_ID, 목록 확인: booting_Alert --telegram-chatid)
//   토큰 유지(갱신만): booting_Alert --kakao-keepalive   (작업 스케줄러에서 주기 실행)
//   무인 모드: KAKAO_NO_AUTH=1 이면 브라우저 인증을 시도하지 않고, KAKAO_ALERT_PHONE 이 있으면 SMS 로 알림
//   빌드 시 -lws2_32 -lshell32 추가

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static const char* kSender = "********@gmail.com";
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
        while (end > pos && end < s.size() && (s[end] & 0xC0) == 0x80) --end;
        if (end == pos) end = std::min(pos + 1, s.size());   // 무한 루프 방지
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

// 전화번호 판별: '-' 와 공백을 뺀 값이 0 으로 시작하는 10~11자리 숫자
static bool toPhone(const std::string& r, std::string& out) {
    out.clear();
    for (char c : r) if (c != '-' && c != ' ') out += c;
    if (out.size() < 10 || out.size() > 11 || out[0] != '0') return false;
    return std::all_of(out.begin(), out.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// 쉼표로 구분된 수신자 목록 파싱 (@ 없으면 @gmail.com 추가, 전화번호는 phones 로 분리)
static std::vector<std::string> parseRecipients(const std::string& line, std::vector<std::string>& phones, bool& kakao, bool& telegram) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= line.size()) {
        size_t e = line.find(',', p);
        if (e == std::string::npos) e = line.size();
        std::string r = trim(line.substr(p, e - p));
        std::string ph;
        std::string lower = r;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (lower == "kakao") kakao = true;
        else if (lower == "telegram" || lower == "tg") telegram = true;
        else if (toPhone(r, ph)) phones.push_back(ph);
        else if (!r.empty()) {
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

// ---- SMS (Solapi) ----
// EUC-KR 기준 바이트 수 (ASCII 1, 한글 등 2) - SMS/LMS 길이 기준
static size_t krLen(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) { if ((c & 0xC0) == 0x80) continue; n += (c < 0x80) ? 1 : 2; }
    return n;
}

// EUC-KR 기준 maxB 바이트 이내로 문자 경계에서 자름
static std::string krCut(const std::string& s, size_t maxB) {
    size_t n = 0, i = 0;
    while (i < s.size()) {
        size_t j = i + 1;
        while (j < s.size() && ((unsigned char)s[j] & 0xC0) == 0x80) ++j;
        size_t w = ((unsigned char)s[i] < 0x80) ? 1 : 2;
        if (n + w > maxB) break;
        n += w; i = j;
    }
    return s.substr(0, i);
}

static std::string jsonEsc(const std::string& s) {
    std::string o;
    char b[8];
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static std::string toHex(const unsigned char* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}

// HMAC-SHA256 (Windows CNG) -> 소문자 hex
static bool hmacSha256Hex(const std::string& key, const std::string& msg, std::string& hex) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return false;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char dig[32];
    bool ok = BCryptCreateHash(alg, &h, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) >= 0
           && BCryptHashData(h, (PUCHAR)msg.data(), (ULONG)msg.size(), 0) >= 0
           && BCryptFinishHash(h, dig, sizeof dig, 0) >= 0;
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (ok) hex = toHex(dig, sizeof dig);
    return ok;
}

// 요청마다 달라지는 무작위 salt (16바이트 -> hex 32자)
static bool randomSalt(std::string& salt) {
    unsigned char r[16];
    if (BCryptGenRandom(nullptr, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    salt = toHex(r, sizeof r);
    return true;
}

static size_t writeCb(char* p, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(p, size * nmemb);
    return size * nmemb;
}

// "[제목] 본문" 을 SMS 로, 90바이트 초과면 LMS(제목 별도)로 발송. 성공 0, 실패 2
static int sendSms(const std::vector<std::string>& phones, const std::string& subject, const std::string& body) {
    const char* key = std::getenv("SOLAPI_API_KEY");
    const char* secret = std::getenv("SOLAPI_API_SECRET");
    const char* from = std::getenv("SOLAPI_FROM");
    if (!key || !secret || !from) {
        puts("오류: SMS 환경변수 필요 (SOLAPI_API_KEY, SOLAPI_API_SECRET, SOLAPI_FROM)");
        return 2;
    }

    std::string full = "[" + subject + "] " + body;
    bool lms = krLen(full) > 90;
    std::string content = krCut(body, 2000);
    if (lms && content.size() < body.size()) puts("경고: 본문이 2000바이트를 넘어 잘렸습니다.");

    std::string json = "{\"messages\":[";
    for (size_t i = 0; i < phones.size(); ++i) {
        if (i) json += ",";
        json += "{\"to\":\"" + phones[i] + "\",\"from\":\"" + jsonEsc(from) + "\",\"type\":\"" + (lms ? "LMS" : "SMS") + "\"";
        if (lms) json += ",\"subject\":\"" + jsonEsc(krCut(subject, 40)) + "\",\"text\":\"" + jsonEsc(content) + "\"";
        else json += ",\"text\":\"" + jsonEsc(full) + "\"";
        json += "}";
    }
    json += "]}";

    // 인증: Authorization: HMAC-SHA256 apiKey=..., date=..., salt=..., signature=hex(HMAC(secret, date+salt))
    char date[32];
    std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    std::string salt, sig;
    if (!randomSalt(salt) || !hmacSha256Hex(secret, std::string(date) + salt, sig)) { puts("오류: 서명 계산 실패"); return 2; }
    std::string auth = std::string("Authorization: HMAC-SHA256 apiKey=") + key + ", date=" + date +
                       ", salt=" + salt + ", signature=" + sig;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* c = curl_easy_init();
    if (!c) { puts("오류: curl 초기화 실패"); curl_global_cleanup(); return 2; }

    curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json; charset=utf-8");
    hdr = curl_slist_append(hdr, auth.c_str());

    std::string resp;
    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, "https://api.solapi.com/messages/v4/send-many/detail");
    curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)json.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    printf("SMS(%s) %zu명 발송 요청 중... (%zu바이트)\n", lms ? "LMS" : "SMS", phones.size(), krLen(full));
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);

    // 접수 실패 건은 응답의 failedMessageList 에 담겨 옴 (비어 있으면 성공)
    bool failedItems = false;
    size_t p = resp.find("\"failedMessageList\"");
    if (p != std::string::npos && (p = resp.find('[', p)) != std::string::npos) {
        p = resp.find_first_not_of(" \r\n\t", p + 1);
        failedItems = (p != std::string::npos && resp[p] != ']');
    }
    bool ok = (rc == CURLE_OK && code >= 200 && code < 300 && !failedItems);
    if (ok) puts("SMS 접수 성공");
    else if (rc != CURLE_OK) printf("SMS 실패: %s\n", err[0] ? err : curl_easy_strerror(rc));
    else printf("SMS 실패 (HTTP %ld): %s\n", code, resp.c_str());

    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    curl_global_cleanup();
    return ok ? 0 : 2;
}

// ---- 카카오톡 "나에게 보내기" ----
// "[제목] 본문" 을 본인 카카오톡 "나와의 채팅" 으로 발송 (텍스트 템플릿, 최대 200자)

// 글자(UTF-8 문자) 수 기준으로 자름
static std::string cutChars(const std::string& s, size_t maxC) {
    size_t n = 0, i = 0;
    while (i < s.size()) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) { if (n == maxC) break; ++n; }
        ++i;
    }
    return s.substr(0, i);
}

// JSON 에서 "key":"value" 의 문자열 값을 꺼냄 (토큰처럼 이스케이프 없는 값 전용)
static std::string jsonStr(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = j.find('"', p);
    if (p == std::string::npos) return {};
    size_t e = j.find('"', p + 1);
    if (e == std::string::npos) return {};
    return j.substr(p + 1, e - p - 1);
}

// x-www-form-urlencoded POST. auth 가 비어 있지 않으면 그 헤더 한 줄을 추가
static bool postForm(const std::string& url, const std::string& auth, const std::string& form,
                     std::string& resp, long& code) {
    CURL* c = curl_easy_init();
    if (!c) { puts("오류: curl 초기화 실패"); return false; }
    curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded;charset=utf-8");
    if (!auth.empty()) hdr = curl_slist_append(hdr, auth.c_str());
    char err[CURL_ERROR_SIZE] = {0};
    resp.clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)form.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(c);
    code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (rc != CURLE_OK) printf("연결 실패: %s\n", err[0] ? err : curl_easy_strerror(rc));
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return rc == CURLE_OK;
}

// ---- 텔레그램 봇 (나에게 보내기) ----
// 환경변수: TELEGRAM_BOT_TOKEN (BotFather 에서 발급). TELEGRAM_CHAT_ID 는 선택.
// chat id 우선순위: 환경변수 > 실행 파일 폴더의 telegram_chat.txt > getUpdates 로 자동 탐색 후 telegram_chat.txt 에 저장
//   (자동 탐색은 봇과 대화한 개인 채팅이 정확히 1명일 때만 사용 - 낯선 사람에게 가는 것을 방지)
// "[제목] 본문" 을 발송. 4096자 제한이라 4000자 단위로 나눠 보냄. 성공 0, 실패 2
static std::wstring exeDirFile(const wchar_t* name) {
    wchar_t mp[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, mp, MAX_PATH);
    std::wstring d(mp, n);
    return d.substr(0, d.find_last_of(L"\\/") + 1) + name;
}

// getUpdates 응답에서 chat id 를 나온 순서대로 (오래된 것 -> 최근 것). curl_global_init 이 되어 있어야 함
static bool telegramFetchIds(const char* tok, std::vector<std::string>& ids) {
    std::string resp;
    long code = 0;
    if (!postForm(std::string("https://api.telegram.org/bot") + tok + "/getUpdates", "", "", resp, code)) return false;
    if (code != 200) { printf("텔레그램 조회 실패 (HTTP %ld): %s\n", code, resp.c_str()); return false; }
    const std::string k = "\"chat\":{\"id\":";
    size_t p = 0;
    while ((p = resp.find(k, p)) != std::string::npos) {
        p += k.size();
        size_t e = resp.find_first_not_of("-0123456789", p);
        std::string id = resp.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!id.empty()) ids.push_back(id);
    }
    return true;
}

static bool telegramResolveChat(const char* tok, std::string& chat) {
    const char* env = std::getenv("TELEGRAM_CHAT_ID");
    if (env && *env) { chat = env; return true; }

    std::wstring f = exeDirFile(L"telegram_chat.txt");
    std::string saved;
    if (readBin(f, saved) && !trim(saved).empty()) { chat = trim(saved); return true; }

    std::vector<std::string> ids;
    if (!telegramFetchIds(tok, ids)) return false;
    std::vector<std::string> priv;   // 개인 채팅(양수 id)만, 중복 제거
    for (const auto& id : ids)
        if (id[0] != '-' && std::find(priv.begin(), priv.end(), id) == priv.end()) priv.push_back(id);

    if (priv.empty()) {
        puts("텔레그램: 봇에게 먼저 아무 메시지나 보내세요. 그러면 나 자신의 chat id 를 자동으로 찾습니다.");
        return false;
    }
    if (priv.size() > 1) {
        puts("텔레그램: 봇과 대화한 사람이 여러 명입니다. 본인 id 를 TELEGRAM_CHAT_ID 에 직접 지정하세요 (booting_Alert --telegram-chatid).");
        return false;
    }
    chat = priv[0];
    FILE* fp = _wfopen(f.c_str(), L"wb");
    if (fp) { fwrite(chat.data(), 1, chat.size(), fp); fclose(fp); puts("텔레그램 chat id 를 자동으로 찾아 telegram_chat.txt 에 저장했습니다."); }
    return true;
}

static int sendTelegram(const std::string& subject, const std::string& body) {
    const char* tok = std::getenv("TELEGRAM_BOT_TOKEN");
    if (!tok) { puts("오류: 환경변수 TELEGRAM_BOT_TOKEN 이 필요합니다."); return 2; }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::string chat;
    if (!telegramResolveChat(tok, chat)) { curl_global_cleanup(); return 2; }

    std::string full = subject.empty() ? body : "[" + subject + "] " + body;
    std::string url = std::string("https://api.telegram.org/bot") + tok + "/sendMessage";   // 토큰은 화면에 출력하지 않음

    puts("텔레그램(나에게) 발송 요청 중...");
    int result = 0;
    size_t pos = 0;
    while (pos < full.size()) {
        std::string chunk = cutChars(full.substr(pos), 4000);
        if (chunk.empty()) break;
        std::string resp;
        long code = 0;
        std::string form = "chat_id=" + pctEncode(chat) + "&text=" + pctEncode(chunk);
        if (!postForm(url, "", form, resp, code)) { result = 2; break; }
        if (code != 200 || resp.find("\"ok\":true") == std::string::npos) {
            printf("텔레그램 발송 실패 (HTTP %ld): %s\n", code, resp.c_str());
            result = 2;
            break;
        }
        pos += chunk.size();
    }
    if (result == 0) puts("텔레그램 발송 성공");
    curl_global_cleanup();
    return result;
}

// 사용법: booting_Alert --telegram-chatid  (봇과 대화한 chat id 목록 확인용)
static int telegramChatIds() {
    const char* tok = std::getenv("TELEGRAM_BOT_TOKEN");
    if (!tok) { puts("오류: 환경변수 TELEGRAM_BOT_TOKEN 이 필요합니다."); return 2; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::vector<std::string> ids, uniq;
    bool ok = telegramFetchIds(tok, ids);
    curl_global_cleanup();
    if (!ok) return 2;
    for (const auto& id : ids) if (std::find(uniq.begin(), uniq.end(), id) == uniq.end()) uniq.push_back(id);
    if (uniq.empty()) { puts("받은 메시지가 없습니다. 텔레그램에서 봇에게 아무 메시지나 보낸 뒤 다시 실행하세요."); return 2; }
    for (const auto& id : uniq) printf("TELEGRAM_CHAT_ID=%s\n", id.c_str());
    return 0;
}

static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

static std::string queryParam(const std::string& q, const std::string& name) {
    size_t p = 0;
    while (p < q.size()) {
        size_t e = q.find('&', p);
        if (e == std::string::npos) e = q.size();
        std::string kv = q.substr(p, e - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == name) return urlDecode(kv.substr(eq + 1));
        p = e + 1;
    }
    return {};
}

// "http://localhost:8080/callback" -> port=8080, path="/callback"
static bool localPort(const std::string& uri, int& port, std::string& path) {
    const std::string pre = "http://localhost:";
    if (uri.compare(0, pre.size(), pre) != 0) return false;
    size_t i = pre.size();
    int p = 0;
    while (i < uri.size() && std::isdigit((unsigned char)uri[i])) p = p * 10 + (uri[i++] - '0');
    if (p <= 0 || p > 65535) return false;
    port = p;
    path = (i < uri.size()) ? uri.substr(i) : "/";
    return true;
}

// 로컬 포트에서 브라우저 리다이렉트를 받아 code 를 꺼냄
static bool waitForCode(int port, const std::string& path, const std::string& state,
                        std::string& code, std::string& errMsg, int timeoutSec) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { errMsg = "WSAStartup 실패"; return false; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { errMsg = "소켓 생성 실패"; WSACleanup(); return false; }

    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (sockaddr*)&a, sizeof a) == SOCKET_ERROR || listen(ls, 4) == SOCKET_ERROR) {
        errMsg = "포트 " + std::to_string(port) + " 를 사용할 수 없습니다 (다른 프로그램이 사용 중?)";
        closesocket(ls); WSACleanup();
        return false;
    }

    bool done = false, ok = false;
    auto deadline = Clock::now() + std::chrono::seconds(timeoutSec);
    while (!done && Clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ls, &fds);
        timeval tv{1, 0};
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET cs = accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) continue;

        char buf[8192];
        int n = recv(cs, buf, sizeof buf - 1, 0);
        std::string req = (n > 0) ? std::string(buf, n) : std::string();

        // "GET /callback?code=...&state=... HTTP/1.1"
        std::string target;
        size_t s1 = req.find(' ');
        if (s1 != std::string::npos) {
            size_t s2 = req.find(' ', s1 + 1);
            if (s2 != std::string::npos) target = req.substr(s1 + 1, s2 - s1 - 1);
        }
        size_t qp = target.find('?');
        std::string pathPart = target.substr(0, qp);
        std::string query = (qp == std::string::npos) ? std::string() : target.substr(qp + 1);

        std::string html;
        bool relevant = (pathPart == path);
        if (relevant) {
            std::string c = queryParam(query, "code");
            std::string e = queryParam(query, "error");
            if (!e.empty()) {
                errMsg = e + " " + queryParam(query, "error_description");
                done = true;
            } else if (!c.empty()) {
                if (queryParam(query, "state") == state) { code = c; ok = true; }
                else errMsg = "state 불일치 (잘못된 요청)";
                done = true;
            }
        }
        if (done) {
            html = std::string("<html><head><meta charset=\"utf-8\"></head><body><h3>") +
                   (ok ? "인증 완료. 이 창을 닫아도 됩니다." : "인증 실패. 콘솔 메시지를 확인하세요.") + "</h3></body></html>";
        } else {
            html = "<html><body>waiting</body></html>";
        }
        std::string r = std::string("HTTP/1.1 ") + (relevant ? "200 OK" : "404 Not Found") +
                        "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                        std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
        send(cs, r.data(), (int)r.size(), 0);
        closesocket(cs);
    }
    closesocket(ls);
    WSACleanup();
    if (!done) errMsg = "시간 초과 (브라우저에서 동의를 완료하지 않음)";
    return ok;
}

static std::wstring kakaoTokenPath() {
    wchar_t mp[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, mp, MAX_PATH);
    std::wstring tf(mp, n);
    return tf.substr(0, tf.find_last_of(L"\\/") + 1) + L"kakao_token.txt";
}

// 브라우저 인증 -> code -> 토큰 교환 -> kakao_token.txt 저장. 성공 true
// (curl_global_init 이 이미 호출된 상태여야 함)
static bool kakaoAuthorize(const std::string& key, const char* secret, const std::wstring& tf) {
    const char* ru = std::getenv("KAKAO_REDIRECT_URI");
    std::string redirect = ru ? ru : "http://localhost:8080/callback";

    std::string state;
    if (!randomSalt(state)) { puts("오류: 난수 생성 실패"); return false; }

    std::string url = "https://kauth.kakao.com/oauth/authorize?client_id=" + pctEncode(key) +
                      "&redirect_uri=" + pctEncode(redirect) +
                      "&response_type=code&scope=talk_message&state=" + state;

    puts("[카카오 인증] 브라우저를 엽니다. 로그인 후 '카카오톡 메시지 전송'에 동의하세요.");
    printf("  (자동으로 안 열리면 아래 주소를 직접 여세요)\n  %s\n", url.c_str());
    ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    std::string code, err;
    int port = 0;
    std::string path;
    if (localPort(redirect, port, path)) {
        puts("  동의 완료를 기다리는 중... (최대 3분)");
        if (!waitForCode(port, path, state, code, err, 180)) {
            printf("인증 실패: %s\n", err.c_str());
            return false;
        }
    } else {
        // localhost 가 아닌 Redirect URI: 주소창 전체 URL 을 붙여넣기
        puts("  동의 후 이동된 페이지의 주소창 URL 전체를 복사해서 붙여넣고 Enter:");
        char line[4096];
        if (!fgets(line, sizeof line, stdin)) return false;
        std::string u = trim(line);
        size_t q = u.find('?');
        code = queryParam(q == std::string::npos ? u : u.substr(q + 1), "code");
        if (code.empty()) { puts("오류: URL 에서 code 를 찾지 못했습니다."); return false; }
    }

    // code -> 토큰
    std::string form = "grant_type=authorization_code&client_id=" + pctEncode(key) +
                       "&redirect_uri=" + pctEncode(redirect) + "&code=" + pctEncode(code);
    if (secret) form += "&client_secret=" + pctEncode(secret);
    std::string resp;
    long hc = 0;
    if (!postForm("https://kauth.kakao.com/oauth/token", "", form, resp, hc)) return false;

    std::string rt = jsonStr(resp, "refresh_token");
    if (hc != 200 || rt.empty()) {
        printf("토큰 발급 실패 (HTTP %ld): %s\n", hc, resp.c_str());
        return false;
    }
    if (jsonStr(resp, "scope").find("talk_message") == std::string::npos) {
        puts("오류: 토큰에 talk_message 권한이 없습니다.");
        puts("  카카오 콘솔에서 [동의항목 > 카카오톡 메시지 전송] 설정과");
        puts("  [제품 설정 > 카카오톡 메시지] 활성화를 확인한 뒤 다시 실행하세요.");
        return false;
    }

    FILE* f = _wfopen(tf.c_str(), L"wb");
    if (!f) { puts("오류: kakao_token.txt 를 저장하지 못했습니다."); return false; }
    fwrite(rt.data(), 1, rt.size(), f);
    fclose(f);
    puts("[카카오 인증] 완료. refresh_token 을 kakao_token.txt 에 저장했습니다.");
    return true;
}

// "[제목] 본문" 발송. 토큰이 없거나 만료/권한 부족이면 자동으로 재인증 후 1회 재시도. 성공 0, 실패 2
static int sendKakao(const std::string& subject, const std::string& body, bool refreshOnly = false) {
    const char* keyEnv = std::getenv("KAKAO_REST_API_KEY");
    const char* secret = std::getenv("KAKAO_CLIENT_SECRET");
    const char* linkEnv = std::getenv("KAKAO_LINK_URL");
    if (!keyEnv) { puts("오류: 환경변수 KAKAO_REST_API_KEY 가 필요합니다."); return 2; }
    std::string key = keyEnv;
    std::string link = linkEnv ? linkEnv : "https://developers.kakao.com";   // 앱 플랫폼(Web)에 등록된 도메인

    std::wstring tf = kakaoTokenPath();
    const char* na = std::getenv("KAKAO_NO_AUTH");      // 1 이면 무인 모드: 브라우저 인증을 시도하지 않음
    bool noAuth = na && na[0] == '1';
    bool needAlert = false;

    std::string full = "[" + subject + "] " + body;
    std::string msg = cutChars(full, 200);
    if (msg.size() < full.size()) puts("경고: 카카오톡 메시지는 200자까지라 뒤쪽이 잘렸습니다.");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::string resp;
    long code = 0;
    int result = 2;
    bool reauthed = false;

    for (;;) {
        bool need = false;   // 재인증 필요 여부
        std::string rt;
        if (!readBin(tf, rt) || trim(rt).empty()) {
            puts("kakao_token.txt 가 없거나 비어 있습니다.");
            need = true;
        } else {
            rt = trim(rt);
            // 1) refresh_token -> access_token
            std::string form = "grant_type=refresh_token&client_id=" + pctEncode(key) + "&refresh_token=" + pctEncode(rt);
            if (secret) form += "&client_secret=" + pctEncode(secret);
            if (!postForm("https://kauth.kakao.com/oauth/token", "", form, resp, code)) break;   // 네트워크 오류
            std::string at = jsonStr(resp, "access_token");
            if (code != 200 || at.empty()) {
                printf("카카오 토큰 갱신 실패 (HTTP %ld): %s\n", code, resp.c_str());
                need = (resp.find("invalid_grant") != std::string::npos);
                if (!need) break;
            } else {
                std::string nrt = jsonStr(resp, "refresh_token");   // 남은 기간이 짧을 때만 새로 옴
                if (!nrt.empty()) {
                    FILE* f = _wfopen(tf.c_str(), L"wb");
                    if (f) { fwrite(nrt.data(), 1, nrt.size(), f); fclose(f); puts("카카오 refresh_token 갱신됨"); }
                    else printf("경고: 새 refresh_token 저장 실패. 직접 갱신하세요: %s\n", nrt.c_str());
                }

                if (refreshOnly) { puts("카카오 토큰 정상 (갱신만 수행)"); result = 0; break; }

                // 2) 나에게 보내기
                std::string tmpl = "{\"object_type\":\"text\",\"text\":\"" + jsonEsc(msg) + "\",\"link\":{\"web_url\":\"" +
                                   jsonEsc(link) + "\",\"mobile_web_url\":\"" + jsonEsc(link) + "\"}}";
                puts("카카오톡(나에게) 발송 요청 중...");
                if (!postForm("https://kapi.kakao.com/v2/api/talk/memo/default/send", "Authorization: Bearer " + at,
                              "template_object=" + pctEncode(tmpl), resp, code)) break;
                if (code == 200) { puts("카카오톡 발송 성공"); result = 0; break; }
                printf("카카오톡 발송 실패 (HTTP %ld): %s\n", code, resp.c_str());
                need = (resp.find("insufficient scopes") != std::string::npos);   // talk_message 권한 없음
                if (!need) break;
            }
        }

        if (!need) break;
        if (noAuth) {
            puts("카카오 재인증이 필요합니다 (무인 모드). PC에서 booting_Alert.exe --kakao-auth 를 실행하세요.");
            needAlert = true;
            break;
        }
        if (reauthed) break;
        reauthed = true;
        puts("카카오 재인증을 시작합니다...");
        if (!kakaoAuthorize(key, secret, tf)) break;
    }
    curl_global_cleanup();

    // 무인 모드에서 재인증이 필요하면 SMS 로 알림 (KAKAO_ALERT_PHONE + Solapi 환경변수 필요)
    if (needAlert) {
        const char* ap = std::getenv("KAKAO_ALERT_PHONE");
        std::string ph;
        if (ap && toPhone(ap, ph))
            sendSms(std::vector<std::string>{ph}, "booting_Alert", "카카오 재인증 필요. PC에서 booting_Alert.exe --kakao-auth 실행");
        if (std::getenv("TELEGRAM_BOT_TOKEN"))
            sendTelegram("booting_Alert", "카카오 재인증 필요. PC에서 booting_Alert.exe --kakao-auth 실행");
    }
    return result;
}

// 사용법: booting_Alert --kakao-auth   (인증만 단독 실행)
static int kakaoSetup() {
    const char* keyEnv = std::getenv("KAKAO_REST_API_KEY");
    const char* secret = std::getenv("KAKAO_CLIENT_SECRET");
    if (!keyEnv) { puts("오류: 환경변수 KAKAO_REST_API_KEY 가 필요합니다."); return 2; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    bool ok = kakaoAuthorize(keyEnv, secret, kakaoTokenPath());
    curl_global_cleanup();
    return ok ? 0 : 2;
}

// ---- booting_Alert.ini : 실행 파일과 같은 폴더. "키=값" 을 환경변수로 등록 (이미 있는 환경변수는 덮어쓰지 않음) ----
static void loadIni() {
    std::string t;
    if (!readBin(exeDirFile(L"booting_Alert.ini"), t)) return;
    if (t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    size_t pos = 0;
    std::string line;
    while (nextLine(t, pos, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (k.empty() || v.empty()) continue;
        if (!std::getenv(k.c_str())) _putenv_s(k.c_str(), v.c_str());
    }
}

static bool iniTrue(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    std::string s = trim(v);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s == "TRUE" || s == "1" || s == "Y" || s == "YES" || s == "ON";
}

// ini 의 EMAIL_TO / SOLAPI_TO / KAKAO / TELEGRAM 으로 수신자 줄("a@b.com,010...,kakao,telegram")을 만듦. 없으면 빈 문자열
static std::string iniRecipients() {
    std::string out;
    auto add = [&](const std::string& v) { if (!v.empty()) { if (!out.empty()) out += ","; out += v; } };
    const char* e = std::getenv("EMAIL_TO");
    if (e) add(trim(e));
    const char* ph = std::getenv("SOLAPI_TO");
    if (ph) add(trim(ph));
    if (iniTrue("KAKAO")) add("kakao");
    if (iniTrue("TELEGRAM")) add("telegram");
    return out;
}

// 수신자 목록처럼 보이는 줄인지 (ini 에 수신자를 두면 메일 파일 1줄은 제목이므로, 옛 수신자 줄이 남았는지 경고용)
static bool looksLikeRecipients(const std::string& line) {
    size_t p = 0;
    int n = 0;
    while (p <= line.size()) {
        size_t e = line.find(',', p);
        if (e == std::string::npos) e = line.size();
        std::string r = trim(line.substr(p, e - p));
        p = e + 1;
        if (r.empty()) continue;
        std::string low = r, ph;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (low == "kakao" || low == "telegram" || low == "tg" || toPhone(r, ph) ||
            (r.find('@') != std::string::npos && r.find(' ') == std::string::npos)) ++n;
        else return false;
    }
    return n > 0;
}

static bool isDir(const char* path) {
    DWORD a = GetFileAttributesW(wide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    loadIni();
    if (argc == 2 && std::string(argv[1]) == "--telegram-chatid") return telegramChatIds();
    if (argc == 2 && std::string(argv[1]) == "--kakao-auth") return kakaoSetup();
    if (argc == 2 && std::string(argv[1]) == "--kakao-keepalive") return sendKakao("", "", true);   // 토큰 갱신만(스케줄러용)
    auto t0 = Clock::now();

    // 인자 해석
    //   booting_Alert <메일파일> [첨부폴더]              (비밀번호는 ini/환경변수의 GMAIL_APP_PASSWORD)
    //   booting_Alert <앱비밀번호> <메일파일> [첨부폴더]   (기존 방식)
    const char* ep = std::getenv("GMAIL_APP_PASSWORD");
    std::string passStr = ep ? ep : "";
    const char* mailArg = nullptr;
    const char* attArg = nullptr;
    if (argc == 2) { mailArg = argv[1]; }
    else if (argc == 3 && isDir(argv[2])) { mailArg = argv[1]; attArg = argv[2]; }
    else if (argc == 3 || argc == 4) { passStr = argv[1]; mailArg = argv[2]; if (argc == 4) attArg = argv[3]; }
    else {
        puts("사용법: booting_Alert <메일파일(UTF-8)> [첨부폴더]   (앱 비밀번호는 booting_Alert.ini 의 GMAIL_APP_PASSWORD)");
        puts("        booting_Alert <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]");
        puts("메일 파일 형식: 1줄=수신자ID, 2줄=제목, 3줄~=본문  (ini 에 수신자가 있으면 1줄=제목, 2줄~=본문)");
        return 1;
    }

    std::string pass = passStr;
    pass.erase(std::remove(pass.begin(), pass.end(), ' '), pass.end());

    const char* env = std::getenv("GMAIL_USER");
    std::string from = env ? env : kSender;
    if (from.find('@') == std::string::npos) {
        puts("오류: 발신자 주소가 없습니다. 환경변수 GMAIL_USER 설정 또는 kSender 수정.");
        return 1;
    }

    // 메일 파일 파싱
    std::string text;
    if (!readMailFile(mailArg, text)) { puts("오류: 메일 파일을 열 수 없습니다."); return 1; }

    size_t pos = 0;
    std::string to, subject;
    std::string iniTo = iniRecipients();
    if (!iniTo.empty()) {
        // 수신자가 booting_Alert.ini 에 있음: 메일 파일 1줄=제목, 2줄~=본문
        to = iniTo;
        nextLine(text, pos, subject);
        if (looksLikeRecipients(trim(subject)))
            puts("경고: ini 에 수신자가 있으면 메일 파일 1줄은 제목입니다. 1줄의 옛 수신자 줄을 삭제하세요.");
    } else {
        // 기존 방식: 메일 파일 1줄=수신자, 2줄=제목, 3줄~=본문
        nextLine(text, pos, to);
        nextLine(text, pos, subject);
    }
/*
    to = trim(to);
    subject = trim(subject);
    if (to.empty()) { puts("오류: 파일 1줄에 수신자 ID 가 없습니다."); return 1; }
    if (to.find('@') == std::string::npos) to += "@gmail.com";
*/	

	subject = trim(subject);
    std::vector<std::string> phones;
    bool kakao = false, telegram = false;
    std::vector<std::string> rcpts = parseRecipients(to, phones, kakao, telegram);
    if (rcpts.empty() && phones.empty() && !kakao && !telegram) { puts("오류: 파일 1줄에 수신자 ID 가 없습니다."); return 1; }
    to.clear();   // 이후 To 헤더 및 로그 출력용: "<a>, <b>"
    for (size_t i = 0; i < rcpts.size(); ++i) to += (i ? ", <" : "<") + rcpts[i] + ">";
	

    if (subject.empty()) subject = "(제목 없음)";
    std::string body = toCrlf(text.substr(pos));   // 3줄째부터 전부 본문

    // 전화번호 수신자가 있으면 SMS, kakao 가 있으면 카카오톡(나에게) 발송 (이메일 수신자가 없으면 여기서 종료)
    int smsRc = 0;
    if (!phones.empty()) smsRc = sendSms(phones, subject, trim(text.substr(pos)));
    if (kakao && sendKakao(subject, trim(text.substr(pos))) != 0) smsRc = 2;
    if (telegram && sendTelegram(subject, trim(text.substr(pos))) != 0) smsRc = 2;
    if (rcpts.empty()) return smsRc;
    if (pass.empty()) { puts("오류: Gmail 앱 비밀번호가 없습니다. booting_Alert.ini 의 GMAIL_APP_PASSWORD 를 설정하거나 인자로 전달하세요."); return 1; }

    // 첨부 폴더
    std::vector<Attach> attachments;
    if (attArg) {
        std::string err;
        if (!loadFolder(attArg, attachments, err)) { puts(("오류: " + err).c_str()); return 1; }
    }

    auto t1 = Clock::now();

    // 메시지 조립 (Date, Message-ID 는 Gmail 이 자동으로 채움)
    const std::string B = "----=_booting_Alert_8f3c1a7e";   // Base64 문자에 없는 '-', '=', '_' 포함 -> 충돌 없음
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
    if (!curl) { puts("오류: curl 초기화 실패"); curl_global_cleanup(); return 2; }
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
    curl_off_t speed = 0;   // 바이트/초
    curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD_T, &speed);
    printf("시간: 읽기 %.2f초 / 인코딩 %.2f초 / 전송 %.2f초 (업로드 평균 %.2f MB/s)\n",
           sec(t0, t1), sec(t1, t2), sec(t2, t3), (double)speed / 1048576.0);
    if (up.started)
        printf("전송 세부: 연결·로그인 %.2f초 / 데이터 업로드 %.2f초 / 서버 응답 대기 %.2f초\n",
               sec(t2, up.first), sec(up.first, up.last), sec(up.last, t3));

    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return (rc == CURLE_OK && smsRc == 0) ? 0 : 2;
}