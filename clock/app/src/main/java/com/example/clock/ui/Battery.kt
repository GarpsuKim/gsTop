package com.example.clock.ui

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.RingtoneManager
import android.os.BatteryManager
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Battery1Bar
import androidx.compose.material.icons.filled.Battery3Bar
import androidx.compose.material.icons.filled.Battery5Bar
import androidx.compose.material.icons.filled.BatteryAlert
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material.icons.filled.BatteryFull
import androidx.compose.material.icons.filled.PowerOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.example.clock.R

/**
 * 시계 화면의 배터리 표시 ([시보·알람] → 배터리). 순서 = 저장값(ordinal) → 새 항목은 끝에 추가할 것.
 * 버튼이 숨겨진 전체화면일 때만 오른쪽 위에 작게 보임 (버튼을 보이면 상태바가 돌아오므로).
 */
enum class BatteryDisplay(@StringRes val label: Int) {
    Off(R.string.battery_display_off),
    Charging(R.string.battery_display_charging), // 충전 중일 때만
    Always(R.string.battery_display_always),
}

/** 충전 완료 알림: 이 % 에 닿으면 (0 = 끔). 순서 = 저장값(ordinal). */
enum class BatteryFullAlert(val percent: Int) {
    Off(0),
    P80(80),
    P90(90),
    P100(100),
}

/** 배터리 부족 알림: 충전기가 빠진 채 이 % 이하로 내려가면 (0 = 끔). 순서 = 저장값(ordinal). */
enum class BatteryLowAlert(val percent: Int) {
    Off(0),
    P15(15),
    P20(20),
    P30(30),
}

/** 지금 배터리: 남은 % + 충전기 연결 여부. */
data class BatteryState(val percent: Int, val plugged: Boolean)

/** 화면 위에 잠깐 띄우는 배터리 안내. */
sealed interface BatteryAlert {
    val percent: Int

    data class Full(override val percent: Int) : BatteryAlert
    data class Unplugged(override val percent: Int) : BatteryAlert
    data class Low(override val percent: Int) : BatteryAlert
}

/**
 * 배터리 상태를 계속 받음 (시스템 방송 ACTION_BATTERY_CHANGED, 권한 필요 없음).
 * 남은 % 나 충전기 연결이 바뀔 때만 값이 바뀜 (온도·전압만 바뀐 방송은 무시됨).
 */
@Composable
fun rememberBatteryState(): State<BatteryState?> {
    val context = LocalContext.current
    val state = remember { mutableStateOf<BatteryState?>(null) }
    DisposableEffect(context) {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                state.value = batteryStateOf(intent) ?: return
            }
        }
        // 이 방송은 마지막 값을 기억해 두는 방송이라, 등록하자마자 지금 상태가 한 번 들어옴
        context.registerReceiver(receiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))?.let { sticky ->
            batteryStateOf(sticky)?.let { state.value = it }
        }
        onDispose { context.unregisterReceiver(receiver) }
    }
    return state
}

private fun batteryStateOf(intent: Intent): BatteryState? {
    val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
    val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
    if (level < 0 || scale <= 0) return null
    val plugged = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0
    return BatteryState(percent = (level * 100f / scale).toInt().coerceIn(0, 100), plugged = plugged)
}

/**
 * 배터리 변화를 보고 안내를 띄울지 정함 (화면에는 아무것도 그리지 않음).
 *  - 충전 완료: 충전 중 [full] % 에 닿으면 한 번 (충전기를 빼면 다시 준비)
 *  - 충전기 빠짐: 연결돼 있다가 빠지면
 *  - 배터리 부족: 충전기 없이 [low] % 이하로 내려가면 한 번 (충전기를 꽂으면 다시 준비)
 * 앱을 처음 켤 때의 상태로는 알리지 않음 (예: 이미 100% 충전 중이면 "충전 완료" 를 띄우지 않음).
 */
