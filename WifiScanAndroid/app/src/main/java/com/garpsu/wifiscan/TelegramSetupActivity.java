package com.garpsu.wifiscan;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.location.Location;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * 텔레그램 설정 도우미 (처음 사용하는 사람용, 단계별 화면).
 * 1 봇 만들기 -> 2 토큰 넣기 -> 3 받는 사람 초대(초대 코드가 든 링크) -> 4 연결 확인(자동) -> 5 테스트 -> 6 자동 송신 시작
 * 받는 사람은 초대 링크를 눌러 [시작]만 하면 앱이 자동으로 등록한다. (초대 코드가 없는 사람은 등록하지 않음)
 */
public class TelegramSetupActivity extends Activity {

    // 도우미가 열려 있는 동안 true: 백그라운드의 원격 위치 요청 수신이 잠시 쉼 (같은 봇 메시지를 서로 가져가지 않게)
    static volatile boolean active;
    private static final String TG_PKG = "org.telegram.messenger";
    private static final long POLL = 3000; // 연결 확인 간격

    private SharedPreferences sp;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private EditText token, senderName;
    private TextView tokenStatus, linkText, waitStatus, testStatus;
    private LinearLayout people;
    private final List<View> afterToken = new ArrayList<>(); // 토큰 확인 후에 쓸 수 있는 단계들
    private String bot;      // 확인된 봇 아이디
    private String invite;   // 초대 코드
    private boolean polling, pollBusy;

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            checkNewPeople();
            handler.postDelayed(this, POLL);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        invite = sp.getString("invite", "");
        if (invite.isEmpty()) {
            invite = newCode();
            sp.edit().putString("invite", invite).apply();
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(40, 30, 40, 60);

        TextView title = new TextView(this);
        title.setText("텔레그램 설정 도우미");
        title.setTextSize(22);
        title.setTypeface(null, Typeface.BOLD);
        root.addView(title);
        root.addView(note("아래 순서대로 하면 받는 사람에게 위치가 자동으로 전송됩니다.\n"
                + "받는 사람은 링크를 눌러 [시작]만 누르면 됩니다."));

        // ---------- 1단계: 봇 만들기 ----------
        root.addView(step("1", "봇 만들기 (처음 한 번)"));
        boolean hasTg = getPackageManager().getLaunchIntentForPackage(TG_PKG) != null;
        if (!hasTg) {
            TextView warn = note("이 폰에 텔레그램이 없습니다. 먼저 설치하세요.");
            warn.setTextColor(0xFFC62828);
            root.addView(warn);
            root.addView(button("텔레그램 설치하기", view -> open("market://details?id=" + TG_PKG,
                    "https://play.google.com/store/apps/details?id=" + TG_PKG)));
        }
        root.addView(note("텔레그램의 BotFather(봇을 만들어 주는 공식 계정)에게 차례로 입력합니다.\n"
                + " ① /newbot\n"
                + " ② 봇 이름  (예: 우리집 위치알림)\n"
                + " ③ 봇 아이디  (영문·숫자, 끝은 bot / 예: hong_location_bot)\n"
                + "완료되면 BotFather가 보낸 긴 메시지를 길게 눌러 [복사]하고 이 화면으로 돌아오세요.\n"
                + "※ 이미 봇이 있으면: /mybots → 봇 선택 → API Token"));
        LinearLayout r1 = row();
        r1.addView(button("BotFather 열기", view -> open("https://t.me/BotFather", null)), weight());
        r1.addView(button("/newbot 복사", view -> copy("/newbot")), weight());
        root.addView(r1);

        // ---------- 2단계: 토큰 넣기 ----------
        root.addView(step("2", "봇 토큰 넣기"));
        root.addView(note("1단계에서 복사한 메시지를 [붙여넣기] 하세요. 토큰만 골라서 넣습니다."));
        token = new EditText(this);
        token.setSingleLine(true);
        token.setHint("123456789:AAH...");
        token.setText(sp.getString("token", ""));
        root.addView(token);
        LinearLayout r2 = row();
        r2.addView(button("붙여넣기", view -> pasteToken()), weight());
        r2.addView(button("확인", view -> verifyToken()), weight());
        root.addView(r2);
        tokenStatus = note("");
        root.addView(tokenStatus);
        token.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int pos, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence text, int pos, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable text) {
                if (bot != null) { // 토큰이 바뀌면 다시 확인해야 함
                    setBot(null);
                    tokenStatus.setText("토큰이 바뀌었습니다. [확인]을 눌러 주세요.");
                    tokenStatus.setTextColor(Color.DKGRAY);
                }
            }
        });

        // ---------- 3단계: 받는 사람 초대 ----------
        LinearLayout s3 = section(root);
        s3.addView(step("3", "받는 사람 초대"));
        s3.addView(note("아래 링크를 카톡·문자로 받는 사람에게 보내세요.\n"
                + "받는 사람이 링크를 누르고 텔레그램에서 [시작]을 누르면 자동으로 등록됩니다."));
        linkText = note("");
        linkText.setTextIsSelectable(true);
        linkText.setTextColor(0xFF1976D2);
        s3.addView(linkText);
        s3.addView(button("받는 사람에게 링크 보내기 (카톡·문자)", view -> shareLink(false)));
        s3.addView(button("단톡방용 링크 보내기", view -> shareLink(true)));
        TextView n3 = note("※ 이 링크를 받은 사람만 등록됩니다. 링크가 퍼졌다면 [새 링크 만들기]를 누르세요.");
        n3.setTextSize(12);
        s3.addView(n3);
        s3.addView(button("새 링크 만들기", view -> {
            invite = newCode();
            sp.edit().putString("invite", invite).apply();
            showLink();
            Toast.makeText(this, "새 링크를 만들었습니다. 예전 링크로는 더 이상 등록되지 않습니다.",
                    Toast.LENGTH_LONG).show();
        }));

        // ---------- 4단계: 연결 확인 ----------
        LinearLayout s4 = section(root);
        s4.addView(step("4", "연결 확인 (자동)"));
        waitStatus = note("");
        s4.addView(waitStatus);
        people = new LinearLayout(this);
        people.setOrientation(LinearLayout.VERTICAL);
        s4.addView(people);

        // ---------- 5단계: 테스트 ----------
        LinearLayout s5 = section(root);
        s5.addView(step("5", "테스트"));
        s5.addView(note("등록된 모든 사람에게 지금 위치를 한 번 보냅니다. 받는 사람 폰에 왔는지 확인하세요."));
        s5.addView(button("테스트 보내기", view -> sendTest()));
        testStatus = note("");
        s5.addView(testStatus);

        // ---------- 6단계: 자동 송신 시작 ----------
        LinearLayout s6 = section(root);
        s6.addView(step("6", "자동 송신 시작"));
        s6.addView(note("보내는 사람 이름과 간격을 정하고 [시작하기]를 누르면 끝입니다. 앱을 닫아도 계속 보냅니다."));
        TextView wl = note("보내는 사람 이름 (받는 사람에게 표시, 예: 할머니)");
        wl.setTypeface(null, Typeface.BOLD);
        s6.addView(wl);
        senderName = new EditText(this);
        senderName.setSingleLine(true);
        senderName.setHint("예: 할머니, 민준이, 아빠");
        senderName.setText(TelegramSender.senderName(this));
        s6.addView(senderName);
        TextView il = note("보내는 간격");
        il.setTypeface(null, Typeface.BOLD);
        s6.addView(il);
        final Spinner interval = new Spinner(this);
        interval.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, TelegramSender.INTERVAL_LABELS));
        int cur = sp.getInt("interval", 10);
        for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
            if (TelegramSender.INTERVALS[i] == cur) {
                interval.setSelection(i);
            }
        }
        s6.addView(interval);
        Button go = button("시작하기", view -> start(TelegramSender.INTERVALS[interval.getSelectedItemPosition()]));
        go.setTypeface(null, Typeface.BOLD);
        s6.addView(go);
        root.addView(button("닫기 (나중에 하기)", view -> finish())); // 토큰 전에도 누를 수 있게 단계 밖에 둠

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);
        ScreenId.tag(this, "1070"); // 화면 번호

        renderPeople();
        setBot(null);
        if (!token.getText().toString().trim().isEmpty()) {
            verifyToken(); // 이미 넣은 토큰이 있으면 바로 확인
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        active = true;
        // BotFather 에서 복사하고 돌아오면 자동으로 붙여넣기 (토큰이 아직 없을 때만)
        if (bot == null && token.getText().toString().trim().isEmpty()) {
            handler.postDelayed(() -> {
                String found = TelegramSender.extractToken(clipText());
                if (found != null && token.getText().toString().trim().isEmpty()) {
                    token.setText(found);
                    verifyToken();
                }
            }, 500); // 화면에 포커스가 온 뒤에 클립보드 읽기
        }
        startPolling();
    }

    @Override
    protected void onPause() {
        super.onPause();
        active = false;
        polling = false;
        handler.removeCallbacks(poll);
    }

    // ---------- 2단계: 토큰 ----------

    private CharSequence clipText() {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            ClipData clip = cm.getPrimaryClip();
            return clip != null && clip.getItemCount() > 0 ? clip.getItemAt(0).coerceToText(this) : null;
        } catch (Exception e) {
            return null;
        }
    }

    private void pasteToken() {
        CharSequence txt = clipText();
        String found = TelegramSender.extractToken(txt);
        if (found == null) {
            Toast.makeText(this, txt == null ? "복사된 내용이 없습니다. BotFather 메시지를 먼저 복사하세요."
                    : "복사된 내용에서 봇 토큰을 찾지 못했습니다.", Toast.LENGTH_LONG).show();
            return;
        }
        token.setText(found);
        verifyToken();
    }

    private void verifyToken() {
        final String tk = token.getText().toString().trim();
        if (tk.isEmpty()) {
            Toast.makeText(this, "봇 토큰을 넣어 주세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        tokenStatus.setText("확인 중...");
        tokenStatus.setTextColor(Color.DKGRAY);
        new Thread(() -> {
            String[] err = new String[1];
            String username = TelegramSender.botUsername(tk, err);
            runOnUiThread(() -> {
                if (username == null) {
                    tokenStatus.setText("✗ 확인 실패: " + err[0] + "\n토큰을 다시 복사해 붙여 넣으세요.");
                    tokenStatus.setTextColor(0xFFC62828);
                    return;
                }
                SharedPreferences.Editor ed = sp.edit().putString("token", tk);
                if (!tk.equals(sp.getString("token", ""))) {
                    ed.putLong("lastUpdate", 0); // 다른 봇: 메시지 번호가 달라지므로 처음부터
                }
                ed.apply();
                setBot(username);
                tokenStatus.setText("✓ 확인됨: @" + username);
                tokenStatus.setTextColor(0xFF2E7D32);
                startPolling();
            });
        }).start();
    }

    // 토큰 확인 전에는 3~6단계를 흐리게, 누를 수 없게
    private void setBot(String username) {
        bot = username;
        for (View view : afterToken) {
            view.setAlpha(username == null ? 0.35f : 1f);
            setEnabledAll(view, username != null);
        }
        showLink();
        waitStatus.setText(username == null ? "2단계(토큰 확인)를 먼저 해 주세요."
                : "받는 사람이 [시작]을 누르기를 기다리는 중... (3초마다 자동 확인)");
    }

    private static void setEnabledAll(View view, boolean on) {
        view.setEnabled(on);
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                setEnabledAll(group.getChildAt(i), on);
            }
        }
    }

    // ---------- 3단계: 초대 링크 ----------

    private static String newCode() {
        String abc = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        SecureRandom rnd = new SecureRandom();
        StringBuilder sb = new StringBuilder("w");
        for (int i = 0; i < 11; i++) {
            sb.append(abc.charAt(rnd.nextInt(abc.length())));
        }
        return sb.toString();
    }

    private String link(boolean group) {
        return "https://t.me/" + bot + (group ? "?startgroup=" : "?start=") + invite;
    }

    private void showLink() {
        linkText.setText(bot == null ? "(토큰을 확인하면 링크가 만들어집니다)" : link(false));
    }

    private void shareLink(boolean group) {
        if (bot == null) {
            return;
        }
        String who = senderName.getText().toString().trim();
        if (who.isEmpty()) {
            who = TelegramSender.senderName(this);
        }
        String title = "📍 " + (who.isEmpty() ? "" : who + "의 ") + "위치 알림 받기";
        String text = group
                ? title + " (단톡방)\n\n아래 링크를 누르고, 위치 알림을 받을 단톡방을 골라 봇을 추가해 주세요.\n"
                + link(true) + "\n\n(텔레그램이 없으면 먼저 설치해 주세요)"
                : title + "\n\n아래 링크를 누르고, 텔레그램 화면 아래의 [시작]을 눌러 주세요.\n"
                + link(false) + "\n\n(텔레그램이 없으면 먼저 설치해 주세요)";
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, "위치 알림 받기 (텔레그램)");
        send.putExtra(Intent.EXTRA_TEXT, text);
        startActivity(Intent.createChooser(send, "링크 보내기"));
    }

    // ---------- 4단계: 연결 확인 (초대 코드로 /start 한 사람 자동 등록) ----------

    private void startPolling() {
        if (bot == null || polling) {
            return;
        }
        polling = true;
        handler.removeCallbacks(poll);
        handler.post(poll);
    }

    private void checkNewPeople() {
        if (bot == null || pollBusy) {
            return;
        }
        pollBusy = true;
        final String tk = sp.getString("token", "");
        final String code = invite;
        new Thread(() -> {
            String[] err = new String[1];
            JSONArray arr = TelegramSender.getUpdates(tk, err);
            final List<String[]> added = new ArrayList<>();
            boolean noCode = false;
            if (arr != null) {
                long last = sp.getLong("lastUpdate", 0);
                long max = last;
                Pattern pattern = Pattern.compile("^/start(@\\S+)?(\\s+(\\S+))?\\s*$");
                for (int i = 0; i < arr.length(); i++) {
                    JSONObject update = arr.optJSONObject(i);
                    if (update == null) {
                        continue;
                    }
                    long uid = update.optLong("update_id");
                    if (uid <= last) {
                        continue; // 이미 처리한 메시지
                    }
                    max = Math.max(max, uid);
                    JSONObject msg = update.optJSONObject("message");
                    if (msg == null || msg.optJSONObject("chat") == null) {
                        continue;
                    }
                    Matcher mt = pattern.matcher(msg.optString("text"));
                    if (!mt.matches()) {
                        continue;
                    }
                    if (!code.equals(mt.group(3))) {
                        noCode = true; // 초대 링크 없이 /start
                        continue;
                    }
                    JSONObject chat = msg.optJSONObject("chat");
                    String id = String.valueOf(chat.optLong("id"));
                    String label = TelegramSender.chatLabel(chat);
                    added.add(new String[]{id, label});
                    // 받는 사람에게 연결 완료 안내
                    String who = TelegramSender.senderName(this);
                    TelegramSender.send(tk, id, "✅ 연결되었습니다!\n이제 이 대화방으로 "
                            + (who.isEmpty() ? "" : who + "의 ") + "위치 알림이 옵니다.");
                }
                sp.edit().putLong("lastUpdate", max).apply();
            }
            final boolean hint = noCode;
            runOnUiThread(() -> {
                pollBusy = false;
                if (arr == null) {
                    waitStatus.setText("확인 실패: " + err[0] + " (계속 다시 시도합니다)");
                    return;
                }
                for (String[] person : added) {
                    addPerson(person[0], person[1]);
                    Toast.makeText(this, person[1] + " 님이 연결되었습니다 ✓", Toast.LENGTH_LONG).show();
                }
                waitStatus.setText("받는 사람이 [시작]을 누르기를 기다리는 중... (3초마다 자동 확인)"
                        + (hint ? "\n※ 초대 링크 없이 [시작]한 사람이 있습니다. 3단계의 링크를 눌러야 등록됩니다." : ""));
            });
        }).start();
    }

    private LinkedHashSet<String> chatIds() {
        LinkedHashSet<String> ids = new LinkedHashSet<>();
        for (String chat : sp.getString("chats", "").split("[,\\s]+")) {
            if (!chat.isEmpty()) {
                ids.add(chat);
            }
        }
        return ids;
    }

    private void saveIds(LinkedHashSet<String> ids) {
        sp.edit().putString("chats", android.text.TextUtils.join(", ", ids)).apply();
    }

    private void addPerson(String id, String label) {
        LinkedHashSet<String> ids = chatIds();
        ids.add(id);
        saveIds(ids);
        TelegramSender.saveName(this, id, label);
        renderPeople();
    }

    private void renderPeople() {
        people.removeAllViews();
        LinkedHashSet<String> ids = chatIds();
        TextView title = note("등록된 받는 사람: " + ids.size() + "명");
        title.setTypeface(null, Typeface.BOLD);
        people.addView(title);
        for (final String id : ids) {
            String name = TelegramSender.nameOf(this, id);
            LinearLayout rowBox = row();
            rowBox.setGravity(Gravity.CENTER_VERTICAL);
            TextView tv = new TextView(this);
            tv.setText("✓ " + (name == null ? "ID " + id : name));
            tv.setTextSize(15);
            tv.setTextColor(0xFF2E7D32);
            rowBox.addView(tv, weight());
            Button del = new Button(this);
            del.setText("삭제");
            del.setTextSize(13);
            del.setOnClickListener(view -> {
                LinkedHashSet<String> chatSet = chatIds();
                chatSet.remove(id);
                saveIds(chatSet);
                renderPeople();
            });
            rowBox.addView(del);
            people.addView(rowBox);
        }
    }

    // ---------- 5단계: 테스트 ----------

    private void sendTest() {
        final String tk = sp.getString("token", "");
        final String ch = sp.getString("chats", "");
        if (ch.trim().isEmpty()) {
            Toast.makeText(this, "등록된 받는 사람이 없습니다. 3·4단계를 먼저 하세요.", Toast.LENGTH_LONG).show();
            return;
        }
        TelegramSender.setSenderName(this, senderName.getText().toString()); // 6단계에 적은 이름도 테스트에 반영
        testStatus.setText("보내는 중...");
        testStatus.setTextColor(Color.DKGRAY);
        final Location loc = lastLocation();
        new Thread(() -> {
            String err = TelegramSender.send(tk, ch, TelegramSender.message(this, loc));
            runOnUiThread(() -> {
                testStatus.setText(err == null ? "✓ 보냈습니다. 받는 사람 폰을 확인하세요." : "✗ 실패: " + err);
                testStatus.setTextColor(err == null ? 0xFF2E7D32 : 0xFFC62828);
            });
        }).start();
    }

    private Location lastLocation() {
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            return null;
        }
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        Location best = null;
        try {
            for (String str : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                Location loc = lm.getLastKnownLocation(str);
                if (loc != null && (best == null || loc.getTime() > best.getTime())) {
                    best = loc;
                }
            }
        } catch (SecurityException ignored) {
        }
        return best;
    }

    // ---------- 6단계: 자동 송신 시작 ----------

    private void start(int min) {
        if (chatIds().isEmpty()) {
            Toast.makeText(this, "등록된 받는 사람이 없습니다. 3·4단계를 먼저 하세요.", Toast.LENGTH_LONG).show();
            return;
        }
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION}, 1);
            Toast.makeText(this, "위치 권한을 허용한 뒤 [시작하기]를 다시 누르세요.", Toast.LENGTH_LONG).show();
            return;
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 2);
        }
        TelegramSender.setSenderName(this, senderName.getText().toString());
        sp.edit().putInt("interval", min).putBoolean("on", true).putLong("next", 0).apply();
        HomeService.refresh(this);
        Toast.makeText(this, "텔레그램 자동 송신을 시작합니다 (" + TelegramSender.intervalLabel(min) + "마다)",
                Toast.LENGTH_LONG).show();
        finish();
    }

    // ---------- 화면 도우미 ----------

    private TextView step(String stepNo, String title) {
        TextView tv = new TextView(this);
        tv.setText(stepNo + "단계  " + title);
        tv.setTextSize(18);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextColor(0xFF1976D2);
        tv.setPadding(0, 50, 0, 10);
        return tv;
    }

    private TextView note(String text) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextSize(14);
        tv.setTextColor(Color.DKGRAY);
        tv.setPadding(0, 6, 0, 6);
        return tv;
    }

    private Button button(String label, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(label);
        btn.setTextSize(14);
        btn.setOnClickListener(listener);
        return btn;
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        return row;
    }

    private static LinearLayout.LayoutParams weight() {
        return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
    }

    // 토큰 확인 후에만 쓸 수 있는 단계 묶음
    private LinearLayout section(LinearLayout root) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        root.addView(box);
        afterToken.add(box);
        return box;
    }

    private void copy(String text) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        cm.setPrimaryClip(ClipData.newPlainText("text", text));
        Toast.makeText(this, "'" + text + "' 를 복사했습니다. BotFather 대화창에 붙여 넣으세요.", Toast.LENGTH_SHORT).show();
    }

    // 텔레그램 앱(또는 스토어)으로 열기, 실패하면 대체 주소(브라우저)
    private void open(String url, String fallback) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (Exception e) {
            if (fallback != null) {
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(fallback)));
                    return;
                } catch (Exception ignored) {
                }
            }
            Toast.makeText(this, "열 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }
}
