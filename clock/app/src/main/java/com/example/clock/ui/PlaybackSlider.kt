package com.example.clock.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

/**
 * 재생 위치 슬라이더 (메뉴 안에 넣는 용도). 0.25초마다 현재 위치를 읽어 움직이고,
 * 끌어서 놓으면 그 위치로 이동한다. 아래에 "1:23 / 3:45" 표시.
 * 길이를 아직 모르면(불러오는 중) 슬라이더는 비활성.
 */
@Composable
fun PlaybackSlider(
    positionMs: () -> Long,
    durationMs: () -> Long,
    onSeek: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    val readPosition by rememberUpdatedState(positionMs)
    val readDuration by rememberUpdatedState(durationMs)
    var position by remember { mutableLongStateOf(positionMs()) }
    var duration by remember { mutableLongStateOf(durationMs()) }
    var dragging by remember { mutableStateOf<Float?>(null) } // 끄는 중인 위치 (0..1)

    LaunchedEffect(Unit) {
        while (true) {
            duration = readDuration()
            if (dragging == null) position = readPosition()
            delay(250L)
        }
    }

    val fraction = dragging ?: if (duration > 0) (position.toFloat() / duration).coerceIn(0f, 1f) else 0f
    val shown = dragging?.let { (it * duration).toLong() } ?: position

    Column(modifier.padding(horizontal = 12.dp)) {
        Slider(
            value = fraction,
            onValueChange = { dragging = it },
            onValueChangeFinished = {
                val f = dragging
                if (f != null && duration > 0) {
                    val target = (f * duration).toLong()
                    onSeek(target)
                    position = target
                }
                dragging = null
            },
            enabled = duration > 0,
            modifier = Modifier.width(240.dp),
        )
        Text(
            "${formatTime(shown)} / ${formatTime(duration)}",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.align(Alignment.End),
        )
    }
}

/** 밀리초 → "m:ss" (1시간 이상이면 "h:mm:ss"). */
private fun formatTime(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    val h = total / 3600
    val m = (total % 3600) / 60
    val s = total % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
}