@Composable
fun BatteryAlertWatcher(
    state: BatteryState?,
    full: BatteryFullAlert,
    unplug: Boolean,
    low: BatteryLowAlert,
    onAlert: (BatteryAlert) -> Unit,
) {
    val latestFull by rememberUpdatedState(full)
    val latestUnplug by rememberUpdatedState(unplug)
    val latestLow by rememberUpdatedState(low)
    val latestOnAlert by rememberUpdatedState(onAlert)
    // 화면 회전 등으로 다시 그려져도 이어지도록 한 덩어리로 기억
    val memo = remember { BatteryMemo() }
    LaunchedEffect(state) {
        val s = state ?: return@LaunchedEffect
        val prev = memo.prev
        memo.prev = s
        val fullOn = latestFull != BatteryFullAlert.Off
        val lowOn = latestLow != BatteryLowAlert.Off
        if (prev == null) {
            // 첫 값: 기준만 잡음
            memo.fullDone = fullOn && s.plugged && s.percent >= latestFull.percent
            memo.lowDone = lowOn && !s.plugged && s.percent <= latestLow.percent
            return@LaunchedEffect
        }
        if (!s.plugged) memo.fullDone = false
        if (s.plugged) memo.lowDone = false
        if (latestUnplug && prev.plugged && !s.plugged) latestOnAlert(BatteryAlert.Unplugged(s.percent))
        if (fullOn && s.plugged && s.percent >= latestFull.percent && !memo.fullDone) {
            memo.fullDone = true
            latestOnAlert(BatteryAlert.Full(s.percent))
        }
        if (lowOn && !s.plugged && s.percent <= latestLow.percent && !memo.lowDone) {
            memo.lowDone = true
            latestOnAlert(BatteryAlert.Low(s.percent))
        }
    }
}

private class BatteryMemo {
    var prev: BatteryState? = null
    var fullDone = false
    var lowDone = false
}

/** 알림음 한 번 (폰의 기본 알림 소리, 무음·진동 모드면 폰 설정대로 소리 없음). */
fun playBatteryAlertSound(context: Context) {
    runCatching {
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION) ?: return
        RingtoneManager.getRingtone(context, uri)?.play()
    }
}

/** 배터리 아이콘: 충전 중이면 번개, 아니면 남은 양에 따라. */
private fun batteryIcon(s: BatteryState): ImageVector = when {
    s.plugged -> Icons.Filled.BatteryChargingFull
    s.percent <= 15 -> Icons.Filled.BatteryAlert
    s.percent <= 35 -> Icons.Filled.Battery1Bar
    s.percent <= 65 -> Icons.Filled.Battery3Bar
    s.percent <= 90 -> Icons.Filled.Battery5Bar
    else -> Icons.Filled.BatteryFull
}

/** 시계 화면 오른쪽 위의 작은 배터리 표시: 아이콘 + "87%". */
@Composable
fun BatteryIndicator(state: BatteryState, modifier: Modifier = Modifier) {
    val color = if (!state.plugged && state.percent <= 15) Color(0xFFFF6B6B) else Color.White.copy(alpha = 0.7f)
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Icon(batteryIcon(state), contentDescription = null, tint = color, modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(2.dp))
        Text("${state.percent}%", style = MaterialTheme.typography.labelMedium, color = color)
        Spacer(Modifier.width(6.dp))
        BatteryLevelBar(state, Modifier.width(100.dp).height(6.dp), dim = true)
    }
}

/**
 * 충전 수준 막대 (0~100%). 보기 전용 (배터리는 사용자가 바꿀 수 없으므로 끌 수 없는 막대).
 * 색: 충전 중 = 초록, 부족(15% 이하·충전기 없음) = 빨강, 그 밖 = 흰색. [dim] = 시계 화면용으로 흐리게.
 */
@Composable
fun BatteryLevelBar(state: BatteryState, modifier: Modifier = Modifier, dim: Boolean = false) {
    val fill = when {
        state.plugged -> Color(0xFF4CAF50)
        state.percent <= 15 -> Color(0xFFFF5252)
        else -> Color.White
    }.copy(alpha = if (dim) 0.75f else 1f)
    LinearProgressIndicator(
        progress = { state.percent / 100f },
        modifier = modifier,
        color = fill,
        trackColor = Color.White.copy(alpha = if (dim) 0.18f else 0.25f),
        strokeCap = StrokeCap.Round,
        gapSize = 0.dp,
        drawStopIndicator = {},
    )
}

/** 메뉴의 "지금 값" 글자: "87% 충전 중" / "87%". */
@Composable
fun batteryValueText(state: BatteryState?): String = when {
    state == null -> "…"
    state.plugged -> stringResource(R.string.battery_value_charging, state.percent)
    else -> stringResource(R.string.battery_value, state.percent)
}

/**
 * 화면 위쪽 가운데의 배터리 안내. 저절로 사라지지 않음:
 * 누르면 닫히고, 충전기 상태가 바뀌면 닫힘 ([isResolvedBy] 를 보고 화면 쪽에서 지움).
 */
