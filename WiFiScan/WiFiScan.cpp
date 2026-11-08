// WiFiScan.cpp - 블루투스/Wi-Fi 스캔 + 집 안팎 감시 (Windows 11, g++/MinGW)
// 빌드: g++ -O2 -std=c++17 -static -mwindows -o WiFiScan.exe WiFiScan.cpp -liphlpapi -lws2_32 -lbthprops   (_build.bat)
// 실행: WiFiScan.exe   (인자 없음. 콘솔 창 없는 GUI 프로그램 - WinMain. 로그는 파일로만 남긴다)
//   1) 블루투스 스캔 -> WiFiScan_result.txt 를 새로 만든다 ([Bluetooth] 구역: 페어링된 장치 + 주변 장치)
//   2) Wi-Fi 전체 스캔 -> 같은 파일 끝에 내용을 추가한다 ([Wi-Fi] 구역)
//   3) 감시 시작
//      이전 결과 파일의 User Text 와 응답 없는 장치(OFF)는 새 파일에 이어서 유지된다.
//   종료: ..\screen_telegram\wifiscanTriger.txt 에 END 를 쓴다 (텔레그램 /stop 이 해 준다). 시작할 때 이 파일은 무조건 삭제한다.
//         (콘솔이 없으므로 Ctrl+C 는 없다. 급하면 작업 관리자 또는 taskkill /im WiFiScan.exe /f)
//   파일 위치: 로그 WiFiScan_watch.log, 결과 WiFiScan_result.txt, oui.txt 는 모두 exe 와 같은 폴더.
//
// 감시 (..\screen_telegram\screen_telegram.ini 사용)
//     HomeBluetooth=폰 블루투스 주소 (AA:BB:CC:DD:EE:FF, 먼저 PC 와 페어링 필요. 주소는 결과 파일 [Bluetooth] 구역에서 확인)
//     HomeMac=폰 Wi-Fi MAC, HomeIp=폰 고정 IP (선택, 있으면 먼저 직접 확인), WiFiScanWatchInterval=초 (기본 15), NotHome=외출 판정 초 (기본 60)
//   판정 순서: 1) 블루투스(HomeBluetooth) 로 확인  2) 확인되지 않으면 Wi-Fi 스캔(HomeMac) 으로 확인
//     둘 중 하나라도 확인되면 집에 있음 (HomeBluetooth 나 HomeMac 중 하나만 있어도 동작)
//   집에 있음으로 바뀌면 -> /HomeBack, NotHome 초 연속 둘 다 미확인 -> /NotHome (WiFiScan_watch.log)
//   전달 방식: screen_telegram 폴더의 trigger.txt 에 명령 한 줄을 쓴다. screen_telegram.exe 가 1초마다 읽어 처리한다.
//     (텔레그램 sendMessage 로 보낸 /HomeBack 은 봇이 자기 메시지를 받지 못해 처리되지 않으므로 파일로 전달)
//     screen_telegram.exe 가 실행 중이어야 하고 INI 의 LOCAL_TRIGGER 가 FALSE 가 아니어야 한다.
// 선택: oui.txt (IEEE) 를 exe 옆에 두면 제조사명 표시
//   https://standards-oui.ieee.org/oui/oui.txt
//
// 구조 (class)
//   StrUtil / AppDir / Logger          : 문자열 도구, exe 폴더, 파일 로그
//   IniFile                            : INI 읽기 (읽기 전용, 수정하지 않음)
//   NameResolver / OuiDb               : 장치 이름(DNS, NetBIOS, mDNS) / 제조사 조회
//   Subnet / Arp                       : 서브넷 감지 / ARP 확인
//   Bluetooth                          : 블루투스 주소 변환, 직접 불러 보기, 스캔
//   PrevResult / ResultFile            : 이전 결과 읽기 / 결과 파일 쓰기
//   WifiScanner                        : Wi-Fi 전체 스캔
//   EndFile / TriggerFile              : 종료 파일(wifiscanTriger.txt) / 명령 전달 파일(trigger.txt)
//   HomeWatcher                        : 집 안팎 감시 (상태 판정)
//   WiFiScanApp                        : 전체 실행 순서
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <cstring>
#include <ctime>
#include <cstdlib>

// ======================================================================
// 문자열 / 주소 도구
// ======================================================================
class StrUtil {
public:
    static std::string u8(const std::wstring& w) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), 0, 0, 0, 0);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, 0, 0);
        return s;
    }
    static std::string fromOem(const char* p, int len) {
        int n = MultiByteToWideChar(CP_OEMCP, 0, p, len, 0, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_OEMCP, 0, p, len, &w[0], n);
        return u8(w);
    }
    static std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    }
    static std::vector<std::string> split(const std::string& s, char c) {
        std::vector<std::string> v; std::string t; std::stringstream ss(s);
        while (std::getline(ss, t, c)) v.push_back(t);
        return v;
    }
    static std::string ipStr(uint32_t ip) {
        char b[32];
        snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        return b;
    }
    static bool parseIp(const std::string& s, uint32_t& ip) {
        in_addr a;
        if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
        ip = ntohl(a.s_addr);
        return true;
    }
    static std::string normMac(const std::string& s) {      // xx:xx.. / xx-xx.. -> XX-XX-XX-XX-XX-XX
        std::string h;
        for (char c : s) if (isxdigit((unsigned char)c)) h += (char)toupper((unsigned char)c);
        if (h.size() != 12) return "";
        std::string o;
        for (int i = 0; i < 12; i += 2) { if (i) o += '-'; o += h.substr(i, 2); }
        return o;
    }
    static std::string macStr(const unsigned char* p) {
        char t[32];
        snprintf(t, sizeof t, "%02X-%02X-%02X-%02X-%02X-%02X", p[0], p[1], p[2], p[3], p[4], p[5]);
        return t;
    }
    static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
};

