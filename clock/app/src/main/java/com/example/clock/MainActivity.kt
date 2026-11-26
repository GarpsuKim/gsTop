package com.example.clock

import android.app.PictureInPictureParams
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.util.Rational
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
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.platform.LocalConfiguration
import com.example.clock.split.SplitSide
import com.example.clock.ui.AppListPanel
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
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
import androidx.compose.runtime.mutableIntStateOf
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
import com.example.clock.widget.ClockWidgetProvider
import com.example.clock.chime.ChimeScheduler
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
        // 홈 화면 위젯을 다시 그림 (누르기 동작·성명·글자색).
        // 재부팅·앱 업데이트 뒤 위젯 갱신 신호가 늦거나 막혀도(삼성 절전 앱 등) 앱을 한 번 열면 복구됨
        ClockWidgetProvider.updateAll(this)
        // 시보 정각 알람 다시 맞춤 (강제 중지 등으로 알람이 지워졌어도 앱을 열면 복구)
        ChimeScheduler.update(this)
        if (savedInstanceState == null) {
            requestHomeShortcutOnce()
            receiveShared(intent) // 앱이 꺼져 있을 때 "공유 → 시계" 로 시작된 경우
            receiveWidget(intent) // 앱이 꺼져 있을 때 위젯을 눌러 시작된 경우
        }
        setContent {
            var settings by remember { mutableStateOf(loadClockSettings(this)) }

            val context = this

            // 검은 배경일 때만 투명도 적용: 투명할수록 폰 배경화면이 비쳐 보임
            // (축소 화면일 때는 작은 창에 배경화면이 비치면 어색하므로 항상 검정)
            val transparency = if (settings.isBlackBackground && !inPip) settings.blackTransparency else 0f

            // 자체 화면 분할: 시계(배경 포함)를 고른 절반에만 그리고, 나머지 절반은 투명 → 폰 배경화면이 보임.
            // 화면을 돌리면 左↔上, 右↔下 로 자리를 맞춤. 축소 화면(PIP)에서는 분할하지 않음.
            // 고른 자리는 설정에 저장 → 앱을 다시 열어도 분할된 채로 시작 ([초기화] 하면 분할 해제)
            val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
            // 시스템 화면 분할로 다른 앱과 나란히 떠 있으면(앱 목록에서 연 앱이 옆에 열린 경우 등) 자체 분할은 하지 않음
            val activeSplit = if (inPip || inMultiWindow) null else settings.splitSide?.forOrientation(landscape)

            val showWallpaper = transparency > 0f || activeSplit != null
            LaunchedEffect(showWallpaper) { setWallpaperVisible(showWallpaper) }

            ClockTheme(darkTheme = true) {
                Box(Modifier.fillMaxSize()) {
                // 빈 절반: 앱 목록 (누르면 그 앱을 엶)
                val appListArea = when (activeSplit) {
                    null -> null
                    SplitSide.Left -> Modifier.fillMaxHeight().fillMaxWidth(0.5f).align(Alignment.CenterEnd)
                    SplitSide.Right -> Modifier.fillMaxHeight().fillMaxWidth(0.5f).align(Alignment.CenterStart)
                    SplitSide.Top -> Modifier.fillMaxWidth().fillMaxHeight(0.5f).align(Alignment.BottomCenter)
                    SplitSide.Bottom -> Modifier.fillMaxWidth().fillMaxHeight(0.5f).align(Alignment.TopCenter)
                }
                if (appListArea != null) {
                    AppListPanel(appListArea.windowInsetsPadding(WindowInsets.safeDrawing))
                }
                val clockArea = when (activeSplit) {
                    null -> Modifier.fillMaxSize()
                    SplitSide.Left -> Modifier.fillMaxHeight().fillMaxWidth(0.5f).align(Alignment.CenterStart)
                    SplitSide.Right -> Modifier.fillMaxHeight().fillMaxWidth(0.5f).align(Alignment.CenterEnd)
                    SplitSide.Top -> Modifier.fillMaxWidth().fillMaxHeight(0.5f).align(Alignment.TopCenter)
                    SplitSide.Bottom -> Modifier.fillMaxWidth().fillMaxHeight(0.5f).align(Alignment.BottomCenter)
                }
                Box(
                    clockArea
                        .clipToBounds() // 배경 사진·동영상·애니메이션이 나머지 절반으로 넘치지 않게
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
                        video = videoBackground,
                        inPip = inPip,
                        onEnterPip = ::enterPip,
                        fullscreenRequest = widgetLaunch,
                        split = activeSplit,
                        onSplitChange = {
                            settings = settings.copy(splitSide = it)
                            saveClockSettings(context, settings)
                        },
                        inSystemSplit = inMultiWindow,
                    )
                }
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

    /** 축소 화면(화면 속 화면, PIP) 모드인지. 축소 중에는 버튼을 숨기고 시계만 보여 준다. */
    private var inPip by mutableStateOf(false)

    /** 시스템 화면 분할 중인지 (그때는 자체 분할 대신 시계를 창 전체에). */
    private var inMultiWindow by mutableStateOf(false)

    override fun onResume() {
        super.onResume()
        inMultiWindow = isInMultiWindowMode && !isInPictureInPictureMode
    }

    override fun onMultiWindowModeChanged(isInMultiWindowMode: Boolean, newConfig: Configuration) {
        super.onMultiWindowModeChanged(isInMultiWindowMode, newConfig)
        inMultiWindow = isInMultiWindowMode && !isInPictureInPictureMode
    }

    /**
     * 축소 화면으로 전환: 다른 앱 위에 작은 창으로 시계가 떠 있음.
     * 창 비율: 디지털 = 가로로 긴 16:9, 아날로그/하이브리드 = 정사각형.
     * 작은 창을 누르면 나오는 확대 버튼으로 다시 전체 화면이 된다.
     */
    private fun enterPip(digital: Boolean) {
        val supported = packageManager.hasSystemFeature(PackageManager.FEATURE_PICTURE_IN_PICTURE)
        val params = PictureInPictureParams.Builder()
            .setAspectRatio(if (digital) Rational(16, 9) else Rational(1, 1))
            .build()
        val ok = supported && runCatching { enterPictureInPictureMode(params) }.getOrDefault(false)
        if (!ok) {
            Toast.makeText(this, getString(R.string.pip_failed, getString(R.string.app_name)), Toast.LENGTH_LONG).show()
        }
    }

    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean, newConfig: Configuration) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        inPip = isInPictureInPictureMode
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
        receiveWidget(intent)
    }

    /** 위젯을 눌러 들어올 때마다 1씩 커짐 → 시계 화면이 버튼을 숨긴 전체화면으로 바뀜. */
    private var widgetLaunch by mutableIntStateOf(0)

    private fun receiveWidget(intent: Intent?) {
        if (intent?.getBooleanExtra(EXTRA_FROM_WIDGET, false) == true) widgetLaunch++
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

    companion object {
        /** 위젯에서 시작했다는 표시 (true 면 전체화면으로 시작). */
        const val EXTRA_FROM_WIDGET = "com.example.clock.FROM_WIDGET"

        const val KEY_SHORTCUT_REQUESTED = "home_shortcut_requested"

        /** 배경 사진 위에 덮는 검은색 투명도 (0 = 사진 그대로, 1 = 완전 검정). */
        const val BACKGROUND_DIM = 0.3f
    }
}