@Composable
fun BatteryAlertBanner(alert: BatteryAlert, onDismiss: () -> Unit, modifier: Modifier = Modifier) {
    val latestDismiss by rememberUpdatedState(onDismiss)
    val (icon, text, color) = when (alert) {
        is BatteryAlert.Full -> Triple(Icons.Filled.BatteryFull, stringResource(R.string.battery_alert_full, alert.percent), Color(0xFF2E7D32))
        is BatteryAlert.Unplugged -> Triple(Icons.Filled.PowerOff, stringResource(R.string.battery_alert_unplugged, alert.percent), Color(0xFFE65100))
        is BatteryAlert.Low -> Triple(Icons.Filled.BatteryAlert, stringResource(R.string.battery_alert_low, alert.percent), Color(0xFFC62828))
    }
    Row(
        modifier
            .padding(horizontal = 16.dp)
            .background(color.copy(alpha = 0.92f), RoundedCornerShape(16.dp))
            .clickable { latestDismiss() }
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, contentDescription = null, tint = Color.White, modifier = Modifier.size(28.dp))
        Spacer(Modifier.width(12.dp))
        Text(text, style = MaterialTheme.typography.titleMedium, color = Color.White)
    }
}

/**
 * 충전기 상태가 바뀌어 이 안내가 더는 필요 없는지.
 *  - 충전 완료: 충전기를 빼면
 *  - 충전기 빠짐·배터리 부족: 충전기를 꽂으면
 */
fun BatteryAlert.isResolvedBy(state: BatteryState): Boolean = when (this) {
    is BatteryAlert.Full -> !state.plugged
    is BatteryAlert.Unplugged, is BatteryAlert.Low -> state.plugged
}

/**
 * [배터리 설정] 창 (닫힘 설정 창과 같은 모양). 바꿀 때마다 바로 저장 ([onChange]).
 *  1. 지금 배터리 (87% 충전 중)
 *  2. 화면에 표시: 끔 / 충전 중일 때만 / 항상
 *  3. 충전 완료 알림: 끔 / 80% / 90% / 100%
 *  4. 충전기 빠짐 알림: 켜기/끄기
 *  5. 배터리 부족 알림: 끔 / 15% / 20% / 30%
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BatteryDialog(
    state: BatteryState?,
    display: BatteryDisplay,
    full: BatteryFullAlert,
    unplug: Boolean,
    low: BatteryLowAlert,
    onChange: (BatteryDisplay, BatteryFullAlert, Boolean, BatteryLowAlert) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.battery_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                // 1. 지금 배터리
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (state != null) {
                        Icon(batteryIcon(state), contentDescription = null, modifier = Modifier.size(22.dp))
                        Spacer(Modifier.width(6.dp))
                    }
                    Text(batteryValueText(state), style = MaterialTheme.typography.titleMedium)
                }
                if (state != null) {
                    Spacer(Modifier.height(8.dp))
                    BatteryLevelBar(state, Modifier.fillMaxWidth().height(10.dp))
                }
                Spacer(Modifier.height(16.dp))

                // 2. 화면에 표시
                Text(stringResource(R.string.battery_display), style = MaterialTheme.typography.labelLarge)
                Spacer(Modifier.height(2.dp))
                Text(
                    stringResource(R.string.battery_display_desc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    BatteryDisplay.entries.forEach { d ->
                        FilterChip(
                            selected = display == d,
                            onClick = { onChange(d, full, unplug, low) },
                            label = { Text(stringResource(d.label)) },
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))

                // 3. 충전 완료 알림
                Text(stringResource(R.string.battery_full_alert), style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    BatteryFullAlert.entries.forEach { f ->
                        FilterChip(
                            selected = full == f,
                            onClick = { onChange(display, f, unplug, low) },
                            label = { Text(if (f == BatteryFullAlert.Off) stringResource(R.string.battery_off) else "${f.percent}%") },
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))

                // 4. 충전기 빠짐 알림
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(R.string.battery_unplug_alert),
                        Modifier.weight(1f),
                        style = MaterialTheme.typography.labelLarge,
                    )
                    Switch(checked = unplug, onCheckedChange = { onChange(display, full, it, low) })
                }
                Spacer(Modifier.height(12.dp))

                // 5. 배터리 부족 알림
                Text(stringResource(R.string.battery_low_alert), style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    BatteryLowAlert.entries.forEach { l ->
                        FilterChip(
                            selected = low == l,
                            onClick = { onChange(display, full, unplug, l) },
                            label = { Text(if (l == BatteryLowAlert.Off) stringResource(R.string.battery_off) else stringResource(R.string.battery_below, l.percent)) },
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text(
                    stringResource(R.string.battery_alert_desc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.chime_close)) }
        },
    )
}