// ======================================================================
// exe 폴더 / 각종 파일 경로
// ======================================================================
class AppDir {
public:
    static const std::string& dir() {                        // exe 가 있는 폴더 (끝에 \ 포함)
        static const std::string d = [] {
            char exe[MAX_PATH] = {0}; GetModuleFileNameA(0, exe, MAX_PATH);
            std::string s = exe; s.resize(s.find_last_of("\\/") + 1);
            return s;
        }();
        return d;
    }
    static std::string logFile()    { return dir() + "WiFiScan_watch.log"; }
    static std::string resultFile() { return dir() + "WiFiScan_result.txt"; }
    static std::string ouiFile()    { return dir() + "oui.txt"; }
    static std::string telegramDir() { return dir() + "..\\screen_telegram\\"; }
    static std::string iniFile()    { return telegramDir() + "screen_telegram.ini"; }
    static std::string triggerFile() { return telegramDir() + "trigger.txt"; }
    static std::string endFile()    { return telegramDir() + "wifiscanTriger.txt"; }
};

// ======================================================================
// 파일 로그 (콘솔 출력 없음)
// ======================================================================
class Logger {
public:
    static void log(const std::string& msg) {
        time_t t = time(0); char ts[32];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
        std::lock_guard<std::mutex> g(mu());
        std::ofstream f(AppDir::logFile(), std::ios::app | std::ios::binary);
        f << ts << " " << msg << "\r\n";
    }
private:
    static std::mutex& mu() { static std::mutex m; return m; }
};

// ======================================================================
// INI (읽기 전용)
// ======================================================================
class IniFile {
public:
    explicit IniFile(std::string path) : path_(std::move(path)) {}
    const std::string& path() const { return path_; }
    std::string get(const std::string& key) const {
        std::ifstream f(path_, std::ios::binary);
        std::string l;
        while (std::getline(f, l)) {
            if (l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3);
            l = StrUtil::trim(l);
            if (l.empty() || l[0] == ';' || l[0] == '#' || l[0] == '[') continue;
            size_t e = l.find('=');
            if (e == std::string::npos) continue;
            std::string k = StrUtil::trim(l.substr(0, e));
            if (k.size() != key.size()) continue;
            bool same = true;
            for (size_t i = 0; i < k.size(); i++) if (tolower((unsigned char)k[i]) != tolower((unsigned char)key[i])) { same = false; break; }
            if (!same) continue;
            std::string v = l.substr(e + 1);
            for (size_t i = 1; i < v.size(); i++)
                if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) { v.resize(i); break; }
            return StrUtil::trim(v);
        }
        return "";
    }
private:
    std::string path_;
};

