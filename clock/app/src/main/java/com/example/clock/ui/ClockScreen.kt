package com.example.clock.ui

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import androidx.compose.ui.platform.LocalConfiguration
import android.provider.AlarmClock
import android.widget.Toast
import androidx.compose.material.icons.filled.Alarm
import androidx.compose.material.icons.filled.CalendarMonth
import android.net.Uri
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.RestartAlt
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
import androidx.compose.runtime.rememberUpdatedState
import kotlinx.coroutines.launch
import androidx.compose.ui.platform.LocalContext
import androidx.annotation.StringRes
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.runtime.saveable.rememberSaveable
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
data class ClockSettings(
    val theme: DialTheme = DialTheme.Default,
    val animation: DialAnimation = DialAnimation.Off,
    val mode: DisplayMode = DisplayMode.Hybrid,
    val showSeconds: Boolean = true,
    val digitalColor: DigitalColor = DigitalColor.White,
    val digitalBold: Boolean = false,
    val digitalNeon: Boolean = false,
    val digitalScroll: DigitalScroll = DigitalScroll.Off,
    /** 0 = 검은 바탕, 그 외 = 배경 사진 있음 (바뀔 때마다 새 값 → 다시 불러옴). */
    val backgroundVersion: Long = 0L,
    /** 유튜브 배경 주소 ("" = 꺼짐). 켜져 있으면 사진 배경보다 우선. */
    val youtubeUrl: String = "",
    val youtubeSound: Boolean = false,
    /** 배경(동영상) 목록 (비어 있으면 꺼짐). 우선순위: 유튜브 > 동영상 > 이미지. */
    val videoUris: List<String> = emptyList(),
    val videoSound: Boolean = false,
    /** true = 목록 전체 무한 반복, false = 한 번씩만 재생하고 마지막 화면에서 멈춤. */
    val videoRepeat: Boolean = true,
    /** 이미지 슬라이드쇼: true = 무한 반복, false = 한 번씩만 보여주고 마지막 사진에서 멈춤. */
    val imageRepeat: Boolean = true,
    /** 고급 모드(기본) / 전원 절약 모드. */
    val powerMode: PowerMode = PowerMode.Advanced,
    /** 검은 배경 투명도 (0 = 완전 검정, 1 = 완전 투명 → 폰 배경화면이 보임). 검은 배경일 때만 적용. */
    val blackTransparency: Float = 0f,
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
    // 예전 "디지털 체크박스" 값이 있으면 그걸로 모드를 정함 (켜짐=디지털, 꺼짐=아날로그)
    val legacyMode = if (p.getBoolean(KEY_SHOW_DIGITAL, true)) DisplayMode.Hybrid else DisplayMode.Analog
    return ClockSettings(
        theme = DialTheme.entries.getOrElse(p.getInt(KEY_DIAL_THEME, 0)) { DialTheme.Default },
        animation = DialAnimation.entries.getOrElse(p.getInt(KEY_DIAL_ANIMATION, 0)) { DialAnimation.Off },
        mode = DisplayMode.entries.getOrElse(p.getInt(KEY_DISPLAY_MODE, legacyMode.ordinal)) { legacyMode },
        showSeconds = p.getBoolean(KEY_SHOW_SECONDS, true),
        digitalColor = DigitalColor.entries.getOrElse(p.getInt(KEY_DIGITAL_COLOR, 0)) { DigitalColor.White },
        digitalBold = p.getBoolean(KEY_DIGITAL_BOLD, false),
        digitalNeon = p.getBoolean(KEY_DIGITAL_NEON, false),
        digitalScroll = DigitalScroll.entries.getOrElse(p.getInt(KEY_DIGITAL_SCROLL, 0)) { DigitalScroll.Off },
        backgroundVersion = p.getLong(KEY_BACKGROUND_VERSION, 0L),
        youtubeUrl = p.getString(KEY_YOUTUBE_URL, "").orEmpty(),
        youtubeSound = p.getBoolean(KEY_YOUTUBE_SOUND, false),
        videoUris = p.getString(KEY_VIDEO_URIS, "").orEmpty().lines().filter { it.isNotBlank() },
        videoSound = p.getBoolean(KEY_VIDEO_SOUND, false),
        videoRepeat = p.getBoolean(KEY_VIDEO_REPEAT, true),
        imageRepeat = p.getBoolean(KEY_IMAGE_REPEAT, true),
        powerMode = PowerMode.entries.getOrElse(p.getInt(KEY_POWER_MODE, 0)) { PowerMode.Advanced },
        blackTransparency = p.getFloat(KEY_BLACK_TRANSPARENCY, 0f).coerceIn(0f, 1f),
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
    }
}

