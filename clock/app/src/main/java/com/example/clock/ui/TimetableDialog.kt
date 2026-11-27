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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.SliderDefaults
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
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
import com.example.clock.schedule.hhmm
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
                        0 -> WorkTab(t.work, onChange = { update(t.copy(work = it)) }, onPickSound = ::pickFor)
                        1 -> {
                            Card {
                                Title("학교 수업 시간표")
                                Hint("칸을 누르면 내용, 길게 누르면 편집·복사·붙여넣기. 교시를 누르면 시각을 바꿉니다. 빨강 = 수업, 파랑 = 빈 시간")
                                ClassGrid(t.school, rowName = "강의실", clip = clip, onCopy = { clip = it }, onChange = { update(t.copy(school = it)) })
                            }
                            Card {
                                AlertRow("수업 알림", t.schoolAlert, onChange = { update(t.copy(schoolAlert = it)) }, onPickSound = { pickFor("school") })
                            }
                        }
                        else -> {
                            repeat(ACADEMY_COUNT) { i ->
                                Card {
                                    Title("학원 ${i + 1}")
                                    ClassGrid(
                                        t.academies[i], rowName = "학원명", clip = clip, onCopy = { clip = it },
                                        onChange = { nt -> update(t.copy(academies = t.academies.mapIndexed { j, a -> if (j == i) nt else a })) },
                                    )
                                }
                            }
                            Card {
                                AlertRow("학원 알림", t.academyAlert, onChange = { update(t.copy(academyAlert = it)) }, onPickSound = { pickFor("academy") })
                            }
                        }
                    }
                    Spacer(Modifier.height(24.dp))
                }
            }
        }
      }
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
        Title("근무 시간표")
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
    w.days.forEachIndexed { i, d ->
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
    Row(Modifier.fillMaxWidth()) {
        Spacer(Modifier.width(labelW))
        DAY_NAMES.forEach { HeadCell(it, Teal, Modifier.weight(1f)) }
    }
    Row(Modifier.fillMaxWidth()) {
        Box(Modifier.width(labelW))
        w.days.forEach { d -> HeadCell(if (d.on) d.place.ifBlank { "근무" } else "-", Sky, Modifier.weight(1f), small = true) }
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
        DAY_NAMES.indices.forEach { i ->
            BoxWithConstraints(
                Modifier.weight(1f).fillMaxHeight().padding(horizontal = 1.dp).clip(RoundedCornerShape(6.dp)).background(Color(0xFFE8EDF2)),
            ) {
                val colH = maxHeight
                // 6시간마다 옅은 줄
                listOf(6, 12, 18).forEach { hr ->
                    Box(Modifier.fillMaxWidth().height(1.dp).offset(y = colH * (hr / 24f)).background(Line))
                }
                val today = w.days[i]
                val prev = w.days[(i + 6) % 7]
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
 * 요일 하나: [켜기] 일요일  [근무 장소 ____]
 *            09:00 ~ 18:00 (9시간)   ← 시각을 누르면 시각 고르기
 *            ●━━━━━━━━━●            ← 15분 간격 타임 슬라이더 (0시 ~ 다음 날 24시)
 */
@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
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
                onValueChange = { place = it; onChange(d.copy(place = it)) },
                placeholder = { Text("근무 장소", fontSize = 14.sp) },
                singleLine = true,
                enabled = d.on,
                modifier = Modifier.weight(1f),
                textStyle = androidx.compose.ui.text.TextStyle(fontSize = 15.sp),
            )
        }
        if (d.on) {
            // 개시·종료 시각 글자 (누르면 시각 고르기)
            Row(verticalAlignment = Alignment.CenterVertically) {
                TimeChip(hhmm(d.start)) { pickTime(context, d.start, true) { v -> onChange(d.copy(start = v, end = if (d.end <= v) v + (d.end - d.start).coerceAtLeast(15) else d.end)) } }
                Text("  ~  ", fontSize = 16.sp, color = SubInk)
                TimeChip((if (d.end > 1440) "다음날 " else "") + hhmm(d.end)) {
                    pickTime(context, d.end, true) { v -> onChange(d.copy(end = if (v <= d.start) v + 1440 else v)) }
                }
                Spacer(Modifier.weight(1f))
                val len = d.end - d.start
                Text("${len / 60}시간" + if (len % 60 != 0) " ${len % 60}분" else "", fontSize = 13.sp, color = SubInk)
            }
            // 타임 슬라이더: 0 ~ 192칸 (15분 × 192 = 48시간 = 오늘 0시 ~ 다음 날 24시)
            RangeSlider(
                value = (d.start / 15f)..(d.end / 15f),
                onValueChange = { r ->
                    val s = (r.start.toInt()).coerceIn(0, 95) * 15
                    val e = (r.endInclusive.toInt() * 15).coerceIn(s + 15, s + 1440)
                    if (s != d.start || e != d.end) onChange(d.copy(start = s, end = e))
                },
                valueRange = 0f..192f,
                steps = 191,
                colors = SliderDefaults.colors(activeTrackColor = Teal, thumbColor = Teal, inactiveTrackColor = Color(0xFFD5E3EC), activeTickColor = Color.Transparent, inactiveTickColor = Color.Transparent),
            )
            Row {
                Text("0시", fontSize = 10.sp, color = SubInk)
                Spacer(Modifier.weight(1f))
                Text("24시", fontSize = 10.sp, color = SubInk)
                Spacer(Modifier.weight(1f))
                Text("다음날 24시", fontSize = 10.sp, color = SubInk)
            }
        }
    }
}

@Composable
private fun TimeChip(text: String, onClick: () -> Unit) {
    Text(
        text, fontSize = 17.sp, fontWeight = FontWeight.Bold, color = Teal,
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
        FilterChip(selected = a.notify, onClick = { onChange(a.copy(notify = !a.notify)) }, label = { Text("알림") })
        FilterChip(selected = a.popup, onClick = { onChange(a.copy(popup = !a.popup)) }, label = { Text("팝업창") })
        FilterChip(selected = a.sound, onClick = { onChange(a.copy(sound = !a.sound)) }, label = { Text("소리") })
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
    val labelWeight = 1.35f

    Row(Modifier.fillMaxWidth()) {
        HeadCell("요일", Teal, Modifier.weight(labelWeight))
        DAY_NAMES.forEach { HeadCell(it, Teal, Modifier.weight(1f)) }
    }
    Row(Modifier.fillMaxWidth()) {
        HeadCell(rowName, Sky, Modifier.weight(labelWeight), small = true)
        table.dayNames.forEachIndexed { d, name ->
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
                    "${pr.name}\n${hhmm(pr.start)}", fontSize = 11.sp, lineHeight = 13.sp, fontWeight = FontWeight.Bold,
                    color = Ink, textAlign = TextAlign.Center, maxLines = 2,
                )
            }
            (0 until 7).forEach { d ->
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
