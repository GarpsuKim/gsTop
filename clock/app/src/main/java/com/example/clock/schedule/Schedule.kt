package com.example.clock.schedule

import android.app.AlarmManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.RingtoneManager
import android.os.Build
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.core.app.NotificationCompat
import com.example.clock.MainActivity
import com.example.clock.R
import com.example.clock.auto.AutoEvent
import com.example.clock.auto.ClockWaker
import com.example.clock.auto.GreetingPlayer
import org.json.JSONArray
import org.json.JSONObject
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime

/*
 * 시간표 ([시보·알람] → 시간표): 근무 시간표 · 학교 수업 시간표 · 학원 수업 시간표 (최대 3곳)
 *  - 시계 화면 왼쪽 위에 지금 상황(근무 중 / 수업 중 / 학원)과 다음 일정을 늘 표시 (1분마다 다시 계산)
 *  - 출근·퇴근·수업 몇 분 전 알림 (알림 / 팝업창(시계 화면 + 안내) / 소리), 앱이 꺼져 있어도 정확한 시각에 깨어나 처리
 * 요일 번호: 0 = 일요일 … 6 = 토요일. 시각: 자정부터 분.
 */

// ─────────────────────────────── 데이터 ───────────────────────────────

/** 알림 설정: [minutesBefore] 분 전에 알림·팝업창·소리. */
data class AlertSetting(
    val enabled: Boolean = false,
    val minutesBefore: Int = 10,
    val notify: Boolean = true,
    val popup: Boolean = true,
    val sound: Boolean = true,
    val soundUri: String = "",
    val soundName: String = "",
)

/** 근무 하루: [start]~[end] (분). [end] 가 1440 을 넘으면 다음 날까지 (밤샘 근무). */
data class WorkDay(val on: Boolean = false, val place: String = "", val start: Int = 9 * 60, val end: Int = 18 * 60)

data class WorkSchedule(
    val days: List<WorkDay> = List(7) { WorkDay() },
    val sameEveryDay: Boolean = true,
    val startAlert: AlertSetting = AlertSetting(),
    val endAlert: AlertSetting = AlertSetting(),
    /** 토요일 / 일요일도 쓰는지 (따로). 기본 꺼짐 = 그 요일은 안 보이고 일정·알림에서도 빠짐. */
    val sat: Boolean = false,
    val sun: Boolean = false,
    /** 메모 / 참고사항 (그냥 글자, [텍스트 공유] 에 포함). */
    val memo: String = "",
) {
    val any get() = days.withIndex().any { (i, d) -> d.on && dayVisible(i, sat, sun) }
}

/** 요일이 보이는지: 월~금은 늘, 토요일(6)·일요일(0) 은 [sat]·[sun] 을 켰을 때만. */
fun dayVisible(day: Int, sat: Boolean, sun: Boolean) = day in 1..5 || (day == 6 && sat) || (day == 0 && sun)

/** 보이는 요일 번호들 (일 0 … 토 6 순서). */
fun visibleDays(sat: Boolean, sun: Boolean): List<Int> = (0..6).filter { dayVisible(it, sat, sun) }

/** 교시: 이름 + 시작·종료 (분). */
data class Period(val name: String, val start: Int, val end: Int)

/** 시간표 한 칸: 과목명 · 강의실 · 교수명. 과목명이 있으면 "수업 있음". */
data class Cell(val subject: String = "", val room: String = "", val teacher: String = "") {
    val hasClass get() = subject.isNotBlank()
    val isEmpty get() = subject.isBlank() && room.isBlank() && teacher.isBlank()
}

/**
 * 수업 시간표 (학교 하나 또는 학원 하나): 교시들 × 7요일.
 * [dayNames]: 요일마다 위쪽 줄 글자 (학교 = 강의실, 학원 = 학원명). [cells][교시][요일].
 */
data class ClassTable(
    val periods: List<Period>,
    val dayNames: List<String> = List(7) { "" },
    val cells: List<List<Cell>> = List(periods.size) { List(7) { Cell() } },
    /** 토요일 / 일요일도 쓰는지 (따로, 기본 꺼짐). */
    val sat: Boolean = false,
    val sun: Boolean = false,
    /** 메모 / 참고사항. */
    val memo: String = "",
) {
    val any get() = cells.any { row -> row.withIndex().any { (d, c) -> c.hasClass && dayVisible(d, sat, sun) } }

    fun withCell(p: Int, d: Int, c: Cell) = copy(cells = cells.mapIndexed { i, row -> if (i == p) row.mapIndexed { j, x -> if (j == d) c else x } else row })
    fun withPeriod(p: Int, pr: Period) = copy(periods = periods.mapIndexed { i, x -> if (i == p) pr else x })
    fun withDayName(d: Int, name: String) = copy(dayNames = dayNames.mapIndexed { i, x -> if (i == d) name else x })
}

