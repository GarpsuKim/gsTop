package com.example.clock.ui

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import android.widget.Toast
import android.provider.AlarmClock
import androidx.compose.material.icons.filled.Alarm
import androidx.compose.material3.HorizontalDivider
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.ui.semantics.Role
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.NotificationsOff
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.clock.R
import com.example.clock.chime.ChimeLength
import com.example.clock.chime.ChimeScheduler
import com.example.clock.chime.ChimeService
import com.example.clock.chime.ChimeSettings
import com.example.clock.chime.ChimeStore
import java.time.format.DateTimeFormatter

/**
 * [🔔 시보·알람 ▾] 버튼: 시보와 알람을 한 메뉴에서, 구분선으로 나눠서.
 *  - 시보: [시보 설정] → 시보 설정 창 (켜기/끄기, 음악 파일, 연주 시간 …). 오른쪽에 켜짐/꺼짐 표시
 *  - 알람: [알람 열기] → 시스템 시계 앱의 알람 화면
 * 버튼 아이콘으로 시보 켜짐/꺼짐 표시 (설정 창을 닫을 때 다시 읽음).
 */
@Composable
fun ChimeAlarmButton() {
    val context = LocalContext.current
    var open by remember { mutableStateOf(false) }
    var expanded by remember { mutableStateOf(false) }
    var enabled by remember { mutableStateOf(ChimeStore.load(context).enabled) }

    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(
                if (enabled) Icons.Filled.Notifications else Icons.Filled.NotificationsOff,
                contentDescription = null,
                modifier = Modifier.size(16.dp),
            )
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.chime_alarm), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            // ── 시보 ──
            MenuSectionTitle(R.string.chime)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.chime_settings_item), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = {
                    Icon(
                        if (enabled) Icons.Filled.Notifications else Icons.Filled.NotificationsOff,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                    )
                },
                trailingIcon = {
                    Text(
                        stringResource(if (enabled) R.string.chime_state_on else R.string.chime_state_off),
                        style = MaterialTheme.typography.labelSmall,
                        color = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                },
                onClick = {
                    expanded = false
                    open = true
                },
            )
            // ── 알람 ──
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            MenuSectionTitle(R.string.alarm)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.alarm_open), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.Alarm, contentDescription = null, modifier = Modifier.size(18.dp)) },
                onClick = {
                    expanded = false
                    openSystemAlarms(context)
                },
            )
        }
    }

    if (open) {
        ChimeDialog(onDismiss = {
            open = false
            enabled = ChimeStore.load(context).enabled
        })
    }
}

