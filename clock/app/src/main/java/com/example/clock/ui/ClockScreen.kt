package com.example.clock.ui

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import androidx.compose.ui.platform.LocalConfiguration
import android.provider.AlarmClock
import android.widget.Toast
import androidx.compose.material.icons.filled.Alarm
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Public
import androidx.compose.material.icons.filled.WbTwilight
import androidx.compose.ui.graphics.vector.ImageVector
import android.net.Uri
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.HideImage
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.PictureInPictureAlt
import androidx.compose.material.icons.filled.HorizontalSplit
import androidx.compose.material.icons.filled.VerticalSplit
import com.example.clock.split.SplitSide
import kotlin.math.abs
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.material.icons.filled.BatterySaver
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Repeat
import androidx.compose.material.icons.filled.RepeatOne
import androidx.compose.material3.AlertDialog
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.material.icons.filled.Wallpaper
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.produceState
import androidx.compose.runtime.rememberUpdatedState
import kotlinx.coroutines.launch
import androidx.compose.ui.platform.LocalContext
import androidx.annotation.StringRes
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.ui.semantics.Role
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.withFrameNanos
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import android.os.SystemClock
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import com.example.clock.UserStore
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import com.example.clock.R
import kotlinx.coroutines.delay
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import kotlin.math.cos
import kotlin.math.sin

private val dateFormatter = DateTimeFormatter.ofLocalizedDate(FormatStyle.LONG) // 2026년 11월 24일
private val dayOfWeekFormatter = DateTimeFormatter.ofPattern("EEEE") // 화요일

private const val PREFS = "clock_prefs"
private const val KEY_DIAL_THEME = "dial_theme"
private const val KEY_DIAL_ANIMATION = "dial_animation"
private const val KEY_SHOW_DIGITAL = "show_digital"
private const val KEY_SHOW_SECONDS = "show_seconds"
private const val KEY_DISPLAY_MODE = "display_mode"
private const val KEY_DIGITAL_COLOR = "digital_color"
private const val KEY_DIGITAL_BOLD = "digital_bold"
private const val KEY_DIGITAL_NEON = "digital_neon"
private const val KEY_BACKGROUND_VERSION = "background_version"
private const val KEY_DIGITAL_SCROLL = "digital_scroll"
private const val KEY_YOUTUBE_URL = "youtube_url"
private const val KEY_YOUTUBE_SOUND = "youtube_sound"
private const val KEY_VIDEO_URIS = "video_uris"
private const val KEY_VIDEO_SOUND = "video_sound"
private const val KEY_VIDEO_REPEAT = "video_repeat"
private const val KEY_IMAGE_REPEAT = "image_repeat"
private const val KEY_POWER_MODE = "power_mode"
private const val KEY_BLACK_TRANSPARENCY = "black_transparency"
private const val KEY_AUTO_CLOSE = "auto_close"
private const val KEY_DIAL_NUMBERS = "dial_numbers"
private const val KEY_DIGITAL_DATE = "digital_date"
private const val KEY_DIGITAL_DATE_COLOR = "digital_date_color"
private const val KEY_SPLIT_SIDE = "split_side"
private const val KEY_SPLIT_RATIO = "split_ratio"

/** 아날로그·하이브리드 시계판 숫자. 순서 = 저장값(ordinal) → 새 항목은 끝에 추가할 것. */
enum class DialNumbers(@StringRes val label: Int) {
    Off(R.string.numbers_off),
    Quarters(R.string.numbers_quarters), // 3, 6, 9, 12
    All(R.string.numbers_all),           // 1 ~ 12
}

/**
 * [시계 닫히는 시간]: 마지막으로 화면을 만진 뒤(또는 앱이 화면에 나타난 뒤) 이 시간이 지나면 앱을 닫음.
 * Off(0) = 닫지 않음 (무한). 순서 = 저장값(ordinal) → 새 항목은 끝에 추가할 것.
 */
enum class AutoClose(val seconds: Long, @StringRes val unit: Int, val amount: Int) {
    Off(0L, 0, 0),
    S30(30L, R.string.unit_seconds, 30),
    M1(60L, R.string.unit_minutes, 1),
    M2(2 * 60L, R.string.unit_minutes, 2),
    M3(3 * 60L, R.string.unit_minutes, 3),
    M5(5 * 60L, R.string.unit_minutes, 5),
    M10(10 * 60L, R.string.unit_minutes, 10),
    M15(15 * 60L, R.string.unit_minutes, 15),
    M30(30 * 60L, R.string.unit_minutes, 30),
    M60(60 * 60L, R.string.unit_minutes, 60),
    M90(90 * 60L, R.string.unit_minutes, 90),
    M120(120 * 60L, R.string.unit_minutes, 120),
    M180(180 * 60L, R.string.unit_minutes, 180),
    M360(360 * 60L, R.string.unit_minutes, 360),
    H12(12 * 3600L, R.string.unit_hours, 12),
    D1(86_400L, R.string.unit_days, 1),
    D2(2 * 86_400L, R.string.unit_days, 2),
    D3(3 * 86_400L, R.string.unit_days, 3),
    D5(5 * 86_400L, R.string.unit_days, 5),
}

/** OLED 번인 방지: 이 간격마다 시계 화면 전체를 조금씩 옮김. */
private const val BURN_IN_SHIFT_MS = 3 * 60_000L
/** 번인 방지로 옮기는 거리 (가운데 기준 상하좌우·대각선). */
private val BurnInShift = 4.dp
/** 번인 방지 위치 순서: 가운데 → 8방향을 한 바퀴 (3분 × 9 = 27분 주기). */
private val burnInSteps = listOf(
    0 to 0, 1 to 0, 1 to 1, 0 to 1, -1 to 1, -1 to 0, -1 to -1, 0 to -1, 1 to -1,
)

enum class DialTheme(@StringRes val label: Int) {
    // 순서 = 저장값(ordinal). 새 테마는 끝에 추가할 것.
    Default(R.string.theme_default),
    Rainbow(R.string.theme_rainbow),
    Neon(R.string.theme_neon),
    Ocean(R.string.theme_ocean),
    Sunset(R.string.theme_sunset),
    Forest(R.string.theme_forest),
    Blossom(R.string.theme_blossom),
    Gold(R.string.theme_gold),
    Silver(R.string.theme_silver),
    Red(R.string.theme_red),
}

/** 화면 설정 묶음 (모두 SharedPreferences 에 저장). */
// 기본값 = 처음 설치했을 때의 설정이자 [초기화] 했을 때의 설정.
// (2026-11-25 사용 중이던 휴대폰 설정을 그대로 옮겨 옴: 디지털 · 오션 · 눈 내리는 밤 · 무지개 굵게 · 스크롤 ← · 투명 100%)
data class ClockSettings(
    val theme: DialTheme = DialTheme.Ocean,
    val animation: DialAnimation = DialAnimation.Snow,
    val mode: DisplayMode = DisplayMode.Digital,
    val showSeconds: Boolean = true,
    val digitalColor: DigitalColor = DigitalColor.Rainbow,
    val digitalBold: Boolean = true,
    val digitalNeon: Boolean = false,
    val digitalScroll: DigitalScroll = DigitalScroll.Left,
    /** 0 = 검은 바탕, 그 외 = 배경 사진 있음 (바뀔 때마다 새 값 → 다시 불러옴). */
    val backgroundVersion: Long = 0L,
    /** 유튜브 배경 주소 ("" = 꺼짐). 켜져 있으면 사진 배경보다 우선. */
    val youtubeUrl: String = "",
    val youtubeSound: Boolean = true,
    /** 배경(동영상) 목록 (비어 있으면 꺼짐). 우선순위: 유튜브 > 동영상 > 이미지. */
    val videoUris: List<String> = emptyList(),
    val videoSound: Boolean = true,
    /** true = 목록 전체 무한 반복, false = 한 번씩만 재생하고 마지막 화면에서 멈춤. */
    val videoRepeat: Boolean = true,
    /** 이미지 슬라이드쇼: true = 무한 반복, false = 한 번씩만 보여주고 마지막 사진에서 멈춤. */
    val imageRepeat: Boolean = true,
    /** 고급 모드(기본) / 전원 절약 모드. */
    val powerMode: PowerMode = PowerMode.Advanced,
    /** 검은 배경 투명도 (0 = 완전 검정, 1 = 완전 투명 → 폰 배경화면이 보임). 검은 배경일 때만 적용. */
    val blackTransparency: Float = 1f,
    /** 시계 닫히는 시간 (Off = 무한). */
    val autoClose: AutoClose = AutoClose.Off,
    /** 아날로그·하이브리드 시계판 숫자 (끔 / 3·6·9·12 / 전부). */
    val dialNumbers: DialNumbers = DialNumbers.Off,
    /** 디지털 날짜·요일 줄 (끔 / 위 / 아래). */
    val digitalDate: DigitalDate = DigitalDate.Off,
    /** 디지털 날짜·요일 줄 글자색 (날짜를 켰을 때만 [날짜색] 버튼이 보임). */
    val digitalDateColor: DigitalColor = DigitalColor.White,
    /** 자체 화면 분할: 시계 자리 (null = 분할 안 함). 앱을 다시 열어도 유지. */
    val splitSide: SplitSide? = null,
    /** 자체 화면 분할: 시계가 차지하는 비율 (구분선을 끌어 조절, 두 번 누르면 반반). */
    val splitRatio: Float = SplitSide.DEFAULT_RATIO,
) {
    /** 이미지·동영상·유튜브 배경이 모두 꺼진 검은 배경인지. */
    val isBlackBackground: Boolean
        get() = backgroundVersion == 0L && videoUris.isEmpty() && parseYouTube(youtubeUrl) == null
}

