package com.example.clock.ui

import android.app.Application
import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.net.Uri
import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.Stop
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
import androidx.compose.ui.text.style.TextOverflow
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

    private fun displayName(uri: Uri): String =
        runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        }.getOrNull()?.substringBeforeLast('.') ?: uri.lastPathSegment.orEmpty()

    override fun onCleared() {
        release()
    }
}

/**
 * [♪ 음악] 버튼.
 * 곡이 없으면 바로 파일 선택(여러 곡 가능), 재생 중이면 메뉴:
 * 현재 곡 / 재생·일시정지 / 다음 곡 / 다른 음악 선택 / 정지.
 */
@Composable
fun MusicButton(music: MusicViewModel) {
    var expanded by remember { mutableStateOf(false) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.GetMultipleContents()) { uris ->
        music.play(uris)
    }
    val hasTracks = music.tracks.isNotEmpty()

    Box {
        TextButton(
            onClick = { if (hasTracks) expanded = true else picker.launch("audio/*") },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(
                if (music.isPlaying) Icons.Filled.MusicNote else Icons.Filled.LibraryMusic,
                contentDescription = null,
                modifier = Modifier.size(16.dp),
            )
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.music), style = MaterialTheme.typography.labelMedium)
            if (hasTracks) {
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
            }
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            music.tracks.getOrNull(music.index)?.let { track ->
                DropdownMenuItem(
                    text = {
                        Text(
                            "${music.index + 1}/${music.tracks.size}  ${track.name}",
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.widthIn(max = 220.dp),
                        )
                    },
                    onClick = {},
                    enabled = false,
                )
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
                    expanded = false
                    picker.launch("audio/*")
                },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.music_stop)) },
                leadingIcon = { Icon(Icons.Filled.Stop, contentDescription = null) },
                onClick = {
                    expanded = false
                    music.stop()
                },
            )
        }
    }
}
