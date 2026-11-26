package com.example.clock.chime

import android.content.Context
import android.net.Uri
import androidx.annotation.StringRes
import androidx.core.content.edit
import com.example.clock.R

/** 시보 연주 시간. 순서 = 저장값(ordinal) → 새 항목은 끝에 추가할 것. */
enum class ChimeLength(val seconds: Int, @StringRes val unit: Int, val amount: Int) {
    S5(5, R.string.unit_seconds, 5),
    S10(10, R.string.unit_seconds, 10),
    S15(15, R.string.unit_seconds, 15),
    S20(20, R.string.unit_seconds, 20),
    S30(30, R.string.unit_seconds, 30),
    M1(60, R.string.unit_minutes, 1),
    M2(2 * 60, R.string.unit_minutes, 2),
    M3(3 * 60, R.string.unit_minutes, 3),
    M5(5 * 60, R.string.unit_minutes, 5),
    M10(10 * 60, R.string.unit_minutes, 10),
    M15(15 * 60, R.string.unit_minutes, 15),
    M30(30 * 60, R.string.unit_minutes, 30),
    /** 곡 끝까지. 단, 다음 정각 시보와 겹치지 않게 최대 59분. */
    ToEnd(59 * 60, R.string.chime_to_end, 0),
}

/** 시보 설정: 켜짐 여부 + 음악 파일 + 연주 시간. 앱·위젯·알람 수신기가 함께 읽음. */
// 기본값 = 처음 설치했을 때의 시보 설정.
// (2026-11-25 사용 중이던 휴대폰 설정을 그대로 옮겨 옴: 켜짐 · 괘종시계 종소리 · 정각 · 끝까지 · 1~5시 건너뜀)
data class ChimeSettings(
    val enabled: Boolean = true,
    val uri: String = BUILTIN_BELL,
    /** 고른 음악 파일 이름 (괘종시계 종소리는 화면에서 따로 이름을 붙임). */
    val fileName: String = "",
    val length: ChimeLength = ChimeLength.ToEnd,
    /** 매시 몇 분에 울릴지 (0..59). 0 = 정각. */
    val minute: Int = 0,
    /** 울리지 않을 시각 (0~23시). 비트 n 이 1 이면 n시는 건너뜀. 기본: 1·2·3·4·5시 (새벽). */
    val skipHours: Int = 0b111110,
    /** true = 진동·무음 모드에서도 울림 (미디어 음량으로). false = 소리 모드일 때만. */
    val ringWhenSilent: Boolean = false,
) {
    fun isSkipped(hour: Int): Boolean = (skipHours shr hour) and 1 == 1

    fun withSkip(hour: Int, skip: Boolean): ChimeSettings =
        copy(skipHours = if (skip) skipHours or (1 shl hour) else skipHours and (1 shl hour).inv())

    val hasFile: Boolean get() = uri.isNotBlank()

    /** 파일 대신 앱에 들어 있는 괘종시계 종소리 (시각 수만큼 침). */
    val isBuiltinBell: Boolean get() = uri == BUILTIN_BELL

    /** 고른 음악 파일 주소 (괘종시계 종소리면 null). */
    val fileUri: Uri? get() = if (hasFile && !isBuiltinBell) Uri.parse(uri) else null

    companion object {
        const val BUILTIN_BELL = "builtin:grandfather_clock"
    }
}

object ChimeStore {
    private const val PREFS = "chime_prefs"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_URI = "uri"
    private const val KEY_FILE_NAME = "file_name"
    private const val KEY_LENGTH = "length"
    private const val KEY_MINUTE = "minute"
    private const val KEY_SKIP_HOURS = "skip_hours"
    private const val KEY_RING_WHEN_SILENT = "ring_when_silent"

    fun load(context: Context): ChimeSettings {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val d = ChimeSettings() // 저장된 값이 없는 항목은 기본값
        return ChimeSettings(
            enabled = p.getBoolean(KEY_ENABLED, d.enabled),
            uri = p.getString(KEY_URI, d.uri).orEmpty(),
            fileName = p.getString(KEY_FILE_NAME, d.fileName).orEmpty(),
            length = ChimeLength.entries.getOrElse(p.getInt(KEY_LENGTH, d.length.ordinal)) { d.length },
            minute = p.getInt(KEY_MINUTE, d.minute).coerceIn(0, 59),
            skipHours = p.getInt(KEY_SKIP_HOURS, d.skipHours),
            ringWhenSilent = p.getBoolean(KEY_RING_WHEN_SILENT, d.ringWhenSilent),
        )
    }

    /** 저장하고 다음 정각 알람을 다시 맞춤 (끄면 알람 취소). */
    fun save(context: Context, s: ChimeSettings) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putBoolean(KEY_ENABLED, s.enabled)
            putString(KEY_URI, s.uri)
            putString(KEY_FILE_NAME, s.fileName)
            putInt(KEY_LENGTH, s.length.ordinal)
            putInt(KEY_MINUTE, s.minute)
            putInt(KEY_SKIP_HOURS, s.skipHours)
            putBoolean(KEY_RING_WHEN_SILENT, s.ringWhenSilent)
        }
        ChimeScheduler.update(context)
    }
}
