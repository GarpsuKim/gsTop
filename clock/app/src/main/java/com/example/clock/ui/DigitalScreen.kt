package com.example.clock.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import android.os.SystemClock
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableFloatStateOf
import kotlinx.coroutines.delay
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.draw.BlurredEdgeTreatment
import androidx.compose.ui.draw.blur
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.foundation.layout.wrapContentSize
import com.example.clock.R
import java.time.LocalDate
import java.time.LocalTime
import androidx.compose.foundation.layout.Column
import java.time.format.DateTimeFormatter

/** 화면 표시 방식. 순서 = 저장값(ordinal). */
enum class DisplayMode(@StringRes val label: Int) {
    Analog(R.string.mode_analog),     // 아날로그만
    Hybrid(R.string.mode_hybrid),     // 하이브리드: 아날로그 + 시계판 안 요일/날짜
    Digital(R.string.mode_digital),   // 디지털: 화면 전체에 큰 HH:MM:SS
}

/** 디지털 글자색. 순서 = 저장값(ordinal). */
enum class DigitalColor(@StringRes val label: Int, val brush: Brush) {
    White(R.string.color_white, SolidColor(Color(0xFFFFFFFF))),
    Red(R.string.color_red, SolidColor(Color(0xFFFF1744))),
    Orange(R.string.color_orange, SolidColor(Color(0xFFFF9100))),
    Yellow(R.string.color_yellow, SolidColor(Color(0xFFFFEA00))),
    Green(R.string.color_green, SolidColor(Color(0xFF00E676))),
    Cyan(R.string.color_cyan, SolidColor(Color(0xFF00E5FF))),
    Blue(R.string.color_blue, SolidColor(Color(0xFF448AFF))),
    Purple(R.string.color_purple, SolidColor(Color(0xFFB388FF))),
    Pink(R.string.color_pink, SolidColor(Color(0xFFFF80AB))),
    Rainbow(
        R.string.color_rainbow,
        Brush.horizontalGradient(List(7) { Color.hsv(it / 6f * 300f, 0.85f, 0.95f) }),
    ),
}

/** 디지털 글자 스크롤 방향. 순서 = 저장값(ordinal). */
enum class DigitalScroll(@StringRes val label: Int) {
    Off(R.string.scroll_off),
    Left(R.string.scroll_left),   // ← 오른쪽 밖에서 들어와 왼쪽 밖으로, 반복
    Right(R.string.scroll_right), // → 왼쪽 밖에서 들어와 오른쪽 밖으로, 반복
    Both(R.string.scroll_both),   // ↔ 왼쪽으로 빠져나갔다가 같은 길로 되돌아옴, 반복
    Up(R.string.scroll_up),       // ↑ 아래 밖에서 들어와 위 밖으로, 반복
    Down(R.string.scroll_down),   // ↓ 위 밖에서 들어와 아래 밖으로, 반복
}

/** 디지털 날짜·요일 줄 위치. 순서 = 저장값(ordinal). */
enum class DigitalDate(@StringRes val label: Int) {
    Off(R.string.date_off),
    Top(R.string.date_top),       // 날짜(상): 시각 위
    Bottom(R.string.date_bottom), // 날짜(하): 시각 아래
}

/** 날짜 줄 글자 크기 = 시분초 글자의 이 비율. */
private const val DATE_RATIO = 0.5f

private val dateFormatter = DateTimeFormatter.ofPattern("M월 d일 EEEE")

/** 날짜 줄이 가장 넓은 경우 (크기 계산용). */
private const val DATE_PROBE = "12월 30일 수요일"

/** 화면을 한 번 가로지르는 데 걸리는 시간 (밀리초). 작을수록 빠름. */
private const val SCROLL_PASS_MS = 12_000

private val hmsFormatter = DateTimeFormatter.ofPattern("HH:mm:ss") // 24시간제

