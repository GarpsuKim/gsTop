package com.example.clock.chime

import android.app.AlarmManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.RingtoneManager
import android.os.Build
import androidx.annotation.StringRes
import androidx.core.app.NotificationCompat
import com.example.clock.MainActivity
import com.example.clock.R
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime

/**
 * 약 먹는 시간 관리 ([시보·알람] → 약 먹을 시간).
 * 하루 세·네 번(아침·점심·저녁·심야) 정한 시각에 약을 먹고 [실행] 을 체크. 정한 시각에 "약 먹을 시간이에요" 알림,
 * 30분이 지나도 체크하지 않으면 "30분 지났어요" 알림 (소리 + 팝업, [먹었어요] 버튼). 앱이 꺼져 있어도 시스템 알람이 깨워서 알림.
 */
enum class MedSlot(@StringRes val label: Int, val defaultMinutes: Int, val defaultOn: Boolean) {
    Morning(R.string.med_morning, 7 * 60 + 30, true),
    Lunch(R.string.med_lunch, 12 * 60 + 30, true),
    Evening(R.string.med_evening, 18 * 60, true),
    /** 하루 4번 먹는 경우 (기본 꺼짐). */
    LateNight(R.string.med_late_night, 23 * 60, false),
}

/** 약 복용 기간 기본값 (일분). */
const val MED_DEFAULT_DAYS = 7

/** 한 회차 = 날짜(epochDay) + 아침/점심/저녁/심야. 저장용 글자: "20419:2". */
fun doseKey(day: Long, slot: MedSlot) = "$day:${slot.ordinal}"

data class MedSchedule(
    /** 한 번이라도 [저장] 했는지 (저장 전에는 알림 없음). */
    val active: Boolean = false,
    /** 1회차 날짜 ([초기화] 하면 오늘). */
    val startDay: Long = LocalDate.now().toEpochDay(),
    /** 약 복용 기간 (며칠분). 계획은 이보다 하루 더 많게 만듦 (1회차 날부터 days + 1 일). */
    val days: Int = MED_DEFAULT_DAYS,
    /** 아침·점심·저녁·심야 시각 (자정부터 분). */
    val minutes: List<Int> = MedSlot.entries.map { it.defaultMinutes },
    /** 아침·점심·저녁·심야 중 먹는 때 (끈 때는 목록에서 계획 없음). */
    val slotOn: List<Boolean> = MedSlot.entries.map { it.defaultOn },
    /** 계획에서 뺀 회차 (그 회차만 [계획] 을 눌러 끈 것). */
    val skipped: Set<String> = emptySet(),
    /** 먹은 회차 → 체크한 시각 (밀리초). */
    val taken: Map<String, Long> = emptyMap(),
) {
    /** 계획된 회차인지: 복용 기간 안 + 그때 먹음 + 그 회차만 건너뛰지 않음. (기간 밖은 알림도 없음) */
    fun isPlanned(day: Long, slot: MedSlot) =
        day in startDay..lastDay() && slotOn[slot.ordinal] && doseKey(day, slot) !in skipped
    fun isTaken(day: Long, slot: MedSlot) = doseKey(day, slot) in taken

    /** 그 회차의 계획 시각. */
    fun doseTime(day: Long, slot: MedSlot): ZonedDateTime =
        LocalDate.ofEpochDay(day).atStartOfDay(ZoneId.systemDefault()).plusMinutes(minutes[slot.ordinal].toLong())

    /** 계획의 마지막 날: 복용 기간보다 하루 더 (예: 7일분 → 1회차 날부터 8일째까지). */
    fun lastDay(): Long = startDay + days
}

/** 약 일정 저장 (SharedPreferences "medicine"). */
object MedStore {
    private const val PREFS = "medicine"

    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(context: Context): MedSchedule {
        val p = prefs(context)
        val d = MedSchedule()
        return MedSchedule(
            active = p.getBoolean("active", false),
            startDay = p.getLong("start_day", d.startDay),
            days = p.getInt("days", MED_DEFAULT_DAYS),
            minutes = MedSlot.entries.map { p.getInt("min_${it.ordinal}", it.defaultMinutes) },
            slotOn = MedSlot.entries.map { p.getBoolean("on_${it.ordinal}", it.defaultOn) },
            skipped = p.getStringSet("skipped", emptySet())!!.toSet(),
            taken = p.getStringSet("taken", emptySet())!!.mapNotNull { e ->
                val i = e.indexOf('=')
                if (i <= 0) null else e.substring(0, i) to (e.substring(i + 1).toLongOrNull() ?: return@mapNotNull null)
            }.toMap(),
        )
    }

