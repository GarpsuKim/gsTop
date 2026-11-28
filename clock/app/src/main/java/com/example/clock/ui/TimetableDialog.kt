package com.example.clock.ui

import android.app.TimePickerDialog
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CalendarViewWeek
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentPaste
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.TabRowDefaults
import androidx.compose.material3.TabRowDefaults.tabIndicatorOffset
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.verticalDrag
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.pointerInput
import kotlin.math.abs
import kotlin.math.roundToInt
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.example.clock.schedule.ACADEMY_COUNT
import com.example.clock.schedule.AlertItem
import com.example.clock.schedule.AlertSetting
import com.example.clock.schedule.Cell
import com.example.clock.schedule.ClassTable
import com.example.clock.schedule.DAY_NAMES
import com.example.clock.schedule.Period
import com.example.clock.schedule.ScheduleAlarm
import com.example.clock.schedule.ScheduleStore
import com.example.clock.schedule.Timetables
import com.example.clock.schedule.WorkDay
import com.example.clock.schedule.WorkSchedule
import com.example.clock.schedule.dayVisible
import com.example.clock.schedule.hhmm
import com.example.clock.schedule.visibleDays
import com.example.clock.schedule.scheduleStatus
import kotlinx.coroutines.delay
import java.time.ZonedDateTime

/** 시간표 창이 열려 있는지 ([시보·알람] → 시간표). */
object TimetableUi {
    var open by mutableStateOf(false)
}

// ── 색 (참고 그림의 파란 계열을 밝고 깔끔하게) ──
private val Teal = Color(0xFF17607F)      // 요일 머리줄·근무 막대
private val Sky = Color(0xFF129FD6)       // 장소·강의실·학원명 줄
private val Page = Color(0xFFF2F5F8)
private val CardBg = Color.White
private val Ink = Color(0xFF13293A)
private val SubInk = Color(0xFF5D7180)
private val Line = Color(0xFFD5DEE6)
private val ClassRed = Color(0xFFE53935)  // 수업 있음
private val EmptyBlue = Color(0xFFBBDEFB) // 수업 없음

/** [시보·알람] 메뉴 오른쪽 값: 지금 상황 앞부분 ("수업 중" 등) 또는 "끔". */
@Composable
fun timetableValue(): String {
    val context = LocalContext.current
    val t = remember(ScheduleStore.version) { ScheduleStore.load(context) }
    return scheduleStatus(t)?.now?.substringBefore(" · ")?.substringBefore("  ") ?: "끔"
}

/**
 * 시계 화면 왼쪽 위: 지금 상황 + 다음 일정 (1분마다 다시 계산). 시간표가 비어 있으면 아무것도 안 그림.
 *  근무 중 · 본사 (~18:00)
 *  다음: 3교시 영어 11:00 (25분 후)
 */
@Composable
fun ScheduleStatusText(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val t = remember(ScheduleStore.version) { ScheduleStore.load(context) }
    var now by remember { mutableStateOf(ZonedDateTime.now()) }
    LaunchedEffect(Unit) {
        while (true) {
            // 다음 정각 분까지 기다렸다가 다시 계산
            val n = ZonedDateTime.now()
            delay(((60 - n.second) * 1000L - n.nano / 1_000_000).coerceAtLeast(200L))
            now = ZonedDateTime.now()
        }
    }
    val st = scheduleStatus(t, now) ?: return
    Column(
        modifier
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0x66000000))
            .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(st.now, color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 2)
        st.next?.let { Text(it, color = Color(0xFFCFE3F0), fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis) }
    }
}

// ─────────────────────────────── 창 ───────────────────────────────

