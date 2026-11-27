package com.example.clock.ui

import android.Manifest
import android.app.Activity
import android.app.TimePickerDialog
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Bedtime
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.LunchDining
import androidx.compose.material.icons.filled.Medication
import androidx.compose.material.icons.filled.NightsStay
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.WbSunny
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.ContextCompat
import com.example.clock.R
import com.example.clock.chime.MedScheduler
import com.example.clock.chime.MedSchedule
import com.example.clock.chime.MedSlot
import com.example.clock.chime.MedStore
import com.example.clock.chime.doseKey
import com.example.clock.chime.hour12

import java.time.LocalDate
import java.time.format.TextStyle
import java.util.Locale

/**
 * 약 먹는 시간 창이 열려 있는지. [시보·알람] 메뉴나 약 알림을 누르면 true.
 * 창은 시계 화면이 띄우므로 버튼이 숨겨져 있어도 열림.
 */
object MedicineUi {
    var open by mutableStateOf(false)
}

// ── 색 (노란 바탕) ──
private val PageBg = Color(0xFFFFE873)      // 노란 바탕
private val CardBg = Color(0xFFFFFBEA)      // 카드: 크림색
private val Ink = Color(0xFF2B2410)         // 글자: 진한 갈색-검정
private val SubInk = Color(0xFF7A6A35)      // 보조 글자
private val Accent = Color(0xFF2E7D32)      // 먹음(체크): 초록
private val AccentSoft = Color(0xFFE3F2D9)
private val LateRed = Color(0xFFD32F2F)
private val LateSoft = Color(0xFFFDE3E1)
private val NextRing = Color(0xFFFF8F00)    // 다음에 먹을 회차 테두리: 주황
private val Dark = Color(0xFF2B2410)

private fun slotIcon(slot: MedSlot): ImageVector = when (slot) {
    MedSlot.Morning -> Icons.Filled.WbSunny
    MedSlot.Lunch -> Icons.Filled.LunchDining
    MedSlot.Evening -> Icons.Filled.NightsStay
    MedSlot.LateNight -> Icons.Filled.Bedtime
}

/** 시각 글자 "오전 7:30" / "오후 6:00". */
@Composable
private fun timeText(minutes: Int): String {
    val h = minutes / 60
    return stringResource(
        if (h < 12) R.string.med_am_time else R.string.med_pm_time,
        hour12(h), minutes % 60,
    )
}

/** [시보·알람] 메뉴 오른쪽 값: 다음에 먹을 때 "저녁 6:00", 저장 전이면 "끔". */
@Composable
fun medicineValue(): String {
    val context = LocalContext.current
    val s = remember { MedStore.load(context) }
    if (!s.active) return stringResource(R.string.chime_state_off)
    val next = nextDose(s) ?: return stringResource(R.string.chime_state_on)
    val t = s.doseTime(next.first, next.second)
    return stringResource(R.string.med_next, stringResource(next.second.label), hour12(t.hour), t.minute)
}

/** 다음에 먹을 회차 (계획됨, 안 먹음, 30분이 아직 안 지남). 오늘·내일 중에서. */
private fun nextDose(s: MedSchedule): Pair<Long, MedSlot>? {
    val now = System.currentTimeMillis()
    val today = LocalDate.now().toEpochDay()
    for (day in maxOf(s.startDay, today)..today + 1) {
        for (slot in MedSlot.entries) {
            if (!s.isPlanned(day, slot) || s.isTaken(day, slot)) continue
            if (s.doseTime(day, slot).toInstant().toEpochMilli() + MedScheduler.LATE_MS < now) continue
            return day to slot
        }
    }
    return null
}

/** 먹음 체크한 시각 → 자정부터 분 ("오후 4:08" 로 보이려고). */
private fun takenMinutes(ms: Long): Int =
    java.time.Instant.ofEpochMilli(ms).atZone(java.time.ZoneId.systemDefault()).let { it.hour * 60 + it.minute }

private tailrec fun Context.findActivityOrNull(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivityOrNull()
    else -> null
}

