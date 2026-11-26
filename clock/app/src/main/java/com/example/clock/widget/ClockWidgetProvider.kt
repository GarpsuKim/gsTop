package com.example.clock.widget

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.view.View
import android.widget.RemoteViews
import com.example.clock.R
import com.example.clock.UserStore
import com.example.clock.chime.ChimeScheduler

/**
 * 홈 화면 시계 위젯 (납작하고 긴 2줄)
 *  1줄 : 오전/오후 HH:MM (초 표시 모드면 :SS 까지)
 *  2줄 : MM월 DD일 X요일 + 오른쪽에 사용자 성명 (등록했을 때만)
 * 시간 갱신은 TextClock 이 스스로 하므로 알람/서비스가 필요 없음.
 * 한 번 누르기 = 시계 앱 열기, 두 번 누르기 = 긴급 연락처로 전화 ([WidgetTapActivity]).
 */
class ClockWidgetProvider : AppWidgetProvider() {

    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        update(context, manager, ids)
        // 위젯만 쓰는 경우에도 시보 정각 알람이 유지되도록 (위젯 갱신 때마다 다시 맞춤)
        ChimeScheduler.update(context)
    }

    override fun onDeleted(context: Context, ids: IntArray) {
        ids.forEach { WidgetColorStore.delete(context, it) }
    }

    companion object {
        /** 사용자 성명이 바뀌었을 때 홈 화면의 모든 위젯을 다시 그림. */
        fun updateAll(context: Context) {
            val manager = AppWidgetManager.getInstance(context) ?: return
            val ids = manager.getAppWidgetIds(ComponentName(context, ClockWidgetProvider::class.java))
            if (ids.isNotEmpty()) update(context, manager, ids)
        }

        private fun update(context: Context, manager: AppWidgetManager, ids: IntArray) {
            val tap = PendingIntent.getActivity(
                context, 0,
                Intent(context, WidgetTapActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )
            val name = UserStore.load(context).name
            for (id in ids) {
                val views = RemoteViews(context.packageName, R.layout.widget_clock)
                views.setOnClickPendingIntent(R.id.widget_root, tap)
                views.setTextViewText(R.id.widget_name, name)
                views.setViewVisibility(R.id.widget_name, if (name.isBlank()) View.GONE else View.VISIBLE)
                // 그룹마다 따로 정한 글자색 + 초 표시 (위젯 길게 누르기 → 위젯 설정)
                val colors = WidgetColorStore.load(context, id)
                views.setViewVisibility(R.id.widget_sec, if (colors.seconds) View.VISIBLE else View.GONE)
                views.setTextColor(R.id.widget_ampm, colors.time)
                views.setTextColor(R.id.widget_time, colors.time)
                views.setTextColor(R.id.widget_sec, colors.time)
                views.setTextColor(R.id.widget_date, colors.date)
                views.setTextColor(R.id.widget_name, colors.name)
                manager.updateAppWidget(id, views)
            }
        }
    }
}
