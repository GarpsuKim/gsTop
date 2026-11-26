package com.example.clock.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.example.clock.R
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/** 켤 때 처음 고르는 시간 (전에 고른 시간이 없을 때). */
private val DefaultAutoClose = AutoClose.M30

/** 닫히는 시간 묶음: 제목 + 그 단위의 항목들. */
private class AutoCloseGroup(@StringRes val title: Int, val items: List<AutoClose>)

private val autoCloseGroups: List<AutoCloseGroup> = run {
    val on = AutoClose.entries.filter { it != AutoClose.Off }
    listOf(
        AutoCloseGroup(R.string.auto_close_group_minutes, on.filter { it.seconds < 3_600L }),
        AutoCloseGroup(R.string.auto_close_group_hours, on.filter { it.seconds in 3_600L until 86_400L }),
        AutoCloseGroup(R.string.auto_close_group_days, on.filter { it.seconds >= 86_400L }),
    ).filter { it.items.isNotEmpty() }
}

/**
 * [닫힘 설정] 창 (시보 설정 창과 같은 모양):
 *  1. 닫힘 사용 켜기/끄기 (끄면 = 무한, 시계를 닫지 않음)
 *  2. 켜져 있으면: "지금부터 화면을 만지지 않으면 ○시 ○분에 닫힙니다" 안내
 *  3. 닫히는 시간: 초·분 / 시간 / 일 묶음별 버튼
 * 바꿀 때마다 바로 저장 ([onChange]). 이 창을 연 것도 화면을 만진 것이라, 안내 시각은 지금부터 계산하면 맞음.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun AutoCloseDialog(current: AutoClose, onChange: (AutoClose) -> Unit, onDismiss: () -> Unit) {
    var value by remember { mutableStateOf(current) }
    // 끄기 전에 고른 시간 (다시 켤 때 그 시간으로)
    var lastOn by remember { mutableStateOf(if (current != AutoClose.Off) current else DefaultAutoClose) }
    fun update(new: AutoClose) {
        value = new
        if (new != AutoClose.Off) lastOn = new
        onChange(new)
    }
    val enabled = value != AutoClose.Off

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.auto_close_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                // 1. 켜기/끄기
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(R.string.auto_close_on),
                        Modifier.weight(1f),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Switch(
                        checked = enabled,
                        onCheckedChange = { on -> update(if (on) lastOn else AutoClose.Off) },
                    )
                }
                // 2. 언제 닫히는지
                Text(
                    if (enabled) {
                        stringResource(R.string.auto_close_next, closeTimeText(value))
                    } else {
                        stringResource(R.string.auto_close_never)
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    stringResource(R.string.auto_close_desc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(16.dp))

                // 3. 닫히는 시간 (켰을 때만 고를 수 있음)
                Text(stringResource(R.string.auto_close_length), style = MaterialTheme.typography.labelLarge)
                autoCloseGroups.forEach { group ->
                    Spacer(Modifier.height(8.dp))
                    Text(
                        stringResource(group.title),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(Modifier.height(2.dp))
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        group.items.forEach { a ->
                            FilterChip(
                                selected = enabled && value == a,
                                enabled = enabled,
                                onClick = { update(a) },
                                label = { Text(autoCloseValue(a)) },
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.chime_close)) }
        },
    )
}

/** 지금부터 [a] 만큼 지난 시각: 오늘이면 "오후 3:25", 다른 날이면 "11월 28일 오후 3:25". */
@Composable
private fun closeTimeText(a: AutoClose): String {
    val now = remember(a) { LocalDateTime.now() }
    val at = now.plusSeconds(a.seconds)
    val pattern = if (at.toLocalDate() == now.toLocalDate()) "a h:mm" else "M월 d일 a h:mm"
    return at.format(DateTimeFormatter.ofPattern(pattern))
}
