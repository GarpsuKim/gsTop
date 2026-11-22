package com.garpsu.wifiscan;

import android.content.SharedPreferences;
import android.util.Base64;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * 이메일 원격 위치 요청: 사용자 Gmail 받은편지함(IMAP, SSL 993)에 접속해 새 메일을 기다린다 (IMAP IDLE).
 * 제목에 "#where"(영어 고정, 대소문자 무관)가 있는 새 메일이 오면 Callback.onRequest 로 알려 줌.
 * - 앱 비밀번호(사용자 등록 1075)로 로그인. 권한 필요 없음 (인터넷만)
 * - 메일 내용은 읽지 않고 보낸 사람·제목 머리글만 봄 (BODY.PEEK: 읽음 표시 안 함)
 * - 처음 켤 때 이미 와 있던 메일은 무시 (마지막으로 확인한 UID 를 설정 "mailUid" 에 저장)
 * 네트워크 사용 -> 작업 스레드에서 호출. 다른 스레드에서 stop() 으로 끊을 수 있음.
 */
final class MailWatcher {

    interface Callback {
        boolean running();

        // 위치 요청 메일. 처리했으면 true (그 메일을 읽음으로 표시), 무시했으면 false
        boolean onRequest(String fromAddr, String subject);

        // 지금 상태 (화면 표시용): "새 메일 대기 중 (마지막 확인 10:42)" 등
        void state(String s);
    }

    private static String now() {
        return new java.text.SimpleDateFormat("HH:mm", Locale.KOREA).format(new java.util.Date());
    }

    static final String HOST = "imap.gmail.com";
    private static final Pattern LITERAL = Pattern.compile("\\{(\\d+)\\}$");
    private static final Pattern UID = Pattern.compile("UID (\\d+)");
    private static final Pattern UIDNEXT = Pattern.compile("UIDNEXT (\\d+)");
    private static final Pattern ADDR = Pattern.compile("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}");
    private static final Pattern ENCODED = Pattern.compile("=\\?([^?]+)\\?([BbQq])\\?([^?]*)\\?=");

    private volatile SSLSocket s;
    private InputStream in;
    private OutputStream out;
    private int tagN;

    // 접속해서 끊길 때까지 감시. 정상 종료면 null, 문제가 있으면 오류 내용
    String watch(String user, String pass, SharedPreferences st, Callback cb) {
        try {
            return session(user.trim(), pass.replace(" ", ""), st, cb);
        } catch (SocketTimeoutException e) {
            return "응답 시간 초과";
        } catch (Exception e) {
            return cb.running() ? "연결 끊김 (인터넷 확인)" : null;
        } finally {
            stop();
        }
    }

    // 다른 스레드에서 감시 중단 (대기 중인 읽기를 바로 끝냄)
    void stop() {
        SSLSocket x = s;
        if (x != null) {
            try {
                x.close();
            } catch (Exception ignored) {
            }
        }
    }

    // 접속 + 로그인 + 받은편지함 열기. 성공하면 null (uidNext[0] = 다음 UID), 실패하면 오류 내용
    private String open(String user, String pass, long[] uidNext) throws Exception {
        Socket raw = new Socket();
        raw.connect(new InetSocketAddress(HOST, 993), 15000);
        s = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket(raw, HOST, 993, true);
        s.setSoTimeout(30000);
        s.startHandshake();
        // 인증서가 실제 Gmail 서버의 것인지 확인 (가짜 서버에 앱 비밀번호가 넘어가지 않게)
        if (!HttpsURLConnection.getDefaultHostnameVerifier().verify(HOST, s.getSession())) {
            return "서버 인증서 확인 실패 - 공용 Wi-Fi 등 네트워크를 확인하세요";
        }
        in = new BufferedInputStream(s.getInputStream());
        out = s.getOutputStream();
        readLogical(); // 인사말 "* OK"

        List<String> r = cmd("LOGIN " + quote(user) + " " + quote(pass));
        if (!ok(r)) {
            return "로그인 실패 - 사용자 등록의 Gmail·앱 비밀번호를 확인하세요 (" + r.get(r.size() - 1) + ")";
        }
        r = cmd("SELECT INBOX");
        if (!ok(r)) {
            return "받은편지함 열기 실패";
        }
        for (String line : r) {
            Matcher m = UIDNEXT.matcher(line);
            if (m.find()) {
                uidNext[0] = Long.parseLong(m.group(1));
            }
        }
        return null;
    }

