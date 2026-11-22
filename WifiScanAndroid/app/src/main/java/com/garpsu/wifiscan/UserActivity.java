package com.garpsu.wifiscan;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.util.Patterns;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.text.SimpleDateFormat;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * 화면 1075: 사용자 등록 (1010 [사용자 등록]에서 열림)
 * - 기본 정보: 이름(별명) / 내 번호 / 기기 이름
 * - 발신 이메일: 주소 + 앱 비밀번호, 인증번호 메일로 검증
 * - 텔레그램: 봇 토큰 확인, 설정 도우미
 * - Tailscale: 이 폰의 Tailscale IP(자동), PC 서버 IP:포트, 접속 키, 연결 테스트
 * - 긴급 의료 정보: SOS 메시지에 첨부
 * - 설정 백업·복원: 모든 설정을 파일로 내보내기 / 불러오기
 * 여기서 저장한 값은 기존 기능에서 그대로 씀 (별명, 이메일 1150·1160, 텔레그램 1180).
 */
public class UserActivity extends Activity {

    static final String PREF_USER = "user";
    private static final int REQ_EXPORT = 11, REQ_IMPORT = 12;
    private static final String TS_PKG = "com.tailscale.ipn";

    // 이 앱이 쓰는 설정 저장소 전부 (백업·복원, 초기화 공용)
    static final String[] ALL_PREFS = {"home", "lan", TelegramSender.PREF, TelegramSender.PREF_APP,
            HomeService.PREF_KAKAO, HomeService.PREF_EMAIL, HomeService.PREF_KMAIL,
            HomeService.PREF_SMS, HomeService.PREF_SMSAUTO, HomeService.PREF_SELF, HomeService.PREF_EVENT, HomeService.PREF_SCHED,
            "ward",
            SosActivity.PREF_SOS, PREF_USER};

    private SharedPreferences up;
    private EditText name, phone, device, mail, pass, code, token, pcIp, pcPort, pcKey, blood, disease, meds, memo;
    private static final String mailDomain = "gmail.com"; // 발신 이메일은 구글(Gmail)만 (@ 뒤 고정)
    private TextView mailStatus, tgStatus, tsStatus, pcStatus;
    private Button sendBtn; // [인증 메일 보내기]: 인증되면 비활성, 아니면 빨간 글씨 + (필수)
    private LinearLayout afterSend; // [인증 메일 보내기] 아래 항목들 (인증되면 숨김)
    private Button changeBtn; // 제목 줄 오른쪽 [변경] (인증 완료 시)
    private CheckBox medSos;
    private String pendingCode;   // 보낸 인증번호
    private long codeTime;        // 보낸 시각 (10분 유효)
    private String pendingImport; // 불러온 백업 파일 내용 (확인 후 적용)

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        up = getSharedPreferences(PREF_USER, MODE_PRIVATE);
        SharedPreferences em = getSharedPreferences(HomeService.PREF_EMAIL, MODE_PRIVATE);
        SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(40, 30, 40, 60);

        TextView title = new TextView(this);
        title.setText("사용자 등록");
        title.setTextSize(22);
        title.setTypeface(null, Typeface.BOLD);
        root.addView(title);
        // (화면 1075: 설명문 없이 입력 칸만)

        // ---------- 1) 기본 정보 ----------
        section(root, "■ 기본 정보");
        name = edit(root, "이름(별명)", TelegramSender.senderName(this), InputType.TYPE_CLASS_TEXT);
        phone = edit(root, "내 휴대폰 번호", up.getString("phone", ""), InputType.TYPE_CLASS_PHONE);
        device = edit(root, "기기 이름",
                up.getString("device", Build.MANUFACTURER + " " + Build.MODEL), InputType.TYPE_CLASS_TEXT);

