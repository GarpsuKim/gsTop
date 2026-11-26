package com.example.clock.ui

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.OutlinedButton
import androidx.compose.runtime.LaunchedEffect
import android.content.res.Configuration
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Surface
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import android.graphics.Color as AndroidColor
import android.net.Uri
import android.util.Log
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebView
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SmartDisplay
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.material.icons.filled.Check
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.example.clock.R
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.ui.Alignment

/** 유튜브 주소에서 뽑은 영상 ID / 재생목록 ID. */
data class YouTubeSource(val videoId: String?, val playlistId: String?)

private val VIDEO_ID = Regex("^[A-Za-z0-9_-]{11}$")

/**
 * 유튜브 주소 해석. 지원 형식:
 * youtu.be/ID, youtube.com/watch?v=ID, /shorts/ID, /live/ID, /embed/ID,
 * 재생목록(…&list=ID 또는 /playlist?list=ID), 영상 ID 11자리만.
 */
fun parseYouTube(input: String): YouTubeSource? {
    val s = input.trim()
    if (s.isEmpty()) return null
    if (VIDEO_ID.matches(s)) return YouTubeSource(s, null)
    val uri = runCatching { Uri.parse(if (s.startsWith("http")) s else "https://$s") }.getOrNull() ?: return null
    val host = uri.host?.lowercase() ?: return null
    if (!host.endsWith("youtube.com") && !host.endsWith("youtu.be") && !host.endsWith("youtube-nocookie.com")) {
        return null
    }
    val segments = uri.pathSegments
    val video = when {
        host.endsWith("youtu.be") -> segments.firstOrNull()
        uri.getQueryParameter("v") != null -> uri.getQueryParameter("v")
        segments.size >= 2 && segments[0] in listOf("shorts", "live", "embed", "v") -> segments[1]
        else -> null
    }?.takeIf { VIDEO_ID.matches(it) }
    val list = uri.getQueryParameter("list")?.takeIf { it.isNotBlank() && it.all { c -> c.isLetterOrDigit() || c == '_' || c == '-' } }
    if (video == null && list == null) return null
    return YouTubeSource(video, list)
}

private const val ORIGIN = "https://com.example.clock"
private const val LOG_TAG = "ClockYouTube"

/** 화면을 꽉 채우는(넘치는 부분은 잘림) 컨트롤 없는 반복 재생 플레이어 페이지. */
private fun playerHtml(source: YouTubeSource, sound: Boolean): String {
    val video = source.videoId?.let { "'$it'" } ?: "undefined"
    val list = source.playlistId?.let { "listType:'playlist', list:'$it'," } ?: ""
    // 영상 1개 반복은 playlist 에 자기 자신을 넣어야 함
    val loopList = if (source.playlistId == null && source.videoId != null) "playlist:'${source.videoId}'," else ""
    return """
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  html,body{margin:0;height:100%;background:#000;overflow:hidden}
  #player{position:fixed;top:50%;left:50%;
    width:max(100vw,177.78vh);height:max(100vh,56.25vw);
    transform:translate(-50%,-50%);pointer-events:none;border:0}
</style></head><body>
<div id="player"></div>
<script src="https://www.youtube.com/iframe_api"></script>
<script>
var player;
function onYouTubeIframeAPIReady(){
  player = new YT.Player('player',{
    videoId:$video,
    playerVars:{autoplay:1,mute:${if (sound) 0 else 1},controls:0,loop:1,$loopList$list
      playsinline:1,rel:0,modestbranding:1,iv_load_policy:3,disablekb:1,fs:0,origin:'$ORIGIN'},
    events:{
      onReady:function(e){console.log('YT ready');e.target.playVideo();},
      onStateChange:function(e){console.log('YT state '+e.data);
        if(e.data===0){e.target.seekTo(0);e.target.playVideo();}},
      onError:function(e){console.log('YT error '+e.data);}
    }});
}
</script></body></html>
""".trimIndent()
}

/**
 * 유튜브 영상 배경. 주소나 소리 설정이 바뀌면 플레이어를 새로 만든다.
 * 앱이 화면에서 사라지면 일시정지, 돌아오면 다시 재생 (축소 화면 중에는 계속 재생).
 */
