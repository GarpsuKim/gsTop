package com.example.clock.widget

import android.appwidget.AppWidgetManager
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.clock.R
import com.example.clock.UserStore
import com.example.clock.ui.theme.ClockTheme
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.Locale

/**
 * 위젯 설정 팝업 (글자색 + 초 표시).
 * 위젯을 홈 화면에 추가할 때, 그리고 위젯을 길게 눌러 "위젯 설정"(연필)을 누를 때 열림 (Android 12 이상).
 * 3그룹(시각 / 날짜·요일 / 성명)을 각각 다른 색으로, 초 표시는 켜기/끄기.
 */
class WidgetColorActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val widgetId = intent?.extras?.getInt(
            AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID,
        ) ?: AppWidgetManager.INVALID_APPWIDGET_ID
        if (widgetId == AppWidgetManager.INVALID_APPWIDGET_ID) {
            finish()
            return
        }
        // 취소해도 위젯은 기본색(흰색)으로 추가되도록 항상 "완료"로 응답
        setResult(RESULT_OK, Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId))
        // 팝업을 열자마자 위젯을 한 번 그려 둠: 저장/취소 없이 홈 버튼으로 나가도
        // 위젯의 누르기(앱 열기 / 두 번 = 긴급 전화)가 바로 동작하도록
        ClockWidgetProvider.updateAll(this)

        val initial = WidgetColorStore.load(this, widgetId)
        val userName = UserStore.load(this).name
        setContent {
            ClockTheme(darkTheme = true) {
                WidgetColorDialog(
                    initial = initial,
                    userName = userName,
                    onSave = {
                        WidgetColorStore.save(this, widgetId, it)
                        close()
                    },
                    onCancel = ::close,
                )
            }
        }
    }

    /** 위젯을 다시 그리고 닫음 (처음 추가할 때는 이 화면이 위젯을 처음 그려야 함). */
    private fun close() {
        ClockWidgetProvider.updateAll(this)
        finish()
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun WidgetColorDialog(
    initial: WidgetColors,
    userName: String,
    onSave: (WidgetColors) -> Unit,
    onCancel: () -> Unit,
) {
    var colors by remember { mutableStateOf(initial) }
    val now = remember { LocalDateTime.now() }
    val locale = Locale.getDefault()

    @Composable
    fun ColorRow(label: String, selected: Int, onPick: (Int) -> Unit) {
        Text(label, style = MaterialTheme.typography.labelLarge)
        Spacer(Modifier.height(6.dp))
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            WidgetColors.PALETTE.forEach { c ->
                val isSelected = c == selected
                Box(
                    Modifier
                        .size(30.dp)
                        .background(Color(c), CircleShape)
                        .border(
                            width = if (isSelected) 3.dp else 1.dp,
                            color = if (isSelected) MaterialTheme.colorScheme.primary else Color.Gray,
                            shape = CircleShape,
                        )
                        .clickable { onPick(c) },
                )
            }
        }
        Spacer(Modifier.height(14.dp))
    }

    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(stringResource(R.string.widget_color_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                // 미리보기: 실제 위젯과 같은 모양 (반투명 검정 바탕, 납작한 2줄)
                Column(
                    Modifier
                        .fillMaxWidth()
                        .background(Color(0x66000000), RoundedCornerShape(16.dp))
                        .padding(vertical = 6.dp, horizontal = 8.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            now.format(DateTimeFormatter.ofPattern("a", locale)),
                            color = Color(colors.time), fontSize = 13.sp, fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(bottom = 4.dp),
                        )
                        Spacer(Modifier.width(5.dp))
                        Text(
                            now.format(DateTimeFormatter.ofPattern("hh:mm", locale)),
                            color = Color(colors.time), fontSize = 28.sp, fontWeight = FontWeight.Bold,
                        )
                        if (colors.seconds) {
                            Text(
                                now.format(DateTimeFormatter.ofPattern(":ss", locale)),
                                color = Color(colors.time), fontSize = 18.sp, fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(bottom = 3.dp),
                            )
                        }
                    }
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            now.format(DateTimeFormatter.ofPattern("MM월 dd일 EEEE", locale)),
                            color = Color(colors.date), fontSize = 13.sp,
                        )
                        Spacer(Modifier.width(10.dp))
                        Text(
                            userName.ifBlank { stringResource(R.string.user_name) },
                            color = Color(colors.name), fontSize = 13.sp, maxLines = 1,
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))
                // 모드: 초 표시 켜기/끄기
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clickable { colors = colors.copy(seconds = !colors.seconds) },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        stringResource(R.string.widget_seconds),
                        style = MaterialTheme.typography.labelLarge,
                        modifier = Modifier.weight(1f),
                    )
                    Switch(
                        checked = colors.seconds,
                        onCheckedChange = { colors = colors.copy(seconds = it) },
                    )
                }
                Spacer(Modifier.height(12.dp))
                ColorRow(stringResource(R.string.widget_line_time), colors.time) { colors = colors.copy(time = it) }
                ColorRow(stringResource(R.string.widget_line_date), colors.date) { colors = colors.copy(date = it) }
                ColorRow(stringResource(R.string.widget_line_name), colors.name) { colors = colors.copy(name = it) }
            }
        },
        confirmButton = {
            TextButton(onClick = { onSave(colors) }) { Text(stringResource(R.string.user_save)) }
        },
        dismissButton = {
            TextButton(onClick = onCancel) { Text(stringResource(R.string.cancel)) }
        },
    )
}