/** 시스템 시계 앱의 알람 화면 열기. 시계 앱이 없으면 안내만. */
private fun openSystemAlarms(context: Context) {
    val intent = Intent(AlarmClock.ACTION_SHOW_ALARMS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(intent)
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, R.string.alarm_not_found, Toast.LENGTH_SHORT).show()
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ChimeDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    // 바꿀 때마다 바로 저장 → 정각 알람도 바로 다시 맞춤
    var s by remember { mutableStateOf(ChimeStore.load(context)) }
    fun update(new: ChimeSettings) {
        s = new
        ChimeStore.save(context, new)
    }

    // 알림 권한 (Android 13+): 연주 중 알림의 [정지] 버튼이 보이도록. 거부해도 시보는 울림.
    val notificationPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    fun askNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    // 시스템 기본 파일 선택기에서 음악 파일 하나 선택. 앱이 꺼져 있어도 읽을 수 있게 영구 권한을 받아 둠.
    val pickFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            Toast.makeText(context, R.string.chime_file_failed, Toast.LENGTH_SHORT).show()
            return@rememberLauncherForActivityResult
        }
        // 이전 파일의 영구 권한은 반납 (권한은 앱당 개수 제한이 있음)
        s.fileUri?.takeIf { it != uri }?.let { old ->
            try {
                context.contentResolver.releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: SecurityException) {
                // 이미 없음
            }
        }
        // 파일을 고르면 시보도 켬 (파일이 없어서 못 켰던 경우)
        update(s.copy(uri = uri.toString(), fileName = displayName(context, uri), enabled = true))
        askNotificationPermission()
    }
    fun launchPicker() {
        try {
            pickFile.launch(arrayOf("audio/*"))
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(context, R.string.chime_picker_not_found, Toast.LENGTH_SHORT).show()
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.chime_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                // 시보 켜기/끄기
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.chime_on), Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                    Switch(
                        checked = s.enabled,
                        onCheckedChange = { on ->
                            if (on && !s.hasFile) {
                                launchPicker() // 파일부터 고르게 함 (고르면 자동으로 켜짐)
                            } else {
                                update(s.copy(enabled = on))
                                if (on) askNotificationPermission()
                            }
                        },
                    )
                }
                if (s.enabled && s.hasFile) {
                    val next = ChimeScheduler.nextChimeTime(s)
                    Text(
                        if (next == null) {
                            stringResource(R.string.chime_all_skipped)
                        } else {
                            stringResource(
                                R.string.chime_next,
                                next.format(DateTimeFormatter.ofPattern(if (s.minute == 0) "a h시" else "a h시 m분")),
                            )
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = if (next == null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                    )
                }
                // 진동·무음 모드에서도 울리기 (끄면: 소리 모드일 때만 울림)
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp)
                        .toggleable(
                            value = s.ringWhenSilent,
                            role = Role.Checkbox,
                            onValueChange = { update(s.copy(ringWhenSilent = it)) },
                        ),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Checkbox(checked = s.ringWhenSilent, onCheckedChange = null) // 줄 전체를 눌러도 체크
                    Column(Modifier.padding(start = 4.dp)) {
                        Text(stringResource(R.string.chime_ring_silent), style = MaterialTheme.typography.bodyMedium)
                        Text(
                            stringResource(R.string.chime_ring_silent_desc),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                Spacer(Modifier.height(16.dp))

                // 울리는 분: 목록에서 0~59분 중 선택 (기본 0 = 정각)
                Text(stringResource(R.string.chime_minute), style = MaterialTheme.typography.labelLarge)
                Box {
                    var minuteMenu by remember { mutableStateOf(false) }
                    TextButton(onClick = { minuteMenu = true }) {
                        Text(minuteLabel(s.minute), style = MaterialTheme.typography.bodyLarge)
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = minuteMenu,
                        onDismissRequest = { minuteMenu = false },
                        modifier = Modifier.heightIn(max = 320.dp), // 60개 → 목록 안에서 스크롤
                    ) {
                        for (m in 0..59) {
                            DropdownMenuItem(
                                text = { Text(minuteLabel(m)) },
                                onClick = {
                                    minuteMenu = false
                                    update(s.copy(minute = m))
                                },
                                trailingIcon = if (m == s.minute) {
                                    { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                                } else {
                                    null
                                },
                            )
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))

                // 음악 파일
                Text(stringResource(R.string.chime_file), style = MaterialTheme.typography.labelLarge)
                Text(
                    when {
                        s.isBuiltinBell -> stringResource(R.string.chime_bell)
                        else -> s.fileName.ifBlank { stringResource(R.string.chime_file_none) }
                    },
                    style = MaterialTheme.typography.bodyLarge,
                )
                TextButton(onClick = ::launchPicker) {
                    Icon(Icons.Filled.LibraryMusic, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.chime_pick))
                }
                // 파일 대신 앱에 들어 있는 괘종시계 종소리 (시각 수만큼 "뎅~")
                val bellName = stringResource(R.string.chime_bell)
                TextButton(onClick = {
                    // 이전에 고른 파일의 영구 권한은 반납
                    s.fileUri?.let { old ->
                        try {
                            context.contentResolver.releasePersistableUriPermission(
                                old, Intent.FLAG_GRANT_READ_URI_PERMISSION,
                            )
                        } catch (e: SecurityException) {
                            // 이미 없음
                        }
                    }
                    // 12번 치면 약 28초 → 중간에 끊기지 않게 연주 시간은 "끝까지" 로
                    update(
                        s.copy(
                            uri = ChimeSettings.BUILTIN_BELL,
                            fileName = bellName,
                            length = ChimeLength.ToEnd,
                            enabled = true,
                        ),
                    )
                    askNotificationPermission()
                }) {
                    Icon(Icons.Filled.Notifications, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.chime_use_bell))
                }
                Spacer(Modifier.height(8.dp))

                // 연주 시간
                Text(stringResource(R.string.chime_length), style = MaterialTheme.typography.labelLarge)
                Spacer(Modifier.height(4.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ChimeLength.entries.forEach { len ->
                        FilterChip(
                            selected = s.length == len,
                            onClick = { update(s.copy(length = len)) },
                            label = { Text(chimeLengthLabel(len)) },
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))

                // 건너뛸 시각: 체크한 시각에는 울리지 않음 (0시 ~ 23시)
                Text(stringResource(R.string.chime_skip), style = MaterialTheme.typography.labelLarge)
                FlowRow {
                    for (h in 0..23) {
                        Row(
                            Modifier
                                .width(76.dp)
                                .toggleable(
                                    value = s.isSkipped(h),
                                    role = Role.Checkbox,
                                    onValueChange = { update(s.withSkip(h, it)) },
                                ),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Checkbox(checked = s.isSkipped(h), onCheckedChange = null) // 줄 전체를 눌러도 체크
                            Text(stringResource(R.string.chime_hour, h), style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))

                // 들어보기 / 정지
                Row {
                    TextButton(
                        enabled = s.hasFile,
                        onClick = { ChimeService.preview(context) },
                    ) {
                        Icon(Icons.Filled.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(4.dp))
                        Text(stringResource(R.string.chime_preview))
                    }
                    TextButton(onClick = { ChimeService.stop(context) }) {
                        Icon(Icons.Filled.Stop, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(4.dp))
                        Text(stringResource(R.string.chime_stop))
                    }
                }
                Text(
                    stringResource(R.string.chime_note),
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

/** "매시 정각 (00분)", "매시 15분" … */
@Composable
private fun minuteLabel(m: Int): String =
    if (m == 0) stringResource(R.string.chime_minute_zero) else stringResource(R.string.chime_minute_format, m)

@Composable
private fun chimeLengthLabel(len: ChimeLength): String =
    if (len == ChimeLength.ToEnd) stringResource(len.unit) else stringResource(len.unit, len.amount)

/** 고른 파일의 이름 (예: "뻐꾸기.mp3"). 못 읽으면 주소 끝부분. */
private fun displayName(context: Context, uri: Uri): String = try {
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(0) else null
    }
} catch (e: Exception) {
    null
} ?: uri.lastPathSegment.orEmpty()