/** 시간표 창 (전체 화면): 위쪽 탭 [근무] [학교] [학원]. 바꾸는 즉시 저장되고 알림도 다시 맞춤. */
@Composable
fun TimetableDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var t by remember { mutableStateOf(ScheduleStore.load(context)) }
    fun update(n: Timetables) {
        t = n
        ScheduleStore.save(context, n)
    }
    var tab by remember { mutableIntStateOf(0) }
    var clip by remember { mutableStateOf<Cell?>(null) } // 칸 복사·붙여넣기 (학교·학원 공용)
    var resetAsk by remember { mutableStateOf<String?>(null) } // 초기화 확인: "work" / "school" / "a0" "a1" "a2"

    // 세로 화면으로만 (근무 요일 카드의 세로 슬라이더 배치에 맞춤)
    LockPortrait()

    // 알림 소리 파일 고르기 (어느 알림의 것인지 기억해 둠)
    var soundFor by remember { mutableStateOf<String?>(null) }
    val pickSound = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val f = soundFor ?: return@rememberLauncherForActivityResult
        if (uri == null) return@rememberLauncherForActivityResult
        runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        val name = queryDisplayName(context, uri)
        fun AlertSetting.withSound() = copy(soundUri = uri.toString(), soundName = name, sound = true)
        update(
            when (f) {
                "ws" -> t.copy(work = t.work.copy(startAlert = t.work.startAlert.withSound()))
                "we" -> t.copy(work = t.work.copy(endAlert = t.work.endAlert.withSound()))
                "school" -> t.copy(schoolAlert = t.schoolAlert.withSound())
                else -> t.copy(academyAlert = t.academyAlert.withSound())
            },
        )
    }
    fun pickFor(key: String) {
        soundFor = key
        pickSound.launch(arrayOf("audio/*"))
    }

    // 세로로 돌아간 뒤 창 크기가 가로 그대로 남지 않게: 화면 방향이 바뀌면 창을 새로 만듦 (내용 상태는 창 밖에 있어 그대로)
    androidx.compose.runtime.key(androidx.compose.ui.platform.LocalConfiguration.current.orientation) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
      // 이 창만 밝은 테마 (앱은 검은 테마라 흰 바탕에서 글자·버튼·칩이 흐리게 보이므로)
      MaterialTheme(colorScheme = TimetableColors) {
        Surface(Modifier.fillMaxSize(), color = Page, contentColor = Ink) {
            Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
                Row(Modifier.fillMaxWidth().background(CardBg).padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = onDismiss) { Icon(Icons.Filled.Close, contentDescription = null, tint = Ink) }
                    Icon(Icons.Filled.CalendarViewWeek, contentDescription = null, tint = Teal)
                    Spacer(Modifier.width(8.dp))
                    Text("시간표", fontSize = 21.sp, fontWeight = FontWeight.ExtraBold, color = Ink)
                }
                TabRow(
                    selectedTabIndex = tab,
                    containerColor = CardBg,
                    contentColor = Teal,
                    indicator = { pos -> TabRowDefaults.SecondaryIndicator(Modifier.tabIndicatorOffset(pos[tab]), color = Teal) },
                ) {
                    listOf("근무", "학교", "학원").forEachIndexed { i, name ->
                        Tab(selected = tab == i, onClick = { tab = i }, text = { Text(name, fontWeight = FontWeight.Bold, fontSize = 16.sp) })
                    }
                }
                // 탭마다 스크롤을 따로 (탭을 바꾸면 맨 위부터)
                val scroll = androidx.compose.runtime.key(tab) { rememberScrollState() }
                Column(
                    Modifier.fillMaxSize().verticalScroll(scroll).padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    when (tab) {
                        0 -> {
                            WorkTab(t.work, onChange = { update(t.copy(work = it)) }, onPickSound = ::pickFor)
                            ResetButton("초기화") { resetAsk = "work" }
                            ShareTextButton(Teal) { shareText(context, "근무 시간표", workShareText(t.work)) }
                        }
                        1 -> {
                            Card {
                                TitleWithWeekend(
                                    "학교 수업 시간표", t.school.sat, t.school.sun,
                                    onSat = { update(t.copy(school = t.school.copy(sat = it))) },
                                    onSun = { update(t.copy(school = t.school.copy(sun = it))) },
                                )
                                Memo(t.school.memo) { update(t.copy(school = t.school.copy(memo = it))) }
                                Hint("칸을 누르면 내용, 길게 누르면 편집·복사·붙여넣기. 교시를 누르면 시각을 바꿉니다. 빨강 = 수업, 파랑 = 빈 시간")
                                ClassGrid(t.school, rowName = "강의실", clip = clip, onCopy = { clip = it }, onChange = { update(t.copy(school = it)) })
                            }
                            Card {
                                AlertRow("수업 알림", t.schoolAlert, onChange = { update(t.copy(schoolAlert = it)) }, onPickSound = { pickFor("school") })
                            }
                            ResetButton("초기화") { resetAsk = "school" }
                            ShareTextButton(Teal) { shareText(context, "학교 수업 시간표", schoolShareText(t.school, t.schoolAlert)) }
                        }
                        else -> {
                            repeat(ACADEMY_COUNT) { i ->
                                Card {
                                    val a = t.academies[i]
                                    fun setA(n: ClassTable) = update(t.copy(academies = t.academies.mapIndexed { j, x -> if (j == i) n else x }))
                                    TitleWithWeekend("학원${i + 1}", a.sat, a.sun, onSat = { setA(a.copy(sat = it)) }, onSun = { setA(a.copy(sun = it)) })
                                    Memo(a.memo) { setA(a.copy(memo = it)) }
                                    ClassGrid(
                                        t.academies[i], rowName = "학원명", clip = clip, onCopy = { clip = it },
                                        onChange = { nt -> update(t.copy(academies = t.academies.mapIndexed { j, a -> if (j == i) nt else a })) },
                                    )
                                }
                            }
                            Card {
                                AlertRow("학원 알림", t.academyAlert, onChange = { update(t.copy(academyAlert = it)) }, onPickSound = { pickFor("academy") })
                            }
                            repeat(ACADEMY_COUNT) { i -> ResetButton("학원${i + 1} 초기화") { resetAsk = "a$i" } }
                            ShareTextButton(Teal) { shareText(context, "학원 수업 시간표", academyShareText(t.academies, t.academyAlert)) }
                        }
                    }
                    Spacer(Modifier.height(24.dp))
                }
            }
        }
        // 초기화 확인
        resetAsk?.let { k ->
            val what = when (k) {
                "work" -> "근무 시간표 (요일·장소·시각·출근/퇴근 알림)"
                "school" -> "학교 수업 시간표 (수업·강의실·교시 시각·수업 알림)"
                else -> "학원${k.drop(1).toInt() + 1} 시간표 (수업·학원명·교시 시각)"
            }
            AlertDialog(
                onDismissRequest = { resetAsk = null },
                title = { Text("초기화") },
                text = { Text("$what 을(를) 처음 상태로 되돌릴까요?") },
                confirmButton = {
                    TextButton(onClick = {
                        val d = Timetables()
                        update(
                            when (k) {
                                "work" -> t.copy(work = d.work)
                                "school" -> t.copy(school = d.school, schoolAlert = d.schoolAlert)
                                else -> { val i = k.drop(1).toInt(); t.copy(academies = t.academies.mapIndexed { j, a -> if (j == i) d.academies[i] else a }) }
                            },
                        )
                        resetAsk = null
                    }) { Text("초기화", color = ClassRed, fontWeight = FontWeight.Bold) }
                },
                dismissButton = { TextButton(onClick = { resetAsk = null }) { Text("취소") } },
            )
        }
      }
    }
    }
}

