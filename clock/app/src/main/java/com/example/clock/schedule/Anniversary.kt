package com.example.clock.schedule

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import org.json.JSONArray
import org.json.JSONObject
import java.time.LocalDate
import java.time.temporal.ChronoUnit

/*
 * 해마다 돌아오는 기념일 ([시보·알람] → 기념일)
 *  본인 생일 · 배우자 생일 · 생일 1~20 · 첫 만남일 · 기념일 1~20
 *  항목마다: 최초 날짜, 경과 일수 표시 on/off, 다음 회까지 D-day 표시 on/off (X일 전부터)
 *  시계 화면 왼쪽 아래에 1시간마다 다시 계산해 표시 (양력, 2월 29일은 평년에 2월 28일)
 */

enum class AnnivType(val label: String, val emoji: String) {
    Self("본인 생일", "🎂"),
    Spouse("배우자 생일", "🎂"),
    Birthday("생일", "🎂"),
    FirstMeet("첫 만남일", "💑"),
    Anniversary("기념일", "🎉"),
}

const val ANNIV_MAX = 20

/** 기념일 하나. [date] 가 없으면 아직 등록 안 함. */
data class Anniv(
    val type: AnnivType,
    val name: String = "",        // 관련자 성명
    val alias: String = "",       // 기념일 별명 (기념일만)
    val date: LocalDate? = null,  // 최초 날짜
    val showElapsed: Boolean = false,
    val showCountdown: Boolean = true,
    val daysBefore: Int = 30,     // 다음 회까지 이 날수 안으로 들어오면 D-day 표시
)

data class AnnivBook(
    val self: Anniv = Anniv(AnnivType.Self),
    val spouse: Anniv = Anniv(AnnivType.Spouse),
    val firstMeet: Anniv = Anniv(AnnivType.FirstMeet),
    val birthdays: List<Anniv> = emptyList(),
    val anniversaries: List<Anniv> = emptyList(),
) {
    val all get() = listOf(self, spouse, firstMeet) + birthdays + anniversaries
}

object AnnivStore {
    private const val PREFS = "anniversary"

    var version by mutableIntStateOf(0)
        private set

    fun load(context: Context): AnnivBook {
        val raw = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("json", null) ?: return AnnivBook()
        return runCatching { fromJson(JSONObject(raw)) }.getOrElse { AnnivBook() }
    }

    fun save(context: Context, b: AnnivBook) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("json", toJson(b).toString()).apply()
        version++
    }

    private fun j(a: Anniv) = JSONObject().put("type", a.type.name).put("name", a.name).put("alias", a.alias)
        .put("date", a.date?.toString() ?: "").put("elapsed", a.showElapsed).put("countdown", a.showCountdown).put("before", a.daysBefore)

    private fun a(o: JSONObject?, type: AnnivType): Anniv {
        if (o == null) return Anniv(type)
        return Anniv(
            type = type,
            name = o.optString("name"), alias = o.optString("alias"),
            date = o.optString("date").takeIf { it.isNotBlank() }?.let { runCatching { LocalDate.parse(it) }.getOrNull() },
            showElapsed = o.optBoolean("elapsed", false), showCountdown = o.optBoolean("countdown", true),
            daysBefore = o.optInt("before", 30),
        )
    }

    private fun toJson(b: AnnivBook) = JSONObject()
        .put("self", j(b.self)).put("spouse", j(b.spouse)).put("firstMeet", j(b.firstMeet))
        .put("birthdays", JSONArray(b.birthdays.map { j(it) })).put("anniversaries", JSONArray(b.anniversaries.map { j(it) }))

    private fun fromJson(o: JSONObject): AnnivBook {
        val bd = o.optJSONArray("birthdays")
        val an = o.optJSONArray("anniversaries")
        return AnnivBook(
            self = a(o.optJSONObject("self"), AnnivType.Self),
            spouse = a(o.optJSONObject("spouse"), AnnivType.Spouse),
            firstMeet = a(o.optJSONObject("firstMeet"), AnnivType.FirstMeet),
            birthdays = (0 until minOf(bd?.length() ?: 0, ANNIV_MAX)).map { a(bd!!.optJSONObject(it), AnnivType.Birthday) },
            anniversaries = (0 until minOf(an?.length() ?: 0, ANNIV_MAX)).map { a(an!!.optJSONObject(it), AnnivType.Anniversary) },
        )
    }
}

/** 그해의 같은 월·일 (2월 29일은 평년에 2월 28일). */
private fun inYear(first: LocalDate, year: Int): LocalDate =
    if (first.monthValue == 2 && first.dayOfMonth == 29 && !java.time.Year.isLeap(year.toLong())) LocalDate.of(year, 2, 28)
    else first.withYear(year)

/** 오늘 이후(오늘 포함) 가장 가까운 회차. 최초 날짜가 아직 안 왔으면 그날. */
fun nextOccurrence(first: LocalDate, today: LocalDate): LocalDate {
    if (!first.isBefore(today)) return first
    val thisYear = inYear(first, today.year)
    return if (thisYear.isBefore(today)) inYear(first, today.year + 1) else thisYear
}

fun annivTitle(a: Anniv): String = when (a.type) {
    AnnivType.Self -> "내 생일"
    AnnivType.Spouse -> "${a.name.ifBlank { "배우자" }} 생일"
    AnnivType.Birthday -> "${a.name.ifBlank { "생일" }} 생일".replace("생일 생일", "생일")
    AnnivType.FirstMeet -> if (a.name.isBlank()) "첫 만남" else "${a.name} 첫 만남"
    AnnivType.Anniversary -> a.alias.ifBlank { "기념일" } + if (a.name.isNotBlank()) " · ${a.name}" else ""
}

/** 시계 화면에 보일 한 줄. [daysLeft] 가 작을수록 위에. */
data class AnnivLine(val text: String, val daysLeft: Long, val today: Boolean)

/**
 * 오늘 기준으로 보일 줄들: 오늘인 것 → 가까운 순.
 *  🎉 오늘 엄마 생일 (62세)
 *  🎂 아빠 생일 D-5 (12/3 · 65세)
 *  💑 철수 첫 만남 D+1,234 · D-20 (3주년)
 */
fun annivLines(b: AnnivBook, today: LocalDate = LocalDate.now()): List<AnnivLine> =
    b.all.mapNotNull { a ->
        val first = a.date ?: return@mapNotNull null
        if (!a.showElapsed && !a.showCountdown) return@mapNotNull null
        val next = nextOccurrence(first, today)
        val left = ChronoUnit.DAYS.between(today, next)
        val nth = next.year - first.year
        val count = when {
            nth <= 0 -> ""
            a.type == AnnivType.Self || a.type == AnnivType.Spouse || a.type == AnnivType.Birthday -> "${nth}세"
            else -> "${nth}주년"
        }
        val title = annivTitle(a)
        if (left == 0L) {
            return@mapNotNull AnnivLine("🎉 오늘 $title" + if (count.isNotBlank()) " ($count)" else "", 0, true)
        }
        val parts = mutableListOf<String>()
        if (a.showElapsed && !first.isAfter(today)) parts += "D+%,d".format(ChronoUnit.DAYS.between(first, today))
        if (a.showCountdown && left <= a.daysBefore) {
            parts += "D-$left (${next.monthValue}/${next.dayOfMonth}" + (if (count.isNotBlank()) " · $count" else "") + ")"
        }
        if (parts.isEmpty()) null else AnnivLine("${a.type.emoji} $title ${parts.joinToString(" · ")}", left, false)
    }.sortedWith(compareBy({ !it.today }, { it.daysLeft }))
