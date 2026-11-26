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

    Box(
        modifier.background(Color(0x55000000)), // 배경화면 위에서도 글자가 보이게 살짝 어둡게
        contentAlignment = Alignment.Center,
    ) {
        val list = apps
        if (list == null) {
            CircularProgressIndicator()
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 76.dp),
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