    private String session(String user, String pass, SharedPreferences st, Callback cb) throws Exception {
        long[] un = new long[1];
        String err = open(user, pass, un);
        if (err != null) {
            return err;
        }
        long uidNext = un[0];
        findTrash();

        // 0) 접속할 때마다(앱 시작 포함): 받은편지함에 남아 있는 위치 요청 메일을 모두 찾아 회신·삭제
        //    (처리한 요청 메일은 지우므로, 남아 있는 것 = 아직 회신하지 않은 요청)
        cb.state("받은편지함의 위치 요청 메일 찾는 중...");
        scanInbox(uidNext, cb);
        long last = Math.max(0, uidNext - 1); // 이후로는 새로 오는 메일만 확인
        st.edit().putLong("mailUid", last).apply();

        while (cb.running()) {
            // 1) 새 메일(마지막 확인 UID 다음부터)의 보낸 사람·제목만 확인
            List<String> r = cmd("UID FETCH " + (last + 1) + ":* (UID BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])");
            if (!ok(r)) {
                return "메일 확인 실패";
            }
            last = handle(r, last, cb);
            st.edit().putLong("mailUid", last).apply();
            cb.state("새 메일 대기 중 (마지막 확인 " + now() + ")");

            // 2) 새 메일이 올 때까지 대기 (IMAP IDLE). 4분마다 다시 확인
            //    (통신사 장비가 조용한 연결을 몇 분 만에 몰래 끊는 경우가 있어 짧게)
            String tag = nextTag();
            write(tag + " IDLE");
            if (!readLogical().startsWith("+")) {
                return "메일 서버가 대기(IDLE)를 지원하지 않습니다";
            }
            s.setSoTimeout(4 * 60 * 1000);
            try {
                while (cb.running()) {
                    if (readLogical().contains("EXISTS")) {
                        break; // 새 메일 도착
                    }
                }
            } catch (SocketTimeoutException ignored) {
                // 4분 동안 새 메일 없음 -> 다시 확인 후 대기
            }
            s.setSoTimeout(30000);
            write("DONE");
            String l;
            do {
                l = readLogical();
            } while (!l.startsWith(tag + " "));
        }
        try {
            cmd("LOGOUT");
        } catch (Exception ignored) {
        }
        return null;
    }

    // FETCH 결과에서 UID 가 after 보다 큰 메일들을 검사: 위치 요청이고 받아들여지면(onRequest true) 삭제.
    // 가장 큰 UID 를 돌려줌
    private long handle(List<String> r, long after, Callback cb) throws IOException {
        long max = after;
        java.util.Set<Long> seen = new java.util.HashSet<>();
        for (String line : r) {
            Matcher m = UID.matcher(line);
            if (!line.startsWith("*") || !line.contains("FETCH") || !m.find()) {
                continue;
            }
            long uid = Long.parseLong(m.group(1));
            if (uid <= after || !seen.add(uid)) {
                continue; // "n:*" 는 새 메일이 없으면 마지막 메일 하나를 돌려줌
            }
            max = Math.max(max, uid);
            String subject = decode(header(line, "Subject")).trim();
            Matcher a = ADDR.matcher(header(line, "From"));
            if (a.find() && isRequest(subject) && cb.onRequest(a.group().toLowerCase(Locale.US), subject)) {
                delete(uid); // 처리한 요청 메일은 삭제 (휴지통으로)
            }
        }
        return max;
    }

    // 받은편지함 전체에서 위치 요청 메일 찾기: 제목 검색("where") + 혹시 몰라 최근 100통
    private void scanInbox(long uidNext, Callback cb) throws IOException {
        java.util.TreeSet<Long> uids = new java.util.TreeSet<>();
        for (String l : cmd("UID SEARCH SUBJECT \"where\"")) {
            if (l.startsWith("* SEARCH")) {
                for (String t : l.substring(8).trim().split("\\s+")) {
                    try {
                        uids.add(Long.parseLong(t));
                    } catch (NumberFormatException ignored) {
                    }
                }
            }
        }
        while (uids.size() > 200) {
            uids.pollFirst(); // 너무 많으면 최근 200통만
        }
        StringBuilder set = new StringBuilder(Math.max(1, uidNext - 100) + ":*");
        for (Long u : uids) {
            set.append(',').append(u);
        }
        List<String> r = cmd("UID FETCH " + set + " (UID BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])");
        if (ok(r)) {
            handle(r, 0, cb);
        }
    }

