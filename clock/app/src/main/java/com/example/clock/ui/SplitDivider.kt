package com.example.clock.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.example.clock.split.SplitSide
import kotlin.math.roundToInt

/** 손가락으로 잡는 폭 (보이는 선보다 넓게). 양쪽 영역에 8dp 씩만 걸쳐서 옆 버튼·앱 아이콘을 덜 가림. */
private val DividerTouch = 16.dp

/**
 * 자체 화면 분할의 가운데 구분선: 끌어서 시계 / 앱 목록 비율 조절, 두 번 누르면 반반으로.
 * [ratio] = 시계가 차지하는 비율. [totalPx] = 나누는 방향의 화면 길이 (左/右 = 폭, 上/下 = 높이).
 * 끄는 동안은 [onRatioChange] 로 바로바로 반영하고, 손을 떼면 [onDragEnd] (저장).
 */
@Composable
fun BoxScope.SplitDivider(
    side: SplitSide,
    ratio: Float,
    totalPx: Float,
    onRatioChange: (Float) -> Unit,
    onDragEnd: () -> Unit,
    onReset: () -> Unit,
) {
    val touchPx = with(LocalDensity.current) { DividerTouch.toPx() }
    val verticalBar = side == SplitSide.Left || side == SplitSide.Right // 좌우 분할 → 세로 선
    val clockFirst = side == SplitSide.Left || side == SplitSide.Top    // 시계가 왼쪽/위
    // 구분선 위치 (화면 왼쪽/위 끝에서부터)
    val pos = if (clockFirst) ratio * totalPx else (1f - ratio) * totalPx

    val latestRatio by rememberUpdatedState(ratio)
    val latestOnChange by rememberUpdatedState(onRatioChange)
    val latestOnEnd by rememberUpdatedState(onDragEnd)
    val latestOnReset by rememberUpdatedState(onReset)

    Box(
        Modifier
            .align(Alignment.TopStart)
            .offset {
                val p = (pos - touchPx / 2f).roundToInt()
                if (verticalBar) IntOffset(p, 0) else IntOffset(0, p)
            }
            .then(
                if (verticalBar) Modifier.fillMaxHeight().width(DividerTouch)
                else Modifier.fillMaxWidth().height(DividerTouch),
            )
            .pointerInput(side, totalPx) {
                var startRatio = 0f
                var moved = 0f
                detectDragGestures(
                    onDragStart = {
                        startRatio = latestRatio
                        moved = 0f
                    },
                    onDragEnd = { latestOnEnd() },
                    onDragCancel = { latestOnEnd() },
                ) { change, amount ->
                    change.consume()
                    moved += if (verticalBar) amount.x else amount.y
                    // 시계가 왼쪽/위면 오른쪽/아래로 끌수록 시계가 커짐, 반대쪽이면 그 반대
                    val delta = (if (clockFirst) moved else -moved) / totalPx
                    latestOnChange((startRatio + delta).coerceIn(SplitSide.MIN_RATIO, SplitSide.MAX_RATIO))
                }
            }
            .pointerInput(Unit) { detectTapGestures(onDoubleTap = { latestOnReset() }) },
        contentAlignment = Alignment.Center,
    ) {
        // 가는 선
        Box(
            Modifier
                .then(if (verticalBar) Modifier.fillMaxHeight().width(2.dp) else Modifier.fillMaxWidth().height(2.dp))
                .background(Color.White.copy(alpha = 0.3f)),
        )
        // 가운데 손잡이
        Box(
            Modifier
                .size(if (verticalBar) 6.dp else 48.dp, if (verticalBar) 48.dp else 6.dp)
                .background(Color.White.copy(alpha = 0.8f), RoundedCornerShape(3.dp)),
        )
    }
}
