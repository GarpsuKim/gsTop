package com.garpsu.wifiscan;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/**
 * 일반 문자(자동): 솔라피(SOLAPI) 문자 API 로 폰에서 직접 발송. (네트워크 사용 -> 작업 스레드에서 호출)
 * - 인증: Authorization: HMAC-SHA256 apiKey=…, date=…, salt=…, signature=HMAC_SHA256(date+salt, apiSecret)
 * - 90바이트가 넘는 긴 내용은 솔라피가 자동으로 장문(LMS)으로 보냄 (요금 다름)
 * - 발신번호는 솔라피에 미리 등록(발신번호 사전등록)한 번호여야 함
 */
final class SmsSender {

    private static final String URL_SEND = "https://api.solapi.com/messages/v4/send-many/detail";

    private SmsSender() {
    }

    private static String digits(String s) {
        return s == null ? "" : s.replaceAll("[^0-9]", "");
    }

    // 받는 번호 여러 개(쉼표 구분)에게 같은 내용 발송. 성공하면 null, 실패하면 오류 내용
    static String send(String apiKey, String apiSecret, String from, String toCsv, String text) {
        apiKey = apiKey == null ? "" : apiKey.trim();
        apiSecret = apiSecret == null ? "" : apiSecret.trim();
        String sender = digits(from);
        if (apiKey.isEmpty() || apiSecret.isEmpty() || sender.isEmpty()) {
            return "솔라피 API 키·시크릿·발신번호를 확인하세요";
        }
        HttpURLConnection con = null;
        try {
            JSONArray msgs = new JSONArray();
            for (String t : toCsv.split("[,;]+")) {
                String to = digits(t);
                if (to.length() >= 8) {
                    msgs.put(new JSONObject().put("to", to).put("from", sender).put("text", text));
                }
            }
            if (msgs.length() == 0) {
                return "받는 번호를 확인하세요";
            }
            byte[] body = new JSONObject().put("messages", msgs).toString().getBytes(StandardCharsets.UTF_8);

            con = (HttpURLConnection) new URL(URL_SEND).openConnection();
            con.setConnectTimeout(15000);
            con.setReadTimeout(20000);
            con.setRequestMethod("POST");
            con.setDoOutput(true);
            con.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            con.setRequestProperty("Authorization", authHeader(apiKey, apiSecret));
            try (OutputStream os = con.getOutputStream()) {
                os.write(body);
            }
            int code = con.getResponseCode();
            String res = read(code >= 200 && code < 300 ? con.getInputStream() : con.getErrorStream());
            JSONObject r = res.isEmpty() ? new JSONObject() : new JSONObject(res);
            if (code < 200 || code >= 300) {
                return r.optString("errorMessage", r.optString("errorCode", "HTTP " + code));
            }
            // 일부 번호만 실패한 경우
            JSONArray failed = r.optJSONArray("failedMessageList");
            if (failed != null && failed.length() > 0) {
                JSONObject f = failed.optJSONObject(0);
                return failed.length() + "건 실패"
                        + (f == null ? "" : ": " + f.optString("statusMessage", f.optString("statusCode", "")));
            }
            return null;
        } catch (Exception e) {
            return "연결 실패 (인터넷 확인)";
        } finally {
            if (con != null) {
                con.disconnect();
            }
        }
    }

    // 솔라피 인증 헤더 (API 시크릿은 서명에만 쓰고 전송하지 않음)
    private static String authHeader(String apiKey, String apiSecret) throws Exception {
        SimpleDateFormat iso = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US);
        iso.setTimeZone(TimeZone.getTimeZone("UTC"));
        String date = iso.format(new Date());
        byte[] rnd = new byte[16];
        new SecureRandom().nextBytes(rnd);
        String salt = hex(rnd);
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(apiSecret.getBytes(StandardCharsets.UTF_8), "HmacSHA256"));
        String sig = hex(mac.doFinal((date + salt).getBytes(StandardCharsets.UTF_8)));
        return "HMAC-SHA256 apiKey=" + apiKey + ", date=" + date + ", salt=" + salt + ", signature=" + sig;
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) {
            sb.append(String.format(Locale.US, "%02x", x));
        }
        return sb.toString();
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