/**
 * 약 먹는 시간 관리 창 (전체 화면, 노란 바탕, 세로 고정).
 *  ┌ [✕]   약 먹는 시간            [저장] ┐   ← 위쪽 막대
 *  │ ┌ 오늘 ───────────────── 2 / 3 ┐ │   ← 오늘 카드: 진행 막대 + 다음 복용
 *  │ │ ████████░░░░  다음: 저녁 6:00 │ │
 *  │ └──────────────────────────────┘ │
 *  │ [☀ 아침 7:30] [🍔 점심 12:30] [🌙 저녁 6:00]  ← 시각 카드 (누르면 시각, 스위치로 끄기)
 *  │ 11월 27일 (금) · 오늘                │   ← 날짜별
 *  │ ┌ ☀ 아침  오전 7:30   1회차   (✓) ┐ │   ← 회차 카드: 큰 동그라미 = 먹음 체크 (체크 시각 표시)
 *  │ └  [계획 ◯] 를 끄면 이 회차만 건너뜀 ┘ │
 *  │ …                                   │
 *  └ 기록 초기화                          ┘
 * [저장] 해야 반영되고 알람이 맞춰짐. [✕]·[뒤로] 는 바꾼 것을 버리고 닫기.
 */
@Composable
fun MedicineDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var s by remember { mutableStateOf(MedStore.load(context)) }
    var confirmReset by remember { mutableStateOf(false) }

    // 이 창은 세로 화면으로만: 열려 있는 동안 세로로 고정, 닫으면 원래대로.
    // (MainActivity 가 회전을 직접 처리하므로 돌아가도 창이 닫히지 않음)
    DisposableEffect(Unit) {
        val activity = context.findActivityOrNull()
        val before = activity?.requestedOrientation
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        onDispose {
            if (activity != null && before != null) activity.requestedOrientation = before
        }
    }

    // 알림 권한 (Android 13+): 약 알림이 보이도록. 저장할 때 한 번 물음
    val notificationPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    fun save() {
        // 꺼져 있던 상태(처음, 또는 기록 초기화 뒤)에서 기록 없이 켜면 저장하는 날이 1회차
        val start = if (!s.active && s.taken.isEmpty()) LocalDate.now().toEpochDay() else s.startDay
        MedStore.save(context, s.copy(active = true, startDay = start))
        MedScheduler.update(context)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        onDismiss()
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        Surface(Modifier.fillMaxSize(), color = PageBg, contentColor = Ink) {
            Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
                TopBar(onClose = onDismiss, onSave = ::save)
                // 타이틀 바로 밑: 약 복용 기간 [−] 7 [+] 일분 (계획은 하루 더)
                PeriodBar(s, onChange = { s = it })
                DoseList(
                    s = s,
                    onChange = { s = it },
                    onReset = { confirmReset = true },
                    modifier = Modifier.weight(1f),
                )
            }
        }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.med_reset_title)) },
            text = { Text(stringResource(R.string.med_reset_desc)) },
            confirmButton = {
                TextButton(onClick = {
                    // 기록 초기화 = 약 먹을 시간 끔: 바로 저장하고 알람·알림도 끔 (다시 쓰려면 [저장])
                    s = s.copy(
                        active = false,
                        startDay = LocalDate.now().toEpochDay(),
                        skipped = emptySet(),
                        taken = emptyMap(),
                    )
                    MedStore.save(context, s)
                    MedScheduler.update(context) // active=false → 확인 알람 취소
                    confirmReset = false
                }) { Text(stringResource(R.string.med_reset), color = LateRed) }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.med_cancel)) }
            },
        )
    }
}

/** 위쪽 막대: [✕]  💊 약 먹는 시간  [저장]. */
@Composable
private fun TopBar(onClose: () -> Unit, onSave: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(start = 4.dp, end = 12.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onClose) {
            Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.med_cancel), tint = Ink)
        }
        Icon(Icons.Filled.Medication, contentDescription = null, tint = Ink, modifier = Modifier.size(24.dp))
        Spacer(Modifier.width(6.dp))
        Text(
            stringResource(R.string.med_title),
            fontSize = 21.sp,
            fontWeight = FontWeight.ExtraBold,
            color = Ink,
            modifier = Modifier.weight(1f),
        )
        Button(
            onClick = onSave,
            colors = ButtonDefaults.buttonColors(containerColor = Dark, contentColor = Color.White),
            shape = RoundedCornerShape(50),
            contentPadding = PaddingValues(horizontal = 22.dp, vertical = 8.dp),
        ) { Text(stringResource(R.string.med_save), fontWeight = FontWeight.Bold) }
    }
}

