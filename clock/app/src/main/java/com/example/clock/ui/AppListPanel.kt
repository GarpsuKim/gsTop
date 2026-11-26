package com.example.clock.ui

import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toBitmap
import com.example.clock.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** 앱 목록의 한 칸. */
private class LauncherApp(val label: String, val component: ComponentName, val icon: ImageBitmap)

/** 아이콘 크기. */
private val AppIconSize = 48.dp

/**
 * 자체 화면 분할의 빈 절반: 폰에 설치된 앱 목록 (홈 화면 앱 서랍과 같은 목록, 이름순).
 * 위에 검색창: 이름 일부 또는 초성으로 거름. 키보드가 뜨면 목록이 키보드 위로 줄어듦 (safeDrawing 여백).
 * 누르면 그 앱을 열고, 기기가 지원하면 시스템 화면 분할로 시계 옆에 엶 (FLAG_ACTIVITY_LAUNCH_ADJACENT).
 * 지원하지 않는 기기에서는 그냥 그 앱이 열림.
 */
@Composable
fun AppListPanel(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val iconPx = with(LocalDensity.current) { AppIconSize.roundToPx() }
    // 분할할 때마다 새로 읽음 (그 사이 설치·삭제한 앱 반영). 아이콘 변환은 화면이 멈추지 않게 백그라운드에서
    val apps by produceState<List<LauncherApp>?>(null, iconPx) {
        value = withContext(Dispatchers.IO) { loadLauncherApps(context, iconPx) }
    }

    // 검색어 (화면을 돌려도 유지)
    var query by rememberSaveable { mutableStateOf("") }
    val focusManager = LocalFocusManager.current

    Column(modifier.background(Color(0x55000000))) { // 배경화면 위에서도 글자가 보이게 살짝 어둡게
        // 검색창: 이름 일부 또는 초성 (예: "카톡", "ㅋㅋㅇ" → 카카오톡)
        OutlinedTextField(
            value = query,
            onValueChange = { query = it },
            modifier = Modifier
                .fillMaxWidth()
                .padding(start = 8.dp, end = 8.dp, top = 8.dp),
            placeholder = { Text(stringResource(R.string.app_search_hint)) },
            leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
            trailingIcon = if (query.isNotEmpty()) {
                {
                    IconButton(onClick = { query = "" }) {
                        Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.app_search_clear))
                    }
                }
            } else null,
            singleLine = true,
            shape = RoundedCornerShape(24.dp),
            textStyle = MaterialTheme.typography.bodyMedium,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = { focusManager.clearFocus() }), // 키보드 닫기
        )

        Box(
            Modifier
                .fillMaxWidth()
                .weight(1f),
            contentAlignment = Alignment.Center,
        ) {
        val all = apps
        val list = remember(all, query) { all?.filter { matchesSearch(it.label, query) } }
        if (list == null) {
            CircularProgressIndicator()
        } else if (list.isEmpty()) {
            Text(
                stringResource(R.string.app_search_none),
                style = MaterialTheme.typography.bodyMedium,
                color = Color.White.copy(alpha = 0.8f),
            )
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 76.dp),
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                items(list, key = { it.component.flattenToString() }) { app ->
                    Column(
                        Modifier
                            .clip(RoundedCornerShape(12.dp))
                            .clickable { launchApp(context, app.component) }
                            .padding(vertical = 8.dp, horizontal = 4.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Image(app.icon, contentDescription = null, modifier = Modifier.size(AppIconSize))
                        Spacer(Modifier.height(4.dp))
                        Text(
                            app.label,
                            style = MaterialTheme.typography.labelSmall,
                            color = Color.White,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                }
            }
        }
        }
    }
}

/** 한글 초성 (가 ~ 힣 의 첫 자음 순서). */
private const val CHOSUNG = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"

/** 한글 음절이면 그 초성, 아니면 그대로. */
private fun chosungOf(c: Char): Char =
    if (c in '가'..'힣') CHOSUNG[(c - '가') / 588] else c

/**
 * 앱 이름 검색: 이름 안에 검색어가 이어서 들어 있으면 맞음 (대소문자·띄어쓰기 무시).
 * 검색어의 자음 글자(ㄱ, ㅋ …)는 이름 글자의 초성과 비교 → "ㅋㅋㅇ" 도 "카카오톡" 에 맞음.
 * 글자와 초성을 섞어도 됨 ("카ㅋ오").
 */
private fun matchesSearch(label: String, query: String): Boolean {
    val q = query.filterNot { it.isWhitespace() }.lowercase()
    if (q.isEmpty()) return true
    val name = label.filterNot { it.isWhitespace() }.lowercase()
    if (q.length > name.length) return false
    for (start in 0..name.length - q.length) {
        var ok = true
        for (i in q.indices) {
            val qc = q[i]
            val nc = name[start + i]
            val same = if (qc in CHOSUNG) chosungOf(nc) == qc else nc == qc
            if (!same) {
                ok = false
                break
            }
        }
        if (ok) return true
    }
    return false
}

/** 홈 화면 앱 서랍에 나오는 앱들 (이 시계 앱 제외), 이름순. */
private fun loadLauncherApps(context: Context, iconPx: Int): List<LauncherApp> {
    val pm = context.packageManager
    val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
    return pm.queryIntentActivities(intent, 0)
        .filter { it.activityInfo.packageName != context.packageName }
        .mapNotNull { info ->
            runCatching {
                LauncherApp(
                    label = info.loadLabel(pm).toString(),
                    component = ComponentName(info.activityInfo.packageName, info.activityInfo.name),
                    icon = info.loadIcon(pm).toBitmap(iconPx, iconPx).asImageBitmap(),
                )
            }.getOrNull()
        }
        .sortedBy { it.label.lowercase() }
}

private fun launchApp(context: Context, component: ComponentName) {
    val intent = Intent(Intent.ACTION_MAIN)
        .addCategory(Intent.CATEGORY_LAUNCHER)
        .setComponent(component)
        .addFlags(
            Intent.FLAG_ACTIVITY_NEW_TASK or
                // 기기가 지원하면 시스템 화면 분할로 시계 옆에 열림
                Intent.FLAG_ACTIVITY_LAUNCH_ADJACENT,
        )
    try {
        context.startActivity(intent)
    } catch (_: ActivityNotFoundException) {
        Toast.makeText(context, R.string.app_open_failed, Toast.LENGTH_SHORT).show()
    } catch (_: SecurityException) {
        Toast.makeText(context, R.string.app_open_failed, Toast.LENGTH_SHORT).show()
    }
}
