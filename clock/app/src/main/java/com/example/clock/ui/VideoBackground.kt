package com.example.clock.ui

import android.app.Application
import android.content.Intent
import android.net.Uri
import android.view.LayoutInflater
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.VideoLibrary
import androidx.compose.material.icons.filled.Repeat
import androidx.compose.material.icons.filled.RepeatOne
import androidx.compose.material3.Checkbox
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material.icons.filled.VideocamOff
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.AndroidViewModel
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import com.example.clock.R

/**
 * 배경(동영상) 재생기. 화면 회전에도 끊기지 않도록 ViewModel 에 둔다.
 * 여러 동영상을 차례로 이어서 재생하고, 끝나면 처음부터 반복.
 */
class VideoBackgroundViewModel(app: Application) : AndroidViewModel(app) {
    private var exo: ExoPlayer? = null
    private var current: List<String> = emptyList()

    /** 지금 나오는 동영상 번호 (메뉴에 파일 이름 표시용). */
    var currentIndex by mutableIntStateOf(0)
        private set

    /** 재생기 (처음 쓸 때 만든다). */
    val player: ExoPlayer
        get() = exo ?: ExoPlayer.Builder(getApplication()).build().also { p ->
            exo = p
            p.addListener(object : Player.Listener {
                override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
                    currentIndex = p.currentMediaItemIndex
                }
            })
        }

    /**
     * 목록이 바뀌었을 때만 처음부터 다시 재생 (회전 등으로 다시 불려도 이어서 재생).
     * [repeat]: true = 목록 전체 무한 반복, false = 한 번씩만 재생하고 마지막 화면에서 멈춤.
     */
    fun setPlaylist(uris: List<String>, sound: Boolean, repeat: Boolean) {
        val p = player
        p.repeatMode = if (repeat) Player.REPEAT_MODE_ALL else Player.REPEAT_MODE_OFF
        if (uris != current) {
            current = uris
            currentIndex = 0
            p.setMediaItems(uris.map { MediaItem.fromUri(it) })
            p.prepare()
            p.playWhenReady = true
        } else if (repeat && p.playbackState == Player.STATE_ENDED) {
            // 반복을 끈 채로 끝까지 재생된 뒤 다시 켜면 처음부터 다시 시작
            p.seekTo(0, 0L)
            p.play()
        }
        p.volume = if (sound) 1f else 0f
    }

    /** 지금 재생 중인 동영상의 위치 (밀리초). */
    fun positionMs(): Long = exo?.currentPosition ?: 0L

    /** 지금 재생 중인 동영상의 길이 (밀리초, 모르면 0). */
    fun durationMs(): Long = exo?.duration?.takeIf { it != C.TIME_UNSET && it > 0L } ?: 0L

    /** 지금 동영상의 [ms] 위치로 이동 (끝까지 재생돼 멈춘 상태였으면 다시 재생). */
    fun seekTo(ms: Long) {
        val p = exo ?: return
        p.seekTo(ms)
        p.play()
    }

    /** 배경(동영상)이 화면에 없을 때: 멈추고 목록 비우기. */
    fun clear() {
        if (current.isEmpty()) return
        current = emptyList()
        exo?.run {
            stop()
            clearMediaItems()
        }
    }

    fun onAppShown() {
        if (current.isNotEmpty()) exo?.play()
    }

    fun onAppHidden() {
        exo?.pause()
    }

    override fun onCleared() {
        exo?.release()
        exo = null
    }
}

/** 화면 전체를 꽉 채우는 동영상 배경 (컨트롤 없음). */
@Composable
fun VideoBackground(
    vm: VideoBackgroundViewModel,
    uris: List<String>,
    sound: Boolean,
    repeat: Boolean,
    modifier: Modifier = Modifier,
) {
    LaunchedEffect(uris, sound, repeat) { vm.setPlaylist(uris, sound, repeat) }
    AndroidView(
        modifier = modifier,
        factory = { ctx ->
            (LayoutInflater.from(ctx).inflate(R.layout.background_player_view, null) as PlayerView).apply {
                player = vm.player
            }
        },
        onRelease = { it.player = null },
    )
}

/**
 * 배경 동영상 고르기 창 (여러 개 = 연속 재생). 고른 동영상은 복사하지 않고 원본 읽기 권한만 저장 (앱을 다시 켜도 유지).
 * 메뉴가 닫혀도 결과를 받을 수 있도록, 메뉴 바깥(버튼 쪽)에서 만들어 둔다. 돌려주는 함수를 부르면 창이 열림.
 */
