package com.example.clock.chime

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import java.time.Instant
import java.time.ZoneId

/** 정각 알람 수신: 시보 연주를 시작하고 다음 정각 알람을 맞춤. (외부 앱은 보낼 수 없음: exported=false) */
class ChimeAlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != ChimeScheduler.ACTION_CHIME) return
        // 다음 정각부터 먼저 맞춤 (연주 시작이 실패해도 시보가 끊기지 않게)
        ChimeScheduler.update(context)

        val s = ChimeStore.load(context)
        if (!s.enabled || !s.hasFile) return
        // 폰이 꺼져 있었거나 해서 정각보다 많이 늦게 깨어난 경우에는 연주하지 않음 (예: 3시 37분에 울리지 않게)
        val target = intent.getLongExtra(ChimeScheduler.EXTRA_TARGET_MS, 0L)
        if (target > 0L && System.currentTimeMillis() - target > LATE_LIMIT_MS) return
        // 건너뛸 시각이면 울리지 않음 (알람을 맞춘 뒤에 설정을 바꾼 경우 대비. 판단은 원래 울릴 시각 기준)
        val hour = Instant.ofEpochMilli(if (target > 0L) target else System.currentTimeMillis())
            .atZone(ZoneId.systemDefault()).hour
        if (s.isSkipped(hour)) return

        ChimeService.play(context)
    }

    private companion object {
        const val LATE_LIMIT_MS = 2 * 60_000L
    }
}

/**
 * 재부팅·시각 변경·시간대 변경·앱 업데이트 때 정각 알람을 다시 맞춤.
 * (재부팅하면 알람이 모두 지워지고, 시간대가 바뀌면 "정각" 이 달라질 수 있으므로)
 * 이 방송들은 시스템만 보낼 수 있는 보호된 방송이라 exported=true 여도 안전.
 */
class ChimeRescheduleReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_TIME_CHANGED,
            Intent.ACTION_TIMEZONE_CHANGED,
            Intent.ACTION_MY_PACKAGE_REPLACED,
            -> {
                ChimeScheduler.update(context)
                MedScheduler.update(context) // 약 먹을 시간 확인 알람
            }
        }
    }
}
