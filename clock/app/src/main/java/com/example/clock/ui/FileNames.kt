package com.example.clock.ui

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/** 파일의 표시 이름 (확장자 제외). 알 수 없으면 주소의 마지막 부분. */
fun queryDisplayName(context: Context, uri: Uri): String =
    runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    }.getOrNull()?.substringBeforeLast('.') ?: uri.lastPathSegment.orEmpty()

/**
 * 메뉴 맨 위의 "지금 파일" 줄 (누를 수 없음): "2/5  파일이름".
 * 한 개뿐이면 번호 없이 이름만.
 */
@Composable
fun CurrentFileItem(index: Int, count: Int, name: String) {
    val label = if (count > 1) "${index + 1}/$count  $name" else name
    DropdownMenuItem(
        text = {
            Text(
                label,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.widthIn(max = 240.dp),
            )
        },
        onClick = {},
        enabled = false,
    )
}
