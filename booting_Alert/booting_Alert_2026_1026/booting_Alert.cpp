// booting_Alert - 부팅/이벤트 알림 발송 (Windows / g++ / C++17 / libcurl) - UTF-8 전용
//   Gmail(SMTP) / SMS·LMS(Solapi) / 카카오톡 나에게 / 텔레그램 나에게 로 같은 내용을 보냄
//
// 사용법: booting_Alert <메일파일(UTF-8)> [첨부폴더]              (앱 비밀번호는 booting_Alert.ini 의 GMAIL_APP_PASSWORD)
//         booting_Alert <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]   (기존 방식도 지원)
//         booting_Alert --kakao-auth        카카오 인증만 단독 실행 (브라우저 동의, 최초 1회)
//         booting_Alert --kakao-keepalive   카카오 토큰 유지(갱신만). 작업 스케줄러에서 주기 실행
//         booting_Alert --telegram-chatid   텔레그램 chat id 목록 확인
//
// 메일 파일 형식:
//   1줄 : 수신자 ID (예: hong 또는 hong@gmail.com)
//   2줄 : 제목
//   3줄~: 본문
//  (booting_Alert.ini 에 EMAIL_TO / SOLAPI_TO / KAKAO / TELEGRAM 이 하나라도 있으면 수신자는 ini 에서 읽고,
//   메일 파일은 1줄=제목, 2줄~=본문)
//
// 설정 파일: 실행 파일과 같은 폴더의 booting_Alert.ini  ("키=값", [섹션] 과 ; # 주석 허용)
//   - 같은 이름의 환경변수가 있으면 환경변수가 우선
//   - 주석은 줄 맨 앞(; 또는 #) 또는 값 뒤에 "공백 + ; 또는 #" (값 안의 ; # 는 공백이 앞에 없으면 값으로 취급)
//   - 수신자  : EMAIL_TO, EMAIL_BCC, SOLAPI_TO, KAKAO(TRUE/FALSE), TELEGRAM(TRUE/FALSE)  (쉼표 구분, @ 없으면 @gmail.com)
//   - Gmail   : GMAIL_USER, GMAIL_APP_PASSWORD
//   - Solapi  : SOLAPI_API_KEY, SOLAPI_API_SECRET, SOLAPI_FROM(등록된 발신번호, 숫자만)
//   - 카카오  : KAKAO_REST_API_KEY, KAKAO_CLIENT_SECRET, KAKAO_REDIRECT_URI, KAKAO_LINK_URL, KAKAO_NO_AUTH
//   - 텔레그램: TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID(선택)
//   - 동작    : NET_WAIT_SEC(기본 60), RETRY_COUNT(기본 2), RETRY_DELAY_SEC(기본 10),
//               KAKAO_ALERT_PHONE, ALERT_INTERVAL_HOURS(기본 24)
//
// 발송 방식: 서비스(SMS / 텔레그램 / 이메일 / 카카오)마다 별도 쓰레드를 만들어 동시에 발송 (순서 없음)
//   - 각 쓰레드는 자기 서비스의 호스트가 DNS 로 확인될 때까지 NET_WAIT_SEC 초까지 대기 (부팅 직후 대비)
//   - 연결 오류로 실패하면 RETRY_DELAY_SEC 초 간격으로 RETRY_COUNT 회 재시도 (설정/인증 오류는 재시도 안 함)
//   - 발송 결과(성공/실패)는 확인하지 않음. 모든 쓰레드가 끝날 때까지 기다리기만 함 (먼저 종료되면 쓰레드가 중간에 죽음)
//   - 종료 코드: 0=발송 시도 완료(결과 무관), 1=설정/인자 오류(발송 시도 전)
//
// 이메일 : 첨부폴더를 지정하면 그 폴더 바로 아래의 모든 파일을 첨부 (하위 폴더 제외, 총 약 18MB 까지)
//          EMAIL_BCC 는 To 헤더에 나타나지 않는 숨은 참조. 명령줄 인자는 영문/ASCII 만 사용
// SMS    : 전화번호가 있으면 "[제목] 본문" 발송. 한글 45자(90바이트) 초과 시 LMS 로 자동 전환
// 카카오 : "나에게 보내기" (최대 200자). 실행 파일 폴더의 kakao_token.txt 사용, 없거나 만료되면 자동 인증.
//          KAKAO_NO_AUTH=TRUE (무인 모드): 브라우저 인증 대신 SMS/텔레그램으로 재인증 필요 알림
//          (같은 알림은 ALERT_INTERVAL_HOURS 안에는 반복하지 않음)
// 텔레그램: 봇이 "나에게" 발송 (4096자 초과 시 분할). chat id 는 봇에게 메시지를 보내 두면 자동 탐색/저장
//
// 빌드: build.bat 참고 (g++ ... -lcurl -lbcrypt -lws2_32 -lshell32)
//
// ============================================================================
// 구조 (지금은 한 파일, 나중에 아래 단위로 .h/.cpp 분리 예정. 각 구역 머리의 [파일명] 이 분리 후보)
//
//   util            [util.h]              문자열/인코딩/파일/해시 등 순수 함수 (namespace util)
//   Config          [config.h/.cpp]       ini + 환경변수 설정
//   Http            [http.h/.cpp]         libcurl POST 래퍼
//   Message/SendResult/Notifier [notifier.h]   모든 서비스가 구현하는 공통 인터페이스
//   Recipients      [recipients.h/.cpp]   수신자 파싱
//   SmsNotifier     [sms_notifier.*]      Solapi SMS/LMS
//   TelegramNotifier[telegram_notifier.*] 텔레그램 봇
//   EmailNotifier   [email_notifier.*]    Gmail SMTP
//   LocalCallbackServer [local_callback_server.*]  OAuth 리다이렉트 수신용 로컬 서버
//   KakaoNotifier   [kakao_notifier.*]    카카오톡 나에게 보내기 + OAuth 인증
//   NetworkWaiter / RetryPolicy / Dispatcher [dispatcher.*]  네트워크 대기, 재시도, 서비스별 쓰레드 동시 발송
//   App / main      [main.cpp]            인자 해석과 전체 흐름
//
//  서비스 클래스끼리는 서로 참조하지 않음 (카카오의 재인증 알림은 App 이 넘겨주는 AlertFn 콜백으로 연결).
//  새 서비스를 추가하려면 Notifier 를 상속해 name() / hosts() / send() 를 구현하고 App::run 에서 Dispatcher 에 add 하면 됨.
// ============================================================================

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
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;

// ============================================================================
// [util.h] 순수 함수 모음
// ============================================================================
namespace util {
	
	inline std::string trim(const std::string& s) {
		size_t b = 0, e = s.size();
		while (b < e && (unsigned char)s[b] <= ' ') ++b;
		while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
		return s.substr(b, e - b);
	}
	
