package com.example.clock.widget

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import com.example.clock.MainActivity
import com.example.clock.callEmergency

/**
 * 위젯 누르기 처리용 투명 화면 (사용자에게는 보이지 않음).
 * 위젯은 두 번 누르기를 직접 알 수 없으므로, 첫 번째 누름에서 이 투명 화면을 띄우고 잠깐 기다림:
 *  - 그 사이 한 번 더 누르면 (투명 화면 위든, 위젯 위든) → 긴급 연락처로 전화
 *  - 기다리는 동안 더 누르지 않으면 → 시계 앱을 전체화면으로 엶
 */
class WidgetTapActivity : Activity() {

    private val handler = Handler(Looper.getMainLooper())
    private var handled = false

    private val openClock = Runnable {
        finishWith {
            startActivity(
                Intent(this, MainActivity::class.java)
                    .putExtra(MainActivity.EXTRA_FROM_WIDGET, true) // 버튼 없이 전체화면으로 시작
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handler.postDelayed(openClock, DOUBLE_TAP_MS)
    }

    /** 투명 화면이 뜨기 전에 위젯을 한 번 더 누른 경우. */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        secondTap()
    }

    /** 투명 화면이 뜬 뒤 (위젯 자리 등) 화면을 한 번 더 누른 경우. */
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) secondTap()
        return true
    }

    private fun secondTap() = finishWith { callEmergency(this) }

    private fun finishWith(action: () -> Unit) {
        if (handled) return
        handled = true
        handler.removeCallbacks(openClock)
        action()
        finish()
        @Suppress("DEPRECATION")
        overridePendingTransition(0, 0) // 사라질 때도 깜빡임 없이
    }

    override fun onDestroy() {
        handler.removeCallbacks(openClock)
        super.onDestroy()
    }

    private companion object {
        /** 이 시간 안에 두 번째로 누르면 "두 번 누르기". */
        const val DOUBLE_TAP_MS = 400L
    }
}
