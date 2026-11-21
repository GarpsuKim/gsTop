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

    // Android 12 이상은 '근처 기기' 권한 필요
    static boolean hasPerm(Context c) {
        return Build.VERSION.SDK_INT < 31
                || c.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    static BluetoothAdapter adapter(Context c) {
        BluetoothManager bm = (BluetoothManager) c.getSystemService(Context.BLUETOOTH_SERVICE);
        return bm == null ? null : bm.getAdapter();
    }

    static boolean enabled(Context c) {
        BluetoothAdapter ad = adapter(c);
        return ad != null && ad.isEnabled();
    }

    // 등록한 홈 PC 목록: "MAC|이름"
    static Set<String> registered(Context c) {
        return new HashSet<>(c.getSharedPreferences("home", Context.MODE_PRIVATE)
                .getStringSet("bt", new HashSet<String>()));
    }

    static void save(Context c, Set<String> set) {
        c.getSharedPreferences("home", Context.MODE_PRIVATE).edit().putStringSet("bt", set).apply();
    }

    // 최근(FRESH 안)에 홈 PC를 찾았는지
    static boolean seenRecently(Context c) {
        SharedPreferences sp = c.getSharedPreferences("home", Context.MODE_PRIVATE);
        return !registered(c).isEmpty()
                && System.currentTimeMillis() - sp.getLong("btSeenAt", 0) < FRESH;
    }

    static void markSeen(Context c, String name) {
        c.getSharedPreferences("home", Context.MODE_PRIVATE).edit()
                .putLong("btSeenAt", System.currentTimeMillis()).putString("btSeenName", name).apply();
    }

    // 페어링된 기기 목록 {MAC, 이름}
    @SuppressWarnings("MissingPermission")
    static List<String[]> bonded(Context c) {
        List<String[]> list = new ArrayList<>();
        BluetoothAdapter ad = adapter(c);
        if (ad == null || !hasPerm(c)) {
            return list;
        }
        try {
            for (BluetoothDevice d : ad.getBondedDevices()) {
                String name = d.getName();
                list.add(new String[]{d.getAddress().toUpperCase(Locale.US), name == null ? "(이름 없음)" : name});
            }
        } catch (SecurityException ignored) {
        }
        return list;
    }

    /**
     * 등록한 PC 중 하나라도 가까이 있으면 그 이름, 없으면 null.
     * 오래 걸릴 수 있음(PC당 최대 몇 초) -> 반드시 작업 스레드에서 호출.
     */
    static String findAny(Context c) {
        for (String s : registered(c)) {
            String[] t = s.split("\\|", 2);
            if (probe(c, t[0])) {
                return t.length > 1 ? t[1] : t[0];
            }
        }
        return null;
    }

    @SuppressWarnings("MissingPermission")
    static boolean probe(Context c, final String mac) {
        BluetoothAdapter ad = adapter(c);
        if (ad == null || !ad.isEnabled() || !hasPerm(c)) {
            return false;
        }
        BluetoothDevice d;
        try {
            d = ad.getRemoteDevice(mac);
        } catch (Exception e) {
            return false;
        }
        // 1) 이미 블루투스로 연결되어 있으면 바로 '있음' (숨은 기능이라 안 되는 폰도 있음)
        try {
            Object r = d.getClass().getMethod("isConnected").invoke(d);
            if (Boolean.TRUE.equals(r)) {
                return true;
            }
        } catch (Exception ignored) {
        }
        // 2) 접속 시도 -> 블루투스 연결(ACL)이 생기면 가까이 있는 것
        final CountDownLatch acl = new CountDownLatch(1);
        BroadcastReceiver rcv = new BroadcastReceiver() {
            @Override
            public void onReceive(Context ctx, Intent i) {
                BluetoothDevice x = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (x != null && mac.equalsIgnoreCase(x.getAddress())) {
                    acl.countDown();
                }
            }
        };
        IntentFilter f = new IntentFilter(BluetoothDevice.ACTION_ACL_CONNECTED);
        Context app = c.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(rcv, f, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(rcv, f);
        }
        BluetoothSocket s = null;
        boolean found = false;
        try {
            try {
                ad.cancelDiscovery();
            } catch (Exception ignored) {
            }
            s = d.createInsecureRfcommSocketToServiceRecord(SPP);
            s.connect(); // 성공하면 당연히 있음, 실패해도 아래 ACL 로 판단
            found = true;
        } catch (Exception ignored) {
        } finally {
            if (s != null) {
                try {
                    s.close();
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
