package com.garpsu.wifiscan;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** 폰 재부팅 / 앱 업데이트 후 백그라운드 작업(귀가 감지, 텔레그램 자동 송신)을 다시 시작 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context c, Intent i) {
        String a = i.getAction();
        if ((Intent.ACTION_BOOT_COMPLETED.equals(a) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(a))
                && HomeService.anyOn(c)) {
            try {
                HomeService.refresh(c);
            } catch (Exception ignored) {
            }
        }
    }
}
