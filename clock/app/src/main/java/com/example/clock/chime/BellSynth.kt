package com.example.clock.chime

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.sin

/**
 * 괘종시계 종소리를 직접 만듦 (파일 없이). "뎅~" 을 [strikes] 번, 2초 간격으로.
 * 실제 종처럼 배음(정수배가 아닌 주파수들)을 겹치고, 높은 소리일수록 빨리 사라지게 해서
 * 때리는 순간은 "쨍" 하고 끝은 낮게 "웅~" 하며 길게 남음. 기본음 근처를 살짝 어긋나게 겹쳐 종 특유의 울렁임.
 */
object BellSynth {
    /** 샘플링 주파수. 가장 높은 배음(약 1.3kHz)에도 충분하고 메모리가 적게 듦 (12번 = 약 0.9MB). */
    const val RATE = 16_000

    /** 종을 치는 간격 (초). */
    private const val INTERVAL_S = 2.0

    /** 한 번 친 소리가 남는 길이 (초). */
    private const val RING_S = 6.0

    /** 기본음 (Hz). 괘종시계 종막대의 낮고 굵은 소리. */
    private const val F0 = 180.0

    /** (기본음 대비 주파수 비, 크기, 소리가 1/e 로 줄어드는 시간(초)) */
    private val partials = listOf(
        Triple(0.50, 0.30, 4.5),   // 낮은 웅~ (hum)
        Triple(1.00, 1.00, 3.2),   // 기본음
        Triple(1.004, 0.45, 3.0),  // 기본음과 살짝 어긋남 → 울렁임 (약 0.7번/초)
        Triple(1.19, 0.40, 2.4),   // 단3도 (종소리 특유의 애잔함)
        Triple(1.50, 0.28, 1.9),
        Triple(2.00, 0.55, 1.6),
        Triple(2.52, 0.24, 1.0),
        Triple(2.66, 0.20, 0.9),
        Triple(3.01, 0.17, 0.7),
        Triple(4.07, 0.12, 0.45),
        Triple(5.43, 0.09, 0.25),  // 때리는 순간의 쨍
        Triple(7.11, 0.06, 0.12),
    )

    /** [strikes] 번 친 종소리 (16비트 모노 PCM). */
    fun render(strikes: Int): ShortArray {
        val ring = strike()
        val step = (INTERVAL_S * RATE).toInt()
        val n = strikes.coerceAtLeast(1)
        val mix = FloatArray(step * (n - 1) + ring.size)
        for (k in 0 until n) {
            val off = k * step
            for (i in ring.indices) mix[off + i] += ring[i]
        }
        // 가장 큰 소리가 최대 음량의 85% 가 되게 맞춤 (찢어지는 소리 방지)
        var peak = 0f
        for (v in mix) peak = maxOf(peak, abs(v))
        val gain = if (peak > 0f) 0.85f * Short.MAX_VALUE / peak else 0f
        return ShortArray(mix.size) { (mix[it] * gain).toInt().toShort() }
    }

    /** 종을 한 번 친 소리. */
    private fun strike(): FloatArray {
        val size = (RING_S * RATE).toInt()
        val out = FloatArray(size)
        for ((ratio, amp, decay) in partials) {
            val w = 2.0 * PI * F0 * ratio / RATE
            val fall = exp(-1.0 / (decay * RATE)) // 한 샘플마다 줄어드는 비율
            var env = amp
            for (i in 0 until size) {
                out[i] += (env * sin(w * i)).toFloat()
                env *= fall
            }
        }
        // 시작 3ms 는 부드럽게 (딸깍 소리 방지), 끝 0.3초는 0 으로 (다음 소리와 겹칠 때 툭 끊김 방지)
        val attack = (0.003 * RATE).toInt()
        for (i in 0 until attack) out[i] *= i / attack.toFloat()
        val release = (0.3 * RATE).toInt()
        for (i in 0 until release) out[size - 1 - i] *= i / release.toFloat()
        return out
    }
}
