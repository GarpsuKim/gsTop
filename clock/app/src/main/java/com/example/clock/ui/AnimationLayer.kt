package com.example.clock.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import android.os.SystemClock
import kotlinx.coroutines.delay
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import com.example.clock.R
import java.time.LocalTime
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin
import kotlin.random.Random

/** 순서 = 저장값(ordinal). 새 애니메이션은 끝에 추가할 것. */
enum class DialAnimation(@StringRes val label: Int) {
    Off(R.string.anim_off),
    Stars(R.string.anim_stars),     // 화면 전체에 반짝이는 별
    Pulse(R.string.anim_pulse),     // 매초 테두리에서 퍼지는 링
    Comet(R.string.anim_comet),     // 테두리를 도는 혜성
    Sweep(R.string.anim_sweep),     // 초침 뒤 부채꼴 잔상
    Snow(R.string.anim_snow),       // 흔들리며 내리는 눈
    Glow(R.string.anim_glow),       // 숨쉬는 테두리 빛
    Rays(R.string.anim_rays),       // 회전하는 햇살
    Bubbles(R.string.anim_bubbles), // 떠오르는 방울
    Orbits(R.string.anim_orbits),   // 공전하는 행성
    Ripple(R.string.anim_ripple),   // 가운데서 퍼지는 물결
}

private const val TAU = (2 * PI).toFloat()


/** 위치가 고정된 난수 묶음 (매 프레임 같은 입자 배치). */
private class Seeds(val n: Int, seed: Int) {
    private val r = Random(seed)
    val x = FloatArray(n) { r.nextFloat() }
    val y = FloatArray(n) { r.nextFloat() }
    val speed = FloatArray(n) { r.nextFloat() }
    val phase = FloatArray(n) { r.nextFloat() }
    val size = FloatArray(n) { r.nextFloat() }
}

/**
 * 시계 뒤에 깔리는 애니메이션 레이어. 시계와 같은 Box 를 채우고,
 * 시계판은 이 영역 중앙에 (짧은 변 - 2*dialPadding) 지름으로 놓여 있다고 가정한다.
 */
@Composable
fun AnimationLayer(
    animation: DialAnimation,
    palette: (Float) -> Color,
    dialPadding: Dp,
    /** 한 장면 간격 (밀리초): 고급 모드 0 (화면 최대 속도), 전원 절약 모드 66 (초당 15번). */
    frameMs: Long = 0L,
    modifier: Modifier = Modifier,
) {
    if (animation == DialAnimation.Off) return

    var t by remember { mutableFloatStateOf(0f) }
    // 정해진 간격으로만 다시 그림 (화면 최대 속도 120번으로 그리면 폰이 뜨거워짐)
    LaunchedEffect(frameMs) {
        val start = SystemClock.elapsedRealtime() - (t * 1_000f).toLong() // 모드를 바꿔도 이어서
        while (true) {
            t = (SystemClock.elapsedRealtime() - start) / 1_000f
            awaitNextFrame(frameMs)
        }
    }
    val seeds = remember { Seeds(80, 7) }

    Canvas(modifier) {
        val radius = (size.minDimension - 2 * dialPadding.toPx()) / 2f
        val now = LocalTime.now()
        val sec = now.second + now.nano / 1_000_000_000f
        val time = t // 이 값을 읽어서 매 프레임 다시 그림

        when (animation) {
            DialAnimation.Off -> Unit
            DialAnimation.Stars -> stars(time, seeds, palette)
            DialAnimation.Pulse -> pulse(sec, radius, palette)
            DialAnimation.Comet -> comet(time, radius, palette)
            DialAnimation.Sweep -> sweep(sec, radius, palette)
            DialAnimation.Snow -> snow(time, seeds, palette)
            DialAnimation.Glow -> glow(time, radius, palette)
            DialAnimation.Rays -> rays(time, radius, palette)
            DialAnimation.Bubbles -> bubbles(time, seeds, palette)
            DialAnimation.Orbits -> orbits(time, radius, palette)
            DialAnimation.Ripple -> ripple(time, radius, palette)
        }
    }
}

private fun at(angleRad: Float, length: Float) =
    Offset(cos(angleRad) * length, sin(angleRad) * length)

/** 12시 = 0, 시계방향 0..1 을 라디안으로. */
private fun clockAngle(fraction: Float) = fraction * TAU - TAU / 4f

// 1. 화면 전체에 반짝이는 별
private fun DrawScope.stars(t: Float, s: Seeds, palette: (Float) -> Color) {
    for (i in 0 until s.n) {
        val twinkle = 0.5f + 0.5f * sin(t * (0.8f + s.speed[i] * 2.2f) + s.phase[i] * TAU)
        drawCircle(
            color = palette(s.phase[i]).copy(alpha = 0.1f + 0.9f * twinkle),
            radius = (1f + s.size[i] * 2f) * density,
            center = Offset(s.x[i] * size.width, s.y[i] * size.height),
        )
    }
}

// 2. 매초 시계 테두리에서 바깥으로 퍼지는 링
private fun DrawScope.pulse(sec: Float, radius: Float, palette: (Float) -> Color) {
    val frac = sec % 1f
    for (k in 0..1) {
        val p = (frac + k * 0.5f) % 1f
        drawCircle(
            color = palette(sec / 60f).copy(alpha = (1f - p) * 0.6f),
            radius = radius * (1f + 0.6f * p),
            center = center,
            style = Stroke(width = radius * 0.015f * (1f - p) + density),
        )
    }
}