// ======================================================================
// 장치 이름 조회: DNS -> NetBIOS(UDP 137) -> mDNS(UDP 5353)
// ======================================================================
class NameResolver {
public:
    static std::string hostName(uint32_t ip) {
        sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(ip);
        wchar_t h[NI_MAXHOST];
        if (GetNameInfoW((sockaddr*)&sa, sizeof sa, h, NI_MAXHOST, 0, 0, NI_NAMEREQD) == 0) {
            std::wstring w = h;
            size_t d = w.find(L'.');
            if (d != std::wstring::npos) w.resize(d);
            if (!w.empty()) return StrUtil::u8(w);
        }
        std::string n = netbiosName(ip);
        return n.empty() ? mdnsName(ip) : n;
    }
private:
    // NetBIOS 노드 상태 조회 (UDP 137)
    static std::string netbiosName(uint32_t ip) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return "";
        DWORD to = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        unsigned char q[50] = {0};
        q[0] = 0x13; q[1] = 0x37; q[5] = 1; q[12] = 0x20; q[13] = 'C'; q[14] = 'K';
        for (int i = 15; i < 45; i++) q[i] = 'A';
        q[47] = 0x21; q[49] = 1;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(137); a.sin_addr.s_addr = htonl(ip);
        std::string out;
        if (sendto(s, (char*)q, sizeof q, 0, (sockaddr*)&a, sizeof a) == sizeof q) {
            unsigned char b[600];
            int r = recvfrom(s, (char*)b, sizeof b, 0, 0, 0);
            if (r > 57) {
                int p = 12;
                while (p < r && b[p]) {
                    if ((b[p] & 0xC0) == 0xC0) { p += 1; break; }
                    p += b[p] + 1;
                }
                p += 1;                 // 이름 종료
                p += 2 + 2 + 4 + 2;     // type, class, ttl, rdlength
                if (p < r) {
                    int cnt = b[p++];
                    for (int i = 0; i < cnt && p + 18 <= r; i++, p += 18) {
                        int flags = (b[p + 16] << 8) | b[p + 17];
                        if (b[p + 15] == 0 && !(flags & 0x8000)) {
                            int len = 15;
                            while (len > 0 && b[p + len - 1] == ' ') len--;
                            out = StrUtil::fromOem((char*)b + p, len);
                            break;
                        }
                    }
                }
            }
        }
        closesocket(s);
        return out;
    }
    // DNS 패킷의 이름 디코딩 (압축 포인터 지원). p 는 이름 다음 위치로 갱신
    static std::string dnsName(const unsigned char* b, int len, int& p) {
        std::string s; int pos = p, jumps = 0; bool jumped = false;
        while (pos < len) {
            int c = b[pos];
            if (c == 0) { pos++; break; }
            if ((c & 0xC0) == 0xC0) {
                if (pos + 1 >= len || ++jumps > 10) break;
                if (!jumped) p = pos + 2;
                jumped = true;
                pos = ((c & 0x3F) << 8) | b[pos + 1];
                continue;
            }
            if (pos + 1 + c > len) break;
            if (!s.empty()) s += '.';
            s.append((const char*)b + pos + 1, c);
            pos += 1 + c;
        }
        if (!jumped) p = pos;
        return s;
    }
    // mDNS 역방향 조회 (UDP 5353 유니캐스트, PTR)
    static std::string mdnsName(uint32_t ip) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return "";
        DWORD to = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof to);
        unsigned char q[128] = {0}; int n = 12; q[5] = 1;
        for (int sh = 0; sh <= 24; sh += 8) {
            std::string o = std::to_string((ip >> sh) & 255);
            q[n++] = (unsigned char)o.size(); memcpy(q + n, o.data(), o.size()); n += (int)o.size();
        }
        for (const char* t : {"in-addr", "arpa"}) {
            size_t l = strlen(t); q[n++] = (unsigned char)l; memcpy(q + n, t, l); n += (int)l;
        }
        q[n++] = 0; q[n++] = 0; q[n++] = 12; q[n++] = 0; q[n++] = 1;   // PTR, IN
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(5353); a.sin_addr.s_addr = htonl(ip);
        std::string out;
        if (sendto(s, (char*)q, n, 0, (sockaddr*)&a, sizeof a) == n) {
            unsigned char b[512];
            int r = recvfrom(s, (char*)b, sizeof b, 0, 0, 0);
            if (r > 12) {
                int qd = (b[4] << 8) | b[5], an = (b[6] << 8) | b[7], p = 12;
                for (int i = 0; i < qd && p < r; i++) { dnsName(b, r, p); p += 4; }
                for (int i = 0; i < an && p + 10 <= r; i++) {
                    dnsName(b, r, p);
                    int type = (b[p] << 8) | b[p + 1], rdlen = (b[p + 8] << 8) | b[p + 9];
                    p += 10;
                    if (type == 12) {
                        int pp = p;
                        out = dnsName(b, r, pp);
                        size_t d = out.find('.');
                        if (d != std::string::npos) out.resize(d);
                        break;
                    }
                    p += rdlen;
                }
            }
        }
        closesocket(s);
        return out;
    }
};

// ======================================================================
// 제조사 DB (oui.txt)
// ======================================================================
class OuiDb {
public:
    explicit OuiDb(const std::string& path) {
        std::ifstream f(path);
        std::string l;
        while (std::getline(f, l)) {
            size_t p = l.find("(hex)");
            if (p == std::string::npos || p < 8) continue;
            std::string k = StrUtil::upper(l.substr(0, 8)), v = l.substr(p + 5);
            size_t b = v.find_first_not_of(" \t"), e = v.find_last_not_of(" \t\r\n");
            if (b != std::string::npos) map_[k] = v.substr(b, e - b + 1);
        }
    }
    bool empty() const { return map_.empty(); }
    std::string vendorOf(const std::string& mac) const {     // 랜덤(로컬 관리) MAC 은 제조사 없음
        unsigned first = 0;
        if (mac.size() < 8 || sscanf(mac.c_str(), "%2x", &first) != 1 || (first & 2)) return "";
        auto it = map_.find(mac.substr(0, 8));
        return it == map_.end() ? "" : it->second;
    }
private:
    std::map<std::string, std::string> map_;
};

// ======================================================================
// 서브넷 감지
// ======================================================================
class Subnet {
public:
    uint32_t net = 0, bc = 0;
    bool valid() const { return net != 0; }
    std::string range() const { return StrUtil::ipStr(net + 1) + " ~ " + StrUtil::ipStr(bc - 1); }
    bool detect() {                                           // 게이트웨이가 있는 활성 어댑터의 서브넷
        ULONG sz = 0; GetAdaptersInfo(0, &sz);
        std::vector<char> buf(sz ? sz : 1);
        auto* ai = (IP_ADAPTER_INFO*)buf.data();
        uint32_t myIp = 0, mask = 0;
        if (sz && GetAdaptersInfo(ai, &sz) == NO_ERROR) {
            for (auto* a = ai; a; a = a->Next) {
                uint32_t ip, mk;
                if (StrUtil::parseIp(a->IpAddressList.IpAddress.String, ip) && StrUtil::parseIp(a->IpAddressList.IpMask.String, mk) &&
                    ip && strcmp(a->GatewayList.IpAddress.String, "0.0.0.0") != 0) { myIp = ip; mask = mk; break; }
            }
        }
        if (!myIp) return false;
        net = myIp & mask; bc = net | ~mask;
        if (bc - net > 1022) { net = myIp & 0xFFFFFF00u; bc = net | 255; }
        return true;
    }
};

