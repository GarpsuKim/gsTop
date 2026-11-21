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

    // 위치 -> 메시지 (주소 조회 포함)
    static String message(Context c, Location l) {
        String addr = null, zip = null;
        if (l != null) {
            try {
                if (Geocoder.isPresent()) {
                    List<Address> list = new Geocoder(c, Locale.KOREA)
                            .getFromLocation(l.getLatitude(), l.getLongitude(), 1);
                    if (list != null && !list.isEmpty()) {
                        addr = list.get(0).getAddressLine(0);
                        zip = list.get(0).getPostalCode();
                    }
                }
            } catch (Exception ignored) {
            }
        }
        return format(c, l, addr, zip);
    }

    /*
     * 짧은 위치 메시지 (문자용: 주소 + 지도 링크 1개)
     * 📍 할머니 위치 11-21 14:30
     * 서울특별시 중구 세종대로 110
     * https://maps.google.com/?q=37.566500,126.978000
     */
    static String shortMessage(Context c, Location l) {
        String who = senderName(c);
        StringBuilder sb = new StringBuilder("📍 ");
        sb.append(who.isEmpty() ? "" : who + " ").append("위치 ")
                .append(new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date()));
        if (l == null) {
            return sb.append("\n위치를 확인할 수 없습니다.").toString();
        }
        try {
            if (Geocoder.isPresent()) {
                List<Address> list = new Geocoder(c, Locale.KOREA)
                        .getFromLocation(l.getLatitude(), l.getLongitude(), 1);
                if (list != null && !list.isEmpty()) {
                    // "대한민국 " 은 빼서 짧게
                    sb.append('\n').append(list.get(0).getAddressLine(0).replaceFirst("^대한민국\\s*", ""));
                }
            }
        } catch (Exception ignored) {
        }
        sb.append("\nhttps://maps.google.com/?q=")
                .append(String.format(Locale.US, "%.6f,%.6f", l.getLatitude(), l.getLongitude()));
        return sb.toString();
    }

    // 보내는 사람 이름 (예: 할머니). 받는 사람이 누구의 위치인지 알 수 있게 메시지 제목에 넣음
    static final String PREF_APP = "app";

    static String senderName(Context c) {
        return c.getSharedPreferences(PREF_APP, Context.MODE_PRIVATE).getString("sender", "").trim();
    }

    static void setSenderName(Context c, String name) {
        c.getSharedPreferences(PREF_APP, Context.MODE_PRIVATE).edit().putString("sender", name.trim()).apply();
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
    static String format(Context c, Location l, String addr, String zip) {
        StringBuilder sb = new StringBuilder();
        String who = senderName(c);
        sb.append("📍 ").append(who.isEmpty() ? "" : who + "의 ").append("현재 위치 (")
                .append(new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date())).append(")");
        if (l == null) {
            sb.append("\n\n■ 위치를 확인할 수 없습니다.\n   (위치 서비스가 켜져 있는지 확인하세요)");
            return sb.toString();
        }
        if (addr != null && !addr.isEmpty()) {
            sb.append("\n\n■ 주소: ").append(addr);
        }
        if (zip != null && !zip.isEmpty()) {
            sb.append("\n\n■ 우편번호: ").append(zip);
        }
        sb.append("\n\n■ 위도, 경도: ")
                .append(String.format(Locale.US, "%.6f, %.6f", l.getLatitude(), l.getLongitude()))
                .append("\n   (정확도 ").append(Math.round(l.getAccuracy())).append(" m)");
        sb.append("\n\n■ 측정 시각: ")
                .append(new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date(l.getTime())));
        sb.append("\n\n").append(mapLinks(l.getLatitude(), l.getLongitude()));
        return sb.toString();
    }

    // 지도 링크 3개 (구글 / 카카오 / 네이버). 누르면 각 지도 앱(없으면 웹)에서 열림
    static String mapLinks(double lat, double lng) {
        String la = String.format(Locale.US, "%.6f", lat), lo = String.format(Locale.US, "%.6f", lng);
        String name;
        try {
            name = URLEncoder.encode("현재 위치", "UTF-8").replace("+", "%20");
        } catch (Exception e) {
            name = "here";
        }
        return "■ 지도에서 보기\n"
                + "   구글: https://maps.google.com/?q=" + la + "," + lo + "\n\n"
                + "   카카오: https://map.kakao.com/link/map/" + name + "," + la + "," + lo + "\n\n"
                + "   네이버: https://map.naver.com/p?c=" + lo + "," + la + ",17,0,0,0,dh";
    }

    // 받는 사람(쉼표 구분) 모두에게 전송. 성공하면 null, 실패하면 오류 내용
    static String send(String token, String chats, String text) {
        StringBuilder err = new StringBuilder();
        for (String chat : chats.split("[,\\s]+")) {
            if (chat.isEmpty()) {
                continue;
            }
            String e = sendOne(token, chat, text);
            if (e != null) {
                if (err.length() > 0) {
                    err.append(" / ");
                }
                err.append(chat).append(": ").append(e);
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
            int i = res.indexOf("\"description\":\"");
            if (i >= 0) {
                int j = res.indexOf('"', i + 15);
                return j > 0 ? res.substring(i + 15, j) : res;
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
                org.json.JSONObject u = arr.getJSONObject(i);
                java.util.Iterator<String> keys = u.keys();
                while (keys.hasNext()) {
                    org.json.JSONObject o = u.optJSONObject(keys.next());
                    if (o == null) {
                        continue;
                    }
                    org.json.JSONObject chat = o.optJSONObject("chat");
                    if (chat == null && o.optJSONObject("message") != null) {
                        chat = o.optJSONObject("message").optJSONObject("chat"); // 버튼 응답 등
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
            String u = root.getJSONObject("result").optString("username");
            if (u.isEmpty()) {
                err[0] = "봇 아이디를 알 수 없습니다";
                return null;
            }
            return u;
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
        java.util.regex.Matcher m = java.util.regex.Pattern
                .compile("\\d{6,}:[A-Za-z0-9_-]{30,}").matcher(txt);
        return m.find() ? m.group() : null;
    }

    // 받는 사람 이름 저장/조회 (채팅 ID -> "홍길동 · 개인")
    static void saveName(Context c, String id, String label) {
        android.content.SharedPreferences sp = c.getSharedPreferences(PREF, Context.MODE_PRIVATE);
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
    static String nameOrId(Context c, String id) {
        String n = nameOf(c, id);
        return n == null ? "ID " + id : n;
    }

    static String nameOf(Context c, String id) {
        for (String s : c.getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .getStringSet("names", new java.util.HashSet<String>())) {
            if (s.startsWith(id + "|")) {
                return s.substring(id.length() + 1);
            }
        }
        return null;
    }

    // 예: "홍길동 (@hong) · 개인", "우리가족 · 단톡방"
    static String chatLabel(org.json.JSONObject c) {
        String type = c.optString("type");
        String name;
        if ("private".equals(type)) {
            name = (c.optString("first_name") + " " + c.optString("last_name")).trim();
        } else {
            name = c.optString("title");
        }
        if (name.isEmpty()) {
            name = "(이름 없음)";
        }
        String user = c.optString("username");
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