data class Timetables(
    val work: WorkSchedule = WorkSchedule(),
    val school: ClassTable = defaultSchool(),
    val schoolAlert: AlertSetting = AlertSetting(minutesBefore = 5),
    val academies: List<ClassTable> = List(ACADEMY_COUNT) { defaultAcademy() },
    val academyAlert: AlertSetting = AlertSetting(minutesBefore = 10),
)

const val ACADEMY_COUNT = 3

/** 학교 기본 교시: 아침자습 7:30, 0교시 8:00, 1교시 9:00 … 12교시 20:00 (50분), 야간자습 21:00~22:00. */
fun defaultSchool(): ClassTable {
    val list = mutableListOf(Period("아침자습", 7 * 60 + 20, 7 * 60 + 50), Period("0교시", 8 * 60, 8 * 60 + 50))
    for (n in 1..12) {
        val s = 9 * 60 + (n - 1) * 60
        list += Period("${n}교시", s, s + 50)
    }
    list += Period("야간자습", 21 * 60, 22 * 60)
    return ClassTable(list)
}

/** 학원 기본 교시: 1교시 17:00, 2교시 18:00, 3교시 19:00 (50분). */
fun defaultAcademy() = ClassTable((1..3).map { n -> Period("${n}교시", 16 * 60 + n * 60, 16 * 60 + n * 60 + 50) })

/** 0 = 일요일 … 6 = 토요일. */
fun dayIndex(date: LocalDate) = date.dayOfWeek.value % 7

val DAY_NAMES = listOf("일", "월", "화", "수", "목", "금", "토")

/** "09:00" (1440 이상이면 다음 날 시각). */
fun hhmm(minutes: Int): String {
    val m = ((minutes % 1440) + 1440) % 1440
    return "%02d:%02d".format(m / 60, m % 60)
}

// ─────────────────────────────── 저장 ───────────────────────────────

object ScheduleStore {
    private const val PREFS = "schedule"

    internal fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** 바뀔 때마다 늘어남 → 화면이 다시 읽음. */
    var version by mutableIntStateOf(0)
        private set

    fun load(context: Context): Timetables {
        val raw = prefs(context).getString("json", null) ?: return Timetables()
        return runCatching { fromJson(JSONObject(raw)) }.getOrElse { Timetables() }
    }

    /** 저장하고 다음 알림을 다시 맞춤. */
    fun save(context: Context, t: Timetables) {
        prefs(context).edit().putString("json", toJson(t).toString()).apply()
        version++
        ScheduleAlarm.update(context)
    }

    private fun alertJson(a: AlertSetting) = JSONObject().put("enabled", a.enabled).put("min", a.minutesBefore)
        .put("notify", a.notify).put("popup", a.popup).put("sound", a.sound).put("uri", a.soundUri).put("uriName", a.soundName)

    private fun alert(o: JSONObject?, d: AlertSetting) = if (o == null) d else AlertSetting(
        enabled = o.optBoolean("enabled", d.enabled), minutesBefore = o.optInt("min", d.minutesBefore),
        notify = o.optBoolean("notify", d.notify), popup = o.optBoolean("popup", d.popup), sound = o.optBoolean("sound", d.sound),
        soundUri = o.optString("uri"), soundName = o.optString("uriName"),
    )

    private fun tableJson(t: ClassTable) = JSONObject()
        .put("periods", JSONArray(t.periods.map { JSONObject().put("n", it.name).put("s", it.start).put("e", it.end) }))
        .put("dayNames", JSONArray(t.dayNames))
        .put("cells", JSONArray(t.cells.map { row -> JSONArray(row.map { JSONObject().put("s", it.subject).put("r", it.room).put("t", it.teacher) }) }))
        .put("sat", t.sat).put("sun", t.sun).put("memo", t.memo)

    private fun table(o: JSONObject?, d: ClassTable): ClassTable {
        if (o == null) return d
        val ps = o.optJSONArray("periods")
        val periods = if (ps == null || ps.length() != d.periods.size) d.periods
        else (0 until ps.length()).map { i -> ps.getJSONObject(i).let { Period(it.optString("n", d.periods[i].name), it.optInt("s"), it.optInt("e")) } }
        val dn = o.optJSONArray("dayNames")
        val cs = o.optJSONArray("cells")
        return ClassTable(
            periods = periods,
            dayNames = List(7) { dn?.optString(it).orEmpty() },
            cells = List(periods.size) { p ->
                val row = cs?.optJSONArray(p)
                List(7) { dd -> row?.optJSONObject(dd)?.let { Cell(it.optString("s"), it.optString("r"), it.optString("t")) } ?: Cell() }
            },
            sat = o.optBoolean("sat", o.optBoolean("weekend", false)),
            sun = o.optBoolean("sun", o.optBoolean("weekend", false)),
            memo = o.optString("memo"),
        )
    }