    // 휴지통 폴더 이름 (Gmail 언어에 따라 다름: [Gmail]/Trash, [Gmail]/휴지통 …) -> LIST 의 \Trash 표시로 찾음
    private String trash;
    private static final Pattern LIST = Pattern.compile("^\\* LIST \\(([^)]*)\\) (?:\"[^\"]*\"|NIL) (.+)$");

    private void findTrash() throws IOException {
        trash = null;
        for (String l : cmd("LIST \"\" \"*\"")) {
            Matcher m = LIST.matcher(l);
            if (m.find() && m.group(1).contains("\\Trash")) {
                trash = m.group(2).trim();
            }
        }
    }

    // 메일 삭제: 휴지통으로 옮김 (30일 뒤 Gmail 이 완전 삭제). 안 되면 삭제 표시 + 지우기
    private void delete(long uid) throws IOException {
        if (trash != null && ok(cmd("UID MOVE " + uid + " " + trash))) {
            return;
        }
        cmd("UID STORE " + uid + " +FLAGS (\\Deleted)");
        cmd("UID EXPUNGE " + uid);
    }

    // [지금 확인] 진단: 접속·로그인 결과와 받은편지함 최근 메일 5통을 앱이 어떻게 판단하는지 보여 줌 (처리는 안 함)
    static String diagnose(String user, String pass, long lastUid, java.util.Set<String> allowed) {
        MailWatcher w = new MailWatcher();
        StringBuilder sb = new StringBuilder();
        try {
            long[] un = new long[1];
            String err = w.open(user.trim(), pass.replace(" ", ""), un);
            if (err != null) {
                return "❌ " + err;
            }
            sb.append("✓ Gmail 접속·로그인 성공 (").append(user.trim()).append(")\n");
            sb.append("받은편지함 다음 번호: ").append(un[0])
                    .append(" / 앱이 마지막으로 확인한 번호: ").append(lastUid == 0 ? "아직 없음" : String.valueOf(lastUid))
                    .append("\n\n최근 메일 5통:\n");
            long from = Math.max(1, un[0] - 5);
            List<String> r = w.cmd("UID FETCH " + from + ":* (UID BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])");
            int n = 0;
            for (String line : r) {
                Matcher m = UID.matcher(line);
                if (!line.startsWith("*") || !line.contains("FETCH") || !m.find()) {
                    continue;
                }
                n++;
                long uid = Long.parseLong(m.group(1));
                String rawSubject = header(line, "Subject");
                String subject = decode(rawSubject).trim();
                Matcher a = ADDR.matcher(header(line, "From"));
                String addr = a.find() ? a.group().toLowerCase(Locale.US) : "(주소 없음)";
                boolean req = isRequest(subject);
                sb.append("\n#").append(uid).append("  ").append(addr).append("\n  제목: \"").append(subject).append("\"");
                if (!req) {
                    sb.append("\n  원래 제목: \"").append(rawSubject).append("\"");
                    if (rawSubject.isEmpty()) {
                        sb.append("\n  (머리글 원문: ").append(line.length() > 300 ? line.substring(0, 300) : line).append(")");
                    }
                }
                sb.append("\n  → ")
                        .append(req ? "위치 요청 ✓" : "위치 요청 아님");
                if (req) {
                    sb.append(allowed.contains(addr) ? " · 등록된 보호자 ✓" : " · 등록 안 된 주소 ✗");
                    sb.append(allowed.contains(addr) ? " · 앱 시작(재접속) 때 회신하고 삭제" : "");
                }
                sb.append('\n');
            }
            if (n == 0) {
                sb.append("(메일 없음)\n");
            }
            sb.append("\n등록된 보호자 이메일: ").append(allowed.isEmpty() ? "없음" : android.text.TextUtils.join(", ", allowed));
            return sb.toString();
        } catch (Exception e) {
            return sb + "\n❌ 연결 오류: " + e;
        } finally {
            w.stop();
        }
    }

    // 제목이 위치 요청인지: 영어 "#where" 고정. 대소문자는 아무렇게나 섞여도 됨 (#WHERE, #wHeRe …)
    // 너그럽게: 전각 ＃·전각 영문, 보이지 않는 글자, 앞뒤에 붙은 말("Re: #where", "#where please") 허용
    // "#wherever" 처럼 뒤에 영문이 더 붙은 것은 아님. 한글("#위치")은 인식하지 않음
    static boolean isRequest(String subject) {
        String s = java.text.Normalizer.normalize(subject, java.text.Normalizer.Form.NFKC) // 전각 ＃ｗｈｅｒｅ -> #where
                .replaceAll("[\\u200B-\\u200D\\uFEFF\\u00A0]", " ")
                .toLowerCase(Locale.US);
        return s.matches("(?s).*#\\s*where(?![a-z]).*");
    }

