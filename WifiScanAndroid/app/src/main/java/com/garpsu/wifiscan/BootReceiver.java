package com.garpsu.wifiscan;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** 폰 재부팅 / 앱 업데이트 후 백그라운드 작업(귀가 감지, 텔레그램 자동 송신)을 다시 시작 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        String action = intent.getAction();
        AppLog.add(ctx, "앱", (Intent.ACTION_BOOT_COMPLETED.equals(action) ? "폰 재부팅됨" : "앱 업데이트됨")
                + " -> 백그라운드 " + (HomeService.anyOn(ctx) ? "다시 시작" : "꺼져 있음"));
        if ((Intent.ACTION_BOOT_COMPLETED.equals(action) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(action))
                && HomeService.anyOn(ctx)) {
            try {
                HomeService.refresh(ctx);
            } catch (Exception ignored) {
            }
        }
    }
}