    private fun toJson(t: Timetables) = JSONObject()
        .put(
            "work",
            JSONObject()
                .put("days", JSONArray(t.work.days.map { JSONObject().put("on", it.on).put("place", it.place).put("s", it.start).put("e", it.end) }))
                .put("same", t.work.sameEveryDay)
                .put("startAlert", alertJson(t.work.startAlert)).put("endAlert", alertJson(t.work.endAlert))
                .put("sat", t.work.sat).put("sun", t.work.sun).put("memo", t.work.memo),
        )
        .put("school", tableJson(t.school)).put("schoolAlert", alertJson(t.schoolAlert))
        .put("academies", JSONArray(t.academies.map { tableJson(it) })).put("academyAlert", alertJson(t.academyAlert))

    private fun fromJson(o: JSONObject): Timetables {
        val d = Timetables()
        val w = o.optJSONObject("work")
        val days = w?.optJSONArray("days")
        val ac = o.optJSONArray("academies")
        return Timetables(
            work = WorkSchedule(
                days = List(7) { i ->
                    days?.optJSONObject(i)?.let { WorkDay(it.optBoolean("on"), it.optString("place"), it.optInt("s", 540), it.optInt("e", 1080)) } ?: WorkDay()
                },
                sameEveryDay = w?.optBoolean("same", true) ?: true,
                startAlert = alert(w?.optJSONObject("startAlert"), d.work.startAlert),
                endAlert = alert(w?.optJSONObject("endAlert"), d.work.endAlert),
                sat = w?.optBoolean("sat", w.optBoolean("weekend", false)) ?: false,
                sun = w?.optBoolean("sun", w.optBoolean("weekend", false)) ?: false,
                memo = w?.optString("memo").orEmpty(),
            ),
            school = table(o.optJSONObject("school"), d.school),
            schoolAlert = alert(o.optJSONObject("schoolAlert"), d.schoolAlert),
            academies = List(ACADEMY_COUNT) { i -> table(ac?.optJSONObject(i), d.academies[i]) },
            academyAlert = alert(o.optJSONObject("academyAlert"), d.academyAlert),
        )
    }
}

// ─────────────────────────────── 계산: 일정 · 현재 상황 · 다음 일정 ───────────────────────────────

enum class SlotKind { Work, School, Academy }

/** 실제 날짜에 놓인 일정 하나 (근무 하루 / 수업 한 칸). */
data class Slot(
    val kind: SlotKind,
    val title: String,   // "본사" / "2교시 수학" / "OO학원 1교시 영어"
    val detail: String,  // 강의실·교수 등
    val start: ZonedDateTime,
    val end: ZonedDateTime,
    val key: String,     // 알림을 한 번만 하려고 쓰는 이름
)

/** [from] 부터 [days] 일 동안의 일정 (전날 밤샘 근무도 포함). */
fun slots(t: Timetables, from: LocalDate, days: Int): List<Slot> {
    val zone = ZoneId.systemDefault()
    val out = mutableListOf<Slot>()
    for (k in -1 until days) {
        val date = from.plusDays(k.toLong())
        val di = dayIndex(date)
        val base = date.atStartOfDay(zone)
        val epoch = date.toEpochDay()
        t.work.days[di].takeIf { it.on && it.end > it.start && dayVisible(di, t.work.sat, t.work.sun) }?.let { w ->
            out += Slot(SlotKind.Work, w.place.ifBlank { "근무" }, "", base.plusMinutes(w.start.toLong()), base.plusMinutes(w.end.toLong()), "w:$epoch")
        }
        fun addTable(table: ClassTable, kind: SlotKind, prefix: String, keyBase: String) {
            if (!dayVisible(di, table.sat, table.sun)) return
            table.periods.forEachIndexed { p, pr ->
                val c = table.cells[p][di]
                if (!c.hasClass || pr.end <= pr.start) return@forEachIndexed
                val room = c.room.ifBlank { if (kind == SlotKind.School) table.dayNames[di] else "" }
                val detail = listOf(room, c.teacher).filter { it.isNotBlank() }.joinToString(" · ")
                out += Slot(kind, "$prefix${pr.name} ${c.subject}".trim(), detail, base.plusMinutes(pr.start.toLong()), base.plusMinutes(pr.end.toLong()), "$keyBase:$epoch:$p")
            }
        }
        addTable(t.school, SlotKind.School, "", "s")
        t.academies.forEachIndexed { i, a ->
            val name = a.dayNames[di].ifBlank { "학원${i + 1}" }
            addTable(a, SlotKind.Academy, "$name ", "a$i")
        }
    }
    return out.sortedBy { it.start }
}