/**
 * 전원 모드. 순서 = 저장값(ordinal). 고른 설정(테마·애니메이션·스크롤·네온 등)은 바꾸지 않는다.
 * - 고급 모드: 처음 방식 그대로. 시각 1초에 20번 갱신(부드러운 초침), 애니메이션·스크롤은 화면 최대 속도,
 *   네온사인 흐림(블러) 빛 번짐.
 * - 전원 절약 모드: 시각 1초에 1번(초침 똑딱), 애니메이션·스크롤 초당 15장, 네온사인 흐림 없음.
 */
enum class PowerMode(@StringRes val label: Int) {
    Advanced(R.string.power_advanced),
    Saving(R.string.power_saving),
}

/** 애니메이션·스크롤 한 장면 간격 (밀리초). 0 = 화면 최대 속도 (매 프레임). */
val PowerMode.frameMs: Long
    get() = if (this == PowerMode.Saving) POWER_SAVING_FRAME_MS else 0L

/** 전원 절약 모드의 장면 간격: 66 = 초당 약 15번. */
private const val POWER_SAVING_FRAME_MS = 66L

/** 다음 장면까지 기다림: frameMs 가 0 이면 화면의 다음 프레임까지, 아니면 그만큼. */
internal suspend fun awaitNextFrame(frameMs: Long) {
    if (frameMs <= 0L) withFrameNanos { } else delay(frameMs)
}

fun loadClockSettings(context: Context): ClockSettings {
    val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    // 저장된 값이 없는 항목은 ClockSettings 의 기본값 (기본값은 그곳 한 군데에서만 정함)
    val d = ClockSettings()
    // 예전 "디지털 체크박스" 값이 저장돼 있으면 그걸로 모드를 정함 (켜짐=하이브리드, 꺼짐=아날로그)
    val fallbackMode = if (p.contains(KEY_SHOW_DIGITAL)) {
        if (p.getBoolean(KEY_SHOW_DIGITAL, true)) DisplayMode.Hybrid else DisplayMode.Analog
    } else {
        d.mode
    }
    return ClockSettings(
        theme = DialTheme.entries.getOrElse(p.getInt(KEY_DIAL_THEME, d.theme.ordinal)) { d.theme },
        animation = DialAnimation.entries.getOrElse(p.getInt(KEY_DIAL_ANIMATION, d.animation.ordinal)) { d.animation },
        mode = DisplayMode.entries.getOrElse(p.getInt(KEY_DISPLAY_MODE, fallbackMode.ordinal)) { fallbackMode },
        showSeconds = p.getBoolean(KEY_SHOW_SECONDS, d.showSeconds),
        digitalColor = DigitalColor.entries.getOrElse(p.getInt(KEY_DIGITAL_COLOR, d.digitalColor.ordinal)) { d.digitalColor },
        digitalBold = p.getBoolean(KEY_DIGITAL_BOLD, d.digitalBold),
        digitalNeon = p.getBoolean(KEY_DIGITAL_NEON, d.digitalNeon),
        digitalScroll = DigitalScroll.entries.getOrElse(p.getInt(KEY_DIGITAL_SCROLL, d.digitalScroll.ordinal)) { d.digitalScroll },
        backgroundVersion = p.getLong(KEY_BACKGROUND_VERSION, d.backgroundVersion),
        youtubeUrl = p.getString(KEY_YOUTUBE_URL, d.youtubeUrl).orEmpty(),
        youtubeSound = p.getBoolean(KEY_YOUTUBE_SOUND, d.youtubeSound),
        videoUris = p.getString(KEY_VIDEO_URIS, "").orEmpty().lines().filter { it.isNotBlank() },
        videoSound = p.getBoolean(KEY_VIDEO_SOUND, d.videoSound),
        videoRepeat = p.getBoolean(KEY_VIDEO_REPEAT, d.videoRepeat),
        imageRepeat = p.getBoolean(KEY_IMAGE_REPEAT, d.imageRepeat),
        powerMode = PowerMode.entries.getOrElse(p.getInt(KEY_POWER_MODE, d.powerMode.ordinal)) { d.powerMode },
        blackTransparency = p.getFloat(KEY_BLACK_TRANSPARENCY, d.blackTransparency).coerceIn(0f, 1f),
        autoClose = AutoClose.entries.getOrElse(p.getInt(KEY_AUTO_CLOSE, d.autoClose.ordinal)) { d.autoClose },
        dialNumbers = DialNumbers.entries.getOrElse(p.getInt(KEY_DIAL_NUMBERS, d.dialNumbers.ordinal)) { d.dialNumbers },
        digitalDate = DigitalDate.entries.getOrElse(p.getInt(KEY_DIGITAL_DATE, d.digitalDate.ordinal)) { d.digitalDate },
        digitalDateColor = DigitalColor.entries.getOrElse(p.getInt(KEY_DIGITAL_DATE_COLOR, d.digitalDateColor.ordinal)) { d.digitalDateColor },
        splitSide = SplitSide.entries.getOrNull(p.getInt(KEY_SPLIT_SIDE, -1)), // -1 = 분할 안 함
        splitRatio = p.getFloat(KEY_SPLIT_RATIO, d.splitRatio).coerceIn(SplitSide.MIN_RATIO, SplitSide.MAX_RATIO),
    )
}

fun saveClockSettings(context: Context, s: ClockSettings) {
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
        putInt(KEY_DIAL_THEME, s.theme.ordinal)
        putInt(KEY_DIAL_ANIMATION, s.animation.ordinal)
        putInt(KEY_DISPLAY_MODE, s.mode.ordinal)
        putBoolean(KEY_SHOW_SECONDS, s.showSeconds)
        putInt(KEY_DIGITAL_COLOR, s.digitalColor.ordinal)
        putBoolean(KEY_DIGITAL_BOLD, s.digitalBold)
        putBoolean(KEY_DIGITAL_NEON, s.digitalNeon)
        putInt(KEY_DIGITAL_SCROLL, s.digitalScroll.ordinal)
        putLong(KEY_BACKGROUND_VERSION, s.backgroundVersion)
        putString(KEY_YOUTUBE_URL, s.youtubeUrl)
        putBoolean(KEY_YOUTUBE_SOUND, s.youtubeSound)
        putString(KEY_VIDEO_URIS, s.videoUris.joinToString("\n"))
        putBoolean(KEY_VIDEO_SOUND, s.videoSound)
        putBoolean(KEY_VIDEO_REPEAT, s.videoRepeat)
        putBoolean(KEY_IMAGE_REPEAT, s.imageRepeat)
        putInt(KEY_POWER_MODE, s.powerMode.ordinal)
        putFloat(KEY_BLACK_TRANSPARENCY, s.blackTransparency)
        putInt(KEY_AUTO_CLOSE, s.autoClose.ordinal)
        putInt(KEY_DIAL_NUMBERS, s.dialNumbers.ordinal)
        putInt(KEY_DIGITAL_DATE, s.digitalDate.ordinal)
        putInt(KEY_DIGITAL_DATE_COLOR, s.digitalDateColor.ordinal)
        putInt(KEY_SPLIT_SIDE, s.splitSide?.ordinal ?: -1)
        putFloat(KEY_SPLIT_RATIO, s.splitRatio)
    }
}