@Composable
fun rememberVideoPicker(settings: ClockSettings, onSettingsChange: (ClockSettings) -> Unit): () -> Unit {
    val context = LocalContext.current
    val latestSettings by rememberUpdatedState(settings)
    val latestOnChange by rememberUpdatedState(onSettingsChange)
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult // 선택 취소
        val resolver = context.contentResolver
        // 앱을 다시 켜도 읽을 수 있도록 권한 유지 요청, 이전 목록의 권한은 반납
        val kept = uris.filter { uri ->
            runCatching {
                resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }.isSuccess
        }
        latestSettings.videoUris.map(Uri::parse).filter { it !in kept }.forEach { old ->
            runCatching { resolver.releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        }
        if (kept.isEmpty()) {
            Toast.makeText(context, R.string.video_failed, Toast.LENGTH_SHORT).show()
        } else {
            // 동영상을 고르면 그보다 위에 있는 유튜브 배경은 끔 (고른 동영상이 바로 보이게)
            latestOnChange(latestSettings.copy(videoUris = kept.map(Uri::toString), youtubeUrl = ""))
        }
    }
    return remember(picker) { { picker.launch(arrayOf("video/*")) } }
}

/**
 * [배경 ▾] 메뉴의 "동영상" 구역: 지금 파일 / 재생 위치 슬라이더 / 동영상 선택 / 무한 반복 / 소리 / 끄기.
 * [vm]: 재생 위치 슬라이더용 재생기 (null 이면 슬라이더 없음). [onPick]: 고르기 창 열기. [close]: 메뉴 닫기.
 */
@Composable
fun VideoBackgroundItems(
    settings: ClockSettings,
    onSettingsChange: (ClockSettings) -> Unit,
    vm: VideoBackgroundViewModel?,
    onPick: () -> Unit,
    close: () -> Unit,
) {
    val context = LocalContext.current
    val active = settings.videoUris.isNotEmpty()
    // 유튜브가 켜져 있으면 동영상은 화면에 없고 재생도 멈춰 있음
    val playing = active && parseYouTube(settings.youtubeUrl) == null
    run {
            // 지금(가장 최근) 동영상 파일 이름
            if (active) {
                val names = remember(settings.videoUris) {
                    settings.videoUris.map { queryDisplayName(context, Uri.parse(it)) }
                }
                val i = (vm?.currentIndex ?: 0).coerceIn(0, names.lastIndex)
                CurrentFileItem(i, names.size, names[i])
            }
            // 재생 위치 슬라이더 (여러 동영상이면 지금 나오는 동영상 기준)
            if (playing && vm != null) {
                PlaybackSlider(
                    positionMs = vm::positionMs,
                    durationMs = vm::durationMs,
                    onSeek = vm::seekTo,
                )
            }
            DropdownMenuItem(
                text = { Text(stringResource(R.string.video_pick), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.VideoLibrary, contentDescription = null, modifier = Modifier.size(18.dp)) },
                trailingIcon = if (active) {
                    { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                } else null,
                onClick = {
                    close()
                    onPick()
                },
            )
            // 무한 반복 On/Off (동영상을 고르기 전에도 정할 수 있게 항상 표시)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.video_repeat), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = {
                    Icon(
                        if (settings.videoRepeat) Icons.Filled.Repeat else Icons.Filled.RepeatOne,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                    )
                },
                trailingIcon = {
                    Checkbox(
                        checked = settings.videoRepeat,
                        onCheckedChange = null,
                        modifier = Modifier.size(18.dp),
                    )
                },
                onClick = { onSettingsChange(settings.copy(videoRepeat = !settings.videoRepeat)) },
            )
            if (active) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.video_sound), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = {
                        Icon(
                            if (settings.videoSound) Icons.Filled.VolumeUp else Icons.Filled.VolumeOff,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp),
                        )
                    },
                    trailingIcon = if (settings.videoSound) {
                        { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                    } else null,
                    onClick = { onSettingsChange(settings.copy(videoSound = !settings.videoSound)) },
                )
            }
            // 끄기 (항상 표시, 동영상이 없으면 흐리게)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.video_off), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.VideocamOff, contentDescription = null, modifier = Modifier.size(18.dp)) },
                enabled = active,
                onClick = {
                    close()
                    settings.videoUris.forEach {
                        runCatching {
                            context.contentResolver.releasePersistableUriPermission(
                                Uri.parse(it), Intent.FLAG_GRANT_READ_URI_PERMISSION,
                            )
                        }
                    }
                    onSettingsChange(settings.copy(videoUris = emptyList()))
                },
            )
    }
}