/** 빨간 [초기화] 버튼 (탭 맨 아래, 누르면 확인 창). */
@Composable
private fun ResetButton(text: String, onClick: () -> Unit) {
    androidx.compose.material3.Button(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().height(48.dp),
        colors = androidx.compose.material3.ButtonDefaults.buttonColors(containerColor = ClassRed, contentColor = Color.White),
    ) {
        Icon(Icons.Filled.RestartAlt, contentDescription = null)
        Spacer(Modifier.width(6.dp))
        Text(text, fontSize = 16.sp, fontWeight = FontWeight.Bold)
    }
}

/** 시간표 창 색 (밝은 바탕 + 청록 강조). */
private val TimetableColors = lightColorScheme(
    primary = Teal,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFD3E8F2),
    onPrimaryContainer = Ink,
    secondaryContainer = Color(0xFFD3E8F2),   // 고른 칩 바탕
    onSecondaryContainer = Teal,
    surface = Color.White,
    onSurface = Ink,
    onSurfaceVariant = SubInk,
    surfaceVariant = Color(0xFFE8EDF2),
    outline = Color(0xFF9FB2BF),
    background = Page,
    onBackground = Ink,
)

@Composable
private fun Card(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(CardBg).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        content = content,
    )
}

@Composable
private fun Title(text: String) = Text(text, fontSize = 17.sp, fontWeight = FontWeight.ExtraBold, color = Ink)

@Composable
private fun Hint(text: String) = Text(text, fontSize = 12.sp, color = SubInk)

/**
 * 제목 + 오른쪽 [토] [일] 버튼 (따로 켜고 끔). 켜면 그 요일도 표·카드에 나오고 일정·알림에 들어감.
 * 기본은 둘 다 꺼짐 = 월~금만. 켜진 버튼은 진한 청록 + ✓, 꺼진 버튼은 옅은 회색.
 */
@Composable
private fun TitleWithWeekend(text: String, sat: Boolean, sun: Boolean, onSat: (Boolean) -> Unit, onSun: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(text, fontSize = 17.sp, fontWeight = FontWeight.ExtraBold, color = Ink, modifier = Modifier.weight(1f))
        OptionChip("토", sat) { onSat(!sat) }
        Spacer(Modifier.width(6.dp))
        OptionChip("일", sun) { onSun(!sun) }
    }
}

/** 제목 바로 밑 메모 / 참고사항. */
@Composable
private fun Memo(value: String, onChange: (String) -> Unit) = MemoField(value, onChange, ink = Ink, accent = Teal)

// ─────────────────────────────── 근무 ───────────────────────────────

/** 시각 고르기 (24시간제). [step15] 면 15분 단위로 맞춤. */
private fun pickTime(context: android.content.Context, minutes: Int, step15: Boolean, onPick: (Int) -> Unit) {
    val m = ((minutes % 1440) + 1440) % 1440
    TimePickerDialog(context, { _, h, mm ->
        var v = h * 60 + mm
        if (step15) v = (v + 7) / 15 * 15
        onPick(v)
    }, m / 60, m % 60, true).show()
}