        // ---------- 2) 발신 이메일: 구글(Gmail)만, @gmail.com 고정 ----------
        // 제목 줄: "■ 발신 이메일 (Gmail)" + 오른쪽 끝 [변경] (인증 완료된 경우에만 보임)
        LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(android.view.Gravity.BOTTOM);
        divider(root); // 영역 구분선
        root.addView(titleRow);
        LinearLayout titleBox = new LinearLayout(this);
        titleBox.setOrientation(LinearLayout.VERTICAL);
        titleRow.addView(titleBox, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        section(titleBox, "■ 발신 이메일 (Gmail)");
        changeBtn = new Button(this);
        changeBtn.setText("변경");
        changeBtn.setTextSize(13);
        changeBtn.setOnClickListener(v -> confirmReverify());
        titleRow.addView(changeBtn);
        String saved = up.getString("mail", em.getString("from", ""));
        int at = saved.indexOf('@');
        LinearLayout mr = row(root);
        mail = new EditText(this); // @ 앞 아이디만 입력
        mail.setSingleLine(true);
        mail.setHint("아이디");
        mail.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        mail.setText(at >= 0 ? saved.substring(0, at) : saved);
        mr.addView(mail, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        final TextView domainText = new TextView(this);
        domainText.setText("@" + mailDomain);
        domainText.setTextSize(16);
        mr.addView(domainText);
        gap(root, 14); // 줄 간격 (가독성)
        pass = edit(root, "앱 비밀번호", up.getString("mailPass", em.getString("pass", "")),
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        gap(root, 18);
        // [인증 메일 보내기] (가로 전체)
        sendBtn = new Button(this);
        sendBtn.setText("인증 메일 보내기");
        sendBtn.setTextSize(14);
        sendBtn.setAllCaps(false);
        sendBtn.setOnClickListener(v -> sendCode());
        root.addView(sendBtn, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        // 버튼 아래 항목 묶음: 이미 인증되어 있으면 통째로 숨김
        afterSend = new LinearLayout(this);
        afterSend.setOrientation(LinearLayout.VERTICAL);
        root.addView(afterSend);
        // 작은 글씨 설명 (보낼 주소는 입력한 아이디에 따라 바뀜)
        final TextView sendNote = new TextView(this);
        sendNote.setTextSize(11);
        sendNote.setTextColor(Color.GRAY);
        sendNote.setPadding(0, 6, 0, 6);
        sendNote.setLineSpacing(0, 1.3f); // 설명문 줄 사이 넓게
        afterSend.addView(sendNote);
        final Runnable updateNote = () -> {
            String m = fullMail();
            sendNote.setText("이 버튼을 누르면 6자리 숫자가 " + (m.isEmpty() ? "xxxxxx@" + mailDomain : m)
                    + " 메일로 보내집니다. 그 6자리 숫자를 다음 칸에 입력하여야만 이메일 인증이 완료됩니다.");
        };
        updateNote.run();
        mail.addTextChangedListener(new android.text.TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void onTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void afterTextChanged(android.text.Editable s) {
                updateNote.run();
                showMailStatus(); // 아이디가 바뀌면 인증 상태·버튼도 다시 표시
            }
        });
        gap(afterSend, 14);
        // [인증번호 6자리] + [확인]
        LinearLayout r1 = row(afterSend);
        code = new EditText(this);
        code.setHint("인증번호 6자리");
        code.setInputType(InputType.TYPE_CLASS_NUMBER);
        r1.addView(code, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        button(r1, "확인", v -> verifyCode());
        gap(afterSend, 14);
        afterSend.addView(note("이메일 등록을 위한 자세한 설명 : " + EmailSender.GUIDE_URL)); // 누르면 브라우저로 열림
        gap(afterSend, 6);
        mailStatus = note("");
        afterSend.addView(mailStatus);
        showMailStatus();

        // ---------- 여기부터 선택 항목: 굵은 구분선 + 빈 공간 + 안내 ----------
        divider(root);
        TextView optional = new TextView(this);
        optional.setText("이하, 항목들은 필수 항목이 아닙니다");
        optional.setTextSize(15);
        optional.setTypeface(null, Typeface.BOLD);
        optional.setTextColor(0xFFC62828);
        optional.setGravity(android.view.Gravity.CENTER);
        LinearLayout.LayoutParams olp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        olp.topMargin = (int) (24 * getResources().getDisplayMetrics().density); // 구분선 아래 빈 공간
        olp.bottomMargin = (int) (16 * getResources().getDisplayMetrics().density);
        root.addView(optional, olp);

        // ---------- 3) 텔레그램 ----------
        section(root, "■ 텔레그램");
        token = edit(root, "봇 토큰 123456789:AAH...", tg.getString("token", ""), InputType.TYPE_CLASS_TEXT);
        LinearLayout r2 = row(root);
        button(r2, "붙여넣기", v -> pasteToken());
        button(r2, "확인", v -> checkToken());
        button(r2, "설정 도우미", v -> {
            save();
            startActivity(new Intent(this, TelegramSetupActivity.class));
        });
        tgStatus = note("");
        root.addView(tgStatus);
        showTgStatus(null);

        // ---------- 4) Tailscale VPN / PC 서버 ----------
        divider(root);
        section(root, "■ Tailscale VPN / PC 서버");
        tsStatus = note("");
        root.addView(tsStatus);
        LinearLayout r3 = row(root);
        button(r3, "다시 찾기", v -> showTsStatus());
        button(r3, "Tailscale 앱 열기", v -> openTailscale());
        showTsStatus();
        pcIp = edit(root, "PC 서버 IP", up.getString("pcIp", ""), InputType.TYPE_CLASS_TEXT);
        pcPort = edit(root, "PC 서버 포트", up.getString("pcPort", ""), InputType.TYPE_CLASS_NUMBER);
        pcKey = edit(root, "접속 키", up.getString("pcKey", ""), InputType.TYPE_CLASS_TEXT);
        LinearLayout r4 = row(root);
        button(r4, "접속 키 만들기", v -> pcKey.setText(randomKey(24)));
        button(r4, "접속 키 복사", v -> copy(pcKey.getText().toString()));
        button(r4, "연결 테스트", v -> testPc());
        pcStatus = note("");
        root.addView(pcStatus);

        // ---------- 5) 긴급 의료 정보 ----------
        divider(root);
        section(root, "■ 긴급 의료 정보");
        blood = edit(root, "혈액형", up.getString("blood", ""), InputType.TYPE_CLASS_TEXT);
        disease = edit(root, "지병", up.getString("disease", ""), InputType.TYPE_CLASS_TEXT);
        meds = edit(root, "복용 약", up.getString("meds", ""), InputType.TYPE_CLASS_TEXT);
        memo = edit(root, "메모", up.getString("memo", ""), InputType.TYPE_CLASS_TEXT);
        medSos = new CheckBox(this);
        medSos.setText("SOS 메시지에 의료 정보 첨부");
        medSos.setChecked(up.getBoolean("medSos", true));
        root.addView(medSos);

        // ---------- 6) 설정 백업·복원 ----------
        divider(root);
        section(root, "■ 설정 백업·복원");
        LinearLayout r5 = row(root);
        button(r5, "설정 내보내기", v -> exportSettings());
        button(r5, "설정 불러오기", v -> importSettings());

        Button close = new Button(this);
        close.setText("저장하고 닫기");
        close.setTypeface(null, Typeface.BOLD);
        close.setOnClickListener(v -> finish()); // onPause 에서 저장
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 40;
        root.addView(close, lp);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);
        ScreenId.tag(this, "1075"); // 화면 번호
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (token != null) {
            // 설정 도우미에서 토큰이 바뀌었을 수 있음
            String t = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).getString("token", "");
            if (!t.isEmpty() && !t.equals(token.getText().toString().trim())) {
                token.setText(t);
            }
            showTgStatus(null);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        save(); // 화면을 벗어나면 자동 저장
    }

    // ---------- 저장 (기존 기능에도 반영) ----------

    private void save() {
        TelegramSender.setSenderName(this, name.getText().toString());
        String m = fullMail();
        String p = pass.getText().toString().replace(" ", "");
        SharedPreferences.Editor ed = up.edit()
                .putString("phone", phone.getText().toString().trim())
                .putString("device", device.getText().toString().trim())
                .putString("pcIp", pcIp.getText().toString().trim())
                .putString("pcPort", pcPort.getText().toString().trim())
                .putString("pcKey", pcKey.getText().toString().trim())
                .putString("blood", blood.getText().toString().trim())
                .putString("disease", disease.getText().toString().trim())
                .putString("meds", meds.getText().toString().trim())
                .putString("memo", memo.getText().toString().trim())
                .putBoolean("medSos", medSos.isChecked());
        if (!m.equals(up.getString("mail", "")) || !p.equals(up.getString("mailPass", ""))) {
            ed.putBoolean("mailVerified", false); // 주소나 비밀번호가 바뀌면 다시 검증
        }
        ed.putString("mail", m).putString("mailPass", p).apply();
        if (!m.isEmpty()) {
            // 일반 이메일(1150)·카톡 이메일(1160)의 보내는 계정으로 사용
            for (String pref : new String[]{HomeService.PREF_EMAIL, HomeService.PREF_KMAIL}) {
                getSharedPreferences(pref, MODE_PRIVATE).edit().putString("from", m).putString("pass", p).apply();
            }
        }
        String tk = token.getText().toString().trim();
        if (!tk.isEmpty()) {
            getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).edit().putString("token", tk).apply();
        }
    }

    // 발신 이메일 전체 주소 = 아이디 + "@" + 고른 메일(카카오·네이버·구글). 아이디가 비면 ""
    private String fullMail() {
        String id = mail.getText().toString().trim();
        int at = id.indexOf('@');
        if (at >= 0) {
            id = id.substring(0, at); // 실수로 전체 주소를 넣어도 @ 앞만 사용
        }
        return id.isEmpty() ? "" : id + "@" + mailDomain;
    }

    // ---------- 발신 이메일 검증 ----------

    private void showMailStatus() {
        boolean ok = up.getBoolean("mailVerified", false)
                && !fullMail().isEmpty() && fullMail().equals(up.getString("mail", ""));
        if (mailStatus != null) {
            long t = up.getLong("mailVerifiedAt", 0);
            mailStatus.setText(ok ? "✓ 검증됨 (" + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(t)) + ")"
                    : "아직 검증하지 않았습니다.");
            mailStatus.setTextColor(ok ? 0xFF2E7D32 : Color.DKGRAY);
        }
        // [인증 메일 보내기]: 이미 인증됨 -> 비활성 / 아직 -> 빨간 글씨 + (필수)
        if (sendBtn != null) {
            sendBtn.setEnabled(!ok);
            sendBtn.setText(ok ? "인증 메일 보내기 (인증 완료)" : "인증 메일 보내기 (필수)");
            sendBtn.setTextColor(ok ? Color.GRAY : 0xFFC62828);
        }
        // 이미 인증되어 있으면 버튼 아래 항목(설명문·인증번호 칸·[확인]·링크·결과)은 모두 숨김
        if (afterSend != null) {
            afterSend.setVisibility(ok ? View.GONE : View.VISIBLE);
        }
        // 제목 줄 오른쪽 [변경]: 인증 완료된 경우에만
        if (changeBtn != null) {
            changeBtn.setVisibility(ok ? View.VISIBLE : View.GONE);
        }
    }

