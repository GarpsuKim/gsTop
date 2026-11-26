package com.example.clock.chime

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.AudioFormat
import android.media.AudioTrack
import java.time.LocalTime
import android.media.MediaPlayer
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.example.clock.R

/**
 * 시보 연주 (이 앱이 직접 재생). 정해진 연주 시간이 지나거나 곡이 끝나면 멈추고 스스로 종료.
 * 앱이 꺼져 있어도 재생되도록 포그라운드 서비스(미디어 재생)로 동작하며, 알림에 [정지] 버튼이 있음.
 *  - 무음·진동 모드면 울리지 않음 (단, 설정의 [진동·무음 모드에서도 울리기] 를 켜면 미디어 음량으로 울림)
 *  - 통화 중이면 울리지 않음
 *  - 다른 소리(음악 등)는 시보 동안 잠시 작아졌다가 돌아옴
 *  - 곡은 반복하지 않음: 연주 시간보다 짧은 곡은 곡이 끝나면 멈춤
 */
class ChimeService : Service() {

    private val handler = Handler(Looper.getMainLooper())
    private var player: MediaPlayer? = null
    /** 괘종시계 종소리 재생기. */
    private var track: AudioTrack? = null
    private var wakeLock: PowerManager.WakeLock? = null
    /** 시작·정지할 때마다 바뀜: 종소리를 만드는 사이에 정지·재시작되면 그 결과를 버리기 위해. */
    private var session = 0
    private var focusRequest: AudioFocusRequest? = null
    private val audioManager by lazy { getSystemService(AudioManager::class.java) }