/**
 * 약 복용 기간: "약 복용 기간  [−] 7 [+] 일분" + "11월 27일 ~ 12월 4일 (금) · 8일 계획".
 * 숫자를 누르면 직접 입력. 계획은 복용 기간보다 하루 더 많게 만듦 (첫날 저녁부터 먹기 시작하는 경우 등).
 */
@Composable
private fun PeriodBar(s: MedSchedule, onChange: (MedSchedule) -> Unit) {
    var typing by remember { mutableStateOf(false) }
    fun setDays(d: Int) = onChange(s.copy(days = d.coerceIn(1, MAX_DAYS)))
    val start = LocalDate.ofEpochDay(s.startDay)
    val end = LocalDate.ofEpochDay(s.lastDay())
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(CardBg)
            .padding(start = 14.dp, end = 8.dp, top = 8.dp, bottom = 10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.med_period), fontSize = 15.sp, fontWeight = FontWeight.Bold, color = Ink, modifier = Modifier.weight(1f))
            StepButton(Icons.Filled.Remove, enabled = s.days > 1) { setDays(s.days - 1) }
            Text(
                "${s.days}",
                fontSize = 24.sp,
                fontWeight = FontWeight.ExtraBold,
                color = Ink,
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { typing = true }
                    .padding(horizontal = 10.dp),
            )
            StepButton(Icons.Filled.Add, enabled = s.days < MAX_DAYS) { setDays(s.days + 1) }
            Text(stringResource(R.string.med_days_unit), fontSize = 15.sp, fontWeight = FontWeight.Bold, color = Ink, modifier = Modifier.padding(start = 6.dp, end = 6.dp))
        }
        Text(
            stringResource(
                R.string.med_period_range,
                start.monthValue, start.dayOfMonth,
                end.monthValue, end.dayOfMonth, end.dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.KOREAN),
                s.days + 1,
            ),
            fontSize = 12.sp,
            color = SubInk,
        )
    }
    if (typing) {
        var text by remember { mutableStateOf(s.days.toString()) }
        AlertDialog(
            onDismissRequest = { typing = false },
            title = { Text(stringResource(R.string.med_period_input_title)) },
            text = {
                OutlinedTextField(
                    value = text,
                    onValueChange = { v -> text = v.filter { it.isDigit() }.take(3) },
                    singleLine = true,
                    suffix = { Text(stringResource(R.string.med_days_unit)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    text.toIntOrNull()?.let { setDays(it) }
                    typing = false
                }) { Text(stringResource(android.R.string.ok)) }
            },
            dismissButton = {
                TextButton(onClick = { typing = false }) { Text(stringResource(R.string.med_cancel)) }
            },
        )
    }
}

/** 기간 최대 (일분). */
private const val MAX_DAYS = 365

/** 동그란 [−] / [+] 버튼. */
@Composable
private fun StepButton(icon: ImageVector, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .size(36.dp)
            .clip(CircleShape)
            .background(if (enabled) Dark else Color(0x22000000))
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
    }
}