    // ---------- IMAP 주고받기 ----------

    private String nextTag() {
        return "a" + (++tagN);
    }

    private void write(String line) throws IOException {
        out.write((line + "\r\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }

    // 명령 보내고, 태그가 붙은 마지막 줄까지 읽음 (마지막 원소 = 결과 줄)
    private List<String> cmd(String c) throws IOException {
        String tag = nextTag();
        write(tag + " " + c);
        List<String> r = new ArrayList<>();
        while (true) {
            String l = readLogical();
            r.add(l);
            if (l.startsWith(tag + " ")) {
                return r;
            }
        }
    }

    private static boolean ok(List<String> r) {
        String last = r.get(r.size() - 1);
        int sp = last.indexOf(' ');
        return sp > 0 && last.startsWith("OK", sp + 1);
    }

    // 한 줄 읽기. 줄 끝이 {N} 이면 N 바이트를 더 읽어 이어 붙임 (메일 머리글이 이렇게 옴)
    private String readLogical() throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        while (true) {
            byte[] line = readRawLine();
            buf.write(line);
            Matcher m = LITERAL.matcher(new String(line, StandardCharsets.ISO_8859_1));
            if (!m.find()) {
                break;
            }
            int n = Integer.parseInt(m.group(1));
            buf.write('\r');
            buf.write('\n');
            byte[] lit = new byte[n];
            int got = 0;
            while (got < n) {
                int k = in.read(lit, got, n - got);
                if (k < 0) {
                    throw new IOException("끊김");
                }
                got += k;
            }
            buf.write(lit);
        }
        return new String(buf.toByteArray(), StandardCharsets.UTF_8);
    }

    private byte[] readRawLine() throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int c;
        while ((c = in.read()) >= 0) {
            if (c == '\n') {
                byte[] a = b.toByteArray();
                int len = a.length > 0 && a[a.length - 1] == '\r' ? a.length - 1 : a.length;
                byte[] r = new byte[len];
                System.arraycopy(a, 0, r, 0, len);
                return r;
            }
            b.write(c);
        }
        throw new IOException("끊김");
    }

    private static String quote(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    // ---------- 메일 머리글 ----------

    // 머리글 묶음에서 한 항목 값 (여러 줄로 접힌 것은 펼침)
    static String header(String block, String name) {
        String unfolded = block.replaceAll("\r\n[ \t]+", " ");
        for (String l : unfolded.split("\r\n")) {
            if (l.regionMatches(true, 0, name + ":", 0, name.length() + 1)) {
                return l.substring(name.length() + 1).trim();
            }
        }
        return "";
    }

    // "=?UTF-8?B?I+ychOy5mA==?=" 같은 인코딩된 제목을 글자로
    static String decode(String s) {
        s = s.replaceAll("(\\?=)\\s+(=\\?)", "$1$2"); // 이어진 인코딩 조각 사이 공백 제거
        Matcher m = ENCODED.matcher(s);
        StringBuffer sb = new StringBuffer();
        while (m.find()) {
            String text;
            try {
                String name = m.group(1);
                Charset cs;
                try {
                    cs = Charset.forName(name);
                } catch (Exception e) {
                    cs = Charset.forName("EUC-KR"); // ks_c_5601-1987 등 옛 한글 이름
                }
                byte[] bytes;
                if (m.group(2).equalsIgnoreCase("B")) {
                    bytes = Base64.decode(m.group(3), Base64.DEFAULT);
                } else {
                    bytes = qDecode(m.group(3));
                }
                text = new String(bytes, cs);
            } catch (Exception e) {
                text = m.group();
            }
            m.appendReplacement(sb, Matcher.quoteReplacement(text));
        }
        m.appendTail(sb);
        return sb.toString();
    }

    private static byte[] qDecode(String q) {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        for (int i = 0; i < q.length(); i++) {
            char c = q.charAt(i);
            if (c == '_') {
                b.write(' ');
            } else if (c == '=' && i + 2 < q.length()) {
                b.write(Integer.parseInt(q.substring(i + 1, i + 3), 16));
                i += 2;
            } else {
                b.write(c);
            }
        }
        return b.toByteArray();
    }
}