@Composable
private fun WorkTab(w: WorkSchedule, onChange: (WorkSchedule) -> Unit, onPickSound: (String) -> Unit) {
    Card {
        TitleWithWeekend("근무 시간표", w.sat, w.sun, onSat = { onChange(w.copy(sat = it)) }, onSun = { onChange(w.copy(sun = it)) })
        Memo(w.memo) { onChange(w.copy(memo = it)) }
        WeekOverview(w)
    }
    Card {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("매일 같은 시각", fontSize = 16.sp, fontWeight = FontWeight.Bold)
                Hint("켜면 한 요일의 시각을 바꿀 때 다른 요일도 같이 바뀝니다")
            }
            Switch(checked = w.sameEveryDay, onCheckedChange = { onChange(w.copy(sameEveryDay = it)) }, colors = SwitchDefaults.colors(checkedTrackColor = Teal))
        }
    }
    // 꺼진 토·일 카드는 숨김
    visibleDays(w.sat, w.sun).forEach { i ->
        val d = w.days[i]
        WorkDayCard(i, d) { nd ->
            val times = nd.start != d.start || nd.end != d.end
            onChange(
                w.copy(
                    days = w.days.mapIndexed { j, x ->
                        when {
                            j == i -> nd
                            w.sameEveryDay && times -> x.copy(start = nd.start, end = nd.end)
                            else -> x
                        }
                    },
                ),
            )
        }
    }
    Card {
        AlertRow("출근 알림", w.startAlert, onChange = { onChange(w.copy(startAlert = it)) }, onPickSound = { onPickSound("ws") })
        Spacer(Modifier.height(4.dp).fillMaxWidth().background(Line))
        AlertRow("퇴근 알림", w.endAlert, onChange = { onChange(w.copy(endAlert = it)) }, onPickSound = { onPickSound("we") })
    }
}

/**
 * 일주일 한눈에 보기: 일~토 × 0~24시. 근무 시간은 진한 막대, 밤샘 근무는 다음 날 칸 위쪽으로 이어짐 (토 → 일).
 * 위쪽 두 줄: 요일 / 근무 장소.
 */
@Composable
private fun WeekOverview(w: WorkSchedule) {
    val labelW = 34.dp
    val shown = visibleDays(w.sat, w.sun)
    Row(Modifier.fillMaxWidth()) {
        Spacer(Modifier.width(labelW))
        shown.forEach { HeadCell(DAY_NAMES[it], Teal, Modifier.weight(1f)) }
    }
    Row(Modifier.fillMaxWidth()) {
        Box(Modifier.width(labelW))
        shown.map { w.days[it] }.forEach { d -> HeadCell(if (d.on) d.place.ifBlank { "근무" } else "-", Sky, Modifier.weight(1f), small = true) }
    }
    val h = 240.dp
    Row(Modifier.fillMaxWidth().height(h)) {
        // 시각 눈금
        Box(Modifier.width(labelW).fillMaxHeight()) {
            listOf(0, 6, 12, 18, 24).forEach { hr ->
                Text(
                    "%02d".format(hr), fontSize = 10.sp, color = SubInk,
                    modifier = Modifier.offset(y = h * (hr / 24f) - if (hr == 24) 12.dp else if (hr == 0) 0.dp else 6.dp),
                )
            }
        }
        shown.forEach { i ->
            BoxWithConstraints(
                Modifier.weight(1f).fillMaxHeight().padding(horizontal = 1.dp).clip(RoundedCornerShape(6.dp)).background(Color(0xFFE8EDF2)),
            ) {
                val colH = maxHeight
                // 6시간마다 옅은 줄
                listOf(6, 12, 18).forEach { hr ->
                    Box(Modifier.fillMaxWidth().height(1.dp).offset(y = colH * (hr / 24f)).background(Line))
                }
                val today = w.days[i]
                // 전날이 숨겨진 주말이면 밤샘 근무도 없음
                val prev = w.days[(i + 6) % 7].let { if (dayVisible((i + 6) % 7, w.sat, w.sun)) it else it.copy(on = false) }
                val parts = mutableListOf<Pair<Int, Int>>()
                if (today.on) parts += today.start to minOf(today.end, 1440)
                if (prev.on && prev.end > 1440) parts += 0 to minOf(prev.end - 1440, 1440)
                parts.forEach { (s, e) ->
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 3.dp)
                            .offset(y = colH * (s / 1440f))
                            .height(colH * ((e - s).coerceAtLeast(10) / 1440f))
                            .clip(RoundedCornerShape(4.dp))
                            .background(Teal),
                    )
                }
            }
        }
    }
}

@Composable
private fun HeadCell(text: String, bg: Color, modifier: Modifier, small: Boolean = false) {
    Box(
        modifier.padding(1.dp).clip(RoundedCornerShape(6.dp)).background(bg).padding(vertical = if (small) 4.dp else 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = Color.White, fontSize = if (small) 11.sp else 14.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Clip)
    }
}