/** 스크롤 목록: 오늘 카드 → 시각 카드 → 날짜별 회차 카드 → 기록 초기화. */
@Composable
private fun DoseList(s: MedSchedule, onChange: (MedSchedule) -> Unit, onReset: () -> Unit, modifier: Modifier) {
    val today = LocalDate.now().toEpochDay()
    // 오늘부터 앞으로의 날 → 위에, 지난 날 → 맨 아래 "지난 기록" (최근 것부터)
    val upcoming = remember(s.startDay, s.lastDay(), today) { (maxOf(s.startDay, today)..s.lastDay()).toList() }
    val past = remember(s.startDay, s.lastDay(), today) {
        (s.startDay until minOf(today, s.lastDay() + 1)).reversed().toList()
    }
    val now = System.currentTimeMillis()
    val next = nextDose(s)
    // 열면 맨 위(오늘 카드)부터
    val listState = rememberLazyListState()

    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        state = listState,
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item { TodayCard(s, next) }
        item { SlotTimeCards(s, onChange) }
        items(upcoming, key = { it }) { day ->
            DayGroup(s, day, today, now, next, onChange)
        }
        if (past.isNotEmpty()) {
            item {
                Text(
                    stringResource(R.string.med_past_header),
                    color = SubInk, fontSize = 13.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(start = 4.dp, top = 18.dp),
                )
            }
            items(past, key = { it }) { day ->
                DayGroup(s, day, today, now, next, onChange)
            }
        }
        item {
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                TextButton(onClick = onReset) {
                    Icon(Icons.Filled.RestartAlt, contentDescription = null, tint = LateRed, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.med_reset_button), color = LateRed)
                }
            }
        }
    }
}

/** 오늘 카드: "오늘" 2 / 3 + 진행 막대 + 다음 복용. */
@Composable
private fun TodayCard(s: MedSchedule, next: Pair<Long, MedSlot>?) {
    val today = LocalDate.now().toEpochDay()
    val planned = MedSlot.entries.count { s.isPlanned(today, it) }
    val taken = MedSlot.entries.count { s.isPlanned(today, it) && s.isTaken(today, it) }
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(Dark)
            .padding(18.dp),
    ) {
        Row(verticalAlignment = Alignment.Bottom) {
            Text(stringResource(R.string.med_today), color = Color(0xFFFFE873), fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.weight(1f))
            Text("$taken", color = Color.White, fontSize = 34.sp, fontWeight = FontWeight.ExtraBold)
            Text(" / $planned", color = Color(0xFFBFB38A), fontSize = 20.sp, modifier = Modifier.padding(bottom = 4.dp))
        }
        Spacer(Modifier.height(10.dp))
        LinearProgressIndicator(
            progress = { if (planned == 0) 0f else taken.toFloat() / planned },
            modifier = Modifier.fillMaxWidth().height(10.dp),
            color = Color(0xFFFFE873),
            trackColor = Color(0xFF4A4128),
            strokeCap = StrokeCap.Round,
            gapSize = 0.dp,
            drawStopIndicator = {},
        )
        Spacer(Modifier.height(12.dp))
        Text(
            if (next == null) {
                stringResource(
                    when {
                        planned > 0 && taken == planned -> R.string.med_today_done
                        today > s.lastDay() -> R.string.med_period_over
                        else -> R.string.med_no_next
                    },
                )
            } else {
                val t = s.doseTime(next.first, next.second)
                stringResource(
                    if (next.first == today) R.string.med_next_today else R.string.med_next_tomorrow,
                    stringResource(next.second.label),
                    timeText(t.hour * 60 + t.minute),
                )
            },
            color = Color.White,
            fontSize = 15.sp,
        )
    }
}

