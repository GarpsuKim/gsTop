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
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.AndroidViewModel
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

    /** 재생기 (처음 쓸 때 만든다). */
    val player: ExoPlayer
        get() = exo ?: ExoPlayer.Builder(getApplication()).build().also { exo = it }

    /**
     * 목록이 바뀌었을 때만 처음부터 다시 재생 (회전 등으로 다시 불려도 이어서 재생).
     * [repeat]: true = 목록 전체 무한 반복, false = 한 번씩만 재생하고 마지막 화면에서 멈춤.
     */
    fun setPlaylist(uris: List<String>, sound: Boolean, repeat: Boolean) {
        val p = player
        p.repeatMode = if (repeat) Player.REPEAT_MODE_ALL else Player.REPEAT_MODE_OFF
        if (uris != current) {
            current = uris
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
 * [🎬 배경(동영상) ▾] 메뉴: 동영상 선택(여러 개 = 연속 재생) / 소리 켜기 / 끄기.
 * 고른 동영상은 복사하지 않고 원본 읽기 권한만 저장 (앱을 다시 켜도 유지).
 */
@Composable
fun VideoBackgroundMenu(settings: ClockSettings, onSettingsChange: (ClockSettings) -> Unit) {
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    val active = settings.videoUris.isNotEmpty()

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult // 선택 취소
        val resolver = context.contentResolver
        // 앱을 다시 켜도 읽을 수 있도록 권한 유지 요청, 이전 목록의 권한은 반납
        val kept = uris.filter { uri ->
            runCatching {
                resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }.isSuccess
        }
        settings.videoUris.map(Uri::parse).filter { it !in kept }.forEach { old ->
            runCatching { resolver.releasePersistableUriPermission(old, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        }
        if (kept.isEmpty()) {
            Toast.makeText(context, R.string.video_failed, Toast.LENGTH_SHORT).show()
        } else {
            // 동영상을 고르면 그보다 위에 있는 유튜브 배경은 끔 (고른 동영상이 바로 보이게)
            onSettingsChange(settings.copy(videoUris = kept.map(Uri::toString), youtubeUrl = ""))
        }
    }

    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(Icons.Filled.VideoLibrary, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.video_background), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.video_pick), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.VideoLibrary, contentDescription = null, modifier = Modifier.size(18.dp)) },
                trailingIcon = if (active) {
                    { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                } else null,
                onClick = {
                    expanded = false
                    picker.launch(arrayOf("video/*"))
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
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.video_off), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = { Icon(Icons.Filled.VideocamOff, contentDescription = null, modifier = Modifier.size(18.dp)) },
                    onClick = {
                        expanded = false
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
    }
}