/**
 * 디지털: 화면 가운데에 큰 "AM 13:05:09".
 * AM/PM 은 24시간제여도 항상 앞에 약간 작게 표시.
 * 글자 크기는 화면 너비(가로 모드에서는 높이도)에 맞춰 자동 결정.
 */
@Composable
fun BigDigitalClock(
    time: LocalTime,
    color: DigitalColor,
    /** 날짜·요일 줄 글자색 (날짜를 켰을 때). */
    dateColor: DigitalColor = color,
    bold: Boolean,
    neon: Boolean,
    scroll: DigitalScroll,
    powerMode: PowerMode = PowerMode.Advanced,
    modifier: Modifier = Modifier,
    /** 오늘 날짜 (날짜 줄에 표시). */
    date: LocalDate = LocalDate.now(),
    /** 날짜·요일 줄: 끔 / 시각 위 / 시각 아래. */
    datePosition: DigitalDate = DigitalDate.Off,
) {
    val measurer = rememberTextMeasurer()
    val density = LocalDensity.current
    val baseStyle = MaterialTheme.typography.displayLarge.copy(
        brush = color.brush,
        // 굵게 켜면 Bold. 글자 폭이 넓어지므로 크기는 아래에서 다시 재서 맞춤
        fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
        fontFeatureSettings = "tnum", // 숫자 폭 고정: 초가 바뀌어도 글자가 흔들리지 않음
    )
    fun text(amPm: String, hms: String, timeSize: TextUnit) = buildAnnotatedString {
        withStyle(SpanStyle(fontSize = timeSize * AM_PM_RATIO)) { append("$amPm ") }
        append(hms)
    }

    BoxWithConstraints(modifier, contentAlignment = Alignment.Center) {
        // 가장 넓은 경우("AM 00:00:00")를 기준 크기로 한 번 재서, 화면 너비에 꽉 차게 확대
        val maxW = constraints.maxWidth.toFloat()
        val maxH = constraints.maxHeight.toFloat()
        // 날짜 줄을 켜면 그 높이(시분초의 절반)까지 합쳐서 화면에 들어가게
        val showDate = datePosition != DigitalDate.Off
        val timeSize = remember(maxW, maxH, baseStyle, showDate) {
            val base = 100.sp
            val probe = measurer.measure(
                text("AM", "00:00:00", base),
                style = baseStyle.copy(fontSize = base, lineHeight = base),
                softWrap = false,
            )
            var width = probe.size.width
            var height = probe.size.height
            if (showDate) {
                val dateBase = base * DATE_RATIO
                val dateProbe = measurer.measure(
                    DATE_PROBE,
                    style = baseStyle.copy(fontSize = dateBase, lineHeight = dateBase),
                    softWrap = false,
                )
                width = maxOf(width, dateProbe.size.width)
                height += dateProbe.size.height
            }
            val scale = minOf(
                maxW * 0.97f / width,
                maxH * 0.9f / height,
            )
            with(density) { (base.toPx() * scale).toSp() }
        }
        val amPm = if (time.hour < 12) "AM" else "PM"
        val content = text(amPm, time.format(hmsFormatter), timeSize)
        val style = baseStyle.copy(fontSize = timeSize, lineHeight = timeSize)

        @Composable
        fun line(text: AnnotatedString, lineStyle: TextStyle, brush: Brush) {
            if (neon) {
                // 전원 절약 모드: 흐림(블러) 없는 가벼운 네온
                NeonText(text, lineStyle, brush, bold, glow = powerMode == PowerMode.Advanced)
            } else {
                Text(text = text, style = lineStyle, maxLines = 1, softWrap = false)
            }
        }

        // 날짜·요일 줄: 시분초의 절반 크기, 날짜색으로. 굵기·네온은 시각과 같게
        val dateSize = timeSize * DATE_RATIO
        val dateStyle = baseStyle.copy(brush = dateColor.brush, fontSize = dateSize, lineHeight = dateSize)
        val dateLine = AnnotatedString(date.format(dateFormatter))

        val clockText: @Composable () -> Unit = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                if (datePosition == DigitalDate.Top) line(dateLine, dateStyle, dateColor.brush)
                line(content, style, color.brush)
                if (datePosition == DigitalDate.Bottom) line(dateLine, dateStyle, dateColor.brush)
            }
        }

        val margin = with(density) { 32.dp.toPx() } // 화면 밖 여유 (네온 번짐까지 완전히 사라지게)
        var contentWidth by remember { mutableIntStateOf(0) }
        var contentHeight by remember { mutableIntStateOf(0) }
        val centerX = (maxW - contentWidth) / 2f
        val centerY = (maxH - contentHeight) / 2f
        val latestMaxW by rememberUpdatedState(maxW)
        val latestMaxH by rememberUpdatedState(maxH)

        // 글자 왼쪽 위 모서리의 위치 (x, y). NaN = 가운데.
        // 스크롤이 이 값을 움직이므로, 방향을 바꿔도 글자가 튀지 않고 그 자리에서 이어짐.
        // (글자를 손가락으로 끄는 기능은 없앰: 한 손가락 스와이프는 화면 분할·앱 종료)
        var x by remember { mutableFloatStateOf(Float.NaN) }
        var y by remember { mutableFloatStateOf(Float.NaN) }
        var bounceDir by remember { mutableFloatStateOf(-1f) } // ↔ 에서 지금 가는 방향 (-1 = 왼쪽)

        // 스크롤을 끄면 가운데로
        LaunchedEffect(scroll) {
            if (scroll == DigitalScroll.Off) {
                x = Float.NaN
                y = Float.NaN
            }
        }

        // 일정한 속도로 이동 (고급 모드는 매 프레임, 전원 절약 모드는 frameMs 간격). 드래그 중에는 멈춤.
        LaunchedEffect(scroll, powerMode.frameMs) {
            if (scroll == DigitalScroll.Off) return@LaunchedEffect
            val vertical = scroll == DigitalScroll.Up || scroll == DigitalScroll.Down
            var last = SystemClock.elapsedRealtime()
            while (true) {
                awaitNextFrame(powerMode.frameMs)
                val now = SystemClock.elapsedRealtime()
                val dt = (now - last).coerceAtMost(200L) // 잠깐 멈췄다 돌아와도 크게 튀지 않게
                last = now
                if (contentWidth == 0 || contentHeight == 0) continue
                // 글자 크기·화면 크기는 실행 중에 바뀔 수 있으므로 (첫 측정, 회전) 매번 최신 값으로 계산
                val w = latestMaxW
                val h = latestMaxH
                val cx = (w - contentWidth) / 2f
                val cy = (h - contentHeight) / 2f
                // 스크롤 방향(축)이 아닌 쪽은 가운데로 부드럽게 돌아감 (드래그로 비껴 놓아도 제자리로)
                val ease = (dt / 250f).coerceAtMost(1f)
                fun toward(cur: Float, center: Float) =
                    if (cur.isNaN()) center else cur + (center - cur) * ease

                if (vertical) {
                    val offTop = -contentHeight - margin
                    val offBottom = h + margin
                    val speed = (offBottom - offTop) / SCROLL_PASS_MS
                    var cur = if (y.isNaN()) cy else y
                    if (scroll == DigitalScroll.Up) {
                        cur -= speed * dt
                        if (cur < offTop) cur = offBottom      // 위로 빠져나가면 아래에서 다시
                    } else {
                        cur += speed * dt
                        if (cur > offBottom) cur = offTop      // 아래로 빠져나가면 위에서 다시
                    }
                    y = cur
                    x = toward(x, cx)
                } else {
                    val offLeft = -contentWidth - margin
                    val offRight = w + margin
                    val speed = (offRight - offLeft) / SCROLL_PASS_MS // 화면을 한 번 가로지르는 시간은 그대로
                    var cur = if (x.isNaN()) cx else x
                    when (scroll) {
                        DigitalScroll.Left -> {
                            cur -= speed * dt
                            if (cur < offLeft) cur = offRight  // 왼쪽으로 빠져나가면 오른쪽에서 다시
                        }
                        DigitalScroll.Right -> {
                            cur += speed * dt
                            if (cur > offRight) cur = offLeft  // 오른쪽으로 빠져나가면 왼쪽에서 다시
                        }
                        else -> { // Both
                            cur += bounceDir * speed * dt
                            if (cur < offLeft) { cur = offLeft; bounceDir = 1f }
                            if (cur > offRight) { cur = offRight; bounceDir = -1f }
                        }
                    }
                    x = cur
                    y = toward(y, cy)
                }
            }
        }

        Box(
            Modifier
                .fillMaxSize(),
            contentAlignment = Alignment.TopStart,
        ) {
            Box(
                Modifier
                    .wrapContentSize(Alignment.TopStart, unbounded = true)
                    .onSizeChanged {
                        contentWidth = it.width
                        contentHeight = it.height
                    }
                    .graphicsLayer {
                        translationX = if (x.isNaN()) centerX else x
                        translationY = if (y.isNaN()) centerY else y
                    },
            ) {
                clockText()
            }
        }
    }
}

