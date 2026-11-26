package com.example.clock.split

import androidx.annotation.StringRes
import com.example.clock.R

/**
 * 자체 화면 분할: 시계 앱 화면 안에서 시계를 절반 자리에만 그리고, 나머지 절반은 투명(폰 배경화면이 보임).
 * 가로 화면 = 左/右, 세로 화면 = 上/下.
 */
// 순서 = 설정 저장값(ordinal). 새 항목은 끝에 추가할 것.
enum class SplitSide(@StringRes val label: Int) {
    Left(R.string.split_left),
    Right(R.string.split_right),
    Top(R.string.split_top),
    Bottom(R.string.split_bottom);

    /** 화면을 돌렸을 때 맞는 자리로: 가로 ↔ 세로 (左 ↔ 上, 右 ↔ 下). */
    fun forOrientation(landscape: Boolean): SplitSide = when {
        landscape && this == Top -> Left
        landscape && this == Bottom -> Right
        !landscape && this == Left -> Top
        !landscape && this == Right -> Bottom
        else -> this
    }

    companion object {
        /** 시계가 차지하는 비율: 기본 반반, 구분선을 끌어 25% ~ 75% 사이로. */
        const val DEFAULT_RATIO = 0.5f
        const val MIN_RATIO = 0.25f
        const val MAX_RATIO = 0.75f

        /** 가로 화면 → 左/右, 세로 화면 → 上/下. */
        fun sides(landscape: Boolean): List<SplitSide> =
            if (landscape) listOf(Left, Right) else listOf(Top, Bottom)
    }
}