// ======================================================================
// ARP 확인
// ======================================================================
class Arp {
public:
    static std::string macOf(uint32_t ip) {
        ULONG m[2] = {0}, len = 6;
        if (SendARP(htonl(ip), 0, m, &len) != NO_ERROR || len != 6) return "";
        return StrUtil::macStr((unsigned char*)m);
    }
    // 서브넷 전체에서 mac 을 가진 IP 를 찾는다 (없으면 0). 동시 ARP 가 너무 많으면 느린 기기의 응답을 놓치므로 16 스레드.
    static uint32_t find(const Subnet& sn, const std::string& mac) {
        std::atomic<uint32_t> next(sn.net + 1), hit(0);
        const uint32_t bc = sn.bc;
        auto w = [&]() {
            for (;;) {
                uint32_t ip = next++;
                if (ip >= bc || hit) break;
                if (macOf(ip) == mac) hit = ip;
            }
        };
        std::vector<std::thread> th;
        for (int i = 0; i < 16; i++) th.emplace_back(w);
        for (auto& t : th) t.join();
        return hit;
    }
    // 한 IP 를 직접 확인: ARP 3회 재시도, 안 되면 ping 으로 깨운 뒤 한 번 더 ARP
    static bool check(uint32_t ip, const std::string& mac) {
        for (int t = 0; t < 3; t++) if (macOf(ip) == mac) return true;
        HANDLE ic = IcmpCreateFile();
        if (ic != INVALID_HANDLE_VALUE) {
            char data[4] = {'p', 'i', 'n', 'g'}, reply[sizeof(ICMP_ECHO_REPLY) + 64];
            IcmpSendEcho(ic, htonl(ip), data, 4, 0, reply, sizeof reply, 1500);
            IcmpCloseHandle(ic);
        }
        return macOf(ip) == mac;
    }
};

// ======================================================================
// 이전 결과 / 결과 파일
// ======================================================================
struct BtDev { ULONGLONG addr = 0; std::string name, user; bool on = false, paired = false; };
struct Host { uint32_t ip; bool on; std::string name, mac, vendor, user; };

class PrevResult {
public:
    std::vector<std::vector<std::string>> wifi, bt;           // 이전 결과 파일의 ON/OFF 줄 (탭 분리)
    static PrevResult load(const std::string& path) {
        PrevResult p;
        std::ifstream f(path, std::ios::binary);
        std::string l; bool first = true, bt = false;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (first) { first = false; if (l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3); }
            if (l == "[Bluetooth]") { bt = true; continue; }
            if (l == "[Wi-Fi]") { bt = false; continue; }      // 구역 표시가 없는 예전 파일은 Wi-Fi 로 본다
            auto c = StrUtil::split(l, '\t');
            if (c.size() < 3 || (c[0] != "ON" && c[0] != "OFF")) continue;
            (bt ? p.bt : p.wifi).push_back(c);
        }
        return p;
    }
};

