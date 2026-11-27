package com.example.clock.ui

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.SystemClock
import androidx.annotation.StringRes
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.example.clock.R
import kotlinx.coroutines.delay
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.random.Random

// ─────────────────────────────── 백색 소음 ───────────────────────────────

/** 백색 소음 종류. 소리 파일 없이 앱이 직접 만들어 냄. */
enum class NoiseType(@StringRes val label: Int) {
    Rain(R.string.noise_rain),   // 빗소리: 부드러운 쏴~ 소리 + 가끔 떨어지는 빗방울
    Waves(R.string.noise_waves), // 파도: 낮은 쏴~ 소리가 8~11초마다 밀려왔다 빠짐
    Fan(R.string.noise_fan),     // 선풍기: 낮고 고른 바람 소리
    White(R.string.noise_white), // 백색 소음: 고르게 "쉬~"
    Pink(R.string.noise_pink),   // 핑크 소음: 백색보다 부드러움
    Brown(R.string.noise_brown), // 갈색 소음: 가장 낮고 묵직함
}

/** 백색 소음 크기. [volume]: 재생기 볼륨 (0~1, 폰의 미디어 음량 안에서). */
enum class NoiseLevel(@StringRes val label: Int, val volume: Float) {
    Low(R.string.noise_level_low, 0.25f),
    Medium(R.string.noise_level_medium, 0.5f),
    High(R.string.noise_level_high, 1f),
}

/**
 * 백색 소음 재생기 (앱 전체에 하나, 화면 회전에도 끊기지 않음).
 * 따로 스레드에서 소리를 조금씩 만들어 AudioTrack 에 흘려 넣음 (22kHz 모노).
 * 앱이 화면에서 사라지면 멈췄다가 돌아오면 이어서 ([onAppHidden] / [onAppShown], 음악과 같음).
 */
object WhiteNoise {
    /** 지금 나오는 종류 (null = 꺼짐). */
    var type by mutableStateOf<NoiseType?>(null)
        private set
    var level by mutableStateOf(NoiseLevel.Medium)
        private set

    /** 잠들기 타이머가 끝나기 전 서서히 줄이는 배율 (1 = 그대로). 스레드가 매번 읽음. */
    @Volatile
    private var fade = 1f

    @Volatile
    private var thread: Thread? = null
    private var pausedByApp = false

    fun play(t: NoiseType) {
        type = t
        restart()
    }

    /** 크기 바꾸기 (재생 중이면 다음 조각부터 바로 반영). setLevel 이름은 level 속성의 setter 와 겹쳐서 다른 이름. */
    fun changeLevel(l: NoiseLevel) {
        level = l
    }

    fun stop() {
        type = null
        pausedByApp = false
        stopThread()
    }

    internal fun setFade(f: Float) {
        fade = f.coerceIn(0f, 1f)
    }

    fun onAppHidden() {
        if (thread != null) {
            pausedByApp = true
            stopThread()
        }
    }

    fun onAppShown() {
        if (pausedByApp && type != null) restart()
        pausedByApp = false
    }

    private fun restart() {
        stopThread()
        val t = type ?: return
        val th = Thread({ runLoop(t) }, "WhiteNoise").apply { isDaemon = true }
        thread = th
        th.start()
    }

    private fun stopThread() {
        val th = thread ?: return
        thread = null
        th.interrupt()
    }

    private fun runLoop(t: NoiseType) {
        val me = Thread.currentThread()
        val minBuf = AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build(),
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build(),
                )
                .setBufferSizeInBytes(maxOf(minBuf, CHUNK * 4))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (e: Exception) {
            return
        }
        val gen = NoiseGenerator(t)
        val buf = ShortArray(CHUNK)
        // 처음 0.8초 동안 서서히 커짐 (뚝 시작하지 않게)
        var startRamp = 0f
        try {
            track.play()
            while (thread === me && !me.isInterrupted) {
                gen.fill(buf)
                startRamp = (startRamp + CHUNK / (RATE * 0.8f)).coerceAtMost(1f)
                track.setVolume(level.volume * fade * startRamp)
                if (track.write(buf, 0, buf.size) < 0) break
            }
        } catch (_: Exception) {
        } finally {
            runCatching { track.pause(); track.flush(); track.release() }
        }
    }

    private const val RATE = 22_050
    private const val CHUNK = 2_048
}

