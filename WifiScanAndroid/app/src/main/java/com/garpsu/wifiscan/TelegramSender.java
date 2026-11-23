package com.garpsu.wifiscan;

import android.content.Context;
import android.location.Address;
import android.location.Geocoder;
import android.location.Location;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/** 현재 위치를 텔레그램 메시지로 만들어 봇으로 보낸다. (네트워크 사용 -> 작업 스레드에서 호출) */
final class TelegramSender {

    static final String PREF = "tg";
    static final int[] INTERVALS = {5, 10, 15, 30, 60, 180}; // 분
    static final String[] INTERVAL_LABELS = {"5분", "10분", "15분", "30분", "1시간", "3시간"};

    private TelegramSender() {
    }

    static String intervalLabel(int min) {
        for (int i = 0; i < INTERVALS.length; i++) {
            if (INTERVALS[i] == min) {
                return INTERVAL_LABELS[i];
            }
        }
        return min + "분";
    }

    /*
     * 메시지 첫 줄: 지금 상태
     *   🏠 홈 (우리집 거실) · 공유기 MyHome (AA:BB:CC:DD:EE:01)   ← 별명이 없으면 "🏠 홈 · 공유기 ..."
     *   🏠 홈 · 블루투스 GARPSU-PC (11:22:33:44:55:66)
     *   🚶 외출
     *   ❔ 상태 확인 불가 (이유)
     */
    /*
     * 보호자 회신용 공유기 식별 정보 (등록 안 된 공유기도 최대한 자세히, 별명 줄은 항상 표시)
     * ■ 연결된 공유기
     *    이름(SSID): MyHome
     *    별명: 우리집 거실            (없으면 "(없음)")
     *    등록: 홈 / 기타 / 등록 안 됨
     *    MAC(BSSID): AA:BB:CC:DD:EE:01
     *    신호: -45 dBm (강) · 5GHz 36번 채널 (5180 MHz) · 연결 속도 866 Mbps
     *    폰 IP: 192.168.0.12 · 게이트웨이: 192.168.0.1
     * ■ 주변 공유기 (신호 센 순, 최대 5개)
     *    1) Cafe_WiFi · 별명 (없음) · 등록 안 됨
     *       AA:BB:.. · -60 dBm · 2.4GHz 6번 채널
     */
    @SuppressWarnings("deprecation")
    static String wifiDetails(Context ctx) {
        StringBuilder sb = new StringBuilder();
        android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager)
                ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm == null || !wm.isWifiEnabled()) {
            return "■ 연결된 공유기\n   Wi-Fi 꺼짐";
        }
        String conn = null;
        try {
            conn = HomeService.connectedBssid(ctx);
            android.net.wifi.WifiInfo wi = wm.getConnectionInfo();
            sb.append("■ 연결된 공유기\n");
            if (conn == null || wi == null) {
                sb.append("   연결 없음\n");
            } else {
                String ssid = wi.getSSID();
                if (ssid != null && ssid.length() >= 2 && ssid.startsWith("\"") && ssid.endsWith("\"")) {
                    ssid = ssid.substring(1, ssid.length() - 1);
                }
                if (ssid == null || ssid.isEmpty() || ssid.equals("<unknown ssid>")) {
                    ssid = "<숨김>";
                }
                String alias = MainActivity.routerAlias(ctx, conn);
                sb.append("   이름(SSID): ").append(ssid).append('\n')
                        .append("   별명: ").append(alias.isEmpty() ? "(없음)" : alias).append('\n')
                        .append("   등록: ").append(routerKind(ctx, conn)).append('\n')
                        .append("   MAC(BSSID): ").append(conn.toUpperCase(Locale.US)).append('\n')
                        .append("   신호: ").append(wi.getRssi()).append(" dBm (").append(strength(wi.getRssi())).append(")");
                int freq = wi.getFrequency();
                if (freq > 0) {
                    sb.append(" · ").append(band(freq));
                }
                if (wi.getLinkSpeed() > 0) {
                    sb.append(" · 연결 속도 ").append(wi.getLinkSpeed()).append(" Mbps");
                }
                sb.append('\n');
                int ip = wi.getIpAddress();
                android.net.DhcpInfo dhcp = wm.getDhcpInfo();
                if (ip != 0) {
                    sb.append("   폰 IP: ").append(ipText(ip));
                    if (dhcp != null && dhcp.gateway != 0) {
                        sb.append(" · 게이트웨이: ").append(ipText(dhcp.gateway));
                    }
                    sb.append('\n');
                }
            }
        } catch (Exception e) {
            sb.append("   (확인 실패)\n");
        }
        // 주변 공유기: 마지막 검색 결과에서 신호 센 순으로 (연결된 공유기는 빼고)
        try {
            List<android.net.wifi.ScanResult> list = new java.util.ArrayList<>(wm.getScanResults());
            java.util.Collections.sort(list, (left, right) -> right.level - left.level);
            int count = 0;
            StringBuilder near = new StringBuilder();
            for (android.net.wifi.ScanResult scan : list) {
                String mac = scan.BSSID == null ? "" : scan.BSSID.toLowerCase(Locale.US);
                if (mac.isEmpty() || mac.equals(conn)) {
                    continue;
                }
                count++;
                String alias = MainActivity.routerAlias(ctx, mac);
                near.append("   ").append(count).append(") ")
                        .append(scan.SSID == null || scan.SSID.isEmpty() ? "<숨김>" : scan.SSID)
                        .append(" · 별명 ").append(alias.isEmpty() ? "(없음)" : alias)
                        .append(" · ").append(routerKind(ctx, mac)).append('\n')
                        .append("      ").append(mac.toUpperCase(Locale.US))
                        .append(" · ").append(scan.level).append(" dBm · ").append(band(scan.frequency)).append('\n');
                if (count >= 5) {
                    break;
                }
            }
            if (count > 0) {
                sb.append("\n■ 주변 공유기 (신호 센 순, 최대 5개)\n").append(near);
            }
        } catch (Exception ignored) {
        }
        return sb.toString().trim();
    }

    /*
     * 보호자 회신(이메일·텔레그램)용 주변 블루투스 (약 6초 검색, 신호 센 순, 최대 10개)
     * ■ 주변 블루투스 (6초 검색 · 12개 발견)
     *    1) GARPSU-PC · 별명 거실 PC · 홈
     *       11:22:33:44:55:66 · -52 dBm
     *    2) (이름 없음) · 별명 (없음) · 등록 안 됨
     *       5A:3C:..:.. · -70 dBm
     * 오래 걸림(검색 시간) -> 작업 스레드에서 호출
     */
    static String btDetails(Context ctx) {
        String problem = BtHome.scanProblem(ctx);
        if (problem != null) {
            return "■ 주변 블루투스\n   " + problem;
        }
        List<String[]> list = BtHome.scanNearby(ctx, 6000);
        if (list == null) {
            return "■ 주변 블루투스\n   검색 실패";
        }
        StringBuilder sb = new StringBuilder("■ 주변 블루투스 (6초 검색 · ").append(list.size()).append("개 발견)");
        if (list.isEmpty()) {
            sb.append("\n   찾은 기기 없음");
        }
        int count = 0;
        for (String[] dev : list) {
            count++;
            String alias = BtHome.alias(ctx, dev[0]);
            sb.append("\n   ").append(count).append(") ").append(dev[1].isEmpty() ? "(이름 없음)" : dev[1])
                    .append(" · 별명 ").append(alias.isEmpty() ? "(없음)" : alias)
                    .append(" · ").append(BtHome.kindOf(ctx, dev[0]))
                    .append("\n      ").append(dev[0]).append(" · ").append(dev[2]).append(" dBm");
            if (count >= 10) {
                break;
            }
        }
        return sb.toString();
    }

    // 짧은 문자용 공유기 한 줄: "📶 MyHome (AA:BB:CC:DD:EE:01) 별명 우리집 거실 · 홈"
    @SuppressWarnings("deprecation")
    static String wifiShortLine(Context ctx) {
        try {
            android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager)
                    ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm == null || !wm.isWifiEnabled()) {
                return "📶 Wi-Fi 꺼짐";
            }
            String conn = HomeService.connectedBssid(ctx);
            android.net.wifi.WifiInfo wi = wm.getConnectionInfo();
            if (conn == null || wi == null) {
                return "📶 공유기 연결 없음";
            }
            String ssid = wi.getSSID();
            if (ssid != null && ssid.length() >= 2 && ssid.startsWith("\"") && ssid.endsWith("\"")) {
                ssid = ssid.substring(1, ssid.length() - 1);
            }
            if (ssid == null || ssid.isEmpty() || ssid.equals("<unknown ssid>")) {
                ssid = "<숨김>";
            }
            String alias = MainActivity.routerAlias(ctx, conn);
            return "📶 " + ssid + " (" + conn.toUpperCase(Locale.US) + ") 별명 "
                    + (alias.isEmpty() ? "(없음)" : alias) + " · " + routerKind(ctx, conn);
        } catch (Exception e) {
            return "📶 공유기 확인 실패";
        }
    }

    // 등록 상태: 홈 / 기타 / 등록 안 됨
    private static String routerKind(Context ctx, String bssid) {
        for (String entry : ctx.getSharedPreferences("home", Context.MODE_PRIVATE)
                .getStringSet("routers", new java.util.HashSet<String>())) {
            if (entry.split("\\|", 2)[0].equals(bssid)) {
                return MainActivity.routerIsHome(ctx, bssid) ? "홈" : "기타";
            }
        }
        return "등록 안 됨";
    }

    private static String strength(int rssi) {
        return rssi >= -55 ? "강" : rssi >= -67 ? "중" : rssi >= -78 ? "약" : "매우 약";
    }

    // 주파수 -> "5GHz 36번 채널 (5180 MHz)"
    private static String band(int freq) {
        int ch;
        String name;
        if (freq >= 2400 && freq < 2500) {
            name = "2.4GHz";
            ch = freq == 2484 ? 14 : (freq - 2407) / 5;
        } else if (freq >= 4900 && freq < 5900) {
            name = "5GHz";
            ch = (freq - 5000) / 5;
        } else if (freq >= 5925 && freq < 7125) {
            name = "6GHz";
            ch = (freq - 5950) / 5;
        } else {
            return freq + " MHz";
        }
        return name + " " + ch + "번 채널 (" + freq + " MHz)";
    }

    // Android 가 주는 IPv4 정수(작은 자리부터) -> "192.168.0.12"
    private static String ipText(int ip) {
        return (ip & 0xFF) + "." + ((ip >> 8) & 0xFF) + "." + ((ip >> 16) & 0xFF) + "." + ((ip >> 24) & 0xFF);
    }

    static String statusLine(Context ctx) {
        android.content.SharedPreferences prefs = ctx.getSharedPreferences("home", Context.MODE_PRIVATE);
        java.util.Set<String> routers = prefs.getStringSet("routers", new java.util.HashSet<String>());
        String bssid = null;
        try {
            bssid = HomeService.connectedBssid(ctx);
        } catch (Exception ignored) {
        }
        String otherLine = null; // '기타' 공유기에 연결됨 (귀가로 보지 않음, 외출 줄에 어디인지 덧붙임)
        if (bssid != null) {
            for (String entry : routers) {
                String[] parts = entry.split("\\|", 2);
                if (parts[0].equals(bssid)) {
                    String ssid = parts.length > 1 && !parts[1].isEmpty() ? parts[1] : "<숨김>";
                    // 공유기 등록 별명(1010 에서 입력)이 있으면 "홈 (우리집 거실)" 처럼 함께
                    String alias = MainActivity.routerAlias(ctx, bssid);
                    String where = " · 공유기 " + ssid + " (" + bssid.toUpperCase(Locale.US) + ")";
                    if (MainActivity.routerIsHome(ctx, bssid)) {
                        return "🏠 홈" + (alias.isEmpty() ? "" : " (" + alias + ")") + where;
                    }
                    otherLine = (alias.isEmpty() ? "" : " (" + alias + ")") + where;
                    break;
                }
            }
        }
        String btName = BtHome.seenName(ctx), btMac = BtHome.seenMac(ctx);
        String btAlias = BtHome.alias(ctx, btMac); // 블루투스 기기 별명 (1010 에서 입력)
        String btWhere = (btAlias.isEmpty() ? "" : " (" + btAlias + ")")
                + " · 블루투스 " + btName + (btMac.isEmpty() ? "" : " (" + btMac + ")");
        if (BtHome.seenHome(ctx)) {
            return "🏠 홈" + btWhere;
        }
        if (otherLine != null) {
            return "🚶 외출" + otherLine; // 예: 🚶 외출 (할머니댁) · 공유기 Grandma (AA:...)
        }
        if (BtHome.seenOther(ctx)) {
            return "🚶 외출" + btWhere; // '기타' 블루투스 기기(회사 PC 등)가 가까이 있음
        }
        boolean btReg = BtHome.hasHome(ctx); // '홈'으로 체크된 블루투스 기기
        if (!MainActivity.hasHomeRouter(ctx) && !btReg) {
            return "❔ 상태 확인 불가 (홈 공유기 미등록)";
        }
        if (!btReg && !HomeService.locationOn(ctx)) {
            return "❔ 상태 확인 불가 (위치 서비스 꺼짐)";
        }
        return "🚶 외출";
    }

    // 위치 -> 메시지 (주소 조회 포함). 첫 줄 = 지금 상태
    static String message(Context ctx, Location loc) {
        return message(ctx, loc, true);
    }

    // withStatus=false: 상태 줄 없이 (상태 줄을 맨 앞에 따로 붙이는 알림용)
    static String message(Context ctx, Location loc, boolean withStatus) {
        String addr = null, zip = null;
        if (loc != null) {
            try {
                if (Geocoder.isPresent()) {
                    List<Address> list = new Geocoder(ctx, Locale.KOREA)
                            .getFromLocation(loc.getLatitude(), loc.getLongitude(), 1);
                    if (list != null && !list.isEmpty()) {
                        addr = list.get(0).getAddressLine(0);
                        zip = list.get(0).getPostalCode();
                    }
                }
            } catch (Exception ignored) {
            }
        }
        return format(ctx, loc, addr, zip, withStatus);
    }

    /*
     * 짧은 위치 메시지 (문자용: 주소 + 지도 링크 1개)
     * 📍 할머니 위치 11-21 14:30
     * 서울특별시 중구 세종대로 110
     * https://maps.google.com/?q=37.566500,126.978000
     */
    static String shortMessage(Context ctx, Location loc) {
        return shortMessage(ctx, loc, true);
    }

    static String shortMessage(Context ctx, Location loc, boolean withStatus) {
        String who = senderName(ctx);
        StringBuilder sb = new StringBuilder(withStatus ? statusLine(ctx) + "\n" : "");
        sb.append("📍 ");
        sb.append(who.isEmpty() ? "" : who + " ").append("위치 ")
                .append(new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date()));
        if (loc == null) {
            return sb.append("\n위치를 확인할 수 없습니다.\n").append(wifiShortLine(ctx)).toString();
        }
        try {
            if (Geocoder.isPresent()) {
                List<Address> list = new Geocoder(ctx, Locale.KOREA)
                        .getFromLocation(loc.getLatitude(), loc.getLongitude(), 1);
                if (list != null && !list.isEmpty()) {
                    // "대한민국 " 은 빼서 짧게
                    sb.append('\n').append(list.get(0).getAddressLine(0).replaceFirst("^대한민국\\s*", ""));
                }
            }
        } catch (Exception ignored) {
        }
        sb.append('\n').append(wifiShortLine(ctx)); // 공유기 한 줄 (이름·MAC·별명·등록)
        sb.append("\nhttps://maps.google.com/?q=")
                .append(String.format(Locale.US, "%.6f,%.6f", loc.getLatitude(), loc.getLongitude()));
        return sb.toString();
    }

    // 보내는 사람 이름 (예: 할머니). 받는 사람이 누구의 위치인지 알 수 있게 메시지 제목에 넣음
    static final String PREF_APP = "app";

    static String senderName(Context ctx) {
        return ctx.getSharedPreferences(PREF_APP, Context.MODE_PRIVATE).getString("sender", "").trim();
    }

    static void setSenderName(Context ctx, String name) {
        ctx.getSharedPreferences(PREF_APP, Context.MODE_PRIVATE).edit().putString("sender", name.trim()).apply();
    }

    /*
     * 위치 메시지 (가독성: 항목마다 '■ ', 항목 사이 빈 줄)
     *
     * 📍 현재 위치 (11-21 14:30:05)
     *
     * ■ 주소: 서울특별시 중구 …
     *
     * ■ 우편번호: 04524
     * ...
     */
    static String format(Context ctx, Location loc, String addr, String zip) {
        return format(ctx, loc, addr, zip, true);
    }

    static String format(Context ctx, Location loc, String addr, String zip, boolean withStatus) {
        StringBuilder sb = new StringBuilder(withStatus ? statusLine(ctx) + "\n\n" : "");
        String who = senderName(ctx);
        sb.append("📍 ").append(who.isEmpty() ? "" : who + "의 ").append("현재 위치 (")
                .append(new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date())).append(")");
        if (loc == null) {
            sb.append("\n\n■ 위치를 확인할 수 없습니다.\n   (위치 서비스가 켜져 있는지 확인하세요)");
            sb.append("\n\n").append(wifiDetails(ctx)); // 위치를 못 잡아도 공유기 정보는 단서가 됨
            return sb.toString();
        }
        if (addr != null && !addr.isEmpty()) {
            sb.append("\n\n■ 주소: ").append(addr);
        }
        if (zip != null && !zip.isEmpty()) {
            sb.append("\n\n■ 우편번호: ").append(zip);
        }
        sb.append("\n\n■ 위도, 경도: ")
                .append(String.format(Locale.US, "%.6f, %.6f", loc.getLatitude(), loc.getLongitude()))
                .append("\n   (정확도 ").append(Math.round(loc.getAccuracy())).append(" m)");
        sb.append("\n\n■ 측정 시각: ")
                .append(new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date(loc.getTime())));
        sb.append("\n\n").append(mapLinks(loc.getLatitude(), loc.getLongitude()));
        // 모든 위치 메시지 끝: 공유기 식별 정보 (등록 안 된 공유기도 자세히, 별명 줄은 항상)
        sb.append("\n\n").append(wifiDetails(ctx));
        return sb.toString();
    }

    // 지도 링크 3개 (구글 / 카카오 / 네이버). 누르면 각 지도 앱(없으면 웹)에서 열림
    static String mapLinks(double lat, double lng) {
        return "■ 지도에서 보기\n"
                + "   구글: " + googleUrl(lat, lng) + "\n\n"
                + "   카카오: " + kakaoUrl(lat, lng) + "\n\n"
                + "   네이버: " + naverUrl(lat, lng);
    }

    private static String f6(double value) {
        return String.format(Locale.US, "%.6f", value);
    }

    private static String hereName() {
        try {
            return URLEncoder.encode("현재 위치", "UTF-8").replace("+", "%20");
        } catch (Exception e) {
            return "here";
        }
    }

    static String googleUrl(double lat, double lng) {
        return "https://maps.google.com/?q=" + f6(lat) + "," + f6(lng);
    }

    static String kakaoUrl(double lat, double lng) {
        return "https://map.kakao.com/link/map/" + hereName() + "," + f6(lat) + "," + f6(lng);
    }

    // 네이버: 예전 "p?c=경도,위도" 형식은 지도 중심만 옮기고 표시(핀)가 없었음
    //  -> lng/lat/title 형식 (네이버 담당자 포럼 안내): 그 자리에 "현재 위치" 핀 표시
    static String naverUrl(double lat, double lng) {
        return "https://map.naver.com/?lng=" + f6(lng) + "&lat=" + f6(lat) + "&title=" + hereName();
    }

    // 받는 사람(쉼표 구분) 모두에게 전송. 성공하면 null, 실패하면 오류 내용
    static String send(String token, String chats, String text) {
        StringBuilder err = new StringBuilder();
        for (String chat : chats.split("[,\\s]+")) {
            if (chat.isEmpty()) {
                continue;
            }
            String sendErr = sendOne(token, chat, text);
            if (sendErr != null) {
                if (err.length() > 0) {
                    err.append(" / ");
                }
                err.append(chat).append(": ").append(sendErr);
            }
        }
        return err.length() == 0 ? null : err.toString();
    }

    private static String sendOne(String token, String chat, String text) {
        HttpURLConnection con = null;
        try {
            URL url = new URL("https://api.telegram.org/bot" + token + "/sendMessage");
            con = (HttpURLConnection) url.openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            con.setRequestMethod("POST");
            con.setDoOutput(true);
            con.setRequestProperty("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
            String body = "chat_id=" + URLEncoder.encode(chat, "UTF-8")
                    + "&text=" + URLEncoder.encode(text, "UTF-8");
            try (OutputStream os = con.getOutputStream()) {
                os.write(body.getBytes(StandardCharsets.UTF_8));
            }
            int code = con.getResponseCode();
            if (code == 200) {
                return null;
            }
            String res = read(con.getErrorStream());
            int start = res.indexOf("\"description\":\"");
            if (start >= 0) {
                int end = res.indexOf('"', start + 15);
                return end > 0 ? res.substring(start + 15, end) : res;
            }
            return "HTTP " + code;
        } catch (Exception e) {
            return "연결 실패 (인터넷 확인)";
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
    }

    /**
     * 봇에게 최근 말을 건 사람/단톡방 목록 (getUpdates).
     * offset 을 넘기지 않으므로 다른 프로그램(PC 등)이 받을 메시지를 지우지 않는다.
     * 결과: {채팅ID, 표시 이름} 목록. 실패하면 err[0] 에 오류 내용.
     */
    static List<String[]> findChats(String token, String[] err) {
        List<String[]> list = new java.util.ArrayList<>();
        HttpURLConnection con = null;
        try {
            con = (HttpURLConnection) new URL("https://api.telegram.org/bot" + token + "/getUpdates").openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            int code = con.getResponseCode();
            String res = read(code == 200 ? con.getInputStream() : con.getErrorStream());
            org.json.JSONObject root = new org.json.JSONObject(res);
            if (!root.optBoolean("ok")) {
                err[0] = root.optString("description", "HTTP " + code);
                return list;
            }
            java.util.Set<String> seen = new java.util.HashSet<>();
            org.json.JSONArray arr = root.getJSONArray("result");
            for (int i = arr.length() - 1; i >= 0; i--) { // 최근 것부터
                org.json.JSONObject update = arr.getJSONObject(i);
                java.util.Iterator<String> keys = update.keys();
                while (keys.hasNext()) {
                    org.json.JSONObject chatObj = update.optJSONObject(keys.next());
                    if (chatObj == null) {
                        continue;
                    }
                    org.json.JSONObject chat = chatObj.optJSONObject("chat");
                    if (chat == null && chatObj.optJSONObject("message") != null) {
                        chat = chatObj.optJSONObject("message").optJSONObject("chat"); // 버튼 응답 등
                    }
                    if (chat == null || !chat.has("id")) {
                        continue;
                    }
                    String id = String.valueOf(chat.getLong("id"));
                    if (!seen.add(id)) {
                        continue;
                    }
                    list.add(new String[]{id, chatLabel(chat)});
                }
            }
        } catch (org.json.JSONException e) {
            err[0] = "응답을 해석할 수 없습니다 (봇 토큰 확인)";
        } catch (Exception e) {
            err[0] = "연결 실패 (인터넷 확인)";
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
        return list;
    }

    // 봇 아이디 (getMe 의 username, 예: garpsu_location_bot). 실패하면 null, err[0] 에 오류 내용
    static String botUsername(String token, String[] err) {
        HttpURLConnection con = null;
        try {
            con = (HttpURLConnection) new URL("https://api.telegram.org/bot" + token + "/getMe").openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            int code = con.getResponseCode();
            org.json.JSONObject root = new org.json.JSONObject(
                    read(code == 200 ? con.getInputStream() : con.getErrorStream()));
            if (!root.optBoolean("ok")) {
                err[0] = root.optString("description", "HTTP " + code);
                return null;
            }
            String username = root.getJSONObject("result").optString("username");
            if (username.isEmpty()) {
                err[0] = "봇 아이디를 알 수 없습니다";
                return null;
            }
            return username;
        } catch (org.json.JSONException e) {
            err[0] = "응답을 해석할 수 없습니다 (봇 토큰 확인)";
        } catch (Exception e) {
            err[0] = "연결 실패 (인터넷 확인)";
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
        return null;
    }

    // 봇에게 온 최근 메시지(getUpdates) 원본. offset 을 넘기지 않아 메시지를 지우지 않음. 실패하면 null
    static org.json.JSONArray getUpdates(String token, String[] err) {
        HttpURLConnection con = null;
        try {
            // allowed_updates 는 봇 설정을 바꿔 버리므로 넘기지 않음
            con = (HttpURLConnection) new URL("https://api.telegram.org/bot" + token
                    + "/getUpdates").openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout(15000);
            int code = con.getResponseCode();
            org.json.JSONObject root = new org.json.JSONObject(
                    read(code == 200 ? con.getInputStream() : con.getErrorStream()));
            if (!root.optBoolean("ok")) {
                err[0] = root.optString("description", "HTTP " + code);
                return null;
            }
            return root.getJSONArray("result");
        } catch (org.json.JSONException e) {
            err[0] = "응답을 해석할 수 없습니다 (봇 토큰 확인)";
        } catch (Exception e) {
            err[0] = "연결 실패 (인터넷 확인)";
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
        return null;
    }

    /**
     * 새 메시지 기다리기 (롱 폴링): 새 메시지가 오거나 timeoutSec 초가 지나면 돌아옴.
     * offset 이전 메시지는 '처리함'으로 표시되어 다시 오지 않음. 실패하면 null
     */
    static org.json.JSONArray poll(String token, long offset, int timeoutSec, String[] err) {
        HttpURLConnection con = null;
        try {
            con = (HttpURLConnection) new URL("https://api.telegram.org/bot" + token
                    + "/getUpdates?timeout=" + timeoutSec + (offset > 0 ? "&offset=" + offset : "")).openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout((timeoutSec + 15) * 1000);
            int code = con.getResponseCode();
            org.json.JSONObject root = new org.json.JSONObject(
                    read(code == 200 ? con.getInputStream() : con.getErrorStream()));
            if (!root.optBoolean("ok")) {
                err[0] = root.optString("description", "HTTP " + code);
                return null;
            }
            return root.getJSONArray("result");
        } catch (Exception e) {
            err[0] = "연결 실패";
            return null;
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
    }

    // 원격 위치 요청 명령인지: 위치 / 어디 / 어디야 / /where / location ... (봇 이름 붙은 /where@봇 도 허용)
    static boolean isLocationCommand(String text) {
        return text != null && text.trim().matches(
                "(?i)^/?(위치|어디|어디야|어디예요|어디에요|where|location|loc)(@\\w+)?[?？!.~\\s]*$");
    }

    // 복사한 글(BotFather 메시지 전체여도 됨)에서 봇 토큰만 찾기. 없으면 null
    static String extractToken(CharSequence txt) {
        if (txt == null) {
            return null;
        }
        java.util.regex.Matcher matcher = java.util.regex.Pattern
                .compile("\\d{6,}:[A-Za-z0-9_-]{30,}").matcher(txt);
        return matcher.find() ? matcher.group() : null;
    }

    // 받는 사람 이름 저장/조회 (채팅 ID -> "홍길동 · 개인")
    static void saveName(Context ctx, String id, String label) {
        android.content.SharedPreferences sp = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE);
        java.util.Set<String> set = new java.util.HashSet<>(sp.getStringSet("names", new java.util.HashSet<String>()));
        java.util.Iterator<String> it = set.iterator();
        while (it.hasNext()) {
            if (it.next().startsWith(id + "|")) {
                it.remove();
            }
        }
        set.add(id + "|" + label);
        sp.edit().putStringSet("names", set).apply();
    }

    // 이름을 알면 이름, 모르면 "ID 123..."
    static String nameOrId(Context ctx, String id) {
        String name = nameOf(ctx, id);
        return name == null ? "ID " + id : name;
    }

    static String nameOf(Context ctx, String id) {
        for (String entry : ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .getStringSet("names", new java.util.HashSet<String>())) {
            if (entry.startsWith(id + "|")) {
                return entry.substring(id.length() + 1);
            }
        }
        return null;
    }

    // 예: "홍길동 (@hong) · 개인", "우리가족 · 단톡방"
    static String chatLabel(org.json.JSONObject chat) {
        String type = chat.optString("type");
        String name;
        if ("private".equals(type)) {
            name = (chat.optString("first_name") + " " + chat.optString("last_name")).trim();
        } else {
            name = chat.optString("title");
        }
        if (name.isEmpty()) {
            name = "(이름 없음)";
        }
        String user = chat.optString("username");
        if (!user.isEmpty()) {
            name += " (@" + user + ")";
        }
        String kind = "private".equals(type) ? "개인" : "channel".equals(type) ? "채널" : "단톡방";
        return name + " · " + kind;
    }

    private static String read(InputStream in) {
        if (in == null) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line);
            }
        } catch (Exception ignored) {
        }
        return sb.toString();
    }
}
