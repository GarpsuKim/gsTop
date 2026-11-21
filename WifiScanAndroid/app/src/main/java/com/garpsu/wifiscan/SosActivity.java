package com.garpsu.wifiscan;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

/**
 * 긴급 SOS (홈 화면 바로가기에서 실행).
 * 실수로 눌렸을 때를 대비해 3초 카운트다운 -> [취소] 가능 -> 보호자 전화 + 위치 전송.
 * (앱 화면의 [SOS] 버튼은 2초 길게 누르기로 확인하므로 이 화면 없이 바로 실행)
 */
public class SosActivity extends Activity {

    static final String PREF_SOS = "sos"; // phones: 보호자 번호(쉼표 구분, 첫 번째가 SOS 전화 대상)
    private static final int COUNT = 3;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView count;
    private int left = COUNT;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            left--;
            if (left <= 0) {
                fire(SosActivity.this);
                finish();
                return;
            }
            count.setText(String.valueOf(left));
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(0xFFC62828);
        root.setPadding(60, 60, 60, 60);

        TextView t = new TextView(this);
        t.setText("🆘 SOS");
        t.setTextSize(48);
        t.setTypeface(null, Typeface.BOLD);
        t.setTextColor(Color.WHITE);
        t.setGravity(Gravity.CENTER);
        root.addView(t);

        TextView m = new TextView(this);
        String first = firstPhone(this);
        m.setText(first.isEmpty()
                ? "보호자에게 위치를 보냅니다.\n(보호자 전화번호가 등록되지 않아 전화는 걸지 않습니다)"
                : "보호자(" + first + ")에게 전화하고\n위치를 보냅니다.");
        m.setTextSize(18);
        m.setTextColor(Color.WHITE);
        m.setGravity(Gravity.CENTER);
        m.setPadding(0, 30, 0, 30);
        root.addView(m);

        count = new TextView(this);
        count.setText(String.valueOf(COUNT));
        count.setTextSize(96);
        count.setTypeface(null, Typeface.BOLD);
        count.setTextColor(Color.WHITE);
        count.setGravity(Gravity.CENTER);
        root.addView(count);

        Button cancel = new Button(this);
        cancel.setText("취소");
        cancel.setTextSize(28);
        cancel.setOnClickListener(v -> {
            handler.removeCallbacks(tick);
            Toast.makeText(this, "SOS 를 취소했습니다.", Toast.LENGTH_SHORT).show();
            finish();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 40;
        root.addView(cancel, lp);

        setContentView(root);
        handler.postDelayed(tick, 1000);
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(tick);
        super.onDestroy();
    }

    // ---------- 공용 (앱 화면의 [SOS] 버튼도 사용) ----------

    static String phones(Context c) {
        return c.getSharedPreferences(PREF_SOS, MODE_PRIVATE).getString("phones", "").trim();
    }

    static String firstPhone(Context c) {
        for (String p : phones(c).split("[,;]+")) {
            if (!p.trim().isEmpty()) {
                return p.trim();
            }
        }
        return "";
    }

    // SOS 실행: 위치 전송(서비스) + 첫 번째 보호자에게 전화
    static void fire(Context c) {
        c.getSharedPreferences(PREF_SOS, MODE_PRIVATE).edit().putLong("lastSos", System.currentTimeMillis()).apply();
        try {
            HomeService.sos(c);
        } catch (Exception e) {
            Toast.makeText(c, "위치 전송을 시작하지 못했습니다. (위치 권한 확인)", Toast.LENGTH_LONG).show();
        }
        String p = firstPhone(c);
        if (!p.isEmpty()) {
            call(c, p);
        } else {
            Toast.makeText(c, "보호자 전화번호가 없어 위치만 보냅니다.", Toast.LENGTH_LONG).show();
        }
    }

    // 전화 걸기: '전화 걸기' 권한이 있으면 바로 통화, 없으면 번호가 입력된 전화 화면
    static void call(Context c, String phone) {
        String num = phone.replaceAll("[^0-9+]", "");
        boolean direct = c.checkSelfPermission(Manifest.permission.CALL_PHONE) == PackageManager.PERMISSION_GRANTED;
        Intent i = new Intent(direct ? Intent.ACTION_CALL : Intent.ACTION_DIAL, Uri.parse("tel:" + num))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            c.startActivity(i);
        } catch (Exception e) {
            Toast.makeText(c, "전화를 걸 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREF_SOS, MODE_PRIVATE);
    }
}