/** 시각 카드 4개 (아침·점심·저녁·심야, 2 × 2): 아이콘 + 시각 (누르면 시각 고르기) + 스위치 (끄면 그때는 안 먹음). */
@Composable
private fun SlotTimeCards(s: MedSchedule, onChange: (MedSchedule) -> Unit) {
    val context = LocalContext.current
    Column {
        Text(
            stringResource(R.string.med_times_header),
            color = SubInk, fontSize = 13.sp, fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(start = 4.dp, top = 6.dp, bottom = 6.dp),
        )
        // 2 × 2 칸: 아침 점심 / 저녁 심야
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
          MedSlot.entries.chunked(2).forEach { pair ->
           Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            pair.forEach { slot ->
                val i = slot.ordinal
                val on = s.slotOn[i]
                val m = s.minutes[i]
                Row(
                    Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(16.dp))
                        .background(if (on) CardBg else CardBg.copy(alpha = 0.5f))
                        .clickable(enabled = on) {
                            TimePickerDialog(context, { _, h, min ->
                                onChange(s.copy(minutes = s.minutes.toMutableList().also { it[i] = h * 60 + min }))
                            }, m / 60, m % 60, false).show()
                        }
                        .padding(start = 12.dp, end = 8.dp, top = 10.dp, bottom = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                  Column(Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(slotIcon(slot), contentDescription = null, tint = if (on) Ink else SubInk, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(4.dp))
                        Text(stringResource(slot.label), fontSize = 13.sp, color = if (on) Ink else SubInk, fontWeight = FontWeight.Bold)
                    }
                    // "오전" 작게 + "7:30" 크게 두 줄 (좁은 칸·큰 글씨 폰에서도 시각이 잘리지 않게)
                    Text(
                        stringResource(if (m / 60 < 12) R.string.med_am else R.string.med_pm),
                        fontSize = 12.sp,
                        color = SubInk,
                        maxLines = 1,
                    )
                    Text(
                        "%d:%02d".format(hour12(m / 60), m % 60),
                        fontSize = 20.sp,
                        fontWeight = FontWeight.ExtraBold,
                        color = if (on) Ink else SubInk,
                        maxLines = 1,
                    )
                  }
                    Switch(
                        checked = on,
                        onCheckedChange = { v ->
                            // 켤 때: 지난 날의 이 때는 "건너뜀" 으로 (켜기 전 날들이 "안 드셨어요" 로 보이지 않게)
                            val today = LocalDate.now().toEpochDay()
                            val pastKeys = if (v) (s.startDay until today).map { doseKey(it, slot) } else emptyList()
                            onChange(
                                s.copy(
                                    slotOn = s.slotOn.toMutableList().also { it[i] = v },
                                    skipped = s.skipped + pastKeys.filter { it !in s.taken },
                                ),
                            )
                        },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = Color.White,
                            checkedTrackColor = Dark,
                            uncheckedThumbColor = SubInk,
                            uncheckedTrackColor = CardBg,
                            uncheckedBorderColor = SubInk,
                        ),
                    )
                }
            }
           }
          }
        }
    }
}

/** 하루 묶음: 날짜 머리줄 ("11월 27일 (금) · 오늘") + 회차 카드들 (그날 먹는 때만). */
@Composable
private fun DayGroup(
    s: MedSchedule,
    day: Long,
    today: Long,
    now: Long,
    next: Pair<Long, MedSlot>?,
    onChange: (MedSchedule) -> Unit,
) {
    val date = LocalDate.ofEpochDay(day)
    val dow = date.dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.KOREAN)
    val slots = MedSlot.entries.filter { s.slotOn[it.ordinal] }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(start = 4.dp, top = 8.dp)) {
            Text(
                stringResource(R.string.med_day_header, date.monthValue, date.dayOfMonth, dow),
                fontSize = 15.sp, fontWeight = FontWeight.ExtraBold, color = Ink,
            )
            val tag = when (day) {
                today -> R.string.med_tag_today
                today + 1 -> R.string.med_tag_tomorrow
                today - 1 -> R.string.med_tag_yesterday
                else -> null
            }
            if (tag != null) {
                Spacer(Modifier.width(8.dp))
                Text(
                    stringResource(tag),
                    fontSize = 12.sp, fontWeight = FontWeight.Bold,
                    color = if (day == today) Color.White else Ink,
                    modifier = Modifier
                        .clip(RoundedCornerShape(50))
                        .background(if (day == today) Dark else CardBg)
                        .padding(horizontal = 8.dp, vertical = 2.dp),
                )
            }
        }
        slots.forEach { slot ->
            DoseCard(s, day, slot, now, isNext = next == (day to slot), onChange = onChange)
        }
    }
}

/**
 * 회차 카드: 아이콘 · 아침 / 오전 7:30 · N회차 · [계획] 스위치 · 큰 체크 동그라미.
 * 계획을 끄면 이 회차만 건너뜀 (흐리게). 동그라미를 누르면 먹음 ↔ 안 먹음 (먹은 시각이 아래에).
 * 다음에 먹을 회차는 주황 테두리, 30분 지나도 안 먹은 회차는 빨간 바탕.
 */