/**
 * 요일 하나 (세로 화면):
 *  [켜기] 일요일  [근무 장소 ____]
 *   ┃0시          출근  09:00      ← 시각을 누르면 시각 고르기
 *   ●             퇴근  18:00
 *   ┃24시               9시간
 *   ●
 *   ┃내일 24시
 * 슬라이더는 카드 왼쪽에 위아래로 (위 = 0시, 아래 = 다음 날 24시, 15분 간격).
 * 손잡이(●)를 잡아야만 움직임: 오른손으로 화면을 스크롤하다 닿아도 바뀌지 않게.
 * 꺼진 요일은 흐리게 + 슬라이더 잠금. 장소를 적거나 시각을 고르면 그 요일이 저절로 켜짐.
 */
@Composable
private fun WorkDayCard(i: Int, d: WorkDay, onChange: (WorkDay) -> Unit) {
    val context = LocalContext.current
    Card {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = d.on, onCheckedChange = { onChange(d.copy(on = it)) }, colors = SwitchDefaults.colors(checkedTrackColor = Teal))
            Spacer(Modifier.width(10.dp))
            Text("${DAY_NAMES[i]}요일", fontSize = 17.sp, fontWeight = FontWeight.ExtraBold, color = if (d.on) Ink else SubInk)
            Spacer(Modifier.width(10.dp))
            var place by remember(d.place) { mutableStateOf(d.place) }
            OutlinedTextField(
                value = place,
                // 꺼진 요일도 바로 입력 가능: 장소를 적으면 그 요일이 저절로 켜짐
                onValueChange = { place = it; onChange(d.copy(place = it, on = d.on || it.isNotBlank())) },
                placeholder = { Text("근무 장소", fontSize = 14.sp) },
                singleLine = true,
                modifier = Modifier.weight(1f),
                textStyle = androidx.compose.ui.text.TextStyle(fontSize = 15.sp),
            )
        }
        val onDay = d.copy(on = true)
        Row(Modifier.fillMaxWidth().height(SliderHeight).alpha(if (d.on) 1f else 0.45f)) {
            VerticalTimeSlider(
                start = d.start, end = d.end, enabled = d.on,
                onChange = { s, e -> onChange(d.copy(start = s, end = e)) },
                modifier = Modifier.width(40.dp).fillMaxHeight(),
            )
            SliderLabels(Modifier.width(84.dp).fillMaxHeight())
            // 슬라이더 오른쪽: 1줄 시작 시각, 2줄 종료 시각, 3줄 근무 시간
            Column(
                Modifier.weight(1f).fillMaxHeight(),
                verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("출근", fontSize = 14.sp, color = SubInk, softWrap = false)
                    Spacer(Modifier.width(8.dp))
                    TimeChip(hhmm(d.start)) { pickTime(context, d.start, true) { v -> onChange(onDay.copy(start = v, end = if (d.end <= v) v + (d.end - d.start).coerceAtLeast(15) else d.end)) } }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("퇴근", fontSize = 14.sp, color = SubInk, softWrap = false)
                    Spacer(Modifier.width(8.dp))
                    TimeChip((if (d.end > 1440) "내일 " else "") + hhmm(d.end)) {
                        pickTime(context, d.end, true) { v -> onChange(onDay.copy(end = if (v <= d.start) v + 1440 else v)) }
                    }
                }
                val len = d.end - d.start
                Text(
                    "${len / 60}시간" + if (len % 60 != 0) " ${len % 60}분" else "",
                    fontSize = 16.sp, fontWeight = FontWeight.Bold, color = Ink, softWrap = false,
                )
            }
        }
    }
}

private val SliderHeight = 260.dp
private val ThumbPad = 20.dp      // 맨 위·아래 손잡이가 잘리지 않게 남기는 여백
private const val SLIDER_MAX = 2880 // 0시 ~ 다음 날 24시 (분)

/** 슬라이더 옆 눈금 글자: 0시 / 12시 / 24시 / 다음날 12시 / 다음날 24시. */
@Composable
private fun SliderLabels(modifier: Modifier) {
    BoxWithConstraints(modifier) {
        val track = maxHeight - ThumbPad * 2
        listOf(0 to "0시", 720 to "12시", 1440 to "24시", 2160 to "내일 12시", 2880 to "내일 24시").forEach { (m, label) ->
            Text(
                label, fontSize = 11.sp, color = SubInk, maxLines = 1, softWrap = false,
                fontWeight = if (m == 1440) FontWeight.Bold else FontWeight.Normal,
                modifier = Modifier.offset(y = ThumbPad + track * (m / SLIDER_MAX.toFloat()) - 11.dp),
            )
        }
    }
}

