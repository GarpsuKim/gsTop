package com.example.clock.ui

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.example.clock.BuildConfig
import com.example.clock.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.commonmark.ext.gfm.tables.TablesExtension
import org.commonmark.node.Code
import org.commonmark.node.Heading
import org.commonmark.node.Node
import org.commonmark.node.Text as MdText
import org.commonmark.parser.Parser
import org.commonmark.renderer.html.AttributeProvider
import org.commonmark.renderer.html.HtmlRenderer

/** 소스 저장소 (GitHub). [정보] 창에 링크로. */
private const val GITHUB_URL = "https://github.com/GarpsuKim/gsTop/tree/main/clock"

/** 도움말에서 보여 줄 README 의 끝: 이 제목부터는 개발자용 (소스 구성, 빌드·설치, 권한). */
private const val README_USER_END = "\n## 소스 구성"

/** 도움말 웹 화면의 가짜 주소: 문서 안 링크(#제목)는 이 주소 안에서 스크롤, 그 밖의 상대 링크는 무시. */
private const val HELP_BASE_URL = "https://help.clock.local/"

/**
 * [도움말] 전체화면 창: 앱에 들어 있는 README.md (빌드할 때 복사됨) 의 사용자용 부분을 웹 화면으로.
 * 표는 가로 스크롤, 목차 링크(#…)는 문서 안 이동, http 링크는 브라우저로. [뒤로] / [←] = 닫기.
 */
@Composable
fun HelpDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    val html by produceState<String?>(null) {
        value = withContext(Dispatchers.IO) { runCatching { readmeHtml(context) }.getOrElse { errorHtml(it) } }
    }
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        Column(
            Modifier
                .fillMaxSize()
                .background(Color(HELP_BG))
                .windowInsetsPadding(WindowInsets.safeDrawing),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onDismiss) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.chime_close), tint = Color.White)
                }
                Text(stringResource(R.string.help_title), style = MaterialTheme.typography.titleMedium, color = Color.White)
            }
            HorizontalDivider(color = Color.White.copy(alpha = 0.2f))
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                val page = html
                if (page == null) {
                    CircularProgressIndicator()
                } else {
                    AndroidView(
                        modifier = Modifier.fillMaxSize(),
                        factory = { ctx -> helpWebView(ctx).also { it.loadDataWithBaseURL(HELP_BASE_URL, page, "text/html", "utf-8", null) } },
                    )
                }
            }
        }
    }
}

/** 도움말 웹 화면 바탕색 (HTML 의 CSS 와 같게 → 불러오는 동안 하얗게 번쩍이지 않음). */
private const val HELP_BG = 0xFF121212

@SuppressLint("SetJavaScriptEnabled")
private fun helpWebView(context: Context): WebView = WebView(context).apply {
    setBackgroundColor(HELP_BG.toInt())
    settings.javaScriptEnabled = false
    settings.builtInZoomControls = true // 두 손가락으로 확대·축소
    settings.displayZoomControls = false
    webViewClient = object : WebViewClient() {
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            val url = request.url
            // 문서 안 이동(#제목)은 같은 문서라 여기로 오지 않음. 그 밖에 가짜 주소 안(상대 링크) = 무시
            if (url.toString().startsWith(HELP_BASE_URL)) return true
            if (url.scheme == "http" || url.scheme == "https") openBrowser(view.context, url)
            return true
        }
    }
}

private fun openBrowser(context: Context, url: Uri) {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, url).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    } catch (_: ActivityNotFoundException) {
        Toast.makeText(context, R.string.calendar_not_found, Toast.LENGTH_SHORT).show()
    }
}

/** 앱에 들어 있는 README.md → 사용자용 부분만 HTML (어두운 화면용 CSS 포함). */
private fun readmeHtml(context: Context): String {
    val markdown = context.assets.open("README.md").bufferedReader(Charsets.UTF_8).use { it.readText() }
    val userPart = markdown.substringBefore(README_USER_END).trimEnd().removeSuffix("---").trimEnd()
    val extensions = listOf(TablesExtension.create())
    val parser = Parser.builder().extensions(extensions).build()
    val renderer = HtmlRenderer.builder()
        .extensions(extensions)
        .attributeProviderFactory { HeadingIdProvider() }
        .build()
    return page(renderer.render(parser.parse(userPart)))
}

private fun errorHtml(e: Throwable): String = page("<p>도움말을 불러오지 못했습니다.</p><pre>${e.toString().escapeHtml()}</pre>")

private fun String.escapeHtml() = replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

/**
 * 제목에 GitHub 과 같은 규칙으로 id 를 붙임 → README 의 목차 링크 (예: #시보-매시-연주) 가 앱에서도 동작.
 * 규칙: 소문자로, 글자·숫자·공백·-·_ 만 남기고, 공백은 - 로. 같은 id 가 또 나오면 -1, -2 …
 */
private class HeadingIdProvider : AttributeProvider {
    private val used = HashMap<String, Int>()

