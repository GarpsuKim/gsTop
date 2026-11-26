package com.example.clock.ui

import android.content.ComponentName
import android.content.Context
import android.net.Uri
import android.os.Bundle
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * 홈 화면 폴더(그룹) 하나.
 * [appKeys]: 폴더 안 앱들 (폴더 안 순서), 앱 목록의 [LauncherApp.key] 와 같은 "패키지/액티비티" 형식.
 */
internal class HomeFolder(val id: Long, val title: String, val appKeys: List<String>)

private const val TAG = "ClockFolders"

/**
 * 삼성 홈(One UI Home, HoneySpace) 의 배치 정보 창구. 읽기 권한: com.samsung.android.launcher.permission.READ_SETTINGS (일반 권한).
 * 이 버전은 launcher3 의 favorites 표는 비어 있고(null), call() 로 JSON 을 줌 (2026-11-26 S21 Ultra 에서 확인).
 */
private val SAMSUNG_LAUNCHER: Uri = Uri.parse("content://com.sec.android.app.launcher.settings")

/**
 * 홈 화면 배치를 주는 call() 이름. 삼성 홈 설정의 홈 화면 방식에 따라 한쪽만 채워져 있음:
 *  - get_home_item_info: "홈 화면과 앱스 화면" 방식
 *  - get_home_only_item_info: "홈 화면만 사용" 방식
 * 앞에서부터 폴더가 있는 첫 결과를 씀.
 */
private val HOME_METHODS = listOf("get_home_item_info", "get_home_only_item_info")

private const val CONTAINER_DESKTOP = -100L
private const val CONTAINER_HOTSEAT = -101L

/**
 * 홈 화면의 폴더들과 그 안의 앱.
 * 폴더 순서: 홈 화면 쪽 순서 → 위에서 아래 → 왼쪽에서 오른쪽, 마지막에 아래 고정줄(독) 폴더.
 * 삼성 홈이 아니거나, 읽기가 막혔거나, 형식이 다르면 빈 목록 (앱 목록은 폴더 없이 그대로).
 * 무엇을 읽었는지 logcat 의 "ClockFolders" 에 남김 (다른 폰·새 One UI 에서 안 될 때 원인 확인용).
 */
internal fun loadHomeFolders(context: Context): List<HomeFolder> {
    for (method in HOME_METHODS) {
        val items = try {
            val result = context.contentResolver.call(SAMSUNG_LAUNCHER, method, null, Bundle())
            @Suppress("DEPRECATION")
            val list = result?.get("result_list")
            // 보통 String[] 하나에 JSON 배열 전체가 들어 있음
            val json = when (list) {
                is Array<*> -> list.filterNotNull().joinToString("")
                is String -> list
                else -> null
            }
            if (json.isNullOrBlank()) emptyList() else JSONArray(json).objects()
        } catch (e: Exception) {
            // 권한 없음(삼성 폰이 아님), 없는 call 이름, JSON 형식 다름 등
            Log.w(TAG, "$method 읽기 실패: $e")
            emptyList()
        }
        val folders = foldersFrom(items)
        Log.i(TAG, "$method: 항목 ${items.size}개, 폴더 ${folders.size}개 " + folders.joinToString { "${it.title}(${it.appKeys.size})" })
        if (folders.isNotEmpty()) return folders
    }
    return emptyList()
}

private fun JSONArray.objects(): List<JSONObject> = (0 until length()).mapNotNull { optJSONObject(it) }

/** 배치 항목들 → 폴더 목록 (빈 폴더는 뺌). */
private fun foldersFrom(items: List<JSONObject>): List<HomeFolder> {
    val visible = items.filter { it.optString("hidden", "UNHIDDEN") == "UNHIDDEN" }
    // 폴더 안 항목: containerId = 폴더의 id. 앱(바로가기 포함)만, 폴더 안 순서(rank)대로
    val children = visible
        .filter { it.optString("type") == "app" || it.optString("type") == "shortcut" }
        .groupBy { it.optLong("containerId") }
    return visible
        .filter { it.optString("type") == "folder" }
        .sortedWith(
            compareBy<JSONObject>(
                { if (it.optLong("containerId") == CONTAINER_HOTSEAT) 1 else 0 }, // 독은 마지막
                { it.optInt("screen") },
                { it.optInt("positionY") },
                { it.optInt("positionX") },
                { it.optInt("rank") },
            ),
        )
        .mapNotNull { folder ->
            val keys = children[folder.optLong("id")].orEmpty()
                .sortedBy { it.optInt("rank") }
                .mapNotNull { child ->
                    // "패키지/.액티비티" → 패키지 이름을 붙인 전체 이름으로 (앱 목록 key 와 같게)
                    ComponentName.unflattenFromString(child.optString("component"))?.flattenToString()
                }
                .distinct()
            if (keys.isEmpty()) null else HomeFolder(folder.optLong("id"), folder.optString("title").trim(), keys)
        }
}