    /** 저장 (1회차보다 앞선 기록은 지움). */
    fun save(context: Context, s: MedSchedule) {
        val keep = { key: String -> (key.substringBefore(':').toLongOrNull() ?: 0L) >= s.startDay }
        prefs(context).edit().apply {
            putBoolean("active", s.active)
            putLong("start_day", s.startDay)
            putInt("days", s.days)
            MedSlot.entries.forEach {
                putInt("min_${it.ordinal}", s.minutes[it.ordinal])
                putBoolean("on_${it.ordinal}", s.slotOn[it.ordinal])
            }
            putStringSet("skipped", s.skipped.filter(keep).toSet())
            putStringSet("taken", s.taken.filterKeys(keep).map { (k, v) -> "$k=$v" }.toSet())
            putStringSet("alerted", alerted(context).filter(keep).toSet())
        }.apply()
    }

    /** 이미 띄운 알림 (정한 시각 = "회차@0", 30분 지남 = "회차"). 같은 알림이 두 번 울리지 않게. */
    fun alerted(context: Context): Set<String> = prefs(context).getStringSet("alerted", emptySet())!!.toSet()

    fun markAlerted(context: Context, key: String) {
        prefs(context).edit().putStringSet("alerted", alerted(context) + key).apply()
    }

    /** 알림의 [먹었어요] 로 체크. */
    fun markTaken(context: Context, key: String, atMs: Long) {
        val s = load(context)
        if (key !in s.taken) save(context, s.copy(taken = s.taken + (key to atMs)))
    }
}

/** 다음 알림 시각(정한 시각 또는 30분 지남)에 [MedReceiver] 가 깨어나도록 정확한 알람을 맞춤 (시보 알람과 따로). */
object MedScheduler {
    const val ACTION_CHECK = "com.example.clock.action.MED_CHECK"
    const val ACTION_TAKEN = "com.example.clock.action.MED_TAKEN"
    const val EXTRA_KEY = "dose_key"
    /** MainActivity 를 열 때 이 값이 true 면 약 먹는 시간 창을 바로 엶 (알림을 누른 경우). */
    const val EXTRA_OPEN = "open_medicine"
    const val LATE_MS = 30 * 60_000L

    /** 한 회차의 알림 두 가지: 정한 시각 ("약 먹을 시간이에요"), 30분 뒤에도 체크 안 함 ("30분 지났어요"). */
    enum class Kind(val offsetMs: Long) {
        OnTime(0L),
        Late(LATE_MS);

        /** 이미 띄웠는지 기록할 때 쓰는 글자 (Late 는 예전 저장값과 같게 회차 글자 그대로). */
        fun alertKey(doseKey: String) = if (this == Late) doseKey else "$doseKey@0"
    }

    /** 계획됐고 아직 안 먹었고 그 알림을 아직 안 띄운 (회차, 알림 종류, 울릴 시각) 들. */
    private fun pending(context: Context, days: LongRange): List<Triple<Long, MedSlot, Pair<Kind, Long>>> {
        val s = MedStore.load(context)
        if (!s.active) return emptyList()
        val alerted = MedStore.alerted(context)
        val out = mutableListOf<Triple<Long, MedSlot, Pair<Kind, Long>>>()
        for (day in maxOf(s.startDay, days.first)..days.last) {
            for (slot in MedSlot.entries) {
                val key = doseKey(day, slot)
                if (!s.isPlanned(day, slot) || key in s.taken) continue
                val base = s.doseTime(day, slot).toInstant().toEpochMilli()
                Kind.entries.forEach { k ->
                    if (k.alertKey(key) !in alerted) out += Triple(day, slot, k to base + k.offsetMs)
                }
            }
        }
        return out
    }

    /** 다음에 울릴 시각: 아직 안 띄운 알림 중 지금 이후로 가장 이른 것 (정한 시각 또는 30분 뒤). */
    fun nextCheckMs(context: Context): Long? {
        val now = System.currentTimeMillis()
        val today = LocalDate.now().toEpochDay()
        return pending(context, today - 1..today + 2).map { it.third.second }.filter { it > now }.minOrNull()
    }