private val DialPadding = 16.dp

@OptIn(ExperimentalLayoutApi::class, ExperimentalFoundationApi::class)
@Composable
fun ClockScreen(
    settings: ClockSettings,
    onSettingsChange: (ClockSettings) -> Unit,
    modifier: Modifier = Modifier,
    /** 다른 앱에서 "공유 → 시계" 로 받은 유튜브 주소 (없으면 null). */
    incomingYouTube: String? = null,
    onIncomingYouTubeConsumed: () -> Unit = {},
    /** 음악 재생기 (화면 회전에도 유지되도록 액티비티의 ViewModel). */
    music: MusicViewModel? = null,
    /** 배경(동영상) 재생기 (재생 위치 슬라이더용). */
    video: VideoBackgroundViewModel? = null,
    /** 축소 화면(PIP) 중이면 true: 버튼을 모두 숨기고 시계만. */
    inPip: Boolean = false,
    /** [⧉ 축소] 버튼: 축소 화면으로 전환 (인자 = 디지털 모드인지). null 이면 버튼 없음. */
    onEnterPip: ((digital: Boolean) -> Unit)? = null,
    /** 위젯을 눌러 들어올 때마다 1씩 커짐: 버튼을 숨기고 상태바·내비게이션바까지 감춘 전체화면으로. */
    fullscreenRequest: Int = 0,
    /** 자체 화면 분할: 시계가 있는 자리 (null = 전체 화면). */
    split: SplitSide? = null,
    /** [화면분할] 버튼·두 손가락 스와이프로 분할 자리를 바꿈 (null = 분할 해제). null 이면 기능 없음. */
    onSplitChange: ((SplitSide?) -> Unit)? = null,
    /** 시스템 화면 분할로 다른 앱과 나란히 떠 있는지 ([화면분할] 메뉴에 [분할 해제] 만). */
    inSystemSplit: Boolean = false,
) {
    val theme = settings.theme
    val animation = settings.animation
    var now by remember { mutableStateOf(LocalTime.now()) }
    var today by remember { mutableStateOf(LocalDate.now()) }

    // 고급 모드: 0.05초마다(1초에 20번) 갱신 → 초침이 부드럽게 흐름.
    // 전원 절약 모드: 1초에 한 번, 초가 바뀌는 순간에 맞춰 갱신 → 초침 "똑딱", CPU 크게 절약.
    // 초침이 없을 때 (아날로그/하이브리드 + 초침 끔): 분이 바뀌는 순간에만 1분에 한 번 갱신 → 전력 절약.
    // (디지털은 초까지 표시하므로 해당 없음)
    val minuteOnly = settings.mode != DisplayMode.Digital && !settings.showSeconds
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(settings.powerMode, minuteOnly, lifecycle) {
        val saving = settings.powerMode == PowerMode.Saving
        // 앱이 보일 때만 갱신. 다시 보이면 즉시 현재 시각으로 (화면이 꺼졌다 켜져도 1분 늦게 보이지 않게)
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
        while (true) {
            now = when {
                minuteOnly -> LocalTime.now().withSecond(0).withNano(0) // 분침이 분 눈금에 정확히
                saving -> LocalTime.now().withNano(0)
                else -> LocalTime.now()
            }
            today = LocalDate.now()
            delay(
                when {
                    minuteOnly -> 60_000L - System.currentTimeMillis() % 60_000L
                    saving -> 1_000L - System.currentTimeMillis() % 1_000L
                    else -> 50L
                },
            )
        }
        }
    }

    val style = dialStyle(theme)

    val digitalOnly = settings.mode == DisplayMode.Digital

    // 화면을 누를 때마다 버튼 보이기/숨기기 (회전해도 유지)
    // 앱은 항상 버튼 없이 시작 (화면을 누르면 버튼이 나타남)
    var controlsVisible by rememberSaveable { mutableStateOf(false) }
    // 전체화면: 상태바/내비게이션바 숨김. 앱은 항상 전체화면으로 시작하고,
    // 화면을 눌러 버튼을 다시 보이면 바도 돌아옴. 앱이 떠 있을 때 위젯을 누르면 다시 전체화면으로.
    // 유튜브 주소를 공유받으면 버튼이 숨겨져 있어도 유튜브 창이 보이도록 다시 표시
    var widgetFullscreen by rememberSaveable { mutableStateOf(true) }
    LaunchedEffect(fullscreenRequest) {
        if (fullscreenRequest > 0) {
            controlsVisible = false
            widgetFullscreen = true
        }
    }
    LaunchedEffect(incomingYouTube) {
        if (incomingYouTube != null) {
            controlsVisible = true
            widgetFullscreen = false
        }
    }
    val window = (LocalContext.current as? Activity)?.window
    LaunchedEffect(widgetFullscreen, window) {
        val w = window ?: return@LaunchedEffect
        val bars = WindowCompat.getInsetsController(w, w.decorView)
        if (widgetFullscreen) {
            // 가장자리에서 밀면 잠깐 나타났다가 다시 숨음
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
        }
    }

    // 사용자 등록 창: 사용자 성명이 아직 없으면 앱을 시작할 때 자동으로 띄움
    val appContext = LocalContext.current
    var userDialog by rememberSaveable { mutableStateOf(UserStore.load(appContext).name.isBlank()) }

    // 축소 화면(PIP) 중에는 작은 창에 시계만 보이도록 버튼을 모두 숨김
    val showControls = controlsVisible && !inPip

    val menuHost = remember { InlineMenuHost() }
    // 버튼을 숨기면 열려 있던 메뉴도 함께 사라지도록 (숨겨진 버튼의 메뉴가 남지 않게)
    LaunchedEffect(showControls) { if (!showControls) menuHost.current = null }

    val haptic = LocalHapticFeedback.current

    // [시계 닫히는 시간]: 화면을 만질 때마다, 그리고 앱이 다시 화면에 나타날 때마다 처음부터 다시 셈.
    // 시간이 다 되면 앱을 닫음 → 화면 켜짐 유지가 풀려 폰이 평소대로 화면을 끔. Off = 무한.
    var touchCount by remember { mutableIntStateOf(0) }
    val activity = LocalContext.current as? Activity
    LaunchedEffect(settings.autoClose, touchCount, lifecycle) {
        val limitMs = settings.autoClose.seconds * 1_000L
        if (limitMs <= 0L || activity == null) return@LaunchedEffect
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            // 실제 경과 시간(elapsedRealtime)으로 판단: 폰이 잠들어 delay 가 늦어져도 정확히 닫힘
            val deadline = SystemClock.elapsedRealtime() + limitMs
            while (true) {
                val remain = deadline - SystemClock.elapsedRealtime()
                if (remain <= 0L) {
                    activity.finish()
                    break
                }
                delay(remain.coerceAtMost(60_000L))
            }
        }
    }

    // 두 손가락 스와이프에서 최신 값을 쓰도록
    val latestOnSplitChange by rememberUpdatedState(onSplitChange)
    val screenLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val latestLandscape by rememberUpdatedState(screenLandscape)
    // 분할 중(자체 분할 또는 시스템 분할)에는 두 손가락 스와이프로 분할하지 않음
    val latestSplitting by rememberUpdatedState(split != null || inSystemSplit)

    // OLED 번인 방지: 3분마다 시계 화면 전체를 4dp 씩 옮김 (가운데 → 8방향, 27분에 한 바퀴. 눈에 거의 안 띔)
    var burnInStep by remember { mutableIntStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(BURN_IN_SHIFT_MS)
            burnInStep = (burnInStep + 1) % burnInSteps.size
        }
    }
    val burnInPx = with(LocalDensity.current) { BurnInShift.toPx() }

    CompositionLocalProvider(LocalInlineMenuHost provides menuHost) {
    Box(
        modifier
            .graphicsLayer {
                // 축소 화면(PIP)은 창이 작아서 옮기지 않음
                if (!inPip) {
                    val (dx, dy) = burnInSteps[burnInStep]
                    translationX = dx * burnInPx
                    translationY = dy * burnInPx
                }
            }
            // 화면을 만지면 [시계 닫히는 시간] 을 처음부터 다시 셈 (누르기·끌기 동작은 그대로 전달)
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val e = awaitPointerEvent(PointerEventPass.Initial)
                        if (e.type == PointerEventType.Press) touchCount++
                    }
                }
            }
            // 두 손가락 스와이프 → 화면 분할 (한 손가락 끌기·누르기는 그대로 아래로 전달)
            //  가로 화면: → 右 분할, ← 左 분할 / 세로 화면: ↑ 上 분할, ↓ 下 분할. 분할 중에는 동작 안 함
            .pointerInput(inPip) {
                if (inPip) return@pointerInput
                val minPx = TwoFingerSwipeMin.toPx()
                awaitPointerEventScope {
                    while (true) {
                        var e = awaitPointerEvent(PointerEventPass.Initial)
                        if (e.changes.count { it.pressed } < 2) continue
                        // 두 손가락이 닿은 순간부터 모든 손가락을 뗄 때까지: 이동량 합산 + 아래(스크롤 끌기·누르기)로 안 보냄
                        var dx = 0f
                        var dy = 0f
                        while (true) {
                            val moving = e.changes.filter { it.pressed && it.previousPressed }
                            if (moving.isNotEmpty()) {
                                dx += moving.sumOf { (it.position.x - it.previousPosition.x).toDouble() }.toFloat() / moving.size
                                dy += moving.sumOf { (it.position.y - it.previousPosition.y).toDouble() }.toFloat() / moving.size
                            }
                            e.changes.forEach { it.consume() }
                            if (e.changes.none { it.pressed }) break
                            e = awaitPointerEvent(PointerEventPass.Initial)
                        }
                        // 분할 중이면 무시 (두 손가락 동작은 위에서 이미 막았으므로 글자 끌기 등도 일어나지 않음)
                        if (latestSplitting) continue
                        val change = latestOnSplitChange ?: continue
                        val landscape = latestLandscape
                        val side = when {
                            landscape && abs(dx) > abs(dy) && dx >= minPx -> SplitSide.Right
                            landscape && abs(dx) > abs(dy) && dx <= -minPx -> SplitSide.Left
                            !landscape && abs(dy) > abs(dx) && dy <= -minPx -> SplitSide.Top
                            !landscape && abs(dy) > abs(dx) && dy >= minPx -> SplitSide.Bottom
                            else -> null
                        }
                        if (side != null) change(side)
                    }
                }
            }
            .combinedClickable(
            interactionSource = remember { MutableInteractionSource() },
            indication = null, // 누를 때 물결 효과 없이
            // 디지털: 화면을 꾹 누르고 있으면 스크롤 종료 (설정에 저장됨)
            onLongClick = if (digitalOnly && settings.digitalScroll != DigitalScroll.Off) {
                {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onSettingsChange(settings.copy(digitalScroll = DigitalScroll.Off))
                }
            } else {
                null
            },
        ) {
            controlsVisible = !controlsVisible
            if (controlsVisible) widgetFullscreen = false // 버튼을 다시 보이면 전체화면 해제
        },
    ) {
        if (digitalOnly) {
            // 디지털: 글자를 좌우로 끌면 손가락을 따라 움직이고, 놓으면 그 방향으로 스크롤 (설정에 저장됨)
            val swipeSettings by rememberUpdatedState(settings)
            val swipeOnChange by rememberUpdatedState(onSettingsChange)
            BigDigitalClock(
                time = now,
                color = settings.digitalColor,
                bold = settings.digitalBold,
                neon = settings.digitalNeon,
                scroll = settings.digitalScroll,
                powerMode = settings.powerMode,
                date = today,
                datePosition = settings.digitalDate,
                dateColor = settings.digitalDateColor,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(DialPadding),
                onScrollChange = { swipeOnChange(swipeSettings.copy(digitalScroll = it)) },
            )
        } else {
            AnalogClockArea(settings, style, now, today)
        }

        // 버튼 묶음 A (좌상단): [표시 방식 ▾] + 아날로그/하이브리드 → [☑ 초침],  디지털 → [☑ 굵게] [☑ 네온사인]  + [↺ 초기화]
        val modeControls: @Composable () -> Unit = {
            FlowRow(horizontalArrangement = Arrangement.Start) { // 왼쪽부터, 좁으면 다음 줄로
                SmallMenu(
                    selected = settings.mode,
                    items = DisplayMode.entries,
                    label = { stringResource(it.label) },
                    onSelect = { onSettingsChange(settings.copy(mode = it)) },
                )
                if (digitalOnly) {
                    SmallCheck(
                        label = stringResource(R.string.digital_bold),
                        checked = settings.digitalBold,
                        onCheckedChange = { onSettingsChange(settings.copy(digitalBold = it)) },
                    )
                    SmallCheck(
                        label = stringResource(R.string.digital_neon),
                        checked = settings.digitalNeon,
                        onCheckedChange = { onSettingsChange(settings.copy(digitalNeon = it)) },
                    )
                    // [날짜 ▾] → 위치 (끔 / 상 / 하) + 구분선 + 날짜색 을 한 메뉴에서
                    DigitalDateMenu(settings, onSettingsChange)
                } else {
                    SmallCheck(
                        label = stringResource(R.string.show_seconds),
                        checked = settings.showSeconds,
                        onCheckedChange = { onSettingsChange(settings.copy(showSeconds = it)) },
                    )
                    // [숫자 ▾] → 끔 / 3·6·9·12 / 전부
                    SmallMenu(
                        selected = settings.dialNumbers,
                        items = DialNumbers.entries,
                        label = { stringResource(it.label) },
                        onSelect = { onSettingsChange(settings.copy(dialNumbers = it)) },
                    )
                }
                // [⚡ 고급 모드 ▾] / [전원 절약 모드 ▾]
                SmallMenu(
                    selected = settings.powerMode,
                    items = PowerMode.entries,
                    label = { stringResource(it.label) },
                    icon = {
                        Icon(
                            if (it == PowerMode.Saving) Icons.Filled.BatterySaver else Icons.Filled.Bolt,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp),
                        )
                    },
                    onSelect = { onSettingsChange(settings.copy(powerMode = it)) },
                )
                // [↺ 초기화] → 확인 후 모든 설정을 처음 상태로
                // [⧉ 화면 ▾] → 축소(화면 속 화면) + 구분선 + 화면 분할 (가로: 左/右, 세로: 上/下, 분할 중이면 분할 해제)
                ScreenMenu(
                    onEnterPip = onEnterPip?.let { pip -> { pip(digitalOnly) } },
                    current = split,
                    systemSplit = inSystemSplit,
                    onSplitChange = if (inPip) null else onSplitChange,
                )
                ResetButton(onReset = { onSettingsChange(ClockSettings()) })
            }
        }

        // 버튼 묶음 B (우하단): 아날로그/하이브리드 → [애니메이션 ▾] [테마 ▾],  디지털 → [스크롤 ▾] [글자색 ▾]  + [🖼 배경 ▾](이미지·동영상·음악) [▶ 유튜브] [📅 달력] [🔔 시보·알람 ▾] [⏱ 닫힘 ▾] [👤 사용자]
        val styleControls: @Composable () -> Unit = {
            FlowRow(horizontalArrangement = Arrangement.End) { // 좁으면 다음 줄로
                if (digitalOnly) {
                    SmallMenu(
                        selected = settings.digitalScroll,
                        items = DigitalScroll.entries,
                        label = { stringResource(it.label) },
                        onSelect = { onSettingsChange(settings.copy(digitalScroll = it)) },
                    )
                    SmallMenu(
                        selected = settings.digitalColor,
                        items = DigitalColor.entries,
                        label = { stringResource(it.label) },
                        icon = { ColorSwatch(it) },
                        onSelect = { onSettingsChange(settings.copy(digitalColor = it)) },
                    )
                } else {
                    SmallMenu(
                        selected = animation,
                        items = DialAnimation.entries,
                        label = { stringResource(it.label) },
                        onSelect = { onSettingsChange(settings.copy(animation = it)) },
                    )
                    SmallMenu(
                        selected = theme,
                        items = DialTheme.entries,
                        label = { stringResource(it.label) },
                        onSelect = { onSettingsChange(settings.copy(theme = it)) },
                    )
                }
                // [🖼 배경 ▾] → 이미지 / 동영상 / 음악 을 구분선으로 나눈 한 메뉴 (모든 모드 공통)
                BackgroundButton(settings, onSettingsChange, video, music)
                // [▶ 유튜브] → 유튜브 영상을 배경으로 (모든 모드 공통)
                YouTubeButton(
                    url = settings.youtubeUrl,
                    sound = settings.youtubeSound,
                    onChange = { url, sound ->
                        onSettingsChange(settings.copy(youtubeUrl = url, youtubeSound = sound))
                    },
                    incoming = incomingYouTube,
                    onIncomingConsumed = onIncomingYouTubeConsumed,
                )
                // [📅 달력] → 기본 브라우저로 음력 달력 페이지 (모든 모드 공통)
                CalendarButton()
                // [🔔 시보·알람 ▾] → 시보 설정 (매시 정각 연주, 앱이 꺼져 있어도 울림) + 구분선 + 시스템 알람 화면 (모든 모드 공통)
                ChimeAlarmButton()
                // [⏱ 닫힘: 무한 ▾] → 시계 닫히는 시간 (모든 모드 공통)
                SmallMenu(
                    selected = settings.autoClose,
                    items = AutoClose.entries,
                    label = { autoCloseLabel(it) },
                    onSelect = { onSettingsChange(settings.copy(autoClose = it)) },
                )
                // [👤 사용자 등록] → 성명 + 긴급 연락처 (모든 모드 공통)
                UserButton(onClick = { userDialog = true })
            }
        }

        // 버튼 배치 (가로·세로 공통): A 는 좌상단, B 는 우하단.
        // 가로 화면은 넓어서 각각 1줄, 세로 화면은 폭이 모자라면 자동으로 2줄로 나뉨.
        AnimatedVisibility(
            visible = showControls,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(Alignment.TopStart),
        ) {
            Box(Modifier.padding(top = 4.dp, start = 8.dp, end = 8.dp)) { modeControls() }
        }
        AnimatedVisibility(
            visible = showControls,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(Alignment.BottomEnd),
        ) {
            Box(Modifier.padding(bottom = 4.dp, start = 8.dp, end = 8.dp)) { styleControls() }
        }

        // 검은 배경일 때만 투명도 슬라이더 (폰 배경화면이 비침)
        // 세로 화면 → 오른쪽 가운데에 세로로 (위로 = 투명), 가로 화면 → 상단 가운데에 가로로 (오른쪽 = 투명)
        val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
        AnimatedVisibility(
            visible = showControls && settings.isBlackBackground,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(if (landscape) Alignment.TopCenter else Alignment.CenterEnd),
        ) {
            TransparencySlider(
                value = settings.blackTransparency,
                onValueChange = { onSettingsChange(settings.copy(blackTransparency = it)) },
                // 가로: 상단 버튼 한 줄(약 44dp) 아래로 내려서 버튼과 겹치지 않게
                modifier = if (landscape) Modifier.padding(top = 48.dp) else Modifier.padding(end = 4.dp),
                horizontal = landscape,
            )
        }

        // 열린 목록 메뉴는 별도 창이 아니라 이 화면 맨 위에 그림 (터치가 뒤로 새지 않도록)
        InlineMenuLayer(menuHost)

        // 사용자 등록 창 (축소 화면에서는 띄우지 않음)
        if (userDialog && !inPip) UserRegisterDialog(onDismiss = { userDialog = false })
    }
    }
}

