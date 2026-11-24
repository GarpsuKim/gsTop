package com.example.clock

import android.content.Intent
import android.graphics.PixelFormat
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import com.example.clock.ui.BackgroundSlideshow
import com.example.clock.ui.YouTubeBackground
import com.example.clock.ui.extractYouTubeUrl
import com.example.clock.ui.parseYouTube
import android.widget.Toast
import android.net.Uri
import androidx.activity.viewModels
import com.example.clock.ui.MusicViewModel
import com.example.clock.ui.VideoBackground
import com.example.clock.ui.VideoBackgroundViewModel
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.core.content.edit
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import com.example.clock.ui.ClockScreen
import com.example.clock.ui.loadClockSettings
import com.example.clock.ui.saveClockSettings
import com.example.clock.ui.theme.ClockTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // 모든 테마가 검은 바탕: 상태바/내비게이션바도 투명 + 흰 아이콘
        val barStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
        enableEdgeToEdge(statusBarStyle = barStyle, navigationBarStyle = barStyle)
        // 앱이 화면에 떠 있는 동안 화면이 꺼지지 않게 (앱을 나가면 자동 해제)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        // 창 표면은 처음부터 반투명 형식으로 만든다. 창이 화면에 붙은 뒤에 setFormat 으로 바꾸면
        // 이미 만들어진 불투명 표면/렌더러가 그대로 남아 배경화면이 비치지 않는 기기가 있음.
        window.setFormat(PixelFormat.TRANSLUCENT)
        if (savedInstanceState == null) {
            requestHomeShortcutOnce()
            receiveShared(intent) // 앱이 꺼져 있을 때 "공유 → 시계" 로 시작된 경우
        }
        setContent {
            var settings by remember { mutableStateOf(loadClockSettings(this)) }

            val context = this

            // 검은 배경일 때만 투명도 적용: 투명할수록 폰 배경화면이 비쳐 보임
            val transparency = if (settings.isBlackBackground) settings.blackTransparency else 0f
            val showWallpaper = transparency > 0f
            LaunchedEffect(showWallpaper) { setWallpaperVisible(showWallpaper) }

            ClockTheme(darkTheme = true) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 1f - transparency)),
                ) {
                    // 배경 우선순위: 유튜브 > 동영상 > 이미지 > 검정
                    val hasYouTube = parseYouTube(settings.youtubeUrl) != null
                    val hasVideo = settings.videoUris.isNotEmpty()
                    val showVideo = hasVideo && !hasYouTube
                    // 동영상 배경이 화면에 없으면 재생 멈춤 (회전으로 다시 불려도 보이는 중이면 그대로)
                    LaunchedEffect(showVideo) { if (!showVideo) videoBackground.clear() }

                    if (hasYouTube || hasVideo || settings.backgroundVersion != 0L) {
                        when {
                            hasYouTube ->
                                YouTubeBackground(settings.youtubeUrl, settings.youtubeSound, Modifier.fillMaxSize())
                            showVideo ->
                                // 여러 동영상을 이어서 반복 재생
                                VideoBackground(
                                    videoBackground, settings.videoUris, settings.videoSound, settings.videoRepeat,
                                    Modifier.fillMaxSize(),
                                )
                            else ->
                                // 배경 사진: 1장이면 고정, 여러 장이면 슬라이드쇼
                                BackgroundSlideshow(
                                    settings.backgroundVersion, settings.imageRepeat, Modifier.fillMaxSize(),
                                )
                        }
                        // 시계·글자가 잘 보이도록 살짝 어둡게
                        Box(
                            Modifier
                                .fillMaxSize()
                                .background(Color.Black.copy(alpha = BACKGROUND_DIM)),
                        )
                    }
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    containerColor = Color.Transparent,
                ) { innerPadding ->
                    ClockScreen(
                        settings = settings,
                        onSettingsChange = {
                            settings = it
                            saveClockSettings(context, it)
                        },
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(innerPadding),
                        incomingYouTube = sharedYouTube,
                        onIncomingYouTubeConsumed = { sharedYouTube = null },
                        music = music,
                    )
                }
                }
            }
        }
    }

    /**
     * 폰 배경화면을 앱 뒤에 보이게/안 보이게.
     * 투명도 0% 일 때는 끄고 창 바탕을 검정으로 → 배경화면을 그리는 부담이 없음.
     * (창 형식은 onCreate 에서 TRANSLUCENT 로 고정, 여기서는 플래그와 바탕만 바꿈)
     */
    private fun setWallpaperVisible(visible: Boolean) {
        if (visible) {
            window.setBackgroundDrawable(ColorDrawable(android.graphics.Color.TRANSPARENT))
            window.addFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER)
            window.setBackgroundDrawable(ColorDrawable(android.graphics.Color.BLACK))
        }
    }

    /** 다른 앱에서 공유받은 유튜브 주소 → 유튜브 창에 채워짐. */
    private var sharedYouTube by mutableStateOf<String?>(null)

    /** 음악 재생기: 화면 회전으로 액티비티가 다시 만들어져도 유지. */
    private val music: MusicViewModel by viewModels()

    /** 배경(동영상) 재생기: 화면 회전으로 액티비티가 다시 만들어져도 유지. */
    private val videoBackground: VideoBackgroundViewModel by viewModels()

    override fun onStart() {
        super.onStart()
        music.onAppShown()
        videoBackground.onAppShown()
    }

    override fun onStop() {
        super.onStop()
        // 화면 회전 때문에 잠깐 멈추는 경우는 제외하고, 앱을 나가면 음악 일시정지
        if (!isChangingConfigurations) {
            music.onAppHidden()
            videoBackground.onAppHidden()
        }
    }

    /** 앱이 떠 있을 때 "공유 → 시계" 로 돌아온 경우 (singleTask). */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        receiveShared(intent)
    }

    private fun receiveShared(intent: Intent?) {
        // 음악 파일 공유 → 바로 재생 (한 곡 SEND, 여러 곡 SEND_MULTIPLE)
        if (intent?.type?.startsWith("audio/") == true) {
            val uris = when (intent.action) {
                Intent.ACTION_SEND -> listOfNotNull(intent.streamUri())
                Intent.ACTION_SEND_MULTIPLE -> intent.streamUris()
                else -> emptyList()
            }
            if (uris.isNotEmpty()) music.play(uris)
            return
        }
        if (intent?.action != Intent.ACTION_SEND) return
        val url = extractYouTubeUrl(intent.getStringExtra(Intent.EXTRA_TEXT))
        if (url != null) {
            sharedYouTube = url
        } else {
            Toast.makeText(this, R.string.youtube_invalid, Toast.LENGTH_SHORT).show()
        }
    }

    /**
     * adb 로 설치하면 런처가 홈 화면에 아이콘을 자동으로 만들지 않으므로,
     * 첫 실행 때 한 번만 "홈 화면에 추가" 시스템 요청을 띄운다.
     */
    private fun requestHomeShortcutOnce() {
        val prefs = getSharedPreferences("clock_prefs", MODE_PRIVATE)
        val shortcut = homeShortcut()
        if (prefs.getBoolean(KEY_SHORTCUT_REQUESTED, false)) {
            // 이미 만든 홈 화면 바로가기는 만들 때의 이름을 기억하므로, 앱 이름이 바뀌면 같이 고쳐 줌
            runCatching { ShortcutManagerCompat.updateShortcuts(this, listOf(shortcut)) }
            return
        }
        prefs.edit { putBoolean(KEY_SHORTCUT_REQUESTED, true) }
        if (!ShortcutManagerCompat.isRequestPinShortcutSupported(this)) return
        ShortcutManagerCompat.requestPinShortcut(this, shortcut, null)
    }

    /** 홈 화면 바로가기 (이름 = 앱 이름). */
    private fun homeShortcut(): ShortcutInfoCompat {
        val launchIntent = Intent(this, MainActivity::class.java)
            .setAction(Intent.ACTION_MAIN)
            .addCategory(Intent.CATEGORY_LAUNCHER)
        return ShortcutInfoCompat.Builder(this, "home_clock")
            .setShortLabel(getString(R.string.app_name))
            .setIcon(IconCompat.createWithResource(this, R.mipmap.ic_launcher))
            .setIntent(launchIntent)
            .build()
    }

    @Suppress("DEPRECATION")
    private fun Intent.streamUri(): Uri? = getParcelableExtra(Intent.EXTRA_STREAM)

    @Suppress("DEPRECATION")
    private fun Intent.streamUris(): List<Uri> =
        getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM).orEmpty()

    private companion object {
        const val KEY_SHORTCUT_REQUESTED = "home_shortcut_requested"

        /** 배경 사진 위에 덮는 검은색 투명도 (0 = 사진 그대로, 1 = 완전 검정). */
        const val BACKGROUND_DIM = 0.3f
    }
}
