package com.example.clock.ui

import android.app.DatePickerDialog
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Cake
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.TextButton
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.example.clock.schedule.ANNIV_MAX
import com.example.clock.schedule.Anniv
import com.example.clock.schedule.AnnivBook
import com.example.clock.schedule.AnnivStore
import com.example.clock.schedule.AnnivType
import com.example.clock.schedule.annivLines
import kotlinx.coroutines.delay
import java.time.LocalDate
import java.time.LocalDateTime

/** 기념일 창이 열려 있는지 ([시보·알람] → 기념일). */
object AnniversaryUi {
    var open by mutableStateOf(false)
}

private val ATeal = Color(0xFF17607F)
private val APage = Color(0xFFF2F5F8)
private val AInk = Color(0xFF13293A)
private val ASub = Color(0xFF5D7180)
private val AnnivColors = lightColorScheme(
    primary = ATeal, onPrimary = Color.White, primaryContainer = Color(0xFFD3E8F2), onPrimaryContainer = AInk,
    secondaryContainer = Color(0xFFD3E8F2), onSecondaryContainer = ATeal, surface = Color.White, onSurface = AInk,
    onSurfaceVariant = ASub, surfaceVariant = Color(0xFFE8EDF2), outline = Color(0xFF9FB2BF), background = APage, onBackground = AInk,
)

/** [시보·알람] 메뉴 오른쪽 값 (짧게, 메뉴 제목이 눌리지 않게): 가장 가까운 것 "오늘" / "D-5" / "켜짐" / "끔". */
@Composable
fun anniversaryValue(): String {
    val context = LocalContext.current
    val b = remember(AnnivStore.version) { AnnivStore.load(context) }
    if (b.all.none { it.date != null }) return "끔"
    val first = annivLines(b).firstOrNull { it.today || it.text.contains("D-") } ?: return "켜짐"
    return if (first.today) "오늘 🎉" else "D-${first.daysLeft}"
}

/**
 * 시계 화면 왼쪽 아래: 기념일 줄들 (최대 5줄, 넘으면 [더보기...] → 전체 창). 1시간마다(정각) 다시 계산.
 * 오늘인 것은 노란 글씨로 맨 위.
 */
@Composable
fun AnniversaryLines(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val b = remember(AnnivStore.version) { AnnivStore.load(context) }
    var today by remember { mutableStateOf(LocalDate.now()) }
    LaunchedEffect(Unit) {
        while (true) {
            // 다음 정각까지 기다렸다가 다시 계산 (날짜가 바뀌는 자정도 포함)
            val n = LocalDateTime.now()
            val ms = ((59 - n.minute) * 60_000L + (60 - n.second) * 1_000L).coerceAtLeast(1_000L)
            delay(ms)
            today = LocalDate.now()
        }
    }
    val all = annivLines(b, today)
    if (all.isEmpty()) return
    // 세로 5줄, 가로는 화면 높이가 낮아 왼쪽 위 시간표 글자를 가리지 않게 3줄. 넘는 것은 [더보기...]
    val landscape = androidx.compose.ui.platform.LocalConfiguration.current.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE
    val lines = all.take(if (landscape) 3 else 5)
    var more by remember { mutableStateOf(false) }
    Column(
        modifier.clip(RoundedCornerShape(12.dp)).background(Color(0x66000000)).padding(horizontal = 10.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        lines.forEach { l ->
            Text(
                l.text,
                color = if (l.today) Color(0xFFFFE873) else Color.White,
                fontSize = if (l.today) 14.sp else 12.sp, // 시계를 너무 가리지 않게 작게
                fontWeight = if (l.today) FontWeight.ExtraBold else FontWeight.Bold,
                maxLines = 1, overflow = TextOverflow.Ellipsis,
            )
        }
        // 다 못 보이면 [더보기...] → 별도 창에 전체
        if (all.size > lines.size) {
            Text(
                "더보기... (${all.size}건)",
                color = Color(0xFF9FD8FF), fontSize = 13.sp, fontWeight = FontWeight.Bold,
                modifier = Modifier.clip(RoundedCornerShape(8.dp)).clickable { more = true }.padding(horizontal = 4.dp, vertical = 4.dp),
            )
        }
    }
    if (more) {
        AlertDialog(
            onDismissRequest = { more = false },
            // 가로 화면에서도 한 줄씩 보이게 넓게
            properties = DialogProperties(usePlatformDefaultWidth = false),
            modifier = Modifier.fillMaxWidth(0.92f).widthIn(max = 720.dp),
            icon = { Icon(Icons.Filled.Cake, contentDescription = null) },
            title = { Text("기념일 (${all.size}건)") },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    all.forEach { l ->
                        Text(
                            l.text, fontSize = 15.sp,
                            fontWeight = if (l.today) FontWeight.ExtraBold else FontWeight.Normal,
                            color = if (l.today) Color(0xFFFFE873) else Color.Unspecified,
                        )
                    }
                }
            },
            confirmButton = { TextButton(onClick = { more = false }) { Text("닫기") } },
        )
    }
}