/** 종류별 소리 만들기 (한 번에 [CHUNK] 개씩, -1~1 을 16비트로). */
private class NoiseGenerator(private val type: NoiseType) {
    private val rnd = Random(SystemClock.uptimeMillis())
    private val rate = 22_050f

    // 핑크 소음 필터 (Paul Kellet)
    private var p0 = 0f; private var p1 = 0f; private var p2 = 0f; private var p3 = 0f
    private var p4 = 0f; private var p5 = 0f; private var p6 = 0f

    // 갈색 소음 (적분)
    private var brown = 0f

    // 선풍기: 한 번 더 부드럽게
    private var smooth = 0f

    // 파도: 이번 파도가 시작된 뒤 지난 시간(초)과 한 파도의 길이.
    // 파도마다 0부터 다시 세므로 밤새 틀어도 float 정밀도가 모자라지 않음
    private var waveT = 0f
    private var wavePeriod = 9f

    // 빗방울: 남은 세기
    private var drop = 0f

    private fun white() = rnd.nextFloat() * 2f - 1f

    private fun pink(): Float {
        val w = white()
        p0 = 0.99886f * p0 + w * 0.0555179f
        p1 = 0.99332f * p1 + w * 0.0750759f
        p2 = 0.96900f * p2 + w * 0.1538520f
        p3 = 0.86650f * p3 + w * 0.3104856f
        p4 = 0.55000f * p4 + w * 0.5329522f
        p5 = -0.7616f * p5 - w * 0.0168980f
        val out = p0 + p1 + p2 + p3 + p4 + p5 + p6 + w * 0.5362f
        p6 = w * 0.115926f
        return out * 0.11f
    }

    private fun brownNext(): Float {
        brown = (brown + 0.02f * white()) / 1.02f
        return brown * 3.5f
    }

    fun fill(out: ShortArray) {
        for (i in out.indices) {
            val s = when (type) {
                NoiseType.White -> white() * 0.5f
                NoiseType.Pink -> pink()
                NoiseType.Brown -> brownNext()
                NoiseType.Fan -> {
                    smooth += (brownNext() - smooth) * 0.35f
                    smooth * 1.3f
                }
                NoiseType.Waves -> {
                    waveT += 1f / rate
                    if (waveT > wavePeriod) {
                        waveT = 0f
                        wavePeriod = 8f + rnd.nextFloat() * 3f
                    }
                    val phase = waveT / wavePeriod
                    // 0 → 1 → 0 로 밀려왔다 빠짐 (밀려올 때 조금 빠르게)
                    val env = (0.5f - 0.5f * cos(2.0 * PI * phase).toFloat())
                    val shaped = env * env * (3f - 2f * env)
                    brownNext() * (0.25f + 0.75f * shaped) + pink() * 0.35f * shaped
                }
                NoiseType.Rain -> {
                    if (drop < 0.02f && rnd.nextFloat() < 0.0009f) drop = 0.3f + rnd.nextFloat() * 0.5f
                    val d = drop * white()
                    drop *= DROP_DECAY
                    pink() * 0.7f + d * 0.6f
                }
            }
            out[i] = (s.coerceIn(-1f, 1f) * 30_000f).toInt().toShort()
        }
    }

    companion object {
        /** 빗방울 소리가 약 4ms 만에 사라지게. */
        private val DROP_DECAY = exp(-1.0 / (22_050 * 0.004)).toFloat()
    }
}

// ─────────────────────────────── 잠들기 타이머 ───────────────────────────────