/** 시계 화면 왼쪽 위: 지금 상황 + 다음 일정 (일정이 하나도 없으면 null). */
data class ScheduleStatus(val now: String, val next: String?)

fun scheduleStatus(t: Timetables, at: ZonedDateTime = ZonedDateTime.now()): ScheduleStatus? {
    if (!t.work.any && !t.school.any && t.academies.none { it.any }) return null
    val all = slots(t, at.toLocalDate(), 8)
    val active = all.filter { !it.start.isAfter(at) && it.end.isAfter(at) }
    val now = if (active.isEmpty()) "쉬는 시간" else active.joinToString("  ·  ") { s ->
        val head = when (s.kind) {
            SlotKind.Work -> "근무 중"
            SlotKind.School -> "수업 중"
            SlotKind.Academy -> "학원"
        }
        "$head · ${s.title} (~${hhmm(s.end.hour * 60 + s.end.minute)})"
    }
    val next = all.firstOrNull { it.start.isAfter(at) }?.let { s ->
        val mins = java.time.Duration.between(at, s.start).toMinutes()
        val whenText = when {
            mins < 60 -> "${mins + 1}분 후"
            s.start.toLocalDate() == at.toLocalDate() -> "${mins / 60}시간 ${mins % 60}분 후"
            s.start.toLocalDate() == at.toLocalDate().plusDays(1) -> "내일"
            else -> "${DAY_NAMES[dayIndex(s.start.toLocalDate())]}요일"
        }
        "다음: ${s.title} ${hhmm(s.start.hour * 60 + s.start.minute)} ($whenText)"
    }
    return ScheduleStatus(now, next)
}

// ─────────────────────────────── 알림 ───────────────────────────────

/** 울릴 알림 하나. */
data class AlertItem(val key: String, val atMs: Long, val setting: AlertSetting, val text: String)

/** [from] 부터 [days] 일 동안의 알림들. */
fun alertItems(t: Timetables, from: LocalDate, days: Int): List<AlertItem> {
    val out = mutableListOf<AlertItem>()
    for (s in slots(t, from, days)) {
        val time = hhmm(s.start.hour * 60 + s.start.minute)
        when (s.kind) {
            SlotKind.Work -> {
                t.work.startAlert.takeIf { it.enabled }?.let { a ->
                    out += AlertItem("${s.key}:s", s.start.minusMinutes(a.minutesBefore.toLong()).toInstant().toEpochMilli(), a,
                        "출근 ${a.minutesBefore}분 전 · ${s.title} $time")
                }
                t.work.endAlert.takeIf { it.enabled }?.let { a ->
                    out += AlertItem("${s.key}:e", s.end.minusMinutes(a.minutesBefore.toLong()).toInstant().toEpochMilli(), a,
                        "퇴근 ${a.minutesBefore}분 전 · ${hhmm(s.end.hour * 60 + s.end.minute)}")
                }
            }
            SlotKind.School -> t.schoolAlert.takeIf { it.enabled }?.let { a ->
                out += AlertItem(s.key, s.start.minusMinutes(a.minutesBefore.toLong()).toInstant().toEpochMilli(), a,
                    "수업 ${a.minutesBefore}분 전 · ${s.title} $time" + if (s.detail.isNotBlank()) " (${s.detail})" else "")
            }
            SlotKind.Academy -> t.academyAlert.takeIf { it.enabled }?.let { a ->
                out += AlertItem(s.key, s.start.minusMinutes(a.minutesBefore.toLong()).toInstant().toEpochMilli(), a,
                    "학원 ${a.minutesBefore}분 전 · ${s.title} $time" + if (s.detail.isNotBlank()) " (${s.detail})" else "")
            }
        }
    }
    return out.filter { it.setting.notify || it.setting.popup || it.setting.sound }.sortedBy { it.atMs }
}

/** 다음 알림 시각에 [ScheduleReceiver] 가 깨어나도록 정확한 알람을 맞춤 (시보·약 알람과 따로). */
object ScheduleAlarm {
    private const val ACTION = "com.example.clock.action.SCHEDULE_ALERT"
    private const val CHANNEL_ID = "schedule"
    const val POPUP_TEXT = "popup_text"

