package com.garpsu.wifiscan;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothSocket;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * 홈 PC 블루투스로 '집' 판정 (폰과 PC를 미리 페어링해 두는 방식, PC 쪽 프로그램 수정 없음).
 * 페어링된 PC에 블루투스 접속을 시도해서, 블루투스 연결(ACL)까지 되면 PC가 가까이 있는 것으로 본다.
 * (그 뒤 서비스 접속이 실패해도 상관없음. PC가 멀리 있으면 연결 자체가 안 됨)
 */
final class BtHome {

    static final long FRESH = 3 * 60 * 1000; // 이 시간 안에 PC를 찾았으면 '집'
    private static final UUID SPP = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private BtHome() {
    }

    // ---------- 주변 블루투스 검색 (보호자 회신에 넣는 식별 정보) ----------

    static final int REQ_SCAN = 7; // 근처 기기(블루투스 검색) 권한 요청 번호

    // 검색할 수 없으면 이유, 할 수 있으면 null
    static String scanProblem(Context ctx) {
        BluetoothAdapter ad = adapter(ctx);
        if (ad == null) {
            return "이 폰에는 블루투스가 없습니다";
        }
        if (!ad.isEnabled()) {
            return "블루투스 꺼짐";
        }
        if (Build.VERSION.SDK_INT >= 31) {
            if (ctx.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                return "근처 기기 권한이 없어 검색 못 함 (앱 설정에서 허용)";
            }
        } else if (ctx.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            return "위치 권한이 없어 검색 못 함";
        }
        return null;
    }