@SuppressLint("SetJavaScriptEnabled")
@Composable
fun YouTubeBackground(url: String, sound: Boolean, modifier: Modifier = Modifier) {
    val source = remember(url) { parseYouTube(url) } ?: return
    val lifecycle = LocalLifecycleOwner.current.lifecycle

    key(source, sound) {
        var webView by remember { mutableStateOf<WebView?>(null) }

        DisposableEffect(lifecycle, webView) {
            val observer = LifecycleEventObserver { _, event ->
                // ON_STOP/ON_START 기준: 축소 화면(PIP)은 ON_PAUSE 만 오고 화면에는 보이므로 계속 재생
                when (event) {
                    Lifecycle.Event.ON_STOP -> webView?.onPause()
                    Lifecycle.Event.ON_START -> webView?.run {
                        onResume()
                        // 앱으로 돌아오면 플레이어는 일시정지 상태로 남으므로 직접 다시 재생
                        evaluateJavascript("if(window.player&&player.playVideo)player.playVideo();", null)
                    }
                    else -> Unit
                }
            }
            lifecycle.addObserver(observer)
            onDispose { lifecycle.removeObserver(observer) }
        }

        AndroidView(
            modifier = modifier,
            factory = { ctx ->
                WebView(ctx).apply {
                    setBackgroundColor(AndroidColor.BLACK)
                    settings.javaScriptEnabled = true
                    settings.domStorageEnabled = true
                    settings.mediaPlaybackRequiresUserGesture = false // 자동 재생
                    isFocusable = false
                    webChromeClient = object : WebChromeClient() {
                        override fun onConsoleMessage(message: ConsoleMessage): Boolean {
                            Log.d(LOG_TAG, message.message())
                            return true
                        }
                    }
                    loadDataWithBaseURL("$ORIGIN/", playerHtml(source, sound), "text/html", "utf-8", null)
                    webView = this
                }
            },
            onRelease = { it.destroy() },
        )
    }
}

/**
 * 공유받은 글에서 유튜브 주소를 찾는다.
 * (유튜브 앱은 "https://youtu.be/ID?si=..." 를, 다른 앱은 제목+주소 같은 글을 보내기도 함)
 */
fun extractYouTubeUrl(text: String?): String? {
    if (text.isNullOrBlank()) return null
    return Regex("""https?://\S+""").findAll(text).map { it.value }.firstOrNull { parseYouTube(it) != null }
        ?: text.trim().takeIf { parseYouTube(it) != null }
}

/** 유튜브 앱(없으면 브라우저)을 연다. 영상에서 "공유 → 시계" 를 누르면 주소가 돌아온다. */
private fun openYouTubeForPick(context: Context) {
    val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://www.youtube.com"))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(intent)
        Toast.makeText(context, R.string.youtube_pick_hint, Toast.LENGTH_LONG).show()
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, R.string.youtube_not_found, Toast.LENGTH_SHORT).show()
    }
}

/** 클립보드에 유튜브 주소가 있으면 돌려준다 (입력 창 미리 채우기용). */
private fun clipboardYouTube(context: Context): String? {
    val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return null
    val text = cm.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(context)?.toString()
    return text?.takeIf { parseYouTube(it) != null }
}

/** 유튜브 주소 입력 창 상태: 열림 여부 + 공유받은 주소. [YouTubeItems] (메뉴) 와 [YouTubeDialogHost] (창) 가 함께 씀. */
class YouTubeDialogState {
    var open by mutableStateOf(false)
    var shared by mutableStateOf<String?>(null)
}

/**
 * [배경 ▾] 메뉴의 "유튜브" 구역: [유튜브 주소 입력…] / [끄기] (켜져 있을 때만).
 * [close]: 메뉴 닫기. 주소 입력 창은 [YouTubeDialogHost] 가 그림.
 */