/** 잠들기 타이머 길이 (분). */
val SleepTimerMinutes = listOf(15, 30, 45, 60, 90, 120)

/** 끝나기 이만큼 전부터 소리를 서서히 줄임. */
private const val SLEEP_FADE_MS = 30_000L

/**
 * 잠들기 타이머 (앱 전체에 하나, 화면 회전에도 이어짐).
 * 정한 시간이 지나면 모든 소리를 끔: 백색 소음 정지, 음악 일시정지, 동영상·유튜브 소리 끔.
 * 끝나기 30초 전부터 백색 소음·음악·동영상 소리를 서서히 줄임 ([fade]).
 * 소리를 끈 상태([soundOff])는 새 타이머를 켜거나, 소리를 다시 켜거나, 앱을 다시 열면 풀림.
 */
object SleepTimer {
    /** 끝나는 시각 (SystemClock.elapsedRealtime 기준, null = 꺼짐). */
    var endAt by mutableStateOf<Long?>(null)
        private set

    /** 1 = 그대로, 0 = 소리 없음. 끝나기 30초 전부터 1 → 0. */
    var fade by mutableFloatStateOf(1f)
        private set

    /** 타이머가 끝나 소리를 모두 끈 상태 (동영상·유튜브 소리도 끔). */
    var soundOff by mutableStateOf(false)
        private set

    fun start(minutes: Int) {
        endAt = SystemClock.elapsedRealtime() + minutes * 60_000L
        fade = 1f
        soundOff = false
        WhiteNoise.setFade(1f)
    }

    fun cancel() {
        endAt = null
        fade = 1f
        WhiteNoise.setFade(1f)
    }

    /**
     * 지금 바로 모든 소리 끔 (뒤집으면 무음): 백색 소음 정지, 음악 일시정지·동영상·유튜브 소리 끔 ([SleepTimerEffect] 가 처리).
     * 켜져 있던 잠들기 타이머는 끝난 것으로.
     */
    fun muteNow() {
        endAt = null
        fade = 1f
        WhiteNoise.stop()
        WhiteNoise.setFade(1f)
        soundOff = true
    }

    /** 소리를 다시 켤 때 (새로 재생, 앱을 다시 열 때). */
    fun releaseSoundOff() {
        if (soundOff) soundOff = false
    }

    /** 남은 시간 (밀리초, 꺼져 있으면 null). */
    fun remainingMs(): Long? = endAt?.let { (it - SystemClock.elapsedRealtime()).coerceAtLeast(0L) }

    internal fun tick(): Boolean {
        val left = remainingMs() ?: return false
        fade = (left.toFloat() / SLEEP_FADE_MS).coerceIn(0f, 1f)
        WhiteNoise.setFade(fade)
        if (left <= 0L) {
            endAt = null
            soundOff = true
            fade = 1f
            WhiteNoise.stop()
            WhiteNoise.setFade(1f)
            return true
        }
        return false
    }
}

/**
 * 잠들기 타이머 진행 (화면 맨 바깥에 하나만 둠, 앱이 떠 있는 동안 1초마다).
 * 음악·동영상 소리를 [SleepTimer.fade] 에 맞춰 줄이고, 끝나면 음악을 멈추고 동영상 소리를 끔.
 */
