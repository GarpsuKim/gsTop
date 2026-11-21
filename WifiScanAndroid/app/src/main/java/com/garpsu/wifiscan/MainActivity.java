package com.garpsu.wifiscan;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.location.Address;
import android.location.Geocoder;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.DhcpInfo;
import android.net.Uri;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.text.InputType;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.app.TimePickerDialog;
import android.util.Patterns;
import android.widget.Spinner;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.FileReader;
import java.net.ConnectException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class MainActivity extends Activity {

    private static final int REQ_PERM = 1, REQ_NOTI = 2;
    private CheckBox bgCheck;
    private Button tgBtn, kakaoBtn, emailBtn, kmailBtn, senderBtn, selfBtn, smsBtn;
    private static final int REQ_CONTACT = 3;   // 연락처에서 전화번호 고르기
    private static final int REQ_BT = 4;        // 블루투스(근처 기기) 권한
    private static final int REQ_CALL = 5;      // 전화 걸기 권한 (SOS·보호자 바로 걸기)
    private LinearLayout sosCallBox;            // (2) 탭: 보호자 바로 걸기 버튼들
    private TextView sosInfo;                   // (3) 탭: SOS 설정 요약
    private LinearLayout btBox;                 // (1) 탭: 홈 PC 블루투스
    private EditText contactTarget;             // 고른 번호를 넣을 칸
    private LinearLayout msgBox; // (4) 위치 보내기 탭: 정해진 시각 위치 보내기 버튼 묶음
    private static final int MODE_NONE = 0, MODE_WIFI = 1, MODE_LOC = 3, MODE_LAN = 4;

    private WifiManager wm;
    private LocationManager lm;
    private TextView text, status, statusNote; // text: 가장 최근에 검색을 시작한 탭의 결과 화면
    private LinearLayout wifiRows; // 공유기 체크박스 목록 (탭1)
    private static final int TABS = 5; // (1) 홈 공유기 등록 (2) 현재 위치 (3) 알림 설정 (4) 위치 보내기 (5) 보호자 모드  ※ 기기 목록 탭은 삭제(Play 스토어 정책)
    private final TextView[] texts = new TextView[TABS];
    private final Button[] startBtns = new Button[TABS]; // (4) 탭에는 [검색시작]이 없음 (null)
    private final Button[] tabBtns = new Button[TABS];
    private final LinearLayout[] panels = new LinearLayout[TABS];
    private int curTab = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int mode = MODE_NONE;
    private int scanId = 0; // 기기 목록 결과가 다른 화면을 덮어쓰지 않게 구분

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
            if (mode == MODE_WIFI) {
                showWifiResults();
            }
        }
    };

    // 10초마다 저장된 공유기에 연결되어 있는지 검사
    private final Runnable homeCheck = new Runnable() {
        @Override
        public void run() {
            checkHome();
            handler.postDelayed(this, 10000);
        }
    };

    private final LocationListener locListener = new LocationListener() {
        // 구형 Android(API 29 이하)에서는 아래 3개도 구현해야 함
        @Override
        public void onStatusChanged(String provider, int status, Bundle extras) {
        }

        @Override
        public void onProviderEnabled(String provider) {
        }

        @Override
        public void onProviderDisabled(String provider) {
        }

        @Override
        public void onLocationChanged(Location loc) {
            if (mode == MODE_LOC) {
                showLocation(loc, false);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        // 상단: 좌 [초기화] / 가운데 [☐ 백그라운드 실행] / 우 [종료]
        Button reset = new Button(this);
        reset.setText("초기화");
        reset.setTextColor(0xFFC62828);
        reset.setOnClickListener(v -> confirmReset());

        bgCheck = new CheckBox(this); // 백그라운드 귀가 감지 켜기/끄기 (앱을 닫아도 상단 알림으로 표시)
        bgCheck.setText("백그라운드 실행");
        bgCheck.setTextSize(15);
        bgCheck.setTypeface(null, Typeface.BOLD);
        bgCheck.setChecked(HomeService.homeOn(this));
        bgCheck.setOnClickListener(v -> setBackground(bgCheck.isChecked()));
        LinearLayout center = new LinearLayout(this);
        center.setGravity(Gravity.CENTER);
        center.addView(bgCheck);

        Button exit = new Button(this);
        exit.setText("종료");
        exit.setOnClickListener(v -> finish());

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(reset, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        top.addView(center, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        top.addView(exit, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // HomeBack / NotHome
        status = new TextView(this);
        status.setTextSize(40); // 오른쪽에 SOS 버튼이 있어 조금 작게
        status.setTypeface(null, Typeface.BOLD);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 30, 0, 0);

        statusNote = new TextView(this);
        statusNote.setTextSize(14);
        statusNote.setTextColor(Color.GRAY);
        statusNote.setGravity(Gravity.CENTER);
        statusNote.setPadding(0, 0, 0, 20);

        // 탭 4개: 홈 공유기 등록 / 기기 목록 / 현재 위치 / 알림 설정
        final String[] names = {"(1) 홈 공유기 등록", "(2) 현재 위치", "(3) 알림 설정", "(4) 위치 보내기", "(5) 보호자 모드"};
        final Runnable[] starts = {this::wifiScan, this::location, null, null, null};

        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < TABS; i++) {
            final int idx = i;
            tabBtns[i] = addButton(tabs, names[i], v -> selectTab(idx));
            tabBtns[i].setTextSize(11); // 탭 5개라 글자를 조금 작게
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.addView(top, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        // 상태(HomeBack/NotHome) 왼쪽 + 큰 [SOS] 오른쪽 ([종료] 바로 아래)
        LinearLayout statusCol = new LinearLayout(this);
        statusCol.setOrientation(LinearLayout.VERTICAL);
        statusCol.setGravity(Gravity.CENTER);
        statusCol.addView(status, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        statusCol.addView(statusNote, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setOrientation(LinearLayout.HORIZONTAL);
        statusRow.setGravity(Gravity.CENTER_VERTICAL);
        statusRow.addView(statusCol, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        int sosSize = (int) (110 * getResources().getDisplayMetrics().density); // 약 110dp 정사각형
        LinearLayout.LayoutParams sosLp = new LinearLayout.LayoutParams(sosSize, sosSize);
        sosLp.setMargins(0, 8, 16, 8);
        statusRow.addView(makeSosButton(), sosLp);
        root.addView(statusRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(tabs, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 탭마다: [검색시작/재검색] 버튼(탭1만 [저장] 추가) + 결과 화면. (3) 알림 설정 탭은 설정 화면만
        for (int i = 0; i < TABS; i++) {
            final int idx = i;
            LinearLayout actions = new LinearLayout(this);
            actions.setOrientation(LinearLayout.HORIZONTAL);
            if (starts[i] != null) {
                startBtns[i] = addButton(actions, "검색시작", v -> starts[idx].run());
            }
            if (i == 0) {
                addButton(actions, "등록", v -> saveRouters());
                addButton(actions, "등록 확인", v -> showRegistered());
            }
            if (i == 1) { // (2) 현재 위치
                addButton(actions, "지도", v -> openMap());
                addButton(actions, "복사", v -> copyLocation());
                addButton(actions, "즉시 공유", v -> shareLocation());
            }

            texts[i] = new TextView(this);
            texts[i].setTextSize(15);
            texts[i].setPadding(40, 40, 40, 40);
            texts[i].setText(i >= 2 ? "" : "[검색시작]을 눌러 주세요.");

            LinearLayout body = new LinearLayout(this);
            body.setOrientation(LinearLayout.VERTICAL);
            if (i == 2) {
                buildAlarmTab(body); // (3) 알림 설정: 귀가·외출 순간 알림
            } else if (i == 3) {
                buildSendTab(body);  // (4) 위치 보내기: 정해진 시각 위치 보내기
            } else if (i == 4) {
                buildGuardianTab(body); // (5) 보호자 모드: 아직 준비 중 (빈 화면)
            }
            if (i == 0) {
                // (백그라운드 귀가 감지 체크는 화면 맨 위 가운데 [백그라운드 실행]으로 옮김)
                savedBox = new LinearLayout(this); // 저장된 공유기 목록 (스캔 결과 위)
                savedBox.setOrientation(LinearLayout.VERTICAL);
                body.addView(savedBox);

                btBox = new LinearLayout(this); // 홈 PC 블루투스 (페어링된 기기에서 고르기)
                btBox.setOrientation(LinearLayout.VERTICAL);
                body.addView(btBox);
                renderBt();
            }
            if (i == 1) {
                buildSosBox(body); // (2) 현재 위치 맨 위: [SOS] (2초 길게) + 보호자 바로 걸기
            }
            if (i < 2) { // (3)·(4) 탭은 결과 글자 화면 없음
                body.addView(texts[i]);
            }
            if (i == 0) {
                wifiRows = new LinearLayout(this);
                wifiRows.setOrientation(LinearLayout.VERTICAL);
                body.addView(wifiRows);
            }
            ScrollView scroll = new ScrollView(this);
            scroll.addView(body);

            panels[i] = new LinearLayout(this);
            panels[i].setOrientation(LinearLayout.VERTICAL);
            if (actions.getChildCount() > 0) { // (3)·(4) 탭은 위쪽 버튼 줄 없음
                panels[i].addView(actions, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            }
            panels[i].addView(scroll, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
            root.addView(panels[i], new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        }

        setContentView(root);

        text = texts[0];
        selectTab(0);
        checkHome(); // 실행 처음에 무조건 HomeBack / NotHome 판정
        if (loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
            showIntro(); // 홈 공유기가 아직 없으면 안내 팝업 -> 닫으면 위치 권한 요청
        } else {
            ensurePermission(); // 실행 처음에 위치 권한 요청 (공유기 연결 검사에 필요)
        }
        if (HomeService.anyOn(this) && hasPermission()) {
            HomeService.refresh(this); // 혹시 종료되어 있으면 다시 시작
        }
    }

    // ---------- [초기화]: 모든 설정 삭제 -> 처음 상태 ----------

    // 이 앱이 쓰는 설정 저장소 전부
    private static final String[] ALL_PREFS = {"home", "lan", TelegramSender.PREF, TelegramSender.PREF_APP,
            HomeService.PREF_KAKAO, HomeService.PREF_EMAIL, HomeService.PREF_KMAIL,
            HomeService.PREF_SMS, HomeService.PREF_SELF, HomeService.PREF_EVENT, SosActivity.PREF_SOS};

    private void confirmReset() {
        new AlertDialog.Builder(this)
                .setTitle("초기화")
                .setMessage("모든 설정을 지우고 처음 설치한 상태로 되돌립니다.\n\n"
                        + "지워지는 것:\n"
                        + " · 홈 공유기, 홈 PC 블루투스 등록\n"
                        + " · 백그라운드 실행, 귀가·외출 알림\n"
                        + " · 위치 보내기 설정 (받는 사람, 텔레그램 봇 토큰, 이메일 앱 비밀번호 등)\n"
                        + " · 보내는 사람 별명, 기기 목록 기록\n\n"
                        + "되돌릴 수 없습니다. 계속할까요?")
                .setPositiveButton("초기화", (d, w) -> resetAll())
                .setNegativeButton("취소", null)
                .show();
    }

    private void resetAll() {
        HomeService.stopAll(this); // 백그라운드 서비스·예약 알람·알림 모두 정리
        for (String p : ALL_PREFS) {
            getSharedPreferences(p, MODE_PRIVATE).edit().clear().commit(); // 화면 다시 그리기 전에 확실히 저장
        }
        // 기본값: 귀가·외출 알림의 보낼 곳은 '본인 폰'만 체크 (나머지 값은 각 화면의 기본값 사용)
        getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE).edit().putBoolean("ch_self", true).commit();
        Toast.makeText(this, "초기화했습니다.", Toast.LENGTH_SHORT).show();
        recreate(); // 화면을 처음부터 다시 그림 (홈 공유기 등록 안내 팝업도 다시 나옴)
    }

    // 백그라운드 귀가 감지 켜기/끄기
    private void setBackground(boolean on) {
        if (on) {
            if (!hasPermission()) {
                bgCheck.setChecked(false);
                ensurePermission();
                Toast.makeText(this, "위치 권한을 허용한 뒤 다시 켜 주세요.", Toast.LENGTH_LONG).show();
                return;
            }
            if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
            }
        }
        getSharedPreferences("home", MODE_PRIVATE).edit().putBoolean(HomeService.PREF_BG, on).apply();
        if (on) {
            HomeService.refresh(this);
            Toast.makeText(this, loadRouters().isEmpty()
                    ? "켜졌습니다. 홈 공유기를 먼저 등록하세요."
                    : "켜졌습니다. 앱을 닫아도 귀가를 감지합니다.", Toast.LENGTH_LONG).show();
        } else {
            HomeService.refresh(this);
            Toast.makeText(this, "백그라운드 귀가 감지를 껐습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    // 시작 안내 팝업 (홈 공유기를 등록할 때까지 실행할 때마다 표시)
    private void showIntro() {
        new AlertDialog.Builder(this)
                .setTitle("집에왔어요 · 홈 공유기 등록")
                .setMessage("홈 공유기를 등록하세요.\n\n귀가 할 때 마다 자동으로 검색하여, "
                        + "\"HomeBack\" 또는 \"NotHome\" 상태를 확인합니다.\n\n"
                        + "어린이, 노약자, 치매어르신 들의 안심 귀가 확인에 유용합니다.")
                .setPositiveButton("확인", null)
                .setOnDismissListener(d -> ensurePermission())
                .show();
    }

    private void selectTab(int sel) {
        curTab = sel;
        for (int i = 0; i < TABS; i++) {
            panels[i].setVisibility(i == sel ? View.VISIBLE : View.GONE);
            tabBtns[i].setBackgroundColor(i == sel ? 0xFF1976D2 : 0xFFDDDDDD);
            tabBtns[i].setTextColor(i == sel ? Color.WHITE : Color.BLACK);
        }
    }

    private Button addButton(LinearLayout row, String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(13);
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(4, 0, 4, 0);
        b.setOnClickListener(l);
        row.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return b;
    }

    @Override
    protected void onResume() {
        super.onResume();
        IntentFilter filter = new IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(receiver, filter);
        }
        handler.post(homeCheck); // 실행 즉시 검사 후 주기적으로 반복
        if (tgBtn != null) {
            refreshMsgButtons(); // 설정 도우미 등에서 돌아왔을 때 체크·글자 갱신
            updateSenderButton();
            updateEventInfo();
        }
        renderBt(); // 블루투스 설정(페어링)에서 돌아왔을 때 목록 갱신
        renderSosCalls(); // 보호자 번호가 바뀌었을 수 있음
        updateRemoteInfo();
        updateSosInfo();
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterReceiver(receiver);
        handler.removeCallbacks(homeCheck);
        lm.removeUpdates(locListener);
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code == REQ_CALL) {
            Toast.makeText(this, results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED
                    ? "이제 SOS·보호자 버튼이 바로 전화를 겁니다."
                    : "권한이 없으면 전화 화면까지만 띄웁니다. (통화 버튼은 직접 누름)", Toast.LENGTH_LONG).show();
        } else if (code == REQ_BT) {
            if (BtHome.hasPerm(this)) {
                renderBt(); // 권한을 받았으면 페어링된 기기 목록 표시
            } else {
                Toast.makeText(this, "블루투스(근처 기기) 권한이 있어야 PC를 찾을 수 있습니다.", Toast.LENGTH_LONG).show();
            }
        } else if (code == REQ_NOTI) {
            if (results.length > 0 && results[0] != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "알림 권한이 없으면 귀가 알림이 보이지 않습니다.", Toast.LENGTH_LONG).show();
            }
        } else if (code == REQ_PERM) {
            if (hasPermission()) {
                texts[curTab].setText("[검색시작]을 눌러 주세요.");
                checkHome();
                if (HomeService.anyOn(this)) {
                    HomeService.refresh(this);
                }
            } else {
                texts[curTab].setText("위치 권한이 필요합니다.\n(Wi-Fi 스캔과 위치 확인에 필요합니다.)");
            }
        }
    }

    private boolean hasPermission() {
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    // 권한이 없으면 요청하고 false 반환
    private boolean ensurePermission() {
        if (hasPermission()) {
            return true;
        }
        // Android 12 이상은 FINE 과 COARSE 를 함께 요청해야 함
        requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION}, REQ_PERM);
        return false;
    }

    // 검색 시작: 해당 탭의 결과 화면을 지우고, 버튼 이름을 [재검색]으로 변경
    private void begin(int newMode, String msg) {
        mode = newMode;
        scanId++;
        lm.removeUpdates(locListener);
        int idx = newMode == MODE_WIFI ? 0 : 1; // (1) 홈 공유기 등록 / (2) 현재 위치
        text = texts[idx];
        startBtns[idx].setText("재검색");
        if (newMode == MODE_WIFI) {
            wifiRows.removeAllViews();
            apChecks.clear();
        }
        text.setText(msg);
    }

    // ---------- 홈 공유기 등록 ----------

    private void wifiScan() {
        if (!ensurePermission()) {
            return;
        }
        begin(MODE_WIFI, "스캔 중...");
        if (!wm.isWifiEnabled()) {
            text.setText("Wi-Fi가 꺼져 있습니다.");
            return;
        }
        if (!wm.startScan()) {
            showWifiResults(); // 스캔 횟수 제한 시 마지막 결과 표시
        }
    }

    private static final int C_MARK = 0xFF1976D2;   // ■ 표시 (파랑)
    private static final int C_GOOD = 0xFF2E7D32;   // 강 (초록)
    private static final int C_MID = 0xFFEF6C00;    // 중 (주황)
    private static final int C_BAD = 0xFFC62828;    // 약 (빨강)
    private static final int C_GRAY = 0xFF9E9E9E;

    private final List<CheckBox> apChecks = new ArrayList<>(); // 공유기(BSSID)별 체크박스
    private final Map<String, Integer> seenLevel = new HashMap<>(); // 마지막 스캔: BSSID -> dBm
    private boolean wifiScanned;
    private LinearLayout savedBox;
    private String savedConn = "-"; // 저장 목록을 그릴 때의 연결 공유기 (바뀌면 다시 그림)

    private void showWifiResults() {
        if (!hasPermission()) {
            return;
        }
        List<ScanResult> list = new ArrayList<>(wm.getScanResults());
        Collections.sort(list, (a, b) -> b.level - a.level);

        // 재스캔 결과가 도착해도 사용자가 바꾼 체크 상태를 유지
        Map<String, Boolean> cur = new HashMap<>();
        for (CheckBox cb : apChecks) {
            cur.put(((String[]) cb.getTag())[0], cb.isChecked());
        }
        Map<String, String> saved = loadRouters();
        String conn = connectedBssid();

        // 같은 이름(SSID)끼리 묶기 (신호 센 그룹 순서, 연결 중인 그룹은 맨 위)
        Map<String, List<ScanResult>> groups = new LinkedHashMap<>();
        seenLevel.clear();
        String connSsid = null;
        for (ScanResult r : list) {
            String ssid = ssidOf(r);
            String bssid = r.BSSID.toLowerCase(Locale.US);
            seenLevel.put(bssid, r.level);
            if (bssid.equals(conn)) {
                connSsid = ssid;
            }
            List<ScanResult> g = groups.get(ssid);
            if (g == null) {
                g = new ArrayList<>();
                groups.put(ssid, g);
            }
            g.add(r);
        }
        wifiScanned = true;
        List<String> order = new ArrayList<>(groups.keySet());
        if (connSsid != null) {
            order.remove(connSsid);
            order.add(0, connSsid);
        }

        wifiRows.removeAllViews();
        apChecks.clear();
        text.setText("[ 공유기 " + list.size() + "개 · 이름 " + groups.size() + "개 ]  " + now()
                + (list.isEmpty()
                ? "\n\n결과가 없습니다.\n위치(GPS) 서비스가 켜져 있는지 확인하세요."
                : "\n체크한 공유기를 [등록]으로 등록하세요.\n(이름을 체크하면 같은 이름의 공유기가 모두 체크됩니다)"));

        for (String ssid : order) {
            List<ScanResult> g = groups.get(ssid);
            boolean connGroup = ssid.equals(connSsid);

            LinearLayout box = new LinearLayout(this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding(20, 16, 20, 16);
            if (connGroup) {
                box.setBackgroundColor(0x1A2E7D32);
            }

            // 그룹 머리: ■ 이름 (n개) 📶 연결됨
            final CheckBox head = new CheckBox(this);
            SpannableStringBuilder h = new SpannableStringBuilder();
            append(h, "■ ", C_MARK, true);
            append(h, ssid, 0, true);
            if (g.size() > 1) {
                append(h, "  (" + g.size() + "개)", C_GRAY, false);
            }
            if (connGroup) {
                append(h, "  📶 연결됨", C_GOOD, true);
            }
            head.setText(h);
            head.setTextSize(17);
            box.addView(head);

            final List<CheckBox> kids = new ArrayList<>();
            for (ScanResult r : g) {
                String bssid = r.BSSID.toLowerCase(Locale.US);
                boolean isConn = bssid.equals(conn);
                CheckBox cb = new CheckBox(this);
                cb.setTag(new String[]{bssid, ssid});
                cb.setChecked(cur.containsKey(bssid) ? cur.get(bssid) : saved.containsKey(bssid));

                SpannableStringBuilder s = new SpannableStringBuilder();
                append(s, bssid, 0, false);
                if (isConn) {
                    append(s, "  📶 연결됨", C_GOOD, true);
                }
                s.append('\n');
                appendBars(s, r.level);
                s.append(" ").append(String.valueOf(r.level)).append(" dBm  ")
                        .append(band(r.frequency)).append("  ").append(security(r.capabilities));
                cb.setText(s);
                cb.setTextSize(14);
                cb.setOnCheckedChangeListener((b, c) -> head.setChecked(allChecked(kids)));

                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = 70;
                box.addView(cb, lp);
                kids.add(cb);
                apChecks.add(cb);
            }
            head.setChecked(allChecked(kids));
            head.setOnClickListener(v -> {
                boolean c = head.isChecked();
                for (CheckBox k : kids) {
                    k.setChecked(c);
                }
            });
            wifiRows.addView(box);
        }
        renderSaved();
    }

    private static String ssidOf(ScanResult r) {
        return (r.SSID == null || r.SSID.isEmpty()) ? "<숨김>" : r.SSID;
    }

    private static boolean allChecked(List<CheckBox> list) {
        for (CheckBox c : list) {
            if (!c.isChecked()) {
                return false;
            }
        }
        return !list.isEmpty();
    }

    private static void append(SpannableStringBuilder sb, String s, int color, boolean bold) {
        int st = sb.length();
        sb.append(s);
        if (color != 0) {
            sb.setSpan(new ForegroundColorSpan(color), st, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        if (bold) {
            sb.setSpan(new StyleSpan(Typeface.BOLD), st, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
    }

    // 신호 세기 막대 ▂▄▆█ (강: 초록, 중: 주황, 약: 빨강) + 강/중/약 글자
    private static void appendBars(SpannableStringBuilder sb, int level) {
        int n = level >= -55 ? 4 : level >= -67 ? 3 : level >= -78 ? 2 : 1;
        int color = n >= 3 ? C_GOOD : n == 2 ? C_MID : C_BAD;
        String bars = "▂▄▆█";
        append(sb, bars.substring(0, n), color, true);
        append(sb, bars.substring(n), 0xFFDDDDDD, true);
        append(sb, n >= 3 ? " 강" : n == 2 ? " 중" : " 약", color, true);
    }

    // 지금 연결된 공유기의 BSSID (없거나 알 수 없으면 null)
    private String connectedBssid() {
        if (!hasPermission() || !wm.isWifiEnabled()) {
            return null;
        }
        String b = wm.getConnectionInfo().getBSSID();
        if (b == null || b.equals("02:00:00:00:00:00")) {
            return null; // 위치 서비스가 꺼져 있으면 가짜 값이 옴
        }
        return b.toLowerCase(Locale.US);
    }

    // 저장된 공유기 목록 (주변에 안 보여도 표시, [삭제] 가능)
    private void renderSaved() {
        String conn = connectedBssid();
        savedConn = String.valueOf(conn);
        savedBox.removeAllViews();
        Map<String, String> saved = loadRouters();

        TextView h = new TextView(this);
        h.setText("저장된 공유기 (" + saved.size() + "개)");
        h.setTextSize(16);
        h.setTypeface(null, Typeface.BOLD);
        h.setPadding(40, 30, 40, 10);
        savedBox.addView(h);

        if (saved.isEmpty()) {
            TextView e = new TextView(this);
            e.setText("없음 — 아래 검색 결과에서 체크 후 [등록]을 누르세요.");
            e.setTextColor(C_GRAY);
            e.setPadding(40, 0, 40, 10);
            savedBox.addView(e);
        }

        List<Map.Entry<String, String>> items = new ArrayList<>(saved.entrySet());
        Collections.sort(items, (a, b) -> {
            int c = a.getValue().compareTo(b.getValue());
            return c != 0 ? c : a.getKey().compareTo(b.getKey());
        });
        for (Map.Entry<String, String> it : items) {
            final String bssid = it.getKey();
            String ssid = it.getValue().isEmpty() ? "<숨김>" : it.getValue();

            SpannableStringBuilder s = new SpannableStringBuilder();
            append(s, "■ ", C_MARK, true);
            append(s, ssid, 0, true);
            s.append("\n").append(bssid).append("\n");
            Integer lv = seenLevel.get(bssid);
            if (bssid.equals(conn)) {
                append(s, "📶 연결됨  ", C_GOOD, true);
            }
            if (lv != null) {
                appendBars(s, lv);
                s.append(" ").append(String.valueOf(lv)).append(" dBm");
            } else if (!bssid.equals(conn)) {
                append(s, wifiScanned ? "주변에 없음" : "검색 전", C_GRAY, false);
            }

            TextView tv = new TextView(this);
            tv.setText(s);
            tv.setTextSize(14);
            Button del = new Button(this);
            del.setText("삭제");
            del.setTextSize(13);
            del.setOnClickListener(v -> deleteRouter(bssid));

            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(40, 10, 20, 10);
            if (bssid.equals(conn)) {
                row.setBackgroundColor(0x1A2E7D32);
            }
            row.addView(tv, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            row.addView(del, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            savedBox.addView(row);
        }

        View line = new View(this);
        line.setBackgroundColor(0xFFDDDDDD);
        savedBox.addView(line, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 3));
    }

    // [등록 확인]: 등록된 홈 공유기와 홈 PC 블루투스만 모아서 보여 줌
    private void showRegistered() {
        String conn = connectedBssid();
        SpannableStringBuilder s = new SpannableStringBuilder();

        Map<String, String> routers = loadRouters();
        append(s, "■ 홈 공유기 (" + routers.size() + "개)\n", C_MARK, true);
        if (routers.isEmpty()) {
            append(s, "  없음 — [검색시작] 후 체크하고 [등록]을 누르세요.\n", C_GRAY, false);
        }
        List<Map.Entry<String, String>> items = new ArrayList<>(routers.entrySet());
        Collections.sort(items, (a, b) -> a.getValue().compareTo(b.getValue()));
        for (Map.Entry<String, String> it : items) {
            String bssid = it.getKey();
            append(s, "  " + (it.getValue().isEmpty() ? "<숨김>" : it.getValue()), 0, true);
            append(s, "   " + bssid, C_GRAY, false);
            Integer lv = seenLevel.get(bssid);
            if (bssid.equals(conn)) {
                append(s, "  📶 연결됨", C_GOOD, true);
            } else if (lv != null) {
                append(s, "  주변에 있음 " + lv + " dBm", C_MID, false);
            }
            s.append('\n');
        }

        Set<String> bts = BtHome.registered(this);
        s.append('\n');
        append(s, "■ 홈 PC 블루투스 (" + bts.size() + "개)\n", C_MARK, true);
        if (bts.isEmpty()) {
            append(s, "  없음 — 아래 '홈 PC 블루투스'에서 페어링된 PC를 체크하세요.\n", C_GRAY, false);
        }
        SharedPreferences hp = getSharedPreferences("home", MODE_PRIVATE);
        long seen = hp.getLong("btSeenAt", 0);
        String seenName = hp.getString("btSeenName", "");
        for (String b : bts) {
            String[] t = b.split("\\|", 2);
            String name = t.length > 1 ? t[1] : t[0];
            append(s, "  " + name, 0, true);
            append(s, "   " + t[0], C_GRAY, false);
            if (name.equals(seenName) && seen > 0) {
                append(s, "  마지막 확인 " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(seen)),
                        BtHome.seenRecently(this) ? C_GOOD : C_GRAY, false);
            }
            s.append('\n');
        }

        s.append('\n');
        boolean home = status.getText().toString().equals("HomeBack");
        append(s, "지금 상태: " + (home ? "HomeBack (집)" : "NotHome (외출)"), home ? C_GOOD : C_BAD, true);

        TextView tv = new TextView(this);
        tv.setText(s);
        tv.setTextSize(14);
        tv.setPadding(50, 30, 50, 10);
        tv.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        new AlertDialog.Builder(this)
                .setTitle("등록 확인")
                .setView(sv)
                .setPositiveButton("확인", null)
                .show();
    }

    // ---------- 홈 PC 블루투스 (페어링된 PC가 가까이 있으면 '집') ----------

    private TextView btNote(String s, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(13);
        t.setTextColor(color);
        t.setPadding(40, 4, 40, 4);
        btBox.addView(t);
        return t;
    }

    private Button btButton(LinearLayout row, String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        row.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return b;
    }

    private void renderBt() {
        if (btBox == null) {
            return;
        }
        btBox.removeAllViews();
        TextView h = new TextView(this);
        h.setText("홈 PC 블루투스 (" + BtHome.registered(this).size() + "개 등록)");
        h.setTextSize(16);
        h.setTypeface(null, Typeface.BOLD);
        h.setPadding(40, 30, 40, 6);
        btBox.addView(h);
        btNote("폰과 PC를 미리 페어링한 뒤 아래에서 PC를 체크하세요.\n"
                + "PC가 가까이 있으면 Wi-Fi가 꺼져 있어도 HomeBack으로 판정합니다. (PC 블루투스가 켜져 있어야 함)",
                Color.GRAY);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(30, 0, 30, 0);

        if (BtHome.adapter(this) == null) {
            btNote("이 폰에는 블루투스가 없습니다.", 0xFFC62828);
        } else if (!BtHome.hasPerm(this)) {
            btButton(row, "블루투스 기기 불러오기 (권한 허용)", v -> requestPermissions(
                    new String[]{Manifest.permission.BLUETOOTH_CONNECT}, REQ_BT));
        } else {
            if (!BtHome.enabled(this)) {
                btNote("블루투스가 꺼져 있습니다. 켜야 PC를 찾을 수 있습니다.", 0xFFC62828);
            }
            final Set<String> reg = BtHome.registered(this);
            List<String[]> list = BtHome.bonded(this);
            if (list.isEmpty()) {
                btNote("페어링된 기기가 없습니다. [블루투스 설정]에서 PC와 먼저 페어링하세요.", Color.DKGRAY);
            }
            for (final String[] d : list) {
                boolean on = false;
                for (String s : reg) {
                    if (s.startsWith(d[0] + "|")) {
                        on = true;
                    }
                }
                final CheckBox cb = new CheckBox(this);
                SpannableStringBuilder s = new SpannableStringBuilder();
                append(s, "■ ", C_MARK, true);
                append(s, d[1], 0, true);
                append(s, "\n" + d[0], C_GRAY, false);
                cb.setText(s);
                cb.setTextSize(14);
                cb.setChecked(on);
                cb.setOnClickListener(v -> {
                    Set<String> set = BtHome.registered(this);
                    java.util.Iterator<String> it = set.iterator();
                    while (it.hasNext()) {
                        if (it.next().startsWith(d[0] + "|")) {
                            it.remove();
                        }
                    }
                    if (cb.isChecked()) {
                        set.add(d[0] + "|" + d[1]);
                    }
                    BtHome.save(this, set);
                    Toast.makeText(this, cb.isChecked() ? d[1] + " 을(를) 홈 PC로 등록했습니다."
                            : d[1] + " 등록을 해제했습니다.", Toast.LENGTH_SHORT).show();
                    renderBt();
                    HomeService.refresh(this);
                    checkHome();
                });
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = 40;
                btBox.addView(cb, lp);
            }
            btButton(row, "블루투스 설정 (페어링)", v -> {
                try {
                    startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS));
                } catch (Exception e) {
                    Toast.makeText(this, "블루투스 설정을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
            btButton(row, "지금 확인", v -> checkBtNow());
        }
        btBox.addView(row);

        View line = new View(this);
        line.setBackgroundColor(0xFFDDDDDD);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 3);
        lp.topMargin = 16;
        btBox.addView(line, lp);
    }

    // [지금 확인]: 등록한 PC가 가까이 있는지 바로 찾아봄 (몇 초 걸림)
    private void checkBtNow() {
        if (BtHome.registered(this).isEmpty()) {
            Toast.makeText(this, "먼저 위 목록에서 홈 PC를 체크하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (!BtHome.enabled(this)) {
            Toast.makeText(this, "블루투스를 켜 주세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Toast.makeText(this, "홈 PC를 찾는 중... (몇 초 걸립니다)", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            final String name = BtHome.findAny(this);
            runOnUiThread(() -> {
                if (name != null) {
                    BtHome.markSeen(this, name);
                    Toast.makeText(this, "✓ 홈 PC를 찾았습니다: " + name, Toast.LENGTH_LONG).show();
                    HomeService.refresh(this);
                } else {
                    Toast.makeText(this, "✗ 홈 PC를 찾지 못했습니다.\nPC 블루투스가 켜져 있는지, 가까이 있는지 확인하세요.",
                            Toast.LENGTH_LONG).show();
                }
                checkHome();
            });
        }).start();
    }

    private void deleteRouter(String bssid) {
        Map<String, String> saved = loadRouters();
        saved.remove(bssid);
        writeRouters(saved);
        for (CheckBox cb : apChecks) {
            if (((String[]) cb.getTag())[0].equals(bssid)) {
                cb.setChecked(false);
            }
        }
        Toast.makeText(this, "삭제했습니다 (" + saved.size() + "개 남음)", Toast.LENGTH_SHORT).show();
        renderSaved();
        checkHome();
    }

    private static String band(int f) {
        if (f >= 2400 && f < 2500) {
            return "2.4GHz ch" + (f == 2484 ? 14 : (f - 2407) / 5);
        }
        if (f >= 4900 && f < 5900) {
            return "5GHz ch" + (f - 5000) / 5;
        }
        if (f >= 5925) {
            return "6GHz ch" + (f - 5950) / 5;
        }
        return f + "MHz";
    }

    private static String security(String cap) {
        if (cap == null) return "?";
        if (cap.contains("SAE")) return "WPA3";
        if (cap.contains("WPA2")) return "WPA2";
        if (cap.contains("WPA")) return "WPA";
        if (cap.contains("WEP")) return "WEP";
        return "개방";
    }

    // ---------- 공유기 저장 / 집 감지 ----------

    // 저장된 공유기 (BSSID -> SSID)
    private Map<String, String> loadRouters() {
        Map<String, String> map = new HashMap<>();
        Set<String> set = getSharedPreferences("home", MODE_PRIVATE)
                .getStringSet("routers", new HashSet<String>());
        for (String s : set) {
            String[] t = s.split("\\|", 2);
            map.put(t[0], t.length > 1 ? t[1] : "");
        }
        return map;
    }

    // 화면에 나온 공유기 중 체크한 것은 저장, 체크 해제한 것은 삭제
    private void saveRouters() {
        if (apChecks.isEmpty()) {
            Toast.makeText(this, "먼저 [검색시작]으로 공유기를 찾으세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Map<String, String> saved = loadRouters();
        for (CheckBox cb : apChecks) {
            String[] t = (String[]) cb.getTag();
            if (cb.isChecked()) {
                saved.put(t[0], t[1]);
            } else {
                saved.remove(t[0]);
            }
        }
        writeRouters(saved);
        Toast.makeText(this, "등록했습니다 (홈 공유기 " + saved.size() + "개)", Toast.LENGTH_SHORT).show();
        renderSaved();
        checkHome();
    }

    private void writeRouters(Map<String, String> saved) {
        Set<String> set = new HashSet<>();
        for (Map.Entry<String, String> e : saved.entrySet()) {
            set.add(e.getKey() + "|" + e.getValue());
        }
        getSharedPreferences("home", MODE_PRIVATE).edit().putStringSet("routers", set).apply();
    }

    // 지금 연결된 공유기가 저장된 공유기(BSSID)인지 검사 -> 항상 HomeBack 또는 NotHome 표시
    private void checkHome() {
        Map<String, String> saved = loadRouters();
        boolean btReg = !BtHome.registered(this).isEmpty();
        boolean home = false;
        String note = "";
        if (saved.isEmpty() && !btReg) {
            note = "(저장된 공유기가 없습니다)";
        } else if (!hasPermission() && !btReg) {
            note = "(위치 권한이 필요합니다)";
        } else {
            String cur = hasPermission() ? connectedBssid() : null;
            home = cur != null && saved.containsKey(cur);
        }
        // 공유기로 '집'이 아니어도, 최근 3분 안에 홈 PC 블루투스를 찾았으면 '집'
        if (!home && BtHome.seenRecently(this)) {
            home = true;
            note = "(홈 PC 블루투스 확인됨: "
                    + getSharedPreferences("home", MODE_PRIVATE).getString("btSeenName", "") + ")";
        }
        // 연결된 공유기가 바뀌었으면 저장 목록의 '연결됨' 표시 갱신
        if (!savedConn.equals(String.valueOf(connectedBssid()))) {
            renderSaved();
        }
        status.setText(home ? "HomeBack" : "NotHome");
        status.setTextColor(home ? Color.rgb(46, 125, 50) : Color.rgb(198, 40, 40));
        statusNote.setText(note);
        statusNote.setVisibility(note.isEmpty() ? View.GONE : View.VISIBLE);
    }

    // ---------- 기기 목록 (공유기 대역의 모든 기기, WakeMeOnLan 흉내) ----------

    private static final int[] PROBE = {80, 443, 22, 445, 139, 53, 8080, 62078, 7};

    private static class Dev {
        int num;
        String ip, name = "", mac = "";
        boolean on;
    }

    private void lanScan() {
        begin(MODE_LAN, "기기 탐색 중...");
        DhcpInfo di = wm.getDhcpInfo();
        if (di == null || di.gateway == 0 || di.ipAddress == 0) {
            text.setText("Wi-Fi에 연결되어 있지 않습니다.");
            return;
        }
        // DhcpInfo 값은 리틀엔디언이므로 뒤집어서 사용
        final int my = Integer.reverseBytes(di.ipAddress);
        final int gw = Integer.reverseBytes(di.gateway);
        int m = Integer.reverseBytes(di.netmask);
        if (m == 0 || Integer.bitCount(m) < 24) {
            m = 0xffffff00; // 너무 큰 대역은 /24로 제한
        }
        final int mask = m;
        final int base = my & mask;
        final int last = base | ~mask; // 브로드캐스트 주소
        final String net = toIp(base) + "/" + Integer.bitCount(mask);
        final int id = scanId;
        final SharedPreferences sp = getSharedPreferences("lan", MODE_PRIVATE);
        final Set<String> seen = new HashSet<>(sp.getStringSet("seen", new HashSet<String>()));

        new Thread(() -> {
            ExecutorService ex = Executors.newFixedThreadPool(64);
            try {
                // 1) 응답하는 기기 찾기
                List<Integer> nums = new ArrayList<>();
                List<Future<Boolean>> fs = new ArrayList<>();
                for (int a = base + 1; a < last; a++) {
                    if (a == my) {
                        continue;
                    }
                    final String ip = toIp(a);
                    nums.add(a);
                    fs.add(ex.submit(() -> alive(ip)));
                }
                Map<Integer, Dev> devs = new HashMap<>();
                for (int i = 0; i < fs.size(); i++) {
                    try {
                        if (fs.get(i).get()) {
                            addDev(devs, nums.get(i), true);
                        }
                    } catch (Exception ignored) {
                    }
                }
                addDev(devs, my, true); // 내 폰
                addDev(devs, gw, true); // 공유기

                // 2) 이름(역방향 DNS), MAC(ARP 테이블)
                Map<String, String> arp = readArp();
                Map<Integer, Future<String>> names = new HashMap<>();
                for (Dev d : devs.values()) {
                    final String ip = d.ip;
                    names.put(d.num, ex.submit(() -> hostName(ip)));
                }
                for (Dev d : devs.values()) {
                    try {
                        d.name = names.get(d.num).get();
                    } catch (Exception ignored) {
                    }
                    String mac = arp.get(d.ip);
                    if (mac != null) {
                        d.mac = mac;
                    }
                }

                // 3) 예전에 발견했지만 지금 응답 없는 기기는 OFF(빨강)로 표시
                for (String s : seen) {
                    String[] t = s.split("\\|", -1);
                    if (t.length < 3) {
                        continue;
                    }
                    int n = ipToInt(t[0]);
                    if (n == 0 || (n & mask) != base) {
                        continue;
                    }
                    Dev d = devs.get(n);
                    if (d == null) {
                        d = addDev(devs, n, false);
                    }
                    if (d.name.isEmpty()) {
                        d.name = t[1];
                    }
                    if (d.mac.isEmpty()) {
                        d.mac = t[2];
                    }
                }
                Set<String> save = new HashSet<>();
                for (Dev d : devs.values()) {
                    save.add(d.ip + "|" + d.name + "|" + d.mac);
                }
                sp.edit().putStringSet("seen", save).apply();

                // 4) 표시 (IP 내림차순)
                List<Dev> list = new ArrayList<>(devs.values());
                Collections.sort(list, (x, y) -> Integer.compare(y.num, x.num));
                int on = 0;
                StringBuilder sb = new StringBuilder();
                for (Dev d : list) {
                    if (d.on) {
                        on++;
                    }
                    sb.append(d.on ? "🟢 " : "🔴 ").append(d.ip);
                    if (d.num == gw) {
                        sb.append(" (공유기)");
                    }
                    if (d.num == my) {
                        sb.append(" (내 폰)");
                    }
                    if (!d.name.isEmpty()) {
                        sb.append("  ").append(d.name);
                    }
                    sb.append('\n');
                    if (!d.mac.isEmpty()) {
                        sb.append("      ").append(d.mac).append('\n');
                    }
                }
                String head = "[ " + net + " ]  " + list.size() + " item(s)  🟢 " + on
                        + "  🔴 " + (list.size() - on) + "  " + now() + "\n";
                // (Android 10 이상 MAC 주소 안내는 탭 맨 위 빨간 안내 문구로 옮김)
                show(id, head + "\n" + sb);
            } finally {
                ex.shutdownNow();
            }
        }).start();
    }

    private static Dev addDev(Map<Integer, Dev> devs, int num, boolean on) {
        Dev d = devs.get(num);
        if (d == null) {
            d = new Dev();
            d.num = num;
            d.ip = toIp(num);
            devs.put(num, d);
        }
        d.on = d.on || on;
        return d;
    }

    // 화면 갱신 (다른 버튼으로 화면이 바뀐 뒤에는 무시)
    private void show(final int id, final String s) {
        runOnUiThread(() -> {
            if (id == scanId) {
                text.setText(s);
            }
        });
    }

    private static boolean alive(String ip) {
        for (int p : PROBE) {
            try (Socket s = new Socket()) {
                s.connect(new InetSocketAddress(ip, p), 300);
                return true;
            } catch (SocketTimeoutException e) {
                // 응답 없음 -> 다음 포트 시도
            } catch (ConnectException e) {
                String msg = String.valueOf(e.getMessage());
                if (msg.contains("refused") || msg.contains("ECONNREFUSED")) {
                    return true; // 연결 거부 = 기기가 살아있음
                }
            } catch (Exception ignored) {
            }
        }
        return false;
    }

    private static String hostName(String ip) {
        try {
            String h = InetAddress.getByName(ip).getCanonicalHostName();
            return h.equals(ip) ? "" : h;
        } catch (Exception e) {
            return "";
        }
    }

    // Android 9 이하에서만 내용이 채워짐
    private static Map<String, String> readArp() {
        Map<String, String> map = new HashMap<>();
        try (BufferedReader br = new BufferedReader(new FileReader("/proc/net/arp"))) {
            br.readLine();
            String line;
            while ((line = br.readLine()) != null) {
                String[] t = line.trim().split("\\s+");
                if (t.length >= 4 && !t[3].equals("00:00:00:00:00:00")) {
                    map.put(t[0], t[3].toUpperCase(Locale.US));
                }
            }
        } catch (Exception ignored) {
        }
        return map;
    }

    private static String toIp(int a) {
        return ((a >>> 24) & 0xff) + "." + ((a >> 16) & 0xff) + "." + ((a >> 8) & 0xff) + "." + (a & 0xff);
    }

    private static int ipToInt(String ip) {
        String[] o = ip.split("\\.");
        if (o.length != 4) {
            return 0;
        }
        int r = 0;
        try {
            for (String x : o) {
                r = (r << 8) | (Integer.parseInt(x) & 0xff);
            }
        } catch (NumberFormatException e) {
            return 0;
        }
        return r;
    }

    // ---------- 현재 위치 ----------

    private void location() {
        if (!ensurePermission()) {
            return;
        }
        begin(MODE_LOC, "위치 확인 중...");
        geoLoc = null;

        Location best = null;
        boolean any = false;
        for (String p : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
            if (!lm.isProviderEnabled(p)) {
                continue;
            }
            any = true;
            Location l = lm.getLastKnownLocation(p);
            if (l != null && (best == null || l.getTime() > best.getTime())) {
                best = l;
            }
            lm.requestLocationUpdates(p, 1000, 0, locListener);
        }
        if (!any) {
            text.setText("위치(GPS) 서비스가 꺼져 있습니다.");
            return;
        }
        if (best != null) {
            showLocation(best, true);
        }
    }

    private Location geoLoc;      // 주소를 조회한 위치
    private String geoText = "";  // 조회된 주소 문자열
    private boolean geoBusy;

    private Location lastLoc;     // 화면에 표시 중인 위치 (지도/복사/공유에 사용)

    private void showLocation(Location l, boolean cached) {
        lastLoc = l;
        boolean fresh = geoLoc != null && geoLoc.distanceTo(l) < 100;
        text.setText("[ 현재 위치 ]" + (cached ? "  (마지막 위치, 갱신 중...)" : "") + "\n\n"
                + "위도: " + String.format(Locale.US, "%.6f", l.getLatitude()) + "\n"
                + "경도: " + String.format(Locale.US, "%.6f", l.getLongitude()) + "\n"
                + "정확도: " + Math.round(l.getAccuracy()) + " m\n"
                + "제공: " + l.getProvider() + "\n"
                + "시각: " + new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date(l.getTime()))
                + "\n\n" + (fresh ? geoText : "주소: 조회 중..."));
        if (!fresh && !geoBusy) {
            lookupAddress(l);
        }
    }

    // 위도/경도 -> 주소, 우편번호 (인터넷 필요)
    private void lookupAddress(final Location l) {
        geoBusy = true;
        final int id = scanId;
        new Thread(() -> {
            String r;
            try {
                if (!Geocoder.isPresent()) {
                    r = "주소: 이 기기는 주소 변환을 지원하지 않습니다.";
                } else {
                    List<Address> list = new Geocoder(this, Locale.KOREA)
                            .getFromLocation(l.getLatitude(), l.getLongitude(), 1);
                    if (list == null || list.isEmpty()) {
                        r = "주소: 찾을 수 없습니다.";
                    } else {
                        Address a = list.get(0);
                        String zip = a.getPostalCode() == null ? "(없음)" : a.getPostalCode();
                        r = "주소: " + a.getAddressLine(0) + "\n우편번호: " + zip;
                    }
                }
            } catch (Exception e) {
                r = "주소: 조회 실패 (인터넷 연결을 확인하세요)";
            }
            final String res = r;
            runOnUiThread(() -> {
                geoBusy = false;
                geoLoc = l;
                geoText = res;
                if (mode == MODE_LOC && id == scanId) {
                    showLocation(l, false);
                }
            });
        }).start();
    }

    // ---------- 위치 지도 / 복사 / 공유 ----------

    private boolean needLocation() {
        if (lastLoc == null) {
            Toast.makeText(this, "먼저 [검색시작]으로 위치를 확인하세요.", Toast.LENGTH_SHORT).show();
            return true;
        }
        return false;
    }

    private String latLng() {
        return String.format(Locale.US, "%.6f,%.6f", lastLoc.getLatitude(), lastLoc.getLongitude());
    }

    private String mapUrl() {
        return "https://maps.google.com/?q=" + latLng();
    }

    // 좌표 + (조회된 경우) 주소/우편번호 + 지도 링크
    private String locationText() {
        // 화면에 조회된 주소/우편번호 재사용 (조회 성공 시 geoText = "주소: ...\n우편번호: ...")
        String addr = null, zip = null;
        if (geoLoc != null && geoLoc.distanceTo(lastLoc) < 100 && geoText.contains("우편번호")) {
            for (String line : geoText.split("\n")) {
                if (line.startsWith("주소: ")) {
                    addr = line.substring(4);
                } else if (line.startsWith("우편번호: ") && !line.contains("(없음)")) {
                    zip = line.substring(6);
                }
            }
        }
        return TelegramSender.format(this, lastLoc, addr, zip); // 텔레그램·카톡·이메일과 같은 모양
    }

    // 설치된 지도 앱(구글 지도, 네이버 지도 등)으로 열기, 없으면 브라우저로
    private void openMap() {
        if (needLocation()) {
            return;
        }
        String ll = latLng();
        Intent geo = new Intent(Intent.ACTION_VIEW,
                Uri.parse("geo:" + ll + "?q=" + ll + "(" + Uri.encode("현재 위치") + ")"));
        try {
            startActivity(Intent.createChooser(geo, "지도 앱 선택"));
        } catch (ActivityNotFoundException e) {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(mapUrl())));
            } catch (ActivityNotFoundException e2) {
                Toast.makeText(this, "지도를 열 수 있는 앱이 없습니다.", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void copyLocation() {
        if (needLocation()) {
            return;
        }
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        cm.setPrimaryClip(ClipData.newPlainText("현재 위치", locationText()));
        Toast.makeText(this, "위치를 복사했습니다.", Toast.LENGTH_SHORT).show();
    }

    private void shareLocation() {
        if (needLocation()) {
            return;
        }
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, "현재 위치");
        send.putExtra(Intent.EXTRA_TEXT, locationText());
        startActivity(Intent.createChooser(send, "위치 공유"));
    }

    // [메시지 공유] 버튼 공통 모양: 작은 글자, 왼쪽 정렬 (줄마다 글자 시작 위치가 같게)
    private Button msgStyleButton(View.OnClickListener l) {
        Button b = new Button(this);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        b.setPadding(24, 18, 12, 18); // 위아래 여백은 남겨 누르기 쉽게
        b.setMinHeight(0);
        b.setMinimumHeight(0);
        b.setOnClickListener(l);
        return b;
    }

    // 버튼 글자: 이름 + " · " + 설정값(빨간색). values 가 null 이면 이름만
    private static void setJobText(Button b, String name, String values) {
        SpannableStringBuilder s = new SpannableStringBuilder(name);
        if (values != null && !values.isEmpty()) {
            s.append(" · ");
            int st = s.length();
            s.append(values);
            s.setSpan(new ForegroundColorSpan(0xFFC62828), st, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        b.setText(s);
    }

    // 한 줄 = [체크박스] + [버튼]. 체크박스가 없는 줄(별명)은 같은 폭의 빈자리를 둬서 버튼 왼쪽 끝을 맞춤
    private LinearLayout msgLine(CheckBox cb, Button b) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        if (cb == null) {
            cb = new CheckBox(this);
            cb.setVisibility(View.INVISIBLE); // 자리만 차지
        }
        row.addView(cb);
        row.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        msgBox.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    // [메시지 공유] 아래 체크박스 없는 버튼 (보내는 사람 별명)
    private Button msgButton(View.OnClickListener l) {
        Button b = msgStyleButton(l);
        msgLine(null, b);
        return b;
    }

    // ---------- [메시지 공유] 각 줄: 체크박스(켜기/끄기) + 버튼(설정) ----------

    private final Map<String, CheckBox> jobChecks = new HashMap<>(); // 설정 이름 -> 그 줄의 체크박스

    private Button msgRow(final String pref) {
        final CheckBox cb = new CheckBox(this);
        cb.setOnClickListener(v -> toggleJob(pref, cb)); // 사용자가 누를 때만 (코드로 바꿀 때는 반응 안 함)
        Button b = msgStyleButton(v -> openJobSettings(pref));
        msgLine(cb, b);
        jobChecks.put(pref, cb);
        return b;
    }

    // 모든 줄의 체크·글자를 저장된 설정에 맞춤
    private void refreshMsgButtons() {
        updateJobButtons();
        updateEmailButton();
        updateKakaoButton();
        updateTgButton();
    }

    private void syncCheck(String pref, boolean on) {
        CheckBox cb = jobChecks.get(pref);
        if (cb != null) {
            cb.setChecked(on);
        }
    }

    private void openJobSettings(String pref) {
        if (TelegramSender.PREF.equals(pref)) {
            // 아직 봇 토큰이 없으면(처음) 설정 도우미부터
            if (getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE).getString("token", "").isEmpty()) {
                startActivity(new Intent(this, TelegramSetupActivity.class));
            } else {
                showTelegramDialog();
            }
        } else if (HomeService.PREF_KAKAO.equals(pref)) {
            showKakaoDialog();
        } else if (HomeService.PREF_EMAIL.equals(pref) || HomeService.PREF_KMAIL.equals(pref)) {
            showEmailDialog(pref);
        } else {
            showJobDialog(pref); // 본인 폰 알림 / 일반 문자
        }
    }

    // 켜기 전에 설정이 다 되어 있는지 확인. 문제 있으면 이유, 없으면 null
    private String startProblem(String pref, SharedPreferences sp) {
        if (TelegramSender.PREF.equals(pref)) {
            return sp.getString("token", "").isEmpty() || sp.getString("chats", "").trim().isEmpty()
                    ? "텔레그램 설정(봇 토큰·받는 사람)을 먼저 해 주세요." : null;
        }
        if (HomeService.PREF_KAKAO.equals(pref)) {
            return kakaoInstalled() ? null : "카카오톡이 설치되어 있지 않습니다.";
        }
        // 이메일 / 카톡 이메일 / 문자 / 본인 알림: 시각 설정 필요
        if (!sp.contains("mode")) {
            return "먼저 보내는 시각 등을 설정해 주세요.";
        }
        if ("times".equals(sp.getString("mode", "")) && sp.getString("times", "").trim().isEmpty()) {
            return "보낼 시각을 먼저 등록해 주세요.";
        }
        if (!HomeService.PREF_SELF.equals(pref) && sp.getString("to", "").trim().isEmpty()) {
            return "받는 사람을 먼저 설정해 주세요.";
        }
        boolean mail = HomeService.PREF_EMAIL.equals(pref) || HomeService.PREF_KMAIL.equals(pref);
        if (mail && HomeService.emailSmtp(sp)
                && (sp.getString("from", "").isEmpty() || sp.getString("pass", "").isEmpty())) {
            return "보내는 이메일과 앱 비밀번호를 먼저 설정해 주세요.";
        }
        return null;
    }

    // 체크박스를 눌렀을 때: 켜기(설정 확인 후 바로 시작) / 끄기
    private void toggleJob(String pref, CheckBox cb) {
        SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        if (cb.isChecked()) {
            String problem = startProblem(pref, sp);
            if (problem != null) {
                cb.setChecked(false);
                Toast.makeText(this, problem, Toast.LENGTH_LONG).show();
                openJobSettings(pref); // 설정 팝업을 바로 열어 줌
                return;
            }
            if (!hasPermission()) {
                cb.setChecked(false);
                ensurePermission();
                Toast.makeText(this, "위치 권한을 허용한 뒤 다시 눌러 주세요.", Toast.LENGTH_LONG).show();
                return;
            }
            if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
            }
            // 지정 시각: 다음 시각부터 / 그 밖(간격): 바로 1회 후 간격대로
            long next = "times".equals(sp.getString("mode", ""))
                    ? HomeService.nextTime(sp.getString("times", ""), System.currentTimeMillis()) : 0;
            sp.edit().putBoolean("on", true).putLong("next", next).apply();
        } else {
            sp.edit().putBoolean("on", false).apply();
        }
        HomeService.refresh(this);
        refreshMsgButtons();
        Toast.makeText(this, cb.isChecked() ? "켰습니다." : "껐습니다.", Toast.LENGTH_SHORT).show();
    }

    // ---------- 본인 폰 알림 / 일반 문자(수동) : 정해진 시각·간격마다 알림 ----------

    private void updateJobButtons() {
        String[] prefs = {HomeService.PREF_SELF, HomeService.PREF_SMS};
        Button[] btns = {selfBtn, smsBtn};
        String[] names = {"본인 폰 알림 (자동)", "일반 문자 (수동)"};
        for (int i = 0; i < 2; i++) {
            SharedPreferences sp = getSharedPreferences(prefs[i], MODE_PRIVATE);
            boolean on = sp.getBoolean("on", false);
            setJobText(btns[i], names[i], on ? HomeService.emailSchedule(sp) : null);
            btns[i].setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
            syncCheck(prefs[i], on);
        }
    }

    // 보내는 시각 입력 묶음 (지정 시각(매일) / 일정 간격) - 문자·본인 알림 팝업 공용
    private class ScheduleUi {
        final RadioButton rTimes = new RadioButton(MainActivity.this), rInterval = new RadioButton(MainActivity.this);
        final EditText times = new EditText(MainActivity.this);
        final Spinner interval = new Spinner(MainActivity.this);

        ScheduleUi(LinearLayout f, SharedPreferences sp) {
            label(f, "보내는 시각");
            RadioGroup mode = new RadioGroup(MainActivity.this);
            rTimes.setText("지정 시각 (매일)");
            rTimes.setId(View.generateViewId());
            rInterval.setText("일정 간격");
            rInterval.setId(View.generateViewId());
            mode.addView(rTimes);
            mode.addView(rInterval);
            f.addView(mode);

            final LinearLayout timesRow = new LinearLayout(MainActivity.this);
            timesRow.setOrientation(LinearLayout.HORIZONTAL);
            times.setSingleLine(true);
            times.setHint("예: 08:00, 12:00, 18:00");
            times.setText(sp.getString("times", ""));
            Button addTime = new Button(MainActivity.this);
            addTime.setText("시각 추가");
            addTime.setTextSize(13);
            addTime.setOnClickListener(v -> new TimePickerDialog(MainActivity.this, (tp, h, m) -> {
                String cur = times.getText().toString().trim();
                String add = String.format(Locale.US, "%02d:%02d", h, m);
                String n = normalizeTimes(cur.isEmpty() ? add : cur + ", " + add);
                times.setText(n == null ? cur + ", " + add : n);
            }, 8, 0, true).show());
            timesRow.addView(times, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            timesRow.addView(addTime);
            f.addView(timesRow);

            interval.setAdapter(new ArrayAdapter<>(MainActivity.this,
                    android.R.layout.simple_spinner_dropdown_item, TelegramSender.INTERVAL_LABELS));
            int cur = sp.getInt("interval", 60);
            for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
                if (TelegramSender.INTERVALS[i] == cur) {
                    interval.setSelection(i);
                }
            }
            f.addView(interval);

            mode.setOnCheckedChangeListener((g, id) -> {
                timesRow.setVisibility(id == rTimes.getId() ? View.VISIBLE : View.GONE);
                interval.setVisibility(id == rInterval.getId() ? View.VISIBLE : View.GONE);
            });
            mode.check("times".equals(sp.getString("mode", "times")) ? rTimes.getId() : rInterval.getId());
        }

        boolean byTimes() {
            return rTimes.isChecked();
        }

        String timesText() { // 정리된 시각 목록, 형식이 틀리면 null
            return normalizeTimes(times.getText().toString());
        }

        int minutes() {
            return TelegramSender.INTERVALS[interval.getSelectedItemPosition()];
        }
    }

    // 본인 폰 알림(PREF_SELF) / 일반 문자(PREF_SMS) 설정 팝업
    private void showJobDialog(final String pref) {
        final boolean sms = HomeService.PREF_SMS.equals(pref);
        final String title = sms ? "일반 문자 (수동)" : "본인 폰 알림 (자동)";
        final SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 10, 60, 10);

        TextView info = new TextView(this);
        info.setText(sms
                ? "정해진 시각마다 위치를 확인해 알림을 띄웁니다. 알림을 누르면 받는 사람 번호와 내용이 "
                + "채워진 문자 앱이 열리고, [전송]을 누르면 보내집니다. (지금 요금제로 발송, 가입 불필요)"
                : "정해진 시각마다 이 폰에 현재 위치 알림을 띄웁니다. 아무에게도 보내지 않습니다.\n"
                + "알림을 누르면 지도가 열리고, [공유]로 원하는 곳에 보낼 수 있습니다.");
        info.setTextColor(Color.DKGRAY);
        info.setPadding(0, 16, 0, 0);
        f.addView(info);

        final EditText to = new EditText(this);
        final RadioButton rShort = new RadioButton(this), rFull = new RadioButton(this);
        if (sms) {
            label(f, "받는 사람 번호 (여러 명은 쉼표로 구분)");
            to.setSingleLine(true);
            to.setInputType(InputType.TYPE_CLASS_PHONE);
            to.setHint("예: 010-1234-5678, 010-9876-5432");
            to.setText(sp.getString("to", ""));
            f.addView(to);
            Button pick = new Button(this);
            pick.setText("연락처에서 선택");
            pick.setTextSize(13);
            pick.setOnClickListener(v -> {
                contactTarget = to;
                try {
                    startActivityForResult(new Intent(Intent.ACTION_PICK,
                            android.provider.ContactsContract.CommonDataKinds.Phone.CONTENT_URI), REQ_CONTACT);
                } catch (Exception e) {
                    Toast.makeText(this, "연락처를 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
            f.addView(pick, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

            label(f, "메시지 형식");
            RadioGroup fmt = new RadioGroup(this);
            rShort.setText("짧게 (주소 + 지도 링크 1개)");
            rShort.setId(View.generateViewId());
            rFull.setText("전체 (텔레그램·이메일과 같은 내용, 장문 문자)");
            rFull.setId(View.generateViewId());
            fmt.addView(rShort);
            fmt.addView(rFull);
            fmt.check(sp.getBoolean("short", true) ? rShort.getId() : rFull.getId());
            f.addView(fmt);
        }

        final ScheduleUi sch = new ScheduleUi(f, sp);

        final CheckBox on = new CheckBox(this);
        on.setText("자동알림시작");
        on.setTextSize(16);
        on.setTypeface(null, Typeface.BOLD);
        on.setChecked(sp.getBoolean("on", false));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 24;
        // 켜기·끄기는 메인 화면 버튼 앞의 체크박스로 (팝업에는 표시하지 않음)

        TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0), nx = sp.getLong("next", 0);
        SimpleDateFormat df = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA);
        last.setText((lt == 0 ? "아직 알림을 띄운 적이 없습니다." : "마지막 알림: " + df.format(new Date(lt)))
                + (sp.getBoolean("on", false) && nx > 0 ? "\n다음 알림: " + df.format(new Date(nx)) : ""));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        f.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(f);
        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        d.setOnShowListener(x -> {
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                String nums = to.getText().toString().trim();
                String tm = sch.timesText();
                boolean start = on.isChecked();
                if (sms && !nums.isEmpty() && !validPhones(nums)) {
                    Toast.makeText(this, "전화번호 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (sch.byTimes() && tm == null) {
                    Toast.makeText(this, "시각은 08:00 처럼 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && sms && nums.isEmpty()) {
                    Toast.makeText(this, "받는 사람 번호를 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && sch.byTimes() && tm.isEmpty()) {
                    Toast.makeText(this, "보낼 시각을 하나 이상 등록하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                SharedPreferences.Editor ed = sp.edit()
                        .putString("mode", sch.byTimes() ? "times" : "interval")
                        .putString("times", tm == null ? "" : tm)
                        .putInt("interval", sch.minutes()).putBoolean("on", start);
                if (sms) {
                    ed.putString("to", nums).putBoolean("short", rShort.isChecked());
                }
                if (start) {
                    // 지정 시각: 다음 시각에 알림 / 일정 간격: 바로 1회 알림 후 간격대로
                    ed.putLong("next", sch.byTimes() ? HomeService.nextTime(tm, System.currentTimeMillis()) : 0);
                }
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateJobButtons();
                Toast.makeText(this, start ? "저장했습니다. (켜져 있음 · " + HomeService.emailSchedule(sp) + ")"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                d.dismiss();
            });
            // 지금 테스트: 문자 앱 열기 / 이 폰에 바로 알림
            d.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
                final String nums = to.getText().toString().trim();
                if (sms && (nums.isEmpty() || !validPhones(nums))) {
                    Toast.makeText(this, "받는 사람 번호를 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                final boolean shortFmt = sms && rShort.isChecked();
                final Location loc = currentOrLastLocation();
                last.setText("위치 메시지 만드는 중...");
                new Thread(() -> {
                    String msg = shortFmt ? TelegramSender.shortMessage(this, loc) : TelegramSender.message(this, loc);
                    runOnUiThread(() -> {
                        if (sms) {
                            d.dismiss();
                            try {
                                startActivity(HomeService.smsCompose(nums, msg));
                            } catch (Exception e) {
                                Toast.makeText(this, "문자 앱을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                            }
                        } else {
                            HomeService.notifySelf(this, msg, loc);
                            last.setText("알림을 띄웠습니다. 화면 위쪽 알림창을 확인하세요.");
                        }
                    });
                }).start();
            });
        });
        d.show();
    }

    // 전화번호 목록 확인 (숫자 3자리 이상씩)
    private static boolean validPhones(String s) {
        for (String p : s.split("[,;]+")) {
            String digits = p.replaceAll("[^0-9]", "");
            if (!p.trim().isEmpty() && digits.length() < 3) {
                return false;
            }
        }
        return true;
    }

    // 연락처에서 고른 전화번호를 받는 사람 칸에 추가
    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != REQ_CONTACT || result != RESULT_OK || data == null || data.getData() == null
                || contactTarget == null) {
            return;
        }
        try (android.database.Cursor c = getContentResolver().query(data.getData(),
                new String[]{android.provider.ContactsContract.CommonDataKinds.Phone.NUMBER}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String num = c.getString(0);
                String cur = contactTarget.getText().toString().trim();
                contactTarget.setText(cur.isEmpty() ? num : cur + ", " + num);
            }
        } catch (Exception e) {
            Toast.makeText(this, "연락처 번호를 읽을 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    // ---------- (5) 보호자 모드 탭: 아직 비워 둠 (기능은 추후 결정) ----------

    private TextView remoteInfo;

    private void buildGuardianTab(LinearLayout body) {
        final SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);

        // 1) 원격 위치 요청
        sectionTitle(body, "■ 원격 위치 요청 (텔레그램)");
        smallNote(body, "보호자가 텔레그램 봇에게 \"위치\" (또는 \"어디야\", /where)를 보내면,\n"
                + "이 폰이 현재 위치를 자동으로 답장합니다. 이 폰을 만질 필요가 없습니다.\n"
                + "※ 텔레그램 '받는 사람'으로 등록된 보호자만 요청할 수 있습니다.");
        final CheckBox remote = new CheckBox(this);
        remote.setText("원격 위치 요청 받기");
        remote.setTextSize(15);
        remote.setTypeface(null, Typeface.BOLD);
        remote.setChecked(tg.getBoolean("remote", false));
        remote.setOnClickListener(v -> {
            if (remote.isChecked()) {
                if (tg.getString("token", "").isEmpty() || tg.getString("chats", "").trim().isEmpty()) {
                    remote.setChecked(false);
                    Toast.makeText(this, "텔레그램 설정(봇 토큰·받는 사람)을 먼저 해 주세요.", Toast.LENGTH_LONG).show();
                    openJobSettings(TelegramSender.PREF);
                    return;
                }
                if (!hasPermission()) {
                    remote.setChecked(false);
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 눌러 주세요.", Toast.LENGTH_LONG).show();
                    return;
                }
            }
            tg.edit().putBoolean("remote", remote.isChecked()).apply();
            HomeService.refresh(this);
            Toast.makeText(this, remote.isChecked()
                    ? "켰습니다. 보호자가 봇에게 \"위치\"를 보내 보세요." : "껐습니다.", Toast.LENGTH_LONG).show();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        body.addView(remote, lp);

        remoteInfo = smallNote(body, "");
        remoteInfo.setTextColor(Color.DKGRAY);
        remoteInfo.setTextSize(13);
        updateRemoteInfo();

        smallNote(body, "보호자가 하는 방법:\n"
                + " 1. 텔레그램에서 위치 알림 봇 대화방을 엽니다.\n"
                + " 2. \"위치\" 라고 보냅니다.\n"
                + " 3. \"위치를 확인하고 있습니다...\" 다음에 주소·지도 링크가 옵니다. (최대 30초)\n\n"
                + "※ 이 폰의 배터리 설정을 '제한 없음'으로 해야 화면이 꺼져 있을 때도 빨리 답장합니다.\n"
                + "※ PC 프로그램과 같은 봇을 쓰면 메시지를 서로 가져가므로, 폰 전용 봇을 쓰세요.");

        View sep = new View(this);
        sep.setBackgroundColor(0xFFDDDDDD);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 3);
        sp.topMargin = 24;
        body.addView(sep, sp);

        // 2) 나머지 보호자 기능은 추후
        sectionTitle(body, "■ 그 밖의 보호자 기능");
        msgStyleButtonFor(body, "가족 전체 조회", v -> showComingSoon("가족 전체 조회"));
        msgStyleButtonFor(body, "비밀번호 잠금", v -> showComingSoon("비밀번호 잠금"));

        // 앱 정보 (가족 폰에 어느 버전이 깔렸는지 확인용)
        View sep2 = new View(this);
        sep2.setBackgroundColor(0xFFDDDDDD);
        LinearLayout.LayoutParams sp2 = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 3);
        sp2.topMargin = 24;
        body.addView(sep2, sp2);
        smallNote(body, "집에왔어요  버전 " + appVersion());
    }

    // 앱 버전 "1.1.0 (2)" = versionName (versionCode)  ※ app/build.gradle 에서 관리
    private String appVersion() {
        try {
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            long code = Build.VERSION.SDK_INT >= 28 ? pi.getLongVersionCode() : pi.versionCode;
            return pi.versionName + " (" + code + ")";
        } catch (Exception e) {
            return "?";
        }
    }

    // 아직 만들지 않은 기능 안내
    private void showComingSoon(String name) {
        new AlertDialog.Builder(this)
                .setTitle(name)
                .setMessage("지금 추가 개발 중입니다.")
                .setPositiveButton("확인", null)
                .show();
    }

    private void updateRemoteInfo() {
        if (remoteInfo == null) {
            return;
        }
        SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        long t = tg.getLong("remoteTime", 0);
        remoteInfo.setText(t == 0 ? "아직 요청을 받은 적이 없습니다."
                : "마지막 요청: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(t))
                + "  " + tg.getString("remoteResult", ""));
    }

    // ---------- 긴급 SOS / 보호자 바로 걸기 ----------

    private static final String SOS_IDLE = "🆘\nSOS\n2초 길게";

    // 화면 오른쪽 위 ([종료] 아래): 큰 [SOS] 버튼 (2초 길게 눌러야 작동, 중간에 손을 떼면 취소)
    private Button makeSosButton() {
        final Button sos = new Button(this);
        sos.setText(SOS_IDLE);
        sos.setTextSize(18);
        sos.setTypeface(null, Typeface.BOLD);
        sos.setTextColor(Color.WHITE);
        sos.setBackgroundColor(0xFFC62828);
        sos.setAllCaps(false);
        sos.setGravity(Gravity.CENTER);
        sos.setPadding(10, 10, 10, 10);
        final Runnable fire = () -> {
            sos.setText("🆘\n보내는 중");
            startSosNow();
        };
        sos.setOnTouchListener((v, e) -> {
            switch (e.getAction()) {
                case android.view.MotionEvent.ACTION_DOWN:
                    sos.setText("🆘\n계속\n누르세요");
                    handler.postDelayed(fire, 2000);
                    return true;
                case android.view.MotionEvent.ACTION_UP:
                case android.view.MotionEvent.ACTION_CANCEL:
                    handler.removeCallbacks(fire);
                    handler.postDelayed(() -> sos.setText(SOS_IDLE), 1500);
                    return true;
                default:
                    return true;
            }
        });
        return sos;
    }

    // (2) 현재 위치 탭 맨 위: 보호자 바로 걸기 (SOS 버튼은 화면 오른쪽 위로 옮김)
    private void buildSosBox(LinearLayout body) {
        sosCallBox = new LinearLayout(this);
        sosCallBox.setOrientation(LinearLayout.HORIZONTAL);
        sosCallBox.setPadding(36, 0, 36, 0);
        body.addView(sosCallBox);
        renderSosCalls();
    }

    // 보호자 바로 걸기 버튼 (최대 3명). 번호가 없으면 등록 안내
    private void renderSosCalls() {
        if (sosCallBox == null) {
            return;
        }
        sosCallBox.removeAllViews();
        List<String> ps = new ArrayList<>();
        for (String p : SosActivity.phones(this).split("[,;]+")) {
            if (!p.trim().isEmpty() && ps.size() < 3) {
                ps.add(p.trim());
            }
        }
        if (ps.isEmpty()) {
            Button b = new Button(this);
            b.setText("📞 보호자 전화번호 등록");
            b.setTextSize(13);
            b.setAllCaps(false);
            b.setOnClickListener(v -> showSosSettings());
            sosCallBox.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            return;
        }
        for (int k = 0; k < ps.size(); k++) {
            final String p = ps.get(k);
            Button b = new Button(this);
            b.setText("📞 보호자" + (k + 1) + "\n" + p);
            b.setTextSize(12);
            b.setAllCaps(false);
            b.setOnClickListener(v -> callWithPermission(p));
            sosCallBox.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }
    }

    private void callWithPermission(String phone) {
        if (checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED
                && !getSharedPreferences(SosActivity.PREF_SOS, MODE_PRIVATE).getBoolean("askedCall", false)) {
            // 처음 한 번만 권한 요청 (거절하면 전화 화면까지만 띄우는 방식으로 계속 동작)
            getSharedPreferences(SosActivity.PREF_SOS, MODE_PRIVATE).edit().putBoolean("askedCall", true).apply();
            requestPermissions(new String[]{Manifest.permission.CALL_PHONE}, REQ_CALL);
        }
        SosActivity.call(this, phone);
    }

    // 앱 화면의 SOS: 2초 길게 눌러 확인했으므로 카운트다운 없이 바로 실행
    private void startSosNow() {
        if (!hasPermission()) {
            ensurePermission();
            Toast.makeText(this, "위치 권한이 있어야 위치를 보낼 수 있습니다.", Toast.LENGTH_LONG).show();
        }
        Toast.makeText(this, "🆘 SOS: 보호자에게 전화하고 위치를 보냅니다.", Toast.LENGTH_LONG).show();
        SosActivity.fire(this);
    }

    // (3) 알림 설정 탭 맨 위: SOS 설정
    private void buildSosSection(LinearLayout body) {
        View top = new View(this); // 위 '귀가·외출·미귀가 알림'과 구분선
        top.setBackgroundColor(0xFFDDDDDD);
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 3);
        tp.topMargin = 24;
        body.addView(top, tp);
        sectionTitle(body, "■ 긴급 SOS");
        smallNote(body, "화면 오른쪽 위 [SOS]를 2초 길게 누르면 첫 번째 보호자에게 전화하고, "
                + "설정된 자동 채널(텔레그램, 자동 이메일)로 위치를 바로 보냅니다.\n"
                + "※ 112·119는 Android 정책상 앱이 자동으로 걸 수 없습니다.");
        sosInfo = smallNote(body, "");
        sosInfo.setTextColor(Color.DKGRAY);
        sosInfo.setTextSize(14);
        updateSosInfo();
        msgStyleButtonFor(body, "보호자 전화번호 설정 (최대 3명)", v -> showSosSettings());
        msgStyleButtonFor(body, "홈 화면에 SOS 바로가기 추가", v -> addSosShortcut());
        smallNote(body, "바로가기를 누르면 3초 카운트다운 후 작동합니다. (그 사이 [취소] 가능)\n"
                + "앱 아이콘을 길게 눌러 나오는 메뉴에도 'SOS'가 있습니다.");
    }

    private void updateSosInfo() {
        if (sosInfo == null) {
            return;
        }
        String ps = SosActivity.phones(this);
        SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);
        long t = ev.getLong("sosTime", 0);
        sosInfo.setText("보호자: " + (ps.isEmpty() ? "(등록 안 됨)" : ps)
                + (t == 0 ? "" : "\n마지막 SOS: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(t))
                + "  " + ev.getString("sosResult", "")));
    }

    private void showSosSettings() {
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 20, 60, 10);
        TextView info = new TextView(this);
        info.setText("SOS 때 첫 번째 번호로 전화를 겁니다.\n(2) 현재 위치 탭에 보호자 바로 걸기 버튼(최대 3명)이 생깁니다.");
        info.setTextColor(Color.DKGRAY);
        f.addView(info);
        label(f, "보호자 전화번호 (쉼표로 구분, 첫 번째 = SOS 전화)");
        final EditText phones = new EditText(this);
        phones.setSingleLine(true);
        phones.setInputType(InputType.TYPE_CLASS_PHONE);
        phones.setHint("예: 010-1234-5678, 010-9876-5432");
        phones.setText(SosActivity.phones(this));
        f.addView(phones);
        Button pick = new Button(this);
        pick.setText("연락처에서 선택");
        pick.setTextSize(13);
        pick.setOnClickListener(v -> {
            contactTarget = phones;
            try {
                startActivityForResult(new Intent(Intent.ACTION_PICK,
                        android.provider.ContactsContract.CommonDataKinds.Phone.CONTENT_URI), REQ_CONTACT);
            } catch (Exception e) {
                Toast.makeText(this, "연락처를 열 수 없습니다.", Toast.LENGTH_SHORT).show();
            }
        });
        f.addView(pick, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle("보호자 전화번호")
                .setView(f)
                .setPositiveButton("저장", null)
                .setNegativeButton("취소", null)
                .create();
        d.setOnShowListener(x -> d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String ps = phones.getText().toString().trim();
            if (!ps.isEmpty() && !validPhones(ps)) {
                Toast.makeText(this, "전화번호 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                return;
            }
            SosActivity.prefs(this).edit().putString("phones", ps).apply();
            if (!ps.isEmpty() && checkSelfPermission(Manifest.permission.CALL_PHONE)
                    != PackageManager.PERMISSION_GRANTED) {
                SosActivity.prefs(this).edit().putBoolean("askedCall", true).apply();
                requestPermissions(new String[]{Manifest.permission.CALL_PHONE}, REQ_CALL);
            }
            renderSosCalls();
            updateSosInfo();
            Toast.makeText(this, "저장했습니다.", Toast.LENGTH_SHORT).show();
            d.dismiss();
        }));
        d.show();
    }

    // 홈 화면에 SOS 아이콘 추가 (Android 8 이상, 런처가 지원하는 경우)
    private void addSosShortcut() {
        if (Build.VERSION.SDK_INT < 26) {
            Toast.makeText(this, "앱 아이콘을 길게 눌러 'SOS'를 끌어다 홈 화면에 놓으세요.", Toast.LENGTH_LONG).show();
            return;
        }
        android.content.pm.ShortcutManager sm = getSystemService(android.content.pm.ShortcutManager.class);
        if (sm == null || !sm.isRequestPinShortcutSupported()) {
            Toast.makeText(this, "이 홈 화면은 바로가기 추가를 지원하지 않습니다.\n"
                    + "앱 아이콘을 길게 눌러 'SOS'를 끌어다 놓으세요.", Toast.LENGTH_LONG).show();
            return;
        }
        android.content.pm.ShortcutInfo si = new android.content.pm.ShortcutInfo.Builder(this, "sos_pin")
                .setShortLabel("SOS")
                .setLongLabel("긴급 SOS")
                .setIcon(android.graphics.drawable.Icon.createWithResource(this, R.mipmap.ic_launcher))
                .setIntent(new Intent(this, SosActivity.class).setAction(Intent.ACTION_VIEW))
                .build();
        sm.requestPinShortcut(si, null);
    }

    // ---------- (3) 알림 설정 탭 ----------

    private CheckBox evHome, evAway;
    private TextView evLast;
    // 귀가·외출 알림을 보낼 곳: {설정 키, 표시 이름}
    private static final String[][] EV_CHANNELS = {
            {"ch_self", "본인 폰"}, {"ch_email", "일반 이메일"}, {"ch_kmail", "카톡 이메일"},
            {"ch_kakao", "카톡 본인 메시지"}, {"ch_tg", "텔레그램"}, {"ch_sms", "일반 문자"}};

    private TextView sectionTitle(LinearLayout body, String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(17);
        t.setTypeface(null, Typeface.BOLD);
        t.setTextColor(0xFF1976D2);
        t.setPadding(40, 36, 40, 8);
        body.addView(t);
        return t;
    }

    private TextView smallNote(LinearLayout body, String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(12);
        t.setTextColor(Color.GRAY);
        t.setPadding(40, 0, 40, 8);
        body.addView(t);
        return t;
    }

    private void buildAlarmTab(LinearLayout body) {
        final SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);

        // 1) 귀가·외출 순간 알림
        sectionTitle(body, "■ 귀가 · 외출 · 미귀가 알림");
        smallNote(body, "홈 공유기에 연결되거나 끊기는 순간, 아래에서 고른 곳으로 바로 알립니다.\n"
                + "(받는 사람·계정·보내는 사람 별명은 (4) 위치 보내기 탭의 각 설정을 그대로 씁니다)");
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        evHome = new CheckBox(this);
        evHome.setText("귀가하면 알림 (HomeBack)");
        evHome.setTextSize(15);
        evHome.setChecked(ev.getBoolean("home", false));
        evHome.setOnClickListener(v -> toggleEvent("home", evHome));
        body.addView(evHome, lp);
        evAway = new CheckBox(this);
        evAway.setText("외출하면 알림 (NotHome · 2분 넘게 밖에 있을 때)");
        evAway.setTextSize(15);
        evAway.setChecked(ev.getBoolean("away", false));
        evAway.setOnClickListener(v -> toggleEvent("away", evAway));
        body.addView(evAway, lp);

        // 미귀가 알림: [22:00]까지 귀가하지 않으면 알림 (시각 버튼을 눌러 변경)
        LinearLayout lateRow = new LinearLayout(this);
        lateRow.setOrientation(LinearLayout.HORIZONTAL);
        lateRow.setGravity(Gravity.CENTER_VERTICAL);
        final CheckBox evLate = new CheckBox(this);
        evLate.setText("미귀가 알림:");
        evLate.setTextSize(15);
        evLate.setChecked(ev.getBoolean("late", false));
        evLate.setOnClickListener(v -> toggleEvent("late", evLate));
        lateRow.addView(evLate);
        final Button lateTime = new Button(this);
        lateTime.setText(ev.getString("lateTime", "22:00"));
        lateTime.setTextSize(14);
        lateTime.setTextColor(0xFFC62828);
        lateTime.setOnClickListener(v -> {
            int[] hm = HomeService.parseTime(ev.getString("lateTime", "22:00"));
            new TimePickerDialog(this, (tp, h, m) -> {
                String t = String.format(Locale.US, "%02d:%02d", h, m);
                ev.edit().putString("lateTime", t).apply();
                lateTime.setText(t);
                HomeService.refresh(this); // 새 시각으로 다시 예약
            }, hm == null ? 22 : hm[0], hm == null ? 0 : hm[1], true).show();
        });
        lateRow.addView(lateTime);
        TextView lateTail = new TextView(this);
        lateTail.setText(" 까지 귀가하지 않으면");
        lateTail.setTextSize(15);
        lateRow.addView(lateTail);
        body.addView(lateRow, lp);

        TextView to = new TextView(this);
        to.setText("보낼 곳");
        to.setTextSize(14);
        to.setTypeface(null, Typeface.BOLD);
        to.setPadding(40, 16, 40, 0);
        body.addView(to);
        LinearLayout grid = new LinearLayout(this); // 2칸씩 3줄
        grid.setOrientation(LinearLayout.VERTICAL);
        grid.setPadding(30, 0, 30, 0);
        LinearLayout line = null;
        for (int k = 0; k < EV_CHANNELS.length; k++) {
            if (k % 2 == 0) {
                line = new LinearLayout(this);
                line.setOrientation(LinearLayout.HORIZONTAL);
                grid.addView(line);
            }
            final String key = EV_CHANNELS[k][0];
            final CheckBox c = new CheckBox(this);
            c.setText(EV_CHANNELS[k][1]);
            c.setTextSize(13);
            c.setChecked(ev.getBoolean(key, "ch_self".equals(key))); // 처음에는 '본인 폰'만 체크
            c.setOnClickListener(v -> ev.edit().putBoolean(key, c.isChecked()).apply());
            line.addView(c, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }
        body.addView(grid);
        if (!ev.contains("ch_self")) {
            ev.edit().putBoolean("ch_self", true).apply();
        }
        smallNote(body, "※ 카톡 본인 메시지·일반 문자, 그리고 '수동'으로 설정한 이메일은 알림을 눌러 직접 보내야 합니다.");

        Button test = msgStyleButtonFor(body, "테스트 (지금 '귀가' 알림 보내 보기)", v -> {
            if (!HomeService.eventOn(this)) {
                Toast.makeText(this, "귀가 또는 외출 알림을 먼저 켜 주세요.", Toast.LENGTH_SHORT).show();
                return;
            }
            HomeService.testEvent(this);
            Toast.makeText(this, "테스트 알림을 보냅니다. (위치 확인에 최대 30초)", Toast.LENGTH_LONG).show();
        });
        test.setTextColor(0xFF1976D2);
        evLast = smallNote(body, "");
        updateEventInfo();

        buildSosSection(body); // 2) 긴급 SOS (탭 맨 아래)
    }

    // (4) 위치 보내기 탭: 정해진 시각 위치 보내기 (보내는 사람 별명 + 채널별 줄)
    private void buildSendTab(LinearLayout body) {
        sectionTitle(body, "■ 정해진 시각 위치 보내기");

        // 외출 중에만 보내기: 집에 있으면 보내지 않고, 외출(2분 넘게 밖)이 확인되면 그때부터 보냄
        final CheckBox away = new CheckBox(this);
        away.setText("외출 중에만 보내기");
        away.setTextSize(15);
        away.setTypeface(null, Typeface.BOLD);
        away.setChecked(HomeService.awayOnly(this));
        away.setOnClickListener(v -> {
            if (away.isChecked() && loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
                away.setChecked(false);
                Toast.makeText(this, "홈 공유기 또는 홈 PC 블루투스를 먼저 등록하세요. ((1) 홈 공유기 등록 탭)",
                        Toast.LENGTH_LONG).show();
                return;
            }
            if (away.isChecked() && !hasPermission()) {
                away.setChecked(false);
                ensurePermission();
                Toast.makeText(this, "위치 권한을 허용한 뒤 다시 눌러 주세요.", Toast.LENGTH_LONG).show();
                return;
            }
            getSharedPreferences(TelegramSender.PREF_APP, MODE_PRIVATE).edit()
                    .putBoolean("awayOnly", away.isChecked()).apply();
            HomeService.refresh(this);
            Toast.makeText(this, away.isChecked()
                    ? "집에 있을 때는 보내지 않고, 외출하면 보내기 시작합니다."
                    : "집에 있어도 정해진 시각마다 보냅니다.", Toast.LENGTH_LONG).show();
        });
        LinearLayout.LayoutParams alp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        alp.leftMargin = 30;
        body.addView(away, alp);
        smallNote(body, "집에 있으면 정해진 시각이 되어도 보내지 않습니다. 외출(2분 넘게 밖)이 확인되면 "
                + "그때부터 보내기 시작하고('일정 간격'은 바로 1회), 귀가하면 멈춥니다.");

        smallNote(body, "줄 앞 체크박스 = 켜기/끄기, 버튼 = 설정");
        msgBox = new LinearLayout(this);
        msgBox.setOrientation(LinearLayout.VERTICAL);
        msgBox.setPadding(10, 0, 30, 30);
        body.addView(msgBox);

        senderBtn = msgButton(v -> showSenderDialog()); // 보내는 사람 별명 (모든 위치 메시지 제목에 표시)
        updateSenderButton();
        // 아래 줄들: [☐ 체크박스] = 켜기/끄기, [버튼] = 설정 팝업
        selfBtn = msgRow(HomeService.PREF_SELF);   // 본인 폰 알림 (이 폰에 위치 알림)
        emailBtn = msgRow(HomeService.PREF_EMAIL); // 일반 이메일 (자동/수동)
        kmailBtn = msgRow(HomeService.PREF_KMAIL); // 카톡 이메일 (자동/수동)
        kakaoBtn = msgRow(HomeService.PREF_KAKAO); // 카톡 본인 메시지 (수동: 알림 -> 카톡 공유 화면)
        tgBtn = msgRow(TelegramSender.PREF);       // 텔레그램 (자동)
        smsBtn = msgRow(HomeService.PREF_SMS);     // 일반 문자 (수동: 알림 -> 문자 앱)
        refreshMsgButtons();
    }

    // msgBox 밖(본문)에 같은 모양의 버튼 하나
    private Button msgStyleButtonFor(LinearLayout body, String label, View.OnClickListener l) {
        Button b = msgStyleButton(l);
        b.setText(label);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        lp.rightMargin = 30;
        lp.topMargin = 10;
        body.addView(b, lp);
        return b;
    }

    // 귀가(home) / 외출(away) 알림 켜기·끄기
    private void toggleEvent(String key, CheckBox cb) {
        SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);
        if (cb.isChecked()) {
            if (loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
                cb.setChecked(false);
                Toast.makeText(this, "홈 공유기 또는 홈 PC 블루투스를 먼저 등록하세요. ((1) 홈 공유기 등록 탭)",
                        Toast.LENGTH_LONG).show();
                return;
            }
            if (!hasPermission()) {
                cb.setChecked(false);
                ensurePermission();
                Toast.makeText(this, "위치 권한을 허용한 뒤 다시 눌러 주세요.", Toast.LENGTH_LONG).show();
                return;
            }
            if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
            }
        }
        ev.edit().putBoolean(key, cb.isChecked()).apply();
        HomeService.refresh(this);
        Toast.makeText(this, cb.isChecked() ? "켰습니다. 앱을 닫아도 동작합니다." : "껐습니다.", Toast.LENGTH_SHORT).show();
    }

    private void updateEventInfo() {
        if (evLast == null) {
            return;
        }
        SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);
        long t = ev.getLong("lastTime", 0);
        evLast.setText(t == 0 ? "아직 보낸 적이 없습니다."
                : "마지막: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(t))
                + "  " + ev.getString("lastResult", ""));
    }

    // ---------- 보내는 사람 이름 ----------

    private void updateSenderButton() {
        String who = TelegramSender.senderName(this);
        senderBtn.setTextColor(Color.BLACK);
        if (who.isEmpty()) {
            setJobText(senderBtn, "보내는 사람 별명 설정 (예: 할머니)", null);
        } else {
            setJobText(senderBtn, "보내는 사람 별명", who);
        }
    }

    private void showSenderDialog() {
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 20, 60, 10);
        TextView info = new TextView(this);
        info.setText("받는 사람이 누구의 위치인지 알 수 있게 모든 위치 메시지(텔레그램·카톡·이메일) 맨 위에 표시됩니다.\n\n"
                + "예) 📍 할머니의 현재 위치 (11-21 14:30:05)");
        info.setTextColor(Color.DKGRAY);
        f.addView(info);
        final EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setHint("예: 할머니, 민준이, 아빠");
        name.setText(TelegramSender.senderName(this));
        f.addView(name);
        new AlertDialog.Builder(this)
                .setTitle("보내는 사람 별명")
                .setView(f)
                .setPositiveButton("저장", (d, w) -> {
                    TelegramSender.setSenderName(this, name.getText().toString());
                    updateSenderButton();
                    Toast.makeText(this, "저장했습니다.", Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("취소", null)
                .show();
    }

    // ---------- 텔레그램 자동 송신 ----------

    private void updateTgButton() {
        SharedPreferences sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        boolean on = sp.getBoolean("on", false);
        setJobText(tgBtn, "텔레그램 (자동)",
                on ? TelegramSender.intervalLabel(sp.getInt("interval", 10)) + "마다" : null);
        tgBtn.setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
        syncCheck(TelegramSender.PREF, on);
    }

    private TextView label(LinearLayout f, String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(14);
        t.setTypeface(null, Typeface.BOLD);
        t.setPadding(0, 24, 0, 4);
        f.addView(t);
        return t;
    }

    // 설정 팝업: 봇 토큰, 받는 사람, 자동 송신 간격, 자동송신시작
    private void showTelegramDialog() {
        final SharedPreferences sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 10, 60, 10);

        // 처음 쓰는 사람용: 단계별 설정 도우미 (누르면 이 팝업을 닫고 도우미 화면으로)
        Button wizard = new Button(this);
        wizard.setText("처음이세요? 설정 도우미 (단계별 안내)");
        wizard.setTextSize(14);
        wizard.setTypeface(null, Typeface.BOLD);
        wizard.setTextColor(0xFF1976D2);
        f.addView(wizard, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        label(f, "봇 토큰");
        final EditText token = new EditText(this);
        token.setSingleLine(true);
        token.setHint("123456789:ABC... (BotFather에서 받은 토큰)");
        token.setText(sp.getString("token", ""));
        // 토큰 칸 + [붙여넣기] (BotFather 메시지 전체를 복사해 와도 토큰만 골라 채움)
        LinearLayout tokenRow = new LinearLayout(this);
        tokenRow.setOrientation(LinearLayout.HORIZONTAL);
        tokenRow.addView(token, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Button paste = new Button(this);
        paste.setText("붙여넣기");
        paste.setTextSize(13);
        paste.setOnClickListener(v -> pasteToken(token));
        tokenRow.addView(paste);
        f.addView(tokenRow);

        label(f, "받는 사람 (채팅 ID · 여러 명은 쉼표로 구분)");
        final EditText chats = new EditText(this);
        chats.setSingleLine(true);
        chats.setInputType(InputType.TYPE_CLASS_TEXT);
        chats.setHint("예: 123456789, -100123456789");
        chats.setText(sp.getString("chats", ""));
        f.addView(chats);
        // 등록된 ID 의 이름 (설정 도우미/받는 사람 찾기로 알게 된 이름)
        StringBuilder names = new StringBuilder();
        for (String id : sp.getString("chats", "").split("[,\\s]+")) {
            String nm = id.isEmpty() ? null : TelegramSender.nameOf(this, id);
            if (nm != null) {
                names.append(names.length() > 0 ? ", " : "").append(nm);
            }
        }
        if (names.length() > 0) {
            TextView nv = new TextView(this);
            nv.setText("등록: " + names);
            nv.setTextColor(Color.GRAY);
            nv.setTextSize(12);
            f.addView(nv);
        }
        Button find = new Button(this);
        find.setText("받는 사람 찾기 (봇에게 /start 한 사람·단톡방)");
        find.setTextSize(13);
        find.setOnClickListener(v -> findTelegramChats(token, chats));
        f.addView(find, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        Button shareBot = new Button(this);
        shareBot.setText("봇 링크 공유 (받는 사람에게 보내기)");
        shareBot.setTextSize(13);
        shareBot.setOnClickListener(v -> shareBotLink(token));
        f.addView(shareBot, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        label(f, "자동 송신 간격");
        final Spinner interval = new Spinner(this);
        interval.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, TelegramSender.INTERVAL_LABELS));
        int cur = sp.getInt("interval", 10);
        for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
            if (TelegramSender.INTERVALS[i] == cur) {
                interval.setSelection(i);
            }
        }
        f.addView(interval);

        final CheckBox on = new CheckBox(this);
        on.setText("자동송신시작");
        on.setTextSize(16);
        on.setTypeface(null, Typeface.BOLD);
        on.setChecked(sp.getBoolean("on", false));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 24;
        // 켜기·끄기는 메인 화면 버튼 앞의 체크박스로 (팝업에는 표시하지 않음)

        TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0);
        last.setText(lt == 0 ? "아직 보낸 적이 없습니다."
                : "마지막 송신: " + new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date(lt))
                + "  " + sp.getString("lastResult", ""));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        f.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(f);

        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle("텔레그램 자동 송신")
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        wizard.setOnClickListener(v -> {
            d.dismiss();
            startActivity(new Intent(this, TelegramSetupActivity.class));
        });
        d.setOnShowListener(x -> {
            // 저장: 입력값 확인 후 저장, 자동송신시작이 체크되어 있으면 실행
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                String tk = token.getText().toString().trim();
                String ch = chats.getText().toString().trim();
                int min = TelegramSender.INTERVALS[interval.getSelectedItemPosition()];
                boolean start = on.isChecked();
                if (start && (tk.isEmpty() || ch.isEmpty())) {
                    Toast.makeText(this, "봇 토큰과 받는 사람을 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                boolean changed = start && (!sp.getBoolean("on", false) || sp.getInt("interval", 10) != min
                        || !tk.equals(sp.getString("token", "")) || !ch.equals(sp.getString("chats", "")));
                SharedPreferences.Editor ed = sp.edit().putString("token", tk).putString("chats", ch)
                        .putInt("interval", min).putBoolean("on", start);
                if (changed) {
                    ed.putLong("next", 0); // 시작하거나 설정이 바뀌면 바로 1회 전송 후 간격대로
                }
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateTgButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + TelegramSender.intervalLabel(min) + "마다)"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                d.dismiss();
            });
            // 지금 테스트: 팝업은 그대로 두고 1회 전송
            d.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
                String tk = token.getText().toString().trim();
                String ch = chats.getText().toString().trim();
                if (tk.isEmpty() || ch.isEmpty()) {
                    Toast.makeText(this, "봇 토큰과 받는 사람을 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                sp.edit().putString("token", tk).putString("chats", ch).apply();
                sendTelegramNow(tk, ch, last);
            });
        });
        d.show();
    }

    // 클립보드에서 봇 토큰(숫자:영문 35자 내외)만 찾아 채우기
    private void pasteToken(EditText tokenEdit) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = cm.getPrimaryClip();
        CharSequence txt = clip != null && clip.getItemCount() > 0 ? clip.getItemAt(0).coerceToText(this) : null;
        if (txt == null || txt.toString().trim().isEmpty()) {
            Toast.makeText(this, "복사된 내용이 없습니다. BotFather 메시지에서 토큰을 먼저 복사하세요.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        String found = TelegramSender.extractToken(txt);
        if (found != null) {
            tokenEdit.setText(found);
            Toast.makeText(this, "봇 토큰을 붙여 넣었습니다.", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "복사된 내용에서 봇 토큰을 찾지 못했습니다.\n(형식: 123456789:AAH...)",
                    Toast.LENGTH_LONG).show();
        }
    }

    // 봇 링크(https://t.me/봇아이디)를 카톡·문자 등으로 보내기 -> 받는 사람이 눌러서 [시작]
    private void shareBotLink(EditText tokenEdit) {
        final String tk = tokenEdit.getText().toString().trim();
        if (tk.isEmpty()) {
            Toast.makeText(this, "봇 토큰을 먼저 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Toast.makeText(this, "봇 정보 확인 중...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            final String[] err = new String[1];
            final String bot = TelegramSender.botUsername(tk, err);
            runOnUiThread(() -> {
                if (bot == null) {
                    new AlertDialog.Builder(this).setTitle("봇 링크 공유 실패")
                            .setMessage(err[0]).setPositiveButton("확인", null).show();
                    return;
                }
                String text = "📍 위치 알림 받기\n\n"
                        + "아래 링크를 눌러 텔레그램에서 [시작]을 눌러 주세요.\n"
                        + "https://t.me/" + bot + "\n\n"
                        + "단톡방에서 받으려면 아래 링크로 봇을 단톡방에 추가해 주세요.\n"
                        + "https://t.me/" + bot + "?startgroup=true";
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("text/plain");
                send.putExtra(Intent.EXTRA_SUBJECT, "위치 알림 받기 (텔레그램)");
                send.putExtra(Intent.EXTRA_TEXT, text);
                startActivity(Intent.createChooser(send, "봇 링크 보내기"));
            });
        }).start();
    }

    // 봇에게 /start 한 사람·단톡방 목록을 가져와 골라서 '받는 사람'에 추가
    private void findTelegramChats(final EditText tokenEdit, final EditText chatsEdit) {
        final String tk = tokenEdit.getText().toString().trim();
        if (tk.isEmpty()) {
            Toast.makeText(this, "봇 토큰을 먼저 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Toast.makeText(this, "찾는 중...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            final String[] err = new String[1];
            final List<String[]> found = TelegramSender.findChats(tk, err);
            runOnUiThread(() -> {
                if (err[0] != null) {
                    new AlertDialog.Builder(this).setTitle("받는 사람 찾기 실패")
                            .setMessage(err[0]).setPositiveButton("확인", null).show();
                    return;
                }
                if (found.isEmpty()) {
                    new AlertDialog.Builder(this).setTitle("받는 사람 찾기")
                            .setMessage("찾은 사람이 없습니다.\n\n"
                                    + "1. 받는 사람이 텔레그램에서 봇을 찾아 [시작](/start)을 누르게 하세요.\n"
                                    + "   (단톡방: 봇을 초대한 뒤 단톡방에서 /start 입력)\n"
                                    + "2. 그다음 다시 [받는 사람 찾기]를 누르세요.\n\n"
                                    + "※ 최근 24시간 안에 봇에게 보낸 메시지만 보입니다. "
                                    + "PC 프로그램이 같은 봇으로 메시지를 받고 있으면 안 보일 수 있습니다.")
                            .setPositiveButton("확인", null).show();
                    return;
                }
                // 이미 등록된 ID 는 체크된 상태로 표시
                Set<String> cur = new HashSet<>();
                for (String s : chatsEdit.getText().toString().split("[,\\s]+")) {
                    if (!s.isEmpty()) {
                        cur.add(s);
                    }
                }
                final String[] labels = new String[found.size()];
                final boolean[] checked = new boolean[found.size()];
                for (int i = 0; i < found.size(); i++) {
                    labels[i] = found.get(i)[1] + "\nID: " + found.get(i)[0];
                    checked[i] = cur.contains(found.get(i)[0]);
                }
                new AlertDialog.Builder(this)
                        .setTitle("받는 사람 선택")
                        .setMultiChoiceItems(labels, checked, (dlg, which, isChecked) -> checked[which] = isChecked)
                        .setPositiveButton("추가", (dlg, w) -> {
                            // 기존 목록 순서 유지 + 새로 체크한 것 추가, 체크 해제한 것은 제거
                            java.util.LinkedHashSet<String> ids = new java.util.LinkedHashSet<>();
                            for (String s : chatsEdit.getText().toString().split("[,\\s]+")) {
                                if (!s.isEmpty()) {
                                    ids.add(s);
                                }
                            }
                            for (int i = 0; i < found.size(); i++) {
                                if (checked[i]) {
                                    ids.add(found.get(i)[0]);
                                } else {
                                    ids.remove(found.get(i)[0]);
                                }
                            }
                            chatsEdit.setText(android.text.TextUtils.join(", ", ids));
                        })
                        .setNegativeButton("취소", null)
                        .show();
            });
        }).start();
    }

    // 지금 화면의 위치(없으면 마지막으로 알려진 위치)를 한 번 전송
    private void sendTelegramNow(final String tk, final String ch, final TextView out) {
        Location l = lastLoc;
        if (l == null && hasPermission()) {
            try {
                for (String p : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    Location x = lm.getLastKnownLocation(p);
                    if (x != null && (l == null || x.getTime() > l.getTime())) {
                        l = x;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        final Location loc = l;
        out.setText("보내는 중...");
        new Thread(() -> {
            String err = TelegramSender.send(tk, ch, TelegramSender.message(this, loc));
            runOnUiThread(() -> {
                String r = err == null ? "성공" : "실패: " + err;
                out.setText("지금 테스트: " + r);
                Toast.makeText(this, err == null ? "텔레그램으로 보냈습니다." : r, Toast.LENGTH_LONG).show();
            });
        }).start();
    }

    // ---------- 카톡 본인 메시지 (수동) (간격마다 알림 -> 누르면 카톡 공유 화면) ----------

    private void updateKakaoButton() {
        SharedPreferences sp = getSharedPreferences(HomeService.PREF_KAKAO, MODE_PRIVATE);
        boolean on = sp.getBoolean("on", false);
        setJobText(kakaoBtn, "카톡 본인 메시지 (수동)",
                on ? TelegramSender.intervalLabel(sp.getInt("interval", 10)) + "마다 알림" : null);
        kakaoBtn.setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
        syncCheck(HomeService.PREF_KAKAO, on);
    }

    private boolean kakaoInstalled() {
        return getPackageManager().getLaunchIntentForPackage(HomeService.KAKAO_PKG) != null;
    }

    // 설정 팝업: 보내기 간격, 자동보내기시작
    private void showKakaoDialog() {
        final SharedPreferences sp = getSharedPreferences(HomeService.PREF_KAKAO, MODE_PRIVATE);
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 10, 60, 10);

        TextView info = new TextView(this);
        info.setText("정해진 간격마다 위치를 확인해 알림을 띄웁니다.\n"
                + "알림을 누르면 위치가 채워진 카톡 화면이 열리고, "
                + "받는 사람을 골라 [전송]을 누르면 보내집니다.\n"
                + "(카톡은 완전 자동 전송을 지원하지 않습니다)");
        info.setTextColor(Color.DKGRAY);
        info.setPadding(0, 16, 0, 0);
        f.addView(info);

        label(f, "보내기 간격");
        final Spinner interval = new Spinner(this);
        interval.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, TelegramSender.INTERVAL_LABELS));
        int cur = sp.getInt("interval", 10);
        for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
            if (TelegramSender.INTERVALS[i] == cur) {
                interval.setSelection(i);
            }
        }
        f.addView(interval);

        final CheckBox on = new CheckBox(this);
        on.setText("자동보내기시작");
        on.setTextSize(16);
        on.setTypeface(null, Typeface.BOLD);
        on.setChecked(sp.getBoolean("on", false));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 24;
        // 켜기·끄기는 메인 화면 버튼 앞의 체크박스로 (팝업에는 표시하지 않음)

        TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0);
        last.setText(lt == 0 ? "아직 알림을 보낸 적이 없습니다."
                : "마지막 알림: " + new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date(lt)));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        f.addView(last);
        if (!kakaoInstalled()) {
            last.setText("카카오톡이 설치되어 있지 않습니다.");
            last.setTextColor(0xFFC62828);
        }

        ScrollView sv = new ScrollView(this);
        sv.addView(f);

        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle("카톡 본인 메시지 (수동)")
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        d.setOnShowListener(x -> {
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                int min = TelegramSender.INTERVALS[interval.getSelectedItemPosition()];
                boolean start = on.isChecked();
                if (start && !kakaoInstalled()) {
                    Toast.makeText(this, "카카오톡이 설치되어 있지 않습니다.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                boolean changed = start && (!sp.getBoolean("on", false) || sp.getInt("interval", 10) != min);
                SharedPreferences.Editor ed = sp.edit().putInt("interval", min).putBoolean("on", start);
                if (changed) {
                    ed.putLong("next", 0); // 시작하거나 간격이 바뀌면 바로 1회 알림 후 간격대로
                }
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateKakaoButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + TelegramSender.intervalLabel(min) + "마다 알림)"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                d.dismiss();
            });
            // 지금 테스트: 바로 카톡 공유 화면 열기
            d.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
                if (!kakaoInstalled()) {
                    Toast.makeText(this, "카카오톡이 설치되어 있지 않습니다.", Toast.LENGTH_SHORT).show();
                    return;
                }
                last.setText("위치 메시지 만드는 중...");
                sendKakaoNow(d);
            });
        });
        d.show();
    }

    // 지금 화면의 위치(없으면 마지막으로 알려진 위치)로 카톡 공유 화면 열기
    private void sendKakaoNow(final AlertDialog d) {
        final Location loc = currentOrLastLocation();
        new Thread(() -> {
            String msg = TelegramSender.message(this, loc);
            runOnUiThread(() -> {
                d.dismiss();
                try {
                    startActivity(HomeService.kakaoShare(msg));
                } catch (Exception e) {
                    Toast.makeText(this, "카카오톡을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    // ---------- 이메일 자동 송신 (정해진 시각/간격마다: 앱 비밀번호로 자동 발송, 또는 알림 -> Gmail 작성 화면) ----------

    // 일반 이메일 / 카톡 이메일 버튼 글자 (체크 표시, 시각, 자동/수동)
    private void updateEmailButton() {
        String[] prefs = {HomeService.PREF_EMAIL, HomeService.PREF_KMAIL};
        Button[] btns = {emailBtn, kmailBtn};
        for (int i = 0; i < 2; i++) {
            SharedPreferences sp = getSharedPreferences(prefs[i], MODE_PRIVATE);
            boolean on = sp.getBoolean("on", false);
            String name = HomeService.emailTitle(prefs[i]) + " (자동 / 수동)";
            setJobText(btns[i], name, on ? HomeService.emailSchedule(sp)
                    + (HomeService.emailSmtp(sp) ? " · 자동" : " · 수동") : null);
            btns[i].setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
            syncCheck(prefs[i], on);
        }
    }

    // 카톡 이메일 받는 주소: 카카오메일 도메인만 (kakao.com, daum.net, hanmail.net)
    private static boolean kakaoMailOnly(String s) {
        for (String e : s.split("[,;\\s]+")) {
            String d = e.substring(e.indexOf('@') + 1).toLowerCase(Locale.US);
            if (!e.isEmpty() && !d.equals("kakao.com") && !d.equals("daum.net") && !d.equals("hanmail.net")) {
                return false;
            }
        }
        return true;
    }

    // "8:5, 18:30, 08:05" -> 정렬/중복 제거한 "08:05, 18:30". 틀린 형식이 있으면 null
    private static String normalizeTimes(String s) {
        java.util.TreeSet<String> set = new java.util.TreeSet<>();
        for (String t : s.split("[,\\s]+")) {
            if (t.isEmpty()) {
                continue;
            }
            int[] hm = HomeService.parseTime(t);
            if (hm == null) {
                return null;
            }
            set.add(String.format(Locale.US, "%02d:%02d", hm[0], hm[1]));
        }
        return android.text.TextUtils.join(", ", set);
    }

    // 설정 팝업: 보내는 방식, 받는 사람, 보내는 시각(지정 시각/일정 간격), 자동송신시작
    // pref: HomeService.PREF_EMAIL(일반 이메일) / HomeService.PREF_KMAIL(카톡 이메일: 받는 주소가 카카오메일)
    private void showEmailDialog(final String pref) {
        final boolean kakao = HomeService.PREF_KMAIL.equals(pref);
        final SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        LinearLayout f = new LinearLayout(this);
        f.setOrientation(LinearLayout.VERTICAL);
        f.setPadding(60, 10, 60, 10);

        // 보내는 방식: 자동 발송(앱 비밀번호) / Gmail 작성 화면(알림 -> 직접 보내기)
        label(f, "보내는 방식");
        final RadioGroup how = new RadioGroup(this);
        final RadioButton rSmtp = new RadioButton(this), rCompose = new RadioButton(this);
        rSmtp.setText("완전 자동 발송 (앱 비밀번호)");
        rSmtp.setId(View.generateViewId());
        rCompose.setText("Gmail 작성 화면 (알림 → 직접 [보내기])");
        rCompose.setId(View.generateViewId());
        how.addView(rSmtp);
        how.addView(rCompose);
        f.addView(how);

        final TextView info = new TextView(this);
        info.setTextColor(Color.DKGRAY);
        info.setPadding(0, 8, 0, 0);
        f.addView(info);

        // 자동 발송 전용: 보내는 이메일 + 앱 비밀번호
        final LinearLayout smtpBox = new LinearLayout(this);
        smtpBox.setOrientation(LinearLayout.VERTICAL);
        label(smtpBox, "보내는 이메일 (내 계정)");
        final EditText from = new EditText(this);
        from.setSingleLine(true);
        from.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        from.setHint("예: mygmail@gmail.com");
        // 카톡 이메일을 처음 설정할 때는 일반 이메일에 넣어 둔 보내는 계정을 미리 채움
        SharedPreferences gen = getSharedPreferences(HomeService.PREF_EMAIL, MODE_PRIVATE);
        from.setText(sp.getString("from", kakao ? gen.getString("from", "") : ""));
        smtpBox.addView(from);
        label(smtpBox, "앱 비밀번호 (16자리)");
        final EditText pass = new EditText(this);
        pass.setSingleLine(true);
        pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        pass.setHint("abcd efgh ijkl mnop");
        pass.setText(sp.getString("pass", kakao ? gen.getString("pass", "") : ""));
        smtpBox.addView(pass);
        final CheckBox showPass = new CheckBox(this);
        showPass.setText("비밀번호 보기");
        showPass.setOnCheckedChangeListener((b, c) -> {
            pass.setInputType(InputType.TYPE_CLASS_TEXT | (c
                    ? InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD : InputType.TYPE_TEXT_VARIATION_PASSWORD));
            pass.setSelection(pass.getText().length());
        });
        smtpBox.addView(showPass);
        TextView help = new TextView(this);
        help.setAutoLinkMask(android.text.util.Linkify.WEB_URLS); // 링크를 눌러 브라우저로 열기 (setText 전에 설정)
        help.setText("앱 비밀번호 만들기 (구글 2단계 인증이 켜져 있어야 함):\n"
                + "https://myaccount.google.com/apppasswords\n"
                + "(네이버: 내정보 → 보안설정 → 2단계 인증 → 애플리케이션 비밀번호, 메일 환경설정에서 SMTP 사용)");
        help.setTextColor(Color.GRAY);
        help.setTextSize(12);
        smtpBox.addView(help);
        f.addView(smtpBox);

        how.setOnCheckedChangeListener((g, id) -> {
            boolean smtp = id == rSmtp.getId();
            smtpBox.setVisibility(smtp ? View.VISIBLE : View.GONE);
            info.setText(smtp
                    ? "정해진 시각마다 위치를 확인해, 알림 없이 바로 메일을 보냅니다."
                    : "정해진 시각마다 위치를 확인해 알림을 띄웁니다. 알림을 누르면 내용이 채워진 "
                    + "Gmail 작성 화면이 열리고, [보내기]를 누르면 보내집니다.");
        });
        how.check(HomeService.emailSmtp(sp) ? rSmtp.getId() : rCompose.getId());

        label(f, kakao ? "받는 사람 카카오메일 (여러 명은 쉼표로 구분)" : "받는 사람 (이메일 · 여러 명은 쉼표로 구분)");
        final EditText to = new EditText(this);
        to.setSingleLine(true);
        to.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        to.setHint(kakao ? "예: minjun@kakao.com" : "예: friend@gmail.com, family@naver.com");
        to.setText(sp.getString("to", ""));
        f.addView(to);
        if (kakao) { // 카톡 이메일: 카카오메일 안내 + 주소 확인 버튼
            TextView kakaoHint = new TextView(this);
            kakaoHint.setText("💡 카카오메일(@kakao.com)로 보내면 받는 사람이 카카오톡에서 바로 받아볼 수 있습니다.\n"
                    + "카톡 ID와 메일 주소는 다를 수 있으니 실제 주소를 확인하세요.");
            kakaoHint.setTextColor(Color.GRAY);
            kakaoHint.setTextSize(12);
            f.addView(kakaoHint);
            Button kakaoMail = new Button(this);
            kakaoMail.setText("카톡 메일 열기 (내 @kakao.com 주소 확인)");
            kakaoMail.setTextSize(13);
            kakaoMail.setOnClickListener(v -> openKakaoMail());
            f.addView(kakaoMail, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        label(f, "보내는 시각");
        final RadioGroup mode = new RadioGroup(this);
        final RadioButton rTimes = new RadioButton(this), rInterval = new RadioButton(this);
        rTimes.setText("지정 시각 (매일)");
        rTimes.setId(View.generateViewId());
        rInterval.setText("일정 간격");
        rInterval.setId(View.generateViewId());
        mode.addView(rTimes);
        mode.addView(rInterval);
        f.addView(mode);

        // 지정 시각: 직접 입력 + [시각 추가] (시계에서 고르기)
        final LinearLayout timesRow = new LinearLayout(this);
        timesRow.setOrientation(LinearLayout.HORIZONTAL);
        final EditText times = new EditText(this);
        times.setSingleLine(true);
        times.setHint("예: 08:00, 12:00, 18:00");
        times.setText(sp.getString("times", ""));
        Button addTime = new Button(this);
        addTime.setText("시각 추가");
        addTime.setTextSize(13);
        addTime.setOnClickListener(v -> new TimePickerDialog(this, (tp, h, m) -> {
            String cur = times.getText().toString().trim();
            String add = String.format(Locale.US, "%02d:%02d", h, m);
            String n = normalizeTimes(cur.isEmpty() ? add : cur + ", " + add);
            times.setText(n == null ? cur + ", " + add : n);
        }, 8, 0, true).show());
        timesRow.addView(times, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        timesRow.addView(addTime);
        f.addView(timesRow);

        // 일정 간격
        final Spinner interval = new Spinner(this);
        interval.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, TelegramSender.INTERVAL_LABELS));
        int cur = sp.getInt("interval", 60);
        for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
            if (TelegramSender.INTERVALS[i] == cur) {
                interval.setSelection(i);
            }
        }
        f.addView(interval);

        mode.setOnCheckedChangeListener((g, id) -> {
            timesRow.setVisibility(id == rTimes.getId() ? View.VISIBLE : View.GONE);
            interval.setVisibility(id == rInterval.getId() ? View.VISIBLE : View.GONE);
        });
        mode.check("times".equals(sp.getString("mode", "times")) ? rTimes.getId() : rInterval.getId());

        final CheckBox on = new CheckBox(this);
        on.setText("자동송신시작");
        on.setTextSize(16);
        on.setTypeface(null, Typeface.BOLD);
        on.setChecked(sp.getBoolean("on", false));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 24;
        // 켜기·끄기는 메인 화면 버튼 앞의 체크박스로 (팝업에는 표시하지 않음)

        final TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0), nx = sp.getLong("next", 0);
        SimpleDateFormat fmt = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA);
        boolean wasSmtp = HomeService.emailSmtp(sp);
        last.setText((lt == 0 ? "아직 보낸 적이 없습니다."
                : (wasSmtp ? "마지막 발송: " + fmt.format(new Date(lt)) + "  " + sp.getString("lastResult", "")
                : "마지막 알림: " + fmt.format(new Date(lt))))
                + (sp.getBoolean("on", false) && nx > 0 ? "\n다음 송신: " + fmt.format(new Date(nx)) : ""));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        f.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(f);

        final AlertDialog d = new AlertDialog.Builder(this)
                .setTitle(HomeService.emailTitle(pref) + " (자동 / 수동)")
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        d.setOnShowListener(x -> {
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                String addr = to.getText().toString().trim();
                boolean byTimes = mode.getCheckedRadioButtonId() == rTimes.getId();
                String tm = normalizeTimes(times.getText().toString());
                int min = TelegramSender.INTERVALS[interval.getSelectedItemPosition()];
                boolean start = on.isChecked();
                boolean smtp = how.getCheckedRadioButtonId() == rSmtp.getId();
                String fr = from.getText().toString().trim();
                String pw = pass.getText().toString().replace(" ", "");
                if (!addr.isEmpty() && !validEmails(addr)) {
                    Toast.makeText(this, "이메일 주소 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (kakao && !kakaoMailOnly(addr)) {
                    Toast.makeText(this, "카톡 이메일은 카카오메일 주소(@kakao.com, @daum.net, @hanmail.net)만 "
                            + "넣을 수 있습니다. 다른 주소는 [일반 이메일]을 쓰세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                if (!fr.isEmpty() && !Patterns.EMAIL_ADDRESS.matcher(fr).matches()) {
                    Toast.makeText(this, "보내는 이메일 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && smtp && (fr.isEmpty() || pw.isEmpty())) {
                    Toast.makeText(this, "보내는 이메일과 앱 비밀번호를 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (byTimes && tm == null) {
                    Toast.makeText(this, "시각은 08:00 처럼 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && addr.isEmpty()) {
                    Toast.makeText(this, "받는 사람 이메일을 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && byTimes && tm.isEmpty()) {
                    Toast.makeText(this, "보낼 시각을 하나 이상 등록하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                SharedPreferences.Editor ed = sp.edit().putString("to", addr)
                        .putString("send", smtp ? "smtp" : "compose")
                        .putString("from", fr).putString("pass", pw)
                        .putString("mode", byTimes ? "times" : "interval")
                        .putString("times", tm == null ? "" : tm)
                        .putInt("interval", min).putBoolean("on", start);
                ed.apply();
                if (start) {
                    // 지정 시각: 다음 시각에 알림 / 일정 간격: 바로 1회 알림 후 간격대로
                    sp.edit().putLong("next", byTimes
                            ? HomeService.nextTime(tm, System.currentTimeMillis()) : 0).apply();
                }
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateEmailButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + HomeService.emailSchedule(sp) + ")"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                d.dismiss();
            });
            // 지금 테스트: 자동 발송이면 바로 전송(팝업 유지), 아니면 Gmail 작성 화면 열기
            d.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
                String addr = to.getText().toString().trim();
                if (addr.isEmpty() || !validEmails(addr)) {
                    Toast.makeText(this, "받는 사람 이메일을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (kakao && !kakaoMailOnly(addr)) {
                    Toast.makeText(this, "카톡 이메일은 카카오메일 주소(@kakao.com 등)만 넣을 수 있습니다.",
                            Toast.LENGTH_LONG).show();
                    return;
                }
                sp.edit().putString("to", addr).apply();
                final Location loc = currentOrLastLocation();
                if (how.getCheckedRadioButtonId() == rSmtp.getId()) {
                    final String fr = from.getText().toString().trim();
                    final String pw = pass.getText().toString().replace(" ", "");
                    if (!Patterns.EMAIL_ADDRESS.matcher(fr).matches() || pw.isEmpty()) {
                        Toast.makeText(this, "보내는 이메일과 앱 비밀번호를 확인하세요.", Toast.LENGTH_SHORT).show();
                        return;
                    }
                    sp.edit().putString("from", fr).putString("pass", pw).apply();
                    last.setText("보내는 중...");
                    new Thread(() -> {
                        String err = EmailSender.send(fr, pw, addr, HomeService.emailSubject(this),
                                TelegramSender.message(this, loc));
                        runOnUiThread(() -> {
                            String r = err == null ? "성공" : "실패: " + err;
                            last.setText("지금 테스트: " + r);
                            Toast.makeText(this, err == null ? "메일을 보냈습니다." : r, Toast.LENGTH_LONG).show();
                        });
                    }).start();
                    return;
                }
                last.setText("위치 메시지 만드는 중...");
                new Thread(() -> {
                    String msg = TelegramSender.message(this, loc);
                    runOnUiThread(() -> {
                        d.dismiss();
                        try {
                            startActivity(HomeService.emailCompose(this, addr, HomeService.emailSubject(this), msg));
                        } catch (Exception e) {
                            Toast.makeText(this, "메일 앱을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                        }
                    });
                }).start();
            });
        });
        d.show();
    }

    // 카카오메일 웹(mail.kakao.com) 열기 -> 안 되면 카카오톡 앱 열기
    private void openKakaoMail() {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://mail.kakao.com")));
            return;
        } catch (Exception ignored) {
        }
        Intent kt = getPackageManager().getLaunchIntentForPackage(HomeService.KAKAO_PKG);
        if (kt != null) {
            startActivity(kt);
            Toast.makeText(this, "카카오톡에서 메일 메뉴를 찾아 주세요.", Toast.LENGTH_LONG).show();
        } else {
            Toast.makeText(this, "카카오메일을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    private static boolean validEmails(String s) {
        for (String e : s.split("[,;\\s]+")) {
            if (!e.isEmpty() && !Patterns.EMAIL_ADDRESS.matcher(e).matches()) {
                return false;
            }
        }
        return true;
    }

    private Location currentOrLastLocation() {
        Location l = lastLoc;
        if (l == null && hasPermission()) {
            try {
                for (String p : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    Location x = lm.getLastKnownLocation(p);
                    if (x != null && (l == null || x.getTime() > l.getTime())) {
                        l = x;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        return l;
    }

    private static String now() {
        return new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date());
    }
}