    private fun pi(context: Context) = PendingIntent.getBroadcast(
        context, 71, Intent(context, ScheduleReceiver::class.java).setAction(ACTION),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private fun fired(context: Context) = ScheduleStore.prefs(context).getStringSet("fired", emptySet())!!.toSet()

    fun update(context: Context) {
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        val now = System.currentTimeMillis()
        val done = fired(context)
        val next = alertItems(ScheduleStore.load(context), LocalDate.now(), 8)
            .firstOrNull { it.atMs > now && it.key !in done }?.atMs
        if (next == null) {
            am.cancel(pi(context))
            return
        }
        val canExact = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || am.canScheduleExactAlarms()
        if (canExact) am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, next, pi(context))
        else am.setWindow(AlarmManager.RTC_WAKEUP, next, 60_000L, pi(context))
    }

    /** 지금 울릴 알림들 (3분 전 ~ 1분 뒤, 아직 안 한 것). 꺼내면 "한 것" 으로 기록 (지난 기록은 정리). */
    internal fun takeDue(context: Context): List<AlertItem> {
        val now = System.currentTimeMillis()
        val done = fired(context)
        val due = alertItems(ScheduleStore.load(context), LocalDate.now(), 2)
            .filter { it.atMs in (now - 3 * 60_000L)..(now + 60_000L) && it.key !in done }
        if (due.isNotEmpty()) {
            val keepFrom = LocalDate.now().minusDays(2).toEpochDay()
            val kept = done.filter { k -> (k.split(':').getOrNull(1)?.toLongOrNull() ?: 0L) >= keepFrom }
            ScheduleStore.prefs(context).edit().putStringSet("fired", (kept + due.map { it.key }).toSet()).apply()
        }
        return due
    }

    /** 알림 하나 처리: 알림(알림창) · 팝업창(시계 화면 + 위쪽 안내) · 소리. [테스트] 도 이것을 씀. */
    fun fire(context: Context, item: AlertItem) {
        val s = item.setting
        if (s.notify) notify(context, item)
        if (s.sound) {
            // 고른 파일, 없으면 폰의 기본 알림음 (인사 음악과 같은 재생기로 한 번)
            if (s.soundUri.isNotBlank()) {
                GreetingPlayer.play(context, s.soundUri)
            } else {
                // 기본음: 폰의 알림음 → 없으면(알림음 "없음" 으로 둔 폰) 알람음 → 벨소리. 실제 소리 파일 주소로 열고 6초만
                val uri = listOf(RingtoneManager.TYPE_NOTIFICATION, RingtoneManager.TYPE_ALARM, RingtoneManager.TYPE_RINGTONE)
                    .firstNotNullOfOrNull { RingtoneManager.getActualDefaultRingtoneUri(context, it) }
                if (uri != null) {
                    GreetingPlayer.play(context, uri.toString())
                    android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                        if (GreetingPlayer.playingUri == uri.toString()) GreetingPlayer.stop()
                    }, 6_000L)
                }
            }
        }
        if (s.popup) {
            ScheduleStore.prefs(context).edit().putString(POPUP_TEXT, item.text).apply()
            ClockWaker.bring(context, AutoEvent.SCHEDULE)
        }
    }

    /** 팝업창으로 보여 줄 글자 (시계 화면이 꺼내 씀). */
    fun takePopupText(context: Context): String? {
        val p = ScheduleStore.prefs(context)
        val t = p.getString(POPUP_TEXT, null)
        p.edit().remove(POPUP_TEXT).apply()
        return t
    }

    private fun notify(context: Context, item: AlertItem) {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                // 소리는 [소리] 설정대로 앱이 따로 냄 → 채널은 조용히
                NotificationChannel(CHANNEL_ID, "시간표 알림", NotificationManager.IMPORTANCE_HIGH).apply { setSound(null, null) },
            )
        }
        val open = PendingIntent.getActivity(
            context, 72, Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val n = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(item.text.substringBefore(" · "))
            .setContentText(item.text.substringAfter(" · ", item.text))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        runCatching { nm.notify(9300 + (item.key.hashCode() and 0xFF), n) }
    }
}

/** 시간표 알림 시각이 됨 → 처리하고 다음 알림을 맞춤. (외부 앱은 보낼 수 없음: exported=false) */
class ScheduleReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        ScheduleAlarm.takeDue(context).forEach { ScheduleAlarm.fire(context, it) }
        ScheduleAlarm.update(context)
    }
}