// 3. 테두리 바로 바깥을 도는 혜성 두 개
private fun DrawScope.comet(t: Float, radius: Float, palette: (Float) -> Color) {
    val trail = 40
    for (c in 0..1) {
        val head = t / 6f + c * 0.5f
        for (k in 0 until trail) {
            val f = head - k * 0.004f
            val fade = 1f - k / trail.toFloat()
            drawCircle(
                color = palette(((f % 1f) + 1f) % 1f).copy(alpha = fade),
                radius = radius * 0.018f * fade + density,
                center = center + at(clockAngle(f), radius * 1.03f),
            )
        }
    }
}

// 4. 초침 뒤로 남는 부채꼴 잔상
private fun DrawScope.sweep(sec: Float, radius: Float, palette: (Float) -> Color) {
    val r = radius * 0.92f
    val head = sec * 6f - 90f
    val slices = 15
    val color = palette(sec / 60f)
    for (k in 0 until slices) {
        drawArc(
            color = color.copy(alpha = 0.35f * (1f - k / slices.toFloat())),
            startAngle = head - (k + 1) * 4f,
            sweepAngle = 4.2f,
            useCenter = true,
            topLeft = center - Offset(r, r),
            size = Size(r * 2, r * 2),
        )
    }
}

// 5. 흔들리며 내리는 눈
private fun DrawScope.snow(t: Float, s: Seeds, palette: (Float) -> Color) {
    for (i in 0 until s.n) {
        val speed = 0.03f + s.speed[i] * 0.08f
        val y = ((s.y[i] + t * speed) % 1f) * size.height
        val x = s.x[i] * size.width + sin(t * (0.5f + s.phase[i]) + i) * 12f * density
        drawCircle(
            color = palette(s.phase[i]).copy(alpha = 0.4f + 0.4f * s.size[i]),
            radius = (1.5f + s.size[i] * 2.5f) * density,
            center = Offset(x, y),
        )
    }
}

// 6. 테두리 둘레가 숨쉬듯 밝아졌다 어두워짐 (색도 천천히 바뀜)
private fun DrawScope.glow(t: Float, radius: Float, palette: (Float) -> Color) {
    val alpha = 0.3f + 0.3f * sin(t * TAU / 4f)
    val color = palette((t / 20f) % 1f).copy(alpha = alpha)
    val outer = radius * 1.3f
    drawCircle(
        brush = Brush.radialGradient(
            0f to Color.Transparent,
            0.70f to Color.Transparent,
            0.77f to color,
            1f to Color.Transparent,
            center = center,
            radius = outer,
        ),
        radius = outer,
        center = center,
    )
}

// 7. 시계 바깥으로 뻗어 천천히 도는 햇살
private fun DrawScope.rays(t: Float, radius: Float, palette: (Float) -> Color) {
    val n = 24
    for (i in 0 until n) {
        val f = i / n.toFloat() + t / 45f
        val angle = clockAngle(f)
        val alpha = 0.15f + 0.35f * (0.5f + 0.5f * sin(t * 2f + i))
        drawLine(
            color = palette(i / n.toFloat()).copy(alpha = alpha),
            start = center + at(angle, radius * 1.04f),
            end = center + at(angle, radius * 1.8f),
            strokeWidth = radius * 0.02f,
            cap = StrokeCap.Round,
        )
    }
}

// 8. 아래에서 떠오르는 방울
private fun DrawScope.bubbles(t: Float, s: Seeds, palette: (Float) -> Color) {
    for (i in 0 until s.n / 2) {
        val p = (s.y[i] + t * (0.04f + s.speed[i] * 0.08f)) % 1f
        val x = s.x[i] * size.width + sin(t * 1.3f + s.phase[i] * TAU) * 10f * density
        drawCircle(
            color = palette(s.phase[i]).copy(alpha = 0.1f + 0.5f * (1f - p)),
            radius = (4f + s.size[i] * 10f) * density,
            center = Offset(x, size.height * (1f - p)),
            style = Stroke(width = 1.5f * density),
        )
    }
}

// 9. 시계 둘레 궤도를 도는 행성 세 개 (번갈아 반대 방향)
private fun DrawScope.orbits(t: Float, radius: Float, palette: (Float) -> Color) {
    val orbits = listOf(1.06f to 7f, 1.16f to 11f, 1.26f to 17f)
    orbits.forEachIndexed { idx, (rf, period) ->
        val color = palette(idx / 3f)
        val r = radius * rf
        drawCircle(color.copy(alpha = 0.15f), r, center, style = Stroke(width = density))
        val dir = if (idx % 2 == 0) 1f else -1f
        val f = dir * t / period + idx * 0.3f
        for (k in 12 downTo 1) {
            drawCircle(
                color = color.copy(alpha = (1f - k / 12f) * 0.5f),
                radius = radius * 0.025f * (1f - k / 14f),
                center = center + at(clockAngle(f - dir * k * 0.006f), r),
            )
        }
        drawCircle(color, radius * 0.035f, center + at(clockAngle(f), r))
    }
}

// 10. 시계 가운데서 퍼져 나가는 물결
private fun DrawScope.ripple(t: Float, radius: Float, palette: (Float) -> Color) {
    val period = 3f
    for (k in 0 until 3) {
        val p = ((t / period) + k / 3f) % 1f
        drawCircle(
            color = palette(p).copy(alpha = 0.4f * (1f - p)),
            radius = radius * 0.92f * p,
            center = center,
            style = Stroke(width = radius * 0.012f),
        )
    }
}