@Composable
fun YouTubeItems(
    url: String,
    sound: Boolean,
    onChange: (url: String, sound: Boolean) -> Unit,
    dialog: YouTubeDialogState,
    close: () -> Unit,
) {
    val active = url.isNotBlank()
    DropdownMenuItem(
        text = { Text(stringResource(R.string.youtube_change), style = MaterialTheme.typography.bodyMedium) },
        leadingIcon = { Icon(Icons.Filled.SmartDisplay, contentDescription = null, modifier = Modifier.size(18.dp)) },
        trailingIcon = if (active) {
            { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
        } else null,
        onClick = {
            close()
            dialog.open = true
        },
    )
    // 끄기 (항상 표시, 유튜브가 꺼져 있으면 흐리게)
    DropdownMenuItem(
        text = { Text(stringResource(R.string.youtube_off), style = MaterialTheme.typography.bodyMedium) },
        leadingIcon = { Icon(Icons.Filled.Close, contentDescription = null, modifier = Modifier.size(18.dp)) },
        enabled = active,
        onClick = {
            close()
            onChange("", sound)
        },
    )
}

/**
 * 유튜브 주소 입력 창: 유튜브 영상을 배경으로 재생하거나 끈다. 메뉴 바깥(버튼 쪽)에 두어,
 * 메뉴가 닫혀 있어도 [incoming] (다른 앱에서 "공유 → 시계" 로 받은 주소) 이 들어오면 창을 열고 주소를 채움.
 */
@Composable
fun YouTubeDialogHost(
    url: String,
    sound: Boolean,
    onChange: (url: String, sound: Boolean) -> Unit,
    dialog: YouTubeDialogState,
    incoming: String? = null,
    onIncomingConsumed: () -> Unit = {},
) {
    val context = LocalContext.current
    var open by dialog::open
    var shared by dialog::shared

    LaunchedEffect(incoming) {
        if (incoming != null) {
            shared = incoming
            open = true
            onIncomingConsumed()
        }
    }

    if (open) {
        // 공유받은 주소가 있으면 그걸로, 없으면 현재 주소 → 클립보드 순
        var input by remember(shared) {
            mutableStateOf(shared ?: url.ifBlank { clipboardYouTube(context).orEmpty() })
        }
        var withSound by remember { mutableStateOf(sound) }
        val valid = parseYouTube(input) != null

        // 가로 모드는 화면 높이가 낮아 내용이 잘리므로 팝업 전체(글자·버튼·여백)를 함께 축소
        val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
        val baseDensity = LocalDensity.current
        val dialogDensity = if (landscape) {
            Density(baseDensity.density * LANDSCAPE_DIALOG_SCALE, baseDensity.fontScale)
        } else {
            baseDensity
        }
        val gap = if (landscape) 4.dp else 8.dp

        val close = {
            open = false
            shared = null
        }

        // Dialog 는 별도 창이라 화면 배율을 자체적으로 다시 정하므로, 축소는 창 "안쪽"에서 적용
        Dialog(
            onDismissRequest = close,
            properties = DialogProperties(usePlatformDefaultWidth = false),
        ) {
            CompositionLocalProvider(LocalDensity provides dialogDensity) {
                Surface(
                    shape = MaterialTheme.shapes.extraLarge,
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    tonalElevation = 6.dp,
                    modifier = Modifier
                        .padding(24.dp)
                        .widthIn(min = 280.dp, max = 560.dp),
                ) {
                    Column(Modifier.padding(24.dp)) {
                        Text(
                            stringResource(R.string.youtube_title),
                            style = MaterialTheme.typography.headlineSmall,
                        )
                        Spacer(Modifier.height(16.dp))
                        // 그래도 넘치면 잘리지 않고 스크롤 (버튼 줄은 항상 보이게 남김)
                        Column(
                            modifier = Modifier
                                .weight(1f, fill = false)
                                .verticalScroll(rememberScrollState()),
                            verticalArrangement = Arrangement.spacedBy(gap),
                        ) {
                            // 유튜브를 열어 영상에서 "공유 → 시계" 를 누르면 주소가 여기로 들어옴
                            OutlinedButton(
                                onClick = { openYouTubeForPick(context) },
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Icon(Icons.Filled.SmartDisplay, contentDescription = null, modifier = Modifier.size(18.dp))
                                Spacer(Modifier.width(6.dp))
                                Text(stringResource(R.string.youtube_pick))
                            }
                            Text(
                                stringResource(R.string.youtube_pick_desc),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            OutlinedTextField(
                                value = input,
                                onValueChange = { input = it },
                                singleLine = true,
                                label = { Text(stringResource(R.string.youtube_hint)) },
                                isError = input.isNotBlank() && !valid,
                                supportingText = if (input.isNotBlank() && !valid) {
                                    { Text(stringResource(R.string.youtube_invalid)) }
                                } else null,
                                modifier = Modifier.fillMaxWidth(),
                            )
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                androidx.compose.material3.Checkbox(checked = withSound, onCheckedChange = { withSound = it })
                                Text(stringResource(R.string.youtube_sound))
                            }
                        }
                        Spacer(Modifier.height(16.dp))
                        // 버튼 줄: [끄기] [취소] [재생]
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End,
                        ) {
                            if (url.isNotBlank()) {
                                TextButton(onClick = {
                                    close()
                                    onChange("", withSound)
                                }) { Text(stringResource(R.string.youtube_off)) }
                            }
                            TextButton(onClick = close) { Text(stringResource(R.string.cancel)) }
                            TextButton(
                                enabled = valid,
                                onClick = {
                                    close()
                                    onChange(input.trim(), withSound)
                                },
                            ) { Text(stringResource(R.string.youtube_play)) }
                        }
                    }
                }
            }
        }
    }
}

/** 가로 모드에서 유튜브 팝업 전체 축소 비율 (글자·버튼·여백 모두). */
private const val LANDSCAPE_DIALOG_SCALE = 0.75f