    override fun setAttributes(node: Node, tagName: String, attributes: MutableMap<String, String>) {
        if (node !is Heading) return
        val base = slug(plainText(node))
        val n = used[base]
        used[base] = (n ?: 0) + 1
        attributes["id"] = if (n == null) base else "$base-$n"
    }

    private fun plainText(node: Node): String = buildString {
        var child = node.firstChild
        while (child != null) {
            when (child) {
                is MdText -> append(child.literal)
                is Code -> append(child.literal)
                else -> append(plainText(child))
            }
            child = child.next
        }
    }

    private fun slug(text: String): String =
        text.trim().lowercase()
            .filter { it.isLetterOrDigit() || it == ' ' || it == '-' || it == '_' }
            .replace(' ', '-')
}

/** 본문 HTML 을 어두운 화면용 문서로 감쌈. 넓은 표는 가로로 스크롤. */
private fun page(body: String): String = """
<!DOCTYPE html>
<html lang="ko"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { background:#121212; color:#e6e6e6; font-family:sans-serif; font-size:15px; line-height:1.6; margin:0; padding:12px 16px 40px; }
  h1 { font-size:1.5em; color:#fff; }
  h2 { font-size:1.25em; color:#fff; border-bottom:1px solid #333; padding-bottom:4px; margin-top:1.6em; }
  h3 { font-size:1.08em; color:#9ecbff; margin-top:1.4em; }
  a { color:#7ab7ff; }
  hr { border:0; border-top:1px solid #333; margin:1.5em 0; }
  code { background:#262626; border-radius:4px; padding:1px 5px; font-size:0.9em; }
  pre { background:#1e1e1e; padding:10px; border-radius:6px; overflow-x:auto; }
  pre code { background:none; padding:0; }
  table { display:block; overflow-x:auto; border-collapse:collapse; margin:0.6em 0; font-size:0.92em; }
  th, td { border:1px solid #3a3a3a; padding:5px 8px; vertical-align:top; }
  th { background:#1f2a36; color:#fff; white-space:nowrap; }
  tr:nth-child(even) td { background:#181818; }
  ul { padding-left:1.3em; }
</style>
</head><body>
$body
</body></html>
"""

/**
 * [정보] 창: 앱 이름, 버전(빌드 날짜 + git 커밋), 빌드 시각, 앱 ID, 기기, 삼성 홈 화면 폴더 읽기 결과, GitHub, 오픈소스.
 * 문제가 생겼을 때 "폰에 깔린 게 어느 빌드인지" 바로 알 수 있게.
 */
@Composable
fun AboutDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    // 홈 화면 폴더 읽기 결과 (삼성 홈이 아니거나 막혔으면 0개 → "읽을 수 없음")
    val folderCount by produceState<Int?>(null) {
        value = withContext(Dispatchers.IO) { runCatching { loadHomeFolders(context).size }.getOrDefault(0) }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        // 기본 대화상자 폭(약 280~560dp, 가로 화면에서는 좁음)은 앱 ID·기기 이름이 단어 중간에서 꺾이므로 넓게
        properties = DialogProperties(usePlatformDefaultWidth = false),
        modifier = Modifier
            .fillMaxWidth(0.92f)
            .widthIn(max = 640.dp),
        title = { Text(stringResource(R.string.app_name)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                AboutRow(R.string.about_version, "${BuildConfig.VERSION_NAME} (${BuildConfig.GIT_HASH})")
                AboutRow(R.string.about_build_time, BuildConfig.BUILD_TIME)
                AboutRow(R.string.about_app_id, context.packageName)
                AboutRow(R.string.about_device, "${Build.MANUFACTURER} ${Build.MODEL} · Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
                AboutRow(
                    R.string.about_home_folders,
                    when (val n = folderCount) {
                        null -> "…"
                        0 -> stringResource(R.string.about_home_folders_none)
                        else -> stringResource(R.string.about_home_folders_count, n)
                    },
                )
                Row {
                    Text(
                        stringResource(R.string.about_source),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.width(AboutLabelWidth),
                    )
                    Text(
                        "GitHub gsTop/clock",
                        style = MaterialTheme.typography.bodyMedium.copy(textDecoration = TextDecoration.Underline),
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.clickable { openBrowser(context, Uri.parse(GITHUB_URL)) },
                    )
                }
                Spacer(Modifier.size(4.dp))
                Text(
                    stringResource(R.string.about_open_source),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    "AndroidX · Jetpack Compose · Material 3 (Apache 2.0)\n" +
                        "Media3 ExoPlayer (Apache 2.0)\n" +
                        "commonmark-java (BSD 2-Clause)\n" +
                        "YouTube IFrame Player API",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.chime_close)) }
        },
    )
}

private val AboutLabelWidth = 84.dp

@Composable
private fun AboutRow(label: Int, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(
            stringResource(label),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.width(AboutLabelWidth).padding(top = 2.dp),
        )
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}
