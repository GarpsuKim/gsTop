package com.example.clock.chime

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import java.time.ZonedDateTime
import java.time.temporal.ChronoUnit

/**
 * 다음 정각에 [ChimeAlarmReceiver] 가 깨어나도록 정확한 알람을 맞춤.
 * 앱이 꺼져 있어도(위젯만 있어도) 시스템 알람이 앱을 깨워서 연주함.
 * 알람은 한 번에 한 시간씩: 울릴 때마다 다음 정각을 다시 맞춤.
 */
object ChimeScheduler {
    const val ACTION_CHIME = "com.example.clock.action.CHIME"
    const val EXTRA_TARGET_MS = "target_ms"

    /** 시보 설정에 맞춰 알람을 맞추거나 취소. 여러 번 불러도 알람은 하나만 유지됨. */
    fun update(context: Context) {
        val s = ChimeStore.load(context)
        val next = if (s.enabled && s.hasFile) nextChimeTime(s) else null
        if (next != null) schedule(context, next) else cancel(context)
    }

    /**
     * 다음 시보 시각: 매시 [ChimeSettings.minute] 분 중 지금 이후로 가장 가까운 때. 건너뛸 시각은 넘김.
     * (예: 지금 3:20, 15분 → 4:15. 4시를 건너뛰면 5:15) 모든 시각을 건너뛰면 null.
     */
    fun nextChimeTime(s: ChimeSettings): ZonedDateTime? {
        // 1초 여유: 알람이 아주 조금 일찍 깨어나도 같은 시각으로 다시 맞춰 두 번 울리는 일이 없게
        val now = ZonedDateTime.now().plusSeconds(1)
        val thisHour = now.truncatedTo(ChronoUnit.HOURS).plusMinutes(s.minute.coerceIn(0, 59).toLong())
        var t = if (thisHour.isAfter(now)) thisHour else thisHour.plusHours(1)
        repeat(25) {
            if (!s.isSkipped(t.hour)) return t
            t = t.plusHours(1)
        }
        return null
    }

    private fun schedule(context: Context, next: ZonedDateTime) {
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        val target = next.toInstant().toEpochMilli()
        val pi = pendingIntent(context, target)
        val canExact = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || am.canScheduleExactAlarms()
        if (canExact) {
            // 절전(Doze) 중에도 정확히 그 시각에 깨움
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, target, pi)
        } else {
            // 정확한 알람 권한이 없으면 (사용자가 끈 경우) 1분 안쪽 오차로
            am.setWindow(AlarmManager.RTC_WAKEUP, target, 60_000L, pi)
        }
    }

    private fun cancel(context: Context) {
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        am.cancel(pendingIntent(context, 0L))
    }

    /** 같은 requestCode·Intent 라서 새로 맞추면 이전 알람을 대신함 (FLAG_UPDATE_CURRENT). */
    private fun pendingIntent(context: Context, targetMs: Long): PendingIntent =
        PendingIntent.getBroadcast(
            context, 0,
            Intent(context, ChimeAlarmReceiver::class.java)
                .setAction(ACTION_CHIME)
                .putExtra(EXTRA_TARGET_MS, targetMs),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
}