    private val stopRunnable = Runnable { fadeOutAndStop() }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // 포그라운드 서비스는 시작 후 바로 알림을 띄워야 함 (안 그러면 시스템이 앱을 종료)
        startInForeground()
        when (intent?.action) {
            ACTION_STOP -> stopAll()
            ACTION_PREVIEW -> startChime(preview = true)
            else -> startChime(preview = false)
        }
        return START_NOT_STICKY // 연주 도중 시스템이 서비스를 죽이면 다시 살리지 않음 (다음 정각에 다시 울림)
    }

    /** @param preview 설정 창의 [들어보기]: 무음·진동 모드여도 연주 (사용자가 직접 누른 것이므로). */
    private fun startChime(preview: Boolean) {
        // 연주 중에 다시 시작하면 이전 연주를 깨끗이 정리하고 처음부터
        session++
        handler.removeCallbacksAndMessages(null)
        releasePlayer()
        focusRequest?.let { audioManager?.abandonAudioFocusRequest(it) }
        focusRequest = null

        val s = ChimeStore.load(this)
        val uri = s.fileUri
        val am = audioManager
        val silent = am?.ringerMode != AudioManager.RINGER_MODE_NORMAL
        // 무음·진동 모드: [진동·무음 모드에서도 울리기] 를 켰거나 [들어보기] 면 그래도 울림 (미디어 음량)
        if ((uri == null && !s.isBuiltinBell) || am == null || (silent && !preview && !s.ringWhenSilent)) {
            stopAll() // 파일 없음 / 무음·진동 모드
            return
        }

        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()
        val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
            .setAudioAttributes(attrs)
            .setOnAudioFocusChangeListener { change ->
                // 전화가 오는 등 다른 앱이 소리를 가져가면 시보 중단
                if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                    stopAll()
                }
            }
            .build()
        if (am.requestAudioFocus(request) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            stopAll() // 통화 중 등
            return
        }
        focusRequest = request

        if (s.isBuiltinBell) {
            startBell(attrs, s)
            return
        }
        if (uri == null) return stopAll()

        try {
            player = MediaPlayer().apply {
                setAudioAttributes(attrs)
                setWakeMode(this@ChimeService, PowerManager.PARTIAL_WAKE_LOCK) // 화면이 꺼져 있어도 끝까지
                setDataSource(this@ChimeService, uri)
                setOnPreparedListener {
                    it.start()
                    handler.postDelayed(stopRunnable, s.length.seconds * 1_000L)
                }
                setOnCompletionListener { stopAll() } // 곡이 먼저 끝남
                setOnErrorListener { _, what, extra ->
                    Log.w(TAG, "재생 오류 what=$what extra=$extra")
                    stopAll()
                    true
                }
                prepareAsync()
            }
        } catch (e: Exception) {
            // 파일이 지워졌거나 권한이 없어진 경우 등
            Log.w(TAG, "시보 파일을 열 수 없음: $uri", e)
            stopAll()
        }
    }

    /**
     * 괘종시계 종소리: 지금 시각 수만큼 침 (오전·오후 모두 1~12번, 0시·12시 = 12번).
     * 소리 만들기(0.1초 안팎)는 따로 스레드에서 하고, 다 되면 AudioTrack 으로 재생.
     */
    private fun startBell(attrs: AudioAttributes, s: ChimeSettings) {
        val strikes = (LocalTime.now().hour % 12).let { if (it == 0) 12 else it }
        val mySession = session
        // AudioTrack 은 MediaPlayer 처럼 화면 꺼짐 대비를 스스로 못 하므로 직접 CPU 깨움 유지 (최대 2분)
        wakeLock = getSystemService(PowerManager::class.java)
            ?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "clock:chime")
            ?.apply { acquire(2 * 60_000L) }

        Thread {
            val pcm = BellSynth.render(strikes)
            handler.post {
                if (session != mySession) return@post // 만드는 사이에 정지됨
                try {
                    val t = AudioTrack.Builder()
                        .setAudioAttributes(attrs)
                        .setAudioFormat(
                            AudioFormat.Builder()
                                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(BellSynth.RATE)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                .build(),
                        )
                        .setTransferMode(AudioTrack.MODE_STATIC)
                        .setBufferSizeInBytes(pcm.size * 2)
                        .build()
                    t.write(pcm, 0, pcm.size)
                    track = t
                    t.play()
                    // 종소리가 끝나면 정지. 연주 시간이 더 짧으면 그때 소리를 줄이며 정지.
                    val soundMs = pcm.size * 1_000L / BellSynth.RATE
                    handler.postDelayed({ stopAll() }, soundMs + 300L)
                    handler.postDelayed(stopRunnable, s.length.seconds * 1_000L)
                } catch (e: Exception) {
                    Log.w(TAG, "종소리 재생 실패", e)
                    stopAll()
                }
            }
        }.start()
    }

    /** 끝날 때 뚝 끊기지 않게 1초 동안 소리를 줄이고 멈춤. */
    private fun fadeOutAndStop() {
        val p = player
        val t = track
        if (p == null && t == null) return stopAll()
        val steps = 10
        for (i in 1..steps) {
            handler.postDelayed({
                try {
                    val v = 1f - i / steps.toFloat()
                    p?.setVolume(v, v)
                    t?.setVolume(v)
                } catch (e: IllegalStateException) {
                    // 이미 해제됨
                }
                if (i == steps) stopAll()
            }, i * FADE_MS / steps)
        }
    }

    private fun stopAll() {
        session++ // 만들고 있던 종소리는 버림
        handler.removeCallbacksAndMessages(null)
        releasePlayer()
        focusRequest?.let { audioManager?.abandonAudioFocusRequest(it) }
        focusRequest = null
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun releasePlayer() {
        player?.run {
            try {
                stop()
            } catch (e: IllegalStateException) {
                // 준비 전이면 stop 불가 → 그냥 해제
            }
            release()
        }
        player = null
        track?.run {
            try {
                stop()
            } catch (e: IllegalStateException) {
                // 재생 전
            }
            release()
        }
        track = null
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        releasePlayer()
        focusRequest?.let { audioManager?.abandonAudioFocusRequest(it) }
        super.onDestroy()
    }

    private fun startInForeground() {
        val nm = getSystemService(NotificationManager::class.java)
        if (nm != null && nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, getString(R.string.chime_channel), NotificationManager.IMPORTANCE_LOW),
            )
        }
        val stopIntent = PendingIntent.getService(
            this, 1,
            Intent(this, ChimeService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.chime_playing))
            .setContentText(
                ChimeStore.load(this).let { if (it.isBuiltinBell) getString(R.string.chime_bell) else it.fileName },
            )
            .setOngoing(true)
            .setSilent(true)
            .addAction(0, getString(R.string.chime_stop), stopIntent)
            .build()
        try {
            ServiceCompat.startForeground(
                this, NOTIFICATION_ID, notification,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
                } else {
                    0
                },
            )
        } catch (e: Exception) {
            Log.w(TAG, "포그라운드 시작 실패", e)
        }
    }

    companion object {
        private const val TAG = "ChimeService"
        private const val CHANNEL_ID = "chime"
        private const val NOTIFICATION_ID = 7001
        private const val ACTION_PLAY = "com.example.clock.action.CHIME_PLAY"
        private const val ACTION_STOP = "com.example.clock.action.CHIME_STOP"
        private const val ACTION_PREVIEW = "com.example.clock.action.CHIME_PREVIEW"
        private const val FADE_MS = 1_000L

        /** 정각 시보 연주 시작. */
        fun play(context: Context) = send(context, ACTION_PLAY)

        /** 설정 창의 [들어보기]: 지금 바로 한 번 연주. */
        fun preview(context: Context) = send(context, ACTION_PREVIEW)

        /** 연주 중인 시보 정지. */
        fun stop(context: Context) = send(context, ACTION_STOP)

        private fun send(context: Context, action: String) {
            try {
                ContextCompat.startForegroundService(
                    context, Intent(context, ChimeService::class.java).setAction(action),
                )
            } catch (e: Exception) {
                // 백그라운드에서 서비스 시작이 막힌 경우 등 (정확한 알람으로 깨어난 경우에는 허용됨)
                Log.w(TAG, "시보 서비스 시작 실패", e)
            }
        }
    }
}
