package com.example.clock.ui

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.ActivityInfo
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.Row
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentPaste
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.clock.chime.MedScheduler
import com.example.clock.chime.MedSchedule
import com.example.clock.chime.MedSlot
import com.example.clock.chime.hour12
import com.example.clock.chime.takenTimeText
import com.example.clock.schedule.AlertSetting
import com.example.clock.schedule.AnnivBook
import com.example.clock.schedule.AnnivType
import com.example.clock.schedule.ClassTable
import com.example.clock.schedule.DAY_NAMES
import com.example.clock.schedule.WorkSchedule
import com.example.clock.schedule.visibleDays
import com.example.clock.schedule.annivTitle
import com.example.clock.schedule.hhmm
import com.example.clock.schedule.nextOccurrence
import java.time.LocalDate
import java.time.temporal.ChronoUnit

/*
 * [텍스트 공유]: 약 먹을 시간 · 시간표(근무·학교·학원) · 기념일 창의 맨 아래 버튼.
 * 그 화면의 내용을 일반 글자로 바꿔 다른 앱(문자, 이메일, 텔레그램, 카카오톡 …)으로 보냄.
 */

/** 글자를 다른 앱으로 보내기 (앱 고르기 창). */
fun shareText(context: Context, subject: String, text: String) {
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_SUBJECT, subject)
        putExtra(Intent.EXTRA_TEXT, text)
    }
    val chooser = Intent.createChooser(send, "텍스트 공유")
    if (context !is Activity) chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    runCatching { context.startActivity(chooser) }
}

/** 창 맨 아래 [텍스트 공유] 버튼 (가로 꽉 차게). */
@Composable
fun ShareTextButton(color: Color, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().height(52.dp),
        border = BorderStroke(1.5.dp, color),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = color),
    ) {
        Icon(Icons.Filled.Share, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text("텍스트 공유", fontSize = 16.sp, fontWeight = FontWeight.Bold)
    }
}

/**
 * 메모 / 참고사항 칸 (그냥 글자, 여러 줄). 오른쪽 [복사] = 메모 전체를 클립보드로, [붙여넣기] = 클립보드 글자를 끝에 붙임.
 * (칸 안에서 길게 눌러 고르는 보통 복사·붙여넣기도 됨)
 */
@Composable
fun MemoField(value: String, onChange: (String) -> Unit, ink: Color, accent: Color, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var text by remember(value) { mutableStateOf(value) }
    fun set(v: String) { text = v; onChange(v) }
    OutlinedTextField(
        value = text,
        onValueChange = ::set,
        label = { Text("메모 / 참고사항") },
        placeholder = { Text("자유롭게 적어 두세요 (공유할 때 함께 보냄)", fontSize = 13.sp) },
        minLines = 1,
        maxLines = 4,
        textStyle = TextStyle(fontSize = 15.sp),
        modifier = modifier.fillMaxWidth(),
        trailingIcon = {
            Row {
                IconButton(onClick = {
                    if (text.isNotBlank()) {
                        clipboard.setText(AnnotatedString(text))
                        android.widget.Toast.makeText(context, "메모를 복사했어요", android.widget.Toast.LENGTH_SHORT).show()
                    }
                }) { Icon(Icons.Filled.ContentCopy, contentDescription = "복사", tint = accent) }
                IconButton(onClick = {
                    val clip = clipboard.getText()?.text.orEmpty()
                    if (clip.isNotEmpty()) set(if (text.isEmpty()) clip else text.trimEnd() + "\n" + clip)
                }) { Icon(Icons.Filled.ContentPaste, contentDescription = "붙여넣기", tint = accent) }
            }
        },
        colors = OutlinedTextFieldDefaults.colors(
            focusedTextColor = ink, unfocusedTextColor = ink, cursorColor = accent,
            focusedBorderColor = accent, unfocusedBorderColor = ink.copy(alpha = 0.35f),
            focusedLabelColor = accent, unfocusedLabelColor = ink.copy(alpha = 0.7f),
            focusedPlaceholderColor = ink.copy(alpha = 0.45f), unfocusedPlaceholderColor = ink.copy(alpha = 0.45f),
            focusedContainerColor = Color.Transparent, unfocusedContainerColor = Color.Transparent,
        ),
    )
}