/** 두 손가락 스와이프를 화면 분할로 볼 최소 이동 거리. */
private val TwoFingerSwipeMin = 80.dp

/**
 * [⧉ 화면 ▾] 버튼: 축소 화면과 화면 분할을 한 메뉴에서, 구분선으로 나눠서.
 *  - 축소: [작은 창으로 축소] → 다른 앱 위에 작은 창으로 (화면 속 화면, PIP)
 *  - 화면 분할 (자체 분할: 시계를 고른 쪽에, 나머지는 앱 목록)
 *      가로 화면 → [左 분할] [右 분할], 세로 화면 → [上 분할] [下 분할]
 *      이미 분할된 상태(자체 분할 또는 시스템 분할)면 [분할 해제] 만
 * [onEnterPip] / [onSplitChange] 가 null 이면 그 구역은 없음. 둘 다 없으면 버튼도 없음.
 */
@Composable
private fun ScreenMenu(
    onEnterPip: (() -> Unit)?,
    current: SplitSide?,
    systemSplit: Boolean,
    onSplitChange: ((SplitSide?) -> Unit)?,
) {
    if (onEnterPip == null && onSplitChange == null) return
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(
                if (landscape) Icons.Filled.VerticalSplit else Icons.Filled.HorizontalSplit,
                contentDescription = null,
                modifier = Modifier.size(16.dp),
            )
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.screen_menu), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            // ── 축소 ──
            if (onEnterPip != null) {
                MenuSectionTitle(R.string.pip)
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.pip_item), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = {
                        Icon(Icons.Filled.PictureInPictureAlt, contentDescription = null, modifier = Modifier.size(18.dp))
                    },
                    onClick = {
                        expanded = false
                        onEnterPip()
                    },
                )
            }
            // ── 화면 분할 ──
            if (onSplitChange == null) return@InlineDropdown
            if (onEnterPip != null) HorizontalDivider(Modifier.padding(vertical = 4.dp))
            MenuSectionTitle(R.string.split)
            val onChange = onSplitChange
            if (current != null || systemSplit) {
                // 이미 분할됨 → [분할 해제] 만
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.split_exit), style = MaterialTheme.typography.bodyMedium) },
                    onClick = {
                        expanded = false
                        onChange(null)
                        // 시스템 분할은 앱이 직접 풀 수 없음 → 방법 안내
                        if (systemSplit) {
                            Toast.makeText(context, R.string.split_system_exit_hint, Toast.LENGTH_LONG).show()
                        }
                    },
                )
            } else {
                SplitSide.sides(landscape).forEach { side ->
                    DropdownMenuItem(
                        text = { Text(stringResource(side.label), style = MaterialTheme.typography.bodyMedium) },
                        leadingIcon = {
                            Icon(
                                if (landscape) Icons.Filled.VerticalSplit else Icons.Filled.HorizontalSplit,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                            )
                        },
                        onClick = {
                            expanded = false
                            onChange(side)
                        },
                    )
                }
            }
        }
    }
}