    fun update(context: Context) {
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        val next = nextCheckMs(context)
        val pi = checkIntent(context)
        if (next == null) {
            am.cancel(pi)
            return
        }
        val canExact = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || am.canScheduleExactAlarms()
        if (canExact) am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, next, pi)
        else am.setWindow(AlarmManager.RTC_WAKEUP, next, 60_000L, pi)
    }

    private fun checkIntent(context: Context): PendingIntent =
        PendingIntent.getBroadcast(
            context, 11, // 시보 알람(0)과 다른 번호
            Intent(context, MedReceiver::class.java).setAction(ACTION_CHECK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )

    /**
     * 지금 울릴 알림들 (울릴 시각이 지금 -10분 ~ +1분, 안 먹음, 아직 안 띄움).
     * 폰이 오래 꺼져 있다 켜진 경우 등 한참 지난 것은 울리지 않음.
     * 정한 시각과 30분 뒤가 함께 걸리면(예: 30분 넘게 꺼져 있다 켜짐) 30분 뒤 것만.
     */
    internal fun dueNow(context: Context): List<Triple<Long, MedSlot, Kind>> {
        val now = System.currentTimeMillis()
        val today = LocalDate.now().toEpochDay()
        val due = pending(context, today - 1..today)
            .filter { (_, _, kw) -> kw.second <= now + 60_000L && now - kw.second < 10 * 60_000L }
            .map { (d, sl, kw) -> Triple(d, sl, kw.first) }
        return due.filterNot { (d, sl, k) -> k == Kind.OnTime && due.any { it.first == d && it.second == sl && it.third == Kind.Late } }
    }

    // ── 알림 ──
    private const val CHANNEL_ID = "medicine"

    private fun notificationId(key: String) = 8000 + (key.hashCode() and 0x3FFF)

    /** 알림 띄우기. 같은 회차는 같은 번호라서 30분 뒤 알림이 정한 시각 알림을 대신함 (쌓이지 않음). */
    internal fun notifyDose(context: Context, day: Long, slot: MedSlot, kind: Kind) {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, context.getString(R.string.med_channel), NotificationManager.IMPORTANCE_HIGH).apply {
                    setSound(
                        RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM),
                        AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build(),
                    )
                    enableVibration(true)
                },
            )
        }
        val s = MedStore.load(context)
        val key = doseKey(day, slot)
        val t = s.doseTime(day, slot)
        val open = PendingIntent.getActivity(
            context, 12,
            Intent(context, MainActivity::class.java)
                .putExtra(EXTRA_OPEN, true)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val taken = PendingIntent.getBroadcast(
            context, notificationId(key),
            Intent(context, MedReceiver::class.java).setAction(ACTION_TAKEN).putExtra(EXTRA_KEY, key),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val text = context.getString(
            if (kind == Kind.Late) R.string.med_late_text else R.string.med_now_text,
            t.monthValue, t.dayOfMonth, context.getString(slot.label), hour12(t.hour), t.minute,
        )
        val n = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(context.getString(if (kind == Kind.Late) R.string.med_late_title else R.string.med_now_title))
            .setContentText(text)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setContentIntent(open)
            .setAutoCancel(true)
            .addAction(0, context.getString(R.string.med_taken_action), taken)
            .build()
        try {
            nm.notify(notificationId(key), n)
        } catch (_: SecurityException) { // 알림 권한 없음
        }
    }

    internal fun cancelNotification(context: Context, key: String) {
        context.getSystemService(NotificationManager::class.java)?.cancel(notificationId(key))
    }
}

/** 정한 시각·30분 지남 알람 + 알림의 [먹었어요]. (외부 앱은 보낼 수 없음: exported=false) */
class MedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            MedScheduler.ACTION_CHECK -> {
                val due = MedScheduler.dueNow(context)
                due.forEach { (day, slot, kind) ->
                    MedStore.markAlerted(context, kind.alertKey(doseKey(day, slot)))
                    MedScheduler.notifyDose(context, day, slot, kind)
                }
                // 이벤트 때 화면 켜기: 시계 화면을 켜고 약 먹는 시간 창을 엶
                if (due.isNotEmpty()) {
                    com.example.clock.auto.ClockWaker.bring(context, com.example.clock.auto.AutoEvent.MEDICINE)
                }
            }
            MedScheduler.ACTION_TAKEN -> {
                val key = intent.getStringExtra(MedScheduler.EXTRA_KEY) ?: return
                MedStore.markTaken(context, key, System.currentTimeMillis())
                MedScheduler.cancelNotification(context, key)
            }
        }
        MedScheduler.update(context)
    }
}

/** 12시간제 시 (그림처럼 "저녁 6시"). 0시 → 12. */
fun hour12(h: Int): Int = (h % 12).let { if (it == 0) 12 else it }

/** 체크한 시각 "18:05" (밀리초 → 시:분). */
fun takenTimeText(ms: Long): String {
    val t = Instant.ofEpochMilli(ms).atZone(ZoneId.systemDefault())
    return "%d:%02d".format(t.hour, t.minute)
}