// ======================================================================
// 블루투스
// ======================================================================
class Bluetooth {
public:
    static ULONGLONG addrOf(const std::string& norm) {        // "AA-BB-CC-DD-EE-FF" -> 0xAABBCCDDEEFF
        ULONGLONG v = 0;
        for (char c : norm) if (isxdigit((unsigned char)c)) v = (v << 4) | (ULONGLONG)(isdigit((unsigned char)c) ? c - '0' : toupper((unsigned char)c) - 'A' + 10);
        return v;
    }
    static std::string str(ULONGLONG a) {
        char t[32];
        snprintf(t, sizeof t, "%02X-%02X-%02X-%02X-%02X-%02X", (unsigned)((a >> 40) & 255), (unsigned)((a >> 32) & 255),
                 (unsigned)((a >> 24) & 255), (unsigned)((a >> 16) & 255), (unsigned)((a >> 8) & 255), (unsigned)(a & 255));
        return t;
    }
    // 1) 이미 연결 상태이면 바로 "있음"  2) 아니면 페어링 여부를 확인한 뒤 직접 불러 본다
    static bool present(ULONGLONG addr, std::string& info) {
        BLUETOOTH_DEVICE_INFO di = {};
        di.dwSize = sizeof di; di.Address.ullLong = addr;
        DWORD r = BluetoothGetDeviceInfo(nullptr, &di);
        if (r != ERROR_SUCCESS) { info = "BluetoothGetDeviceInfo 오류 " + std::to_string(r) + " (어댑터 없음/꺼짐?)"; return false; }
        if (di.fConnected) { info = "연결 상태"; return true; }
        if (!di.fRemembered && !di.fAuthenticated) { info = "PC 에 페어링되어 있지 않음 (먼저 폰과 페어링 필요)"; return false; }
        return probe(addr, info);
    }
    // 블루투스 스캔: 페어링된 장치(연결 상태/직접 불러 보기) + 주변의 검색 가능한 장치(탐색). 이전 결과와 병합.
    static std::vector<BtDev> scan(const PrevResult& prev) {
        std::map<ULONGLONG, BtDev> m;
        for (int pass = 0; pass < 2; pass++) {                // 0: 페어링 목록, 1: 주변 탐색(약 5초)
            BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {};
            sp.dwSize = sizeof sp; sp.fReturnAuthenticated = TRUE; sp.fReturnRemembered = TRUE; sp.fReturnConnected = TRUE;
            sp.fReturnUnknown = pass ? TRUE : FALSE; sp.fIssueInquiry = pass ? TRUE : FALSE;
            sp.cTimeoutMultiplier = pass ? 4 : 0; sp.hRadio = nullptr;
            BLUETOOTH_DEVICE_INFO di = {};
            di.dwSize = sizeof di;
            HBLUETOOTH_DEVICE_FIND h = BluetoothFindFirstDevice(&sp, &di);
            if (!h) continue;
            do {
                BtDev& d = m[di.Address.ullLong];
                d.addr = di.Address.ullLong;
                std::string n = StrUtil::u8(di.szName);
                if (!n.empty()) d.name = n;
                const bool paired = di.fRemembered || di.fAuthenticated;
                if (paired) d.paired = true;
                if (di.fConnected || (pass && !paired)) d.on = true;   // 연결 중이거나, 탐색에서 새로 발견된 장치
            } while (BluetoothFindNextDevice(h, &di));
            BluetoothFindDeviceClose(h);
        }
        std::vector<std::thread> th;                          // 연결 안 된 페어링 장치는 직접 불러 본다 (동시에)
        for (auto& kv : m)
            if (kv.second.paired && !kv.second.on)
                th.emplace_back([d = &kv.second]() { std::string info; d->on = present(d->addr, info); });
        for (auto& t : th) t.join();

        for (auto& c : prev.bt) {                             // 이전 결과 병합: User Text 유지, 이번에 안 보인 장치는 OFF
            const std::string nm = StrUtil::normMac(c[1]);
            if (nm.empty()) continue;
            const ULONGLONG a = addrOf(nm);
            auto it = m.find(a);
            const std::string user = c.size() > 4 ? c[4] : "";
            if (it != m.end()) { it->second.user = user; if (it->second.name.empty()) it->second.name = c[2]; }
            else { BtDev d; d.addr = a; d.name = c[2]; d.paired = c.size() > 3 && c[3] == "paired"; d.user = user; m[a] = d; }
        }
        std::vector<BtDev> v;
        for (auto& kv : m) v.push_back(kv.second);
        return v;
    }
private:
    static constexpr int kProbeMs = 8000;                     // 블루투스 호출(page) 대기 시간
    // 장치를 직접 불러 본다 (RFCOMM 채널 1 연결 시도). 연결되거나 "거부됨" 이면 장치가 응답한 것 -> 근처에 있음.
    // 시간 초과/네트워크 오류면 응답 없음. 페어링된 장치만 시험한다 (미페어링 장치를 부르면 PC 에 페어링 요청이 뜰 수 있음).
    static bool probe(ULONGLONG addr, std::string& info) {
        SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (s == INVALID_SOCKET) { info = "블루투스 소켓 생성 실패 (오류 " + std::to_string(WSAGetLastError()) + ", 어댑터 없음/꺼짐?)"; return false; }
        SOCKADDR_BTH sa = {};
        sa.addressFamily = AF_BTH; sa.btAddr = addr; sa.port = 1;
        u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
        bool ok = false;
        if (connect(s, (sockaddr*)&sa, sizeof sa) == 0) {
            ok = true; info = "응답함 (연결됨)";
        } else {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) {
                fd_set wf, ef; FD_ZERO(&wf); FD_ZERO(&ef); FD_SET(s, &wf); FD_SET(s, &ef);
                timeval tv; tv.tv_sec = kProbeMs / 1000; tv.tv_usec = (kProbeMs % 1000) * 1000;
                if (select(0, nullptr, &wf, &ef, &tv) > 0) {
                    int err = 0, len = sizeof err;
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
                    if (err == 0 && FD_ISSET(s, &wf)) { ok = true; info = "응답함 (연결됨)"; }
                    else if (err == WSAECONNREFUSED) { ok = true; info = "응답함 (서비스 거부 = 장치는 근처에 있음)"; }
                    else info = "응답 없음 (오류 " + std::to_string(err) + ")";
                } else info = "응답 없음 (" + std::to_string(kProbeMs / 1000) + "초 시간 초과)";
            } else if (e == WSAECONNREFUSED) {
                ok = true; info = "응답함 (서비스 거부 = 장치는 근처에 있음)";
            } else {
                info = "응답 없음 (오류 " + std::to_string(e) + ")";
            }
        }
        closesocket(s);
        return ok;
    }
};

