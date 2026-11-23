package com.garpsu.wifiscan;

import android.util.Base64;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * 앱 비밀번호로 메일 서버(SMTP, SSL 465)에 직접 접속해 메일을 보낸다. (네트워크 사용 -> 작업 스레드에서 호출)
 * 보내는 계정은 구글(Gmail)만 지원 (2단계 인증 + 앱 비밀번호 필요).
 */
final class EmailSender {

    static final String SUPPORTED = "구글(@gmail.com)";

    // 자세한 안내문 (GitHub 의 GuideEmail.md)
    static final String GUIDE_URL = "https://github.com/GarpsuKim/gsTop/blob/main/WifiScanAndroid/GuideEmail.md";

    // 앱 비밀번호 안내 (1150·1160 화면 공용). 링크는 눌러서 바로 열림 (화면에서 Linkify)
    static final String HELP = "보내는 계정: " + SUPPORTED + " 만 가능\n"
            + "이메일 등록을 위한 자세한 설명 : " + GUIDE_URL + "\n"
            + "앱 비밀번호 (2단계 인증을 먼저 켜야 함): https://myaccount.google.com/apppasswords";

    private EmailSender() {
    }

    // 보내는 주소의 도메인으로 SMTP 서버 결정. 구글(Gmail)이 아니면 null
    static String smtpHost(String from) {
        String domain = from.substring(from.indexOf('@') + 1).trim().toLowerCase(Locale.US);
        return domain.equals("gmail.com") || domain.equals("googlemail.com") ? "smtp.gmail.com" : null;
    }

    // 성공하면 null, 실패하면 오류 내용
    static String send(String from, String appPassword, String toCsv, String subject, String body) {
        return send(from, appPassword, toCsv, subject, body, null, null);
    }

