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
 * 보내는 계정은 네이버·카카오·구글(Gmail) 3가지.
 * (구글은 2단계 인증 + 앱 비밀번호, 네이버·카카오는 메일 환경설정에서 SMTP/IMAP 사용 켜기 + 앱 비밀번호)
 */
final class EmailSender {

    // 고를 수 있는 메일: {도메인, 이름, SMTP 서버, IMAP 서버} (1075 화면 목록 순서)
    static final String[][] PROVIDERS = {
            {"naver.com", "네이버", "smtp.naver.com", "imap.naver.com"},
            {"kakao.com", "카카오", "smtp.kakao.com", "imap.kakao.com"},
            {"gmail.com", "구글(Gmail)", "smtp.gmail.com", "imap.gmail.com"},
    };

    static final String SUPPORTED = "네이버(@naver.com), 카카오(@kakao.com), 구글(@gmail.com)";

    // 자세한 안내문 (GitHub 의 GuideEmail.md)
    static final String GUIDE_URL = "https://github.com/GarpsuKim/gsTop/blob/main/WifiScanAndroid/GuideEmail.md";

    // 앱 비밀번호 안내 (1150·1160 화면 공용). 링크는 눌러서 바로 열림 (화면에서 Linkify)
    static final String HELP = "보내는 계정: " + SUPPORTED + " 중 하나 (사용자 등록 1075 에서 등록)\n"
            + "이메일 등록을 위한 자세한 설명 : " + GUIDE_URL;

    private EmailSender() {
    }

    // 주소의 메일 종류 {도메인, 이름, SMTP 서버, IMAP 서버}. 지원하지 않는 메일이면 null
    static String[] provider(String addr) {
        if (addr == null || addr.indexOf('@') < 0) {
            return null;
        }
        String domain = addr.substring(addr.indexOf('@') + 1).trim().toLowerCase(Locale.US);
        if (domain.equals("googlemail.com")) {
            domain = "gmail.com";
        }
        for (String[] prov : PROVIDERS) {
            if (prov[0].equals(domain)) {
                return prov;
            }
        }
        return null;
    }

    // 보내는 주소의 도메인으로 SMTP 서버 결정. 지원하지 않는 메일이면 null
    static String smtpHost(String from) {
        String[] prov = provider(from);
        return prov == null ? null : prov[2];
    }

    // 받는(IMAP) 서버: 이메일 위치 요청 감시용. 지원하지 않는 메일이면 null
    static String imapHost(String addr) {
        String[] prov = provider(addr);
        return prov == null ? null : prov[3];
    }

    // 화면 표시용 메일 이름 ("네이버" 등). 모르면 "이메일"
    static String providerName(String addr) {
        String[] prov = provider(addr);
        return prov == null ? "이메일" : prov[1];
    }

    // 1075 화면: 고른 메일(도메인)에 맞는 준비 안내 (링크는 눌러서 바로 열림)
    static String setupTip(String domain) {
        switch (domain) {
            case "naver.com":
                return "네이버 준비: ① 네이버 메일 → 환경설정 → POP3/IMAP 설정 → IMAP/SMTP 사용 '사용함' → 저장\n"
                        + "② 2단계 인증을 쓰면 '애플리케이션 비밀번호'를 만들어 위 칸에 입력"
                        + " (2단계 인증을 안 쓰면 네이버 로그인 비밀번호)\n"
                        + "   https://nid.naver.com → 보안설정 → 2단계 인증 → 애플리케이션 비밀번호 관리";
            case "kakao.com":
                return "카카오 준비: ① 카카오메일 → 환경설정 → IMAP/SMTP → 사용함 → 저장\n"
                        + "② 2단계 인증을 쓰면 '앱 비밀번호'를 만들어 위 칸에 입력"
                        + " (2단계 인증을 안 쓰면 카카오계정 비밀번호)\n"
                        + "   https://accounts.kakao.com → 계정 보안 → 2단계 인증 / 앱 비밀번호";
            default:
                return "구글 준비: 2단계 인증을 켠 뒤 '앱 비밀번호'(16자리)를 만들어 위 칸에 입력\n"
                        + "   https://myaccount.google.com/apppasswords";
        }
    }

    // 로그인 실패 때 메일별 확인할 점
    static String loginHint(String addr) {
        String[] prov = provider(addr);
        if (prov == null || prov[0].equals("gmail.com")) {
            return "2단계 인증을 켜고 만든 앱 비밀번호(16자리)인지 확인하세요";
        }
        return prov[1] + " 메일 환경설정에서 IMAP/SMTP 사용을 켰는지, 앱 비밀번호(2단계 인증을 켠 경우)인지 확인하세요";
    }

    // 성공하면 null, 실패하면 오류 내용
    static String send(String from, String appPassword, String toCsv, String subject, String body) {
        return send(from, appPassword, toCsv, subject, body, null, null);
    }

    // 첨부 파일 1개 포함 (attachName·attachData 가 null 이면 첨부 없음). 성공하면 null, 실패하면 오류 내용
    static String send(String from, String appPassword, String toCsv, String subject, String body,
                       String attachName, byte[] attachData) {
        from = from.trim();
        String pass = appPassword.replace(" ", ""); // 구글은 4자리씩 띄어 보여 줌 (네이버·카카오 앱 비밀번호에도 공백 없음)
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
                return "로그인 실패 - 이메일 주소/앱 비밀번호를 확인하세요. " + loginHint(from)
                        + " (" + firstLine(resp) + ")";
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
