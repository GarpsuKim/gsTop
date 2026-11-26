package com.example.clock.widget

import android.content.Context
import androidx.core.content.edit

/** 위젯 설정: 3그룹(시각 / 날짜 / 성명) 글자색 (ARGB) + 초 표시 여부. 위젯마다 따로 저장. */
data class WidgetColors(
    val time: Int = WHITE,  // 1줄: 오전/오후 HH:MM(:SS)
    val date: Int = WHITE,  // 2줄 왼쪽: MM월 DD일 X요일
    val name: Int = WHITE,  // 2줄 오른쪽: 사용자 성명
    val seconds: Boolean = false, // 초 표시 모드
) {
    companion object {
        const val WHITE = 0xFFFFFFFF.toInt()

        /** 고를 수 있는 색. 밝은 배경화면용으로 회색·검정도 포함. */
        val PALETTE = listOf(
            0xFFFFFFFF, // 흰색
            0xFFFF1744, // 빨강
            0xFFFF9100, // 주황
            0xFFFFEA00, // 노랑
            0xFF00E676, // 초록
            0xFF00E5FF, // 하늘
            0xFF448AFF, // 파랑
            0xFFB388FF, // 보라
            0xFFFF80AB, // 분홍
            0xFF9E9E9E, // 회색
            0xFF000000, // 검정
        ).map { it.toInt() }
    }
}

object WidgetColorStore {
    private const val PREFS = "widget_colors"

    fun load(context: Context, widgetId: Int): WidgetColors {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return WidgetColors(
            time = p.getInt("time_$widgetId", WidgetColors.WHITE),
            date = p.getInt("date_$widgetId", WidgetColors.WHITE),
            name = p.getInt("name_$widgetId", WidgetColors.WHITE),
            seconds = p.getBoolean("sec_$widgetId", false),
        )
    }

    fun save(context: Context, widgetId: Int, colors: WidgetColors) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putInt("time_$widgetId", colors.time)
            putInt("date_$widgetId", colors.date)
            putInt("name_$widgetId", colors.name)
            putBoolean("sec_$widgetId", colors.seconds)
        }
    }

    /** 위젯을 홈 화면에서 지우면 그 위젯의 색 설정도 지움. */
    fun delete(context: Context, widgetId: Int) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            remove("time_$widgetId")
            remove("date_$widgetId")
            remove("name_$widgetId")
            remove("sec_$widgetId")
        }
    }
}