private val DialPadding = 16.dp

@OptIn(ExperimentalLayoutApi::class)
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
) {
    val theme = settings.theme
    val animation = settings.animation
    var now by remember { mutableStateOf(LocalTime.now()) }
    var today by remember { mutableStateOf(LocalDate.now()) }

    // 고급 모드: 0.05초마다(1초에 20번) 갱신 → 초침이 부드럽게 흐름.
    // 전원 절약 모드: 1초에 한 번, 초가 바뀌는 순간에 맞춰 갱신 → 초침 "똑딱", CPU 크게 절약.
    LaunchedEffect(settings.powerMode) {
        val saving = settings.powerMode == PowerMode.Saving
        while (true) {
            now = if (saving) LocalTime.now().withNano(0) else LocalTime.now()
            today = LocalDate.now()
            delay(if (saving) 1_000L - System.currentTimeMillis() % 1_000L else 50L)
        }
    }

    val style = dialStyle(theme)

    val digitalOnly = settings.mode == DisplayMode.Digital

    // 화면을 누를 때마다 버튼 보이기/숨기기 (회전해도 유지)
    var controlsVisible by rememberSaveable { mutableStateOf(true) }
    // 유튜브 주소를 공유받으면 버튼이 숨겨져 있어도 유튜브 창이 보이도록 다시 표시
    LaunchedEffect(incomingYouTube) {
        if (incomingYouTube != null) controlsVisible = true
    }

    val menuHost = remember { InlineMenuHost() }
    // 버튼을 숨기면 열려 있던 메뉴도 함께 사라지도록 (숨겨진 버튼의 메뉴가 남지 않게)
    LaunchedEffect(controlsVisible) { if (!controlsVisible) menuHost.current = null }

    CompositionLocalProvider(LocalInlineMenuHost provides menuHost) {
    Box(
        modifier.clickable(
            interactionSource = remember { MutableInteractionSource() },
            indication = null, // 누를 때 물결 효과 없이
        ) { controlsVisible = !controlsVisible },
    ) {
        if (digitalOnly) {
            // 디지털: 다른 것은 모두 지우고 큰 시각만
            BigDigitalClock(
                time = now,
                color = settings.digitalColor,
                bold = settings.digitalBold,
                neon = settings.digitalNeon,
                scroll = settings.digitalScroll,
                powerMode = settings.powerMode,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(DialPadding),
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
                } else {
                    SmallCheck(
                        label = stringResource(R.string.show_seconds),
                        checked = settings.showSeconds,
                        onCheckedChange = { onSettingsChange(settings.copy(showSeconds = it)) },
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
                ResetButton(onReset = { onSettingsChange(ClockSettings()) })
            }
        }

        // 버튼 묶음 B (우하단): 아날로그/하이브리드 → [애니메이션 ▾] [테마 ▾],  디지털 → [스크롤 ▾] [글자색 ▾]  + [🖼 배경(이미지) ▾] [🎬 배경(동영상) ▾] [▶ 유튜브] [♪ 음악] [📅 달력] [⏰ 알람]
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
                // [🖼 배경 ▾] → 사진 선택 / 검은색 (모든 모드 공통)
                BackgroundMenu(settings, onSettingsChange)
                // [🎬 배경(동영상) ▾] → 동영상 여러 개 연속 재생 (모든 모드 공통)
                VideoBackgroundMenu(settings, onSettingsChange)
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
                // [♪ 음악] → 시계 앱이 직접 재생 (모든 모드 공통)
                if (music != null) MusicButton(music)
                // [📅 달력] → 기본 브라우저로 음력 달력 페이지 (모든 모드 공통)
                CalendarButton()
                // 마지막: [⏰ 알람] → 시스템 시계 앱의 알람 화면 (모든 모드 공통)
                AlarmButton()
            }
        }

        // 버튼 배치 (가로·세로 공통): A 는 좌상단, B 는 우하단.
        // 가로 화면은 넓어서 각각 1줄, 세로 화면은 폭이 모자라면 자동으로 2줄로 나뉨.
        AnimatedVisibility(
            visible = controlsVisible,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(Alignment.TopStart),
        ) {
            Box(Modifier.padding(top = 4.dp, start = 8.dp, end = 8.dp)) { modeControls() }
        }
        AnimatedVisibility(
            visible = controlsVisible,
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
            visible = controlsVisible && settings.isBlackBackground,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(if (landscape) Alignment.TopCenter else Alignment.CenterEnd),
        ) {
            TransparencySlider(
                value = settings.blackTransparency,
                onValueChange = { onSettingsChange(settings.copy(blackTransparency = it)) },
                modifier = if (landscape) Modifier.padding(top = 4.dp) else Modifier.padding(end = 4.dp),
                horizontal = landscape,
            )
        }

        // 열린 목록 메뉴는 별도 창이 아니라 이 화면 맨 위에 그림 (터치가 뒤로 새지 않도록)
        InlineMenuLayer(menuHost)
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

/** 배경: 사진 한 장을 골라 검은 바탕 대신 쓰거나, 다시 검은색으로. */
@Composable
private fun BackgroundMenu(settings: ClockSettings, onSettingsChange: (ClockSettings) -> Unit) {
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

    Box {
        TextButton(
            onClick = { expanded = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            Icon(Icons.Filled.Wallpaper, contentDescription = null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.background), style = MaterialTheme.typography.labelMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }
        InlineDropdown(expanded = expanded, onDismissRequest = { expanded = false }) {
            val hasImage = settings.backgroundVersion != 0L
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
        }
    }
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

/** 달력 페이지 주소. */
private const val CALENDAR_URL = "https://garpsukim.github.io/Calendar_Lunar_-_HTML/m_calendar.html"

/** 기본 브라우저로 달력 페이지를 연다. 브라우저가 없으면 안내 메시지만 띄운다. */
@Composable
private fun CalendarButton() {
    val context = LocalContext.current
    TextButton(
        onClick = {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(CALENDAR_URL))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            try {
                context.startActivity(intent)
            } catch (e: ActivityNotFoundException) {
                Toast.makeText(context, R.string.calendar_not_found, Toast.LENGTH_SHORT).show()
            }
        },
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
    ) {
        Icon(Icons.Filled.CalendarMonth, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(Modifier.width(4.dp))
        Text(stringResource(R.string.calendar), style = MaterialTheme.typography.labelMedium)
    }
}

/** 시스템 시계 앱의 알람 목록을 연다. 처리할 앱이 없으면 안내 메시지만 띄운다. */
@Composable
private fun AlarmButton(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    TextButton(
        onClick = {
            val intent = Intent(AlarmClock.ACTION_SHOW_ALARMS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            try {
                context.startActivity(intent)
            } catch (e: ActivityNotFoundException) {
                Toast.makeText(context, R.string.alarm_not_found, Toast.LENGTH_SHORT).show()
            }
        },
        modifier = modifier,
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
    ) {
        Icon(Icons.Filled.Alarm, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(Modifier.width(4.dp))
        Text(stringResource(R.string.alarm), style = MaterialTheme.typography.labelMedium)
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
