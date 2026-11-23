package com.garpsu.wifiscan;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * 백그라운드 작업 (포그라운드 서비스, 상단 알림 1개).
 * 1) 귀가 감지: Wi-Fi 연결이 바뀔 때마다 홈 공유기 연결 여부 검사 -> 홈 / 외출, 귀가 알림
 * 2) 텔레그램 자동 송신: 정해진 간격마다 현재 위치를 텔레그램으로 전송
 * 3) 카톡 자동 보내기: 정해진 간격마다 위치를 측정해 알림 -> 누르면 카톡 공유 화면 (전송은 사람이)
 * 하나라도 켜져 있으면 실행되고, 모두 꺼지면 종료된다.
 */
public class HomeService extends Service {

    static final String PREF_BG = "bg";                 // "home" 설정: 백그라운드 귀가 감지 켜짐
    static final String PREF_KAKAO = "kakao";           // 카톡 자동 보내기 설정
    static final String PREF_SCHED = "sched";           // 1040 공통 보내는 시각 (지정 시각 / 일정 간격)
    static final String KAKAO_PKG = "com.kakao.talk";
    static final String PREF_EMAIL = "email";           // 이메일 자동 송신 설정
    static final String GMAIL_PKG = "com.google.android.gm";
    private static final String ACTION_TG = "com.garpsu.wifiscan.TG_SEND";
    private static final String ACTION_KAKAO = "com.garpsu.wifiscan.KAKAO_SEND";
    private static final String ACTION_EMAIL = "com.garpsu.wifiscan.EMAIL_SEND";
    private static final String CH_EMAIL = "email";     // 이메일 보내기 알림 (소리/팝업)
    private static final int ID_EMAIL = 4;
    // 카톡 이메일: 일반 이메일과 같은 방식, 받는 사람이 카카오메일(@kakao.com) -> 카톡에서 받아봄. 설정은 따로
    static final String PREF_KMAIL = "kmail";
    private static final String ACTION_KMAIL = "com.garpsu.wifiscan.KMAIL_SEND";
    private static final int ID_KMAIL = 5;
    // 일반 문자(수동): 정해진 시각마다 알림 -> 누르면 문자 앱(받는 사람·내용 채워짐) -> 직접 [전송]. 문자 권한 불필요
    static final String PREF_SMS = "sms";
    private static final String ACTION_SMS = "com.garpsu.wifiscan.SMS_SEND";
    // 일반 문자(자동): 솔라피로 폰에서 바로 발송 (apiKey, apiSecret, from, to, 시각 설정)
    static final String PREF_SMSAUTO = "smsauto";
    private static final String ACTION_SMSAUTO = "com.garpsu.wifiscan.SMSAUTO_SEND";
    private static final int ID_SMS = 6;
    // 본인 폰 알림: 정해진 시각마다 이 폰에 현재 위치 알림 (아무에게도 보내지 않음)
    static final String PREF_SELF = "self";
    private static final String ACTION_SELF = "com.garpsu.wifiscan.SELF_NOTIFY";
    private static final String CH_SELF = "self";
    private static final int ID_SELF = 7;
    // 귀가·외출 순간 알림 (home / away 켜짐, ch_* 보낼 곳)
    static final String PREF_EVENT = "event";
    private static final String ACTION_CHECK = "com.garpsu.wifiscan.AWAY_CHECK";      // 나간 뒤 2분 후 재확인
    private static final String ACTION_EVENT_TEST = "com.garpsu.wifiscan.EVENT_TEST"; // 테스트 보내기
    private static final String ACTION_LATE = "com.garpsu.wifiscan.LATE_CHECK";       // 미귀가 확인 시각
    private static final String ACTION_SOS = "com.garpsu.wifiscan.SOS";               // 긴급 SOS 위치 전송
    private static final String ACTION_START_MSG = "com.garpsu.wifiscan.START_MSG";   // 앱 시작 위치 알림
    private static final String ACTION_STOP_MSG = "com.garpsu.wifiscan.STOP_MSG";     // 앱 종료 위치 알림
    static final String ACTION_MAIL_YES = "com.garpsu.wifiscan.MAIL_YES"; // 이메일 위치 요청: 회신 (Yes)
    static final String ACTION_MAIL_NO = "com.garpsu.wifiscan.MAIL_NO";   // 이메일 위치 요청: 회신 안 함 (No)
    static final String ACTION_MAIL_RESCAN = "com.garpsu.wifiscan.MAIL_RESCAN"; // 앱 시작: 받은편지함 요청 메일 다시 찾기

    // 앱을 켤 때: Gmail 에 다시 접속해 받은편지함에 남아 있는 위치 요청 메일을 찾아 회신·삭제
    static void mailRescan(Context ctx) {
        if (!mailRemoteOn(ctx)) {
            return;
        }
        Intent intent = new Intent(ctx, HomeService.class).setAction(ACTION_MAIL_RESCAN);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }
    private static final String CH_STATUS = "status";   // 상시 알림 (조용히)
    private static final String CH_ALERT = "alert";     // 귀가 알림 (소리/팝업)
    private static final String CH_KAKAO = "kakao";     // 카톡 보내기 알림 (소리/팝업)
    private static final int ID_STATUS = 1, ID_ALERT = 2, ID_KAKAO = 3;
    private static final long AWAY_MIN = 2 * 60 * 1000; // 이 시간 이상 밖에 있다 돌아와야 '귀가'
    private static final long SAFETY = 60 * 1000;        // 혹시 놓친 변화를 위해 1분마다 재검사
    private static final long FIX_TIMEOUT = 30 * 1000;   // 위치 측정 최대 대기

    private ConnectivityManager cm;
    private LocationManager lm;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private String homeTitle = "외출", homeNote = "";
    private boolean tgBusy, kakaoBusy;
    private final Set<String> jobBusy = new HashSet<>(); // 처리 중인 작업 설정 (email / kmail / sms / self)

    private final Runnable check = this::evaluate;
    private final Runnable periodic = new Runnable() {
        @Override
        public void run() {
            evaluate();
            handler.postDelayed(this, SAFETY);
        }
    };