private fun StringBuilder.memoLines(memo: String) {
    if (memo.isNotBlank()) appendLine("📝 ${memo.trim()}")
}

private tailrec fun Context.activityOrNull(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.activityOrNull()
    else -> null
}

/** 이 창이 열려 있는 동안 세로 화면으로 고정, 닫으면 원래대로 (MainActivity 가 회전을 직접 처리하므로 창이 닫히지 않음). */
@Composable
fun LockPortrait() {
    val context = LocalContext.current
    DisposableEffect(Unit) {
        val activity = context.activityOrNull()
        val before = activity?.requestedOrientation
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        onDispose { if (activity != null && before != null) activity.requestedOrientation = before }
    }
}

// ─────────────────────────────── 글자로 바꾸기 ───────────────────────────────

private fun dateText(d: LocalDate) = "${d.monthValue}월 ${d.dayOfMonth}일 (${DAY_NAMES[d.dayOfWeek.value % 7]})"

private fun lengthText(len: Int) = "${len / 60}시간" + if (len % 60 != 0) " ${len % 60}분" else ""

private fun alertText(label: String, a: AlertSetting): String {
    if (!a.enabled) return "$label: 꺼짐"
    val how = listOfNotNull("알림".takeIf { a.notify }, "팝업창".takeIf { a.popup }, "소리".takeIf { a.sound })
    return "$label: ${a.minutesBefore}분 전" + if (how.isNotEmpty()) " (${how.joinToString(" · ")})" else ""
}

/** 근무 시간표 → 글자. */
fun workShareText(w: WorkSchedule): String = buildString {
    appendLine("🗓 근무 시간표")
    memoLines(w.memo)
    visibleDays(w.sat, w.sun).forEach { i ->
        val d = w.days[i]
        append("${DAY_NAMES[i]}요일: ")
        if (!d.on) appendLine("쉼")
        else {
            append("${hhmm(d.start)} ~ ${if (d.end > 1440) "다음날 " else ""}${hhmm(d.end)} (${lengthText(d.end - d.start)})")
            appendLine(if (d.place.isNotBlank()) " · ${d.place}" else "")
        }
    }
    appendLine()
    appendLine(alertText("출근 알림", w.startAlert))
    append(alertText("퇴근 알림", w.endAlert))
}

/** 수업 시간표 하나의 본문: 요일마다 수업 있는 교시만. 수업이 없으면 빈 글자. */
private fun StringBuilder.classLines(t: ClassTable, rowName: String) {
    visibleDays(t.sat, t.sun).forEach { d ->
        val rows = t.periods.indices.filter { p -> t.cells[p][d].hasClass }
        if (rows.isEmpty()) return@forEach
        append("■ ${DAY_NAMES[d]}요일")
        appendLine(if (t.dayNames[d].isNotBlank()) " ($rowName: ${t.dayNames[d]})" else "")
        rows.forEach { p ->
            val pr = t.periods[p]
            val c = t.cells[p][d]
            val extra = listOf(c.room, c.teacher).filter { it.isNotBlank() }
            appendLine(" · ${pr.name} ${hhmm(pr.start)}~${hhmm(pr.end)} ${c.subject}" + if (extra.isNotEmpty()) " (${extra.joinToString(", ")})" else "")
        }
    }
}

/** 학교 수업 시간표 → 글자. */
fun schoolShareText(t: ClassTable, alert: AlertSetting): String = buildString {
    appendLine("🏫 학교 수업 시간표")
    memoLines(t.memo)
    if (!t.any) appendLine("(등록된 수업 없음)") else classLines(t, "강의실")
    appendLine()
    append(alertText("수업 알림", alert))
}