@Composable
fun SleepTimerEffect(music: MusicViewModel?, video: VideoBackgroundViewModel?) {
    val endAt = SleepTimer.endAt
    LaunchedEffect(endAt) {
        if (endAt == null) return@LaunchedEffect
        while (true) {
            val ended = SleepTimer.tick()
            music?.setVolumeFactor(SleepTimer.fade)
            video?.setVolumeFactor(SleepTimer.fade)
            if (ended) {
                music?.pauseIfPlaying()
                music?.setVolumeFactor(1f)
                video?.setVolumeFactor(0f) // 다시 소리를 켜기 전까지 동영상 소리 없음
                break
            }
            delay(if ((SleepTimer.remainingMs() ?: 0L) <= SLEEP_FADE_MS) 250L else 1_000L)
        }
    }
    // 소리 끔 (타이머 끝 또는 뒤집으면 무음): 음악 일시정지, 동영상 소리 끔
    LaunchedEffect(SleepTimer.soundOff) {
        if (SleepTimer.soundOff) {
            music?.pauseIfPlaying()
            video?.setVolumeFactor(0f)
        }
    }
    // 타이머가 꺼지거나 소리 끔이 풀리면 동영상 소리를 원래대로
    LaunchedEffect(endAt, SleepTimer.soundOff) {
        if (endAt == null && !SleepTimer.soundOff) {
            music?.setVolumeFactor(1f)
            video?.setVolumeFactor(1f)
        }
    }
}

/** "23분 남음" / "끔". 1분 미만이면 "1분 미만". */
@Composable
fun sleepTimerValue(): String {
    val left = SleepTimer.remainingMs() ?: return stringResource(R.string.chime_state_off)
    val min = ((left + 59_999L) / 60_000L).toInt()
    return if (left < 60_000L) stringResource(R.string.sleep_less_than_minute) else stringResource(R.string.sleep_remaining, min)
}

/** [배경 ▾] 메뉴의 "백색 소음" 영역: 종류 6가지 (누르면 재생, 지금 것에 ✓) / ── / 크기 / ── / 끄기. */
@Composable
fun WhiteNoiseItems(close: () -> Unit) {
    val check: @Composable () -> Unit = { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
    NoiseType.entries.forEach { n ->
        DropdownMenuItem(
            text = { Text(stringResource(n.label), style = MaterialTheme.typography.bodyMedium) },
            trailingIcon = if (WhiteNoise.type == n) check else null,
            onClick = {
                SleepTimer.releaseSoundOff()
                WhiteNoise.play(n)
            },
        )
    }
    HorizontalDivider(Modifier.padding(vertical = 4.dp))
    MenuSectionTitle(R.string.noise_level)
    NoiseLevel.entries.forEach { l ->
        DropdownMenuItem(
            text = { Text(stringResource(l.label), style = MaterialTheme.typography.bodyMedium) },
            trailingIcon = if (WhiteNoise.level == l) check else null,
            onClick = { WhiteNoise.changeLevel(l) },
        )
    }
    if (WhiteNoise.type != null) {
        HorizontalDivider(Modifier.padding(vertical = 4.dp))
        DropdownMenuItem(
            text = { Text(stringResource(R.string.noise_stop), style = MaterialTheme.typography.bodyMedium) },
            leadingIcon = { Icon(Icons.Filled.Close, contentDescription = null, modifier = Modifier.size(18.dp)) },
            onClick = {
                WhiteNoise.stop()
                close()
            },
        )
    }
}

/** [배경 ▾] 메뉴의 "잠들기 타이머" 영역: 15 / 30 / 45 / 60 / 90 / 120분 / ── / 끄기 (켜져 있을 때). 고르면 메뉴 닫힘. */
@Composable
fun SleepTimerItems(close: () -> Unit) {
    MenuSectionTitle(R.string.sleep_timer_desc)
    SleepTimerMinutes.forEach { m ->
        DropdownMenuItem(
            text = { Text(stringResource(R.string.sleep_minutes, m), style = MaterialTheme.typography.bodyMedium) },
            onClick = {
                SleepTimer.start(m)
                close()
            },
        )
    }
    if (SleepTimer.endAt != null) {
        HorizontalDivider(Modifier.padding(vertical = 4.dp))
        DropdownMenuItem(
            text = { Text(stringResource(R.string.sleep_cancel), style = MaterialTheme.typography.bodyMedium) },
            leadingIcon = { Icon(Icons.Filled.Close, contentDescription = null, modifier = Modifier.size(18.dp)) },
            onClick = {
                SleepTimer.cancel()
                close()
            },
        )
    }
}