class ResultFile {
public:
    explicit ResultFile(std::string path) : path_(std::move(path)) {}
    const std::string& path() const { return path_; }
    // 블루투스 구역: 파일을 새로 만든다
    void writeBluetooth(const std::vector<BtDev>& bt, int& onCount) const {
        std::ofstream o(path_, std::ios::binary | std::ios::trunc);
        o << "\xEF\xBB\xBF" << "[Bluetooth]\r\n" << "Status\tBluetooth Address\tDevice Name\tPaired\tUser Text\r\n";
        onCount = 0;
        for (auto& d : bt) {
            onCount += d.on;
            o << (d.on ? "ON" : "OFF") << '\t' << Bluetooth::str(d.addr) << '\t' << (d.name.empty() ? "<UnKnown>" : d.name) << '\t'
              << (d.paired ? "paired" : "-") << '\t' << d.user << "\r\n";
        }
        o << "\r\n";
    }
    // Wi-Fi 구역: 블루투스 구역 뒤에 추가
    void appendWifi(const std::vector<Host>& hosts, int& onCount) const {
        std::ofstream o(path_, std::ios::binary | std::ios::app);
        o << "[Wi-Fi]\r\n" << "Status\tIP Address\tMAC Address\tComputer Name\tNetwork Adapter Company\tUser Text\r\n";
        onCount = 0;
        for (auto& h : hosts) {
            onCount += h.on;
            o << (h.on ? "ON" : "OFF") << '\t' << StrUtil::ipStr(h.ip) << '\t' << h.mac << '\t'
              << (h.name.empty() ? "<UnKnown>" : h.name) << '\t' << h.vendor << '\t' << h.user << "\r\n";
        }
    }
private:
    std::string path_;
};