/**
 * 네온사인: 글자를 유리관처럼 외곽선으로 그리고
 * [넓은 빛 번짐] + [좁은 빛 번짐] + [색 유리관] + [관 가운데 흰 심지] 를 겹친다.
 * (빛 번짐 blur 는 Android 12 이상에서 보임. 그 아래 버전은 번짐 없이 관만 보임)
 */
@Composable
private fun NeonText(
    content: AnnotatedString,
    style: TextStyle,
    brush: Brush,
    bold: Boolean,
    /** false = 전원 절약: 흐림(블러) 대신 옅은 굵은 테두리 1겹으로 빛 표현. */
    glow: Boolean = true,
) {
    val density = LocalDensity.current
    val sizePx = with(density) { style.fontSize.toPx() }
    val tube = sizePx * if (bold) 0.055f else 0.035f // 관 굵기
    val wideGlow = with(density) { (sizePx * 0.12f).toDp() }
    val nearGlow = with(density) { (sizePx * 0.04f).toDp() }

    @Composable
    fun layer(width: Float, layerBrush: Brush, alpha: Float, blur: Dp?) {
        Text(
            text = content,
            style = style.copy(
                brush = layerBrush,
                alpha = alpha,
                drawStyle = Stroke(width = width, join = StrokeJoin.Round, cap = StrokeCap.Round),
            ),
            maxLines = 1,
            softWrap = false,
            modifier = if (blur != null) {
                Modifier.blur(blur, BlurredEdgeTreatment.Unbounded)
            } else {
                Modifier
            },
        )
    }

    Box(contentAlignment = Alignment.Center) {
        if (glow) {
            layer(tube * 3f, brush, 0.9f, wideGlow)                 // 넓은 빛 번짐
            layer(tube * 1.8f, brush, 1f, nearGlow)                 // 관 주위 빛
        } else {
            layer(tube * 2.6f, brush, 0.35f, null)                  // 가벼운 빛 (흐림 없음)
        }
        layer(tube, brush, 1f, null)                                // 색 유리관
        layer(tube * 0.35f, SolidColor(Color.White), 0.85f, null)  // 가운데 흰 심지
    }
}

/** AM/PM 글자 크기 = 시분초 글자의 이 비율. */
private const val AM_PM_RATIO = 0.4f

/** 목록에서 색을 보여주는 작은 동그라미. */
@Composable
fun ColorSwatch(color: DigitalColor, modifier: Modifier = Modifier) {
    Box(
        modifier
            .size(14.dp)
            .background(color.brush, CircleShape),
    )
}
