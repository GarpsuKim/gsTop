package com.example.clock

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.provider.ContactsContract
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import com.example.clock.widget.ClockWidgetProvider

/** 사용자 등록 정보: 성명 + 긴급 연락처 (이름, 전화번호). */
data class UserInfo(
    val name: String = "",
    val emergencyName: String = "",
    val emergencyNumber: String = "",
)

/** 사용자 등록 정보 저장소. 앱과 위젯이 함께 읽음. */
object UserStore {
    private const val PREFS = "user_prefs"
    private const val KEY_NAME = "name"
    private const val KEY_EMERGENCY_NAME = "emergency_name"
    private const val KEY_EMERGENCY_NUMBER = "emergency_number"

    fun load(context: Context): UserInfo {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return UserInfo(
            name = p.getString(KEY_NAME, "").orEmpty(),
            emergencyName = p.getString(KEY_EMERGENCY_NAME, "").orEmpty(),
            emergencyNumber = p.getString(KEY_EMERGENCY_NUMBER, "").orEmpty(),
        )
    }

    /** 저장하고 홈 화면 위젯의 성명 줄도 바로 갱신. */
    fun save(context: Context, info: UserInfo) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putString(KEY_NAME, info.name)
            putString(KEY_EMERGENCY_NAME, info.emergencyName)
            putString(KEY_EMERGENCY_NUMBER, info.emergencyNumber)
        }
        ClockWidgetProvider.updateAll(context)
    }
}

/** 연락처 앱에서 고른 전화번호 한 건 (Phone.CONTENT_URI 의 항목) 에서 이름·번호를 읽음. */
fun readPickedPhone(context: Context, uri: Uri): Pair<String, String>? = try {
    context.contentResolver.query(
        uri,
        arrayOf(
            ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
            ContactsContract.CommonDataKinds.Phone.NUMBER,
        ),
        null, null, null,
    )?.use { c ->
        if (c.moveToFirst()) {
            val name = c.getString(0).orEmpty()
            val number = c.getString(1).orEmpty()
            if (number.isBlank()) null else name to number
        } else {
            null
        }
    }
} catch (e: SecurityException) {
    null
} catch (e: IllegalArgumentException) {
    null
}

/**
 * 긴급 연락처로 전화.
 * 전화 권한이 있으면 바로 걸고 (ACTION_CALL), 없으면 번호가 입력된 전화 앱을 엶 (ACTION_DIAL).
 */
fun callEmergency(context: Context) {
    val number = UserStore.load(context).emergencyNumber
    if (number.isBlank()) {
        Toast.makeText(context, R.string.user_emergency_missing, Toast.LENGTH_LONG).show()
        return
    }
    val canCall = ContextCompat.checkSelfPermission(context, Manifest.permission.CALL_PHONE) ==
        PackageManager.PERMISSION_GRANTED
    val intent = Intent(
        if (canCall) Intent.ACTION_CALL else Intent.ACTION_DIAL,
        Uri.parse("tel:" + Uri.encode(number)),
    ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(intent)
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, R.string.user_call_failed, Toast.LENGTH_SHORT).show()
    } catch (e: SecurityException) {
        Toast.makeText(context, R.string.user_call_failed, Toast.LENGTH_SHORT).show()
    }
}