/**
 * 초기화: 확인 창을 띄운 뒤 모든 설정(표시 방식, 테마, 애니메이션, 초침, 글자색,
 * 굵게, 네온사인, 스크롤, 배경 사진)을 처음 상태로 되돌린다. 배경 사진 파일도 지운다.
 */
@Composable
private fun ResetButton(onReset: () -> Unit) {
    val context = LocalContext.current
    var confirming by remember { mutableStateOf(false) }

    TextButton(
        onClick = { confirming = true },
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
    ) {
        Icon(Icons.Filled.RestartAlt, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(Modifier.width(4.dp))
        Text(stringResource(R.string.reset), style = MaterialTheme.typography.labelMedium)
    }

    if (confirming) {
        AlertDialog(
            onDismissRequest = { confirming = false },
            title = { Text(stringResource(R.string.reset_title)) },
            text = { Text(stringResource(R.string.reset_message)) },
            confirmButton = {
                TextButton(onClick = {
                    confirming = false
                    BackgroundStore.clear(context)
                    onReset()
                }) { Text(stringResource(R.string.reset)) }
            },
            dismissButton = {
                TextButton(onClick = { confirming = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

/**
 * [🖼 배경 ▾] 버튼: 배경(이미지) / 배경(동영상) / 음악 을 한 메뉴에서, 구분선으로 나눠서.
 *  - 이미지: 지금 사진 / 사진 선택(여러 장 = 슬라이드쇼) / 무한 반복 / 검은색 / 끄기
 *  - 동영상: 지금 파일 / 재생 위치 / 동영상 선택 / 무한 반복 / 소리 / 끄기   ([VideoBackgroundItems])
 *  - 음악:   현재 곡 / 재생 위치 / 재생·일시정지 / 다음 곡 / 다른 음악 / 끄기 ([MusicItems])
 * 고르기 창(사진·동영상·음악)은 메뉴가 닫혀도 결과를 받을 수 있도록 메뉴 바깥(이 버튼)에 만들어 둔다.
 */
@Composable
private fun BackgroundButton(
    settings: ClockSettings,
    onSettingsChange: (ClockSettings) -> Unit,
    video: VideoBackgroundViewModel?,
    music: MusicViewModel?,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    // 사진을 복사하는 동안 설정이 바뀌어도 최신 값에 반영되도록
    val latestSettings by rememberUpdatedState(settings)
    val latestOnChange by rememberUpdatedState(onSettingsChange)
    var expanded by remember { mutableStateOf(false) }

    // 1장 이상 선택: 1장 = 고정 배경, 여러 장 = 슬라이드쇼
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult // 선택 취소
        scope.launch {
            val saved = BackgroundStore.saveAll(context, uris)
            when {
                saved == 0 ->
                    Toast.makeText(context, R.string.background_failed, Toast.LENGTH_SHORT).show()
                else -> {
                    // 이미지를 고르면 그보다 위에 있는 동영상·유튜브 배경은 끔 (고른 사진이 바로 보이게)
                    latestOnChange(
                        latestSettings.copy(
                            backgroundVersion = System.currentTimeMillis(),
                            videoUris = emptyList(),
                            youtubeUrl = "",
                        ),
                    )
                    if (saved > 1) {
                        val msg = context.getString(R.string.background_slideshow, saved)
                        Toast.makeText(context, msg, Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }
    }

    val pickVideo = rememberVideoPicker(settings, onSettingsChange)
    val pickMusic = if (music != null) rememberMusicPicker(music) else null
    val close = { expanded = false }

    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(Icons.Filled.Wallpaper, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.background_menu), style = MaterialTheme.typography.labelMedium)
            // 음악이 나오는 중이면 버튼에도 표시
            if (music?.isPlaying == true) {
                Spacer(Modifier.width(4.dp))
                Icon(Icons.Filled.MusicNote, contentDescription = null, modifier = Modifier.size(14.dp))
            }
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            // ── 이미지 ──
            MenuSectionTitle(R.string.background)
            val hasImage = settings.backgroundVersion != 0L
            // 지금(가장 최근) 사진 파일 이름 (이름 기록이 없는 예전 사진이면 표시 안 함)
            val names by produceState(emptyList<String>(), settings.backgroundVersion) {
                value = if (hasImage) BackgroundStore.names(context) else emptyList()
            }
            if (hasImage && names.isNotEmpty()) {
                val i = BackgroundStore.shownIndex.coerceIn(0, names.lastIndex)
                CurrentFileItem(i, names.size, names[i])
            }
            DropdownMenuItem(
                text = { Text(stringResource(R.string.background_pick), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.Image, contentDescription = null, modifier = Modifier.size(18.dp)) },
                trailingIcon = if (hasImage) {
                    { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                } else null,
                onClick = {
                    expanded = false
                    picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
                },
            )
            // 슬라이드쇼 무한 반복 On/Off (사진을 고르기 전에도 정할 수 있게 항상 표시)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.image_repeat), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = {
                    Icon(
                        if (settings.imageRepeat) Icons.Filled.Repeat else Icons.Filled.RepeatOne,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                    )
                },
                trailingIcon = {
                    Checkbox(checked = settings.imageRepeat, onCheckedChange = null, modifier = Modifier.size(18.dp))
                },
                onClick = { onSettingsChange(settings.copy(imageRepeat = !settings.imageRepeat)) },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.background_black), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { ColorDot(Color.Black) },
                trailingIcon = if (!hasImage) {
                    { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                } else null,
                onClick = {
                    expanded = false
                    BackgroundStore.clear(context)
                    onSettingsChange(settings.copy(backgroundVersion = 0L))
                },
            )
            // 끄기: 고른 사진을 지우고 검은 배경으로 (사진이 없으면 흐리게)
            DropdownMenuItem(
                text = { Text(stringResource(R.string.image_off), style = MaterialTheme.typography.bodyMedium) },
                leadingIcon = { Icon(Icons.Filled.HideImage, contentDescription = null, modifier = Modifier.size(18.dp)) },
                enabled = hasImage,
                onClick = {
                    expanded = false
                    BackgroundStore.clear(context)
                    onSettingsChange(settings.copy(backgroundVersion = 0L))
                },
            )

            // ── 동영상 ──
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            MenuSectionTitle(R.string.video_background)
            VideoBackgroundItems(settings, onSettingsChange, video, onPick = pickVideo, close = close)

            // ── 음악 ──
            if (music != null && pickMusic != null) {
                HorizontalDivider(Modifier.padding(vertical = 4.dp))
                MenuSectionTitle(R.string.music)
                MusicItems(music, onPick = pickMusic, close = close)
            }
        }
    }
}

/**
 * 디지털 [날짜 ▾] 버튼: 한 메뉴에서 구분선으로 나눠
 *  - 위치: 날짜 끔 / 날짜(상) / 날짜(하)
 *  - 날짜색: 흰색 ~ 무지개 (날짜를 켰을 때만 고를 수 있음, 끄면 흐리게)
 * 버튼에는 지금 위치와, 날짜를 켰으면 날짜색 동그라미를 표시.
 * 위치·색을 골라도 메뉴는 열려 있어서 바로 이어서 고를 수 있음.
 */
@Composable
private fun DigitalDateMenu(settings: ClockSettings, onSettingsChange: (ClockSettings) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val dateOn = settings.digitalDate != DigitalDate.Off
    val checkIcon: @Composable () -> Unit = {
        Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp))
    }
    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            if (dateOn) {
                ColorSwatch(settings.digitalDateColor)
                Spacer(Modifier.width(6.dp))
            }
            Text(stringResource(settings.digitalDate.label), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            // ── 위치 ──
            MenuSectionTitle(R.string.date_section_position)
            DigitalDate.entries.forEach { d ->
                DropdownMenuItem(
                    text = { Text(stringResource(d.label), style = MaterialTheme.typography.bodyMedium) },
                    trailingIcon = if (d == settings.digitalDate) checkIcon else null,
                    onClick = { onSettingsChange(settings.copy(digitalDate = d)) },
                )
            }
            // ── 날짜색 ──
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            MenuSectionTitle(R.string.date_section_color)
            DigitalColor.entries.forEach { c ->
                DropdownMenuItem(
                    text = { Text(stringResource(c.label), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = { ColorSwatch(c) },
                    trailingIcon = if (dateOn && c == settings.digitalDateColor) checkIcon else null,
                    enabled = dateOn,
                    onClick = { onSettingsChange(settings.copy(digitalDateColor = c)) },
                )
            }
        }
    }
}

/** 메뉴 안 구역 제목 (작은 흐린 글자). [배경], [날짜], [화면], [시보·알람] 메뉴가 함께 씀. */
@Composable
internal fun MenuSectionTitle(@StringRes title: Int) {
    Text(
        stringResource(title),
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 6.dp, bottom = 2.dp),
    )
}

/** 검은 바탕 항목 표시용: 테두리 있는 검은 동그라미. */
@Composable
private fun ColorDot(color: Color) {
    Box(
        Modifier
            .size(18.dp)
            .background(color, CircleShape)
            .border(1.dp, MaterialTheme.colorScheme.outline, CircleShape),
    )
}

/** [달력...] 목록에 나오는 웹 페이지들 (이름, 아이콘, 주소). */
private enum class WebLink(@StringRes val label: Int, val icon: ImageVector, val url: String) {
    Calendar(R.string.calendar, Icons.Filled.CalendarMonth, "https://garpsukim.github.io/Calendar_Lunar_-_HTML/m_calendar.html"),
    Weather(R.string.weather, Icons.Filled.Cloud, "https://www.weather.go.kr/w/index.do"),
    Sunrise(R.string.sunrise_sunset, Icons.Filled.WbTwilight, "https://astro.kasi.re.kr"),
    WorldTime(R.string.world_time, Icons.Filled.Public, "https://vclock.kr/time"),
}

/**
 * [달력...] 버튼: 누르면 달력 / 날씨 / 일출일몰 / 세계시 목록을 보여 주고,
 * 고른 페이지를 기본 브라우저로 연다. 브라우저가 없으면 안내 메시지만 띄운다.
 */
@Composable
private fun CalendarButton() {
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(Icons.Filled.CalendarMonth, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.calendar_menu), style = MaterialTheme.typography.labelMedium)
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            WebLink.entries.forEach { link ->
                DropdownMenuItem(
                    text = { Text(stringResource(link.label), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = { Icon(link.icon, contentDescription = null, modifier = Modifier.size(18.dp)) },
                    onClick = {
                        expanded = false
                        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(link.url))
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        try {
                            context.startActivity(intent)
                        } catch (e: ActivityNotFoundException) {
                            Toast.makeText(context, R.string.calendar_not_found, Toast.LENGTH_SHORT).show()
                        }
                    },
                )
            }
        }
    }
}

/** 아날로그 / 하이브리드 모드: 애니메이션 + 큰 아날로그 시계 (+ 시계판 안 요일/날짜). */
@Composable
private fun AnalogClockArea(
    settings: ClockSettings,
    style: DialStyle,
    now: LocalTime,
    today: LocalDate,
) {
    Box(Modifier.fillMaxSize()) {
        // 시계 뒤 검은 바탕에 깔리는 애니메이션 (테마 색 사용)
        AnimationLayer(
            animation = settings.animation,
            palette = style.palette,
            dialPadding = DialPadding,
            frameMs = settings.powerMode.frameMs,
            modifier = Modifier.fillMaxSize(),
        )

        // 가로/세로 공통: 화면 짧은 쪽에 꽉 차는 시계를 중앙에
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(DialPadding),
            contentAlignment = Alignment.Center,
        ) {
            val dialSize = minOf(maxWidth, maxHeight)
            Box(modifier = Modifier.size(dialSize), contentAlignment = Alignment.Center) {
                DialFace(style, Modifier.fillMaxSize())
                DialNumberLayer(style, settings.dialNumbers, Modifier.fillMaxSize())
                if (settings.mode == DisplayMode.Hybrid) {
                    // 시계판 안쪽: 위에 요일(AM/PM), 아래에 년월일
                    val amPm = if (now.hour < 12) "AM" else "PM"
                    DialText(
                        text = "${today.format(dayOfWeekFormatter)} ($amPm)",
                        style = style,
                        dialSize = dialSize,
                        sizeRatio = 0.085f,
                        modifier = Modifier.offset(y = -dialSize * 0.22f),
                    )
                    DialText(
                        text = today.format(dateFormatter),
                        style = style,
                        dialSize = dialSize,
                        sizeRatio = 0.055f,
                        modifier = Modifier.offset(y = dialSize * 0.22f),
                    )
                }
                ClockHands(now, style, settings.showSeconds, Modifier.fillMaxSize())
            }
        }
    }
}

/** 작은 체크박스 + 글자 (글자까지 눌러도 토글). */
@Composable
private fun SmallCheck(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier
            .toggleable(value = checked, role = Role.Checkbox, onValueChange = onCheckedChange)
            .padding(horizontal = 4.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(
            checked = checked,
            onCheckedChange = null,
            modifier = Modifier.size(20.dp),
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(start = 6.dp),
        )
    }
}

/**
 * 시계판 숫자: 긴 눈금(3·6·9·12) 바로 안쪽 원 위에. 크기는 시계판에 비례, 색은 테마 글자색.
 * 하이브리드의 요일·날짜 글자(중심에서 위아래 0.44r)와 겹치지 않는 위치·크기.
 */
@Composable
private fun DialNumberLayer(style: DialStyle, numbers: DialNumbers, modifier: Modifier = Modifier) {
    if (numbers == DialNumbers.Off) return
    val measurer = rememberTextMeasurer()
    val hours = if (numbers == DialNumbers.Quarters) listOf(12, 3, 6, 9) else (1..12).toList()
    Canvas(modifier) {
        val radius = size.minDimension / 2f
        val fontPx = radius * 0.15f
        val textStyle = TextStyle(fontSize = fontPx.toSp(), fontWeight = FontWeight.Bold)
        // 눈금 바깥 끝(DialFace 와 같은 계산) → 긴 눈금 길이 → 조금 띄우고 → 글자 반 높이만큼 안쪽
        val tickOuter = radius - radius * 0.035f - radius * 0.025f
        val r = tickOuter - radius * 0.18f - radius * 0.04f - fontPx * 0.55f
        for (h in hours) {
            val layout = measurer.measure(h.toString(), textStyle)
            val c = center + polar(Math.toRadians(h * 30.0 - 90.0), r)
            drawText(
                textLayoutResult = layout,
                brush = style.textBrush,
                topLeft = Offset(c.x - layout.size.width / 2f, c.y - layout.size.height / 2f),
                alpha = 0.9f,
            )
        }
    }
}

/** 시계판 크기에 비례하는 글자 크기 (시스템 글꼴 크기 설정과 무관하게 판 안에 맞춤). */
@Composable
private fun DialText(
    text: String,
    style: DialStyle,
    dialSize: Dp,
    sizeRatio: Float,
    modifier: Modifier = Modifier,
) {
    val fontSize = with(LocalDensity.current) { (dialSize * sizeRatio).toSp() }
    Text(
        text = text,
        style = MaterialTheme.typography.titleMedium.copy(
            brush = style.textBrush,
            alpha = 0.75f,
            fontSize = fontSize,
            lineHeight = fontSize,
        ),
        maxLines = 1,
        modifier = modifier,
    )
}

/** 우상단의 작은 선택 드롭다운. */
@Composable
private fun <T> SmallMenu(
    selected: T,
    items: List<T>,
    label: @Composable (T) -> String,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    icon: (@Composable (T) -> Unit)? = null,
) {
    var expanded by remember { mutableStateOf(false) }
    Box(modifier) {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            if (icon != null) {
                icon(selected)
                Spacer(Modifier.width(6.dp))
            }
            Text(label(selected), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            items.forEach { t ->
                DropdownMenuItem(
                    text = { Text(label(t), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = icon?.let { { it(t) } },
                    onClick = {
                        onSelect(t)
                        expanded = false
                    },
                    trailingIcon = if (t == selected) {
                        { Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp)) }
                    } else null,
                )
            }
        }
    }
}

/** "닫힘: 무한", "닫힘: 30초", "닫힘: 90분", "닫힘: 12시간", "닫힘: 1일" … */
@Composable
private fun autoCloseLabel(a: AutoClose): String {
    val value = if (a == AutoClose.Off) {
        stringResource(R.string.auto_close_infinite)
    } else {
        stringResource(a.unit, a.amount)
    }
    return stringResource(R.string.auto_close_format, value)
}

/** 시계판 하나를 그리는 데 필요한 색 묶음. */
class DialStyle(
    val rim: Brush?,
    /** 애니메이션이 쓰는 테마 색 (0..1 위치 → 색). */
    val palette: (Float) -> Color,
    val tickColor: (index: Int) -> Color,
    // 바늘 색: 인자는 바늘이 가리키는 위치 (0..1, 12시 = 0, 시계방향)
    val hourHand: (position: Float) -> Color,
    val minuteHand: (position: Float) -> Color,
    val secondHand: (position: Float) -> Color,
    val pin: Color,
    val textBrush: Brush,
)

/** 0..1 위치를 무지개 색으로 (0 = 빨강, 12시 방향부터 시계방향). */
private fun rainbow(fraction: Float): Color =
    Color.hsv(((fraction % 1f) + 1f) % 1f * 360f, 0.85f, 0.95f)

@Composable
private fun dialStyle(theme: DialTheme): DialStyle {
    val cs = MaterialTheme.colorScheme
    return when (theme) {
        DialTheme.Default -> DialStyle(
            rim = null,
            palette = cyclic(listOf(cs.primary, cs.secondary, cs.tertiary)),
            tickColor = { cs.onSurfaceVariant },
            hourHand = { cs.primary },
            minuteHand = { cs.onSurface },
            secondHand = { cs.secondary },
            pin = cs.secondary,
            textBrush = SolidColor(cs.onSurface),
        )

        DialTheme.Rainbow -> {
            val spectrum = List(7) { rainbow(it / 6f) }
            DialStyle(
                rim = Brush.sweepGradient(spectrum),
                palette = ::rainbow,
                tickColor = { rainbow(it / 60f) },
                // 각 바늘은 가리키는 눈금과 같은 무지개 색
                hourHand = ::rainbow,
                minuteHand = ::rainbow,
                secondHand = ::rainbow,
                pin = Color.White,
                textBrush = Brush.horizontalGradient(spectrum),
            )
        }

        DialTheme.Neon -> {
            val cyan = Color(0xFF00E5FF)
            val magenta = Color(0xFFFF2BD6)
            val colors = listOf(cyan, magenta)
            DialStyle(
                rim = sweep(colors),
                palette = cyclic(colors),
                tickColor = { cyclic(colors)(it / 60f) },
                hourHand = { cyan },
                minuteHand = { magenta },
                secondHand = { Color(0xFFFFEA00) },
                pin = Color(0xFFFFEA00),
                textBrush = Brush.horizontalGradient(colors),
            )
        }

        DialTheme.Ocean -> gradientStyle(
            listOf(Color(0xFF00E5FF), Color(0xFF2979FF), Color(0xFF1DE9B6)),
        )

        DialTheme.Sunset -> gradientStyle(
            listOf(Color(0xFFFFAB40), Color(0xFFFF4081), Color(0xFF7C4DFF)),
        )

        DialTheme.Forest -> gradientStyle(
            listOf(Color(0xFFB2FF59), Color(0xFF00E676), Color(0xFFFFEE58)),
        )

        DialTheme.Blossom -> gradientStyle(
            listOf(Color(0xFFFFC1E3), Color(0xFFFF80AB), Color(0xFFFFFFFF)),
        )

        DialTheme.Gold -> monoStyle(
            main = Color(0xFFFFD54F),
            dim = Color(0xFF8D6E2A),
            accent = Color(0xFFFF7043),
            rimColors = listOf(Color(0xFFFFE082), Color(0xFFB8860B)),
        )

        DialTheme.Silver -> monoStyle(
            main = Color(0xFFF5F5F5),
            dim = Color(0xFF757575),
            accent = Color(0xFFFF5252),
            rimColors = listOf(Color(0xFFFFFFFF), Color(0xFF9E9E9E), Color(0xFFFFFFFF), Color(0xFF616161)),
        )

        DialTheme.Red -> monoStyle(
            main = Color(0xFFFF1744),
            dim = Color(0xFF7A0F22),
            accent = Color(0xFFFF8A80),
            rimColors = null,
        )
    }
}

/** 색 목록을 시계 한 바퀴에 고르게 펼쳐, 0..1 위치의 색을 돌려준다 (끝과 처음이 이어짐). */
private fun cyclic(colors: List<Color>): (Float) -> Color = { f ->
    val n = colors.size
    val x = (((f % 1f) + 1f) % 1f) * n
    val i = x.toInt() % n
    lerp(colors[i], colors[(i + 1) % n], x - x.toInt())
}

/** cyclic 과 같은 배치의 테두리 그라데이션 (12시 기준, DialFace 에서 -90도 회전). */
private fun sweep(colors: List<Color>): Brush = Brush.sweepGradient(colors + colors.first())

/** 그라데이션형: 눈금·바늘이 가리키는 위치의 색을 따른다. */
private fun gradientStyle(colors: List<Color>): DialStyle {
    val at = cyclic(colors)
    return DialStyle(
        rim = sweep(colors),
        palette = at,
        tickColor = { at(it / 60f) },
        hourHand = at,
        minuteHand = at,
        secondHand = at,
        pin = Color.White,
        textBrush = Brush.horizontalGradient(colors),
    )
}

/** 단색형: 시 눈금·바늘은 main, 분 눈금은 dim, 초침은 accent. */
private fun monoStyle(main: Color, dim: Color, accent: Color, rimColors: List<Color>?): DialStyle =
    DialStyle(
        rim = rimColors?.let(::sweep),
        palette = cyclic(listOf(main, accent)),
        tickColor = { if (it % 5 == 0) main else dim },
        hourHand = { main },
        minuteHand = { main },
        secondHand = { accent },
        pin = accent,
        textBrush = SolidColor(main),
    )

/** 시계판: 바탕, 테두리, 눈금. 크기에 비례해서 굵기를 정한다. */
@Composable
private fun DialFace(style: DialStyle, modifier: Modifier = Modifier) {
    Canvas(modifier) {
        val radius = size.minDimension / 2f
        val rimWidth = radius * 0.035f
        // 시계판 바탕은 칠하지 않음: 앱 바탕(검정)과 뒤의 애니메이션이 그대로 보이게

        // 테두리 (무지개는 12시가 빨강이 되도록 -90도 회전)
        style.rim?.let { rim ->
            rotate(-90f) {
                drawCircle(
                    brush = rim,
                    radius = radius - rimWidth / 2f,
                    style = Stroke(width = rimWidth),
                )
            }
        }

        // 눈금: 3·6·9·12시는 길게, 나머지 시 눈금은 조금 짧게, 분 눈금은 가장 짧게
        val outer = radius - rimWidth - radius * 0.025f
        for (i in 0 until 60) {
            val isHour = i % 5 == 0
            val isQuarter = i % 15 == 0
            val angle = Math.toRadians(i * 6.0 - 90.0)
            val length = when {
                isQuarter -> radius * 0.18f
                isHour -> radius * 0.11f
                else -> radius * 0.06f
            }
            val inner = outer - length
            drawLine(
                color = style.tickColor(i),
                start = center + polar(angle, inner),
                end = center + polar(angle, outer),
                strokeWidth = if (isHour) radius * 0.04f else radius * 0.016f,
                cap = StrokeCap.Round,
            )
        }
    }
}

/** 바늘: 디지털 글자 위에 그려지도록 별도 레이어. */
@Composable
private fun ClockHands(
    time: LocalTime,
    style: DialStyle,
    showSeconds: Boolean,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier) {
        val radius = size.minDimension / 2f
        val seconds = time.second + time.nano / 1_000_000_000.0
        val minutes = time.minute + seconds / 60.0
        val hours = (time.hour % 12) + minutes / 60.0

        drawHand(hours * 30.0, radius * 0.48f, radius * 0.04f, style.hourHand((hours / 12.0).toFloat()))
        drawHand(minutes * 6.0, radius * 0.68f, radius * 0.028f, style.minuteHand((minutes / 60.0).toFloat()))
        if (showSeconds) {
            drawHand(seconds * 6.0, radius * 0.76f, radius * 0.014f, style.secondHand((seconds / 60.0).toFloat()))
        }
        drawCircle(style.pin, radius * 0.035f)
    }
}

private fun polar(angleRad: Double, length: Float) =
    Offset((cos(angleRad) * length).toFloat(), (sin(angleRad) * length).toFloat())

private fun DrawScope.drawHand(degrees: Double, length: Float, width: Float, color: Color) {
    val angle = Math.toRadians(degrees - 90.0)
    drawLine(
        color = color,
        start = center,
        end = center + polar(angle, length),
        strokeWidth = width,
        cap = StrokeCap.Round,
    )
}