	inline std::string toLower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}
	
	inline std::string toUpper(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
		return s;
	}
	
	inline std::string utf8(const std::wstring& w) {
		if (w.empty()) return {};
		int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
		return s;
	}
	
	inline std::wstring wide(const std::string& s) {
		if (s.empty()) return {};
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}
	
	inline bool isAscii(const std::string& s) {
		return std::all_of(s.begin(), s.end(), [](char c) { return c >= 0; });
	}
	
	// ---- Base64 ----
	inline std::string b64(const std::string& in) {
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
	inline std::string wrap76(const std::string& s) {
		std::string out;
		out.reserve(s.size() + s.size() / 76 * 2 + 2);
		for (size_t i = 0; i < s.size(); i += 76) out += s.substr(i, 76) + "\r\n";
		return out;
	}
	
	// 퍼센트 인코딩 (RFC 3986 unreserved 만 그대로)
	inline std::string pctEncode(const std::string& s) {
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
	
	inline std::string urlDecode(const std::string& s) {
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
	
	// "a=1&b=2" 에서 name 의 값 (URL 디코드)
	inline std::string queryParam(const std::string& q, const std::string& name) {
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
	
	// ---- 파일 ----
	inline bool readBin(const std::wstring& path, std::string& out) {
		FILE* f = _wfopen(path.c_str(), L"rb");
		if (!f) return false;
		char buf[65536];
		size_t n;
		out.clear();
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
		fclose(f);
		return true;
	}
	
	inline bool writeBin(const std::wstring& path, const std::string& data) {
		FILE* f = _wfopen(path.c_str(), L"wb");
		if (!f) return false;
		fwrite(data.data(), 1, data.size(), f);
		fclose(f);
		return true;
	}
	
	// 실행 파일과 같은 폴더의 파일 경로
	inline std::wstring exeDirFile(const wchar_t* name) {
		wchar_t mp[MAX_PATH];
		DWORD n = GetModuleFileNameW(nullptr, mp, MAX_PATH);
		std::wstring d(mp, n);
		return d.substr(0, d.find_last_of(L"\\/") + 1) + name;
	}
	
	inline bool isDir(const std::string& path) {
		DWORD a = GetFileAttributesW(wide(path).c_str());
		return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
	}
	
	// pos 위치에서 한 줄을 꺼냄 (CR/LF 제거). 더 읽을 줄이 없으면 false
	inline bool nextLine(const std::string& s, size_t& pos, std::string& line) {
		if (pos >= s.size()) return false;
		size_t e = s.find('\n', pos);
		if (e == std::string::npos) e = s.size();
		line = s.substr(pos, e - pos);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		pos = (e < s.size()) ? e + 1 : e;
		return true;
	}
	
	// 줄바꿈을 CRLF 로 통일
	inline std::string toCrlf(const std::string& s) {
		std::string out;
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') ++i; out += "\r\n"; }
			else if (s[i] == '\n') out += "\r\n";
			else out += s[i];
		}
		return out;
	}
	
	// 전화번호 판별: '-' 와 공백을 뺀 값이 0 으로 시작하는 10~11자리 숫자
	inline bool toPhone(const std::string& r, std::string& out) {
		out.clear();
		for (char c : r) if (c != '-' && c != ' ') out += c;
		if (out.size() < 10 || out.size() > 11 || out[0] != '0') return false;
		return std::all_of(out.begin(), out.end(), [](char c) { return c >= '0' && c <= '9'; });
	}
	
	// ---- 길이 계산 / 자르기 ----
	// EUC-KR 기준 바이트 수 (ASCII 1, 한글 등 2) - SMS/LMS 길이 기준
	inline size_t krLen(const std::string& s) {
		size_t n = 0;
		for (unsigned char c : s) { if ((c & 0xC0) == 0x80) continue; n += (c < 0x80) ? 1 : 2; }
		return n;
	}
	
	// EUC-KR 기준 maxB 바이트 이내로 문자 경계에서 자름
	inline std::string krCut(const std::string& s, size_t maxB) {
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
	
	// 글자(UTF-8 문자) 수 기준으로 자름
	inline std::string cutChars(const std::string& s, size_t maxC) {
		size_t n = 0, i = 0;
		while (i < s.size()) {
			if (((unsigned char)s[i] & 0xC0) != 0x80) { if (n == maxC) break; ++n; }
			++i;
		}
		return s.substr(0, i);
	}
	
	// ---- JSON (최소) ----
	inline std::string jsonEsc(const std::string& s) {
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
	
	// JSON 에서 "key":"value" 의 문자열 값을 꺼냄 (토큰처럼 이스케이프 없는 값 전용)
	inline std::string jsonStr(const std::string& j, const char* key) {
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
	
	// ---- 해시 / 난수 (Windows CNG) ----
	inline std::string toHex(const unsigned char* p, size_t n) {
		static const char* H = "0123456789abcdef";
		std::string s;
		for (size_t i = 0; i < n; ++i) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
		return s;
	}
	
	// HMAC-SHA256 -> 소문자 hex
	inline bool hmacSha256Hex(const std::string& key, const std::string& msg, std::string& hex) {
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
	
	// 요청마다 달라지는 무작위 값 (16바이트 -> hex 32자)
	inline bool randomSalt(std::string& salt) {
		unsigned char r[16];
		if (BCryptGenRandom(nullptr, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
		salt = toHex(r, sizeof r);
		return true;
	}
	
}  // namespace util

// libcurl 전역 초기화/정리 (프로그램 전체에서 한 번)
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

// ============================================================================
// [config.h] 설정: booting_Alert.ini + 환경변수 (환경변수 우선)
// ============================================================================
class Config {
	public:
    bool loadIni(const std::wstring& path);
    std::string get(const std::string& key, const std::string& def = "") const;
    bool has(const std::string& key) const { return !get(key).empty(); }
    bool getBool(const std::string& key) const;   // TRUE / 1 / Y / YES / ON
    int getInt(const std::string& key, int def) const;
	
	private:
    std::map<std::string, std::string> ini_;
};

// ---- [config.cpp] ----
bool Config::loadIni(const std::wstring& path) {
    std::string t;
    if (!util::readBin(path, t)) return false;
    if (t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    size_t pos = 0;
    std::string line;
    while (util::nextLine(t, pos, line)) {
        line = util::trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = util::trim(line.substr(0, eq)), v = util::trim(line.substr(eq + 1));
        if (!v.empty() && v[0] == '"') {
            size_t q = v.find('"', 1);                      // "값" 뒤의 주석은 무시
            if (q != std::string::npos) v = v.substr(1, q - 1);
			} else {
            for (size_t i = 1; i < v.size(); ++i)           // 공백/탭 뒤의 ; 또는 # 는 줄 끝 주석
			if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) { v = util::trim(v.substr(0, i)); break; }
		}
        if (k.empty() || v.empty()) continue;
        ini_[k] = v;
	}
    return true;
}

std::string Config::get(const std::string& key, const std::string& def) const {
    const char* e = std::getenv(key.c_str());
    if (e && *e) return e;
    auto it = ini_.find(key);
    if (it != ini_.end() && !it->second.empty()) return it->second;
    return def;
}

bool Config::getBool(const std::string& key) const {
    std::string s = util::toUpper(util::trim(get(key)));
    return s == "TRUE" || s == "1" || s == "Y" || s == "YES" || s == "ON";
}

int Config::getInt(const std::string& key, int def) const {
    std::string t = util::trim(get(key));
    if (t.empty()) return def;
    char* e = nullptr;
    long n = std::strtol(t.c_str(), &e, 10);
    return (e && *e == '\0') ? (int)n : def;
}

// ============================================================================
// [http.h] libcurl POST 래퍼
// ============================================================================
struct HttpResponse {
    bool ok = false;      // 통신 자체 성공 여부 (false 면 연결/네트워크 오류)
    long status = 0;      // HTTP 상태 코드
    std::string body;
    std::string error;    // ok=false 일 때 원인
};

class Http {
	public:
    // headers: "Name: value" 형식의 전체 줄
    static HttpResponse postForm(const std::string& url, const std::string& form,
	const std::vector<std::string>& headers = {});
    static HttpResponse postJson(const std::string& url, const std::string& json,
	const std::vector<std::string>& headers = {});
	
	private:
    static HttpResponse post(const std::string& url, const std::string& body, const char* contentType,
	const std::vector<std::string>& headers);
    static size_t writeCb(char* p, size_t size, size_t nmemb, void* userp);
};

// ---- [http.cpp] ----
size_t Http::writeCb(char* p, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(p, size * nmemb);
    return size * nmemb;
}

HttpResponse Http::post(const std::string& url, const std::string& body, const char* contentType,
	const std::vector<std::string>& headers) {
    HttpResponse r;
    CURL* c = curl_easy_init();
    if (!c) { r.error = "curl 초기화 실패"; return r; }
    curl_slist* hdr = curl_slist_append(nullptr, contentType);
    for (const auto& h : headers) hdr = curl_slist_append(hdr, h.c_str());
    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    r.ok = (rc == CURLE_OK);
    if (!r.ok) r.error = err[0] ? err : curl_easy_strerror(rc);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return r;
}

HttpResponse Http::postForm(const std::string& url, const std::string& form, const std::vector<std::string>& headers) {
    return post(url, form, "Content-Type: application/x-www-form-urlencoded;charset=utf-8", headers);
}

HttpResponse Http::postJson(const std::string& url, const std::string& json, const std::vector<std::string>& headers) {
    return post(url, json, "Content-Type: application/json; charset=utf-8", headers);
}

// ============================================================================
// [notifier.h] 공통 인터페이스
// ============================================================================
struct Message {
    std::string subject;
    std::string plain;      // 줄바꿈 원본, 앞뒤 공백 제거 (SMS / 텔레그램 / 카카오)
    std::string bodyCrlf;   // CRLF 로 통일한 본문 (이메일)
};

struct SendResult {
    int code = 0;             // 0=성공, 1=설정/인자 오류, 2=발송 실패
    bool retryable = false;   // 연결 오류처럼 다시 시도해 볼 만한 실패인지
    static SendResult ok() { return {0, false}; }
    static SendResult configError() { return {1, false}; }
    static SendResult fail(bool retryable = false) { return {2, retryable}; }
};

class Notifier {
	public:
    virtual ~Notifier() = default;
    virtual const char* name() const = 0;
    virtual std::vector<std::string> hosts() const { return {}; }   // 네트워크 준비 확인용 호스트
    virtual SendResult send(const Message& m) = 0;
};

// ============================================================================
// [recipients.h] 수신자 파싱
// ============================================================================
class Recipients {
	public:
    std::vector<std::string> emails;
    std::vector<std::string> phones;
    bool kakao = false;
    bool telegram = false;
	
    // "a@b.com, 010-1234-5678, kakao, telegram" 형태의 쉼표 구분 목록 (@ 없으면 @gmail.com)
    static Recipients parse(const std::string& line);
    // ini/환경변수의 EMAIL_TO / SOLAPI_TO / KAKAO / TELEGRAM 으로 같은 형태의 줄을 만듦 (없으면 빈 문자열)
    static std::string lineFromConfig(const Config& cfg);
    // 수신자 목록처럼 보이는 줄인지 (ini 에 수신자가 있는데 메일 파일에 옛 수신자 줄이 남았는지 경고용)
    static bool looksLikeList(const std::string& line);
	
    bool empty() const { return emails.empty() && phones.empty() && !kakao && !telegram; }
};

// ---- [recipients.cpp] ----
Recipients Recipients::parse(const std::string& line) {
    Recipients out;
    size_t p = 0;
    while (p <= line.size()) {
        size_t e = line.find(',', p);
        if (e == std::string::npos) e = line.size();
        std::string r = util::trim(line.substr(p, e - p));
        std::string ph, lower = util::toLower(r);
        if (lower == "kakao") out.kakao = true;
        else if (lower == "telegram" || lower == "tg") out.telegram = true;
        else if (util::toPhone(r, ph)) out.phones.push_back(ph);
        else if (!r.empty()) {
            if (r.find('@') == std::string::npos) r += "@gmail.com";
            out.emails.push_back(r);
		}
        p = e + 1;
	}
    return out;
}

std::string Recipients::lineFromConfig(const Config& cfg) {
    std::string out;
    auto add = [&](const std::string& v) { if (!v.empty()) { if (!out.empty()) out += ","; out += v; } };
    add(util::trim(cfg.get("EMAIL_TO")));
    add(util::trim(cfg.get("SOLAPI_TO")));
    if (cfg.getBool("KAKAO")) add("kakao");
    if (cfg.getBool("TELEGRAM")) add("telegram");
    return out;
}

bool Recipients::looksLikeList(const std::string& line) {
    size_t p = 0;
    int n = 0;
    while (p <= line.size()) {
        size_t e = line.find(',', p);
        if (e == std::string::npos) e = line.size();
        std::string r = util::trim(line.substr(p, e - p));
        p = e + 1;
        if (r.empty()) continue;
        std::string low = util::toLower(r), ph;
        if (low == "kakao" || low == "telegram" || low == "tg" || util::toPhone(r, ph) ||
		(r.find('@') != std::string::npos && r.find(' ') == std::string::npos)) ++n;
        else return false;
	}
    return n > 0;
}

// ============================================================================
// [sms_notifier.h] Solapi SMS/LMS
// ============================================================================
class SmsNotifier : public Notifier {
	public:
    SmsNotifier(const Config& cfg, std::vector<std::string> phones) : cfg_(cfg), phones_(std::move(phones)) {}
    const char* name() const override { return "SMS"; }
    std::vector<std::string> hosts() const override { return {"api.solapi.com"}; }
    // "[제목] 본문" 을 SMS 로, 90바이트 초과면 LMS(제목 별도)로 발송
    SendResult send(const Message& m) override;
	
	private:
    const Config& cfg_;
    std::vector<std::string> phones_;
};

// ---- [sms_notifier.cpp] ----
SendResult SmsNotifier::send(const Message& m) {
    std::string key = cfg_.get("SOLAPI_API_KEY"), secret = cfg_.get("SOLAPI_API_SECRET"), from = cfg_.get("SOLAPI_FROM");
    if (key.empty() || secret.empty() || from.empty()) {
        puts("오류: SMS 설정 필요 (SOLAPI_API_KEY, SOLAPI_API_SECRET, SOLAPI_FROM)");
        return SendResult::configError();
	}
	
    std::string full = "[" + m.subject + "] " + m.plain;
    bool lms = util::krLen(full) > 90;
    std::string content = util::krCut(m.plain, 2000);
    if (lms && content.size() < m.plain.size()) puts("경고: 본문이 2000바이트를 넘어 잘렸습니다.");
	
    std::string json = "{\"messages\":[";
    for (size_t i = 0; i < phones_.size(); ++i) {
        if (i) json += ",";
        json += "{\"to\":\"" + phones_[i] + "\",\"from\":\"" + util::jsonEsc(from) + "\",\"type\":\"" + (lms ? "LMS" : "SMS") + "\"";
        if (lms) json += ",\"subject\":\"" + util::jsonEsc(util::krCut(m.subject, 40)) + "\",\"text\":\"" + util::jsonEsc(content) + "\"";
        else json += ",\"text\":\"" + util::jsonEsc(full) + "\"";
        json += "}";
	}
    json += "]}";
	
    // 인증: Authorization: HMAC-SHA256 apiKey=..., date=..., salt=..., signature=hex(HMAC(secret, date+salt))
    char date[32];
    std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    std::string salt, sig;
    if (!util::randomSalt(salt) || !util::hmacSha256Hex(secret, std::string(date) + salt, sig)) {
        puts("오류: 서명 계산 실패");
        return SendResult::fail();
	}
    std::string auth = "Authorization: HMAC-SHA256 apiKey=" + key + ", date=" + date + ", salt=" + salt + ", signature=" + sig;
	
    printf("SMS(%s) %zu명 발송 요청 중... (%zu바이트)\n", lms ? "LMS" : "SMS", phones_.size(), util::krLen(full));
    HttpResponse r = Http::postJson("https://api.solapi.com/messages/v4/send-many/detail", json, {auth});
	
    // 접수 실패 건은 응답의 failedMessageList 에 담겨 옴 (비어 있으면 성공)
    bool failedItems = false;
    size_t p = r.body.find("\"failedMessageList\"");
    if (p != std::string::npos && (p = r.body.find('[', p)) != std::string::npos) {
        p = r.body.find_first_not_of(" \r\n\t", p + 1);
        failedItems = (p != std::string::npos && r.body[p] != ']');
	}
    bool ok = (r.ok && r.status >= 200 && r.status < 300 && !failedItems);
    if (ok) { puts("SMS 접수 성공"); return SendResult::ok(); }
    if (!r.ok) printf("SMS 실패: %s\n", r.error.c_str());
    else printf("SMS 실패 (HTTP %ld): %s\n", r.status, r.body.c_str());
    return SendResult::fail(!r.ok);
}

// ============================================================================
// [telegram_notifier.h] 텔레그램 봇 ("나에게 보내기")
//   TELEGRAM_BOT_TOKEN (BotFather 발급). TELEGRAM_CHAT_ID 는 선택.
//   chat id 우선순위: 설정값 > 실행 파일 폴더의 telegram_chat.txt > getUpdates 로 자동 탐색 후 저장
//   (자동 탐색은 봇과 대화한 개인 채팅이 정확히 1명일 때만 사용 - 낯선 사람에게 가는 것을 방지)
// ============================================================================
class TelegramNotifier : public Notifier {
	public:
    explicit TelegramNotifier(const Config& cfg) : cfg_(cfg) {}
    const char* name() const override { return "텔레그램"; }
    std::vector<std::string> hosts() const override { return {"api.telegram.org"}; }
    bool configured() const { return cfg_.has("TELEGRAM_BOT_TOKEN"); }
    // "[제목] 본문" 발송. 4096자 제한이라 4000자 단위로 나눠 보냄
    SendResult send(const Message& m) override;
    // --telegram-chatid : 봇과 대화한 chat id 목록 출력
    int listChatIds();
	
	private:
    std::string apiUrl(const char* method) const;
    bool fetchIds(std::vector<std::string>& ids, bool& netFail);
    bool resolveChat(std::string& chat, bool& netFail);
	
    const Config& cfg_;
};

// ---- [telegram_notifier.cpp] ----
std::string TelegramNotifier::apiUrl(const char* method) const {
    return "https://api.telegram.org/bot" + cfg_.get("TELEGRAM_BOT_TOKEN") + "/" + method;   // 토큰은 화면에 출력하지 않음
}

// getUpdates 응답에서 chat id 를 나온 순서대로 (오래된 것 -> 최근 것)
bool TelegramNotifier::fetchIds(std::vector<std::string>& ids, bool& netFail) {
    HttpResponse r = Http::postForm(apiUrl("getUpdates"), "");
    if (!r.ok) { netFail = true; printf("연결 실패: %s\n", r.error.c_str()); return false; }
    if (r.status != 200) { printf("텔레그램 조회 실패 (HTTP %ld): %s\n", r.status, r.body.c_str()); return false; }
    const std::string k = "\"chat\":{\"id\":";
    size_t p = 0;
    while ((p = r.body.find(k, p)) != std::string::npos) {
        p += k.size();
        size_t e = r.body.find_first_not_of("-0123456789", p);
        std::string id = r.body.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!id.empty()) ids.push_back(id);
	}
    return true;
}

bool TelegramNotifier::resolveChat(std::string& chat, bool& netFail) {
    std::string cfgChat = cfg_.get("TELEGRAM_CHAT_ID");
    if (!cfgChat.empty()) { chat = cfgChat; return true; }
	
    std::wstring f = util::exeDirFile(L"telegram_chat.txt");
    std::string saved;
    if (util::readBin(f, saved) && !util::trim(saved).empty()) { chat = util::trim(saved); return true; }
	
    std::vector<std::string> ids;
    if (!fetchIds(ids, netFail)) return false;
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
    if (util::writeBin(f, chat)) puts("텔레그램 chat id 를 자동으로 찾아 telegram_chat.txt 에 저장했습니다.");
    return true;
}

SendResult TelegramNotifier::send(const Message& m) {
    if (!configured()) { puts("오류: TELEGRAM_BOT_TOKEN 이 필요합니다."); return SendResult::configError(); }
	
    bool netFail = false;
    std::string chat;
    if (!resolveChat(chat, netFail)) return SendResult::fail(netFail);
	
    std::string full = m.subject.empty() ? m.plain : "[" + m.subject + "] " + m.plain;
    std::string url = apiUrl("sendMessage");
	
    puts("텔레그램(나에게) 발송 요청 중...");
    size_t pos = 0;
    while (pos < full.size()) {
        std::string chunk = util::cutChars(full.substr(pos), 4000);
        if (chunk.empty()) break;
        HttpResponse r = Http::postForm(url, "chat_id=" + util::pctEncode(chat) + "&text=" + util::pctEncode(chunk));
        if (!r.ok) { printf("연결 실패: %s\n", r.error.c_str()); return SendResult::fail(true); }
        if (r.status != 200 || r.body.find("\"ok\":true") == std::string::npos) {
            printf("텔레그램 발송 실패 (HTTP %ld): %s\n", r.status, r.body.c_str());
            return SendResult::fail();
		}
        pos += chunk.size();
	}
    puts("텔레그램 발송 성공");
    return SendResult::ok();
}

int TelegramNotifier::listChatIds() {
    if (!configured()) { puts("오류: TELEGRAM_BOT_TOKEN 이 필요합니다."); return 1; }
    std::vector<std::string> ids, uniq;
    bool netFail = false;
    if (!fetchIds(ids, netFail)) return 2;
    for (const auto& id : ids) if (std::find(uniq.begin(), uniq.end(), id) == uniq.end()) uniq.push_back(id);
    if (uniq.empty()) { puts("받은 메시지가 없습니다. 텔레그램에서 봇에게 아무 메시지나 보낸 뒤 다시 실행하세요."); return 2; }
    for (const auto& id : uniq) printf("TELEGRAM_CHAT_ID=%s\n", id.c_str());
    return 0;
}

// ============================================================================
// [email_notifier.h] Gmail SMTP
// ============================================================================
class EmailNotifier : public Notifier {
	public:
    static constexpr const char* kDefaultSender = "analogclockseoul@gmail.com";
	
    // rcpts: 실제 수신자(To + BCC 전체), toHeader: To 헤더에 표시할 문자열, attachDir: 첨부 폴더(없으면 빈 문자열)
    EmailNotifier(std::string from, std::string pass, std::string toHeader, std::vector<std::string> rcpts,
	std::string attachDir, Clock::time_point startTime)
	: from_(std::move(from)), pass_(std::move(pass)), toHeader_(std::move(toHeader)),
	rcpts_(std::move(rcpts)), attachDir_(std::move(attachDir)), t0_(startTime) {}
    const char* name() const override { return "이메일"; }
    std::vector<std::string> hosts() const override { return {"smtp.gmail.com"}; }
    SendResult send(const Message& m) override;
	
	private:
    struct Attach { std::string name; std::string data; };
    struct Upload {
        const std::string* data;
        size_t pos;
        int pct;
        bool started;
        Clock::time_point first, last;   // 첫 데이터 요청 시각 / 마지막 데이터 전달 시각
	};
    static constexpr uint64_t kMaxAttachBytes = 18ull * 1024 * 1024;   // Gmail 한도 25MB, Base64 증가분 고려
	
    static size_t readCb(char* ptr, size_t size, size_t nmemb, void* userp);
    static bool loadFolder(const std::string& dir, std::vector<Attach>& out, std::string& err);
    static std::string encodeSubject(const std::string& s);    // 비ASCII 면 =?UTF-8?B?...?= (문자 경계에서 분할)
    static std::string asciiFallback(const std::string& s);
	
    std::string from_, pass_, toHeader_;
    std::vector<std::string> rcpts_;
    std::string attachDir_;
    Clock::time_point t0_;
};

// ---- [email_notifier.cpp] ----
std::string EmailNotifier::encodeSubject(const std::string& s) {
    if (util::isAscii(s)) return s;
    std::string out;
    for (size_t pos = 0; pos < s.size();) {
        size_t end = std::min(pos + 45, s.size());
        while (end > pos && end < s.size() && (s[end] & 0xC0) == 0x80) --end;
        if (end == pos) end = std::min(pos + 1, s.size());   // 무한 루프 방지
        if (!out.empty()) out += ' ';
        out += "=?UTF-8?B?" + util::b64(s.substr(pos, end - pos)) + "?=";
        pos = end;
	}
    return out;
}

std::string EmailNotifier::asciiFallback(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c < 32 || c > 126 || c == '"' || c == '\\') ? '_' : (char)c;
    return out;
}

// 폴더 바로 아래의 모든 파일 (하위 폴더 제외)
bool EmailNotifier::loadFolder(const std::string& dir, std::vector<Attach>& out, std::string& err) {
    std::wstring w = util::wide(dir);
    while (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
	
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { err = "첨부 폴더를 열 수 없습니다: " + dir; return false; }
	
    uint64_t total = 0;
    bool ok = true;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = util::utf8(fd.cFileName);
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (total + size > kMaxAttachBytes) { err = "첨부 총량이 18MB 를 넘습니다 (" + name + " 에서 초과)"; ok = false; break; }
        Attach a;
        a.name = name;
        if (!util::readBin(w + L"\\" + fd.cFileName, a.data)) { err = "파일을 읽을 수 없습니다: " + name; ok = false; break; }
        total += size;
        out.push_back(std::move(a));
	} while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

size_t EmailNotifier::readCb(char* ptr, size_t size, size_t nmemb, void* userp) {
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

SendResult EmailNotifier::send(const Message& m) {
    if (pass_.empty()) {
        puts("오류: Gmail 앱 비밀번호가 없습니다. booting_Alert.ini 의 GMAIL_APP_PASSWORD 를 설정하거나 인자로 전달하세요.");
        return SendResult::configError();
	}
	
    // 첨부 폴더
    std::vector<Attach> attachments;
    if (!attachDir_.empty()) {
        std::string aerr;
        if (!loadFolder(attachDir_, attachments, aerr)) { puts(("오류: " + aerr).c_str()); return SendResult::configError(); }
	}
	
    auto t1 = Clock::now();
	
    // 메시지 조립 (Date, Message-ID 는 Gmail 이 자동으로 채움)
    const std::string B = "----=_booting_Alert_8f3c1a7e";   // Base64 문자에 없는 '-', '=', '_' 포함 -> 충돌 없음
    std::string msg =
	"From: <" + from_ + ">\r\n"
	"To: " + toHeader_ + "\r\n"
	"Subject: " + encodeSubject(m.subject) + "\r\n"
	"MIME-Version: 1.0\r\n"
	"Content-Type: multipart/mixed; boundary=\"" + B + "\"\r\n"
	"\r\n"
	"--" + B + "\r\n"
	"Content-Type: text/plain; charset=UTF-8\r\n"
	"Content-Transfer-Encoding: base64\r\n"
	"\r\n" + util::wrap76(util::b64(m.bodyCrlf));
	
    for (const Attach& a : attachments) {
        msg += "--" + B + "\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Transfer-Encoding: base64\r\n"
		"Content-Disposition: attachment; filename=\"" + asciiFallback(a.name) + "\"";
        if (!util::isAscii(a.name)) msg += "; filename*=UTF-8''" + util::pctEncode(a.name);   // RFC 2231
        msg += "\r\n\r\n" + util::wrap76(util::b64(a.data));
	}
    msg += "--" + B + "--\r\n";
	
    auto t2 = Clock::now();
	
    // 전송
    CURL* curl = curl_easy_init();
    if (!curl) { puts("오류: curl 초기화 실패"); return SendResult::fail(); }
    char err[CURL_ERROR_SIZE] = {0};
    Upload up{&msg, 0, 0, false, {}, {}};
	
    curl_slist* rcpt = nullptr;
    for (const auto& r : rcpts_) rcpt = curl_slist_append(rcpt, ("<" + r + ">").c_str());
	
    curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(curl, CURLOPT_USERNAME, from_.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, pass_.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + from_ + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)msg.size());
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 524288L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
	
    printf("%s -> %s 전송 중... (메시지 %.1f MB, 수신 %zu건)\n", from_.c_str(), toHeader_.c_str(), msg.size() / 1048576.0, rcpts_.size());
    for (const Attach& a : attachments)
	printf("  첨부: %s (%zu KB)\n", a.name.c_str(), (a.data.size() + 1023) / 1024);
	
    CURLcode rc = curl_easy_perform(curl);
    auto t3 = Clock::now();
    printf("\n");
    bool retryable = false;
    if (rc == CURLE_OK) puts("전송 성공");
    else {
        // 접속/통신 단계 오류만 재시도 대상 (로그인 거부 등은 제외)
        retryable = (rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_COULDNT_CONNECT || rc == CURLE_OPERATION_TIMEDOUT ||
		rc == CURLE_SEND_ERROR || rc == CURLE_RECV_ERROR);
        printf("전송 실패: %s\n", err[0] ? err : curl_easy_strerror(rc));
	}
	
    auto sec = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
    curl_off_t speed = 0;   // 바이트/초
    curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD_T, &speed);
    printf("시간: 읽기 %.2f초 / 인코딩 %.2f초 / 전송 %.2f초 (업로드 평균 %.2f MB/s)\n",
	sec(t0_, t1), sec(t1, t2), sec(t2, t3), (double)speed / 1048576.0);
    if (up.started)
	printf("전송 세부: 연결·로그인 %.2f초 / 데이터 업로드 %.2f초 / 서버 응답 대기 %.2f초\n",
	sec(t2, up.first), sec(up.first, up.last), sec(up.last, t3));
	
    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK ? SendResult::ok() : SendResult::fail(retryable);
}

// ============================================================================
// [local_callback_server.h] OAuth 리다이렉트를 받는 로컬 HTTP 서버 (http://localhost:PORT/PATH)
// ============================================================================
class LocalCallbackServer {
	public:
    // "http://localhost:8080/callback" -> port=8080, path="/callback". localhost http 형식이 아니면 false
    static bool parseUri(const std::string& uri, int& port, std::string& path);
    // path 로 들어온 요청의 code 를 꺼냄 (state 일치 확인). 성공 true
    static bool waitForCode(int port, const std::string& path, const std::string& state,
	std::string& code, std::string& errMsg, int timeoutSec);
};

// ---- [local_callback_server.cpp] ----
bool LocalCallbackServer::parseUri(const std::string& uri, int& port, std::string& path) {
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

bool LocalCallbackServer::waitForCode(int port, const std::string& path, const std::string& state,
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
		
        // 데이터 없이 연결만 열어두는 브라우저(사전 연결)에 멈추지 않도록 타임아웃을 걸고, 요청 첫 줄까지만 읽음
        DWORD tmo = 3000;
        setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
        setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof tmo);
        char buf[8192];
        int got = 0;
        while (got < (int)sizeof buf - 1) {
            int n = recv(cs, buf + got, (int)sizeof buf - 1 - got, 0);
            if (n <= 0) break;
            got += n;
            if (std::memchr(buf, '\n', (size_t)got)) break;
		}
        std::string req(buf, (size_t)got);
		
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
            std::string c = util::queryParam(query, "code");
            std::string e = util::queryParam(query, "error");
            if (!e.empty()) {
                errMsg = e + " " + util::queryParam(query, "error_description");
                done = true;
				} else if (!c.empty()) {
                if (util::queryParam(query, "state") == state) { code = c; ok = true; }
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

// ============================================================================
// [kakao_notifier.h] 카카오톡 "나에게 보내기" + OAuth 인증
//   - 토큰: 실행 파일 폴더의 kakao_token.txt (refresh_token 한 줄)
//   - 토큰이 없거나 만료/권한 부족이면 자동 인증 후 1회 재시도
//   - KAKAO_NO_AUTH=TRUE (무인 모드): 브라우저 인증 대신 AlertFn 으로 재인증 필요 알림
//     (같은 알림은 ALERT_INTERVAL_HOURS 안에는 반복하지 않음. 기록: kakao_alert.txt)
// ============================================================================
class KakaoNotifier : public Notifier {
	public:
    using AlertFn = std::function<void(const std::string& text)>;   // 재인증 필요 알림을 보내는 콜백 (다른 서비스와 분리)
	
    KakaoNotifier(const Config& cfg, AlertFn alert) : cfg_(cfg), alert_(std::move(alert)) {}
    const char* name() const override { return "카카오"; }
    std::vector<std::string> hosts() const override { return {"kapi.kakao.com"}; }
    SendResult send(const Message& m) override { return execute(&m); }
    SendResult keepAlive() { return execute(nullptr); }   // --kakao-keepalive : 토큰 갱신만
    SendResult authorize();                               // --kakao-auth : 인증만 단독 실행
	
	private:
    SendResult execute(const Message* m);   // m == nullptr 이면 갱신만
    bool authorizeFlow(const std::string& key, const std::string& secret, const std::wstring& tokenFile);
    void notifyReauthNeeded();
	
    const Config& cfg_;
    AlertFn alert_;
};

// ---- [kakao_notifier.cpp] ----
namespace {
	const char* const kKakaoTokenUrl = "https://kauth.kakao.com/oauth/token";
	const char* const kKakaoMemoUrl = "https://kapi.kakao.com/v2/api/talk/memo/default/send";
	std::wstring kakaoTokenPath() { return util::exeDirFile(L"kakao_token.txt"); }
	std::wstring kakaoAlertPath() { return util::exeDirFile(L"kakao_alert.txt"); }
}  // namespace

// 브라우저 인증 -> code -> 토큰 교환 -> kakao_token.txt 저장. 성공 true
bool KakaoNotifier::authorizeFlow(const std::string& key, const std::string& secret, const std::wstring& tokenFile) {
    std::string redirect = cfg_.get("KAKAO_REDIRECT_URI", "http://localhost:8080/callback");
	
    std::string state;
    if (!util::randomSalt(state)) { puts("오류: 난수 생성 실패"); return false; }
	
    std::string url = "https://kauth.kakao.com/oauth/authorize?client_id=" + util::pctEncode(key) +
	"&redirect_uri=" + util::pctEncode(redirect) +
	"&response_type=code&scope=talk_message&state=" + state;
	
    puts("[카카오 인증] 브라우저를 엽니다. 로그인 후 '카카오톡 메시지 전송'에 동의하세요.");
    printf("  (자동으로 안 열리면 아래 주소를 직접 여세요)\n  %s\n", url.c_str());
    ShellExecuteW(nullptr, L"open", util::wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	
    std::string code, err;
    int port = 0;
    std::string path;
    if (LocalCallbackServer::parseUri(redirect, port, path)) {
        puts("  동의 완료를 기다리는 중... (최대 3분)");
        if (!LocalCallbackServer::waitForCode(port, path, state, code, err, 180)) {
            printf("인증 실패: %s\n", err.c_str());
            return false;
		}
		} else {
        // localhost 가 아닌 Redirect URI: 주소창 전체 URL 을 붙여넣기
        puts("  동의 후 이동된 페이지의 주소창 URL 전체를 복사해서 붙여넣고 Enter:");
        char line[4096];
        if (!fgets(line, sizeof line, stdin)) return false;
        std::string u = util::trim(line);
        size_t q = u.find('?');
        code = util::queryParam(q == std::string::npos ? u : u.substr(q + 1), "code");
        if (code.empty()) { puts("오류: URL 에서 code 를 찾지 못했습니다."); return false; }
	}
	
    // code -> 토큰
    std::string form = "grant_type=authorization_code&client_id=" + util::pctEncode(key) +
	"&redirect_uri=" + util::pctEncode(redirect) + "&code=" + util::pctEncode(code);
    if (!secret.empty()) form += "&client_secret=" + util::pctEncode(secret);
    HttpResponse r = Http::postForm(kKakaoTokenUrl, form);
    if (!r.ok) { printf("연결 실패: %s\n", r.error.c_str()); return false; }
	
    std::string rt = util::jsonStr(r.body, "refresh_token");
    if (r.status != 200 || rt.empty()) {
        printf("토큰 발급 실패 (HTTP %ld): %s\n", r.status, r.body.c_str());
        return false;
	}
    if (util::jsonStr(r.body, "scope").find("talk_message") == std::string::npos) {
        puts("오류: 토큰에 talk_message 권한이 없습니다.");
        puts("  카카오 콘솔에서 [동의항목 > 카카오톡 메시지 전송] 설정과");
        puts("  [제품 설정 > 카카오톡 메시지] 활성화를 확인한 뒤 다시 실행하세요.");
        return false;
	}
	
    if (!util::writeBin(tokenFile, rt)) { puts("오류: kakao_token.txt 를 저장하지 못했습니다."); return false; }
    puts("[카카오 인증] 완료. refresh_token 을 kakao_token.txt 에 저장했습니다.");
    return true;
}

SendResult KakaoNotifier::authorize() {
    std::string key = cfg_.get("KAKAO_REST_API_KEY");
    if (key.empty()) { puts("오류: KAKAO_REST_API_KEY 가 필요합니다."); return SendResult::configError(); }
    return authorizeFlow(key, cfg_.get("KAKAO_CLIENT_SECRET"), kakaoTokenPath()) ? SendResult::ok() : SendResult::fail();
}

// 무인 모드에서 재인증이 필요할 때의 알림. ALERT_INTERVAL_HOURS(기본 24, 0=매번) 안에는 반복하지 않음
void KakaoNotifier::notifyReauthNeeded() {
    std::wstring af = kakaoAlertPath();
    long long now = (long long)std::time(nullptr), last = 0;
    std::string t;
    if (util::readBin(af, t)) last = std::atoll(util::trim(t).c_str());
    int hours = cfg_.getInt("ALERT_INTERVAL_HOURS", 24);
    if (hours > 0 && last > 0 && now >= last && now - last < (long long)hours * 3600) {
        printf("재인증 알림은 최근 %d시간 안에 이미 보냈습니다. 이번에는 건너뜁니다.\n", hours);
        return;
	}
    if (alert_) alert_("카카오 재인증 필요. PC에서 booting_Alert.exe --kakao-auth 실행");
    util::writeBin(af, std::to_string(now));
}

SendResult KakaoNotifier::execute(const Message* m) {
    std::string key = cfg_.get("KAKAO_REST_API_KEY");
    if (key.empty()) { puts("오류: KAKAO_REST_API_KEY 가 필요합니다."); return SendResult::configError(); }
    std::string secret = cfg_.get("KAKAO_CLIENT_SECRET");
    std::string link = cfg_.get("KAKAO_LINK_URL", "https://developers.kakao.com");   // 앱 플랫폼(Web)에 등록된 도메인
    bool noAuth = cfg_.getBool("KAKAO_NO_AUTH");   // 무인 모드: 브라우저 인증을 시도하지 않음
    std::wstring tf = kakaoTokenPath();
	
    std::string text;
    if (m) {
        std::string full = "[" + m->subject + "] " + m->plain;
        text = util::cutChars(full, 200);
        if (text.size() < full.size()) puts("경고: 카카오톡 메시지는 200자까지라 뒤쪽이 잘렸습니다.");
	}
	
    bool netFail = false, needAlert = false, reauthed = false;
    int result = 2;
	
    for (;;) {
        bool need = false;   // 재인증 필요 여부
        std::string rt;
        if (!util::readBin(tf, rt) || util::trim(rt).empty()) {
            puts("kakao_token.txt 가 없거나 비어 있습니다.");
            need = true;
			} else {
            rt = util::trim(rt);
            // 1) refresh_token -> access_token
            std::string form = "grant_type=refresh_token&client_id=" + util::pctEncode(key) + "&refresh_token=" + util::pctEncode(rt);
            if (!secret.empty()) form += "&client_secret=" + util::pctEncode(secret);
            HttpResponse r = Http::postForm(kKakaoTokenUrl, form);
            if (!r.ok) { netFail = true; printf("연결 실패: %s\n", r.error.c_str()); break; }
            std::string at = util::jsonStr(r.body, "access_token");
            if (r.status != 200 || at.empty()) {
                printf("카카오 토큰 갱신 실패 (HTTP %ld): %s\n", r.status, r.body.c_str());
                need = (r.body.find("invalid_grant") != std::string::npos);
                if (!need) break;
				} else {
                std::string nrt = util::jsonStr(r.body, "refresh_token");   // 남은 기간이 짧을 때만 새로 옴
                if (!nrt.empty()) {
                    if (util::writeBin(tf, nrt)) puts("카카오 refresh_token 갱신됨");
                    else printf("경고: 새 refresh_token 저장 실패. 직접 갱신하세요: %s\n", nrt.c_str());
				}
				
                if (!m) { puts("카카오 토큰 정상 (갱신만 수행)"); result = 0; break; }
				
                // 2) 나에게 보내기
                std::string tmpl = "{\"object_type\":\"text\",\"text\":\"" + util::jsonEsc(text) + "\",\"link\":{\"web_url\":\"" +
				util::jsonEsc(link) + "\",\"mobile_web_url\":\"" + util::jsonEsc(link) + "\"}}";
                puts("카카오톡(나에게) 발송 요청 중...");
                HttpResponse s = Http::postForm(kKakaoMemoUrl, "template_object=" + util::pctEncode(tmpl), {"Authorization: Bearer " + at});
                if (!s.ok) { netFail = true; printf("연결 실패: %s\n", s.error.c_str()); break; }
                if (s.status == 200) { puts("카카오톡 발송 성공"); result = 0; break; }
                printf("카카오톡 발송 실패 (HTTP %ld): %s\n", s.status, s.body.c_str());
                need = (s.body.find("insufficient scopes") != std::string::npos);   // talk_message 권한 없음
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
        if (!authorizeFlow(key, secret, tf)) break;
	}
	
    if (needAlert) notifyReauthNeeded();
    if (result == 0) DeleteFileW(kakaoAlertPath().c_str());   // 정상 복구되면 알림 기록 초기화
    return result == 0 ? SendResult::ok() : SendResult::fail(netFail);
}

// ============================================================================
// [dispatcher.h] 네트워크 대기 / 재시도 / 서비스별 쓰레드 동시 발송
// ============================================================================
class NetworkWaiter {
	public:
    // hosts 중 하나라도 이름 확인이 되면 네트워크가 올라온 것으로 봄. NET_WAIT_SEC(기본 60, 0=대기 안 함)까지 대기
    static void wait(const Config& cfg, const std::vector<std::string>& hosts);
	
	private:
    static bool dnsOk(const std::string& host);
};

class RetryPolicy {
	public:
    explicit RetryPolicy(const Config& cfg)
	: tries_(cfg.getInt("RETRY_COUNT", 2)), delaySec_(cfg.getInt("RETRY_DELAY_SEC", 10)) {}
    // 연결 오류(retryable)로 실패했을 때만 tries_ 회, delaySec_ 초 간격으로 재시도
    SendResult run(const char* name, const std::function<SendResult()>& fn) const;
	
	private:
    int tries_, delaySec_;
};

class Dispatcher {
	public:
    explicit Dispatcher(const Config& cfg) : cfg_(cfg), retry_(cfg) {}
    void add(std::unique_ptr<Notifier> n) { notifiers_.push_back(std::move(n)); }
    // add 한 서비스마다 쓰레드를 하나씩 만들어 동시에 발송 (쓰레드 안에서 네트워크 대기 -> 재시도 포함 발송).
    // 결과는 확인하지 않고, 모든 쓰레드가 끝나길 기다린 뒤 항상 0 을 반환
    int run(const Message& m);
	
	private:
    const Config& cfg_;
    RetryPolicy retry_;
    std::vector<std::unique_ptr<Notifier>> notifiers_;
};

// ---- [dispatcher.cpp] ----
bool NetworkWaiter::dnsOk(const std::string& host) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int r = getaddrinfo(host.c_str(), "443", &hints, &res);
    if (res) freeaddrinfo(res);
    return r == 0;
}

void NetworkWaiter::wait(const Config& cfg, const std::vector<std::string>& hosts) {
    int maxSec = cfg.getInt("NET_WAIT_SEC", 60);
    if (maxSec <= 0 || hosts.empty()) return;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    auto start = Clock::now();
    bool waited = false, ready = false;
    for (;;) {
        for (const auto& h : hosts) if (dnsOk(h)) { ready = true; break; }
        if (ready) break;
        if (std::chrono::duration<double>(Clock::now() - start).count() >= maxSec) break;
        if (!waited) { printf("네트워크 연결 대기 중... (최대 %d초)\n", maxSec); waited = true; }
        Sleep(3000);
	}
    if (!ready) puts("경고: 네트워크가 확인되지 않았지만 발송을 시도합니다.");
    else if (waited) puts("네트워크 연결 확인됨");
    WSACleanup();
}

SendResult RetryPolicy::run(const char* name, const std::function<SendResult()>& fn) const {
    SendResult r;
    for (int i = 0;; ++i) {
        r = fn();
        if (r.code == 0 || !r.retryable || i >= tries_) break;
        printf("%s: 네트워크 오류, %d초 후 재시도 (%d/%d)\n", name, delaySec_, i + 1, tries_);
        Sleep((DWORD)delaySec_ * 1000);
	}
    return r;
}

int Dispatcher::run(const Message& m) {
    // 서비스마다 쓰레드 하나. 각자 자기 호스트만 기다리므로 한 서비스가 느려도 다른 서비스는 영향 없음.
    // join 은 결과를 보려는 것이 아님: main 이 먼저 끝나면 프로세스가 종료되어 쓰레드가 중간에 죽기 때문.
    std::vector<std::thread> threads;
    for (const auto& n : notifiers_) {
        Notifier* p = n.get();
        threads.emplace_back([this, p, &m] {
            try {
                NetworkWaiter::wait(cfg_, p->hosts());
                retry_.run(p->name(), [&] { return p->send(m); });   // 반환값(결과)은 버림
            } catch (...) {}   // 쓰레드 밖으로 예외가 나가면 프로세스가 종료되므로 삼킴
        });
    }
    for (auto& t : threads) t.join();
    return 0;
}

// ============================================================================
// [main.cpp] 인자 해석과 전체 흐름
// ============================================================================
class App {
	public:
    int run(int argc, char** argv);
	
	private:
    int usage() const;
    int keepAlive();
    KakaoNotifier::AlertFn makeAlert();
	
    Config cfg_;
};

int App::usage() const {
    puts("사용법: booting_Alert <메일파일(UTF-8)> [첨부폴더]   (앱 비밀번호는 booting_Alert.ini 의 GMAIL_APP_PASSWORD)");
    puts("        booting_Alert <앱비밀번호> <메일파일(UTF-8)> [첨부폴더]");
    puts("        booting_Alert --kakao-auth | --kakao-keepalive | --telegram-chatid");
    puts("메일 파일 형식: 1줄=수신자ID, 2줄=제목, 3줄~=본문  (ini 에 수신자가 있으면 1줄=제목, 2줄~=본문)");
    return 1;
}

// 카카오 재인증이 필요할 때 SMS(KAKAO_ALERT_PHONE) / 텔레그램으로 알리는 콜백 (서비스끼리 직접 참조하지 않도록 여기서 연결)
KakaoNotifier::AlertFn App::makeAlert() {
    return [this](const std::string& text) {
        Message am{"booting_Alert", text, ""};
        std::string ph;
        if (util::toPhone(cfg_.get("KAKAO_ALERT_PHONE"), ph)) SmsNotifier(cfg_, std::vector<std::string>{ph}).send(am);
        TelegramNotifier tg(cfg_);
        if (tg.configured()) tg.send(am);
	};
}

int App::keepAlive() {   // 토큰 갱신만 (스케줄러용)
    KakaoNotifier kakao(cfg_, makeAlert());
    NetworkWaiter::wait(cfg_, kakao.hosts());
    return RetryPolicy(cfg_).run(kakao.name(), [&] { return kakao.keepAlive(); }).code;
}

int App::run(int argc, char** argv) {
    cfg_.loadIni(util::exeDirFile(L"booting_Alert.ini"));
	
    if (argc == 2) {
        std::string a = argv[1];
        if (a == "--telegram-chatid") return TelegramNotifier(cfg_).listChatIds();
        if (a == "--kakao-auth") return KakaoNotifier(cfg_, nullptr).authorize().code;
        if (a == "--kakao-keepalive") return keepAlive();
	}
    auto t0 = Clock::now();
	
    // 인자 해석
    //   booting_Alert <메일파일> [첨부폴더]              (비밀번호는 ini/환경변수의 GMAIL_APP_PASSWORD)
    //   booting_Alert <앱비밀번호> <메일파일> [첨부폴더]   (기존 방식)
    std::string pass = cfg_.get("GMAIL_APP_PASSWORD");
    std::string mailArg, attArg;
    if (argc == 2) { mailArg = argv[1]; }
    else if (argc == 3 && util::isDir(argv[2])) { mailArg = argv[1]; attArg = argv[2]; }
    else if (argc == 3 || argc == 4) { pass = argv[1]; mailArg = argv[2]; if (argc == 4) attArg = argv[3]; }
    else return usage();
    pass.erase(std::remove(pass.begin(), pass.end(), ' '), pass.end());
	
    std::string from = cfg_.get("GMAIL_USER", EmailNotifier::kDefaultSender);
    if (from.find('@') == std::string::npos) {
        puts("오류: 발신자 주소가 없습니다. GMAIL_USER 설정 또는 EmailNotifier::kDefaultSender 수정.");
        return 1;
	}
	
    // 메일 파일 파싱 (UTF-8, BOM 제거)
    std::string text;
    if (!util::readBin(util::wide(mailArg), text)) { puts("오류: 메일 파일을 열 수 없습니다."); return 1; }
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
	
    size_t pos = 0;
    std::string to, subject;
    std::string cfgTo = Recipients::lineFromConfig(cfg_);
    if (!cfgTo.empty()) {
        // 수신자가 booting_Alert.ini 에 있음: 메일 파일 1줄=제목, 2줄~=본문
        to = cfgTo;
        util::nextLine(text, pos, subject);
        if (Recipients::looksLikeList(util::trim(subject)))
		puts("경고: ini 에 수신자가 있으면 메일 파일 1줄은 제목입니다. 1줄의 옛 수신자 줄을 삭제하세요.");
		} else {
        // 기존 방식: 메일 파일 1줄=수신자, 2줄=제목, 3줄~=본문
        util::nextLine(text, pos, to);
        util::nextLine(text, pos, subject);
	}
    subject = util::trim(subject);
    if (subject.empty()) subject = "(제목 없음)";
	
    Recipients rcp = Recipients::parse(to);
	
    // 숨은 참조 (To 헤더에는 나타나지 않음)
    std::vector<std::string> bcc;
    for (const auto& r : Recipients::parse(cfg_.get("EMAIL_BCC")).emails)
	if (std::find(rcp.emails.begin(), rcp.emails.end(), r) == rcp.emails.end() &&
	std::find(bcc.begin(), bcc.end(), r) == bcc.end()) bcc.push_back(r);
	
    if (rcp.empty() && bcc.empty()) {
        puts("오류: 수신자가 없습니다 (메일 파일 1줄 또는 ini 의 EMAIL_TO / SOLAPI_TO / KAKAO / TELEGRAM).");
        return 1;
	}
	
    std::string toHeader;   // To 헤더 및 로그 출력용: "<a>, <b>"
    for (size_t i = 0; i < rcp.emails.size(); ++i) toHeader += (i ? ", <" : "<") + rcp.emails[i] + ">";
    if (toHeader.empty()) toHeader = "undisclosed-recipients:;";
    std::vector<std::string> rcptAll = rcp.emails;
    rcptAll.insert(rcptAll.end(), bcc.begin(), bcc.end());
	
    Message msg;
    msg.subject = subject;
    msg.plain = util::trim(text.substr(pos));      // SMS / 텔레그램 / 카카오 본문
    msg.bodyCrlf = util::toCrlf(text.substr(pos)); // 이메일 본문
	
    // 서비스별 쓰레드로 동시에 발송 (add 순서는 의미 없음)
    Dispatcher dispatcher(cfg_);
    if (!rcp.phones.empty()) dispatcher.add(std::make_unique<SmsNotifier>(cfg_, rcp.phones));
    if (rcp.telegram) dispatcher.add(std::make_unique<TelegramNotifier>(cfg_));
    if (!rcptAll.empty())
	dispatcher.add(std::make_unique<EmailNotifier>(from, pass, toHeader, rcptAll, attArg, t0));
    if (rcp.kakao) dispatcher.add(std::make_unique<KakaoNotifier>(cfg_, makeAlert()));
    return dispatcher.run(msg);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CurlGlobal curlGlobal;
    App app;
    return app.run(argc, argv);
}