/**
 * 기념일 창 (전체 화면, 밝은 테마). 바꾸는 즉시 저장.
 *  본인 생일 · 배우자 생일 · 첫 만남일 / 생일 1~20 [+ 추가] / 기념일 1~20 [+ 추가]
 */
@Composable
fun AnniversaryDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var b by remember { mutableStateOf(AnnivStore.load(context)) }
    fun update(n: AnnivBook) {
        b = n
        AnnivStore.save(context, n)
    }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        MaterialTheme(colorScheme = AnnivColors) {
            Surface(Modifier.fillMaxSize(), color = APage, contentColor = AInk) {
                Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
                    Row(Modifier.fillMaxWidth().background(Color.White).padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = onDismiss) { Icon(Icons.Filled.Close, contentDescription = null, tint = AInk) }
                        Icon(Icons.Filled.Cake, contentDescription = null, tint = ATeal)
                        Spacer(Modifier.width(8.dp))
                        Text("기념일", fontSize = 21.sp, fontWeight = FontWeight.ExtraBold, color = AInk)
                    }
                    Column(
                        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        // 지금 보이는 줄 미리보기
                        val preview = annivLines(b)
                        if (preview.isNotEmpty()) {
                            Column(
                                Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Color(0xFF13293A)).padding(12.dp),
                                verticalArrangement = Arrangement.spacedBy(2.dp),
                            ) {
                                Text("시계 화면에 보이는 것", color = Color(0xFFBFD3E0), fontSize = 12.sp)
                                preview.take(5).forEach { Text(it.text, color = if (it.today) Color(0xFFFFE873) else Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            }
                        }
                        AnnivCard(b.self, title = "본인 생일") { update(b.copy(self = it)) }
                        AnnivCard(b.spouse, title = "배우자 생일") { update(b.copy(spouse = it)) }
                        AnnivCard(b.firstMeet, title = "첫 만남일") { update(b.copy(firstMeet = it)) }

                        SectionHead("생일 (${b.birthdays.size}/$ANNIV_MAX)")
                        b.birthdays.forEachIndexed { i, a ->
                            AnnivCard(
                                a, title = "생일 ${i + 1}",
                                onDelete = { update(b.copy(birthdays = b.birthdays.filterIndexed { j, _ -> j != i })) },
                            ) { n -> update(b.copy(birthdays = b.birthdays.mapIndexed { j, x -> if (j == i) n else x })) }
                        }
                        if (b.birthdays.size < ANNIV_MAX) AddButton("생일 추가") { update(b.copy(birthdays = b.birthdays + Anniv(AnnivType.Birthday))) }

                        SectionHead("기념일 (${b.anniversaries.size}/$ANNIV_MAX)")
                        b.anniversaries.forEachIndexed { i, a ->
                            AnnivCard(
                                a, title = "기념일 ${i + 1}",
                                onDelete = { update(b.copy(anniversaries = b.anniversaries.filterIndexed { j, _ -> j != i })) },
                            ) { n -> update(b.copy(anniversaries = b.anniversaries.mapIndexed { j, x -> if (j == i) n else x })) }
                        }
                        if (b.anniversaries.size < ANNIV_MAX) AddButton("기념일 추가") { update(b.copy(anniversaries = b.anniversaries + Anniv(AnnivType.Anniversary))) }
                        // 맨 마지막: [텍스트 공유] (등록한 기념일 모두를 글자로)
                        Spacer(Modifier.height(8.dp))
                        ShareTextButton(ATeal) { shareText(context, "기념일", annivShareText(b)) }
                        Spacer(Modifier.height(24.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionHead(text: String) {
    Text(text, fontSize = 15.sp, fontWeight = FontWeight.ExtraBold, color = ASub, modifier = Modifier.padding(start = 4.dp, top = 8.dp))
}

@Composable
private fun AddButton(text: String, onClick: () -> Unit) {
    OutlinedButton(onClick = onClick, modifier = Modifier.fillMaxWidth()) {
        Icon(Icons.Filled.Add, contentDescription = null)
        Spacer(Modifier.width(6.dp))
        Text(text)
    }
}

/**
 * 기념일 한 개:
 *  [제목]                              [삭제]
 *  (기념일 별명) (성명)
 *  최초 날짜  2000-05-12  (누르면 날짜 고르기)
 *  경과 일수               [스위치]
 *  다음 회까지  [−] 30 [+] 일 전부터   [스위치]
 */
@Composable
private fun AnnivCard(a: Anniv, title: String, onDelete: (() -> Unit)? = null, onChange: (Anniv) -> Unit) {
    val context = LocalContext.current
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Color.White).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("${a.type.emoji} $title", fontSize = 16.sp, fontWeight = FontWeight.ExtraBold, modifier = Modifier.weight(1f))
            if (onDelete != null) IconButton(onClick = onDelete) { Icon(Icons.Filled.Delete, contentDescription = "삭제", tint = ASub) }
        }
        if (a.type == AnnivType.Anniversary) {
            var alias by remember(a.alias) { mutableStateOf(a.alias) }
            OutlinedTextField(alias, { alias = it; onChange(a.copy(alias = it)) }, label = { Text("기념일 별명") }, singleLine = true,
                modifier = Modifier.fillMaxWidth(), textStyle = TextStyle(fontSize = 15.sp))
        }
        if (a.type != AnnivType.Self) {
            var name by remember(a.name) { mutableStateOf(a.name) }
            OutlinedTextField(name, { name = it; onChange(a.copy(name = it)) }, label = { Text(if (a.type == AnnivType.Anniversary) "관련자 성명" else "성명") },
                singleLine = true, modifier = Modifier.fillMaxWidth(), textStyle = TextStyle(fontSize = 15.sp))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("최초 날짜", fontSize = 14.sp, color = ASub, modifier = Modifier.weight(1f))
            Text(
                a.date?.let { "%d년 %d월 %d일".format(it.year, it.monthValue, it.dayOfMonth) } ?: "날짜 고르기",
                fontSize = 16.sp, fontWeight = FontWeight.Bold, color = ATeal,
                modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(Color(0xFFE3F0F6)).clickable {
                    val d = a.date ?: LocalDate.now()
                    DatePickerDialog(context, { _, y, m, day -> onChange(a.copy(date = LocalDate.of(y, m + 1, day))) }, d.year, d.monthValue - 1, d.dayOfMonth).show()
                }.padding(horizontal = 10.dp, vertical = 6.dp),
            )
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("오늘까지 경과 일수 (D+)", fontSize = 14.sp, modifier = Modifier.weight(1f))
            Switch(a.showElapsed, { onChange(a.copy(showElapsed = it)) }, colors = SwitchDefaults.colors(checkedTrackColor = ATeal))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("다음 회까지 (D-)", fontSize = 14.sp, modifier = Modifier.weight(1f))
            Switch(a.showCountdown, { onChange(a.copy(showCountdown = it)) }, colors = SwitchDefaults.colors(checkedTrackColor = ATeal))
        }
        if (a.showCountdown) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                // 10일 아래는 1일씩, 그 위는 5일씩
                IconButton(onClick = { onChange(a.copy(daysBefore = (a.daysBefore - if (a.daysBefore > 10) 5 else 1).coerceAtLeast(0))) }) { Icon(Icons.Filled.Remove, null, tint = ATeal) }
                Text("${a.daysBefore}일 전부터 표시", fontSize = 15.sp, fontWeight = FontWeight.Bold)
                IconButton(onClick = { onChange(a.copy(daysBefore = (a.daysBefore + if (a.daysBefore >= 10) 5 else 1).coerceAtMost(365))) }) { Icon(Icons.Filled.Add, null, tint = ATeal) }
            }
        }
    }
}