/**
 * 세로 범위 슬라이더 (15분 간격). 위 = 0시, 아래 = 다음 날 24시.
 * 손잡이 근처를 눌러 끌 때만 움직이고, 그 밖을 누르면 화면 스크롤로 넘김.
 */
@Composable
private fun VerticalTimeSlider(start: Int, end: Int, enabled: Boolean, onChange: (Int, Int) -> Unit, modifier: Modifier) {
    val cur by rememberUpdatedState(start to end)
    val on by rememberUpdatedState(enabled)
    val cb by rememberUpdatedState(onChange)
    Canvas(
        modifier.pointerInput(Unit) {
            val pad = ThumbPad.toPx()
            fun yOf(m: Int) = pad + (size.height - 2 * pad) * m / SLIDER_MAX.toFloat()
            fun minutesAt(y: Float) = (((y - pad) / (size.height - 2 * pad) * 192f).roundToInt().coerceIn(0, 192)) * 15
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                if (!on) return@awaitEachGesture
                val (s0, e0) = cur
                val ds = abs(down.position.y - yOf(s0))
                val de = abs(down.position.y - yOf(e0))
                if (minOf(ds, de) > 30.dp.toPx()) return@awaitEachGesture // 손잡이가 아니면 스크롤에 양보
                // 두 손잡이가 겹쳐 있으면 아래쪽(퇴근)을 잡음
                val grabEnd = de <= ds
                down.consume()
                verticalDrag(down.id) { ch ->
                    ch.consume()
                    val m = minutesAt(ch.position.y)
                    val (s, e) = cur
                    if (grabEnd) {
                        val ne = m.coerceIn(s + 15, s + 1440)
                        if (ne != e) cb(s, ne)
                    } else {
                        val ns = m.coerceIn(maxOf(0, e - 1440), minOf(1425, e - 15))
                        if (ns != s) cb(ns, e)
                    }
                }
            }
        },
    ) {
        val pad = ThumbPad.toPx()
        val cx = size.width / 2
        fun yOf(m: Int) = pad + (size.height - 2 * pad) * m / SLIDER_MAX.toFloat()
        // 전체 막대 (옅게) + 12시간마다 눈금 (24시는 길게)
        drawLine(Color(0xFFD5E3EC), Offset(cx, pad), Offset(cx, size.height - pad), strokeWidth = 8.dp.toPx(), cap = StrokeCap.Round)
        listOf(0, 720, 1440, 2160, 2880).forEach { m ->
            val w = if (m == 1440) 14.dp.toPx() else 8.dp.toPx()
            drawLine(Color(0xFF9FB2BF), Offset(cx - w, yOf(m)), Offset(cx + w, yOf(m)), strokeWidth = 1.5.dp.toPx())
        }
        // 근무 구간 (진하게)
        drawLine(Teal, Offset(cx, yOf(start)), Offset(cx, yOf(end)), strokeWidth = 10.dp.toPx(), cap = StrokeCap.Round)
        // 손잡이 두 개
        listOf(start, end).forEach { m ->
            drawCircle(Teal, radius = 12.dp.toPx(), center = Offset(cx, yOf(m)))
            drawCircle(Color.White, radius = 4.5.dp.toPx(), center = Offset(cx, yOf(m)))
        }
    }
}

@Composable
private fun TimeChip(text: String, onClick: () -> Unit) {
    Text(
        text, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = Teal, maxLines = 1, softWrap = false,
        modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(Color(0xFFE3F0F6)).clickable(onClick = onClick).padding(horizontal = 10.dp, vertical = 4.dp),
    )
}

// ─────────────────────────────── 알림 줄 ───────────────────────────────