/** 학원 수업 시간표 (최대 3곳) → 글자. */
fun academyShareText(list: List<ClassTable>, alert: AlertSetting): String = buildString {
    appendLine("📚 학원 수업 시간표")
    if (list.none { it.any }) appendLine("(등록된 수업 없음)")
    list.forEachIndexed { i, t ->
        if (!t.any && t.memo.isBlank()) return@forEachIndexed
        appendLine()
        appendLine("[학원 ${i + 1}]")
        memoLines(t.memo)
        classLines(t, "학원명")
    }
    appendLine()
    append(alertText("학원 알림", alert))
}

/** 기념일 → 글자: 등록한 것 모두, 다음 회까지 가까운 순. */
fun annivShareText(b: AnnivBook, today: LocalDate = LocalDate.now()): String = buildString {
    appendLine("🎂 기념일")
    val list = b.all.filter { it.date != null }.map { a ->
        val first = a.date!!
        val next = nextOccurrence(first, today)
        val left = ChronoUnit.DAYS.between(today, next)
        val nth = next.year - first.year
        val count = when {
            nth <= 0 -> ""
            a.type == AnnivType.Self || a.type == AnnivType.Spouse || a.type == AnnivType.Birthday -> "${nth}세"
            else -> "${nth}주년"
        }
        val whenText = if (left == 0L) "오늘" else "D-$left"
        val line = "${a.type.emoji} ${annivTitle(a)}: ${first.year}년 ${first.monthValue}월 ${first.dayOfMonth}일" +
            " → 다음 ${next.year}년 ${next.monthValue}월 ${next.dayOfMonth}일 ($whenText" + (if (count.isNotBlank()) ", $count" else "") + ")" +
            if (!first.isAfter(today)) " · D+%,d".format(ChronoUnit.DAYS.between(first, today)) else ""
        left to line
    }.sortedBy { it.first }
    if (list.isEmpty()) append("(등록된 기념일 없음)") else append(list.joinToString("\n") { it.second })
}

/** 약 먹을 시간 → 글자: 복용 기간, 먹는 시각, 날짜별 회차 상태. */
fun medShareText(context: Context, s: MedSchedule): String = buildString {
    fun t12(m: Int) = (if (m / 60 < 12) "오전" else "오후") + " ${hour12(m / 60)}:%02d".format(m % 60)
    val slots = MedSlot.entries.filter { s.slotOn[it.ordinal] }
    appendLine("💊 약 먹을 시간" + if (!s.active) " (꺼짐)" else "")
    memoLines(s.memo)
    val first = LocalDate.ofEpochDay(s.startDay)
    val last = LocalDate.ofEpochDay(s.lastDay())
    appendLine("복용 기간: ${s.days}일분 · ${dateText(first)} ~ ${dateText(last)}")
    appendLine("먹는 시각: " + slots.joinToString(" · ") { "${context.getString(it.label)} ${t12(s.minutes[it.ordinal])}" })
    val now = System.currentTimeMillis()
    for (day in s.startDay..s.lastDay()) {
        appendLine()
        appendLine(dateText(LocalDate.ofEpochDay(day)))
        slots.forEach { slot ->
            val key = com.example.clock.chime.doseKey(day, slot)
            val state = when {
                s.taken[key] != null -> "✓ ${takenTimeText(s.taken[key]!!)} 먹음"
                !s.isPlanned(day, slot) -> "계획 없음"
                s.doseTime(day, slot).toInstant().toEpochMilli() + MedScheduler.LATE_MS < now -> "✗ 안 먹음"
                else -> "예정"
            }
            appendLine(" · ${context.getString(slot.label)} ${t12(s.minutes[slot.ordinal])}  $state")
        }
    }
}.trimEnd()