@Composable
private fun DoseCard(
    s: MedSchedule,
    day: Long,
    slot: MedSlot,
    now: Long,
    isNext: Boolean,
    onChange: (MedSchedule) -> Unit,
) {
    val key = doseKey(day, slot)
    val planned = s.isPlanned(day, slot)
    val takenAt = s.taken[key]
    val due = s.doseTime(day, slot).toInstant().toEpochMilli()
    val late = planned && takenAt == null && due + MedScheduler.LATE_MS < now
    // 회차 번호: 1회차 날 아침 = 1, 하루에 3칸씩 (그림처럼 아침·점심·저녁을 모두 셈)
    val onSlots = MedSlot.entries.filter { s.slotOn[it.ordinal] }
    val round = ((day - s.startDay) * onSlots.size + onSlots.indexOf(slot) + 1).toInt()
    val bg by animateColorAsState(
        when {
            takenAt != null -> AccentSoft
            late -> LateSoft
            else -> CardBg
        },
        label = "doseBg",
    )
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(if (planned) bg else CardBg.copy(alpha = 0.45f))
            .then(if (isNext) Modifier.border(2.dp, NextRing, RoundedCornerShape(18.dp)) else Modifier)
            .padding(start = 14.dp, end = 10.dp, top = 10.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 아이콘 동그라미
        Box(
            Modifier.size(40.dp).clip(CircleShape).background(if (planned) PageBg else Color(0x22000000)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(slotIcon(slot), contentDescription = null, tint = if (planned) Ink else SubInk, modifier = Modifier.size(22.dp))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    stringResource(slot.label),
                    fontSize = 17.sp, fontWeight = FontWeight.ExtraBold,
                    color = when {
                        !planned -> SubInk
                        late -> LateRed
                        else -> Ink
                    },
                )
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.med_round, round), fontSize = 12.sp, color = SubInk)
            }
            Text(
                when {
                    !planned -> stringResource(R.string.med_skipped)
                    takenAt != null -> stringResource(R.string.med_taken_at, timeText(takenMinutes(takenAt)))
                    else -> timeText(s.minutes[slot.ordinal])
                },
                fontSize = 14.sp,
                color = when {
                    takenAt != null -> Accent
                    late -> LateRed
                    else -> SubInk
                },
                fontWeight = if (takenAt != null || late) FontWeight.Bold else FontWeight.Normal,
                maxLines = 1,
            )
            // 30분 지나도 안 먹음: 따로 한 줄 (큰 글씨 폰에서도 시각이 줄바꿈되지 않게)
            if (late) {
                Text(
                    stringResource(R.string.med_late_label),
                    fontSize = 12.sp, color = LateRed, fontWeight = FontWeight.Bold, maxLines = 1,
                )
            }
        }
        // 계획 스위치 (끄면 이 회차만 건너뜀)
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Switch(
                checked = planned,
                onCheckedChange = { on ->
                    onChange(s.copy(skipped = if (on) s.skipped - key else s.skipped + key))
                },
                modifier = Modifier.height(32.dp),
                colors = SwitchDefaults.colors(
                    checkedThumbColor = Color.White,
                    checkedTrackColor = Dark,
                    uncheckedThumbColor = SubInk,
                    uncheckedTrackColor = CardBg,
                    uncheckedBorderColor = SubInk,
                ),
            )
            Text(stringResource(R.string.med_col_plan), fontSize = 10.sp, color = SubInk)
        }
        Spacer(Modifier.width(10.dp))
        // 먹음 체크: 큰 동그라미
        val done = takenAt != null
        Box(
            Modifier
                .size(48.dp)
                .clip(CircleShape)
                .background(if (done) Accent else Color.Transparent)
                .border(2.5.dp, if (done) Accent else if (planned) (if (late) LateRed else Ink) else Color(0x33000000), CircleShape)
                .clickable(enabled = planned || done) {
                    onChange(
                        s.copy(taken = if (done) s.taken - key else s.taken + (key to System.currentTimeMillis())),
                    )
                },
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Filled.Check,
                contentDescription = stringResource(R.string.med_taken_action),
                tint = if (done) Color.White else if (planned) Color(0x33000000) else Color(0x14000000),
                modifier = Modifier.size(28.dp),
            )
        }
    }
}
