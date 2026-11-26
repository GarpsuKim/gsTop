package com.example.clock.ui

import android.app.Application
import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.net.Uri
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
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.MusicOff
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.AndroidViewModel
import com.example.clock.R

/**
 * 시계 앱 안에서 음악을 재생한다.
 * 화면 회전에도 끊기지 않도록 ViewModel 에 둔다 (화면이 다시 만들어져도 재생기는 그대로).
 * 여러 곡이면 차례로 재생하고 끝나면 처음부터 반복, 한 곡이면 그 곡을 반복.
 */
class MusicViewModel(app: Application) : AndroidViewModel(app) {
    data class Track(val uri: Uri, val name: String)

    var tracks by mutableStateOf<List<Track>>(emptyList())
        private set
    var index by mutableIntStateOf(0)
        private set
    var isPlaying by mutableStateOf(false)
        private set

    private var player: MediaPlayer? = null
    private var prepared = false
    private var pausedByApp = false // 앱을 나가서 멈춘 경우 → 돌아오면 이어서 재생

    private val context: Context get() = getApplication()

    /** 새 곡 목록으로 바꾸고 첫 곡부터 재생. */
    fun play(uris: List<Uri>) {
        if (uris.isEmpty()) return
        tracks = uris.map { Track(it, displayName(it)) }
        startAt(0, failures = 0)
    }

    fun togglePause() {
        val mp = player ?: return
        if (!prepared) return
        if (mp.isPlaying) {
            mp.pause()
            isPlaying = false
        } else {
            mp.start()
            isPlaying = true
        }
        pausedByApp = false
    }

    fun next() {
        if (tracks.isNotEmpty()) startAt((index + 1) % tracks.size, failures = 0)
    }

    /** 현재 곡의 재생 위치 (밀리초, 준비 전이면 0). */
    fun positionMs(): Long = if (prepared) player?.currentPosition?.toLong() ?: 0L else 0L

    /** 현재 곡의 길이 (밀리초, 모르면 0). */
    fun durationMs(): Long = if (prepared) player?.duration?.toLong()?.coerceAtLeast(0L) ?: 0L else 0L

    /** 현재 곡의 [ms] 위치로 이동. */
    fun seekTo(ms: Long) {
        if (prepared) player?.seekTo(ms.toInt())
    }

    /** 재생을 멈추고 곡 목록을 비운다. */
    fun stop() {
        release()
        tracks = emptyList()
        isPlaying = false
        pausedByApp = false
    }

    /** 앱이 화면에서 사라질 때 (홈 버튼 등). */
    fun onAppHidden() {
        if (player?.isPlaying == true) {
            player?.pause()
            isPlaying = false
            pausedByApp = true
        }
    }

    /** 앱으로 돌아왔을 때. */
    fun onAppShown() {
        if (pausedByApp && prepared) {
            player?.start()
            isPlaying = true
        }
        pausedByApp = false
    }

    private fun startAt(i: Int, failures: Int) {
        release()
        if (tracks.isEmpty()) return
        if (failures >= tracks.size) { // 모든 곡이 재생 실패
            isPlaying = false
            Toast.makeText(context, R.string.music_failed, Toast.LENGTH_SHORT).show()
            return
        }
        index = i
        val mp = MediaPlayer()
        player = mp
        try {
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build(),
            )
            mp.setDataSource(context, tracks[i].uri)
            mp.isLooping = tracks.size == 1
            mp.setOnPreparedListener {
                prepared = true
                it.start()
                isPlaying = true
            }
            mp.setOnCompletionListener { startAt((index + 1) % tracks.size, failures = 0) }
            mp.setOnErrorListener { _, _, _ ->
                startAt((index + 1) % tracks.size, failures + 1)
                true
            }
            mp.prepareAsync()
        } catch (e: Exception) {
            startAt((i + 1) % tracks.size, failures + 1)
        }
    }

    private fun release() {
        player?.release()
        player = null
        prepared = false
    }

    private fun displayName(uri: Uri): String = queryDisplayName(context, uri)

    override fun onCleared() {
        release()
    }
}

/**
 * 음악 고르기 창 (여러 곡 가능). 메뉴가 닫혀도 결과를 받을 수 있도록 메뉴 바깥(버튼 쪽)에서 만들어 둔다.
 * 돌려주는 함수를 부르면 창이 열림.
 */
@Composable
fun rememberMusicPicker(music: MusicViewModel): () -> Unit {
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.GetMultipleContents()) { uris ->
        if (uris.isNotEmpty()) music.play(uris)
    }
    return remember(picker) { { picker.launch("audio/*") } }
}

/**
 * [배경 ▾] 메뉴의 "음악" 구역.
 * 곡이 없으면 [음악 선택] 만, 있으면 현재 곡 + 재생 위치 슬라이더 / 재생·일시정지 / 다음 곡 / 다른 음악 선택 / 끄기.
 * [onPick]: 고르기 창 열기. [close]: 메뉴 닫기.
 */
@Composable
fun MusicItems(music: MusicViewModel, onPick: () -> Unit, close: () -> Unit) {
    if (music.tracks.isEmpty()) {
        DropdownMenuItem(
            text = { Text(stringResource(R.string.music_pick)) },
            leadingIcon = { Icon(Icons.Filled.LibraryMusic, contentDescription = null) },
            onClick = {
                close()
                onPick()
            },
        )
        return
    }
    run {
            music.tracks.getOrNull(music.index)?.let { track ->
                CurrentFileItem(music.index, music.tracks.size, track.name)
                // 재생 위치 슬라이더 (곡이 바뀌면 새로 읽도록 곡 번호를 키로)
                androidx.compose.runtime.key(music.index) {
                    PlaybackSlider(
                        positionMs = music::positionMs,
                        durationMs = music::durationMs,
                        onSeek = music::seekTo,
                    )
                }
            }
            DropdownMenuItem(
                text = { Text(stringResource(if (music.isPlaying) R.string.music_pause else R.string.music_play)) },
                leadingIcon = {
                    Icon(if (music.isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow, contentDescription = null)
                },
                onClick = { music.togglePause() },
            )
            if (music.tracks.size > 1) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.music_next)) },
                    leadingIcon = { Icon(Icons.Filled.SkipNext, contentDescription = null) },
                    onClick = { music.next() },
                )
            }
            DropdownMenuItem(
                text = { Text(stringResource(R.string.music_pick_other)) },
                leadingIcon = { Icon(Icons.Filled.LibraryMusic, contentDescription = null) },
                onClick = {
                    close()
                    onPick()
                },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.music_stop)) },
                leadingIcon = { Icon(Icons.Filled.MusicOff, contentDescription = null) },
                onClick = {
                    close()
                    music.stop()
                },
            )
    }
}