    // 첨부 파일 1개 포함 (attachName·attachData 가 null 이면 첨부 없음). 성공하면 null, 실패하면 오류 내용
    static String send(String from, String appPassword, String toCsv, String subject, String body,
                       String attachName, byte[] attachData) {
        from = from.trim();
        String pass = appPassword.replace(" ", ""); // 구글은 4자리씩 띄어 보여 줌
        List<String> to = new ArrayList<>();
        for (String addr : toCsv.split("[,;\\s]+")) {
            if (!addr.isEmpty()) {
                to.add(addr);
            }
        }
        if (from.indexOf('@') < 1 || pass.isEmpty() || to.isEmpty()) {
            return "보내는 주소, 앱 비밀번호, 받는 사람을 확인하세요";
        }

        String host = smtpHost(from);
        if (host == null) {
            return "보내는 계정은 " + SUPPORTED + " 만 사용할 수 있습니다";
        }
        SSLSocket sock = null;
        try {
            // 일반 소켓으로 접속(시간 제한) 후 SSL 로 감싸기 (서버 이름을 넘겨 SNI 포함)
            Socket raw = new Socket();
            raw.connect(new InetSocketAddress(host, 465), 15000);
            sock = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket(raw, host, 465, true);
            sock.setSoTimeout(20000);
            sock.startHandshake();
            // 인증서가 실제 그 서버의 것인지 확인 (가짜 서버에 앱 비밀번호가 넘어가지 않게)
            if (!HttpsURLConnection.getDefaultHostnameVerifier().verify(host, sock.getSession())) {
                return "서버 인증서 확인 실패 (" + host + ") - 공용 Wi-Fi 등 네트워크를 확인하세요";
            }
            BufferedReader in = new BufferedReader(new InputStreamReader(sock.getInputStream(), StandardCharsets.UTF_8));
            OutputStream out = sock.getOutputStream();

            expect(in, 220, "서버 연결");
            cmd(out, "EHLO wifiscan");
            expect(in, 250, "EHLO");
            cmd(out, "AUTH LOGIN");
            expect(in, 334, "로그인");
            cmd(out, b64(from));
            expect(in, 334, "로그인");
            cmd(out, b64(pass));
            String resp = reply(in);
            if (!resp.startsWith("235")) {
                return "로그인 실패 - 이메일 주소/앱 비밀번호를 확인하세요 (" + firstLine(resp) + ")";
            }
            cmd(out, "MAIL FROM:<" + from + ">");
            expect(in, 250, "보내는 주소");
            for (String rcpt : to) {
                cmd(out, "RCPT TO:<" + rcpt + ">");
                resp = reply(in);
                if (!resp.startsWith("250") && !resp.startsWith("251")) {
                    return "받는 주소 거부: " + rcpt + " (" + firstLine(resp) + ")";
                }
            }
            cmd(out, "DATA");
            expect(in, 354, "DATA");

            StringBuilder msg = new StringBuilder();
            msg.append("From: ").append(from).append("\r\n");
            msg.append("To: ").append(android.text.TextUtils.join(", ", to)).append("\r\n");
            msg.append("Subject: =?UTF-8?B?").append(b64(subject)).append("?=\r\n");
            msg.append("Date: ").append(new SimpleDateFormat("EEE, dd MMM yyyy HH:mm:ss Z", Locale.US)
                    .format(new Date())).append("\r\n");
            msg.append("MIME-Version: 1.0\r\n");
            // 본문·첨부 모두 base64 76자 줄바꿈 (줄이 '.' 으로 시작할 일이 없음)
            String enc = Base64.encodeToString(body.getBytes(StandardCharsets.UTF_8), Base64.DEFAULT);
            if (attachData == null) {
                msg.append("Content-Type: text/plain; charset=UTF-8\r\n");
                msg.append("Content-Transfer-Encoding: base64\r\n\r\n");
                msg.append(enc.replace("\r\n", "\n").replace("\n", "\r\n"));
            } else {
                String boundary = "----wifiscan" + System.currentTimeMillis();
                msg.append("Content-Type: multipart/mixed; boundary=\"").append(boundary).append("\"\r\n\r\n");
                msg.append("--").append(boundary).append("\r\n");
                msg.append("Content-Type: text/plain; charset=UTF-8\r\n");
                msg.append("Content-Transfer-Encoding: base64\r\n\r\n");
                msg.append(enc.replace("\r\n", "\n").replace("\n", "\r\n"));
                msg.append("\r\n--").append(boundary).append("\r\n");
                String fileName = "=?UTF-8?B?" + b64(attachName) + "?=";
                msg.append("Content-Type: text/plain; charset=UTF-8; name=\"").append(fileName).append("\"\r\n");
                msg.append("Content-Disposition: attachment; filename=\"").append(fileName).append("\"\r\n");
                msg.append("Content-Transfer-Encoding: base64\r\n\r\n");
                String att = Base64.encodeToString(attachData, Base64.DEFAULT);
                msg.append(att.replace("\r\n", "\n").replace("\n", "\r\n"));
                msg.append("\r\n--").append(boundary).append("--\r\n");
            }
            msg.append("\r\n.\r\n");
            out.write(msg.toString().getBytes(StandardCharsets.UTF_8));
            out.flush();
            expect(in, 250, "전송");
            cmd(out, "QUIT");
            return null;
        } catch (SmtpError e) {
            return e.getMessage();
        } catch (SocketTimeoutException e) {
            return "응답 시간 초과 (" + host + ")";
        } catch (Exception e) {
            return "연결 실패 (인터넷 확인) - " + host;
        } finally {
            if (sock != null) {
                try {
                    sock.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private static class SmtpError extends Exception {
        SmtpError(String msg) {
            super(msg);
        }
    }

    private static String b64(String text) {
        return Base64.encodeToString(text.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
    }

    private static void cmd(OutputStream out, String line) throws java.io.IOException {
        out.write((line + "\r\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }

    // 여러 줄 응답("250-..." ... "250 ...")을 끝까지 읽음
    private static String reply(BufferedReader in) throws java.io.IOException {
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = in.readLine()) != null) {
            sb.append(line).append('\n');
            if (line.length() < 4 || line.charAt(3) != '-') {
                break;
            }
        }
        return sb.toString();
    }

    private static void expect(BufferedReader in, int code, String step) throws Exception {
        String resp = reply(in);
        if (!resp.startsWith(String.valueOf(code))) {
            throw new SmtpError(step + " 실패 (" + firstLine(resp) + ")");
        }
    }

    private static String firstLine(String resp) {
        int nl = resp.indexOf('\n');
        String first = (nl >= 0 ? resp.substring(0, nl) : resp).trim();
        return first.isEmpty() ? "응답 없음" : first;
    }
}
