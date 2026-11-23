package com.garpsu.wifiscan;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * 앱 로그: 앱 내부 파일(app.log)에 한 줄씩 쌓음. 사용자가 [...] 메뉴 [로그 보내기] 를 누르면
 * 사용자 본인 Gmail 로 첨부 파일 전송 후 삭제. 보내지 않고 너무 커지면 오래된 앞부분부터 잘라 냄.
 * 형식: 2026-11-23 14:30:05.123 [분류] 내용
 */
final class AppLog {

    private static final String FILE = "app.log";
    private static final long MAX_BYTES = 2 * 1024 * 1024; // 2MB 넘으면 앞쪽 절반을 버림
    private static final Object LOCK = new Object();

    private AppLog() {
    }

    // 분류: 앱 / 서비스 / 판정 / 보내기 / 요청 / 위치 / 메일 / 설정 / 권한 / 오류
    static void add(Context ctx, String kind, String msg) {
        if (ctx == null) {
            return;
        }
        String line = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.KOREA).format(new Date())
                + " [" + kind + "] " + msg.replace("\r", "").replace("\n", " / ") + "\n";
        synchronized (LOCK) {
            try {
                File file = file(ctx);
                if (file.length() > MAX_BYTES) {
                    trim(file);
                }
                try (FileOutputStream out = new FileOutputStream(file, true)) {
                    out.write(line.getBytes(StandardCharsets.UTF_8));
                }
            } catch (Exception ignored) {
                // 로그 실패로 앱 동작이 멈추면 안 됨
            }
        }
    }

    // 오류: 예외의 종류·내용·발생 위치(맨 위 몇 줄)까지
    static void error(Context ctx, String where, Throwable ex) {
        StringBuilder sb = new StringBuilder(where).append(" - ").append(ex);
        StackTraceElement[] stack = ex.getStackTrace();
        for (int i = 0; i < stack.length && i < 6; i++) {
            sb.append(" | at ").append(stack[i]);
        }
        add(ctx, "오류", sb.toString());
    }

    static File file(Context ctx) {
        return new File(ctx.getApplicationContext().getFilesDir(), FILE);
    }

    static long size(Context ctx) {
        return file(ctx).length();
    }

    // 지금까지의 로그 내용 (보내기용). 맨 앞에 기기 정보 머리말
    static byte[] read(Context ctx) {
        synchronized (LOCK) {
            try {
                File file = file(ctx);
                byte[] body = new byte[(int) file.length()];
                try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
                    raf.readFully(body);
                }
                String head = "# 집에왔어요 로그\n# 기기: " + Build.MANUFACTURER + " " + Build.MODEL
                        + " · Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")\n"
                        + "# 앱 버전: " + version(ctx) + "\n# 보낸 시각: "
                        + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.KOREA).format(new Date()) + "\n\n";
                byte[] headBytes = head.getBytes(StandardCharsets.UTF_8);
                byte[] all = new byte[headBytes.length + body.length];
                System.arraycopy(headBytes, 0, all, 0, headBytes.length);
                System.arraycopy(body, 0, all, headBytes.length, body.length);
                return all;
            } catch (Exception e) {
                return new byte[0];
            }
        }
    }

    static void clear(Context ctx) {
        synchronized (LOCK) {
            //noinspection ResultOfMethodCallIgnored
            file(ctx).delete();
        }
    }

    static String version(Context ctx) {
        try {
            return ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "?";
        }
    }

    // 너무 커지면 앞쪽(오래된) 절반을 버리고 뒤쪽만 남김 (줄 중간에서 끊지 않게 다음 줄부터)
    private static void trim(File file) throws Exception {
        long len = file.length();
        byte[] tail = new byte[(int) (len / 2)];
        try (RandomAccessFile raf = new RandomAccessFile(file, "r")) {
            raf.seek(len - tail.length);
            raf.readFully(tail);
        }
        int start = 0;
        while (start < tail.length && tail[start] != '\n') {
            start++;
        }
        try (FileOutputStream out = new FileOutputStream(file, false)) {
            out.write("(앞부분은 크기 제한으로 잘림)\n".getBytes(StandardCharsets.UTF_8));
            out.write(tail, Math.min(start + 1, tail.length), tail.length - Math.min(start + 1, tail.length));
        }
    }
}
