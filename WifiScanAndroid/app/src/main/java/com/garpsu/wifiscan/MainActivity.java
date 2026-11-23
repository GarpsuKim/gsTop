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
    private Button tgBtn, kakaoBtn, emailBtn, selfBtn, smsBtn, smsAutoBtn;
    private static final int REQ_CONTACT = 3;   // 연락처에서 전화번호 고르기
    private static final int REQ_BT = 4;        // 블루투스(근처 기기) 권한
    private static final int REQ_CALL = 5;      // 전화 걸기 권한 (SOS·보호자 바로 걸기)
    private TextView sosInfo;                   // (3) 탭: SOS 설정 요약
    private LinearLayout btBox;                 // (1) 탭: 홈 PC 블루투스
    private EditText contactTarget;             // 고른 번호를 넣을 칸
    private boolean contactWithName;            // true: "이름 번호"로 넣기 (보호자), false: 번호만 (문자)
    private EditText contactNameTarget;         // (6) 피보호자 카드: 번호 칸을 바꾸고, 이름 칸이 비었으면 이름도 채움
    private LinearLayout msgBox; // (3) 위치 보내기 탭: 정해진 시각 위치 보내기 버튼 묶음
    private static final int MODE_NONE = 0, MODE_WIFI = 1, MODE_LOC = 3, MODE_LAN = 4;

    private WifiManager wm;
    private LocationManager lm;
    private TextView text, status, statusNote; // text: 가장 최근에 검색을 시작한 탭의 결과 화면
    private LinearLayout wifiRows; // 공유기 체크박스 목록 (탭1)
    private static final int TABS = 6; // 화면 탭: (1) 공유기 (2) 알림 설정 (3) 위치 보내기 (4) 응답 모드 (5) 보호자 전용 모드. 내부 자리 1번(옛 현재 위치)은 숨김 -> 1020 팝업
    private final TextView[] texts = new TextView[TABS];
    private final Button[] startBtns = new Button[TABS]; // (4) 탭에는 [검색시작]이 없음 (null)
    private final Button[] tabBtns = new Button[TABS];
    private final LinearLayout[] panels = new LinearLayout[TABS];
    private int curTab = 0;
    private boolean statusHome; // 메인 상태: true = 홈(HomeBack), false = 외출(NotHome)
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int mode = MODE_NONE;
    private int scanId = 0; // 기기 목록 결과가 다른 화면을 덮어쓰지 않게 구분

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
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
            if (mode != MODE_LOC || loc == null) {
                return;
            }
            // 지금 보이는 위치보다 덜 정확한 새 값은, 보이는 값이 15초 넘게 묵었을 때만 바꿈
            Location cur = lastLoc;
            if (cur != null && cur.hasAccuracy() && loc.hasAccuracy() && loc.getAccuracy() > cur.getAccuracy()
                    && loc.getTime() - cur.getTime() < 15 * 1000) {
                return;
            }
            showLocation(loc, false);
        }
    };

    // 구글 통합 위치 (GPS + Wi-Fi + 기지국 + 센서). Play 서비스가 없으면 안 씀
    private com.google.android.gms.location.FusedLocationProviderClient fused;
    private com.google.android.gms.location.LocationCallback fusedCb;

    private boolean startFused() {
        stopFused();
        try {
            if (com.google.android.gms.common.GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(this)
                    != com.google.android.gms.common.ConnectionResult.SUCCESS) {
                return false;
            }
            if (fused == null) {
                fused = com.google.android.gms.location.LocationServices.getFusedLocationProviderClient(this);
            }
            fusedCb = new com.google.android.gms.location.LocationCallback() {
                @Override
                public void onLocationResult(com.google.android.gms.location.LocationResult result) {
                    for (Location loc : result.getLocations()) {
                        locListener.onLocationChanged(loc);
                    }
                }
            };
            fused.requestLocationUpdates(new com.google.android.gms.location.LocationRequest.Builder(
                            com.google.android.gms.location.Priority.PRIORITY_HIGH_ACCURACY, 1000)
                            .setMinUpdateIntervalMillis(500).build(),
                    fusedCb, Looper.getMainLooper());
            return true;
        } catch (Throwable e) {
            fusedCb = null;
            return false;
        }
    }

    private void stopFused() {
        if (fused != null && fusedCb != null) {
            fused.removeLocationUpdates(fusedCb);
        }
        fusedCb = null;
    }

    // 위치 갱신 모두 멈춤 (기본 GPS·통신망 + 구글 통합 위치)
    private void stopLocUpdates() {
        lm.removeUpdates(locListener);
        stopFused();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        // 상단: 좌 [초기화] / 가운데 [☐ 백그라운드 실행] / 우 [종료]
        Button reset = new Button(this);
        reset.setText("초기화");
        reset.setTextColor(0xFFC62828);
        reset.setOnClickListener(view -> confirmReset());

        bgCheck = new CheckBox(this); // 백그라운드 귀가 감지 켜기/끄기 (앱을 닫아도 상단 알림으로 표시)
        bgCheck.setText("백그라운드 실행");
        bgCheck.setTextSize(15);
        bgCheck.setTypeface(null, Typeface.BOLD);
        bgCheck.setChecked(HomeService.homeOn(this));
        bgCheck.setOnClickListener(view -> setBackground(bgCheck.isChecked()));
        LinearLayout center = new LinearLayout(this);
        center.setGravity(Gravity.CENTER);
        center.addView(bgCheck);

        Button exit = new Button(this);
        exit.setText("종료");
        exit.setOnClickListener(view -> finish());

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(reset, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        top.addView(center, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        top.addView(exit, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 상태: 홈 / 외출
        status = new TextView(this);
        status.setTextSize(40); // "홈" / "외출" 두 글자라 크게 (양옆에 [현재 위치]·[SOS] 버튼)
        status.setTypeface(null, Typeface.BOLD);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 30, 0, 0);

        statusNote = new TextView(this);
        statusNote.setTextSize(14);
        statusNote.setTextColor(Color.GRAY);
        statusNote.setGravity(Gravity.CENTER);
        statusNote.setPadding(0, 0, 0, 20);

        // 탭 4개: 홈 공유기 등록 / 기기 목록 / 현재 위치 / 알림 설정
        final String[] names = {"(1) 공유기", "현재 위치(숨김)", "(2) 알림 설정", "(3) 위치 보내기", "(4) 응답 모드",
                "(5) 보호자 전용 모드"};
        final Runnable[] starts = {this::wifiScan, this::location, null, null, null, null};

        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        // 탭 높이 통일: 글자 줄 수가 달라도 모든 탭이 가장 높은 탭 높이로 (위치도 글자 기준선 대신 위쪽 맞춤)
        tabs.setBaselineAligned(false);
        for (int i = 0; i < TABS; i++) {
            final int idx = i;
            tabBtns[i] = addButton(tabs, names[i], view -> selectTab(idx));
            tabBtns[i].setTextSize(10); // 탭 6개라 글자를 작게
            tabBtns[i].setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        }
        // (2) 현재 위치(1020)는 탭 메뉴에서 떼어 앱 시작 팝업으로 독립. 다른 탭 번호가 밀리지 않게 자리만 남기고 숨김
        tabBtns[1].setVisibility(View.GONE);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.addView(top, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        // 상태(홈/외출) 왼쪽 + 큰 [SOS] 오른쪽 ([종료] 바로 아래)
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
        // 2번째 줄: 왼쪽 [현재 위치](화면 1020 팝업) / 가운데 홈·외출 / 오른쪽 [SOS]
        int sosSize = (int) (100 * getResources().getDisplayMetrics().density); // 약 100dp 정사각형 (양쪽 같은 크기)
        Button locBtn = new Button(this);
        locBtn.setText("📍\n현재\n위치");
        locBtn.setTextSize(18);
        locBtn.setTypeface(null, Typeface.BOLD);
        locBtn.setTextColor(Color.WHITE);
        locBtn.setBackgroundColor(0xFF1976D2);
        locBtn.setAllCaps(false);
        locBtn.setGravity(Gravity.CENTER);
        locBtn.setPadding(10, 10, 10, 10);
        locBtn.setOnClickListener(view -> showLocPopup());
        LinearLayout.LayoutParams locLp = new LinearLayout.LayoutParams(sosSize, sosSize);
        locLp.setMargins(16, 8, 0, 8);
        statusRow.addView(locBtn, locLp);
        statusRow.addView(statusCol, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        LinearLayout.LayoutParams sosLp = new LinearLayout.LayoutParams(sosSize, sosSize);
        sosLp.setMargins(0, 8, 16, 8);
        statusRow.addView(makeSosButton(), sosLp);
        root.addView(statusRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(tabs, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 탭마다: [검색시작/재검색] 버튼(탭1만 [저장] 추가) + 결과 화면. (2) 알림 설정 탭은 설정 화면만
        for (int i = 0; i < TABS; i++) {
            final int idx = i;
            LinearLayout actions = new LinearLayout(this);
            actions.setOrientation(LinearLayout.HORIZONTAL);
            // 1010 의 [재검색] 은 영역 3 맨 위로 옮김, (2) 현재 위치(1020)는 탭에서 떼어 팝업으로 (showLocPopup)
            if (i == 0) {
                addButton(actions, "등록 확인", view -> showRegistered());
                addButton(actions, "나의 보호자", view -> showSosSettings()); // 보호자 전화번호 (SOS·바로 걸기)
                addButton(actions, "사용자 등록", view -> startActivity(new Intent(this, UserActivity.class))); // 화면 1075
            }

            texts[i] = new TextView(this);
            texts[i].setTextSize(15);
            texts[i].setPadding(40, 40, 40, 40);
            texts[i].setText(i >= 2 ? "" : i == 0 ? "[재검색]을 눌러 주세요." : "[검색시작]을 눌러 주세요.");

            LinearLayout body = new LinearLayout(this);
            body.setOrientation(LinearLayout.VERTICAL);
            if (i == 2) {
                buildAlarmTab(body); // (2) 알림 설정: 귀가·외출 순간 알림
            } else if (i == 3) {
                buildSendTab(body);  // (3) 위치 보내기: 정해진 시각 위치 보내기
            } else if (i == 4) {
                buildGuardianTab(body); // (4) 응답 모드: 원격 위치 요청에 회신
            } else if (i == 5) {
                buildWardTab(body);     // (5) 보호자 전용 모드: 피보호자 관리 (화면 1058)
            }
            if (i == 0) {
                // (백그라운드 귀가 감지 체크는 화면 맨 위 가운데 [백그라운드 실행]으로 옮김)
                savedBox = new LinearLayout(this); // 저장된 공유기 목록 (스캔 결과 위)
                savedBox.setOrientation(LinearLayout.VERTICAL);
                body.addView(savedBox);
                body.addView(thickDivider(), thickDividerLp()); // 영역 1(저장된 공유기) | 영역 2

                btBox = new LinearLayout(this); // 홈 PC 블루투스 (페어링된 기기에서 고르기)
                btBox.setOrientation(LinearLayout.VERTICAL);
                body.addView(btBox);
                renderBt();
                body.addView(thickDivider(), thickDividerLp()); // 영역 2(홈 PC 블루투스) | 영역 3(검색 결과)
                // 영역 3 맨 위: [재검색] (공유기 검색, 예전 상단 [검색시작])
                LinearLayout scanRow = new LinearLayout(this);
                scanRow.setPadding(30, 0, 30, 0);
                startBtns[0] = addButton(scanRow, "재검색", view -> wifiScan());
                body.addView(scanRow);
            }
            if (i < 2) { // (3)·(4) 탭은 결과 글자 화면 없음
                body.addView(texts[i]);
            }
            if (i == 0) {
                wifiRows = new LinearLayout(this);
                wifiRows.setOrientation(LinearLayout.VERTICAL);
                body.addView(wifiRows);
                body.addView(registerRow(view -> saveRouters())); // 영역 3(검색 결과) 오른쪽 아래 [등록]
            }
            ScrollView scroll = new ScrollView(this);
            scroll.addView(body);

            panels[i] = new LinearLayout(this);
            panels[i].setOrientation(LinearLayout.VERTICAL);
            // 내용 영역 첫 줄: 탭 제목을 좌우로 긴 파란 바(흰 글씨)로 한 번 더 (선택된 탭 색과 같음)
            TextView titleBar = new TextView(this);
            titleBar.setText(names[i]);
            titleBar.setTextSize(17);
            titleBar.setTypeface(null, Typeface.BOLD);
            titleBar.setTextColor(Color.WHITE);
            titleBar.setBackgroundColor(0xFF1976D2);
            titleBar.setGravity(Gravity.CENTER);
            titleBar.setPadding(dpx(12), dpx(10), dpx(12), dpx(10));
            panels[i].addView(titleBar, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            if (actions.getChildCount() > 0) { // (3)·(4) 탭은 위쪽 버튼 줄 없음
                panels[i].addView(actions, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            }
            panels[i].addView(scroll, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
            // 화면 번호 1010~1050 (맨 아래 검은 줄). (6) 탭은 1060 이 SOS 화면이라 1058
            panels[i].addView(ScreenId.label(this, i == 5 ? "1058" : String.valueOf(1010 + i * 10)));
            root.addView(panels[i], new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        }

        setContentView(root);

        text = texts[0];
        selectTab(0);
        checkHome(); // 실행 처음에 무조건 홈 / 외출 판정
        if (savedInstanceState == null) {
            HomeService.mailRescan(this); // 앱 시작: 받은편지함에 남은 위치 요청 메일을 찾아 회신·삭제
            // 앱 시작: 먼저 현재 위치 팝업(1020). [닫기]를 누르면 탭 메뉴로 (공유기 안내 팝업은 그 다음)
            showLocPopup();
        } else {
            afterLocPopup();
        }
        handleMailAsk(getIntent()); // 이메일 위치 질문 알림을 눌러 열린 경우 (화면 1057, 현재 위치 팝업보다 위에)
        if (HomeService.anyOn(this) && hasPermission()) {
            HomeService.refresh(this); // 혹시 종료되어 있으면 다시 시작
        }
        // 앱을 새로 켤 때마다 한 번: 현재 위치를 '보낼 곳'으로 통보 (화면 회전·초기화로 다시 그릴 때는 제외)
        if (savedInstanceState == null) {
            startMsgPending = true;
            sendStartMessageOnce();
        }
    }

    // 앱 종료 위치 통보: [종료] 버튼·뒤로 가기로 앱 화면을 닫을 때 한 번 ("[집에 왔어요] 앱이 종료됩니다")
    // ※ 최근 앱 목록에서 밀어서 지우거나 시스템이 강제로 끝내면 Android 가 알려 주지 않아 보낼 수 없음
    @Override
    protected void onDestroy() {
        if (isFinishing() && hasPermission()) {
            try {
                HomeService.stopMessage(this);
            } catch (Exception ignored) {
            }
        }
        super.onDestroy();
    }

    // 앱 시작 위치 통보: 위치 권한이 있으면 바로, 없으면 권한을 허용한 직후에 한 번
    private boolean startMsgPending;

    private void sendStartMessageOnce() {
        if (!startMsgPending || !hasPermission()) {
            return;
        }
        startMsgPending = false;
        try {
            HomeService.startMessage(this);
        } catch (Exception ignored) {
        }
    }

    // 시각 선택 같은 팝업: 띄우고 나서 화면 번호 붙이기 (Dialog.show() 는 값을 돌려주지 않아서 따로 둠)
    private static void showTagged(android.app.Dialog dialog, String id) {
        dialog.show();
        ScreenId.tag(dialog, id);
    }

    // ---------- [초기화]: 모든 설정 삭제 -> 처음 상태 ----------

    // 이 앱이 쓰는 설정 저장소 전부 (사용자 등록 포함, 백업·복원과 같은 목록)
    private static final String[] ALL_PREFS = UserActivity.ALL_PREFS;

    private void confirmReset() {
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("초기화")
                .setMessage("모든 설정을 지우고 처음 설치한 상태로 되돌립니다.\n\n"
                        + "지워지는 것:\n"
                        + " · 홈 공유기, 홈 PC 블루투스 등록\n"
                        + " · 백그라운드 실행, 귀가·외출 알림\n"
                        + " · 위치 보내기 설정 (받는 사람, 텔레그램 봇 토큰, 이메일 앱 비밀번호 등)\n"
                        + " · 보내는 사람 별명, 기기 목록 기록\n\n"
                        + "되돌릴 수 없습니다. 계속할까요?")
                .setPositiveButton("초기화", (dialog, which) -> resetAll())
                .setNegativeButton("취소", null)
                .show(), "1110");
    }

    private void resetAll() {
        HomeService.stopAll(this); // 백그라운드 서비스·예약 알람·알림 모두 정리
        for (String prefName : ALL_PREFS) {
            getSharedPreferences(prefName, MODE_PRIVATE).edit().clear().commit(); // 화면 다시 그리기 전에 확실히 저장
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
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("집에왔어요 · 홈 공유기 등록")
                .setMessage("홈 공유기를 등록하세요.\n\n귀가 할 때 마다 자동으로 검색하여, "
                        + "\"홈\" 또는 \"외출\" 상태를 확인합니다.\n\n"
                        + "어린이, 노약자, 치매어르신 들의 안심 귀가 확인에 유용합니다.")
                .setPositiveButton("확인", null)
                .setOnDismissListener(dialog -> ensurePermission())
                .show(), "1080");
    }

    private void selectTab(int sel) {
        curTab = sel;
        for (int i = 0; i < TABS; i++) {
            panels[i].setVisibility(i == sel ? View.VISIBLE : View.GONE);
            tabBtns[i].setBackgroundColor(i == sel ? 0xFF1976D2 : 0xFFDDDDDD);
            tabBtns[i].setTextColor(i == sel ? Color.WHITE : Color.BLACK);
        }
    }

    private Button addButton(LinearLayout row, String label, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(label);
        btn.setTextSize(13);
        btn.setMinWidth(0);
        btn.setMinimumWidth(0);
        btn.setPadding(4, 0, 4, 0);
        btn.setOnClickListener(listener);
        row.addView(btn, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return btn;
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
            updateEventInfo();
        }
        renderBt(); // 블루투스 설정(페어링)에서 돌아왔을 때 목록 갱신
        updateRemoteInfo();
        updateSosInfo();
        updateWardHead(); // 사용자 등록(1075)에서 이름을 바꾸고 돌아온 경우
        if (locPopupOpen() && hasPermission() && mode == MODE_LOC) {
            location(); // 다른 앱(지도·공유)에 갔다 오면 멈췄던 위치 갱신을 다시 시작
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterReceiver(receiver);
        handler.removeCallbacks(homeCheck);
        stopLocUpdates();
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
                if (curTab == 0) {
                    texts[0].setText("[재검색]을 눌러 주세요.");
                }
                if (locPopupOpen()) {
                    location(); // 현재 위치 팝업(1020)이 떠 있으면 권한 허용 직후 바로 검색
                }
                checkHome();
                sendStartMessageOnce(); // 앱 시작 위치 통보 (권한이 없어 미뤄 둔 경우)
                if (HomeService.anyOn(this)) {
                    HomeService.refresh(this);
                }
            } else {
                (locPopupOpen() ? texts[1] : texts[curTab]).setText("위치 권한이 필요합니다.\n(Wi-Fi 스캔과 위치 확인에 필요합니다.)");
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
        stopLocUpdates();
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
        Collections.sort(list, (left, right) -> right.level - left.level);

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
        for (ScanResult scan : list) {
            String ssid = ssidOf(scan);
            String bssid = scan.BSSID.toLowerCase(Locale.US);
            seenLevel.put(bssid, scan.level);
            if (bssid.equals(conn)) {
                connSsid = ssid;
            }
            List<ScanResult> group = groups.get(ssid);
            if (group == null) {
                group = new ArrayList<>();
                groups.put(ssid, group);
            }
            group.add(scan);
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
                : "\n체크한 공유기를 [저장]으로 등록하세요.\n(이름을 체크하면 같은 이름의 공유기가 모두 체크됩니다)"));

        for (String ssid : order) {
            List<ScanResult> group = groups.get(ssid);
            boolean connGroup = ssid.equals(connSsid);

            LinearLayout box = new LinearLayout(this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding(20, 16, 20, 16);
            if (connGroup) {
                box.setBackgroundColor(0x1A2E7D32);
            }

            // 그룹 머리: ■ 이름 (n개) 📶 연결됨
            final CheckBox head = new CheckBox(this);
            SpannableStringBuilder headText = new SpannableStringBuilder();
            append(headText, "■ ", C_MARK, true);
            append(headText, ssid, 0, true);
            if (group.size() > 1) {
                append(headText, "  (" + group.size() + "개)", C_GRAY, false);
            }
            if (connGroup) {
                append(headText, "  📶 연결됨", C_GOOD, true);
            }
            head.setText(headText);
            head.setTextSize(17);
            box.addView(head);

            final List<CheckBox> kids = new ArrayList<>();
            for (ScanResult scan : group) {
                String bssid = scan.BSSID.toLowerCase(Locale.US);
                boolean isConn = bssid.equals(conn);
                CheckBox cb = new CheckBox(this);
                cb.setTag(new String[]{bssid, ssid});
                cb.setChecked(cur.containsKey(bssid) ? cur.get(bssid) : saved.containsKey(bssid));

                SpannableStringBuilder sb = new SpannableStringBuilder();
                append(sb, bssid, 0, false);
                if (isConn) {
                    append(sb, "  📶 연결됨", C_GOOD, true);
                }
                sb.append('\n');
                appendBars(sb, scan.level);
                sb.append(" ").append(String.valueOf(scan.level)).append(" dBm  ")
                        .append(band(scan.frequency)).append("  ").append(security(scan.capabilities));
                cb.setText(sb);
                cb.setTextSize(14);
                cb.setOnCheckedChangeListener((button, isChecked) -> head.setChecked(allChecked(kids)));

                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = 70;
                box.addView(cb, lp);
                kids.add(cb);
                apChecks.add(cb);
            }
            head.setChecked(allChecked(kids));
            head.setOnClickListener(view -> {
                boolean all = head.isChecked();
                for (CheckBox kid : kids) {
                    kid.setChecked(all);
                }
            });
            wifiRows.addView(box);
        }
        renderSaved();
    }

    private static String ssidOf(ScanResult scan) {
        return (scan.SSID == null || scan.SSID.isEmpty()) ? "<숨김>" : scan.SSID;
    }

    private static boolean allChecked(List<CheckBox> list) {
        for (CheckBox check : list) {
            if (!check.isChecked()) {
                return false;
            }
        }
        return !list.isEmpty();
    }

    private static void append(SpannableStringBuilder sb, String piece, int color, boolean bold) {
        int st = sb.length();
        sb.append(piece);
        if (color != 0) {
            sb.setSpan(new ForegroundColorSpan(color), st, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        if (bold) {
            sb.setSpan(new StyleSpan(Typeface.BOLD), st, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
    }

    // 신호 세기 막대 ▂▄▆█ (강: 초록, 중: 주황, 약: 빨강) + 강/중/약 글자
    private static void appendBars(SpannableStringBuilder sb, int level) {
        int barCount = level >= -55 ? 4 : level >= -67 ? 3 : level >= -78 ? 2 : 1;
        int color = barCount >= 3 ? C_GOOD : barCount == 2 ? C_MID : C_BAD;
        String bars = "▂▄▆█";
        append(sb, bars.substring(0, barCount), color, true);
        append(sb, bars.substring(barCount), 0xFFDDDDDD, true);
        append(sb, barCount >= 3 ? " 강" : barCount == 2 ? " 중" : " 약", color, true);
    }

    // 지금 연결된 공유기의 BSSID (없거나 알 수 없으면 null)
    private String connectedBssid() {
        if (!hasPermission() || !wm.isWifiEnabled()) {
            return null;
        }
        String bssid = wm.getConnectionInfo().getBSSID();
        if (bssid == null || bssid.equals("02:00:00:00:00:00")) {
            return null; // 위치 서비스가 꺼져 있으면 가짜 값이 옴
        }
        return bssid.toLowerCase(Locale.US);
    }

    // 지금 연결된 공유기 이름(SSID). 모르면 null
    private String connectedSsid() {
        if (!hasPermission() || !wm.isWifiEnabled()) {
            return null;
        }
        String ssid = wm.getConnectionInfo().getSSID();
        if (ssid == null || ssid.equals("<unknown ssid>") || ssid.isEmpty()) {
            return null;
        }
        if (ssid.length() >= 2 && ssid.startsWith("\"") && ssid.endsWith("\"")) {
            ssid = ssid.substring(1, ssid.length() - 1);
        }
        return ssid.isEmpty() ? null : ssid;
    }

    // 화면 1020 첫 줄: "공유기: 이름, MAC, 등록 별명" (별명이 없으면 '.....', 별명은 1010 에서 입력)
    private String routerLine() {
        String bssid = connectedBssid();
        if (bssid == null) {
            return "공유기: (연결 안 됨)";
        }
        String ssid = connectedSsid();
        String alias = routerAlias(this, bssid);
        return "공유기: " + (ssid == null ? "<숨김>" : ssid) + ", " + bssid.toUpperCase(Locale.US) + ", "
                + (alias.isEmpty() ? "....." : alias);
    }

    // 저장된 공유기 목록 (주변에 안 보여도 표시, [삭제] 가능)
    private void renderSaved() {
        String conn = connectedBssid();
        savedConn = String.valueOf(conn);
        savedBox.removeAllViews();
        savedChecks.clear();
        Map<String, String> saved = loadRouters();

        TextView header = new TextView(this);
        header.setText("저장된 공유기 (" + saved.size() + "개)");
        header.setTextSize(16);
        header.setTypeface(null, Typeface.BOLD);
        header.setPadding(40, 30, 40, 10);
        savedBox.addView(header);

        // 영역 1 안내문
        TextView guide = new TextView(this);
        guide.setText("사용자 폰이 이 목록에서 ◉ 홈 으로 체크된 공유기에 연결되면 [홈]으로 판단합니다. ◉ 기타 공유기(학교·회사 등)는 별명만 표시하고 귀가로 보지 않습니다");
        guide.setTextSize(13);
        guide.setTextColor(Color.DKGRAY);
        guide.setPadding(40, 0, 40, 10);
        savedBox.addView(guide);

        if (saved.isEmpty()) {
            TextView empty = new TextView(this);
            empty.setText("없음 — 아래 검색 결과에서 체크 후 [저장]을 누르세요.");
            empty.setTextColor(C_GRAY);
            empty.setPadding(40, 0, 40, 10);
            savedBox.addView(empty);
        }

        List<Map.Entry<String, String>> items = new ArrayList<>(saved.entrySet());
        Collections.sort(items, (left, right) -> {
            int cmp = left.getValue().compareTo(right.getValue());
            return cmp != 0 ? cmp : left.getKey().compareTo(right.getKey());
        });
        for (Map.Entry<String, String> it : items) {
            final String bssid = it.getKey();
            String ssid = it.getValue().isEmpty() ? "<숨김>" : it.getValue();

            SpannableStringBuilder sb = new SpannableStringBuilder();
            append(sb, "■ ", C_MARK, true);
            append(sb, ssid, 0, true);
            sb.append("\n").append(bssid).append("\n");
            Integer lv = seenLevel.get(bssid);
            if (bssid.equals(conn)) {
                append(sb, "📶 연결됨  ", C_GOOD, true);
            }
            if (lv != null) {
                appendBars(sb, lv);
                sb.append(" ").append(String.valueOf(lv)).append(" dBm");
            } else if (!bssid.equals(conn)) {
                append(sb, wifiScanned ? "주변에 없음" : "검색 전", C_GRAY, false);
            }

            // 체크 = 계속 등록, 체크 해제 후 이 영역의 [등록] = 등록에서 빼기
            final CheckBox cb = new CheckBox(this);
            cb.setText(sb);
            cb.setTextSize(14);
            cb.setChecked(true);
            cb.setTag(bssid);
            savedChecks.add(cb);

            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(30, 6, 20, 6);
            if (bssid.equals(conn)) {
                row.setBackgroundColor(0x1A2E7D32);
            }
            row.addView(cb, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            savedBox.addView(row);

            // 공유기 종류: (●) 홈 / ( ) 기타 - 둘 중 하나는 반드시 체크, 고르면 바로 저장 (기존 공유기는 '기타')
            final RadioGroup kindGroup = new RadioGroup(this);
            kindGroup.setOrientation(RadioGroup.HORIZONTAL);
            kindGroup.setPadding(110, 0, 40, 0);
            if (bssid.equals(conn)) {
                kindGroup.setBackgroundColor(0x1A2E7D32);
            }
            RadioButton rbHome = new RadioButton(this);
            rbHome.setText("홈");
            rbHome.setTextSize(14);
            rbHome.setId(View.generateViewId());
            RadioButton rbOther = new RadioButton(this);
            rbOther.setText("기타");
            rbOther.setTextSize(14);
            rbOther.setId(View.generateViewId());
            kindGroup.addView(rbHome);
            RadioGroup.LayoutParams gap = new RadioGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            gap.leftMargin = dpx(24);
            kindGroup.addView(rbOther, gap);
            kindGroup.check(routerIsHome(this, bssid) ? rbHome.getId() : rbOther.getId());
            final int homeId = rbHome.getId();
            kindGroup.setOnCheckedChangeListener((group, id) -> {
                setRouterHome(MainActivity.this, bssid, id == homeId);
                HomeService.refresh(MainActivity.this); // 귀가 판정 다시
                checkHome();
            });
            savedBox.addView(kindGroup);

            // 공유기 등록 별명 (예: 우리집 거실). 글자를 바꾸면 바로 저장
            final LinearLayout aliasRow = aliasRow(routerAlias(this, bssid), value -> setRouterAlias(MainActivity.this, bssid, value), cb);
            if (bssid.equals(conn)) {
                aliasRow.setBackgroundColor(0x1A2E7D32);
            }
            savedBox.addView(aliasRow);
            // 체크를 푼(등록에서 뺄) 공유기는 라디오·별명 칸을 숨김. 다시 체크하면 바로 나타남
            cb.setOnCheckedChangeListener((button, isChecked) -> {
                kindGroup.setVisibility(isChecked ? View.VISIBLE : View.GONE);
                aliasRow.setVisibility(isChecked ? View.VISIBLE : View.GONE);
                ((Runnable) aliasRow.getTag()).run();
            });
        }
        if (!items.isEmpty()) {
            savedBox.addView(registerRow(view -> commitSaved())); // 영역 1 오른쪽 아래 [등록]
        }
        // (아래 영역과의 구분은 굵은 구분선 thickDivider)
    }

    private final List<CheckBox> savedChecks = new ArrayList<>(); // 영역 1: 저장된 공유기 체크박스

    interface TextSaver {
        void save(String value);
    }

    // "별명 [______]" 한 줄 (공유기·블루투스 공용). 글자를 바꾸면 바로 saver 로 저장
    // 비어 있으면: 진한 빨간 칸 + 흰 글씨 "별명을 입력하세요", 입력하면 평소 모양
    private LinearLayout aliasRow(String value, final TextSaver saver) {
        return aliasRow(value, saver, null);
    }

    // needWhen: 이 체크박스가 체크됐을 때만 빈 별명을 빨간색으로 (null 이면 항상).
    // 체크를 바꾸면 다시 칠하도록 row 의 tag 에 칠하기(Runnable)를 넣어 둠
    private LinearLayout aliasRow(String value, final TextSaver saver, final CheckBox needWhen) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(110, 0, 40, 10); // 체크박스 글자 시작 위치에 맞춤
        TextView al = new TextView(this);
        al.setText("별명 ");
        al.setTextSize(14);
        al.setTypeface(null, Typeface.BOLD);
        row.addView(al);
        final EditText alias = new EditText(this);
        alias.setSingleLine(true);
        alias.setTextSize(14);
        alias.setHint("별명을 입력하세요");
        alias.setText(value);
        final android.graphics.drawable.Drawable normalBg = alias.getBackground(); // 평소 입력칸 모양
        final int padL = alias.getPaddingLeft(), padT = alias.getPaddingTop(),
                padR = alias.getPaddingRight(), padB = alias.getPaddingBottom();
        final Runnable paint = () -> {
            boolean needed = needWhen == null || needWhen.isChecked(); // 체크 안 한 기기는 별명 없어도 됨
            if (needed && alias.getText().toString().trim().isEmpty()) {
                alias.setBackgroundColor(0xFFB71C1C);
                alias.setHintTextColor(Color.WHITE);
                alias.setTextColor(Color.WHITE);
                alias.setPadding(dpx(10), dpx(8), dpx(10), dpx(8));
            } else {
                alias.setBackground(normalBg);
                alias.setTextColor(Color.BLACK);
                alias.setHintTextColor(Color.GRAY);
                alias.setPadding(padL, padT, padR, padB);
            }
        };
        paint.run();
        row.setTag(paint);
        alias.addTextChangedListener(new android.text.TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence chars, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence chars, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(android.text.Editable editable) {
                saver.save(editable.toString());
                paint.run();
            }
        });
        row.addView(alias, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return row;
    }

    // 공유기 등록 별명: "home" 설정의 "alias_<MAC 소문자>"
    static String routerAlias(Context ctx, String bssid) {
        return ctx.getSharedPreferences("home", MODE_PRIVATE)
                .getString("alias_" + bssid.toLowerCase(Locale.US), "");
    }

    // 공유기 종류: "home" 설정의 "kind_<MAC 소문자>" = "home"(홈) / "other"(기타). 값이 없으면(기존 공유기) 기타
    static boolean routerIsHome(Context ctx, String bssid) {
        return "home".equals(ctx.getSharedPreferences("home", MODE_PRIVATE)
                .getString("kind_" + bssid.toLowerCase(Locale.US), "other"));
    }

    // 저장된 공유기 중 '홈'으로 체크된 것이 하나라도 있나 ('기타'만 있으면 집을 판정할 수 없음)
    static boolean hasHomeRouter(Context ctx) {
        for (String entry : ctx.getSharedPreferences("home", MODE_PRIVATE)
                .getStringSet("routers", new HashSet<String>())) {
            if (routerIsHome(ctx, entry.split("\\|", 2)[0])) {
                return true;
            }
        }
        return false;
    }

    static void setRouterHome(Context ctx, String bssid, boolean home) {
        ctx.getSharedPreferences("home", MODE_PRIVATE).edit()
                .putString("kind_" + bssid.toLowerCase(Locale.US), home ? "home" : "other").apply();
    }

    static void setRouterAlias(Context ctx, String bssid, String alias) {
        ctx.getSharedPreferences("home", MODE_PRIVATE).edit()
                .putString("alias_" + bssid.toLowerCase(Locale.US), alias.trim()).apply();
    }

    // 영역 1 [등록]: 체크를 해제한 공유기만 등록에서 뺌
    private void commitSaved() {
        Map<String, String> saved = loadRouters();
        int removed = 0;
        for (CheckBox cb : savedChecks) {
            if (!cb.isChecked() && saved.remove((String) cb.getTag()) != null) {
                removed++;
                for (CheckBox ap : apChecks) { // 아래 검색 결과의 체크도 맞춤
                    if (((String[]) ap.getTag())[0].equals(cb.getTag())) {
                        ap.setChecked(false);
                    }
                }
            }
        }
        writeRouters(saved);
        Toast.makeText(this, removed == 0 ? "변경 없음 (홈 공유기 " + saved.size() + "개)"
                : removed + "개를 등록에서 뺐습니다 (홈 공유기 " + saved.size() + "개)", Toast.LENGTH_SHORT).show();
        renderSaved();
        checkHome();
    }

    // 영역마다 오른쪽 아래 [저장] 버튼 한 줄
    private LinearLayout registerRow(View.OnClickListener listener) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.END);
        row.setPadding(20, 6, 30, 0);
        Button btn = new Button(this);
        btn.setText("저장");
        btn.setTextSize(14);
        btn.setTypeface(null, Typeface.BOLD);
        btn.setOnClickListener(listener);
        row.addView(btn);
        return row;
    }

    // 화면 1010 영역 사이 굵은 구분선 (위아래로 넓은 공간)
    private View thickDivider() {
        View view = new View(this);
        view.setBackgroundColor(0xFF424242);
        return view;
    }

    private LinearLayout.LayoutParams thickDividerLp() {
        float density = getResources().getDisplayMetrics().density;
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, (int) (4 * density)); // 굵기 4dp
        lp.topMargin = (int) (28 * density);    // 위 공간
        lp.bottomMargin = (int) (28 * density); // 아래 공간
        return lp;
    }

    // [등록 확인]: 등록된 홈 공유기와 홈 PC 블루투스만 모아서 보여 줌
    private void showRegistered() {
        String conn = connectedBssid();
        SpannableStringBuilder sb = new SpannableStringBuilder();

        Map<String, String> routers = loadRouters();
        append(sb, "■ 홈 공유기 (" + routers.size() + "개)\n", C_MARK, true);
        if (routers.isEmpty()) {
            append(sb, "  없음 — [재검색] 후 체크하고 [저장]을 누르세요.\n", C_GRAY, false);
        }
        List<Map.Entry<String, String>> items = new ArrayList<>(routers.entrySet());
        Collections.sort(items, (left, right) -> left.getValue().compareTo(right.getValue()));
        for (Map.Entry<String, String> it : items) {
            String bssid = it.getKey();
            append(sb, "  " + (it.getValue().isEmpty() ? "<숨김>" : it.getValue()), 0, true);
            String alias = routerAlias(this, bssid); // 공유기 등록 별명 (1010 에서 입력)
            if (!alias.isEmpty()) {
                append(sb, " (" + alias + ")", C_MARK, true);
            }
            boolean rh = routerIsHome(this, bssid);
            append(sb, rh ? " [홈]" : " [기타]", rh ? C_GOOD : C_MARK, true);
            append(sb, "   " + bssid, C_GRAY, false);
            Integer lv = seenLevel.get(bssid);
            if (bssid.equals(conn)) {
                append(sb, "  📶 연결됨", C_GOOD, true);
            } else if (lv != null) {
                append(sb, "  주변에 있음 " + lv + " dBm", C_MID, false);
            }
            sb.append('\n');
        }

        Set<String> bts = BtHome.registered(this);
        sb.append('\n');
        append(sb, "■ 홈 PC 블루투스 (" + bts.size() + "개)\n", C_MARK, true);
        if (bts.isEmpty()) {
            append(sb, "  없음 — 아래 '홈 PC 블루투스'에서 페어링된 PC를 체크하세요.\n", C_GRAY, false);
        }
        SharedPreferences hp = getSharedPreferences("home", MODE_PRIVATE);
        long seen = hp.getLong("btSeenAt", 0);
        String seenName = hp.getString("btSeenName", "");
        for (String entry : bts) {
            String[] parts = entry.split("\\|", 2);
            String name = parts.length > 1 ? parts[1] : parts[0];
            append(sb, "  " + name, 0, true);
            String ba = BtHome.alias(this, parts[0]);
            if (!ba.isEmpty()) {
                append(sb, " (" + ba + ")", C_MARK, true);
            }
            append(sb, BtHome.isHome(this, parts[0]) ? " [홈]" : " [기타]", BtHome.isHome(this, parts[0]) ? C_GOOD : C_MARK, true);
            append(sb, "   " + parts[0], C_GRAY, false);
            if (name.equals(seenName) && seen > 0) {
                append(sb, "  마지막 확인 " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(seen)),
                        BtHome.seenRecently(this) ? C_GOOD : C_GRAY, false);
            }
            sb.append('\n');
        }

        sb.append('\n');
        boolean home = statusHome;
        append(sb, "지금 상태: " + (home ? "홈" : "외출"), home ? C_GOOD : C_BAD, true);

        TextView tv = new TextView(this);
        tv.setText(sb);
        tv.setTextSize(14);
        tv.setPadding(50, 30, 50, 10);
        tv.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("등록 확인")
                .setView(sv)
                .setPositiveButton("확인", null)
                .show(), "1090");
    }

    // ---------- 홈 PC 블루투스 (페어링된 PC가 가까이 있으면 '집') ----------

    private TextView btNote(String msg, int color) {
        TextView tv = new TextView(this);
        tv.setText(msg);
        tv.setTextSize(13);
        tv.setTextColor(color);
        tv.setPadding(40, 4, 40, 4);
        btBox.addView(tv);
        return tv;
    }

    private Button btButton(LinearLayout row, String label, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(label);
        btn.setTextSize(12);
        btn.setAllCaps(false);
        btn.setOnClickListener(listener);
        row.addView(btn, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return btn;
    }

    private void renderBt() {
        if (btBox == null) {
            return;
        }
        btBox.removeAllViews();
        btChecks.clear();
        TextView header = new TextView(this);
        header.setText("홈 PC 블루투스 (" + BtHome.registered(this).size() + "개 등록)");
        header.setTextSize(16);
        header.setTypeface(null, Typeface.BOLD);
        header.setPadding(40, 30, 40, 6);
        btBox.addView(header);
        btNote("폰과 PC를 미리 페어링한 뒤 아래에서 PC를 체크하고 [저장]을 누르세요.\n"
                + "◉ 홈 으로 체크한 기기가 가까이 있으면 Wi-Fi가 꺼져 있어도 홈으로 판정합니다. (PC 블루투스가 켜져 있어야 함)\n"
                + "◉ 기타 기기(회사 PC·차 등)는 가까이 있어도 귀가로 보지 않고 이름만 표시합니다.",
                Color.GRAY);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(30, 0, 30, 0);

        if (BtHome.adapter(this) == null) {
            btNote("이 폰에는 블루투스가 없습니다.", 0xFFC62828);
        } else if (!BtHome.hasPerm(this)) {
            btButton(row, "블루투스 기기 불러오기 (권한 허용)", view -> requestPermissions(
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
            for (final String[] dev : list) {
                boolean on = false;
                for (String entry : reg) {
                    if (entry.startsWith(dev[0] + "|")) {
                        on = true;
                    }
                }
                final CheckBox cb = new CheckBox(this);
                SpannableStringBuilder sb = new SpannableStringBuilder();
                append(sb, "■ ", C_MARK, true);
                append(sb, dev[1], 0, true);
                append(sb, "\n" + dev[0], C_GRAY, false);
                cb.setText(sb);
                cb.setTextSize(14);
                cb.setChecked(on);
                cb.setTag(dev); // {MAC, 이름}. 체크만 바꾸고, 이 영역의 [등록]을 눌러야 저장
                btChecks.add(cb);
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = 40;
                btBox.addView(cb, lp);

                // 기기 종류: (●) 홈 / ( ) 기타 - 반드시 하나, 고르면 바로 저장 (기존 기기는 '기타')
                // 기타 기기(회사 PC, 차 등)는 가까이 있어도 귀가로 보지 않음
                final RadioGroup kind = new RadioGroup(this);
                kind.setOrientation(RadioGroup.HORIZONTAL);
                kind.setPadding(110, 0, 40, 6);
                RadioButton kh = new RadioButton(this);
                kh.setText("홈");
                kh.setTextSize(14);
                kh.setId(View.generateViewId());
                RadioButton ko = new RadioButton(this);
                ko.setText("기타");
                ko.setTextSize(14);
                ko.setId(View.generateViewId());
                kind.addView(kh);
                RadioGroup.LayoutParams kgap = new RadioGroup.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                kgap.leftMargin = dpx(24);
                kind.addView(ko, kgap);
                kind.check(BtHome.isHome(this, dev[0]) ? kh.getId() : ko.getId());
                final int khId = kh.getId();
                kind.setOnCheckedChangeListener((group, id) -> {
                    BtHome.setHome(MainActivity.this, dev[0], id == khId);
                    HomeService.refresh(MainActivity.this); // 귀가 판정 다시
                    checkHome();
                });
                btBox.addView(kind);
                // 블루투스 기기 별명 (예: 거실 PC). 글자를 바꾸면 바로 저장
                // 체크(저장할 기기)한 경우만 빈 별명을 빨간색으로, 체크를 바꾸면 바로 다시 칠함
                final LinearLayout btAlias = aliasRow(BtHome.alias(this, dev[0]),
                        value -> BtHome.setAlias(MainActivity.this, dev[0], value), cb);
                // 체크 안 한(저장하지 않을) 기기는 라디오·별명 칸을 숨김. 체크하면 바로 나타남
                final int shown = cb.isChecked() ? View.VISIBLE : View.GONE;
                kind.setVisibility(shown);
                btAlias.setVisibility(shown);
                cb.setOnCheckedChangeListener((button, isChecked) -> {
                    kind.setVisibility(isChecked ? View.VISIBLE : View.GONE);
                    btAlias.setVisibility(isChecked ? View.VISIBLE : View.GONE);
                    ((Runnable) btAlias.getTag()).run();
                });
                btBox.addView(btAlias);
            }
            btButton(row, "블루투스 설정 (페어링)", view -> {
                try {
                    startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS));
                } catch (Exception e) {
                    Toast.makeText(this, "블루투스 설정을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
            btButton(row, "지금 확인", view -> checkBtNow());
        }
        btBox.addView(row);
        if (!btChecks.isEmpty()) {
            btBox.addView(registerRow(view -> commitBt())); // 영역 2 오른쪽 아래 [등록]
        }
        // (아래 검색 결과 영역과의 구분은 굵은 구분선 thickDivider)
    }

    private final List<CheckBox> btChecks = new ArrayList<>(); // 영역 2: 페어링된 기기 체크박스

    // 영역 2 [등록]: 체크한 기기만 홈 PC 블루투스로 저장
    private void commitBt() {
        Set<String> set = new HashSet<>();
        for (CheckBox cb : btChecks) {
            if (cb.isChecked()) {
                String[] dev = (String[]) cb.getTag();
                set.add(dev[0] + "|" + dev[1]);
            }
        }
        BtHome.save(this, set);
        Toast.makeText(this, "등록했습니다 (홈 PC 블루투스 " + set.size() + "개)", Toast.LENGTH_SHORT).show();
        renderBt();
        HomeService.refresh(this);
        checkHome();
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
            final String[] found = BtHome.findAny(this);
            runOnUiThread(() -> {
                if (found != null) {
                    BtHome.markSeen(this, found[0], found[1]);
                    Toast.makeText(this, (BtHome.isHome(this, found[0]) ? "✓ 홈 기기를 찾았습니다: "
                            : "✓ 기타 기기를 찾았습니다 (집으로 보지 않음): ") + found[1], Toast.LENGTH_LONG).show();
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

    private static String band(int freq) {
        if (freq >= 2400 && freq < 2500) {
            return "2.4GHz ch" + (freq == 2484 ? 14 : (freq - 2407) / 5);
        }
        if (freq >= 4900 && freq < 5900) {
            return "5GHz ch" + (freq - 5000) / 5;
        }
        if (freq >= 5925) {
            return "6GHz ch" + (freq - 5950) / 5;
        }
        return freq + "MHz";
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
        for (String entry : set) {
            String[] parts = entry.split("\\|", 2);
            map.put(parts[0], parts.length > 1 ? parts[1] : "");
        }
        return map;
    }

    // 화면에 나온 공유기 중 체크한 것은 저장, 체크 해제한 것은 삭제
    private void saveRouters() {
        if (apChecks.isEmpty()) {
            Toast.makeText(this, "먼저 [재검색]으로 공유기를 찾으세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Map<String, String> saved = loadRouters();
        for (CheckBox cb : apChecks) {
            String[] parts = (String[]) cb.getTag();
            if (cb.isChecked()) {
                saved.put(parts[0], parts[1]);
            } else {
                saved.remove(parts[0]);
            }
        }
        writeRouters(saved);
        Toast.makeText(this, "등록했습니다 (홈 공유기 " + saved.size() + "개)", Toast.LENGTH_SHORT).show();
        renderSaved();
        checkHome();
    }

    private void writeRouters(Map<String, String> saved) {
        Set<String> set = new HashSet<>();
        for (Map.Entry<String, String> entry : saved.entrySet()) {
            set.add(entry.getKey() + "|" + entry.getValue());
        }
        getSharedPreferences("home", MODE_PRIVATE).edit().putStringSet("routers", set).apply();
    }

    // 지금 연결된 공유기가 저장된 공유기(BSSID)인지 검사 -> 항상 홈 또는 외출 표시
    private void checkHome() {
        Map<String, String> saved = loadRouters();
        boolean btReg = BtHome.hasHome(this); // '홈'으로 체크된 블루투스 기기
        boolean home = false;
        String note = "";
        String alias = ""; // 연결된 홈 공유기의 등록 별명 (있으면 '홈' 대신 표시)
        boolean other = false; // 연결된 공유기 종류가 '기타' (1010 라디오) -> 별명만 표시
        if (saved.isEmpty() && !btReg) {
            note = "(저장된 공유기가 없습니다)";
        } else if (!hasPermission() && !btReg) {
            note = "(위치 권한이 필요합니다)";
        } else {
            String cur = hasPermission() ? connectedBssid() : null;
            if (cur != null && saved.containsKey(cur)) {
                alias = routerAlias(this, cur);
                other = !routerIsHome(this, cur);
                home = !other; // '기타' 공유기는 귀가(집)로 보지 않음
            }
            if (!hasHomeRouter(this) && !btReg) {
                note = "('홈'으로 체크된 공유기·블루투스가 없습니다)";
            }
        }
        // 공유기로 '집'이 아니어도, 최근 3분 안에 '홈' 블루투스 기기를 찾았으면 '집'
        if (!home && BtHome.seenHome(this)) {
            home = true;
            other = false;
            alias = BtHome.alias(this, BtHome.seenMac(this)); // 별명이 있으면 별명, 없으면 "홈"
            note = "(홈 PC 블루투스 확인됨: " + BtHome.seenName(this) + ")";
        } else if (!home && !other && BtHome.seenOther(this)) {
            // '기타' 블루투스 기기(회사 PC 등)가 가까이 있음: 집 아님, 그 기기 이름만 파란색으로
            other = true;
            alias = BtHome.label(this, BtHome.seenMac(this), BtHome.seenName(this)); // 별명, 없으면 기기 이름
            note = "(기타 블루투스 기기 가까이 있음)";
        }
        // 연결된 공유기가 바뀌었으면 저장 목록의 '연결됨' 표시 갱신
        if (!savedConn.equals(String.valueOf(connectedBssid()))) {
            renderSaved();
        }
        statusHome = home;
        // 메인 화면 표시: 연결된 홈 공유기에 등록 별명이 있으면 그 별명(예: 우리집 거실), 없으면 홈 / 외출
        //  '기타' 공유기(학교·회사 등)는 '홈' 없이 별명만 (별명이 없으면 "기타"), 파란색
        String label = other ? (alias.isEmpty() ? "기타" : alias)
                : home ? (alias.isEmpty() ? "홈" : alias) : "외출";
        status.setText(label);
        // 별명이 길면 글자를 줄여 양옆 버튼 사이에 들어가게
        int len = label.length();
        status.setTextSize(len <= 3 ? 40 : len <= 5 ? 32 : len <= 8 ? 24 : 18);
        status.setTextColor(other ? Color.rgb(21, 101, 192) // 기타: 파랑
                : home ? Color.rgb(46, 125, 50) : Color.rgb(198, 40, 40)); // 홈: 초록, 외출: 빨강
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
        int maskInt = Integer.reverseBytes(di.netmask);
        if (maskInt == 0 || Integer.bitCount(maskInt) < 24) {
            maskInt = 0xffffff00; // 너무 큰 대역은 /24로 제한
        }
        final int mask = maskInt;
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
                for (int host = base + 1; host < last; host++) {
                    if (host == my) {
                        continue;
                    }
                    final String ip = toIp(host);
                    nums.add(host);
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
                for (Dev dev : devs.values()) {
                    final String ip = dev.ip;
                    names.put(dev.num, ex.submit(() -> hostName(ip)));
                }
                for (Dev dev : devs.values()) {
                    try {
                        dev.name = names.get(dev.num).get();
                    } catch (Exception ignored) {
                    }
                    String mac = arp.get(dev.ip);
                    if (mac != null) {
                        dev.mac = mac;
                    }
                }

                // 3) 예전에 발견했지만 지금 응답 없는 기기는 OFF(빨강)로 표시
                for (String entry : seen) {
                    String[] parts = entry.split("\\|", -1);
                    if (parts.length < 3) {
                        continue;
                    }
                    int ipNum = ipToInt(parts[0]);
                    if (ipNum == 0 || (ipNum & mask) != base) {
                        continue;
                    }
                    Dev dev = devs.get(ipNum);
                    if (dev == null) {
                        dev = addDev(devs, ipNum, false);
                    }
                    if (dev.name.isEmpty()) {
                        dev.name = parts[1];
                    }
                    if (dev.mac.isEmpty()) {
                        dev.mac = parts[2];
                    }
                }
                Set<String> save = new HashSet<>();
                for (Dev dev : devs.values()) {
                    save.add(dev.ip + "|" + dev.name + "|" + dev.mac);
                }
                sp.edit().putStringSet("seen", save).apply();

                // 4) 표시 (IP 내림차순)
                List<Dev> list = new ArrayList<>(devs.values());
                Collections.sort(list, (left, right) -> Integer.compare(right.num, left.num));
                int on = 0;
                StringBuilder sb = new StringBuilder();
                for (Dev dev : list) {
                    if (dev.on) {
                        on++;
                    }
                    sb.append(dev.on ? "🟢 " : "🔴 ").append(dev.ip);
                    if (dev.num == gw) {
                        sb.append(" (공유기)");
                    }
                    if (dev.num == my) {
                        sb.append(" (내 폰)");
                    }
                    if (!dev.name.isEmpty()) {
                        sb.append("  ").append(dev.name);
                    }
                    sb.append('\n');
                    if (!dev.mac.isEmpty()) {
                        sb.append("      ").append(dev.mac).append('\n');
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
        Dev dev = devs.get(num);
        if (dev == null) {
            dev = new Dev();
            dev.num = num;
            dev.ip = toIp(num);
            devs.put(num, dev);
        }
        dev.on = dev.on || on;
        return dev;
    }

    // 화면 갱신 (다른 버튼으로 화면이 바뀐 뒤에는 무시)
    private void show(final int id, final String msg) {
        runOnUiThread(() -> {
            if (id == scanId) {
                text.setText(msg);
            }
        });
    }

    private static boolean alive(String ip) {
        for (int port : PROBE) {
            try (Socket sock = new Socket()) {
                sock.connect(new InetSocketAddress(ip, port), 300);
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
            String host = InetAddress.getByName(ip).getCanonicalHostName();
            return host.equals(ip) ? "" : host;
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
                String[] parts = line.trim().split("\\s+");
                if (parts.length >= 4 && !parts[3].equals("00:00:00:00:00:00")) {
                    map.put(parts[0], parts[3].toUpperCase(Locale.US));
                }
            }
        } catch (Exception ignored) {
        }
        return map;
    }

    private static String toIp(int ipNum) {
        return ((ipNum >>> 24) & 0xff) + "." + ((ipNum >> 16) & 0xff) + "." + ((ipNum >> 8) & 0xff) + "." + (ipNum & 0xff);
    }

    private static int ipToInt(String ip) {
        String[] parts = ip.split("\\.");
        if (parts.length != 4) {
            return 0;
        }
        int value = 0;
        try {
            for (String octet : parts) {
                value = (value << 8) | (Integer.parseInt(octet) & 0xff);
            }
        } catch (NumberFormatException e) {
            return 0;
        }
        return value;
    }

    // ---------- 현재 위치 팝업 (화면 1020) ----------
    // 앱을 켜면 처음에 보여 줌. 위: [재검색] [현재 위치(검은 제목)] [닫기], 가운데: 위치 정보, 아래: [지도] [복사] [즉시 공유]
    // [닫기](또는 뒤로 가기) -> 위치 갱신을 멈추고 탭 메뉴로

    private android.app.Dialog locDlg;

    private int dpx(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    // 타원 버튼 (입체: 오른쪽 아래 그림자 + 위가 밝은 파랑 그라데이션 + 위쪽 광택, 흰 굵은 글자)
    // 색: 가장 흔한 버튼 색인 파랑 (Material Blue 계열)
    private Button ovalButton(String label, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(label);
        btn.setTextSize(16);
        btn.setTypeface(null, Typeface.BOLD);
        btn.setTextColor(Color.WHITE);
        btn.setShadowLayer(2f, 0f, 1.5f, 0x80000000); // 글자도 살짝 그림자
        btn.setAllCaps(false);
        btn.setMinWidth(0);
        btn.setMinimumWidth(0);
        btn.setMinHeight(0);
        btn.setMinimumHeight(0);
        btn.setGravity(Gravity.CENTER);
        int sh = dpx(3);
        btn.setPadding(0, 0, sh, sh); // 글자를 그림자 뺀 타원 가운데에
        android.graphics.drawable.StateListDrawable st = new android.graphics.drawable.StateListDrawable();
        st.addState(new int[]{android.R.attr.state_pressed}, ovalBevel(true));
        st.addState(new int[]{}, ovalBevel(false));
        btn.setBackground(st);
        btn.setStateListAnimator(null); // 기본 그림자 대신 직접 그린 그림자 사용
        btn.setOnClickListener(listener);
        return btn;
    }

    // 타원 버튼 배경. pressed: 그림자 쪽으로 눌려 들어간 모양 (더 진한 파랑, 그림자 없음)
    private android.graphics.drawable.Drawable ovalBevel(boolean pressed) {
        int sh = dpx(3);
        android.graphics.drawable.GradientDrawable shadow = new android.graphics.drawable.GradientDrawable();
        shadow.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        shadow.setColor(pressed ? Color.TRANSPARENT : 0x55000000);
        android.graphics.drawable.GradientDrawable face = new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                pressed ? new int[]{0xFF1565C0, 0xFF0D47A1} : new int[]{0xFF64B5F6, 0xFF1E88E5, 0xFF1565C0});
        face.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        face.setStroke(dpx(1), 0xFF0D47A1);
        android.graphics.drawable.GradientDrawable gloss = new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{pressed ? 0x22FFFFFF : 0x66FFFFFF, 0x00FFFFFF});
        gloss.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        android.graphics.drawable.LayerDrawable ld = new android.graphics.drawable.LayerDrawable(
                new android.graphics.drawable.Drawable[]{shadow, face, gloss});
        ld.setLayerInset(0, sh, sh, 0, 0);
        int off = pressed ? sh : 0; // 누르면 그림자 자리로 내려앉음
        ld.setLayerInset(1, off, off, sh - off, sh - off);
        // 광택: 타원 위쪽 절반 안쪽에만
        ld.setLayerInset(2, off + dpx(8), off + dpx(3), sh - off + dpx(8), sh - off + dpx(22));
        return ld;
    }

    private android.graphics.drawable.GradientDrawable rounded(int color, int radiusDp) {
        android.graphics.drawable.GradientDrawable shape = new android.graphics.drawable.GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dpx(radiusDp));
        return shape;
    }

    // 1020 카드 배경: 겹친 그림 5장으로 입체 테두리
    //  1) 오른쪽 아래로 비낀 흐린 그림자  2) 진한 남색 테두리
    //  3) 흰색 (위·왼쪽 밝은 경사면)  4) 회색 (아래·오른쪽 어두운 경사면)  5) 약한 회색 바탕 (위가 조금 밝은 그라데이션)
    private android.graphics.drawable.Drawable bevelCard() {
        int sh = 6, edge = 2, bevel = 3, radius = 18;
        android.graphics.drawable.GradientDrawable face = new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{0xFFF5F5F5, 0xFFE8E8E8});
        face.setCornerRadius(dpx(radius - edge - bevel));
        android.graphics.drawable.LayerDrawable ld = new android.graphics.drawable.LayerDrawable(
                new android.graphics.drawable.Drawable[]{
                        rounded(0x40000000, radius),            // 1) 그림자
                        rounded(0xFF0B2533, radius),            // 2) 진한 테두리
                        rounded(0xFFFFFFFF, radius - edge),     // 3) 밝은 경사면
                        rounded(0xFFA9A9A9, radius - edge),     // 4) 어두운 경사면
                        face});                            // 5) 바탕
        ld.setLayerInset(0, dpx(sh), dpx(sh), 0, 0);
        ld.setLayerInset(1, 0, 0, dpx(sh), dpx(sh));
        ld.setLayerInset(2, dpx(edge), dpx(edge), dpx(sh + edge + bevel), dpx(sh + edge + bevel));
        ld.setLayerInset(3, dpx(edge + bevel), dpx(edge + bevel), dpx(sh + edge), dpx(sh + edge));
        ld.setLayerInset(4, dpx(edge + bevel), dpx(edge + bevel), dpx(sh + edge + bevel), dpx(sh + edge + bevel));
        return ld;
    }

    private void showLocPopup() {
        if (locDlg != null && locDlg.isShowing()) {
            return;
        }
        // 가장 바깥: 둥근 테두리 카드 (입체감: 오른쪽 아래 그림자 + 밝은 위·왼쪽 / 어두운 아래·오른쪽 경사면)
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(bevelCard());
        int sh = 6, edge = 2, bevel = 3; // dp: 그림자 / 진한 테두리 / 경사면 두께
        int in = edge + bevel;
        card.setPadding(dpx(in + 10), dpx(in + 10), dpx(sh + in + 10), dpx(sh + in + 4));

        // 위쪽 줄: [재검색]  현재 위치  [닫기]
        int ow = dpx(82), oh = dpx(56); // 그림자 3dp 포함 ("즉시 공유" 두 줄이 들어가게)
        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        Button again = ovalButton("재검색", view -> location());
        top.addView(again, new LinearLayout.LayoutParams(ow, oh));
        TextView title = new TextView(this);
        title.setText("현재 위치");
        title.setTextSize(24);
        title.setTypeface(null, Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        android.graphics.drawable.GradientDrawable tb = new android.graphics.drawable.GradientDrawable();
        tb.setColor(Color.BLACK);
        tb.setStroke(dpx(2), 0xFF0B2533);
        title.setBackground(tb);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(0, oh, 1f);
        tlp.setMargins(dpx(8), 0, dpx(8), 0);
        top.addView(title, tlp);
        top.addView(ovalButton("닫기", view -> locDlg.dismiss()), new LinearLayout.LayoutParams(ow, oh));
        card.addView(top, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 가운데: 위치 정보 (길면 스크롤)
        TextView info = new TextView(this);
        info.setTextSize(17);
        info.setTextColor(Color.BLACK);
        info.setLineSpacing(0, 1.15f);
        info.setPadding(dpx(14), dpx(16), dpx(14), dpx(16));
        info.setText("위치 확인 중...");
        info.setMovementMethod(android.text.method.LinkMovementMethod.getInstance()); // 지도 링크 누르기
        info.setLinkTextColor(0xFF1565C0);
        info.setHighlightColor(Color.TRANSPARENT);
        ScrollView sc = new ScrollView(this);
        sc.addView(info);
        card.addView(sc, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        // 아래쪽 줄: [복사] [즉시 공유] 두 개를 가운데에 넉넉히 벌려 배치 ([지도]는 삭제 - 데이터 영역의 지도 링크 사용)
        LinearLayout bottom = new LinearLayout(this);
        bottom.setOrientation(LinearLayout.HORIZONTAL);
        bottom.setGravity(Gravity.CENTER);
        String[] labels = {"복사", "즉시\n공유"};
        View.OnClickListener[] acts = {view -> copyLocation(), view -> shareLocation()};
        for (int k = 0; k < labels.length; k++) {
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dpx(96), oh);
            lp.setMargins(dpx(20), 0, dpx(20), 0);
            bottom.addView(ovalButton(labels[k], acts[k]), lp);
        }
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        blp.setMargins(0, dpx(6), 0, dpx(10));
        card.addView(bottom, blp);
        card.addView(ScreenId.label(this, "1020"), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 위치 검색(location/begin/showLocation)이 이 팝업의 글자·[재검색]을 쓰도록 연결
        texts[1] = info;
        startBtns[1] = again;

        locDlg = new android.app.Dialog(this);
        locDlg.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE);
        locDlg.setContentView(card);
        android.view.Window window = locDlg.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            window.setLayout((int) (dm.widthPixels * 0.92), (int) (dm.heightPixels * 0.80));
        }
        locDlg.setOnDismissListener(dialog -> {
            if (mode == MODE_LOC) {
                mode = MODE_NONE;
                scanId++;
            }
            stopLocUpdates();
            text = texts[curTab] != null ? texts[curTab] : texts[0];
            afterLocPopup();
        });
        locDlg.show();
        location(); // 바로 위치 검색 (권한이 없으면 권한 요청 -> 허용되면 다시 검색)
    }

    private boolean locPopupOpen() {
        return locDlg != null && locDlg.isShowing();
    }

    // 현재 위치 팝업을 닫은 뒤(또는 팝업 없이 다시 그릴 때): 예전 시작 순서 그대로
    private boolean afterLocDone;

    private void afterLocPopup() {
        if (afterLocDone) {
            return;
        }
        afterLocDone = true;
        if (loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
            showIntro(); // 홈 공유기가 아직 없으면 안내 팝업 -> 닫으면 위치 권한 요청
        } else if (ensurePermission() && (HomeService.mailRemoteOn(this) || HomeService.remoteOn(this))) {
            // 위치 권한이 이미 있고 원격 위치 요청을 받는 중이면: 회신에 넣을 주변 블루투스 검색 권한(근처 기기)
            BtHome.requestScan(this);
        }
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
        for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
            if (!lm.isProviderEnabled(provider)) {
                continue;
            }
            any = true;
            Location loc = lm.getLastKnownLocation(provider);
            if (loc != null && (best == null || loc.getTime() > best.getTime())) {
                best = loc;
            }
            lm.requestLocationUpdates(provider, 1000, 0, locListener);
        }
        if (startFused()) { // 구글 통합 위치도 함께 (더 정확한 값이 오면 그걸로 바뀜)
            any = true;
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

    private void showLocation(Location loc, boolean cached) {
        lastLoc = loc;
        boolean fresh = geoLoc != null && geoLoc.distanceTo(loc) < 100;
        // 첫 줄 "[ 현재 위치 ]" 는 제거 (팝업 제목이 '현재 위치'). 마지막 영역: 지도 링크 3개 (누르면 열림)
        SpannableStringBuilder sb = new SpannableStringBuilder();
        if (cached) {
            sb.append("(마지막 위치, 갱신 중...)\n\n");
        }
        sb.append(routerLine() + "\n\n");
        // 공유기 줄과 위도 줄 사이 넉넉한 빈 공간 (빈 줄 1개 + 그 줄을 크게)
        int gap = sb.length() - 1;
        sb.setSpan(new android.text.style.RelativeSizeSpan(1.6f), gap, gap + 1,
                android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.append("위도: " + String.format(Locale.US, "%.6f", loc.getLatitude()) + "\n"
                + "경도: " + String.format(Locale.US, "%.6f", loc.getLongitude()) + "\n"
                + "정확도: " + Math.round(loc.getAccuracy()) + " m\n"
                + "제공: " + loc.getProvider() + "\n"
                + "시각: " + new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date(loc.getTime()))
                + "\n\n" + (fresh ? geoText : "주소: 조회 중...") + "\n\n");
        double la = loc.getLatitude(), lo = loc.getLongitude();
        String ll = String.format(Locale.US, "%.6f,%.6f", la, lo);
        String lat = String.format(Locale.US, "%.6f", la), lng = String.format(Locale.US, "%.6f", lo);
        String here = Uri.encode("현재 위치");
        // 지도 앱이 깔려 있으면 앱의 주소 형식으로, 없으면 웹 주소로
        mapLink(sb, "구글 지도", "com.google.android.apps.maps",
                "geo:" + ll + "?q=" + ll + "(" + here + ")",
                TelegramSender.googleUrl(la, lo));
        sb.append("\n");
        mapLink(sb, "네이버 지도", "com.nhn.android.nmap",
                "nmap://place?lat=" + lat + "&lng=" + lng + "&name=" + here + "&appname=" + getPackageName(),
                TelegramSender.naverUrl(la, lo));
        sb.append("\n");
        mapLink(sb, "카카오 지도", "net.daum.android.map",
                "kakaomap://look?p=" + ll,
                TelegramSender.kakaoUrl(la, lo));
        // 티맵·아틀란: 공개된 좌표 보기 주소가 없어 표준 geo: 로 시도 -> 안 되면 앱만 실행. 웹 지도가 없어 미설치면 Play 스토어
        sb.append("\n");
        mapLink(sb, "티맵 (TMAP)", "com.skt.tmap.ku|com.skt.skaf.l001mtm091", null,
                "https://play.google.com/store/apps/details?id=com.skt.tmap.ku");
        sb.append("\n");
        mapLink(sb, "아틀란 (ATLAN)", "kr.mappers.AtlanSmart", null,
                "https://play.google.com/store/apps/details?id=kr.mappers.AtlanSmart");
        // 웨이즈: 공식 링크 waze://?ll=위도,경도 / 웹 waze.com/ul (앱이 없으면 웨이즈 웹 지도)
        sb.append("\n");
        mapLink(sb, "웨이즈 (Waze)", "com.waze",
                "waze://?ll=" + ll + "&z=17",
                "https://www.waze.com/ul?ll=" + ll + "&z=17");
        // 맵스미: 공식 API mapsme://map?v=1&ll=위도,경도&n=이름 / 웹 지도가 없어 미설치면 Play 스토어
        sb.append("\n");
        mapLink(sb, "맵스미 (MAPS.ME)", "com.mapswithme.maps.pro",
                "mapsme://map?v=1&ll=" + ll + "&n=" + here,
                "https://play.google.com/store/apps/details?id=com.mapswithme.maps.pro");
        text.setText(sb);
        if (!fresh && !geoBusy) {
            lookupAddress(loc);
        }
    }

    // "▶ 구글 지도" 처럼 파란 밑줄 글자. 누르면: 그 지도 앱이 설치되어 있으면 앱으로(appUri), 없으면 웹으로(webUrl)
    private void mapLink(SpannableStringBuilder sb, String label, final String pkg, final String appUri,
                         final String webUrl) {
        sb.append("▶ ");
        int start = sb.length();
        sb.append(label);
        sb.setSpan(new android.text.style.ClickableSpan() {
            @Override
            public void onClick(View view) {
                openMapApp(pkg, appUri, webUrl);
            }
        }, start, sb.length(), android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new android.text.style.StyleSpan(Typeface.BOLD), start, sb.length(),
                android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }

    // 지도 앱이 설치되어 있는지 먼저 확인 (Android 11 이상은 매니페스트 <queries> 에 패키지가 있어야 보임)
    private boolean installed(String pkg) {
        try {
            getPackageManager().getPackageInfo(pkg, 0);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // pkgs: 패키지 이름 하나 또는 "새이름|옛이름" (앱이 이름을 바꾼 경우). 설치된 첫 번째를 돌려줌, 없으면 null
    private String installedPkg(String pkgs) {
        for (String pkg : pkgs.split("\\|")) {
            if (installed(pkg)) {
                return pkg;
            }
        }
        return null;
    }

    // 1) 그 앱 전용 주소(appUri)  2) 표준 좌표 주소(geo:)를 그 앱으로  3) 그냥 그 앱 실행
    // 앱이 없으면 webUrl (웹 지도, 웹 지도가 없는 내비 앱은 Play 스토어 설치 페이지)
    private void openMapApp(String pkgs, String appUri, String webUrl) {
        String pkg = installedPkg(pkgs);
        if (pkg != null && lastLoc != null) {
            String ll = String.format(Locale.US, "%.6f,%.6f", lastLoc.getLatitude(), lastLoc.getLongitude());
            String geo = "geo:" + ll + "?q=" + ll + "(" + Uri.encode("현재 위치") + ")";
            for (String link : new String[]{appUri, geo}) {
                if (link == null) {
                    continue;
                }
                Intent app = new Intent(Intent.ACTION_VIEW, Uri.parse(link));
                app.setPackage(pkg); // 브라우저나 다른 지도 앱이 아니라 바로 그 앱으로
                try {
                    startActivity(app);
                    return;
                } catch (Exception ignored) {
                    // 앱이 이 주소 형식을 못 받으면 다음 방법으로
                }
            }
            Intent launch = getPackageManager().getLaunchIntentForPackage(pkg);
            if (launch != null) {
                startActivity(launch);
                Toast.makeText(this, "이 앱은 좌표로 바로 열 수 없어 앱만 실행했습니다.", Toast.LENGTH_LONG).show();
                return;
            }
        }
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(webUrl)));
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, "링크를 열 수 있는 앱이 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    // 위도/경도 -> 주소, 우편번호 (인터넷 필요)
    private void lookupAddress(final Location loc) {
        geoBusy = true;
        final int id = scanId;
        new Thread(() -> {
            String result;
            try {
                if (!Geocoder.isPresent()) {
                    result = "주소: 이 기기는 주소 변환을 지원하지 않습니다.";
                } else {
                    List<Address> list = new Geocoder(this, Locale.KOREA)
                            .getFromLocation(loc.getLatitude(), loc.getLongitude(), 1);
                    if (list == null || list.isEmpty()) {
                        result = "주소: 찾을 수 없습니다.";
                    } else {
                        Address addr = list.get(0);
                        String zip = addr.getPostalCode() == null ? "(없음)" : addr.getPostalCode();
                        result = "주소: " + addr.getAddressLine(0) + "\n우편번호: " + zip;
                    }
                }
            } catch (Exception e) {
                result = "주소: 조회 실패 (인터넷 연결을 확인하세요)";
            }
            final String res = result;
            runOnUiThread(() -> {
                geoBusy = false;
                geoLoc = loc;
                geoText = res;
                if (mode == MODE_LOC && id == scanId) {
                    showLocation(loc, false);
                }
            });
        }).start();
    }

    // ---------- 위치 지도 / 복사 / 공유 ----------

    private boolean needLocation() {
        if (lastLoc == null) {
            Toast.makeText(this, "아직 위치를 받지 못했습니다. 잠시 기다리거나 [재검색]을 누르세요.", Toast.LENGTH_SHORT).show();
            return true;
        }
        return false;
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

    // ([지도] 버튼은 삭제: 1020 데이터 영역의 구글·네이버·카카오 지도 링크로 대신)

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
    private Button msgStyleButton(View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setTextSize(12);
        btn.setAllCaps(false);
        btn.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        btn.setPadding(24, 18, 12, 18); // 위아래 여백은 남겨 누르기 쉽게
        btn.setMinHeight(0);
        btn.setMinimumHeight(0);
        btn.setOnClickListener(listener);
        return btn;
    }

    // 버튼 글자: 이름 + " · " + 설정값(빨간색). values 가 null 이면 이름만
    private static void setJobText(Button btn, String name, String values) {
        SpannableStringBuilder sb = new SpannableStringBuilder(name);
        if (values != null && !values.isEmpty()) {
            sb.append(" · ");
            int st = sb.length();
            sb.append(values);
            sb.setSpan(new ForegroundColorSpan(0xFFC62828), st, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        btn.setText(sb);
    }

    // 한 줄 = [체크박스] + [버튼]. 체크박스가 없는 줄(별명)은 같은 폭의 빈자리를 둬서 버튼 왼쪽 끝을 맞춤
    private LinearLayout msgLine(CheckBox cb, Button btn) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        if (cb == null) {
            cb = new CheckBox(this);
            cb.setVisibility(View.INVISIBLE); // 자리만 차지
        }
        row.addView(cb);
        row.addView(btn, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        msgBox.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    // [메시지 공유] 아래 체크박스 없는 버튼 (보내는 사람 별명)
    private Button msgButton(View.OnClickListener listener) {
        Button btn = msgStyleButton(listener);
        msgLine(null, btn);
        return btn;
    }

    // ---------- [메시지 공유] 각 줄: 체크박스(켜기/끄기) + 버튼(설정) ----------

    private final Map<String, CheckBox> jobChecks = new HashMap<>(); // 설정 이름 -> 그 줄의 체크박스

    private Button msgRow(final String pref) {
        final CheckBox cb = new CheckBox(this);
        cb.setOnClickListener(view -> toggleJob(pref, cb)); // 사용자가 누를 때만 (코드로 바꿀 때는 반응 안 함)
        Button btn = msgStyleButton(view -> openJobSettings(pref));
        msgLine(cb, btn);
        jobChecks.put(pref, cb);
        return btn;
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
                    ? "텔레그램 설정(봇 토큰·받는 사람)을 먼저 해 주세요."
                    : schedProblem(); // 텔레그램도 1040 공통 '보내는 시각'을 씀
        }
        if (HomeService.PREF_KAKAO.equals(pref) && !kakaoInstalled()) {
            return "카카오톡이 설치되어 있지 않습니다.";
        }
        // 그 밖: 1040 맨 위 공통 '보내는 시각'이 저장되어 있어야 함
        if (schedProblem() != null) {
            return schedProblem();
        }
        if (HomeService.PREF_KAKAO.equals(pref)) {
            return null;
        }
        if (HomeService.PREF_SMSAUTO.equals(pref) && (sp.getString("apiKey", "").isEmpty()
                || sp.getString("apiSecret", "").isEmpty() || sp.getString("from", "").isEmpty())) {
            return "솔라피 API Key·Secret·발신번호를 먼저 설정해 주세요.";
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
            // 1040 공통 보내는 시각을 복사 (지정 시각: 다음 시각부터 / 간격: 바로 1회 후 간격대로). 텔레그램도 같음
            SharedPreferences.Editor ed = sp.edit().putBoolean("on", true);
            copySched(ed, true);
            ed.apply();
        } else {
            sp.edit().putBoolean("on", false).apply();
        }
        HomeService.refresh(this);
        refreshMsgButtons();
        Toast.makeText(this, cb.isChecked() ? "켰습니다." : "껐습니다.", Toast.LENGTH_SHORT).show();
    }

    // ---------- 본인 폰 알림 / 일반 문자(수동) : 정해진 시각·간격마다 알림 ----------

    private void updateJobButtons() {
        String[] prefs = {HomeService.PREF_SELF, HomeService.PREF_SMS, HomeService.PREF_SMSAUTO};
        Button[] btns = {selfBtn, smsBtn, smsAutoBtn};
        String[] names = {"본인 폰 알림 (자동)", "일반 문자 (수동)", "일반 문자 (자동)"};
        for (int i = 0; i < prefs.length; i++) {
            if (btns[i] == null) {
                continue;
            }
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
        // 일정 간격: 리스트 박스(Spinner)는 기기 테마에 따라 고른 값이 안 보여서 [1시간 ▼] 버튼 + 고르기 팝업(1245)
        final Button interval = new Button(MainActivity.this);
        int intervalIdx;

        // 줄 사이 간격 (예전보다 1.5배): 각 줄 위에 여백
        private LinearLayout.LayoutParams gapLp() {
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.topMargin = dpx(12);
            return lp;
        }

        ScheduleUi(LinearLayout form, SharedPreferences sp) {
            TextView title = label(form, "보내는 시각");
            title.setPadding(0, dpx(12), 0, dpx(6));
            RadioGroup mode = new RadioGroup(MainActivity.this);
            rTimes.setText("지정 시각 (매일)");
            rTimes.setId(View.generateViewId());
            rInterval.setText("일정 간격");
            rInterval.setId(View.generateViewId());
            for (RadioButton rb : new RadioButton[]{rTimes, rInterval}) {
                rb.setTextSize(15);
                rb.setPadding(rb.getPaddingLeft(), dpx(8), rb.getPaddingRight(), dpx(8)); // 줄 높이 1.5배
            }
            mode.addView(rTimes);
            RadioGroup.LayoutParams second = new RadioGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            second.topMargin = dpx(6);
            mode.addView(rInterval, second);
            form.addView(mode, gapLp());

            final LinearLayout timesRow = new LinearLayout(MainActivity.this);
            timesRow.setOrientation(LinearLayout.HORIZONTAL);
            timesRow.setGravity(Gravity.CENTER_VERTICAL);
            times.setSingleLine(true);
            times.setTextSize(16);
            times.setTextColor(Color.BLACK);      // 설정값이 테마 때문에 안 보이지 않게 글자색 고정
            times.setHintTextColor(Color.GRAY);
            times.setMinHeight(dpx(48));
            times.setHint("예: 08:00, 12:00, 18:00");
            times.setText(sp.getString("times", ""));
            Button addTime = new Button(MainActivity.this);
            addTime.setText("시각 추가");
            addTime.setTextSize(13);
            addTime.setOnClickListener(view -> showTagged(new TimePickerDialog(MainActivity.this, (tp, hour, minute) -> {
                String cur = times.getText().toString().trim();
                String add = String.format(Locale.US, "%02d:%02d", hour, minute);
                String merged = normalizeTimes(cur.isEmpty() ? add : cur + ", " + add);
                times.setText(merged == null ? cur + ", " + add : merged);
            }, 8, 0, true), "1240"));
            timesRow.addView(times, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            timesRow.addView(addTime);
            form.addView(timesRow, gapLp());

            int cur = sp.getInt("interval", 60);
            intervalIdx = 0;
            for (int i = 0; i < TelegramSender.INTERVALS.length; i++) {
                if (TelegramSender.INTERVALS[i] == cur) {
                    intervalIdx = i;
                }
            }
            interval.setAllCaps(false);
            interval.setTextSize(16);
            interval.setTextColor(Color.BLACK);
            interval.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            interval.setText(TelegramSender.INTERVAL_LABELS[intervalIdx] + "   ▼");
            interval.setOnClickListener(view -> ScreenId.tag(new AlertDialog.Builder(MainActivity.this)
                    .setTitle("일정 간격")
                    .setSingleChoiceItems(TelegramSender.INTERVAL_LABELS, intervalIdx, (dialog, which) -> {
                        intervalIdx = which;
                        interval.setText(TelegramSender.INTERVAL_LABELS[which] + "   ▼");
                        dialog.dismiss();
                    })
                    .setNegativeButton("취소", null)
                    .show(), "1245"));
            form.addView(interval, gapLp());

            mode.setOnCheckedChangeListener((group, id) -> {
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
            return TelegramSender.INTERVALS[intervalIdx];
        }
    }

    // 본인 폰 알림(PREF_SELF) / 일반 문자(PREF_SMS) 설정 팝업
    private void showJobDialog(final String pref) {
        final boolean auto = HomeService.PREF_SMSAUTO.equals(pref); // 일반 문자(자동, 솔라피)
        final boolean sms = HomeService.PREF_SMS.equals(pref) || auto;
        final String title = auto ? "일반 문자 (자동 · 솔라피)" : sms ? "일반 문자 (수동)" : "본인 폰 알림 (자동)";
        final SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(60, 10, 60, 10);

        TextView info = new TextView(this);
        info.setText(auto
                ? "정해진 시각마다 위치를 확인해 솔라피(유료 문자 서비스)로 바로 보냅니다.\n"
                + "솔라피 가입, API 키 발급, 발신번호 사전등록이 필요합니다. https://solapi.com"
                : sms
                ? "정해진 시각마다 위치를 확인해 알림을 띄웁니다. 알림을 누르면 받는 사람 번호와 내용이 "
                + "채워진 문자 앱이 열리고, [전송]을 누르면 보내집니다. (지금 요금제로 발송, 가입 불필요)"
                : "정해진 시각마다 이 폰에 현재 위치 알림을 띄웁니다. 아무에게도 보내지 않습니다.\n"
                + "알림을 누르면 지도가 열리고, [공유]로 원하는 곳에 보낼 수 있습니다.");
        info.setTextColor(Color.DKGRAY);
        info.setPadding(0, 16, 0, 0);
        form.addView(info);

        // 솔라피 (자동 문자 전용)
        final EditText apiKey = new EditText(this), apiSecret = new EditText(this), sender = new EditText(this);
        if (auto) {
            info.setAutoLinkMask(android.text.util.Linkify.WEB_URLS);
            info.setText(info.getText().toString());
            label(form, "솔라피 API Key");
            apiKey.setSingleLine(true);
            apiKey.setText(sp.getString("apiKey", ""));
            form.addView(apiKey);
            label(form, "솔라피 API Secret");
            apiSecret.setSingleLine(true);
            apiSecret.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            apiSecret.setText(sp.getString("apiSecret", ""));
            form.addView(apiSecret);
            label(form, "발신번호 (솔라피에 등록한 번호)");
            sender.setSingleLine(true);
            sender.setInputType(InputType.TYPE_CLASS_PHONE);
            sender.setHint("예: 010-1234-5678");
            sender.setText(sp.getString("from", ""));
            form.addView(sender);
        }

        final EditText to = new EditText(this);
        final RadioButton rShort = new RadioButton(this), rFull = new RadioButton(this);
        if (sms) {
            label(form, "받는 사람 번호 (여러 명은 쉼표로 구분)");
            to.setSingleLine(true);
            to.setInputType(InputType.TYPE_CLASS_PHONE);
            to.setHint("예: 010-1234-5678, 010-9876-5432");
            to.setText(sp.getString("to", ""));
            form.addView(to);
            Button pick = new Button(this);
            pick.setText("연락처에서 선택");
            pick.setTextSize(13);
            pick.setOnClickListener(view -> {
                contactTarget = to;
                contactNameTarget = null;
                contactWithName = false; // 문자 받는 사람 칸에는 번호만
                try {
                    startActivityForResult(new Intent(Intent.ACTION_PICK,
                            android.provider.ContactsContract.CommonDataKinds.Phone.CONTENT_URI), REQ_CONTACT);
                } catch (Exception e) {
                    Toast.makeText(this, "연락처를 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
            form.addView(pick, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

            label(form, "메시지 형식");
            RadioGroup fmt = new RadioGroup(this);
            rShort.setText("짧게 (주소 + 지도 링크 1개)");
            rShort.setId(View.generateViewId());
            rFull.setText("전체 (텔레그램·이메일과 같은 내용, 장문 문자)");
            rFull.setId(View.generateViewId());
            fmt.addView(rShort);
            fmt.addView(rFull);
            fmt.check(sp.getBoolean("short", true) ? rShort.getId() : rFull.getId());
            form.addView(fmt);
        }

        // 보내는 시각은 1040 화면 맨 위 공통 설정을 씀 (이 팝업에서는 입력하지 않음)
        schedNote(form);

        TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0), nx = sp.getLong("next", 0);
        SimpleDateFormat df = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA);
        last.setText((lt == 0 ? "아직 보낸 적이 없습니다." : (auto ? "마지막 발송: " : "마지막 알림: ") + df.format(new Date(lt))
                + (auto ? "  " + sp.getString("lastResult", "") : ""))
                + (sp.getBoolean("on", false) && nx > 0 ? "\n다음 알림: " + df.format(new Date(nx)) : ""));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        form.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(form);
        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        dialog.setOnShowListener(shown -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String nums = to.getText().toString().trim();
                boolean start = true; // 저장 = 1040 화면의 이 줄 자동 체크(켜기)
                if (sms && !nums.isEmpty() && !validPhones(nums)) {
                    Toast.makeText(this, "전화번호 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (schedProblem() != null) {
                    Toast.makeText(this, schedProblem(), Toast.LENGTH_LONG).show();
                    return;
                }
                if (start && auto && (apiKey.getText().toString().trim().isEmpty()
                        || apiSecret.getText().toString().trim().isEmpty()
                        || sender.getText().toString().trim().isEmpty())) {
                    Toast.makeText(this, "솔라피 API Key·Secret·발신번호를 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && sms && nums.isEmpty()) {
                    Toast.makeText(this, "받는 사람 번호를 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                SharedPreferences.Editor ed = sp.edit().putBoolean("on", start);
                copySched(ed, start); // 1040 공통 보내는 시각 + 다음 송신 시각
                if (sms) {
                    ed.putString("to", nums).putBoolean("short", rShort.isChecked());
                }
                if (auto) {
                    ed.putString("apiKey", apiKey.getText().toString().trim())
                            .putString("apiSecret", apiSecret.getText().toString().trim())
                            .putString("from", sender.getText().toString().trim());
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
                dialog.dismiss();
            });
            // 지금 테스트: 문자 앱 열기 / 이 폰에 바로 알림
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
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
                    // 자동 문자: 솔라피로 바로 발송하고 결과 표시 (팝업 유지)
                    final String err = auto ? SmsSender.send(apiKey.getText().toString(), apiSecret.getText().toString(),
                            sender.getText().toString(), nums, msg) : null;
                    runOnUiThread(() -> {
                        if (auto) {
                            last.setText(err == null ? "지금 테스트: 성공 (문자를 보냈습니다)" : "지금 테스트: 실패: " + err);
                        } else if (sms) {
                            dialog.dismiss();
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
        dialog.show();
        ScreenId.tag(dialog, auto ? "1235" : sms ? "1230" : "1140");
    }

    // 전화번호 목록 확인 (숫자 3자리 이상씩)
    private static boolean validPhones(String phones) {
        for (String one : phones.split("[,;]+")) {
            String digits = one.replaceAll("[^0-9]", "");
            if (!one.trim().isEmpty() && digits.length() < 3) {
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
        try (android.database.Cursor cursor = getContentResolver().query(data.getData(),
                new String[]{android.provider.ContactsContract.CommonDataKinds.Phone.NUMBER,
                        android.provider.ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String num = cursor.getString(0);
                String name = cursor.getString(1);
                if (contactNameTarget != null) { // (6) 피보호자 카드: 번호는 바꾸고, 이름은 비었을 때만
                    contactTarget.setText(num);
                    if (name != null && contactNameTarget.getText().toString().trim().isEmpty()) {
                        contactNameTarget.setText(name);
                    }
                    contactNameTarget = null;
                    return;
                }
                if (contactWithName && name != null && !name.trim().isEmpty()) {
                    num = name.replaceAll("[,;0-9+]", "").trim() + " " + num; // "엄마 010-1234-5678"
                }
                String cur = contactTarget.getText().toString().trim();
                contactTarget.setText(cur.isEmpty() ? num : cur + ", " + num);
            }
        } catch (Exception e) {
            Toast.makeText(this, "연락처 번호를 읽을 수 없습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    // 본인 발신 Gmail(사용자 등록 1075)이 없으면 경고 팝업(화면 1145)을 띄우고 true
    private boolean noUserMail(String why) {
        SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        if (!user.getString("mail", "").trim().isEmpty() && !user.getString("mailPass", "").trim().isEmpty()) {
            return false;
        }
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("⚠️ 발신 이메일 미등록")
                .setMessage(why + " 먼저 본인 Gmail 과 앱 비밀번호를 등록해야 합니다.\n\n"
                        + "(1) 홈 공유기 등록 → [사용자 등록] 에서 등록하고 검증하세요.")
                .setPositiveButton("사용자 등록 열기", (dialog, which) -> startActivity(new Intent(this, UserActivity.class)))
                .setNegativeButton("닫기", null)
                .show(), "1145");
        return true;
    }

    // ---------- 이메일 위치 질문: 회신할까요? (화면 1057, 알림을 눌러 앱이 열렸을 때) ----------

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleMailAsk(intent);
    }

    private void handleMailAsk(Intent it) {
        final String from = it == null ? null : it.getStringExtra("mailAsk");
        if (from == null) {
            return;
        }
        it.removeExtra("mailAsk"); // 화면을 돌려도 다시 묻지 않게
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("📨 위치 질문")
                .setMessage(HomeService.mailAskText(this, from) + " (Yes/No)")
                .setCancelable(false)
                .setPositiveButton("Yes", (dialog, which) -> mailAnswer(from, true))
                .setNegativeButton("No", (dialog, which) -> mailAnswer(from, false))
                .show(), "1057");
    }

    private void mailAnswer(String from, boolean yes) {
        Intent intent = new Intent(this, HomeService.class)
                .setAction(yes ? HomeService.ACTION_MAIL_YES : HomeService.ACTION_MAIL_NO).putExtra("to", from);
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
        Toast.makeText(this, yes ? "위치를 확인해 이메일로 회신합니다." : "회신하지 않았습니다.", Toast.LENGTH_SHORT).show();
    }

    // ---------- (5) 보호자 전용 모드: 보호자가 피보호자를 관리 (화면 1058) ----------
    // 피보호자 목록은 설정 "ward" 의 "wards" = [{name, phone, email}, …] (JSON)

    private static final String PREF_WARD = "ward";

    private static class WardCard {
        LinearLayout box;
        TextView title;
        EditText name, phone, email;
        Button rel; // 관계
    }

    private final List<WardCard> wardCards = new ArrayList<>();
    private LinearLayout wardList;

    private TextView wardHead; // 1058 첫 줄 안내 (사용자 등록 이름이 들어감)

    // "이 화면에서는 현재 사용자(홍길동)가 …" - 이름은 사용자 등록(1075)의 이름
    private void updateWardHead() {
        if (wardHead == null) {
            return;
        }
        String me = TelegramSender.senderName(this);
        wardHead.setText("이 화면에서는 현재 사용자(" + (me.isEmpty() ? "이름 미등록" : me)
                + ")가 보호자 자격으로 피보호자를 관리합니다.");
    }

    private void buildWardTab(final LinearLayout body) {
        wardHead = new TextView(this);
        wardHead.setTextSize(16);
        wardHead.setTypeface(null, Typeface.BOLD);
        wardHead.setPadding(40, 36, 40, 8);
        updateWardHead();
        body.addView(wardHead);
        // 안내문: '피보호자 폰' 은 굵은 빨간색
        SpannableStringBuilder guide = new SpannableStringBuilder("이 기능이 정상 작동하려면, ");
        append(guide, "피보호자 폰", C_BAD, true);
        guide.append("에서 [나의 보호자 등록]을 먼저 하여야 합니다. 그리고, 이 화면에서 [등록]을 하세요.\n\n"
                + "[지금 위치 확인] 버튼을 누르면, 시스템이 자동적으로 피보호자의 위치를 파악해서, "
                + "2-3분 후에 보호자에게 이메일로 통보합니다.\n\n"
                + "보호자께서는 자신의 메일함을 확인하세요.");
        smallNote(body, "").setText(guide);

        wardList = new LinearLayout(this);
        wardList.setOrientation(LinearLayout.VERTICAL);
        wardList.setPadding(30, 0, 30, 0);
        body.addView(wardList);
        try {
            org.json.JSONArray arr = new org.json.JSONArray(getSharedPreferences(PREF_WARD, MODE_PRIVATE)
                    .getString("wards", "[]"));
            for (int i = 0; i < arr.length(); i++) {
                org.json.JSONObject item = arr.getJSONObject(i);
                addWardCard(item.optString("name"), item.optString("phone"), item.optString("email"), item.optString("rel"));
            }
        } catch (Exception ignored) {
        }
        if (wardCards.isEmpty()) {
            addWardCard("", "", "", ""); // 처음엔 빈 카드 한 장
        }

        // 좌우 넓은 [피보호자 추가]: 새 카드를 맨 아래에 붙이고 그 자리로 스크롤
        Button add = new Button(this);
        add.setText("피보호자 추가");
        add.setTypeface(null, Typeface.BOLD);
        add.setOnClickListener(view -> {
            final WardCard card = addWardCard("", "", "", "");
            if (body.getParent() instanceof ScrollView) {
                final ScrollView sv = (ScrollView) body.getParent();
                sv.postDelayed(() -> { // 새 카드가 화면에 배치된 뒤 그 위치로
                    sv.smoothScrollTo(0, wardList.getTop() + card.box.getTop());
                    card.name.requestFocus();
                }, 150);
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(30, 24, 30, 30);
        body.addView(add, lp);
    }

    // 카드 한 장:  < 피보호자 N > / [지금 위치 확인] (좌우 긴 버튼) / 이름 / 관계 / 전화번호 [연락처에서 선택] / 이메일 / [등록] [테스트] [삭제]
    private WardCard addWardCard(String name, String phone, String email, String relation) {
        final WardCard card = new WardCard();
        card.box = new LinearLayout(this);
        card.box.setOrientation(LinearLayout.VERTICAL);
        card.box.setPadding(30, 20, 30, 20);
        card.box.setBackgroundColor(0x0F000000);

        card.title = new TextView(this);
        card.title.setTextSize(15);
        card.title.setTypeface(null, Typeface.BOLD);
        card.box.addView(card.title);

        // 제목 바로 아래 좌우로 긴 [지금 위치 확인]
        Button locate = new Button(this);
        locate.setText("지금 위치 확인");
        locate.setTypeface(null, Typeface.BOLD);
        locate.setOnClickListener(view -> wardMail(card, true));
        LinearLayout.LayoutParams llp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        llp.topMargin = 8;
        llp.bottomMargin = 8;
        card.box.addView(locate, llp);

        card.name = wardField(card.box, "이름 :", name, InputType.TYPE_CLASS_TEXT);
        card.rel = wardRelation(card.box, relation);
        card.phone = wardField(card.box, "전화번호 :", phone, InputType.TYPE_CLASS_PHONE);
        Button pick = smallBtn("연락처에서 선택", view -> {
            contactTarget = card.phone;
            contactNameTarget = card.name;
            contactWithName = false;
            try {
                startActivityForResult(new Intent(Intent.ACTION_PICK,
                        android.provider.ContactsContract.CommonDataKinds.Phone.CONTENT_URI), REQ_CONTACT);
            } catch (Exception e) {
                Toast.makeText(this, "연락처를 열 수 없습니다.", Toast.LENGTH_SHORT).show();
            }
        });
        ((LinearLayout) card.phone.getParent()).addView(pick);
        card.email = wardField(card.box, "이메일 :", email,
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        // 맨 아래 한 줄: [등록] [테스트] [삭제] - 같은 너비, 같은 간격
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button reg = new Button(this);
        reg.setText("등록");
        reg.setOnClickListener(view -> saveWards(true));
        row.addView(reg, wardBtnLp());
        Button test = new Button(this);
        test.setText("테스트");
        test.setOnClickListener(view -> wardMail(card, false));
        row.addView(test, wardBtnLp());
        Button del = new Button(this);
        del.setText("삭제");
        del.setOnClickListener(view -> ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle("피보호자 삭제")
                .setMessage(card.title.getText() + " 을(를) 삭제할까요?")
                .setPositiveButton("삭제", (dialog, which) -> {
                    wardCards.remove(card);
                    wardList.removeView(card.box);
                    if (wardCards.isEmpty()) {
                        addWardCard("", "", "", "");
                    }
                    renumberWards();
                    saveWards(false);
                    Toast.makeText(this, "삭제했습니다.", Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("취소", null)
                .show(), "1059"));
        row.addView(del, wardBtnLp());
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rlp.topMargin = 12;
        card.box.addView(row, rlp);

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 24;
        wardList.addView(card.box, lp);
        wardCards.add(card);
        renumberWards();
        return card;
    }

    // 카드 맨 아래 버튼 3개: 같은 너비(1:1:1) + 좌우 같은 여백
    private LinearLayout.LayoutParams wardBtnLp() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.leftMargin = dpx(4);
        lp.rightMargin = dpx(4);
        return lp;
    }

    private void renumberWards() {
        for (int i = 0; i < wardCards.size(); i++) {
            wardCards.get(i).title.setText("< 피보호자 " + (i + 1) + " >");
        }
    }

    // "라벨 : [입력 칸]" 한 줄. 입력 칸을 돌려줌 (부모 줄에 버튼을 더 붙일 수 있음)
    private EditText wardField(LinearLayout parent, String label, String value, int type) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView caption = new TextView(this);
        caption.setText(label);
        caption.setTextSize(14);
        caption.setMinWidth((int) (72 * getResources().getDisplayMetrics().density));
        row.addView(caption);
        EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setInputType(type);
        edit.setText(value);
        edit.setTextSize(14);
        row.addView(edit, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        parent.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return edit;
    }

    // "관계 : [관계 버튼 ▼]" 한 줄. 버튼을 누르면 고르기 팝업(1102), 고르면 바로 저장
    private Button wardRelation(LinearLayout parent, String value) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView caption = new TextView(this);
        caption.setText("관계 :");
        caption.setTextSize(14);
        caption.setMinWidth((int) (72 * getResources().getDisplayMetrics().density));
        row.addView(caption);
        Button btn = GuardianActivity.relationButton(this, value, () -> saveWards(false));
        row.addView(btn, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        parent.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return btn;
    }

    private Button smallBtn(String text, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setTextSize(12);
        btn.setAllCaps(false);
        btn.setOnClickListener(listener);
        return btn;
    }

    // 모든 카드를 저장. 이메일 형식이 틀리면 저장하지 않고 안내
    private boolean saveWards(boolean toast) {
        org.json.JSONArray arr = new org.json.JSONArray();
        for (WardCard card : wardCards) {
            String nm = card.name.getText().toString().trim();
            String ph = card.phone.getText().toString().trim();
            String em = card.email.getText().toString().trim();
            if (!em.isEmpty() && !Patterns.EMAIL_ADDRESS.matcher(em).matches()) {
                Toast.makeText(this, card.title.getText() + " 이메일 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                return false;
            }
            if (nm.isEmpty() && ph.isEmpty() && em.isEmpty()) {
                continue; // 빈 카드는 저장 안 함
            }
            try {
                arr.put(new org.json.JSONObject().put("name", nm).put("phone", ph).put("email", em)
                        .put("rel", GuardianActivity.relationOf(card.rel)));
            } catch (Exception ignored) {
            }
        }
        getSharedPreferences(PREF_WARD, MODE_PRIVATE).edit().putString("wards", arr.toString()).apply();
        if (toast) {
            Toast.makeText(this, "등록했습니다. (피보호자 " + arr.length() + "명)", Toast.LENGTH_SHORT).show();
        }
        return true;
    }

    // [테스트] = 테스트 문구 메일, [지금 위치 확인] = 제목 "#where" 메일 (내 Gmail -> 피보호자 이메일)
    private void wardMail(WardCard card, final boolean locate) {
        if (noUserMail(locate ? "위치를 물어보려면" : "테스트 메일을 보내려면")) {
            return;
        }
        final String to = card.email.getText().toString().trim();
        if (!Patterns.EMAIL_ADDRESS.matcher(to).matches()) {
            Toast.makeText(this, "피보호자 이메일을 확인하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        saveWards(false);
        SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        final String from = user.getString("mail", "");
        final String pass = user.getString("mailPass", "");
        String nm = card.name.getText().toString().trim();
        final String ward = nm.isEmpty() ? "피보호자" : nm;
        String me = TelegramSender.senderName(this);
        final String meName = me.isEmpty() ? from : me;
        final String subject = locate ? "#where" : "[집에왔어요] 테스트 메일";
        final String body = locate
                ? "'집에왔어요' 앱이 보낸 위치 요청입니다.\n"
                + ward + " 님 폰의 집에왔어요 앱이 현재 위치를 이 주소(" + from + ")로 회신합니다.\n"
                + "(" + ward + " 님 폰의 보호자 등록에 " + from + " 이 적혀 있어야 합니다)"
                : "보호자 " + meName + " 님이 '집에왔어요' 앱에서 보낸 테스트 메일입니다.\n"
                + "이 메일이 보이면 보호자 → " + ward + " 이메일 연결이 정상입니다.";
        Toast.makeText(this, "보내는 중...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String err = EmailSender.send(from, pass, to, subject, body);
            runOnUiThread(() -> Toast.makeText(this, err != null ? "보내기 실패: " + err
                    : locate ? "위치 요청을 보냈습니다. 잠시 후 Gmail 받은편지함에서 회신을 확인하세요."
                    : "테스트 메일을 보냈습니다. (" + to + ")", Toast.LENGTH_LONG).show());
        }).start();
    }

    // ---------- (4) 응답 모드 탭 ----------

    private TextView remoteInfo, mailRemoteInfo;
    private CheckBox tgRemoteCheck, mailAutoCheck; // (4) 응답 모드(1050) 체크박스

    private void buildGuardianTab(LinearLayout body) {
        final SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);

        // 0) 원격 위치 요청 (이메일): 권한 필요 없음. 요청도 회신도 이메일
        buildMailRemote(body);
        body.addView(thickDivider(), thickDividerLp());

        // 1) 원격 위치 요청
        sectionTitle(body, "■ 원격 위치 요청 (텔레그램)");
        smallNote(body, "보호자가 텔레그램 봇에게 \"위치\" (또는 \"어디야\", /where)를 보내면,\n"
                + "이 폰이 현재 위치를 자동으로 답장합니다. 이 폰을 만질 필요가 없습니다.\n"
                + "※ 텔레그램 '받는 사람'으로 등록된 보호자만 요청할 수 있습니다.");
        final CheckBox remote = new CheckBox(this);
        tgRemoteCheck = remote; // 다른 화면(1100)에서 바뀌면 onResume 에서 맞춤
        remote.setText("원격 위치 요청 받기");
        remote.setTextSize(15);
        remote.setTypeface(null, Typeface.BOLD);
        remote.setChecked(tg.getBoolean("remote", false));
        remote.setOnClickListener(view -> {
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
            if (remote.isChecked()) {
                BtHome.requestScan(this); // 답장에 주변 블루투스 목록을 넣으려면 근처 기기 권한
            }
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
        msgStyleButtonFor(body, "가족 전체 조회", view -> showComingSoon("가족 전체 조회"));
        msgStyleButtonFor(body, "비밀번호 잠금", view -> showComingSoon("비밀번호 잠금"));

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
        ScreenId.tag(new AlertDialog.Builder(this)
                .setTitle(name)
                .setMessage("지금 추가 개발 중입니다.")
                .setPositiveButton("확인", null)
                .show(), "가족 전체 조회".equals(name) ? "1260" : "1270");
    }

    // ■ 원격 위치 요청 (이메일): 보호자가 사용자 Gmail 로 제목 "#where" 메일 -> 이 폰이 위치를 이메일로 회신
    private void buildMailRemote(LinearLayout body) {
        final SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        sectionTitle(body, "■ 원격 위치 요청 (이메일)");
        smallNote(body, "보호자가 이 사용자의 Gmail 로 제목 \"#where\" (대소문자 무관) 메일을 보내면, 이 폰이 현재 위치를 이메일로 회신합니다.\n"
                + "☑ 체크: 바로 자동 회신 (이 폰을 만질 필요 없음)\n"
                + "☐ 체크 안 함: \"보호자 ○○님으로부터 위치 질문을 받았습니다. 회신할까요?\" 를 물어보고 Yes 일 때만 회신\n"
                + "※ 사용자 등록(1075)의 Gmail 인증과, 보호자 등록(1100)에 보호자 이메일이 있어야 요청을 받습니다.\n"
                + "※ 앱을 켤 때 받은편지함에 남아 있는 요청 메일도 찾아 처리합니다. 처리한 요청 메일은 삭제(휴지통)합니다.");
        final CheckBox on = new CheckBox(this);
        on.setText("이메일 위치 요청에 자동으로 회신응답");
        mailAutoCheck = on; // 다른 화면(1100)에서 바뀌면 onResume 에서 맞춤
        on.setTextSize(15);
        on.setTypeface(null, Typeface.BOLD);
        on.setChecked(user.getBoolean("mailRemote", false));
        on.setOnClickListener(view -> {
            if (on.isChecked()) {
                if (!user.getBoolean("mailVerified", false) || user.getString("mailPass", "").isEmpty()) {
                    on.setChecked(false);
                    Toast.makeText(this, "사용자 등록에서 발신 이메일(Gmail) 인증을 먼저 해 주세요.", Toast.LENGTH_LONG).show();
                    startActivity(new Intent(this, UserActivity.class));
                    return;
                }
                if (SosActivity.prefs(this).getString("emails", "").trim().isEmpty()) {
                    on.setChecked(false);
                    Toast.makeText(this, "보호자 등록에서 보호자 이메일을 먼저 적어 주세요.", Toast.LENGTH_LONG).show();
                    showSosSettings();
                    return;
                }
                if (!hasPermission()) {
                    on.setChecked(false);
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 눌러 주세요.", Toast.LENGTH_LONG).show();
                    return;
                }
            }
            user.edit().putBoolean("mailRemote", on.isChecked()).putString("mailRemoteErr", "").apply();
            if (on.isChecked()) {
                BtHome.requestScan(this); // 회신에 주변 블루투스 목록을 넣으려면 근처 기기 권한
            }
            HomeService.refresh(this);
            Toast.makeText(this, on.isChecked()
                    ? "자동 회신을 켰습니다. 보호자가 \"#where\" 메일을 보내 보세요."
                    : "자동 회신을 껐습니다. 요청이 오면 회신할지 물어봅니다.", Toast.LENGTH_LONG).show();
            updateRemoteInfo();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        body.addView(on, lp);

        mailRemoteInfo = smallNote(body, "");
        mailRemoteInfo.setTextColor(Color.DKGRAY);
        mailRemoteInfo.setTextSize(13);
        msgStyleButtonFor(body, "지금 확인 (Gmail 접속·최근 메일 판단 보기)", view -> diagnoseMailRemote());

        smallNote(body, "보호자가 하는 방법:\n"
                + " 1. 아무 메일 앱(Gmail, 네이버 메일 …)을 엽니다.\n"
                + " 2. 받는 사람 = 이 사용자의 Gmail, 제목 = #where (대소문자 무관, 내용은 비워도 됨)\n"
                + " 3. 보내면 잠시 뒤 주소·지도 링크가 담긴 회신 메일이 옵니다. (보통 1분 안)\n"
                + "   (보호자 폰에도 집에왔어요가 있으면 (5) 보호자 전용 모드의 [지금 위치 확인]으로 보낼 수 있습니다)\n\n"
                + "※ 이 폰의 배터리 설정을 '제한 없음'으로 해야 화면이 꺼져 있을 때도 빨리 회신합니다.");
    }

    // [지금 확인]: Gmail 에 접속해 최근 메일 5통을 앱이 어떻게 판단하는지 팝업(화면 1055)으로 보여 줌
    private void diagnoseMailRemote() {
        final SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        if (user.getString("mail", "").isEmpty() || user.getString("mailPass", "").isEmpty()) {
            Toast.makeText(this, "사용자 등록에서 발신 이메일(Gmail)을 먼저 등록하세요.", Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(this, "Gmail 에 접속하는 중...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String rep = MailWatcher.diagnose(user.getString("mail", ""), user.getString("mailPass", ""),
                    user.getLong("mailUid", 0), HomeService.guardianEmails(this));
            String head = "감시: " + (HomeService.mailRemoteOn(this) ? "켜짐" : "꺼짐 (Gmail 인증·보호자 이메일 필요)")
                    + " / 회신: " + (user.getBoolean("mailRemote", false) ? "자동" : "물어보고")
                    + "\n감시 상태: " + user.getString("mailRemoteState", "아직 접속한 적 없음 (서비스가 안 돌고 있을 수 있음)")
                    + "\n\n";
            runOnUiThread(() -> {
                updateRemoteInfo();
                TextView tv = new TextView(this);
                tv.setText(head + rep);
                tv.setTextSize(13);
                tv.setTextIsSelectable(true);
                tv.setPadding(50, 20, 50, 20);
                ScrollView sv = new ScrollView(this);
                sv.addView(tv);
                ScreenId.tag(new AlertDialog.Builder(this)
                        .setTitle("이메일 위치 요청 - 지금 확인")
                        .setView(sv)
                        .setPositiveButton("닫기", null)
                        .show(), "1055");
            });
        }).start();
    }

    private void updateRemoteInfo() {
        // 체크박스를 저장된 값에 맞춤 (나의 보호자 등록(1100)의 '자동 응답' 질문으로 바뀔 수 있음)
        if (mailAutoCheck != null) {
            mailAutoCheck.setChecked(getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE)
                    .getBoolean("mailRemote", false));
        }
        if (tgRemoteCheck != null) {
            tgRemoteCheck.setChecked(getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE)
                    .getBoolean("remote", false));
        }
        if (mailRemoteInfo != null) {
            SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
            long time = user.getLong("mailRemoteTime", 0);
            String err = user.getString("mailRemoteErr", "");
            boolean on = HomeService.mailRemoteOn(this); // Gmail 인증 + 보호자 이메일 있으면 감시 중
            mailRemoteInfo.setText((on ? "상태: " + user.getString("mailRemoteState", "아직 접속 안 함") + "\n" : "")
                    + (time == 0 ? "아직 요청을 받은 적이 없습니다."
                    : "마지막 요청: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(time))
                    + "  " + user.getString("mailRemoteResult", ""))
                    + (on && !err.isEmpty() ? "\n⚠️ " + err : ""));
        }
        if (remoteInfo == null) {
            return;
        }
        SharedPreferences tg = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        long time = tg.getLong("remoteTime", 0);
        remoteInfo.setText(time == 0 ? "아직 요청을 받은 적이 없습니다."
                : "마지막 요청: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(time))
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
        sos.setOnTouchListener((view, event) -> {
            switch (event.getAction()) {
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

    // (2) 현재 위치 탭의 보호자 전화 걸기 버튼은 삭제 (화면 1020). SOS 는 화면 오른쪽 위 버튼으로
    // 앱 화면의 SOS: 2초 길게 눌러 확인했으므로 카운트다운 없이 바로 실행
    private void startSosNow() {
        if (!hasPermission()) {
            ensurePermission();
            Toast.makeText(this, "위치 권한이 있어야 위치를 보낼 수 있습니다.", Toast.LENGTH_LONG).show();
        }
        Toast.makeText(this, "🆘 SOS: 보호자에게 전화하고 위치를 보냅니다.", Toast.LENGTH_LONG).show();
        SosActivity.fire(this);
    }

    // (2) 알림 설정 탭 맨 위: SOS 설정
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
        msgStyleButtonFor(body, "보호자 전화번호 설정 (최대 3명)", view -> showSosSettings());
        msgStyleButtonFor(body, "홈 화면에 SOS 바로가기 추가", view -> addSosShortcut());
        smallNote(body, "바로가기를 누르면 3초 카운트다운 후 작동합니다. (그 사이 [취소] 가능)\n"
                + "앱 아이콘을 길게 눌러 나오는 메뉴에도 'SOS'가 있습니다.");
    }

    private void updateSosInfo() {
        if (sosInfo == null) {
            return;
        }
        StringBuilder ps = new StringBuilder(); // "엄마 010-…, 아빠 010-…"
        for (String[] entry : SosActivity.entries(this)) {
            ps.append(ps.length() > 0 ? ", " : "").append(SosActivity.label(entry));
        }
        SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);
        long time = ev.getLong("sosTime", 0);
        sosInfo.setText("보호자: " + (ps.length() == 0 ? "(등록 안 됨)" : ps)
                + (time == 0 ? "" : "\n마지막 SOS: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(time))
                + "  " + ev.getString("sosResult", "")));
    }

    // 보호자 등록: 화면 1100 (별도 화면 GuardianActivity) 열기
    private void showSosSettings() {
        startActivity(new Intent(this, GuardianActivity.class));
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

    // ---------- (2) 알림 설정 탭 ----------

    private CheckBox evHome, evAway;
    private TextView evLast;
    // 귀가·외출 알림을 보낼 곳: {설정 키, 표시 이름}
    private static final String[][] EV_CHANNELS = {
            {"ch_self", "본인 폰"}, {"ch_email", "일반 이메일"},
            {"ch_kakao", "카톡 본인 메시지"}, {"ch_tg", "텔레그램"}, {"ch_sms", "일반 문자(수동)"}, {"ch_smsauto", "일반 문자(자동)"}};

    private TextView sectionTitle(LinearLayout body, String heading) {
        TextView tv = new TextView(this);
        tv.setText(heading);
        tv.setTextSize(17);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextColor(0xFF1976D2);
        tv.setPadding(40, 36, 40, 8);
        body.addView(tv);
        return tv;
    }

    private TextView smallNote(LinearLayout body, String msg) {
        TextView tv = new TextView(this);
        tv.setText(msg);
        tv.setTextSize(12);
        tv.setTextColor(Color.GRAY);
        tv.setPadding(40, 0, 40, 8);
        body.addView(tv);
        return tv;
    }

    private void buildAlarmTab(LinearLayout body) {
        final SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);

        // 1) 귀가·외출 순간 알림
        sectionTitle(body, "■ 귀가 · 외출 · 미귀가 알림");
        smallNote(body, "홈 공유기에 연결되거나 끊기는 순간, 아래에서 고른 곳으로 바로 알립니다.\n"
                + "(받는 사람·계정·보내는 사람 별명은 (3) 위치 보내기 탭의 각 설정을 그대로 씁니다)");
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        evHome = new CheckBox(this);
        evHome.setText("귀가하면 알림");
        evHome.setTextSize(15);
        evHome.setChecked(ev.getBoolean("home", false));
        evHome.setOnClickListener(view -> toggleEvent("home", evHome));
        body.addView(evHome, lp);
        evAway = new CheckBox(this);
        evAway.setText("외출하면 알림 (2분 넘게 밖에 있을 때)");
        evAway.setTextSize(15);
        evAway.setChecked(ev.getBoolean("away", false));
        evAway.setOnClickListener(view -> toggleEvent("away", evAway));
        body.addView(evAway, lp);

        // 미귀가 알림: [22:00]까지 귀가하지 않으면 알림 (시각 버튼을 눌러 변경)
        LinearLayout lateRow = new LinearLayout(this);
        lateRow.setOrientation(LinearLayout.HORIZONTAL);
        lateRow.setGravity(Gravity.CENTER_VERTICAL);
        final CheckBox evLate = new CheckBox(this);
        evLate.setText("미귀가 알림:");
        evLate.setTextSize(15);
        evLate.setChecked(ev.getBoolean("late", false));
        evLate.setOnClickListener(view -> toggleEvent("late", evLate));
        lateRow.addView(evLate);
        final Button lateTime = new Button(this);
        lateTime.setText(ev.getString("lateTime", "22:00"));
        lateTime.setTextSize(14);
        lateTime.setTextColor(0xFFC62828);
        lateTime.setOnClickListener(view -> {
            int[] hm = HomeService.parseTime(ev.getString("lateTime", "22:00"));
            showTagged(new TimePickerDialog(this, (tp, hour, minute) -> {
                String hhmm = String.format(Locale.US, "%02d:%02d", hour, minute);
                ev.edit().putString("lateTime", hhmm).apply();
                lateTime.setText(hhmm);
                HomeService.refresh(this); // 새 시각으로 다시 예약
            }, hm == null ? 22 : hm[0], hm == null ? 0 : hm[1], true), "1120");
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
            final CheckBox check = new CheckBox(this);
            check.setText(EV_CHANNELS[k][1]);
            check.setTextSize(13);
            check.setChecked(ev.getBoolean(key, "ch_self".equals(key))); // 처음에는 '본인 폰'만 체크
            check.setOnClickListener(view -> ev.edit().putBoolean(key, check.isChecked()).apply());
            line.addView(check, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }
        body.addView(grid);
        if (!ev.contains("ch_self")) {
            ev.edit().putBoolean("ch_self", true).apply();
        }
        smallNote(body, "※ 카톡 본인 메시지·일반 문자, 그리고 '수동'으로 설정한 이메일은 알림을 눌러 직접 보내야 합니다.");

        Button test = msgStyleButtonFor(body, "테스트 (지금 '귀가' 알림 보내 보기)", view -> {
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

    // (3) 위치 보내기 탭: 정해진 시각 위치 보내기 (보내는 사람 별명 + 채널별 줄)
    private void buildSendTab(LinearLayout body) {
        // ---------- 맨 위: 공통 보내는 시각 (1140·1150·1170·1230·1235 가 함께 씀) + [저장] ----------
        sectionTitle(body, "■ 보내는 시각 (공통)");
        smallNote(body, "아래 본인 폰 알림·일반 이메일·카톡 본인 메시지·텔레그램·일반 문자(수동·자동)가 "
                + "모두 이 시각에 보냅니다.");
        LinearLayout schBox = new LinearLayout(this);
        schBox.setOrientation(LinearLayout.VERTICAL);
        schBox.setPadding(40, 0, 40, 30);
        body.addView(schBox);
        seedSched();
        final ScheduleUi sch = new ScheduleUi(schBox, sched());
        Button save = new Button(this);
        save.setText("저장");
        save.setTypeface(null, Typeface.BOLD);
        save.setOnClickListener(view -> saveSched(sch));
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        slp.topMargin = dpx(18); // 줄 사이 간격 1.5배
        schBox.addView(save, slp);
        body.addView(thickDivider(), thickDividerLp());

        sectionTitle(body, "■ 정해진 시각 위치 보내기");

        // 외출 중에만 보내기: 집에 있으면 보내지 않고, 외출(2분 넘게 밖)이 확인되면 그때부터 보냄
        final CheckBox away = new CheckBox(this);
        away.setText("외출 중에만 보내기");
        away.setTextSize(15);
        away.setTypeface(null, Typeface.BOLD);
        away.setChecked(HomeService.awayOnly(this));
        away.setOnClickListener(view -> {
            if (away.isChecked() && loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
                away.setChecked(false);
                Toast.makeText(this, "홈 공유기 또는 홈 PC 블루투스를 먼저 등록하세요. ((1) 공유기 탭)",
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

        // '보내는 사람' 줄은 삭제: 메시지의 보내는 사람 이름은 사용자 등록(1075)의 이름(별명)을 씀
        // 아래 줄들: [☐ 체크박스] = 켜기/끄기, [버튼] = 설정 팝업
        selfBtn = msgRow(HomeService.PREF_SELF);   // 본인 폰 알림 (이 폰에 위치 알림)
        emailBtn = msgRow(HomeService.PREF_EMAIL); // 일반 이메일 (자동/수동)
        // (카톡 이메일 1160 은 삭제)
        kakaoBtn = msgRow(HomeService.PREF_KAKAO); // 카톡 본인 메시지 (수동: 알림 -> 카톡 공유 화면)
        tgBtn = msgRow(TelegramSender.PREF);       // 텔레그램 (자동)
        smsBtn = msgRow(HomeService.PREF_SMS);     // 일반 문자 (수동: 알림 -> 문자 앱)
        smsAutoBtn = msgRow(HomeService.PREF_SMSAUTO); // 일반 문자 (자동: 솔라피로 바로 발송)
        refreshMsgButtons();
    }

    // ---------- 공통 보내는 시각 (설정 이름 HomeService.PREF_SCHED) ----------

    // 공통 보내는 시각을 쓰는 채널 (텔레그램도 포함 - 예전 '자체 간격'은 없앰)
    private static final String[] SCHED_JOBS = {HomeService.PREF_SELF, HomeService.PREF_EMAIL,
            HomeService.PREF_KAKAO, TelegramSender.PREF, HomeService.PREF_SMS, HomeService.PREF_SMSAUTO};

    private SharedPreferences sched() {
        return getSharedPreferences(HomeService.PREF_SCHED, MODE_PRIVATE);
    }

    // 공통 시각이 아직 없으면(업데이트 직후) 예전에 각 팝업에서 저장해 둔 시각을 가져옴
    private void seedSched() {
        if (sched().contains("mode")) {
            return;
        }
        for (String prefName : SCHED_JOBS) {
            SharedPreferences sp = getSharedPreferences(prefName, MODE_PRIVATE);
            if (sp.contains("mode") && !(prefName.equals(HomeService.PREF_KAKAO))
                    && !prefName.equals(TelegramSender.PREF)) {
                sched().edit().putString("mode", sp.getString("mode", "times"))
                        .putString("times", sp.getString("times", ""))
                        .putInt("interval", sp.getInt("interval", 60)).apply();
                return;
            }
        }
    }

    // 공통 시각이 준비되지 않았으면 이유, 됐으면 null
    private String schedProblem() {
        return HomeService.schedProblem(this); // 텔레그램 설정 도우미(1070)와 같이 쓰려고 HomeService 로 옮김
    }

    // 공통 시각을 한 채널 설정에 복사. start 면 다음 송신 시각도 다시 계산
    // (지정 시각: 다음 시각에 / 일정 간격: 바로 1회 후 간격대로)
    private void copySched(SharedPreferences.Editor ed, boolean start) {
        HomeService.copySched(this, ed, start);
    }

    // 팝업 안내: 보내는 시각은 1040 공통 설정
    private void schedNote(LinearLayout form) {
        TextView tv = new TextView(this);
        String problem = schedProblem();
        tv.setText("보내는 시각: " + (problem == null ? HomeService.emailSchedule(sched()) : "아직 없음")
                + "\n((3) 위치 보내기 화면 맨 위에서 바꿉니다)");
        tv.setTextColor(problem == null ? Color.DKGRAY : 0xFFC62828);
        tv.setPadding(0, 24, 0, 0);
        form.addView(tv);
    }

    // 1040 [저장]: 공통 시각 저장 -> 모든 채널에 복사 (켜져 있는 채널은 다음 송신 시각 다시 계산)
    private void saveSched(ScheduleUi sch) {
        String tm = sch.timesText();
        if (sch.byTimes() && tm == null) {
            Toast.makeText(this, "시각은 08:00 처럼 입력하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (sch.byTimes() && tm.isEmpty()) {
            Toast.makeText(this, "보낼 시각을 하나 이상 등록하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        sched().edit().putString("mode", sch.byTimes() ? "times" : "interval")
                .putString("times", tm == null ? "" : tm)
                .putInt("interval", sch.minutes()).apply();
        for (String prefName : SCHED_JOBS) {
            SharedPreferences sp = getSharedPreferences(prefName, MODE_PRIVATE);
            SharedPreferences.Editor ed = sp.edit();
            copySched(ed, sp.getBoolean("on", false));
            ed.apply();
        }
        HomeService.refresh(this);
        refreshMsgButtons();
        Toast.makeText(this, "저장했습니다. (" + HomeService.emailSchedule(sched()) + ")", Toast.LENGTH_SHORT).show();
    }

    // msgBox 밖(본문)에 같은 모양의 버튼 하나
    private Button msgStyleButtonFor(LinearLayout body, String label, View.OnClickListener listener) {
        Button btn = msgStyleButton(listener);
        btn.setText(label);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.leftMargin = 30;
        lp.rightMargin = 30;
        lp.topMargin = 10;
        body.addView(btn, lp);
        return btn;
    }

    // 귀가(home) / 외출(away) 알림 켜기·끄기
    private void toggleEvent(String key, CheckBox cb) {
        SharedPreferences ev = getSharedPreferences(HomeService.PREF_EVENT, MODE_PRIVATE);
        if (cb.isChecked()) {
            if (loadRouters().isEmpty() && BtHome.registered(this).isEmpty()) {
                cb.setChecked(false);
                Toast.makeText(this, "홈 공유기 또는 홈 PC 블루투스를 먼저 등록하세요. ((1) 공유기 탭)",
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
        long time = ev.getLong("lastTime", 0);
        evLast.setText(time == 0 ? "아직 보낸 적이 없습니다."
                : "마지막: " + new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA).format(new Date(time))
                + "  " + ev.getString("lastResult", ""));
    }

    // ---------- 텔레그램 자동 송신 ----------

    private void updateTgButton() {
        SharedPreferences sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        boolean on = sp.getBoolean("on", false);
        HomeService.tgFollowSched(this); // 예전 자체 간격만 있으면 공통 시각으로 맞춤
        setJobText(tgBtn, "텔레그램 (자동)", on ? HomeService.emailSchedule(sp) : null); // 공통 보내는 시각
        tgBtn.setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
        syncCheck(TelegramSender.PREF, on);
    }

    private TextView label(LinearLayout form, String labelText) {
        TextView tv = new TextView(this);
        tv.setText(labelText);
        tv.setTextSize(14);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setPadding(0, 24, 0, 4);
        form.addView(tv);
        return tv;
    }

    // 설정 팝업: 봇 토큰, 받는 사람, 자동 송신 간격, 자동송신시작
    private void showTelegramDialog() {
        final SharedPreferences sp = getSharedPreferences(TelegramSender.PREF, MODE_PRIVATE);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(60, 10, 60, 10);

        // 처음 쓰는 사람용: 단계별 설정 도우미 (누르면 이 팝업을 닫고 도우미 화면으로)
        Button wizard = new Button(this);
        wizard.setText("처음이세요? 설정 도우미 (단계별 안내)");
        wizard.setTextSize(14);
        wizard.setTypeface(null, Typeface.BOLD);
        wizard.setTextColor(0xFF1976D2);
        form.addView(wizard, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        label(form, "봇 토큰");
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
        paste.setOnClickListener(view -> pasteToken(token));
        tokenRow.addView(paste);
        form.addView(tokenRow);

        label(form, "받는 사람 (채팅 ID · 여러 명은 쉼표로 구분)");
        final EditText chats = new EditText(this);
        chats.setSingleLine(true);
        chats.setInputType(InputType.TYPE_CLASS_TEXT);
        chats.setHint("예: 123456789, -100123456789");
        chats.setText(sp.getString("chats", ""));
        form.addView(chats);
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
            form.addView(nv);
        }
        Button find = new Button(this);
        find.setText("받는 사람 찾기 (봇에게 /start 한 사람·단톡방)");
        find.setTextSize(13);
        find.setOnClickListener(view -> findTelegramChats(token, chats));
        form.addView(find, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        Button shareBot = new Button(this);
        shareBot.setText("봇 링크 공유 (받는 사람에게 보내기)");
        shareBot.setTextSize(13);
        shareBot.setOnClickListener(view -> shareBotLink(token));
        form.addView(shareBot, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // 보내는 시각: 텔레그램도 1040 공통 설정을 씀 (예전 '자동 송신 간격' 칸은 없앰)
        schedNote(form);

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
        form.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(form);

        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("텔레그램 자동 송신")
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        wizard.setOnClickListener(view -> {
            dialog.dismiss();
            startActivity(new Intent(this, TelegramSetupActivity.class));
        });
        dialog.setOnShowListener(shown -> {
            // 저장: 입력값 확인 후 저장, 자동송신시작이 체크되어 있으면 실행
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String tk = token.getText().toString().trim();
                String ch = chats.getText().toString().trim();
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
                if (start && schedProblem() != null) {
                    Toast.makeText(this, schedProblem(), Toast.LENGTH_LONG).show();
                    return;
                }
                boolean changed = start && (!sp.getBoolean("on", false)
                        || !tk.equals(sp.getString("token", "")) || !ch.equals(sp.getString("chats", "")));
                SharedPreferences.Editor ed = sp.edit().putString("token", tk).putString("chats", ch)
                        .putBoolean("on", start);
                // 공통 보내는 시각 복사. 시작하거나 설정이 바뀌면 다음 송신 시각도 다시 계산
                copySched(ed, changed);
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateTgButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + HomeService.emailSchedule(sched()) + ")"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            });
            // 지금 테스트: 팝업은 그대로 두고 1회 전송
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
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
        dialog.show();
        ScreenId.tag(dialog, "1180");
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
                    ScreenId.tag(new AlertDialog.Builder(this).setTitle("봇 링크 공유 실패")
                            .setMessage(err[0]).setPositiveButton("확인", null).show(), "1190");
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
                    ScreenId.tag(new AlertDialog.Builder(this).setTitle("받는 사람 찾기 실패")
                            .setMessage(err[0]).setPositiveButton("확인", null).show(), "1200");
                    return;
                }
                if (found.isEmpty()) {
                    ScreenId.tag(new AlertDialog.Builder(this).setTitle("받는 사람 찾기")
                            .setMessage("찾은 사람이 없습니다.\n\n"
                                    + "1. 받는 사람이 텔레그램에서 봇을 찾아 [시작](/start)을 누르게 하세요.\n"
                                    + "   (단톡방: 봇을 초대한 뒤 단톡방에서 /start 입력)\n"
                                    + "2. 그다음 다시 [받는 사람 찾기]를 누르세요.\n\n"
                                    + "※ 최근 24시간 안에 봇에게 보낸 메시지만 보입니다. "
                                    + "PC 프로그램이 같은 봇으로 메시지를 받고 있으면 안 보일 수 있습니다.")
                            .setPositiveButton("확인", null).show(), "1210");
                    return;
                }
                // 이미 등록된 ID 는 체크된 상태로 표시
                Set<String> cur = new HashSet<>();
                for (String chat : chatsEdit.getText().toString().split("[,\\s]+")) {
                    if (!chat.isEmpty()) {
                        cur.add(chat);
                    }
                }
                final String[] labels = new String[found.size()];
                final boolean[] checked = new boolean[found.size()];
                for (int i = 0; i < found.size(); i++) {
                    labels[i] = found.get(i)[1] + "\nID: " + found.get(i)[0];
                    checked[i] = cur.contains(found.get(i)[0]);
                }
                ScreenId.tag(new AlertDialog.Builder(this)
                        .setTitle("받는 사람 선택")
                        .setMultiChoiceItems(labels, checked, (dlg, which, isChecked) -> checked[which] = isChecked)
                        .setPositiveButton("추가", (dlg, pos) -> {
                            // 기존 목록 순서 유지 + 새로 체크한 것 추가, 체크 해제한 것은 제거
                            java.util.LinkedHashSet<String> ids = new java.util.LinkedHashSet<>();
                            for (String chat : chatsEdit.getText().toString().split("[,\\s]+")) {
                                if (!chat.isEmpty()) {
                                    ids.add(chat);
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
                        .show(), "1220");
            });
        }).start();
    }

    // 지금 화면의 위치(없으면 마지막으로 알려진 위치)를 한 번 전송
    private void sendTelegramNow(final String tk, final String ch, final TextView out) {
        Location best = lastLoc;
        if (best == null && hasPermission()) {
            try {
                for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    Location cand = lm.getLastKnownLocation(provider);
                    if (cand != null && (best == null || cand.getTime() > best.getTime())) {
                        best = cand;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        final Location loc = best;
        out.setText("보내는 중...");
        new Thread(() -> {
            String err = TelegramSender.send(tk, ch, TelegramSender.message(this, loc));
            runOnUiThread(() -> {
                String result = err == null ? "성공" : "실패: " + err;
                out.setText("지금 테스트: " + result);
                Toast.makeText(this, err == null ? "텔레그램으로 보냈습니다." : result, Toast.LENGTH_LONG).show();
            });
        }).start();
    }

    // ---------- 카톡 본인 메시지 (수동) (간격마다 알림 -> 누르면 카톡 공유 화면) ----------

    private void updateKakaoButton() {
        SharedPreferences sp = getSharedPreferences(HomeService.PREF_KAKAO, MODE_PRIVATE);
        boolean on = sp.getBoolean("on", false);
        setJobText(kakaoBtn, "카톡 본인 메시지 (수동)",
                on ? HomeService.emailSchedule(sp) + " 알림" : null);
        kakaoBtn.setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
        syncCheck(HomeService.PREF_KAKAO, on);
    }

    private boolean kakaoInstalled() {
        return getPackageManager().getLaunchIntentForPackage(HomeService.KAKAO_PKG) != null;
    }

    // 설정 팝업: 보내기 간격, 자동보내기시작
    private void showKakaoDialog() {
        final SharedPreferences sp = getSharedPreferences(HomeService.PREF_KAKAO, MODE_PRIVATE);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(60, 10, 60, 10);

        TextView info = new TextView(this);
        info.setText("정해진 시각마다 위치를 확인해 알림을 띄웁니다.\n"
                + "알림을 누르면 위치가 채워진 카톡 화면이 열리고, "
                + "받는 사람을 골라 [전송]을 누르면 보내집니다.\n"
                + "(카톡은 완전 자동 전송을 지원하지 않습니다)");
        info.setTextColor(Color.DKGRAY);
        info.setPadding(0, 16, 0, 0);
        form.addView(info);

        // 보내는 시각은 1040 화면 맨 위 공통 설정을 씀
        schedNote(form);

        TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0);
        last.setText(lt == 0 ? "아직 알림을 보낸 적이 없습니다."
                : "마지막 알림: " + new SimpleDateFormat("MM-dd HH:mm:ss", Locale.KOREA).format(new Date(lt)));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        form.addView(last);
        if (!kakaoInstalled()) {
            last.setText("카카오톡이 설치되어 있지 않습니다.");
            last.setTextColor(0xFFC62828);
        }

        ScrollView sv = new ScrollView(this);
        sv.addView(form);

        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("카톡 본인 메시지 (수동)")
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        dialog.setOnShowListener(shown -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                boolean start = true; // 저장 = 1040 화면의 이 줄 자동 체크(켜기)
                if (start && !kakaoInstalled()) {
                    Toast.makeText(this, "카카오톡이 설치되어 있지 않습니다.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (schedProblem() != null) {
                    Toast.makeText(this, schedProblem(), Toast.LENGTH_LONG).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                SharedPreferences.Editor ed = sp.edit().putBoolean("on", start);
                copySched(ed, start); // 1040 공통 보내는 시각 + 다음 송신 시각
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateKakaoButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + HomeService.emailSchedule(sp) + " 알림)"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            });
            // 지금 테스트: 바로 카톡 공유 화면 열기
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
                if (!kakaoInstalled()) {
                    Toast.makeText(this, "카카오톡이 설치되어 있지 않습니다.", Toast.LENGTH_SHORT).show();
                    return;
                }
                last.setText("위치 메시지 만드는 중...");
                sendKakaoNow(dialog);
            });
        });
        dialog.show();
        ScreenId.tag(dialog, "1170");
    }

    // 지금 화면의 위치(없으면 마지막으로 알려진 위치)로 카톡 공유 화면 열기
    private void sendKakaoNow(final AlertDialog dialog) {
        final Location loc = currentOrLastLocation();
        new Thread(() -> {
            String msg = TelegramSender.message(this, loc);
            runOnUiThread(() -> {
                dialog.dismiss();
                try {
                    startActivity(HomeService.kakaoShare(msg));
                } catch (Exception e) {
                    Toast.makeText(this, "카카오톡을 열 수 없습니다.", Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    // ---------- 이메일 자동 송신 (정해진 시각/간격마다: 앱 비밀번호로 자동 발송, 또는 알림 -> Gmail 작성 화면) ----------

    // 일반 이메일 버튼 글자 (체크 표시, 시각). 카톡 이메일(1160)은 삭제
    private void updateEmailButton() {
        SharedPreferences sp = getSharedPreferences(HomeService.PREF_EMAIL, MODE_PRIVATE);
        boolean on = sp.getBoolean("on", false);
        setJobText(emailBtn, HomeService.emailTitle(HomeService.PREF_EMAIL), on ? HomeService.emailSchedule(sp) : null);
        emailBtn.setTextColor(on ? 0xFF2E7D32 : Color.BLACK);
        syncCheck(HomeService.PREF_EMAIL, on);
    }

    // 카톡 이메일 받는 주소: 카카오메일 도메인만 (kakao.com, daum.net, hanmail.net)
    private static boolean kakaoMailOnly(String emails) {
        for (String addr : emails.split("[,;\\s]+")) {
            String domain = addr.substring(addr.indexOf('@') + 1).toLowerCase(Locale.US);
            if (!addr.isEmpty() && !domain.equals("kakao.com") && !domain.equals("daum.net") && !domain.equals("hanmail.net")) {
                return false;
            }
        }
        return true;
    }

    // "8:5, 18:30, 08:05" -> 정렬/중복 제거한 "08:05, 18:30". 틀린 형식이 있으면 null
    private static String normalizeTimes(String raw) {
        java.util.TreeSet<String> set = new java.util.TreeSet<>();
        for (String part : raw.split("[,\\s]+")) {
            if (part.isEmpty()) {
                continue;
            }
            int[] hm = HomeService.parseTime(part);
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
        // 본인 발신 이메일(사용자 등록 1075)이 없으면 경고 팝업(화면 1145)만 띄우고 끝
        SharedPreferences user = getSharedPreferences(UserActivity.PREF_USER, MODE_PRIVATE);
        if (noUserMail("일반 이메일을 보내려면")) {
            return;
        }
        // 보내는 계정 = 사용자 등록(1075)의 Gmail·앱 비밀번호 (이 팝업에서는 입력하지 않음)
        final String fr = user.getString("mail", "").trim();
        final String pw = user.getString("mailPass", "").replace(" ", "");
        final SharedPreferences sp = getSharedPreferences(pref, MODE_PRIVATE);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(60, 10, 60, 10);

        // 받는 사람: 최대 5명, 한 줄에 한 명. 저장할 때 "a, b, c" 한 줄로 묶음
        label(form, "받는 사람 이메일 (최대 5명)");
        final EditText[] tos = new EditText[5];
        String[] saved = sp.getString("to", "").split("[,;\\s]+");
        int slot = 0;
        for (int i = 0; i < tos.length; i++) {
            tos[i] = new EditText(this);
            tos[i].setSingleLine(true);
            tos[i].setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
            tos[i].setHint((i + 1) + ") 예: family@gmail.com");
            while (slot < saved.length && saved[slot].trim().isEmpty()) {
                slot++;
            }
            if (slot < saved.length) {
                tos[i].setText(saved[slot++].trim());
            }
            form.addView(tos[i]);
        }

        // 보내는 시각은 1040 화면 맨 위 공통 설정을 씀
        schedNote(form);

        final TextView last = new TextView(this);
        long lt = sp.getLong("lastTime", 0), nx = sp.getLong("next", 0);
        SimpleDateFormat fmt = new SimpleDateFormat("MM-dd HH:mm", Locale.KOREA);
        last.setText((lt == 0 ? "아직 보낸 적이 없습니다."
                : "마지막 발송: " + fmt.format(new Date(lt)) + "  " + sp.getString("lastResult", ""))
                + (sp.getBoolean("on", false) && nx > 0 ? "\n다음 송신: " + fmt.format(new Date(nx)) : ""));
        last.setTextColor(Color.GRAY);
        last.setPadding(0, 16, 0, 0);
        form.addView(last);

        ScrollView sv = new ScrollView(this);
        sv.addView(form);

        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(HomeService.emailTitle(pref))
                .setView(sv)
                .setPositiveButton("저장", null)
                .setNeutralButton("지금 테스트", null)
                .setNegativeButton("취소", null)
                .create();
        dialog.setOnShowListener(shown -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String addr = joinEmails(tos);
                boolean start = true; // 저장 = 1040 화면의 이 줄 자동 체크(켜기)
                if (!addr.isEmpty() && !validEmails(addr)) {
                    Toast.makeText(this, "이메일 주소 형식을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (start && addr.isEmpty()) {
                    Toast.makeText(this, "받는 사람 이메일을 입력하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (schedProblem() != null) {
                    Toast.makeText(this, schedProblem(), Toast.LENGTH_LONG).show();
                    return;
                }
                if (start && !hasPermission()) {
                    ensurePermission();
                    Toast.makeText(this, "위치 권한을 허용한 뒤 다시 저장하세요.", Toast.LENGTH_LONG).show();
                    return;
                }
                SharedPreferences.Editor ed = sp.edit().putString("to", addr)
                        .putString("send", "smtp")
                        .putString("from", fr).putString("pass", pw)
                        .putBoolean("on", start);
                copySched(ed, start); // 1040 공통 보내는 시각 + 다음 송신 시각
                ed.apply();
                if (start && Build.VERSION.SDK_INT >= 33 && checkSelfPermission(
                        Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTI);
                }
                HomeService.refresh(this);
                updateEmailButton();
                Toast.makeText(this, start
                        ? "저장했습니다. (켜져 있음 · " + HomeService.emailSchedule(sp) + ")"
                        : "저장했습니다.", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            });
            // 지금 테스트: 자동 발송이면 바로 전송(팝업 유지), 아니면 Gmail 작성 화면 열기
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
                final String addr = joinEmails(tos);
                if (addr.isEmpty() || !validEmails(addr)) {
                    Toast.makeText(this, "받는 사람 이메일을 확인하세요.", Toast.LENGTH_SHORT).show();
                    return;
                }
                sp.edit().putString("to", addr).putString("from", fr).putString("pass", pw).apply();
                final Location loc = currentOrLastLocation();
                last.setText("보내는 중...");
                new Thread(() -> {
                    String err = EmailSender.send(fr, pw, addr, HomeService.emailSubject(this),
                            TelegramSender.message(this, loc));
                    runOnUiThread(() -> {
                        String result = err == null ? "성공" : "실패: " + err;
                        last.setText("지금 테스트: " + result);
                        Toast.makeText(this, err == null ? "메일을 보냈습니다." : result, Toast.LENGTH_LONG).show();
                    });
                }).start();
            });
        });
        dialog.show();
        ScreenId.tag(dialog, "1150");
    }

    // 받는 사람 칸들(최대 5개) -> "a@x.com, b@y.com" 한 줄 (빈칸은 건너뜀)
    private static String joinEmails(EditText[] tos) {
        StringBuilder sb = new StringBuilder();
        for (EditText edit : tos) {
            String addr = edit.getText().toString().trim();
            if (!addr.isEmpty()) {
                sb.append(sb.length() > 0 ? ", " : "").append(addr);
            }
        }
        return sb.toString();
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

    private static boolean validEmails(String emails) {
        for (String addr : emails.split("[,;\\s]+")) {
            if (!addr.isEmpty() && !Patterns.EMAIL_ADDRESS.matcher(addr).matches()) {
                return false;
            }
        }
        return true;
    }

    private Location currentOrLastLocation() {
        Location best = lastLoc;
        if (best == null && hasPermission()) {
            try {
                for (String provider : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                    Location cand = lm.getLastKnownLocation(provider);
                    if (cand != null && (best == null || cand.getTime() > best.getTime())) {
                        best = cand;
                    }
                }
            } catch (SecurityException ignored) {
            }
        }
        return best;
    }

    private static String now() {
        return new SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(new Date());
    }
}