/** "출근 알림  [켜기]   [−] 10분 전 [+]" / ☑알림 ☑팝업창 ☑소리 [파일…] [테스트]. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AlertRow(label: String, a: AlertSetting, onChange: (AlertSetting) -> Unit, onPickSound: () -> Unit) {
    val context = LocalContext.current
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, fontSize = 16.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        Switch(checked = a.enabled, onCheckedChange = { onChange(a.copy(enabled = it)) }, colors = SwitchDefaults.colors(checkedTrackColor = Teal))
    }
    if (!a.enabled) return
    Row(verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = { onChange(a.copy(minutesBefore = (a.minutesBefore - 5).coerceAtLeast(0))) }) { Icon(Icons.Filled.Remove, null, tint = Teal) }
        Text("${a.minutesBefore}분 전", fontSize = 17.sp, fontWeight = FontWeight.Bold)
        IconButton(onClick = { onChange(a.copy(minutesBefore = (a.minutesBefore + 5).coerceAtMost(180))) }) { Icon(Icons.Filled.Add, null, tint = Teal) }
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        // 고른 것 = 진한 청록 + 흰 글자 + ✓,  안 고른 것 = 옅은 회색 + 흐린 글자
        OptionChip("알림", a.notify) { onChange(a.copy(notify = !a.notify)) }
        OptionChip("팝업창", a.popup) { onChange(a.copy(popup = !a.popup)) }
        OptionChip("소리", a.sound) { onChange(a.copy(sound = !a.sound)) }
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(a.soundName.ifBlank { "기본 알림음" }, fontSize = 13.sp, color = SubInk, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
        TextButton(onClick = onPickSound) { Text("파일…") }
        if (a.soundUri.isNotBlank()) TextButton(onClick = { onChange(a.copy(soundUri = "", soundName = "")) }) { Text("기본음") }
        TextButton(onClick = { ScheduleAlarm.fire(context, AlertItem("test", System.currentTimeMillis(), a, "$label 테스트 · ${a.minutesBefore}분 전 알림")) }) {
            Text("테스트")
        }
    }
}

@Composable
private fun OptionChip(text: String, selected: Boolean, onClick: () -> Unit) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        label = { Text(text, fontSize = 15.sp, fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal) },
        leadingIcon = if (selected) { { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) } } else null,
        colors = androidx.compose.material3.FilterChipDefaults.filterChipColors(
            containerColor = Color(0xFFEEF2F5),
            labelColor = Color(0xFF9AABB7),
            selectedContainerColor = Teal,
            selectedLabelColor = Color.White,
            selectedLeadingIconColor = Color.White,
        ),
        border = androidx.compose.material3.FilterChipDefaults.filterChipBorder(
            enabled = true, selected = selected,
            borderColor = Color(0xFFDDE4EA), selectedBorderColor = Teal,
        ),
    )
}

// ─────────────────────────────── 수업 시간표 표 ───────────────────────────────

/**
 * 수업 시간표 표: 머리줄(요일) / [rowName] 줄(강의실·학원명) / 교시 줄들.
 * 칸: 수업 있음 = 빨강 + 과목명 3글자, 빈 칸 = 파랑. 누르면 내용 팝업, 길게 누르면 편집·복사·붙여넣기·지우기.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ClassGrid(table: ClassTable, rowName: String, clip: Cell?, onCopy: (Cell) -> Unit, onChange: (ClassTable) -> Unit) {
    var info by remember { mutableStateOf<Pair<Int, Int>?>(null) }   // 내용 팝업 (교시, 요일)
    var edit by remember { mutableStateOf<Pair<Int, Int>?>(null) }   // 편집 창
    var menu by remember { mutableStateOf<Pair<Int, Int>?>(null) }   // 길게 누르기 메뉴
    var periodEdit by remember { mutableStateOf<Int?>(null) }
    var dayNameEdit by remember { mutableStateOf<Int?>(null) }
    val labelWeight = 1.75f // 세로 화면에서도 "아침자습" · "17:00" 이 잘리지 않게

    val shown = visibleDays(table.sat, table.sun) // 토·일을 끄면 월~금
    Row(Modifier.fillMaxWidth()) {
        HeadCell("요일", Teal, Modifier.weight(labelWeight))
        shown.forEach { HeadCell(DAY_NAMES[it], Teal, Modifier.weight(1f)) }
    }
    Row(Modifier.fillMaxWidth()) {
        HeadCell(rowName, Sky, Modifier.weight(labelWeight), small = true)
        shown.forEach { d ->
            val name = table.dayNames[d]
            Box(Modifier.weight(1f).clickable { dayNameEdit = d }) { HeadCell(name.ifBlank { "+" }.take(3), Sky, Modifier.fillMaxWidth(), small = true) }
        }
    }
    table.periods.forEachIndexed { p, pr ->
        Row(Modifier.fillMaxWidth().height(56.dp)) {
            // 교시 이름 + 시작 시각 (누르면 시각 바꾸기)
            Column(
                Modifier.weight(labelWeight).fillMaxHeight().padding(1.dp).clip(RoundedCornerShape(6.dp)).background(Color(0xFFE8EDF2))
                    .clickable { periodEdit = p },
                horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center,
            ) {
                // 한 글자 덩어리로 (큰 글씨 폰에서도 시각 줄이 잘리지 않게)
                Text(
                    "${pr.name}\n${hhmm(pr.start)}", fontSize = 10.sp, lineHeight = 12.sp, fontWeight = FontWeight.Bold,
                    color = Ink, textAlign = TextAlign.Center, maxLines = 2, softWrap = false,
                )
            }
            shown.forEach { d ->
                val c = table.cells[p][d]
                Box(Modifier.weight(1f).fillMaxHeight()) {
                    Box(
                        Modifier.fillMaxSize().padding(1.dp).clip(RoundedCornerShape(6.dp))
                            .background(if (c.hasClass) ClassRed else EmptyBlue)
                            .combinedClickable(
                                onClick = { if (c.isEmpty) edit = p to d else info = p to d },
                                onLongClick = { menu = p to d },
                            ),
                        contentAlignment = Alignment.Center,
                    ) {
                        if (c.hasClass) Text(c.subject.take(3), color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.Bold, maxLines = 1, textAlign = TextAlign.Center)
                    }
                    DropdownMenu(expanded = menu == (p to d), onDismissRequest = { menu = null }) {
                        DropdownMenuItem(text = { Text("편집") }, leadingIcon = { Icon(Icons.Filled.Edit, null) }, onClick = { menu = null; edit = p to d })
                        DropdownMenuItem(text = { Text("복사") }, leadingIcon = { Icon(Icons.Filled.ContentCopy, null) }, enabled = !c.isEmpty, onClick = { menu = null; onCopy(c) })
                        DropdownMenuItem(
                            text = { Text("붙여넣기" + (clip?.subject?.let { " ($it)" } ?: "")) }, leadingIcon = { Icon(Icons.Filled.ContentPaste, null) },
                            enabled = clip != null, onClick = { menu = null; clip?.let { onChange(table.withCell(p, d, it)) } },
                        )
                        DropdownMenuItem(text = { Text("지우기") }, leadingIcon = { Icon(Icons.Filled.Delete, null) }, enabled = !c.isEmpty, onClick = { menu = null; onChange(table.withCell(p, d, Cell())) })
                    }
                }
            }
        }
    }

    // 내용 팝업 ("마우스 호버" 대신: 칸을 누르면)
    info?.let { (p, d) ->
        val c = table.cells[p][d]
        val pr = table.periods[p]
        AlertDialog(
            onDismissRequest = { info = null },
            title = { Text(c.subject.ifBlank { "빈 시간" }) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("${DAY_NAMES[d]}요일 ${pr.name} · ${hhmm(pr.start)} ~ ${hhmm(pr.end)}")
                    if (c.room.isNotBlank() || table.dayNames[d].isNotBlank()) Text("$rowName: ${c.room.ifBlank { table.dayNames[d] }}")
                    if (c.teacher.isNotBlank()) Text("교수: ${c.teacher}")
                }
            },
            confirmButton = { TextButton(onClick = { info = null; edit = p to d }) { Text("편집") } },
            dismissButton = { TextButton(onClick = { info = null }) { Text("닫기") } },
        )
    }
    edit?.let { (p, d) ->
        CellEditDialog(
            title = "${DAY_NAMES[d]}요일 ${table.periods[p].name}",
            cell = table.cells[p][d],
            onSave = { onChange(table.withCell(p, d, it)); edit = null },
            onDismiss = { edit = null },
        )
    }
    periodEdit?.let { p ->
        PeriodEditDialog(table.periods[p], onSave = { onChange(table.withPeriod(p, it)); periodEdit = null }, onDismiss = { periodEdit = null })
    }
    dayNameEdit?.let { d ->
        TextEditDialog(
            title = "${DAY_NAMES[d]}요일 $rowName", value = table.dayNames[d],
            onSave = { onChange(table.withDayName(d, it)); dayNameEdit = null }, onDismiss = { dayNameEdit = null },
        )
    }
}

@Composable
private fun CellEditDialog(title: String, cell: Cell, onSave: (Cell) -> Unit, onDismiss: () -> Unit) {
    var subject by remember { mutableStateOf(cell.subject) }
    var room by remember { mutableStateOf(cell.room) }
    var teacher by remember { mutableStateOf(cell.teacher) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(subject, { subject = it }, label = { Text("과목명") }, singleLine = true)
                OutlinedTextField(room, { room = it }, label = { Text("강의실") }, singleLine = true)
                OutlinedTextField(teacher, { teacher = it }, label = { Text("교수명") }, singleLine = true)
            }
        },
        confirmButton = { TextButton(onClick = { onSave(Cell(subject.trim(), room.trim(), teacher.trim())) }) { Text("저장") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("취소") } },
    )
}

@Composable
private fun PeriodEditDialog(pr: Period, onSave: (Period) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var start by remember { mutableIntStateOf(pr.start) }
    var end by remember { mutableIntStateOf(pr.end) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("${pr.name} 시각") },
        text = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                TimeChip(hhmm(start)) { pickTime(context, start, false) { start = it; if (end <= it) end = it + 50 } }
                Text("  ~  ", fontSize = 16.sp)
                TimeChip(hhmm(end)) { pickTime(context, end, false) { if (it > start) end = it } }
            }
        },
        confirmButton = { TextButton(onClick = { onSave(pr.copy(start = start, end = end)) }) { Text("저장") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("취소") } },
    )
}

@Composable
private fun TextEditDialog(title: String, value: String, onSave: (String) -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf(value) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { OutlinedTextField(text, { text = it }, singleLine = true) },
        confirmButton = { TextButton(onClick = { onSave(text.trim()) }) { Text("저장") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("취소") } },
    )
}