    private final ConnectivityManager.NetworkCallback netCb = new ConnectivityManager.NetworkCallback() {
        @Override
        public void onAvailable(Network network) {
            schedule();
        }

        @Override
        public void onLost(Network network) {
            schedule();
        }

        @Override
        public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
            schedule();
        }
    };

    // ---------- 시작 / 중지 (Activity, 부팅 수신기에서 호출) ----------

    // ---------- 로그 도우미 ----------

    // 켜진 기능 목록 (로그용)
    static String featureSummary(Context ctx) {
        List<String> on = new ArrayList<>();
        if (homeOn(ctx)) on.add("백그라운드 감지");
        if (eventOn(ctx)) on.add("귀가·외출 알림");
        if (tgOn(ctx)) on.add("텔레그램");
        if (kakaoOn(ctx)) on.add("카톡");
        if (emailOn(ctx, PREF_EMAIL)) on.add("일반 이메일");
        for (String[] job : new String[][]{{PREF_SELF, "본인 알림"}, {PREF_SMS, "문자(수동)"}, {PREF_SMSAUTO, "문자(자동)"}}) {
            if (ctx.getSharedPreferences(job[0], MODE_PRIVATE).getBoolean("on", false)) on.add(job[1]);
        }
        if (remoteOn(ctx)) on.add("텔레그램 원격 요청");
        if (mailRemoteOn(ctx)) on.add("이메일 원격 요청("
                + (ctx.getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE).getBoolean("mailRemote", false)
                ? "자동 회신" : "물어보고") + ")");
        if (awayOnly(ctx)) on.add("외출 중에만");
        on.add("공통 시각 " + emailSchedule(ctx.getSharedPreferences(PREF_SCHED, MODE_PRIVATE)));
        on.add("블루투스 포함 " + (BtHome.scanIncluded(ctx) ? "켬" : "끔"));
        return android.text.TextUtils.join(", ", on);
    }

    // 위치 한 줄 (로그용): 위도,경도 ±정확도 제공 시각
    static String locText(Location loc) {
        if (loc == null) {
            return "없음 (측정 실패)";
        }
        return String.format(Locale.US, "%.6f,%.6f ±%dm %s %s", loc.getLatitude(), loc.getLongitude(),
                Math.round(loc.getAccuracy()), loc.getProvider(),
                new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date(loc.getTime())));
    }

    // 다음 송신 시각 (로그용)
    static String nextText(long next) {
        return next > 0 ? new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(next)) : "없음";
    }

    // 앱이 예기치 않게 죽으면 그 원인을 로그에 남김 (기존 처리기는 그대로 이어서 실행)
    static void installCrashLog(final Context ctx) {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        if (prev instanceof CrashLogger) {
            return;
        }
        Thread.setDefaultUncaughtExceptionHandler(new CrashLogger(ctx.getApplicationContext(), prev));
    }

    private static final class CrashLogger implements Thread.UncaughtExceptionHandler {
        private final Context app;
        private final Thread.UncaughtExceptionHandler next;

        CrashLogger(Context app, Thread.UncaughtExceptionHandler next) {
            this.app = app;
            this.next = next;
        }

        @Override
        public void uncaughtException(Thread thread, Throwable ex) {
            AppLog.error(app, "앱이 멈춤 (스레드 " + thread.getName() + ")", ex);
            if (next != null) {
                next.uncaughtException(thread, ex);
            }
        }
    }

    static boolean homeOn(Context ctx) {
        return ctx.getSharedPreferences("home", MODE_PRIVATE).getBoolean(PREF_BG, false);
    }

    static boolean tgOn(Context ctx) {
        return ctx.getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).getBoolean("on", false);
    }

    static boolean kakaoOn(Context ctx) {
        return ctx.getSharedPreferences(PREF_KAKAO, MODE_PRIVATE).getBoolean("on", false);
    }

    static boolean emailOn(Context ctx, String pref) {
        if (PREF_KMAIL.equals(pref)) {
            return false; // 카톡 이메일(1160)은 삭제: 예전에 켜 둔 값이 남아 있어도 동작하지 않음
        }
        return ctx.getSharedPreferences(pref, MODE_PRIVATE).getBoolean("on", false);
    }

    static boolean anyOn(Context ctx) {
        return homeOn(ctx) || tgOn(ctx) || kakaoOn(ctx) || emailOn(ctx, PREF_EMAIL) || emailOn(ctx, PREF_KMAIL)
                || emailOn(ctx, PREF_SMS) || emailOn(ctx, PREF_SMSAUTO) || emailOn(ctx, PREF_SELF) || eventOn(ctx) || remoteOn(ctx)
                || mailRemoteOn(ctx);
    }

    // 이메일 위치 요청 감시를 할지: 사용자 등록(1075) Gmail 인증 완료 + 보호자 이메일(1100)이 있으면 항상 감시.
    // (1050 체크박스 "mailRemote" 는 감시 여부가 아니라 '자동 회신 / 물어보고 회신' 선택)
    static boolean mailRemoteOn(Context ctx) {
        SharedPreferences user = ctx.getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        return user.getBoolean("mailVerified", false) && !user.getString("mail", "").isEmpty()
                && !user.getString("mailPass", "").isEmpty() && !guardianEmails(ctx).isEmpty();
    }

    // 보호자 이메일 -> 보호자 이름 (1100 카드). 이름이 없으면 이메일 그대로
    static String guardianName(Context ctx, String email) {
        try {
            org.json.JSONArray arr = new org.json.JSONArray(SosActivity.prefs(ctx).getString("guardians", "[]"));
            for (int i = 0; i < arr.length(); i++) {
                org.json.JSONObject item = arr.getJSONObject(i);
                if (email.equalsIgnoreCase(item.optString("email")) && !item.optString("name").isEmpty()) {
                    return item.optString("name");
                }
            }
        } catch (Exception ignored) {
        }
        return email;
    }

    // 귀가 또는 외출 알림이 켜져 있는지
    static boolean eventOn(Context ctx) {
        SharedPreferences ev = ctx.getSharedPreferences(PREF_EVENT, MODE_PRIVATE);
        return ev.getBoolean("home", false) || ev.getBoolean("away", false) || ev.getBoolean("late", false);
    }

    // 원격 위치 요청 받기가 켜져 있는지 (텔레그램 봇 토큰이 있어야 함)
    static boolean remoteOn(Context ctx) {
        SharedPreferences tg = ctx.getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        return tg.getBoolean("remote", false) && !tg.getString("token", "").isEmpty();
    }

    // 긴급 SOS: 지금 위치를 설정된 자동 채널 전부로 보냄 (다른 기능이 모두 꺼져 있어도 동작)
    // 앱 시작 알림: 지금 위치를 (2) 알림 설정의 '보낼 곳'으로 보냄 (다른 기능이 모두 꺼져 있어도 동작)
    static void startMessage(Context ctx) {
        Intent intent = new Intent(ctx, HomeService.class).setAction(ACTION_START_MSG);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }

    // 앱 종료 알림: 앱 화면을 닫기 직전, 지금 위치를 '보낼 곳'으로 보냄
    static void stopMessage(Context ctx) {
        Intent intent = new Intent(ctx, HomeService.class).setAction(ACTION_STOP_MSG);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }

    static void sos(Context ctx) {
        Intent intent = new Intent(ctx, HomeService.class).setAction(ACTION_SOS);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }

    // [초기화]: 서비스 종료, 모든 예약 알람 취소, 이 앱의 알림 모두 지우기
    static void stopAll(Context ctx) {
        for (String action : new String[]{ACTION_TG, ACTION_KAKAO, ACTION_EMAIL, ACTION_KMAIL,
                ACTION_SMS, ACTION_SMSAUTO, ACTION_SELF, ACTION_CHECK, ACTION_LATE}) {
            cancelAlarm(ctx, action);
        }
        ctx.stopService(new Intent(ctx, HomeService.class));
        ctx.getSystemService(NotificationManager.class).cancelAll();
    }

    // (2) 알림 설정 탭의 [테스트]: 지금 '귀가' 알림을 고른 곳으로 보내 봄
    static void testEvent(Context ctx) {
        Intent intent = new Intent(ctx, HomeService.class).setAction(ACTION_EVENT_TEST);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(intent);
        } else {
            ctx.startService(intent);
        }
    }

    // 문자 앱 작성 화면 (받는 사람 번호·내용이 채워진 상태). 문자 권한 없이 동작
    static Intent smsCompose(String numbers, String body) {
        String nums = numbers.replaceAll("[^0-9+,;]", "").replace(',', ';');
        return new Intent(Intent.ACTION_SENDTO, android.net.Uri.parse("smsto:" + nums))
                .putExtra("sms_body", body)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    }

    // 본인 폰 알림 띄우기 (서비스·화면 공용). 누르면 지도, [공유]로 다른 앱에 보내기
    static void notifySelf(Context ctx, String msg, Location loc) {
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(
                    CH_SELF, "본인 폰 위치 알림", NotificationManager.IMPORTANCE_HIGH));
        }
        Notification.Builder notif = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(ctx, CH_SELF) : new Notification.Builder(ctx);
        Intent open = loc == null
                ? new Intent(ctx, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP)
                : new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(String.format(Locale.US,
                "https://maps.google.com/?q=%.6f,%.6f", loc.getLatitude(), loc.getLongitude())))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Intent share = Intent.createChooser(new Intent(Intent.ACTION_SEND).setType("text/plain")
                .putExtra(Intent.EXTRA_TEXT, msg), "위치 공유").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent piOpen = PendingIntent.getActivity(ctx, 6, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent piShare = PendingIntent.getActivity(ctx, 7, share,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        int nl = msg.indexOf('\n');
        notif.setSmallIcon(android.R.drawable.ic_dialog_map)
                .setContentTitle(nl > 0 ? msg.substring(0, nl) : "📍 현재 위치")
                .setContentText(loc == null ? "위치를 확인하지 못했습니다" : "눌러서 지도 보기")
                .setStyle(new Notification.BigTextStyle().bigText(msg))
                .setContentIntent(piOpen)
                .setColor(0xFF1976D2)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT >= 20) {
            notif.addAction(new Notification.Action.Builder(0, "지도 보기", piOpen).build());
            notif.addAction(new Notification.Action.Builder(0, "공유", piShare).build());
        }
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        nm.notify(ID_SELF, notif.build());
    }

    private static String emailAction(String pref) {
        return PREF_KMAIL.equals(pref) ? ACTION_KMAIL : ACTION_EMAIL;
    }

    // 화면/알림에 쓰는 이름
    static String emailTitle(String pref) {
        return PREF_KMAIL.equals(pref) ? "카톡 이메일" : "일반 이메일";
    }

    // "08:00, 18:30" -> 지금 이후 가장 가까운 시각 (오늘 또는 내일). 형식이 틀리면 0
    static long nextTime(String times, long now) {
        long best = 0;
        for (String part : times.split("[,\\s]+")) {
            int[] hm = parseTime(part);
            if (hm == null) {
                continue;
            }
            java.util.Calendar cal = java.util.Calendar.getInstance();
            cal.setTimeInMillis(now);
            cal.set(java.util.Calendar.HOUR_OF_DAY, hm[0]);
            cal.set(java.util.Calendar.MINUTE, hm[1]);
            cal.set(java.util.Calendar.SECOND, 0);
            cal.set(java.util.Calendar.MILLISECOND, 0);
            if (cal.getTimeInMillis() <= now + 30 * 1000) {
                cal.add(java.util.Calendar.DAY_OF_MONTH, 1);
            }
            if (best == 0 || cal.getTimeInMillis() < best) {
                best = cal.getTimeInMillis();
            }
        }
        return best;
    }

    // "8:05" / "08:05" -> {8, 5}, 틀리면 null
    static int[] parseTime(String text) {
        String[] parts = text.trim().split(":");
        if (parts.length != 2) {
            return null;
        }
        try {
            int hour = Integer.parseInt(parts[0].trim()), minute = Integer.parseInt(parts[1].trim());
            return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 ? new int[]{hour, minute} : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    // 이메일 설정 -> 화면/알림용 설명 ("매일 08:00, 18:00" 또는 "10분마다")
    static String emailSchedule(SharedPreferences sp) {
        return "times".equals(sp.getString("mode", "interval"))
                ? "매일 " + sp.getString("times", "")
                : TelegramSender.intervalLabel(sp.getInt("interval", 60)) + "마다";
    }

    // 보내는 방식: 항상 앱 비밀번호로 발송 (수동 = Gmail 작성 화면 기능은 삭제)
    static boolean emailSmtp(SharedPreferences sp) {
        return true;
    }

    // ---------- 1040 공통 보내는 시각 (모든 채널이 함께 씀, 텔레그램 포함) ----------

    // 공통 시각에 문제가 있으면 이유, 없으면 null
    static String schedProblem(Context ctx) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREF_SCHED, MODE_PRIVATE);
        if (!prefs.contains("mode")) {
            return "(3) 위치 보내기 화면 맨 위 '보내는 시각'을 먼저 저장하세요.";
        }
        if ("times".equals(prefs.getString("mode", "")) && prefs.getString("times", "").trim().isEmpty()) {
            return "(3) 위치 보내기 화면 맨 위 '보내는 시각'에 시각을 하나 이상 등록하세요.";
        }
        return null;
    }

    // 공통 시각을 한 채널 설정에 복사. start 면 다음 송신 시각도 다시 계산
    // (지정 시각: 다음 시각에 / 일정 간격: 바로 1회 후 간격대로)
    static void copySched(Context ctx, SharedPreferences.Editor ed, boolean start) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREF_SCHED, MODE_PRIVATE);
        String mode = prefs.getString("mode", "times");
        String tm = prefs.getString("times", "");
        ed.putString("mode", mode).putString("times", tm).putInt("interval", prefs.getInt("interval", 60));
        if (start) {
            ed.putLong("next", "times".equals(mode) ? nextTime(tm, System.currentTimeMillis()) : 0);
        }
    }

    // 텔레그램이 예전 '자체 간격'만 갖고 있으면 공통 시각으로 맞춤 (업데이트 직후 한 번)
    static void tgFollowSched(Context ctx) {
        SharedPreferences sp = ctx.getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        if (!sp.contains("mode") && ctx.getSharedPreferences(PREF_SCHED, MODE_PRIVATE).contains("mode")) {
            SharedPreferences.Editor ed = sp.edit();
            copySched(ctx, ed, sp.getBoolean("on", false));
            ed.apply();
        }
    }

    // 다음 송신 시각
    static long emailNext(SharedPreferences sp, long now) {
        return "times".equals(sp.getString("mode", "interval"))
                ? nextTime(sp.getString("times", ""), now)
                : now + sp.getInt("interval", 60) * 60 * 1000L;
    }

    // Gmail 작성 화면 (받는 사람, 제목, 내용이 채워진 상태). Gmail이 없으면 다른 메일 앱
    static Intent emailCompose(Context ctx, String to, String subject, String body) {
        Intent intent = new Intent(Intent.ACTION_SENDTO, android.net.Uri.parse("mailto:"))
                .putExtra(Intent.EXTRA_EMAIL, to.split("[,;\\s]+"))
                .putExtra(Intent.EXTRA_SUBJECT, subject)
                .putExtra(Intent.EXTRA_TEXT, body)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (ctx.getPackageManager().getLaunchIntentForPackage(GMAIL_PKG) != null) {
            intent.setPackage(GMAIL_PKG);
        }
        return intent;
    }

    // 예: "[위치] 할머니 · 11-21 18:00 현재 위치" (보내는 사람 이름이 없으면 이름 생략)
    static String emailSubject(Context ctx) {
        String who = TelegramSender.senderName(ctx);
        return "[위치] " + (who.isEmpty() ? "" : who + " · ")
                + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date()) + " 현재 위치";
    }

    // 설정에 맞게 시작하거나 종료 (이미 실행 중이면 설정을 다시 읽음)
    static void refresh(Context ctx) {
        if (!tgOn(ctx)) {
            cancelAlarm(ctx, ACTION_TG);
        }
        if (!kakaoOn(ctx)) {
            cancelAlarm(ctx, ACTION_KAKAO);
        }
        if (!emailOn(ctx, PREF_EMAIL)) {
            cancelAlarm(ctx, ACTION_EMAIL);
        }
        if (!emailOn(ctx, PREF_KMAIL)) {
            cancelAlarm(ctx, ACTION_KMAIL);
        }
        if (!emailOn(ctx, PREF_SMS)) {
            cancelAlarm(ctx, ACTION_SMS);
        }
        if (!emailOn(ctx, PREF_SMSAUTO)) {
            cancelAlarm(ctx, ACTION_SMSAUTO);
        }
        if (!emailOn(ctx, PREF_SELF)) {
            cancelAlarm(ctx, ACTION_SELF);
        }
        if (anyOn(ctx)) {
            Intent intent = new Intent(ctx, HomeService.class);
            if (Build.VERSION.SDK_INT >= 26) {
                ctx.startForegroundService(intent);
            } else {
                ctx.startService(intent);
            }
        } else {
            ctx.stopService(new Intent(ctx, HomeService.class));
        }
    }

    // 카톡 공유 화면 (위치 메시지가 채워진 상태)
    static Intent kakaoShare(String text) {
        return new Intent(Intent.ACTION_SEND)
                .setType("text/plain")
                .putExtra(Intent.EXTRA_TEXT, text)
                .setPackage(KAKAO_PKG)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    }

    // ---------- 서비스 ----------

    @Override
    public void onCreate() {
        super.onCreate();
        installCrashLog(this);
        AppLog.add(this, "서비스", "시작 · 켜진 기능: " + featureSummary(this));
        createChannels();
        try {
            Notification notification = statusNotification("확인 중...", "백그라운드 실행 중");
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(ID_STATUS, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION);
            } else {
                startForeground(ID_STATUS, notification);
            }
        } catch (Exception e) {
            // 위치 권한이 없거나 백그라운드에서 시작이 막힌 경우
            stopSelf();
            return;
        }
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkRequest req = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build();
        cm.registerNetworkCallback(req, netCb);
        handler.post(periodic);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (action != null) {
            AppLog.add(this, "서비스", "명령: " + action.replace("com.garpsu.wifiscan.", ""));
        } else if (intent == null) {
            AppLog.add(this, "서비스", "시스템이 다시 시작함 (START_STICKY)");
        }
        if (ACTION_MAIL_RESCAN.equals(action)) {
            // 감시 중이면 연결을 끊고 바로 다시 접속 (접속할 때마다 받은편지함 전체를 찾음).
            // 감시 중이 아니면 아래 updateMailRemote() 가 새로 시작하면서 찾음
            MailWatcher watcher = mailWatcher;
            if (watcher != null) {
                mailRescanNow = true;
                watcher.stop();
            }
        }
        if (ACTION_MAIL_YES.equals(action) || ACTION_MAIL_NO.equals(action)) {
            // 이메일 위치 요청 확인 알림(또는 화면 1057)에서 Yes / No
            String to = intent.getStringExtra("to");
            getSystemService(NotificationManager.class).cancel(mailAskId(to == null ? "" : to));
            if (to != null && cm != null && ACTION_MAIL_YES.equals(action)) {
                replyMailLocation(to);
            } else if (to != null) {
                AppLog.add(this, "요청", "이메일 위치 요청에 '회신 안 함' 선택 (" + to + ")");
                getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE).edit()
                        .putLong("mailRemoteTime", System.currentTimeMillis())
                        .putString("mailRemoteResult", "회신 안 함 (" + guardianName(this, to) + ")").apply();
            }
        }
        if (cm != null && (ACTION_SOS.equals(action) || ACTION_START_MSG.equals(action) || ACTION_STOP_MSG.equals(action))) {
            // 다른 기능이 모두 꺼져 있어도 SOS·앱 시작 알림은 보냄 (다 보내면 종료)
            sendEvent(ACTION_SOS.equals(action) ? KIND_SOS : ACTION_START_MSG.equals(action) ? KIND_START : KIND_STOP, null, false);
            if (!anyOn(this)) {
                return START_NOT_STICKY;
            }
        }
        if (cm == null || !anyOn(this)) {
            cancelAlarm(this, ACTION_TG);
            cancelAlarm(this, ACTION_KAKAO);
            cancelAlarm(this, ACTION_EMAIL);
            cancelAlarm(this, ACTION_KMAIL);
            cancelAlarm(this, ACTION_SMS);
            cancelAlarm(this, ACTION_SMSAUTO);
            cancelAlarm(this, ACTION_SELF);
            cancelAlarm(this, ACTION_LATE);
            if (jobBusy.isEmpty()) {
                stopSelf();
            }
            return START_NOT_STICKY;
        }
        if (ACTION_EVENT_TEST.equals(action)) {
            sendEvent(true, null, true);
        }
        if (ACTION_CHECK.equals(action)) {
            evaluate(); // 나간 뒤 2분: 절전 중 알람으로 깨어난 김에 바로 확인 (지연 없이)
        }
        schedule();
        scheduleTelegram(ACTION_TG.equals(action));
        scheduleKakao(ACTION_KAKAO.equals(action));
        scheduleEmail(PREF_EMAIL, ACTION_EMAIL.equals(action));
        scheduleNotifyJob(PREF_SMS, ACTION_SMS, ACTION_SMS.equals(action));
        scheduleNotifyJob(PREF_SMSAUTO, ACTION_SMSAUTO, ACTION_SMSAUTO.equals(action));
        scheduleNotifyJob(PREF_SELF, ACTION_SELF, ACTION_SELF.equals(action));
        scheduleLate(ACTION_LATE.equals(action));
        updateRemotePoll();
        updateMailRemote();
        updateStatus();
        return START_STICKY; // 시스템이 종료해도 다시 시작
    }

    @Override
    public void onDestroy() {
        AppLog.add(this, "서비스", "종료 (onDestroy)");
        pollRunning = false; // 원격 위치 요청 수신 중지
        if (pollThread != null) {
            pollThread.interrupt();
        }
        stopMailRemote(); // 이메일 위치 요청 감시 중지
        handler.removeCallbacksAndMessages(null);
        if (cm != null) {
            try {
                cm.unregisterNetworkCallback(netCb);
            } catch (Exception ignored) {
            }
        }
        if (lm != null) {
            lm.removeUpdates(fixListener);
        }
        if (wake != null && wake.isHeld()) {
            wake.release();
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // ---------- 1) 귀가 감지 ----------

    // 연결 직후에는 공유기 정보가 늦게 갱신되므로 잠시 후 검사
    private void schedule() {
        handler.removeCallbacks(check);
        handler.postDelayed(check, 2000);
    }

    private void evaluate() {
        if (!homeOn(this) && !eventOn(this) && !awayOnly(this)) {
            updateStatus();
            return;
        }
        SharedPreferences sp = getSharedPreferences("home", MODE_PRIVATE);
        Set<String> routers = sp.getStringSet("routers", new HashSet<String>());
        String bssid = connectedBssid(this);
        String ssid = null;
        boolean home = false;
        boolean otherRouter = false;
        if (bssid != null) {
            for (String entry : routers) {
                String[] parts = entry.split("\\|", 2);
                if (parts[0].equals(bssid)) {
                    // '기타' 공유기(1010 라디오)는 귀가로 보지 않음 -> 외출과 같게
                    home = MainActivity.routerIsHome(this, bssid);
                    otherRouter = !home;
                    ssid = parts.length > 1 ? parts[1] : "";
                    break;
                }
            }
        }
        boolean hasHome = MainActivity.hasHomeRouter(this); // '홈'으로 체크된 공유기가 있나

        // 홈 PC 블루투스: 공유기로 '집'이 아닐 때, 최근 3분 안에 페어링된 PC를 찾았으면 '집'
        boolean btAny = !BtHome.registered(this).isEmpty(); // 등록한 블루투스 기기(홈·기타)
        boolean btReg = BtHome.hasHome(this);                 // '홈'으로 체크된 블루투스 기기
        boolean btHome = false;
        if (!home && btAny) {
            long seenAgo = System.currentTimeMillis() - sp.getLong("btSeenAt", 0);
            btHome = seenAgo < BtHome.FRESH && BtHome.isHome(this, BtHome.seenMac(this)); // '기타' 기기는 집 아님
            if (seenAgo > BtHome.FRESH - 60 * 1000) {
                startBtProbe(); // 곧 만료되거나 만료됨 -> 다시 찾아봄 (결과가 나오면 다시 판정)
            }
            setAlarm(ACTION_CHECK, System.currentTimeMillis() + 3 * 60 * 1000); // 공유기 밖이면 3분마다 확인
        }
        if (btHome) {
            home = true;
            ssid = "PC 블루투스 · " + sp.getString("btSeenName", "");
        }

        // 확인할 수 없는 상황(등록 없음 / 위치 서비스 꺼짐)에서는 상태를 바꾸지 않음
        // (안 그러면 위치 서비스를 끄는 순간 '외출'로 잘못 알림). PC 블루투스로 '집'이 확인되면 확실
        boolean known = btHome || (hasHome && locationOn(this));

        // 이전 상태 (서비스가 재시작되어도 이어지도록 저장)
        int prev = sp.getInt("bgState", -1); // -1 모름, 0 외출, 1 홈
        long leftAt = sp.getLong("leftAt", 0);
        long now = System.currentTimeMillis();
        SharedPreferences ev = getSharedPreferences(PREF_EVENT, MODE_PRIVATE);
        SharedPreferences.Editor ed = sp.edit();
        boolean leaveConfirmed = false; // 이번 판정에서 '외출 확정'이 되었는지
        if (!known) {
            // 그대로 둠
        } else if (home) {
            // 2분 넘게 밖에 있다가 돌아옴 = 귀가
            if (prev == 0 && leftAt > 0 && now - leftAt >= AWAY_MIN) {
                if (homeOn(this)) {
                    alertHomeBack(ssid);
                }
                if (ev.getBoolean("home", false)) {
                    sendEvent(true, ssid, false);
                }
            }
            ed.putLong("leftAt", 0).putBoolean("awaySent", false).putBoolean("fromHome", false);
        } else if (prev != 0 || leftAt == 0) {
            // 방금 나감 (또는 상태를 처음 앎): 떠난 시각 기록, 2분 뒤 다시 확인하도록 알람
            ed.putLong("leftAt", now).putBoolean("awaySent", false)
                    .putBoolean("fromHome", prev == 1); // 집에 있다가 나간 경우만 '외출' 알림 대상
            setAlarm(ACTION_CHECK, now + AWAY_MIN + 5000);
        } else if (!sp.getBoolean("awaySent", false) && leftAt > 0 && now - leftAt >= AWAY_MIN) {
            // 2분 넘게 계속 밖 = 외출 확정
            ed.putBoolean("awaySent", true);
            leaveConfirmed = true;
            if (sp.getBoolean("fromHome", false) && ev.getBoolean("away", false)) {
                sendEvent(false, null, false);
            }
        }
        // 로그: 매번 검사 결과, 상태가 바뀌면 따로 한 줄 더
        String seen = (bssid == null ? "Wi-Fi 연결 없음" : "공유기 " + (ssid == null || btHome ? "" : ssid) + " (" + bssid.toUpperCase(Locale.US)
                + ", 별명 " + (MainActivity.routerAlias(this, bssid).isEmpty() ? "(없음)" : MainActivity.routerAlias(this, bssid))
                + (otherRouter ? ", 기타" : home && !btHome ? ", 홈" : ", 등록 안 됨") + ")")
                + (btHome ? " · 홈 블루투스 " + BtHome.seenName(this) : BtHome.seenOther(this) ? " · 기타 블루투스 " + BtHome.seenName(this) : "");
        AppLog.add(this, "판정", "검사: " + (known ? (home ? "홈" : "외출") : "알 수 없음") + " · " + seen);
        if (known && prev != (home ? 1 : 0)) {
            AppLog.add(this, "판정", "상태 바뀜: " + (prev == 1 ? "홈" : prev == 0 ? "외출" : "모름") + " -> " + (home ? "홈" : "외출"));
        }
        if (leaveConfirmed) {
            AppLog.add(this, "판정", "외출 확정 (2분 넘게 밖)");
        }
        if (known) {
            ed.putInt("bgState", home ? 1 : 0);
        }
        ed.apply();
        if (leaveConfirmed && awayOnly(this)) {
            startSendingOnLeave(); // 외출 중에만 보내기: 지금부터 위치 보내기 시작
        }

        homeTitle = home ? "홈" : "외출";
        if (btHome) {
            homeNote = "홈 PC 블루투스 확인됨 · " + sp.getString("btSeenName", "");
        } else if (!hasHome && !btReg) {
            homeNote = routers.isEmpty() ? "등록된 홈 공유기가 없습니다"
                    : "'홈'으로 체크된 공유기가 없습니다 (공유기 화면에서 체크)";
        } else if (!locationOn(this) && !btReg) {
            homeNote = "위치 서비스가 꺼져 있어 공유기를 확인할 수 없습니다";
        } else if (home) {
            homeNote = "홈 공유기 연결됨" + (ssid == null || ssid.isEmpty() ? "" : " · " + ssid);
        } else {
            if (otherRouter) {
                String alias = MainActivity.routerAlias(this, bssid);
                homeNote = "기타 공유기 연결됨 · " + (alias.isEmpty() ? ssid : alias);
            } else if (BtHome.seenOther(this)) {
                homeNote = "기타 블루투스 기기 가까이 있음 · " + BtHome.label(this, BtHome.seenMac(this), BtHome.seenName(this));
            } else {
                homeNote = bssid == null ? "Wi-Fi 연결 없음" : "다른 공유기에 연결됨";
            }
        }
        updateStatus();
    }

    // ---------- 1-1) 홈 PC 블루투스 찾기 (작업 스레드, 최대 몇 초) ----------

    private boolean btBusy;
    private long btLastProbe;

    private void startBtProbe() {
        long now = System.currentTimeMillis();
        if (btBusy || now - btLastProbe < 50 * 1000 || !BtHome.enabled(this) || !BtHome.hasPerm(this)) {
            return;
        }
        btBusy = true;
        btLastProbe = now;
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        final PowerManager.WakeLock lock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "wifiscan:bt");
        lock.acquire(40 * 1000);
        new Thread(() -> {
            final String[] found = BtHome.findAny(this);
            handler.post(() -> {
                btBusy = false;
                if (found != null) {
                    BtHome.markSeen(this, found[0], found[1]);
                    evaluate(); // 찾았으면 바로 다시 판정 (귀가)
                }
                if (lock.isHeld()) {
                    lock.release();
                }
            });
        }).start();
    }

    // ---------- 1-2) 귀가·외출 순간 알림: 고른 곳으로 바로 보내기 ----------

    static final int KIND_HOME = 0, KIND_AWAY = 1, KIND_LATE = 2, KIND_SOS = 3, KIND_START = 4, KIND_STOP = 5;
    private static final String[] KIND_NAME = {"귀가", "외출", "미귀가", "SOS", "앱 시작", "앱 종료"};

    // 예전 호출 형태 유지: arrived true 귀가 / false 외출
    private void sendEvent(boolean arrived, String ssid, boolean test) {
        sendEvent(arrived ? KIND_HOME : KIND_AWAY, ssid, test);
    }

    /*
     * 귀가 / 외출 / 미귀가 / SOS 알림 보내기
     * - 귀가·외출·미귀가: (2) 알림 설정의 '보낼 곳'에서 고른 곳으로
     * - SOS: 설정되어 있는 '자동' 채널 전부(텔레그램, 자동 이메일·카톡 이메일) + 본인 폰 기록
     *        (전화는 화면에서 바로 걸기 때문에, 알림을 눌러야 하는 수동 채널은 제외)
     */
    private void sendEvent(final int kind, final String ssid, final boolean test) {
        final String busy = PREF_EVENT + kind;
        if (jobBusy.contains(busy)) {
            return;
        }
        jobBusy.add(busy);
        final boolean sos = kind == KIND_SOS;
        final SharedPreferences ev = getSharedPreferences(PREF_EVENT, MODE_PRIVATE);
        fetchLocation(loc -> new Thread(() -> {
            String who = TelegramSender.senderName(this);
            String whoPrefix = who.isEmpty() ? "" : who + " ";
            String time = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date());
            String head = (test ? "[테스트] " : "")
                    + (kind == KIND_HOME ? "🏠 " + whoPrefix + "귀가했습니다"
                    : kind == KIND_AWAY ? "🚶 " + whoPrefix + "외출했습니다"
                    : kind == KIND_LATE ? "⚠️ " + whoPrefix + "아직 귀가하지 않았습니다 ("
                    + ev.getString("lateTime", "22:00") + " 기준)"
                    : kind == KIND_START ? "📱 " + whoPrefix + "앱을 시작했습니다"
                    : kind == KIND_STOP ? "📴 " + whoPrefix + "[집에 왔어요] 앱이 종료됩니다"
                    // 백그라운드 기능이 하나라도 켜져 있으면 서비스는 계속 동작 -> 오해하지 않게 안내
                    + (anyOn(this) ? "\n(백그라운드 감지는 계속됩니다)" : "")
                    : "🆘 긴급 SOS — " + whoPrefix + "도움을 요청했습니다");
            // 첫 줄 = 지금 상태 (홈 · 공유기 이름 (MAC) / 외출), 그다음 알림 제목
            String status = TelegramSender.statusLine(this);
            final String full = status + "\n" + head + "  " + time
                    + "\n\n" + TelegramSender.message(this, loc, false)
                    + (kind == KIND_SOS ? UserActivity.medicalText(this) : ""); // SOS: 긴급 의료 정보 첨부
            final String shortMsg = status + "\n" + head + " " + time + "\n"
                    + TelegramSender.shortMessage(this, loc, false);
            String subject = (test ? "[테스트] " : "") + "[" + KIND_NAME[kind] + "] "
                    + (who.isEmpty() ? "" : who + " · ") + time;
            final List<String> res = new ArrayList<>();
            final List<Runnable> notes = new ArrayList<>(); // 알림 띄우기는 화면 스레드에서

            if (sos) {
                // SOS: 설정된 자동 채널 전부
                SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
                String tk = tg.getString("token", ""), ch = tg.getString("chats", "");
                if (!tk.isEmpty() && !ch.trim().isEmpty()) {
                    res.add("텔레그램 " + (TelegramSender.send(tk, ch, full) == null ? "✓" : "✗"));
                }
                for (String pref : new String[]{PREF_EMAIL}) {
                    SharedPreferences em = getSharedPreferences(pref, MODE_PRIVATE);
                    String to = em.getString("to", "");
                    if (!to.trim().isEmpty() && emailSmtp(em) && !em.getString("pass", "").isEmpty()) {
                        String err = EmailSender.send(em.getString("from", ""), em.getString("pass", ""), to, subject, full);
                        res.add(emailTitle(pref) + " " + (err == null ? "✓" : "✗"));
                    }
                }
                notes.add(() -> notifySelf(this, full, loc));
                if (res.isEmpty()) {
                    res.add("자동 전송 채널 없음 (전화만)");
                }
            } else if (ev.getBoolean("ch_tg", false)) {
                SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
                String tk = tg.getString("token", ""), ch = tg.getString("chats", "");
                if (tk.isEmpty() || ch.trim().isEmpty()) {
                    res.add("텔레그램 설정 없음");
                } else {
                    res.add("텔레그램 " + (TelegramSender.send(tk, ch, full) == null ? "✓" : "✗"));
                }
            }
            for (final String pref : new String[]{PREF_EMAIL}) {
                if (sos || !ev.getBoolean(PREF_EMAIL.equals(pref) ? "ch_email" : "ch_kmail", false)) {
                    continue;
                }
                final SharedPreferences em = getSharedPreferences(pref, MODE_PRIVATE);
                final String to = em.getString("to", "");
                String nm = emailTitle(pref);
                if (to.trim().isEmpty()) {
                    res.add(nm + " 설정 없음");
                } else if (emailSmtp(em)) {
                    String err = EmailSender.send(em.getString("from", ""), em.getString("pass", ""), to, subject, full);
                    res.add(nm + " " + (err == null ? "✓" : "✗"));
                } else {
                    notes.add(() -> notifyEmail(pref, to, full, loc));
                    res.add(nm + " 알림");
                }
            }
            if (!sos && ev.getBoolean("ch_kakao", false)) {
                notes.add(() -> notifyKakao(full, loc));
                res.add("카톡 알림");
            }
            // 일반 문자(자동, 솔라피): 고른 경우 + SOS 때는 설정만 되어 있으면 무조건
            SharedPreferences sa = getSharedPreferences(PREF_SMSAUTO, MODE_PRIVATE);
            boolean saReady = !sa.getString("apiKey", "").isEmpty() && !sa.getString("to", "").trim().isEmpty();
            if ((sos && saReady) || (!sos && ev.getBoolean("ch_smsauto", false))) {
                if (!saReady) {
                    res.add("문자(자동) 설정 없음");
                } else {
                    String se = SmsSender.send(sa.getString("apiKey", ""), sa.getString("apiSecret", ""),
                            sa.getString("from", ""), sa.getString("to", ""), shortMsg);
                    res.add("문자(자동) " + (se == null ? "✓" : "✗"));
                }
            }
            if (!sos && ev.getBoolean("ch_sms", false)) {
                final String to = getSharedPreferences(PREF_SMS, MODE_PRIVATE).getString("to", "");
                if (to.trim().isEmpty()) {
                    res.add("문자 설정 없음");
                } else {
                    notes.add(() -> notifySms(to, shortMsg, loc));
                    res.add("문자 알림");
                }
            }
            if (!sos && ev.getBoolean("ch_self", true)) {
                notes.add(() -> notifySelf(this, full, loc));
                res.add("본인 폰 ✓");
            }
            final String summary = KIND_NAME[kind] + (test ? "(테스트)" : "") + ": "
                    + (res.isEmpty() ? "보낼 곳이 선택되지 않음" : android.text.TextUtils.join(", ", res));
            AppLog.add(this, "보내기", summary + " · 위치 " + locText(loc));
            handler.post(() -> {
                for (Runnable note : notes) {
                    note.run();
                }
                ev.edit().putLong(sos ? "sosTime" : "lastTime", System.currentTimeMillis())
                        .putString(sos ? "sosResult" : "lastResult", summary).apply();
                jobBusy.remove(busy);
                updateStatus();
                releaseWakeIfIdle();
                if (!anyOn(this) && jobBusy.isEmpty()) {
                    stopSelf(); // SOS 만 보내려고 잠깐 켜진 경우: 다 보냈으면 종료
                }
            });
        }).start());
    }

    // ---------- 1-4) 원격 위치 요청: 보호자가 텔레그램 봇에게 '위치'를 보내면 자동 답장 ----------

    private volatile boolean pollRunning;
    private Thread pollThread;

    private void updateRemotePoll() {
        if (remoteOn(this)) {
            if (pollThread == null || !pollThread.isAlive()) {
                pollRunning = true;
                pollThread = new Thread(this::pollLoop, "tg-remote");
                pollThread.start();
            }
        } else {
            pollRunning = false;
            if (pollThread != null) {
                pollThread.interrupt();
            }
        }
    }

    private static void nap(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    // 작업 스레드: 새 메시지를 기다렸다가(최대 50초) 위치 요청이면 답장, 반복
    private void pollLoop() {
        final SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        long offset = tg.getLong("pollOffset", 0);
        while (pollRunning) {
            if (TelegramSetupActivity.active) { // 설정 도우미가 열려 있으면 잠시 쉼
                nap(5000);
                continue;
            }
            final String token = tg.getString("token", "");
            String[] err = new String[1];
            org.json.JSONArray arr = TelegramSender.poll(token, offset, 50, err);
            if (arr == null) {
                nap(30 * 1000); // 인터넷 끊김 등: 잠시 후 다시
                continue;
            }
            Set<String> allowed = new HashSet<>();
            for (String chat : tg.getString("chats", "").split("[,\\s]+")) {
                if (!chat.isEmpty()) {
                    allowed.add(chat);
                }
            }
            for (int i = 0; i < arr.length(); i++) {
                org.json.JSONObject update = arr.optJSONObject(i);
                if (update == null) {
                    continue;
                }
                offset = Math.max(offset, update.optLong("update_id") + 1);
                org.json.JSONObject msg = update.optJSONObject("message");
                if (msg == null || msg.optJSONObject("chat") == null
                        || !TelegramSender.isLocationCommand(msg.optString("text"))) {
                    continue;
                }
                final String cid = String.valueOf(msg.optJSONObject("chat").optLong("id"));
                AppLog.add(this, "요청", "텔레그램 위치 요청: 채팅 " + cid + " \"" + msg.optString("text") + "\""
                        + (allowed.contains(cid) ? "" : " - 등록 안 된 사람이라 거절"));
                if (!allowed.contains(cid)) {
                    TelegramSender.send(token, cid, "등록된 보호자만 위치를 요청할 수 있습니다.");
                    continue;
                }
                TelegramSender.send(token, cid, "📍 위치를 확인하고 있습니다... (최대 30초)");
                handler.post(() -> replyLocation(token, cid));
            }
            tg.edit().putLong("pollOffset", offset).apply();
        }
    }

    private void replyLocation(final String token, final String cid) {
        final String busy = "remote" + cid;
        if (jobBusy.contains(busy)) {
            return;
        }
        jobBusy.add(busy);
        fetchLocation(loc -> new Thread(() -> {
            String err = TelegramSender.send(token, cid, TelegramSender.statusLine(this)
                    + "\n📨 요청하신 위치입니다\n\n" + TelegramSender.message(this, loc, false)
                    + TelegramSender.btSection(this)); // 주변 블루투스 (약 6초 검색, [...] 메뉴에서 끄면 없음)
            AppLog.add(this, "요청", "텔레그램 답장 -> " + cid + " : " + (err == null ? "성공" : "실패: " + err) + " · 위치 " + locText(loc));
            handler.post(() -> {
                getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).edit()
                        .putLong("remoteTime", System.currentTimeMillis())
                        .putString("remoteResult", (err == null ? "답장 ✓" : "답장 실패: " + err)
                                + " (" + TelegramSender.nameOrId(this, cid) + ")")
                        .apply();
                jobBusy.remove(busy);
                releaseWakeIfIdle();
            });
        }).start());
    }

    // ---------- 1-5) 이메일 원격 위치 요청: 보호자가 사용자 Gmail 로 제목 "#where" 메일 -> 이메일로 회신 ----------

    private volatile boolean mailRunning;
    private Thread mailThread;
    private volatile MailWatcher mailWatcher;
    private volatile boolean mailRescanNow; // true 면 연결이 끊겨도 쉬지 않고 바로 다시 접속

    private void updateMailRemote() {
        if (mailRemoteOn(this)) {
            if (mailThread == null || !mailThread.isAlive()) {
                mailRunning = true;
                mailThread = new Thread(this::mailLoop, "mail-remote");
                mailThread.start();
            }
        } else {
            stopMailRemote();
        }
    }

    private void stopMailRemote() {
        mailRunning = false;
        MailWatcher watcher = mailWatcher;
        if (watcher != null) {
            watcher.stop(); // 대기 중인 연결을 바로 끊음
        }
        if (mailThread != null) {
            mailThread.interrupt();
        }
    }

    // 보호자 등록(1100)에 적힌 이메일 주소들 (소문자)
    static Set<String> guardianEmails(Context ctx) {
        Set<String> emails = new HashSet<>();
        for (String addr : SosActivity.prefs(ctx).getString("emails", "").split("[,;\\s]+")) {
            if (addr.contains("@")) {
                emails.add(addr.trim().toLowerCase(Locale.US));
            }
        }
        return emails;
    }

    // 작업 스레드: Gmail 에 접속해 새 메일을 기다림. 끊기면 1분 후 다시 접속
    private void mailLoop() {
        final SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        while (mailRunning) {
            MailWatcher watcher = new MailWatcher();
            mailWatcher = watcher;
            user.edit().putString("mailRemoteState", "Gmail 접속 중...").apply();
            String err = watcher.watch(user.getString("mail", ""), user.getString("mailPass", ""), user, new MailWatcher.Callback() {
                @Override
                public boolean running() {
                    return mailRunning;
                }

                @Override
                public boolean onRequest(String from, String subject) {
                    boolean known = guardianEmails(HomeService.this).contains(from);
                    AppLog.add(HomeService.this, "요청", "이메일 위치 요청: " + from + " \"" + subject + "\" - "
                            + (!known ? "등록 안 된 주소라 무시"
                            : user.getBoolean("mailRemote", false) ? "자동 회신" : "회신할지 물어봄"));
                    if (!known) {
                        // 등록되지 않은 주소: 답장하지 않음 (메일도 읽지 않은 채로 둠)
                        user.edit().putLong("mailRemoteTime", System.currentTimeMillis())
                                .putString("mailRemoteResult", "등록 안 된 주소라 무시: " + from).apply();
                        return false;
                    }
                    if (user.getBoolean("mailRemote", false)) {
                        handler.post(() -> replyMailLocation(from)); // 자동 회신
                    } else {
                        handler.post(() -> askMailReply(from)); // 물어보고 회신 (알림 Yes/No)
                    }
                    return true;
                }

                @Override
                public void state(String msg) {
                    // 같은 상태가 4분마다 반복되므로 바뀔 때만 로그
                    if (!msg.equals(user.getString("mailRemoteState", ""))) {
                        AppLog.add(HomeService.this, "메일", "감시 상태: " + msg);
                    }
                    user.edit().putString("mailRemoteState", msg).putString("mailRemoteErr", "").apply();
                }
            });
            mailWatcher = null;
            AppLog.add(this, "메일", "감시 연결 끝남" + (err == null ? "" : " - 오류: " + err));
            user.edit().putString("mailRemoteErr", err == null ? "" : err)
                    .putString("mailRemoteState", mailRunning ? "연결 끊김 - 잠시 후 다시 접속" : "꺼짐").apply();
            if (mailRescanNow) {
                mailRescanNow = false; // 앱 시작: 바로 다시 접속해서 받은편지함 다시 찾기
                continue;
            }
            if (mailRunning) {
                nap(err != null && err.startsWith("로그인") ? 10 * 60 * 1000 : 60 * 1000);
            }
        }
    }

    static int mailAskId(String from) {
        return 7000 + (from.toLowerCase(Locale.US).hashCode() & 0xFFF);
    }

    static String mailAskText(Context ctx, String from) {
        return "보호자 " + guardianName(ctx, from) + "님으로부터 위치 질문을 받았습니다. 회신할까요?";
    }

    // 자동 회신이 꺼져 있을 때: "보호자 ○○님으로부터 위치 질문을 받았습니다. 회신할까요?" 알림 + [Yes] [No]
    // 알림을 누르면 앱 화면에서 같은 질문 팝업(1057)
    private void askMailReply(String from) {
        int id = mailAskId(from);
        int flags = PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT;
        Intent yes = new Intent(this, HomeService.class).setAction(ACTION_MAIL_YES).putExtra("to", from);
        Intent no = new Intent(this, HomeService.class).setAction(ACTION_MAIL_NO).putExtra("to", from);
        PendingIntent pYes = Build.VERSION.SDK_INT >= 26
                ? PendingIntent.getForegroundService(this, id * 2, yes, flags) : PendingIntent.getService(this, id * 2, yes, flags);
        PendingIntent pNo = PendingIntent.getService(this, id * 2 + 1, no, flags);
        Intent open = new Intent(this, MainActivity.class).putExtra("mailAsk", from)
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pOpen = PendingIntent.getActivity(this, id, open, flags);
        String text = mailAskText(this, from);
        Notification.Builder notif = builder(CH_ALERT)
                .setContentTitle("📨 위치 질문")
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setContentIntent(pOpen)
                .setAutoCancel(true)
                .addAction(new Notification.Action.Builder(0, "Yes (회신)", pYes).build())
                .addAction(new Notification.Action.Builder(0, "No", pNo).build());
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        getSystemService(NotificationManager.class).notify(id, notif.build());
        getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE).edit()
                .putLong("mailRemoteTime", System.currentTimeMillis())
                .putString("mailRemoteResult", "회신할지 묻는 중 (" + guardianName(this, from) + ")").apply();
    }

    // 위치를 재서 요청한 보호자에게 이메일로 회신
    private void replyMailLocation(final String to) {
        final String busy = "mailRemote" + to;
        if (jobBusy.contains(busy)) {
            return;
        }
        jobBusy.add(busy);
        fetchLocation(loc -> new Thread(() -> {
            SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
            String err = EmailSender.send(user.getString("mail", ""), user.getString("mailPass", ""), to,
                    emailSubject(this) + " - 요청하신 위치",
                    // 공유기 식별 정보(이름·별명·MAC·신호·채널·IP·주변 공유기)는 message() 끝에 들어 있음
                    TelegramSender.statusLine(this) + "\n📨 요청하신 위치입니다\n\n" + TelegramSender.message(this, loc, false)
                            + TelegramSender.btSection(this)); // 주변 블루투스 (약 6초 검색, [...] 메뉴에서 끄면 없음)
            AppLog.add(this, "요청", "이메일 회신 -> " + to + " : " + (err == null ? "성공" : "실패: " + err) + " · 위치 " + locText(loc));
            handler.post(() -> {
                user.edit().putLong("mailRemoteTime", System.currentTimeMillis())
                        .putString("mailRemoteResult", (err == null ? "회신 ✓" : "회신 실패: " + err) + " (" + to + ")")
                        .apply();
                jobBusy.remove(busy);
                releaseWakeIfIdle();
            });
        }).start());
    }

    // ---------- 1-3) 미귀가 알림: 정해진 시각에 집이 아니면 알림 ----------

    private void scheduleLate(boolean tick) {
        SharedPreferences ev = getSharedPreferences(PREF_EVENT, MODE_PRIVATE);
        if (!ev.getBoolean("late", false)) {
            cancelAlarm(this, ACTION_LATE);
            return;
        }
        if (tick) {
            // 외출 중(집이 아님이 확실)일 때만 알림. 모르면 보내지 않음 (잘못된 '미귀가' 방지)
            if (getSharedPreferences("home", MODE_PRIVATE).getInt("bgState", -1) == 0) {
                sendEvent(KIND_LATE, null, false);
            }
        }
        long nx = nextTime(ev.getString("lateTime", "22:00"), System.currentTimeMillis());
        if (nx > 0) {
            setAlarm(ACTION_LATE, nx);
        }
    }

    // ---------- 외출 중에만 보내기 ((3) 위치 보내기 탭 옵션) ----------

    static boolean awayOnly(Context ctx) {
        return ctx.getSharedPreferences(TelegramSender.PREF_APP, MODE_PRIVATE).getBoolean("awayOnly", false);
    }

    // 지금 '외출 확정(2분 넘게 밖)' 상태가 아니면 true. 집인지 알 수 없으면 false (안전하게 보냄)
    private boolean atHomeForSending() {
        if (!awayOnly(this)) {
            return false;
        }
        SharedPreferences homePrefs = getSharedPreferences("home", MODE_PRIVATE);
        boolean registered = MainActivity.hasHomeRouter(this) // '홈'으로 체크된 공유기만 집
                || BtHome.hasHome(this); // '홈'으로 체크된 블루투스 기기만 집
        if (!registered) {
            return false; // 집이 등록되지 않음 -> 옵션 무시하고 보냄
        }
        int st = homePrefs.getInt("bgState", -1);
        if (st == 1) {
            return true; // 집
        }
        if (st == 0) {
            return !homePrefs.getBoolean("awaySent", false); // 나간 지 2분이 안 됨 -> 아직 집 근처로 봄
        }
        return false; // 모름 -> 보냄
    }

    // 집에 있으면 이번 전송은 건너뛰고 다음 예약만 해 둠
    private boolean skipAtHome(SharedPreferences sp, String action, long nx) {
        if (!atHomeForSending()) {
            return false;
        }
        AppLog.add(this, "보내기", "집에 있어서 건너뜀 (외출 중에만 보내기): "
                + action.replace("com.garpsu.wifiscan.", "") + " · 다음 " + nextText(nx));
        sp.edit().putLong("next", nx).apply();
        if (nx > 0) {
            setAlarm(action, nx);
        }
        return true;
    }

    // 외출이 확정된 순간: '일정 간격' 전송들은 바로 한 번 보내고 그때부터 간격대로
    private void startSendingOnLeave() {
        tgFollowSched(this); // 텔레그램도 공통 시각을 따름
        for (String prefName : new String[]{TelegramSender.PREF, PREF_EMAIL, PREF_KMAIL, PREF_KAKAO, PREF_SMS,
                PREF_SMSAUTO, PREF_SELF}) {
            SharedPreferences sp = getSharedPreferences(prefName, MODE_PRIVATE);
            if ("interval".equals(sp.getString("mode", ""))) {
                sp.edit().putLong("next", 0).apply(); // 지정 시각 방식은 원래 시각대로
            }
        }
        scheduleTelegram(false);
        scheduleKakao(false);
        scheduleEmail(PREF_EMAIL, false);
        scheduleNotifyJob(PREF_SMS, ACTION_SMS, false);
        scheduleNotifyJob(PREF_SMSAUTO, ACTION_SMSAUTO, false);
        scheduleNotifyJob(PREF_SELF, ACTION_SELF, false);
    }

    // ---------- 2) 텔레그램 자동 송신 ----------

    // 보낼 때가 되었으면 보내고, 아니면 다음 송신 알람만 예약
    // 보내는 시각은 1040 공통 설정 (지정 시각 / 일정 간격) - 예전의 텔레그램 자체 간격은 쓰지 않음
    private void scheduleTelegram(boolean tick) {
        SharedPreferences sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        if (!sp.getBoolean("on", false)) {
            cancelAlarm(this, ACTION_TG);
            return;
        }
        tgFollowSched(this);
        long next = sp.getLong("next", 0);
        if (!tick && System.currentTimeMillis() < next - 30 * 1000) {
            setAlarm(ACTION_TG, next);
            return;
        }
        if (skipAtHome(sp, ACTION_TG, emailNext(sp, System.currentTimeMillis()))) {
            return;
        }
        if (tgBusy) {
            return;
        }
        tgBusy = true;
        updateStatus();
        fetchLocation(loc -> {
            final String token = sp.getString("token", "");
            final String chats = sp.getString("chats", "");
            new Thread(() -> {
                String err = TelegramSender.send(token, chats, TelegramSender.message(this, loc));
                handler.post(() -> {
                    long now = System.currentTimeMillis();
                    long nx = emailNext(sp, now); // 공통 시각: 다음 지정 시각 또는 간격 뒤
                    AppLog.add(this, "보내기", "정해진 시각 텔레그램 : " + (err == null ? "성공" : "실패: " + err) + " · 위치 " + locText(loc) + " · 다음 " + nextText(nx));
                    sp.edit().putLong("lastTime", now)
                            .putString("lastResult", err == null ? "성공" : "실패: " + err)
                            .putLong("next", nx)
                            .apply();
                    tgBusy = false;
                    if (tgOn(this)) {
                        setAlarm(ACTION_TG, nx);
                    }
                    updateStatus();
                    releaseWakeIfIdle();
                });
            }).start();
        });
    }

    // ---------- 3) 카톡 자동 보내기 (알림 -> 카톡 공유 화면) ----------

    private void scheduleKakao(boolean tick) {
        SharedPreferences sp = getSharedPreferences(PREF_KAKAO, MODE_PRIVATE);
        if (!sp.getBoolean("on", false)) {
            cancelAlarm(this, ACTION_KAKAO);
            return;
        }
        long next = sp.getLong("next", 0);
        if (!tick && System.currentTimeMillis() < next - 30 * 1000) {
            setAlarm(ACTION_KAKAO, next);
            return;
        }
        if (skipAtHome(sp, ACTION_KAKAO, emailNext(sp, System.currentTimeMillis()))) {
            return;
        }
        if (kakaoBusy) {
            return;
        }
        kakaoBusy = true;
        fetchLocation(loc -> new Thread(() -> {
            String msg = TelegramSender.message(this, loc);
            handler.post(() -> {
                long now = System.currentTimeMillis();
                long nx = emailNext(sp, now);
                AppLog.add(this, "보내기", "정해진 시각 카톡 : " + (kakaoOn(this) ? "알림 띄움" : "꺼져 있어 안 보냄") + " · 위치 " + locText(loc) + " · 다음 " + nextText(nx));
                sp.edit().putLong("lastTime", now).putLong("next", nx).apply();
                kakaoBusy = false;
                if (kakaoOn(this)) {
                    notifyKakao(msg, loc);
                    setAlarm(ACTION_KAKAO, nx);
                }
                updateStatus();
                releaseWakeIfIdle();
            });
        }).start());
    }

    private void notifyKakao(String msg, Location loc) {
        PendingIntent pi = PendingIntent.getActivity(this, 2, kakaoShare(msg),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String time = new SimpleDateFormat("HH:mm", Locale.KOREA).format(new Date());
        Notification.Builder notif = builder(CH_KAKAO)
                .setContentTitle("카톡으로 위치 보내기 (" + time + ")")
                .setContentText(loc == null ? "위치를 확인하지 못했습니다. 눌러서 카톡 열기"
                        : "눌러서 받는 사람을 고르고 [전송]하세요")
                .setStyle(new Notification.BigTextStyle().bigText(msg))
                .setContentIntent(pi)
                .setColor(0xFFFEE500)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT >= 20) {
            notif.addAction(new Notification.Action.Builder(0, "카톡으로 보내기", pi).build());
        }
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        getSystemService(NotificationManager.class).notify(ID_KAKAO, notif.build());
    }

    // ---------- 4) 이메일 자동 송신 (알림 -> Gmail 작성 화면) ----------

    // pref: PREF_EMAIL(일반 이메일) 또는 PREF_KMAIL(카톡 이메일)
    private void scheduleEmail(final String pref, boolean tick) {
        final String action = emailAction(pref);
        SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        if (!sp.getBoolean("on", false)) {
            cancelAlarm(this, action);
            return;
        }
        long next = sp.getLong("next", 0);
        if (!tick && System.currentTimeMillis() < next - 30 * 1000) {
            setAlarm(action, next);
            return;
        }
        if (skipAtHome(sp, action, emailNext(sp, System.currentTimeMillis()))) {
            return;
        }
        if (jobBusy.contains(pref)) {
            return;
        }
        jobBusy.add(pref);
        final boolean smtp = emailSmtp(sp);
        updateStatus();
        fetchLocation(loc -> new Thread(() -> {
            String msg = TelegramSender.message(this, loc);
            // 자동 발송: 앱 비밀번호로 바로 전송 / 작성 화면: 알림만 띄움
            final String err = smtp ? EmailSender.send(sp.getString("from", ""), sp.getString("pass", ""),
                    sp.getString("to", ""), emailSubject(this), msg) : null;
            handler.post(() -> {
                long now = System.currentTimeMillis();
                long nx = emailNext(sp, now);
                AppLog.add(this, "보내기", "정해진 시각 이메일 : " + (smtp ? (err == null ? "성공" : "실패: " + err) : "알림 띄움")
                        + " · 위치 " + locText(loc) + " · 다음 " + nextText(nx));
                SharedPreferences.Editor ed = sp.edit().putLong("lastTime", now).putLong("next", nx);
                if (smtp) {
                    ed.putString("lastResult", err == null ? "성공" : "실패: " + err);
                }
                ed.apply();
                jobBusy.remove(pref);
                if (emailOn(this, pref)) {
                    if (!smtp) {
                        notifyEmail(pref, sp.getString("to", ""), msg, loc);
                    }
                    if (nx > 0) {
                        setAlarm(action, nx);
                    }
                }
                updateStatus();
                releaseWakeIfIdle();
            });
        }).start());
    }

    private void notifyEmail(String pref, String to, String msg, Location loc) {
        boolean kakaoMail = PREF_KMAIL.equals(pref);
        PendingIntent pi = PendingIntent.getActivity(this, kakaoMail ? 4 : 3, emailCompose(this, to, emailSubject(this), msg),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String time = new SimpleDateFormat("HH:mm", Locale.KOREA).format(new Date());
        Notification.Builder notif = builder(CH_EMAIL)
                .setContentTitle(emailTitle(pref) + "로 위치 보내기 (" + time + ")")
                .setContentText(loc == null ? "위치를 확인하지 못했습니다. 눌러서 메일 쓰기"
                        : "받는 사람: " + to + " · 눌러서 [보내기]")
                .setStyle(new Notification.BigTextStyle().bigText("받는 사람: " + to + "\n\n" + msg))
                .setContentIntent(pi)
                .setColor(0xFFD93025)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT >= 20) {
            notif.addAction(new Notification.Action.Builder(0, "메일 쓰기", pi).build());
        }
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        getSystemService(NotificationManager.class).notify(kakaoMail ? ID_KMAIL : ID_EMAIL, notif.build());
    }

    // ---------- 5) 일반 문자(수동) / 본인 폰 알림 (정해진 시각·간격마다 위치 측정 후 알림) ----------

    private void scheduleNotifyJob(final String pref, final String action, boolean tick) {
        final SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        if (!sp.getBoolean("on", false)) {
            cancelAlarm(this, action);
            return;
        }
        long next = sp.getLong("next", 0);
        if (!tick && System.currentTimeMillis() < next - 30 * 1000) {
            setAlarm(action, next);
            return;
        }
        if (skipAtHome(sp, action, emailNext(sp, System.currentTimeMillis()))) {
            return;
        }
        if (jobBusy.contains(pref)) {
            return;
        }
        jobBusy.add(pref);
        final boolean auto = PREF_SMSAUTO.equals(pref); // 일반 문자(자동): 솔라피로 바로 발송
        final boolean sms = PREF_SMS.equals(pref) || auto;
        fetchLocation(loc -> new Thread(() -> {
            // 문자: 짧게(기본) 또는 전체 / 본인 폰 알림: 전체
            String msg = sms && sp.getBoolean("short", true)
                    ? TelegramSender.shortMessage(this, loc) : TelegramSender.message(this, loc);
            final String err = auto ? SmsSender.send(sp.getString("apiKey", ""), sp.getString("apiSecret", ""),
                    sp.getString("from", ""), sp.getString("to", ""), msg) : null;
            handler.post(() -> {
                long now = System.currentTimeMillis();
                long nx = emailNext(sp, now); // 지정 시각 / 일정 간격 (이메일과 같은 설정 방식)
                AppLog.add(this, "보내기", "정해진 시각 " + (auto ? "문자(자동)" : PREF_SELF.equals(pref) ? "본인 알림" : "문자(수동) 알림")
                        + " : " + (auto ? (err == null ? "성공" : "실패: " + err) : "알림 띄움")
                        + " · 위치 " + locText(loc) + " · 다음 " + nextText(nx));
                SharedPreferences.Editor ed = sp.edit().putLong("lastTime", now).putLong("next", nx);
                if (auto) {
                    ed.putString("lastResult", err == null ? "성공" : "실패: " + err);
                }
                ed.apply();
                jobBusy.remove(pref);
                if (emailOn(this, pref)) {
                    if (auto) {
                        // 이미 보냄 (알림 없음)
                    } else if (sms) {
                        notifySms(sp.getString("to", ""), msg, loc);
                    } else {
                        notifySelf(this, msg, loc);
                    }
                    if (nx > 0) {
                        setAlarm(action, nx);
                    }
                }
                updateStatus();
                releaseWakeIfIdle();
            });
        }).start());
    }

    private void notifySms(String to, String msg, Location loc) {
        PendingIntent pi = PendingIntent.getActivity(this, 5, smsCompose(to, msg),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String time = new SimpleDateFormat("HH:mm", Locale.KOREA).format(new Date());
        Notification.Builder notif = builder(CH_EMAIL)
                .setContentTitle("문자로 위치 보내기 (" + time + ")")
                .setContentText(loc == null ? "위치를 확인하지 못했습니다. 눌러서 문자 쓰기"
                        : "받는 사람: " + to + " · 눌러서 [전송]")
                .setStyle(new Notification.BigTextStyle().bigText("받는 사람: " + to + "\n\n" + msg))
                .setContentIntent(pi)
                .setColor(0xFF2E7D32)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT >= 20) {
            notif.addAction(new Notification.Action.Builder(0, "문자 쓰기", pi).build());
        }
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        getSystemService(NotificationManager.class).notify(ID_SMS, notif.build());
    }

    // ---------- 위치 측정 (텔레그램/카톡/이메일/문자/본인 알림 공용) ----------

    interface LocCallback {
        void done(Location loc); // 측정 실패 시 마지막으로 알려진 위치, 그것도 없으면 null
    }

    private final List<LocCallback> waiting = new ArrayList<>();
    private Location best;
    private PowerManager.WakeLock wake;

    private long fixStart; // 이번 측정을 시작한 시각

    // 정확도 기준: 20 m 이내면 바로 사용, 10초가 지나면 50 m 이내도 사용, 30초가 되면 그때까지 가장 정확한 값
    private static final float GOOD_M = 20, OK_M = 50;
    private static final long OK_AFTER = 10 * 1000;

    private final LocationListener fixListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location loc) {
            if (loc == null || System.currentTimeMillis() - loc.getTime() > 2 * 60 * 1000) {
                return; // 2분 넘은 옛 위치(캐시)는 버림
            }
            if (best == null || !best.hasAccuracy() || (loc.hasAccuracy() && loc.getAccuracy() < best.getAccuracy())) {
                best = loc;
            }
            float acc = loc.hasAccuracy() ? loc.getAccuracy() : Float.MAX_VALUE;
            if (acc <= GOOD_M || (System.currentTimeMillis() - fixStart >= OK_AFTER && acc <= OK_M)) {
                finishFix(); // 충분히 정확하면 바로 사용
            }
        }

        @Override
        public void onStatusChanged(String provider, int status, Bundle extras) {
        }

        @Override
        public void onProviderEnabled(String provider) {
        }

        @Override
        public void onProviderDisabled(String provider) {
        }
    };

    private final Runnable fixTimeout = this::finishFix;

    // 새 위치를 측정 (최대 30초). 측정 중이면 같은 결과를 함께 받음
    private void fetchLocation(LocCallback cb) {
        if (wake == null) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "wifiscan:send");
            wake.acquire(FIX_TIMEOUT + 90 * 1000);
        }
        waiting.add(cb);
        if (waiting.size() > 1) {
            return; // 이미 측정 중
        }
        best = null;
        fixStart = System.currentTimeMillis();

        // 0) 1분 이내에 잰 위치가 '정확도 20 m 이내'이면 그대로 사용 (예전엔 정확도와 상관없이 써서 오차가 컸음)
        try {
            for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                Location last = lm.getLastKnownLocation(provider);
                if (last != null && System.currentTimeMillis() - last.getTime() <= 60 * 1000
                        && last.hasAccuracy() && last.getAccuracy() <= GOOD_M) {
                    best = last;
                }
            }
        } catch (SecurityException ignored) {
        }
        if (best != null) {
            handler.post(this::finishFix);
            return;
        }

        // 1) 구글 통합 위치 (Play 서비스): GPS + Wi-Fi + 기지국 + 센서 -> 실내에서도 정확
        boolean any = startFused();
        // 2) Play 서비스가 없는 폰: 안드로이드 기본 GPS·통신망 위치
        if (!any) {
            try {
                for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    if (lm.isProviderEnabled(provider)) {
                        lm.requestLocationUpdates(provider, 1000, 0, fixListener, Looper.getMainLooper());
                        any = true;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        if (!any) {
            handler.post(this::finishFix);
        } else {
            handler.postDelayed(fixTimeout, FIX_TIMEOUT);
        }
    }

    private com.google.android.gms.location.FusedLocationProviderClient fused;
    private com.google.android.gms.location.LocationCallback fusedCb;

    // 구글 통합 위치 측정 시작 (1초마다, 가장 정확하게). Play 서비스가 없거나 실패하면 false
    private boolean startFused() {
        try {
            if (com.google.android.gms.common.GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(this)
                    != com.google.android.gms.common.ConnectionResult.SUCCESS) {
                return false;
            }
            if (fused == null) {
                fused = com.google.android.gms.location.LocationServices.getFusedLocationProviderClient(this);
            }
            com.google.android.gms.location.LocationRequest req =
                    new com.google.android.gms.location.LocationRequest.Builder(
                            com.google.android.gms.location.Priority.PRIORITY_HIGH_ACCURACY, 1000)
                            .setMinUpdateIntervalMillis(500)
                            .setWaitForAccurateLocation(true)
                            .build();
            fusedCb = new com.google.android.gms.location.LocationCallback() {
                @Override
                public void onLocationResult(com.google.android.gms.location.LocationResult result) {
                    for (Location loc : result.getLocations()) {
                        fixListener.onLocationChanged(loc);
                    }
                }
            };
            fused.requestLocationUpdates(req, fusedCb, Looper.getMainLooper());
            return true;
        } catch (SecurityException e) {
            return false;
        } catch (Throwable e) {
            fusedCb = null;
            return false;
        }
    }

    private void finishFix() {
        if (waiting.isEmpty()) {
            return;
        }
        handler.removeCallbacks(fixTimeout);
        lm.removeUpdates(fixListener);
        if (fused != null && fusedCb != null) {
            fused.removeLocationUpdates(fusedCb);
            fusedCb = null;
        }
        Location fix = best;
        if (fix == null) { // 새 측정 실패 -> 마지막으로 알려진 위치라도 사용
            try {
                for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    Location last = lm.getLastKnownLocation(provider);
                    if (last != null && (fix == null || last.getTime() > fix.getTime())) {
                        fix = last;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        AppLog.add(this, "위치", (best != null ? "측정 완료: " : "새 측정 실패 -> 마지막 위치: ") + locText(fix) + " (요청 " + waiting.size() + "건)");
        List<LocCallback> cbs = new ArrayList<>(waiting);
        waiting.clear();
        for (LocCallback cb : cbs) {
            cb.done(fix);
        }
    }

    private void releaseWakeIfIdle() {
        if (!tgBusy && !kakaoBusy && jobBusy.isEmpty() && waiting.isEmpty() && wake != null) {
            if (wake.isHeld()) {
                wake.release();
            }
            wake = null;
        }
    }

    // ---------- 알람 (화면이 꺼진 절전 상태에서도 깨움) ----------

    private static PendingIntent alarmIntent(Context ctx, String action) {
        Intent intent = new Intent(ctx, HomeService.class).setAction(action);
        int req = ACTION_TG.equals(action) ? 1 : ACTION_KAKAO.equals(action) ? 2
                : ACTION_KMAIL.equals(action) ? 4 : ACTION_SMS.equals(action) ? 5 : ACTION_SMSAUTO.equals(action) ? 9
                : ACTION_SELF.equals(action) ? 6 : ACTION_CHECK.equals(action) ? 7
                : ACTION_LATE.equals(action) ? 8 : 3;
        return PendingIntent.getService(ctx, req, intent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private void setAlarm(String action, long at) {
        AlarmManager am = (AlarmManager) getSystemService(Context.ALARM_SERVICE);
        PendingIntent pi = alarmIntent(this, action);
        if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms()) {
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
        } else {
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
        }
    }

    private static void cancelAlarm(Context ctx, String action) {
        ((AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE)).cancel(alarmIntent(ctx, action));
    }

    // ---------- 공용 도우미 ----------

    // 지금 연결된 공유기의 BSSID (소문자, 없거나 알 수 없으면 null)
    static String connectedBssid(Context ctx) {
        WifiManager wm = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm == null || !wm.isWifiEnabled()) {
            return null;
        }
        WifiInfo wi = wm.getConnectionInfo();
        String bssid = wi == null ? null : wi.getBSSID();
        if (bssid == null || bssid.equals("02:00:00:00:00:00")) {
            return null; // 권한/위치 서비스가 없으면 가짜 값이 옴
        }
        return bssid.toLowerCase(Locale.US);
    }

    static boolean locationOn(Context ctx) {
        LocationManager lm = (LocationManager) ctx.getSystemService(Context.LOCATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 28) {
            return lm.isLocationEnabled();
        }
        return lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
    }

    // ---------- 알림 ----------

    private void createChannels() {
        if (Build.VERSION.SDK_INT < 26) {
            return;
        }
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(
                CH_STATUS, "백그라운드 상태", NotificationManager.IMPORTANCE_LOW));
        nm.createNotificationChannel(new NotificationChannel(
                CH_ALERT, "귀가 알림", NotificationManager.IMPORTANCE_HIGH));
        nm.createNotificationChannel(new NotificationChannel(
                CH_KAKAO, "카톡 보내기 알림", NotificationManager.IMPORTANCE_HIGH));
        nm.createNotificationChannel(new NotificationChannel(
                CH_EMAIL, "이메일 보내기 알림", NotificationManager.IMPORTANCE_HIGH));
    }

    private void updateStatus() {
        boolean home = homeOn(this) || eventOn(this); // 귀가 감지 또는 귀가·외출 알림 중이면 상태 표시
        StringBuilder text = new StringBuilder(home ? homeNote : "");
        String title = home ? homeTitle : "위치 자동 보내기 중";
        SharedPreferences evp = getSharedPreferences(PREF_EVENT, MODE_PRIVATE);
        if (eventOn(this)) {
            if (text.length() > 0) {
                text.append(" · ");
            }
            text.append(evp.getBoolean("home", false) && evp.getBoolean("away", false) ? "귀가·외출 알림"
                    : evp.getBoolean("home", false) ? "귀가 알림" : "외출 알림");
        }

        SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        if (tg.getBoolean("on", false)) {
            if (text.length() > 0) {
                text.append(" · ");
            }
            text.append("텔레그램 ").append(emailSchedule(tg)); // "매일 08:00, 18:00" 또는 "5분마다"
            long last = tg.getLong("lastTime", 0);
            if (tgBusy) {
                text.append(" (보내는 중...)");
            } else if (last > 0) {
                text.append(" (마지막 ")
                        .append(new SimpleDateFormat("HH:mm", Locale.KOREA).format(new Date(last)))
                        .append(tg.getString("lastResult", "").startsWith("성공") ? " ✓)" : " ✗)");
            }
        }
        SharedPreferences kk = getSharedPreferences(PREF_KAKAO, MODE_PRIVATE);
        if (kk.getBoolean("on", false)) {
            if (text.length() > 0) {
                text.append(" · ");
            }
            text.append("카톡 ").append(emailSchedule(kk))
                    .append(" 알림");
        }
        for (String pref : new String[]{PREF_EMAIL}) {
            SharedPreferences em = getSharedPreferences(pref, MODE_PRIVATE);
            if (!em.getBoolean("on", false)) {
                continue;
            }
            if (text.length() > 0) {
                text.append(" · ");
            }
            text.append(emailTitle(pref)).append(" ").append(emailSchedule(em));
            if (!emailSmtp(em)) {
                text.append(" 알림");
            } else if (jobBusy.contains(pref)) {
                text.append(" 자동 (보내는 중...)");
            } else {
                long last = em.getLong("lastTime", 0);
                text.append(" 자동");
                if (last > 0) {
                    text.append(" (마지막 ")
                            .append(new SimpleDateFormat("HH:mm", Locale.KOREA).format(new Date(last)))
                            .append(em.getString("lastResult", "").startsWith("성공") ? " ✓)" : " ✗)");
                }
            }
        }
        // 문자 / 본인 폰 알림
        String[][] jobs = {{PREF_SMS, "문자"}, {PREF_SMSAUTO, "문자(자동)"}, {PREF_SELF, "본인 알림"}};
        for (String[] job : jobs) {
            SharedPreferences prefs = getSharedPreferences(job[0], MODE_PRIVATE);
            if (prefs.getBoolean("on", false)) {
                if (text.length() > 0) {
                    text.append(" · ");
                }
                text.append(job[1]).append(" ").append(emailSchedule(prefs));
            }
        }
        if (awayOnly(this)) {
            text.append(atHomeForSending() ? " · 외출 중에만 보내기 (지금은 집이라 대기)" : " · 외출 중에만 보내기");
        }
        getSystemService(NotificationManager.class)
                .notify(ID_STATUS, statusNotification(title, text.toString()));
    }

    private Notification.Builder builder(String ch) {
        Notification.Builder notif = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, ch) : new Notification.Builder(this);
        Intent open = new Intent(this, MainActivity.class)
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        notif.setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentIntent(PendingIntent.getActivity(this, 0, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        return notif;
    }

    private Notification statusNotification(String title, String text) {
        return builder(CH_STATUS)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(new Notification.BigTextStyle().bigText(text))
                .setColor(title.equals("홈") ? 0xFF2E7D32
                        : title.equals("외출") ? 0xFFC62828 : 0xFF1976D2)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .build();
    }

    private void alertHomeBack(String ssid) {
        Notification.Builder notif = builder(CH_ALERT)
                .setContentTitle("홈")
                .setContentText("집에 도착했습니다" + (ssid == null || ssid.isEmpty() ? "" : " (" + ssid + ")"))
                .setColor(0xFF2E7D32)
                .setAutoCancel(true);
        if (Build.VERSION.SDK_INT < 26) {
            notif.setPriority(Notification.PRIORITY_HIGH).setDefaults(Notification.DEFAULT_ALL);
        }
        getSystemService(NotificationManager.class).notify(ID_ALERT, notif.build());
    }
}