    // 근처 기기 권한이 없으면 요청 (Android 12 이상). 위치 권한 요청과 겹치지 않게 부르는 쪽에서 순서를 맞춤
    static void requestScan(android.app.Activity activity) {
        if (Build.VERSION.SDK_INT >= 31
                && activity.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT}, REQ_SCAN);
        }
    }

    /**
     * 주변 블루투스(BLE) 기기를 millis 동안 검색해서 {MAC, 이름, 신호 dBm} 목록 (신호 센 순).
     * 검색할 수 없으면 null. 오래 걸림 -> 반드시 작업 스레드에서 호출.
     */
    @SuppressWarnings("MissingPermission")
    static List<String[]> scanNearby(Context ctx, long millis) {
        if (scanProblem(ctx) != null) {
            return null;
        }
        final java.util.Map<String, String[]> found = new java.util.HashMap<>();
        android.bluetooth.le.BluetoothLeScanner scanner;
        android.bluetooth.le.ScanCallback callback = new android.bluetooth.le.ScanCallback() {
            @Override
            public void onScanResult(int callbackType, android.bluetooth.le.ScanResult result) {
                add(result);
            }

            @Override
            public void onBatchScanResults(List<android.bluetooth.le.ScanResult> results) {
                for (android.bluetooth.le.ScanResult result : results) {
                    add(result);
                }
            }

            private void add(android.bluetooth.le.ScanResult result) {
                if (result == null || result.getDevice() == null) {
                    return;
                }
                String mac = result.getDevice().getAddress().toUpperCase(Locale.US);
                String name = null;
                if (result.getScanRecord() != null) {
                    name = result.getScanRecord().getDeviceName();
                }
                if (name == null) {
                    try {
                        name = result.getDevice().getName();
                    } catch (SecurityException ignored) {
                    }
                }
                synchronized (found) { // 같은 기기가 여러 번 잡히면: 가장 센 신호, 알게 된 이름 유지
                    String[] old = found.get(mac);
                    int rssi = old == null ? result.getRssi() : Math.max(result.getRssi(), Integer.parseInt(old[2]));
                    String label = name != null ? name : (old == null ? "" : old[1]);
                    found.put(mac, new String[]{mac, label, String.valueOf(rssi)});
                }
            }
        };
        try {
            scanner = adapter(ctx).getBluetoothLeScanner();
            if (scanner == null) {
                return null;
            }
            android.bluetooth.le.ScanSettings settings = new android.bluetooth.le.ScanSettings.Builder()
                    .setScanMode(android.bluetooth.le.ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            scanner.startScan(null, settings, callback);
        } catch (Exception e) {
            return null;
        }
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ignored) {
        }
        try {
            scanner.stopScan(callback);
        } catch (Exception ignored) {
        }
        List<String[]> list;
        synchronized (found) {
            list = new ArrayList<>(found.values());
        }
        java.util.Collections.sort(list, (left, right) ->
                Integer.parseInt(right[2]) - Integer.parseInt(left[2]));
        return list;
    }

    // 등록 상태: 홈 / 기타 / 등록 안 됨
    static String kindOf(Context ctx, String mac) {
        for (String entry : registered(ctx)) {
            if (entry.split("\\|", 2)[0].equalsIgnoreCase(mac)) {
                return isHome(ctx, mac) ? "홈" : "기타";
            }
        }
        return "등록 안 됨";
    }

    // Android 12 이상은 '근처 기기' 권한 필요
    static boolean hasPerm(Context ctx) {
        return Build.VERSION.SDK_INT < 31
                || ctx.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    static BluetoothAdapter adapter(Context ctx) {
        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        return bm == null ? null : bm.getAdapter();
    }

    static boolean enabled(Context ctx) {
        BluetoothAdapter ad = adapter(ctx);
        return ad != null && ad.isEnabled();
    }

    // 등록한 홈 PC 목록: "MAC|이름"
    static Set<String> registered(Context ctx) {
        return new HashSet<>(ctx.getSharedPreferences("home", Context.MODE_PRIVATE)
                .getStringSet("bt", new HashSet<String>()));
    }

    static void save(Context ctx, Set<String> set) {
        ctx.getSharedPreferences("home", Context.MODE_PRIVATE).edit().putStringSet("bt", set).apply();
    }

    // 기기 종류: "home" 설정의 "btkind_<MAC 대문자>" = "home"(홈) / "other"(기타). 값이 없으면(기존 기기) 기타
    // 기타 기기(회사 PC, 차 등)는 가까이 있어도 귀가로 보지 않음
    static boolean isHome(Context ctx, String mac) {
        return mac != null && !mac.isEmpty() && "home".equals(ctx.getSharedPreferences("home", Context.MODE_PRIVATE)
                .getString("btkind_" + mac.toUpperCase(Locale.US), "other"));
    }

    static void setHome(Context ctx, String mac, boolean home) {
        ctx.getSharedPreferences("home", Context.MODE_PRIVATE).edit()
                .putString("btkind_" + mac.toUpperCase(Locale.US), home ? "home" : "other").apply();
    }

    // 기기 별명 (1010 에서 입력): "home" 설정의 "btalias_<MAC 대문자>"
    static String alias(Context ctx, String mac) {
        if (mac == null || mac.isEmpty()) {
            return "";
        }
        return ctx.getSharedPreferences("home", Context.MODE_PRIVATE)
                .getString("btalias_" + mac.toUpperCase(Locale.US), "");
    }

    static void setAlias(Context ctx, String mac, String alias) {
        ctx.getSharedPreferences("home", Context.MODE_PRIVATE).edit()
                .putString("btalias_" + mac.toUpperCase(Locale.US), alias.trim()).apply();
    }

    // 별명이 있으면 별명, 없으면 페어링 이름
    static String label(Context ctx, String mac, String name) {
        String nick = alias(ctx, mac);
        return nick.isEmpty() ? name : nick;
    }

    // 등록한 기기 중 '홈'으로 체크된 것이 있나
    static boolean hasHome(Context ctx) {
        for (String entry : registered(ctx)) {
            if (isHome(ctx, entry.split("\\|", 2)[0])) {
                return true;
            }
        }
        return false;
    }

    // 최근(FRESH 안)에 등록한 기기(홈·기타 모두)를 찾았는지
    static boolean seenRecently(Context ctx) {
        SharedPreferences sp = ctx.getSharedPreferences("home", Context.MODE_PRIVATE);
        return !registered(ctx).isEmpty()
                && System.currentTimeMillis() - sp.getLong("btSeenAt", 0) < FRESH;
    }

    // 최근에 찾은 기기의 MAC / 이름
    static String seenMac(Context ctx) {
        return ctx.getSharedPreferences("home", Context.MODE_PRIVATE).getString("btSeenMac", "");
    }

    static String seenName(Context ctx) {
        return ctx.getSharedPreferences("home", Context.MODE_PRIVATE).getString("btSeenName", "");
    }

    // 최근에 '홈' 기기를 찾음 = 집
    static boolean seenHome(Context ctx) {
        return seenRecently(ctx) && isHome(ctx, seenMac(ctx));
    }

    // 최근에 '기타' 기기를 찾음 (집 아님, 이름만 표시)
    static boolean seenOther(Context ctx) {
        return seenRecently(ctx) && !isHome(ctx, seenMac(ctx));
    }

    static void markSeen(Context ctx, String mac, String name) {
        ctx.getSharedPreferences("home", Context.MODE_PRIVATE).edit()
                .putLong("btSeenAt", System.currentTimeMillis())
                .putString("btSeenMac", mac).putString("btSeenName", name).apply();
    }

    // 페어링된 기기 목록 {MAC, 이름}
    @SuppressWarnings("MissingPermission")
    static List<String[]> bonded(Context ctx) {
        List<String[]> list = new ArrayList<>();
        BluetoothAdapter ad = adapter(ctx);
        if (ad == null || !hasPerm(ctx)) {
            return list;
        }
        try {
            for (BluetoothDevice device : ad.getBondedDevices()) {
                String name = device.getName();
                list.add(new String[]{device.getAddress().toUpperCase(Locale.US), name == null ? "(이름 없음)" : name});
            }
        } catch (SecurityException ignored) {
        }
        return list;
    }

    /**
     * 등록한 기기 중 하나라도 가까이 있으면 {MAC, 이름}, 없으면 null. '홈' 기기를 먼저 찾아봄.
     * 오래 걸릴 수 있음(기기당 최대 몇 초) -> 반드시 작업 스레드에서 호출.
     */
    static String[] findAny(Context ctx) {
        List<String> order = new ArrayList<>();
        for (String entry : registered(ctx)) {
            if (isHome(ctx, entry.split("\\|", 2)[0])) {
                order.add(0, entry);
            } else {
                order.add(entry);
            }
        }
        for (String entry : order) {
            String[] parts = entry.split("\\|", 2);
            if (probe(ctx, parts[0])) {
                return new String[]{parts[0], parts.length > 1 ? parts[1] : parts[0]};
            }
        }
        return null;
    }

    @SuppressWarnings("MissingPermission")
    static boolean probe(Context context, final String mac) {
        BluetoothAdapter ad = adapter(context);
        if (ad == null || !ad.isEnabled() || !hasPerm(context)) {
            return false;
        }
        BluetoothDevice device;
        try {
            device = ad.getRemoteDevice(mac);
        } catch (Exception e) {
            return false;
        }
        // 1) 이미 블루투스로 연결되어 있으면 바로 '있음' (숨은 기능이라 안 되는 폰도 있음)
        try {
            Object connected = device.getClass().getMethod("isConnected").invoke(device);
            if (Boolean.TRUE.equals(connected)) {
                return true;
            }
        } catch (Exception ignored) {
        }
        // 2) 접속 시도 -> 블루투스 연결(ACL)이 생기면 가까이 있는 것
        final CountDownLatch acl = new CountDownLatch(1);
        BroadcastReceiver rcv = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ctx, Intent intent) {
                BluetoothDevice dev = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (dev != null && mac.equalsIgnoreCase(dev.getAddress())) {
                    acl.countDown();
                }
            }
        };
        IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_ACL_CONNECTED);
        Context app = context.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(rcv, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(rcv, filter);
        }
        BluetoothSocket sock = null;
        boolean found = false;
        try {
            try {
                ad.cancelDiscovery();
            } catch (Exception ignored) {
            }
            sock = device.createInsecureRfcommSocketToServiceRecord(SPP);
            sock.connect(); // 성공하면 당연히 있음, 실패해도 아래 ACL 로 판단
            found = true;
        } catch (Exception ignored) {
        } finally {
            if (sock != null) {
                try {
                    sock.close();
                } catch (Exception ignored) {
                }
            }
        }
        if (!found) {
            try {
                found = acl.await(1500, TimeUnit.MILLISECONDS);
            } catch (InterruptedException ignored) {
            }
        }
        try {
            app.unregisterReceiver(rcv);
        } catch (Exception ignored) {
        }
        return found;
    }
}