// ======================================================================
// Wi-Fi 전체 스캔 (ping + ARP, 병렬)
// 이전 결과에 있던 IP 는 절전 기기일 수 있으므로 최대 3회 재시도, 이번에 응답 없는 기존 장치는 OFF 로 남기고 User Text 는 유지.
// ======================================================================
class WifiScanner {
public:
    WifiScanner(const Subnet& sn, const PrevResult& prev, const ResultFile& out) : sn_(sn), prev_(prev), out_(out) {}
    void run() {
        OuiDb oui(AppDir::ouiFile());
        if (oui.empty()) Logger::log("(oui.txt 없음: 제조사명은 비어 있습니다)");
        std::set<uint32_t> known;
        for (auto& c : prev_.wifi) { uint32_t kip; if (c.size() > 1 && StrUtil::parseIp(c[1], kip)) known.insert(kip); }
        std::vector<Host> found; std::mutex mu; std::atomic<uint32_t> next(sn_.net + 1);
        const uint32_t bc = sn_.bc;
        auto worker = [&]() {
            HANDLE ic = IcmpCreateFile();
            char data[4] = {'p', 'i', 'n', 'g'}, reply[sizeof(ICMP_ECHO_REPLY) + 64];
            for (;;) {
                uint32_t ip = next++;
                if (ip >= bc) break;
                bool on = false, arp = false;
                ULONG m[2] = {0};
                for (int t = known.count(ip) ? 3 : 1; t > 0 && !on && !arp; t--) {
                    on = IcmpSendEcho(ic, htonl(ip), data, 4, 0, reply, sizeof reply, 1000) > 0 &&
                         ((ICMP_ECHO_REPLY*)reply)->Status == IP_SUCCESS;
                    ULONG len = 6;
                    arp = SendARP(htonl(ip), 0, m, &len) == NO_ERROR && len == 6;
                }
                if (!on && !arp) continue;
                Host h; h.ip = ip; h.on = true;
                if (arp) { h.mac = StrUtil::macStr((unsigned char*)m); h.vendor = oui.vendorOf(h.mac); }
                h.name = NameResolver::hostName(ip);
                std::lock_guard<std::mutex> g(mu);
                found.push_back(h);
            }
            IcmpCloseHandle(ic);
        };
        std::vector<std::thread> th;
        for (int i = 0; i < 64; i++) th.emplace_back(worker);
        for (auto& t : th) t.join();
        mergePrevious(found);
        std::sort(found.begin(), found.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
        int on = 0;
        out_.appendWifi(found, on);
        Logger::log("Wi-Fi 스캔 완료: " + std::to_string(found.size()) + "개 장치 (ON " + std::to_string(on) + " / OFF " +
                    std::to_string(found.size() - on) + ") -> " + out_.path());
    }
    // 네트워크가 없을 때: 이전 결과만 OFF 로 남긴다
    void runOffline() {
        std::vector<Host> found;
        mergePrevious(found);
        std::sort(found.begin(), found.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
        int on = 0;
        out_.appendWifi(found, on);
        Logger::log("Wi-Fi 스캔 완료: " + std::to_string(found.size()) + "개 장치 (ON " + std::to_string(on) + " / OFF " +
                    std::to_string(found.size() - on) + ") -> " + out_.path());
    }
private:
    void mergePrevious(std::vector<Host>& found) const {      // 컬럼 순서: Status, IP, MAC, Name, Company, User Text
        for (auto& c : prev_.wifi) {
            uint32_t ip;
            if (c.size() < 5 || !StrUtil::parseIp(c[1], ip)) continue;
            std::string user = c.size() > 5 ? c[5] : "";
            std::string pname = (c[3] == "<UnKnown>") ? "" : c[3];
            auto it = std::find_if(found.begin(), found.end(), [&](const Host& h) { return h.ip == ip; });
            if (it != found.end()) { it->user = user; if (it->name.empty()) it->name = pname; }
            else found.push_back({ip, false, pname, c[2], c[4], user});
        }
    }
    const Subnet& sn_;
    const PrevResult& prev_;
    const ResultFile& out_;
};

// ======================================================================
// 종료 파일 (wifiscanTriger.txt) : 시작할 때 무조건 지우고, 그 뒤 내용이 END 이면 스스로 종료
// ======================================================================
class EndFile {
public:
    static void remove() { DeleteFileA(AppDir::endFile().c_str()); }
    static bool requested() {
        std::ifstream f(AppDir::endFile(), std::ios::binary);
        if (!f) return false;
        std::string l; std::getline(f, l);
        if (l.compare(0, 3, "\xEF\xBB\xBF") == 0) l.erase(0, 3);
        return StrUtil::upper(StrUtil::trim(l)) == "END";
    }
    // seconds 초 동안 1초 단위로 쉬면서 종료 파일을 본다. 종료 요청이 있으면 true
    static bool sleepOrEnd(int seconds) {
        for (int i = 0; i < seconds; i++) { if (requested()) return true; Sleep(1000); }
        return requested();
    }
};

// ======================================================================
// 명령 전달 파일 (trigger.txt): screen_telegram.exe 가 1초마다 읽어 처리한다
// ======================================================================
class TriggerFile {
public:
    explicit TriggerFile(std::string path) : path_(std::move(path)) {}
    const std::string& path() const { return path_; }
    // 임시 파일에 쓴 뒤 이름 바꾸기 -> 반쯤 쓰인 파일을 읽지 않음
    void send(const std::string& cmd) const {
        const std::string tmp = path_ + ".tmp";
        bool ok = false;
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (f) { f << cmd << "\r\n"; f.flush(); ok = f.good(); }
        }
        if (ok) ok = MoveFileExA(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        DWORD err = ok ? 0 : GetLastError();
        if (!ok) DeleteFileA(tmp.c_str());
        Logger::log(std::string("트리거 파일 ") + (ok ? "기록 성공" : "기록 실패 (오류 " + std::to_string(err) + ", screen_telegram 폴더 확인)") + ": " + cmd);
    }
private:
    std::string path_;
};

// ======================================================================
// 집 안팎 감시
// ======================================================================
class HomeWatcher {
public:
    HomeWatcher(Subnet sn) : sn_(sn), ini_(AppDir::iniFile()), trigger_(AppDir::triggerFile()) {}

    int run() {
        loadIni();
        int v = atoi(ini_.get("WiFiScanWatchInterval").c_str());   // 없으면 15초
        interval_ = v > 0 ? std::max(5, v) : 15;
        int nh = atoi(ini_.get("NotHome").c_str());
        awaySec_ = nh > 0 ? nh : 60;                               // 외출 판정: INI NotHome(초), 없으면 60초
        Logger::log("INI: " + ini_.path() + (btMac_.empty() ? " (HomeBluetooth 없음: Wi-Fi 만 사용)" : "") + (mac_.empty() ? " (HomeMac 없음)" : "") + " / 트리거 파일: " + trigger_.path());
        Logger::log("감시 시작: 블루투스 " + (btMac_.empty() ? std::string("(없음)") : btMac_) + " -> Wi-Fi " + (mac_.empty() ? std::string("(없음)") : mac_) +
                    " (간격 " + std::to_string(interval_) + "초, 외출 판정 " + std::to_string(awaySec_) + "초)");
        for (;;) try {
            if (EndFile::requested()) return endNow();
            if (mac_.empty() && btMac_.empty()) {      // 설정 오류여도 종료하지 않고 INI 를 다시 읽으며 대기
                loadIni();
                if (mac_.empty() && btMac_.empty()) {
                    Logger::log("HomeBluetooth 와 HomeMac 을 모두 읽지 못했습니다. 재시도 중");
                    if (EndFile::sleepOrEnd(interval_)) return endNow();
                    continue;
                }
                Logger::log("HomeBluetooth: " + (btMac_.empty() ? std::string("(없음)") : btMac_) + " / HomeMac: " + (mac_.empty() ? std::string("(없음)") : mac_));
            }
            std::string via;
            const bool present = checkBluetooth(via) || checkWifi(via);   // 1) 블루투스 우선  2) 확인되지 않으면 Wi-Fi
            update(present, via);
            if (EndFile::sleepOrEnd(interval_)) return endNow();
        } catch (const std::exception& e) {
            Logger::log(std::string("오류: ") + e.what());
            if (EndFile::sleepOrEnd(interval_)) return endNow();
        }
    }

private:
    int endNow() { Logger::log("wifiscanTriger.txt = END: 종료합니다"); return 0; }

    void loadIni() {
        mac_ = StrUtil::normMac(ini_.get("HomeMac"));
        btMac_ = StrUtil::normMac(ini_.get("HomeBluetooth"));
        btAddr_ = btMac_.empty() ? 0 : Bluetooth::addrOf(btMac_);
        if (!StrUtil::parseIp(ini_.get("HomeIp"), homeIp_)) homeIp_ = 0;   // 선택: 폰 고정 IP
    }

    bool checkBluetooth(std::string& via) {
        if (!btAddr_) return false;
        std::string info;
        const bool ok = Bluetooth::present(btAddr_, info);
        if (ok) via = "Bluetooth";
        if (info != lastBtInfo_) { Logger::log("Bluetooth: " + info); lastBtInfo_ = info; }
        return ok;
    }

    bool checkWifi(std::string& via) {
        if (mac_.empty()) return false;
        if (!sn_.valid() && sn_.detect())     // 시작할 때 네트워크가 없었다면 다시 찾는다
            Logger::log("네트워크 어댑터 감지: Wi-Fi 확인 시작 (" + sn_.range() + ")");
        if (!sn_.valid()) return false;
        bool ok = false;
        if (homeIp_ && Arp::check(homeIp_, mac_)) { ip_ = homeIp_; ok = true; }
        if (!ok && ip_ && ip_ != homeIp_) ok = Arp::check(ip_, mac_);
        if (!ok) { ip_ = Arp::find(sn_, mac_); ok = (ip_ != 0); }
        if (ok) via = "Wi-Fi " + StrUtil::ipStr(ip_);
        const std::string wi = ok ? "확인됨 (" + StrUtil::ipStr(ip_) + ")"
                                  : "응답 없음 (HomeMac " + mac_ + ", 범위 " + sn_.range() + ")";
        if (wi != lastWifiInfo_) { Logger::log("Wi-Fi: " + wi); lastWifiInfo_ = wi; }
        return ok;
    }

    // 상태 판정: -1 미정 -> 알림, 0(AWAY) -> 1(HOME) 이면 /HomeBack, 1 -> 0 은 awaySec_ 초 연속 미확인일 때 /NotHome
    void update(bool present, const std::string& via) {
        const time_t now = time(0);
        if (present) lastSeen_ = now;
        if (state_ == -1) {
            state_ = present ? 1 : 0;
            Logger::log(std::string("초기 상태: ") + (present ? "HOME (" + via + ")" : "AWAY"));
            trigger_.send(present ? "/HomeBack" : "/NotHome");     // 시작 시 현재 상태도 전송
        } else if (state_ == 0 && present) {
            state_ = 1;
            Logger::log("HOME (" + via + ")");
            trigger_.send("/HomeBack");
        } else if (state_ == 1 && !present && now - lastSeen_ >= awaySec_) {
            state_ = 0;
            Logger::log("AWAY (" + std::to_string(awaySec_) + "초 연속 미확인: 블루투스, Wi-Fi)");
            trigger_.send("/NotHome");
        }
    }

    Subnet sn_;
    IniFile ini_;
    TriggerFile trigger_;
    std::string mac_, btMac_, lastBtInfo_, lastWifiInfo_;
    ULONGLONG btAddr_ = 0;
    uint32_t homeIp_ = 0, ip_ = 0;
    int interval_ = 15, awaySec_ = 60, state_ = -1;           // state_: -1 미정, 1 HOME, 0 AWAY
    time_t lastSeen_ = 0;
};

// ======================================================================
// 전체 실행 순서
// ======================================================================
class WiFiScanApp {
public:
    int run() {
        EndFile::remove();                                    // 시작할 때 종료 파일은 무조건 지운다 (예전 END 로 바로 종료되지 않도록)
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        Logger::log("WiFiScan 시작: 1) 블루투스 스캔 2) Wi-Fi 스캔 3) 감시 (종료: " + AppDir::endFile() + " 에 END)");

        // 부팅 직후에는 네트워크가 아직 준비되지 않았을 수 있으므로 최대 60초 기다린다 (없으면 블루투스만으로 진행, 감시 중 다시 찾음)
        Subnet sn;
        for (int i = 0; i < 12 && !sn.detect(); i++) {
            if (i == 0) Logger::log("네트워크 어댑터 대기 중 (최대 60초)");
            Sleep(5000);
        }
        if (sn.valid()) Logger::log("스캔 범위: " + sn.range());
        else Logger::log("네트워크 어댑터를 찾지 못했습니다: Wi-Fi 스캔은 건너뛰고 블루투스로 감시합니다 (어댑터가 생기면 자동으로 Wi-Fi 확인 시작)");

        ResultFile out(AppDir::resultFile());
        PrevResult prev = PrevResult::load(out.path());       // 새 파일을 만들기 전에 이전 결과를 읽어 둔다

        // 1) 블루투스 스캔 -> 결과 파일 새로 생성
        Logger::log("블루투스 스캔 중 (주변 탐색 약 5초 + 페어링 장치 확인)");
        auto bt = Bluetooth::scan(prev);
        int on = 0;
        out.writeBluetooth(bt, on);
        Logger::log("블루투스 스캔 완료: " + std::to_string(bt.size()) + "개 장치 (ON " + std::to_string(on) + " / OFF " +
                    std::to_string(bt.size() - on) + ")" + (bt.empty() ? " - 어댑터 없음/꺼짐?" : "") + " -> " + out.path());

        // 2) Wi-Fi 전체 스캔 -> 같은 파일에 추가 (네트워크가 없으면 이전 결과만 OFF 로 남긴다)
        WifiScanner ws(sn, prev, out);
        if (sn.valid()) { Logger::log("Wi-Fi 스캔 중"); ws.run(); }
        else ws.runOffline();

        // 3) 감시
        int r = HomeWatcher(sn).run();
        WSACleanup();
        return r;
    }
};

// 치명적 오류(크래시) 시 로그에 기록
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    char b[64];
    snprintf(b, sizeof b, "치명적 오류 0x%08lX", ep->ExceptionRecord->ExceptionCode);
    Logger::log(b);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter(crashHandler);
    try {
        return WiFiScanApp().run();
    } catch (const std::exception& e) {
        Logger::log(std::string("치명적 오류: ") + e.what());
        return 2;
    }
}