    // [변경]: 확인 후 기존 인증을 모두 폐기하고 처음부터 다시 인증 (화면 1077)
    private void confirmReverify() {
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("발신 이메일 변경")
                .setMessage("변경 또는 재인증합니까?")
                .setPositiveButton("Yes", (d, w) -> {
                    up.edit().putBoolean("mailVerified", false).remove("mailVerifiedAt").commit();
                    HomeService.refresh(this);
                    pendingCode = null;
                    code.setText("");
                    showMailStatus(); // [인증 메일 보내기 (필수)] 와 아래 항목이 다시 나타남
                    mail.requestFocus();
                    Toast.makeText(this, "기존 인증을 폐기했습니다. 아이디·앱 비밀번호를 확인하고 다시 인증하세요.",
                            Toast.LENGTH_LONG).show();
                })
                .setNegativeButton("No", null)
                .show(), "1077");
    }

    private void sendCode() {
        final String m = fullMail();
        final String p = pass.getText().toString().replace(" ", "");
        if (!Patterns.EMAIL_ADDRESS.matcher(m).matches() || p.isEmpty()) {
            Toast.makeText(this, "보내는 이메일과 앱 비밀번호를 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (EmailSender.smtpHost(m) == null) {
            Toast.makeText(this, "보내는 계정은 " + EmailSender.SUPPORTED + " 만 사용할 수 있습니다.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        final String c = String.format(Locale.US, "%06d", new SecureRandom().nextInt(1000000));
        mailStatus.setText("인증 메일 보내는 중...");
        mailStatus.setTextColor(Color.DKGRAY);
        new Thread(() -> {
            String err = EmailSender.send(m, p, m, "[집에왔어요] 이메일 인증번호",
                    "인증번호: " + c + "\n\n앱의 [사용자 등록] 화면에 10분 안에 입력하세요.");
            runOnUiThread(() -> {
                if (err == null) {
                    pendingCode = c;
                    codeTime = System.currentTimeMillis();
                    mailStatus.setText("인증번호를 " + m + " 로 보냈습니다. 메일함을 확인하고 입력하세요.");
                } else {
                    mailStatus.setText("✗ 보내기 실패: " + err);
                    mailStatus.setTextColor(0xFFC62828);
                }
            });
        }).start();
    }

    private void verifyCode() {
        String c = code.getText().toString().trim();
        if (pendingCode == null || System.currentTimeMillis() - codeTime > 10 * 60 * 1000) {
            Toast.makeText(this, "먼저 [인증 메일 보내기]를 누르세요. (인증번호는 10분간 유효)", Toast.LENGTH_LONG).show();
            return;
        }
        if (!pendingCode.equals(c)) {
            Toast.makeText(this, "인증번호가 맞지 않습니다.", Toast.LENGTH_SHORT).show();
            return;
        }
        pendingCode = null;
        // 검증되면 [저장하고 닫기]를 누르지 않아도 무조건 바로 저장 (화면 전체 입력값 + 검증 표시)
        save(); // 주소·비밀번호를 먼저 저장 (바뀌었으면 검증 해제됨)
        up.edit().putBoolean("mailVerified", true).putLong("mailVerifiedAt", System.currentTimeMillis()).commit();
        HomeService.refresh(this); // 이메일 위치 요청 감시 시작 (보호자 이메일이 있으면)
        showMailStatus();
        Toast.makeText(this, "이메일 검증 완료 — 저장했습니다", Toast.LENGTH_SHORT).show();
    }

    // ---------- 텔레그램 ----------

    private void pasteToken() {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            ClipData clip = cm.getPrimaryClip();
            CharSequence txt = clip != null && clip.getItemCount() > 0 ? clip.getItemAt(0).coerceToText(this) : null;
            String t = TelegramSender.extractToken(txt);
            if (t == null) {
                Toast.makeText(this, "복사된 내용에서 봇 토큰을 찾지 못했습니다.", Toast.LENGTH_SHORT).show();
                return;
            }
            token.setText(t);
            checkToken();
        } catch (Exception e) {
            Toast.makeText(this, "붙여넣기 실패", Toast.LENGTH_SHORT).show();
        }
    }

    private void checkToken() {
        final String tk = token.getText().toString().trim();
        if (tk.isEmpty()) {
            Toast.makeText(this, "봇 토큰을 넣어 주세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        tgStatus.setText("확인 중...");
        new Thread(() -> {
            String[] err = new String[1];
            String bot = TelegramSender.botUsername(tk, err);
            runOnUiThread(() -> {
                if (bot != null) {
                    getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).edit().putString("token", tk).apply();
                }
                showTgStatus(bot == null ? "✗ 확인 실패: " + err[0] : "✓ 확인됨: @" + bot);
            });
        }).start();
    }

    private void showTgStatus(String head) {
        String chats = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).getString("chats", "").trim();
        int n = chats.isEmpty() ? 0 : chats.split("[,\\s]+").length;
        tgStatus.setText((head == null ? "" : head + "\n") + "받는 사람 " + n + "명 등록"
                + (n == 0 ? " — [설정 도우미]로 초대하세요." : ""));
        tgStatus.setTextColor(head != null && head.startsWith("✗") ? 0xFFC62828 : Color.DKGRAY);
    }

    // ---------- Tailscale / PC 서버 ----------

    // 이 폰의 Tailscale IP (100.64.0.0/10 대역). 없으면 null
    static String tailscaleIp() {
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp()) {
                    continue;
                }
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (a instanceof Inet4Address) {
                        byte[] b = a.getAddress();
                        if ((b[0] & 0xff) == 100 && (b[1] & 0xC0) == 64) {
                            return a.getHostAddress();
                        }
                    }
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private void showTsStatus() {
        String ip = tailscaleIp();
        up.edit().putString("tsIp", ip == null ? "" : ip).apply();
        tsStatus.setText(ip != null ? "✓ 이 폰의 Tailscale IP: " + ip
                : "✗ Tailscale 연결 안 됨 (Tailscale 앱을 설치하고 켜 주세요)");
        tsStatus.setTextColor(ip != null ? 0xFF2E7D32 : 0xFFC62828);
    }

    private void openTailscale() {
        Intent i = getPackageManager().getLaunchIntentForPackage(TS_PKG);
        try {
            startActivity(i != null ? i
                    : new Intent(Intent.ACTION_VIEW, Uri.parse("https://play.google.com/store/apps/details?id=" + TS_PKG)));
        } catch (Exception e) {
            Toast.makeText(this, "열 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    private void testPc() {
        final String ip = pcIp.getText().toString().trim();
        final String ps = pcPort.getText().toString().trim();
        int port;
        try {
            port = Integer.parseInt(ps);
        } catch (NumberFormatException e) {
            port = -1;
        }
        if (ip.isEmpty() || port <= 0 || port > 65535) {
            Toast.makeText(this, "PC 서버 IP와 포트를 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        final int pt = port;
        pcStatus.setText("연결 시험 중...");
        pcStatus.setTextColor(Color.DKGRAY);
        new Thread(() -> {
            long t0 = System.currentTimeMillis();
            String r;
            try (Socket s = new Socket()) {
                s.connect(new InetSocketAddress(ip, pt), 4000);
                r = "✓ 연결됨 (" + (System.currentTimeMillis() - t0) + " ms)";
            } catch (Exception e) {
                r = "✗ 연결 실패 — PC 서버가 켜져 있는지, Tailscale 이 양쪽에서 켜져 있는지 확인하세요.";
            }
            final String res = r;
            runOnUiThread(() -> {
                pcStatus.setText(res);
                pcStatus.setTextColor(res.startsWith("✓") ? 0xFF2E7D32 : 0xFFC62828);
            });
        }).start();
    }

    private static String randomKey(int n) {
        String abc = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        SecureRandom r = new SecureRandom();
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            sb.append(abc.charAt(r.nextInt(abc.length())));
        }
        return sb.toString();
    }

    private void copy(String s) {
        ((ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("key", s));
        Toast.makeText(this, "복사했습니다.", Toast.LENGTH_SHORT).show();
    }

    // ---------- 긴급 의료 정보 (SOS 메시지에 첨부) ----------

    static String medicalText(Context c) {
        SharedPreferences u = c.getSharedPreferences(PREF_USER, MODE_PRIVATE);
        if (!u.getBoolean("medSos", true)) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        String[][] items = {{"혈액형", "blood"}, {"지병", "disease"}, {"복용 약", "meds"}, {"메모", "memo"},
                {"본인 연락처", "phone"}};
        for (String[] it : items) {
            String v = u.getString(it[1], "").trim();
            if (!v.isEmpty()) {
                sb.append("\n■ ").append(it[0]).append(": ").append(v);
            }
        }
        return sb.length() == 0 ? "" : "\n\n🩺 긴급 의료 정보" + sb;
    }

    // ---------- 설정 백업·복원 ----------

    private void exportSettings() {
        save();
        String day = new SimpleDateFormat("yyyyMMdd_HHmm", Locale.KOREA).format(new Date());
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType("application/json")
                .putExtra(Intent.EXTRA_TITLE, "집에왔어요_설정_" + day + ".json");
        startActivityForResult(i, REQ_EXPORT);
    }

    private void importSettings() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*");
        startActivityForResult(i, REQ_IMPORT);
    }

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri uri = data.getData();
        if (req == REQ_EXPORT) {
            try (OutputStream os = getContentResolver().openOutputStream(uri)) {
                os.write(backupJson().toString(2).getBytes(StandardCharsets.UTF_8));
                Toast.makeText(this, "설정을 파일로 저장했습니다.", Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                Toast.makeText(this, "저장 실패: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        } else if (req == REQ_IMPORT) {
            try (BufferedReader br = new BufferedReader(new InputStreamReader(
                    getContentResolver().openInputStream(uri), StandardCharsets.UTF_8))) {
                StringBuilder sb = new StringBuilder();
                String line;
                while ((line = br.readLine()) != null) {
                    sb.append(line).append('\n');
                }
                JSONObject root = new JSONObject(sb.toString());
                if (!"집에왔어요".equals(root.optString("app")) || root.optJSONObject("prefs") == null) {
                    throw new Exception("집에왔어요 설정 파일이 아닙니다.");
                }
                pendingImport = sb.toString();
                String when = root.optString("time", "");
                ScreenId.tag(new AlertDialog.Builder(this)
                        .setTitle("설정 불러오기")
                        .setMessage("지금 설정을 모두 지우고 백업 파일(" + when + ")의 설정으로 바꿉니다.\n"
                                + "되돌릴 수 없습니다. 계속할까요?")
                        .setPositiveButton("불러오기", (d, w) -> applyImport())
                        .setNegativeButton("취소", null)
                        .show(), "1076");
            } catch (Exception e) {
                Toast.makeText(this, "불러오기 실패: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
    }

    // 모든 설정 -> JSON. 값마다 종류(t)를 기록해 그대로 되살림
    private JSONObject backupJson() throws Exception {
        JSONObject root = new JSONObject();
        root.put("app", "집에왔어요");
        root.put("format", 1);
        root.put("time", new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.KOREA).format(new Date()));
        JSONObject prefs = new JSONObject();
        for (String p : ALL_PREFS) {
            JSONObject o = new JSONObject();
            for (Map.Entry<String, ?> e : getSharedPreferences(p, MODE_PRIVATE).getAll().entrySet()) {
                Object v = e.getValue();
                JSONObject t = new JSONObject();
                if (v instanceof String) {
                    t.put("t", "s").put("v", v);
                } else if (v instanceof Boolean) {
                    t.put("t", "b").put("v", v);
                } else if (v instanceof Integer) {
                    t.put("t", "i").put("v", v);
                } else if (v instanceof Long) {
                    t.put("t", "l").put("v", v);
                } else if (v instanceof Float) {
                    t.put("t", "f").put("v", (double) (Float) v);
                } else if (v instanceof Set) {
                    JSONArray a = new JSONArray();
                    for (Object x : (Set<?>) v) {
                        a.put(String.valueOf(x));
                    }
                    t.put("t", "set").put("v", a);
                } else {
                    continue;
                }
                o.put(e.getKey(), t);
            }
            prefs.put(p, o);
        }
        root.put("prefs", prefs);
        return root;
    }

    private void applyImport() {
        try {
            JSONObject prefs = new JSONObject(pendingImport).getJSONObject("prefs");
            HomeService.stopAll(this); // 예약·서비스 정리 후 새 설정으로 다시 시작
            for (String p : ALL_PREFS) {
                SharedPreferences.Editor ed = getSharedPreferences(p, MODE_PRIVATE).edit().clear();
                JSONObject o = prefs.optJSONObject(p);
                if (o != null) {
                    Iterator<String> keys = o.keys();
                    while (keys.hasNext()) {
                        String k = keys.next();
                        JSONObject t = o.getJSONObject(k);
                        switch (t.getString("t")) {
                            case "s":
                                ed.putString(k, t.getString("v"));
                                break;
                            case "b":
                                ed.putBoolean(k, t.getBoolean("v"));
                                break;
                            case "i":
                                ed.putInt(k, t.getInt("v"));
                                break;
                            case "l":
                                ed.putLong(k, t.getLong("v"));
                                break;
                            case "f":
                                ed.putFloat(k, (float) t.getDouble("v"));
                                break;
                            case "set":
                                Set<String> set = new HashSet<>();
                                JSONArray a = t.getJSONArray("v");
                                for (int i = 0; i < a.length(); i++) {
                                    set.add(a.getString(i));
                                }
                                ed.putStringSet(k, set);
                                break;
                            default:
                                break;
                        }
                    }
                }
                ed.commit();
            }
            // 다음 송신 시각은 새로 계산되도록 비움
            for (String p : new String[]{TelegramSender.PREF, HomeService.PREF_KAKAO, HomeService.PREF_EMAIL,
                    HomeService.PREF_KMAIL, HomeService.PREF_SMS, HomeService.PREF_SMSAUTO, HomeService.PREF_SELF}) {
                getSharedPreferences(p, MODE_PRIVATE).edit().putLong("next", 0).commit();
            }
            pendingImport = null;
            Toast.makeText(this, "설정을 불러왔습니다. 앱을 다시 시작합니다.", Toast.LENGTH_LONG).show();
            if (HomeService.anyOn(this)) {
                HomeService.refresh(this);
            }
            // 화면을 처음부터 다시 그림 (불러온 값으로)
            startActivity(new Intent(this, MainActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            finish();
        } catch (Exception e) {
            Toast.makeText(this, "불러오기 실패: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    // ---------- 화면 도우미 ----------

    private void section(LinearLayout root, String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(17);
        t.setTypeface(null, Typeface.BOLD);
        t.setTextColor(0xFF1976D2);
        t.setPadding(0, 44, 0, 6);
        root.addView(t);
    }

    private TextView note(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(13);
        t.setTextColor(Color.DKGRAY);
        t.setPadding(0, 4, 0, 4);
        t.setAutoLinkMask(android.text.util.Linkify.WEB_URLS);
        t.setText(s);
        return t;
    }

    private EditText edit(LinearLayout root, String hint, String value, int type) {
        TextView l = new TextView(this);
        l.setText(hint);
        l.setTextSize(12);
        l.setTextColor(Color.GRAY);
        l.setPadding(0, 10, 0, 0);
        root.addView(l);
        EditText e = new EditText(this);
        e.setSingleLine(true);
        e.setInputType(type);
        e.setText(value);
        root.addView(e);
        return e;
    }

    // 영역 사이 굵은 구분선 (위아래 넉넉한 공간)
    private void divider(LinearLayout root) {
        float dn = getResources().getDisplayMetrics().density;
        View v = new View(this);
        v.setBackgroundColor(0xFF424242);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, (int) (4 * dn));
        lp.topMargin = (int) (36 * dn);
        lp.bottomMargin = (int) (8 * dn);
        root.addView(v, lp);
    }

    // 세로 빈 공간 (dp)
    private void gap(LinearLayout root, int dp) {
        View v = new View(this);
        root.addView(v, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                (int) (dp * getResources().getDisplayMetrics().density)));
    }

    private LinearLayout row(LinearLayout root) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(android.view.Gravity.CENTER_VERTICAL);
        root.addView(r);
        return r;
    }

    private Button button(LinearLayout row, String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        row.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return b;
    }
}
