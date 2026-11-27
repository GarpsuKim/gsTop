package com.example.clock.ui

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.example.clock.R
import kotlinx.coroutines.delay

// ─────────────────────────────── 수면등 ───────────────────────────────

/** 수면등 색. 순서 = 저장값(ordinal). */
enum class NightColor(@StringRes val label: Int, val color: Color) {
    Pink(R.string.night_color_pink, Color(0xFFFFB6C1)),     // 밝은 핑크 (기본)
    Amber(R.string.night_color_amber, Color(0xFFFF7A1A)),   // 따뜻한 주황 (잠을 덜 방해)
    Candle(R.string.night_color_candle, Color(0xFFFFB347)), // 촛불
    Red(R.string.night_color_red, Color(0xFFD32F2F)),       // 빨강 (가장 덜 눈부심)
    Yellow(R.string.night_color_yellow, Color(0xFFFFE082)),
    White(R.string.night_color_white, Color(0xFFFFFFFF)),
    Sky(R.string.night_color_sky, Color(0xFF81D4FA)),
}

/**
 * 수면등 밝기. [screen]: 화면 밝기 (0~1, 이 앱이 떠 있는 동안만), [alpha]: 색의 진하기 (검정 위에).
 * 아주 어둡게도 폰마다 최저 밝기가 달라서 색을 함께 어둡게 함.
 */
enum class NightBrightness(@StringRes val label: Int, val screen: Float, val alpha: Float) {
    VeryDim(R.string.night_bright_very_dim, 0.01f, 0.35f),
    Dim(R.string.night_bright_dim, 0.05f, 0.6f),
    Normal(R.string.night_bright_normal, 0.25f, 0.9f),
    Bright(R.string.night_bright_bright, 0.8f, 1f),
}

/** 수면등 자동 꺼짐 (분, 0 = 계속). */
enum class NightOffTimer(val minutes: Int) { Never(0), M10(10), M30(30), M60(60) }

/** 수면등이 켜져 있는지 (앱 전체에 하나, 화면 회전에도 유지). */
object NightLight {
    var on by mutableStateOf(false)
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

/**
 * 수면등 빛: 시계 **밑**(배경 자리)에 까는 단색 빛. 시계·버튼은 그 위에 그대로 보임.
 * 사진·동영상·유튜브 배경보다 위에 그려서 덮음. 화면 밝기도 맞추고(끄면 원래대로), 꺼짐 시간이 지나면 끔.
 */
@Composable
fun NightLightBackground(
    color: NightColor,
    brightness: NightBrightness,
    offTimer: NightOffTimer,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val activity = LocalContext.current.findActivity()
    // 화면 밝기: 수면등이 켜져 있는 동안만, 끄면 시스템 밝기로 되돌림
    DisposableEffect(activity, brightness) {
        val w = activity?.window
        w?.attributes = w?.attributes?.apply { screenBrightness = brightness.screen }
        onDispose {
            w?.attributes = w?.attributes?.apply {
                screenBrightness = android.view.WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
            }
        }
    }
    // 자동 꺼짐 (꺼짐 시간을 바꾸면 그때부터 다시 셈)
    LaunchedEffect(offTimer) {
        if (offTimer.minutes > 0) {
            delay(offTimer.minutes * 60_000L)
            onExit()
        }
    }
    Box(
        modifier
            .fillMaxSize()
            .background(Color.Black)
            .background(color.color.copy(alpha = brightness.alpha)),
    )
}

/**
 * 수면등 조절 막대 (맨 위에 [수면등 끄기], 색·밝기·자동 꺼짐).
 * 시계 화면의 버튼이 보일 때만 함께 나옴 (화면을 누르면 나오고, 버튼이 숨으면 같이 숨음).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun NightLightPanel(
    color: NightColor,
    brightness: NightBrightness,
    offTimer: NightOffTimer,
    onChange: (NightColor, NightBrightness, NightOffTimer) -> Unit,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .padding(16.dp)
            .widthIn(max = 520.dp)
            .background(Color.Black.copy(alpha = 0.7f), RoundedCornerShape(16.dp))
            // 막대 안의 빈 곳을 눌러도 뒤의 시계 화면이 눌려 버튼이 숨지 않게
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
            .verticalScroll(rememberScrollState()) // 가로 화면처럼 높이가 모자라면 막대 안에서 스크롤
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        val label = MaterialTheme.typography.labelMedium
        // 맨 위: 제목 + [수면등 끄기] (가로 화면에서도 늘 보이게)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.night_light), style = MaterialTheme.typography.titleSmall, color = Color.White)
            Spacer(Modifier.weight(1f))
            TextButton(onClick = onExit) {
                Text(stringResource(R.string.night_light_off))
            }
        }
        // 색: 동그라미 (지금 색에 흰 테두리)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            NightColor.entries.forEach { c ->
                Box(
                    Modifier
                        .size(36.dp)
                        .background(c.color, CircleShape)
                        .border(if (c == color) 3.dp else 1.dp, if (c == color) Color.White else Color.Gray, CircleShape)
                        .clickable { onChange(c, brightness, offTimer) },
                )
            }
        }
        Text(stringResource(R.string.night_brightness), style = label, color = Color.LightGray)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NightBrightness.entries.forEach { b ->
                FilterChip(
                    selected = b == brightness,
                    onClick = { onChange(color, b, offTimer) },
                    label = { Text(stringResource(b.label)) },
                )
            }
        }
        Text(stringResource(R.string.night_off_timer), style = label, color = Color.LightGray)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            NightOffTimer.entries.forEach { t ->
                FilterChip(
                    selected = t == offTimer,
                    onClick = { onChange(color, brightness, t) },
                    label = {
                        Text(
                            if (t.minutes == 0) stringResource(R.string.night_timer_never)
                            else stringResource(R.string.sleep_minutes, t.minutes),
                        )
                    },
                )
            }
        }
    }
}

// ─────────────────────────────── 손전등 ───────────────────────────────

/**
 * 폰 뒤 카메라 플래시(LED)를 손전등으로 켜고 끔. 카메라 권한이 필요 없음 (CameraManager.setTorchMode).
 * 빠른 설정 등 다른 곳에서 켜고 끈 것도 [on] 에 반영 (TorchCallback).
 */
object Flashlight {
    var on by mutableStateOf(false)
        private set

    private var cameraId: String? = null
    private var callbackRegistered = false

    private fun manager(context: Context) = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    /** 플래시가 있는 뒤쪽 카메라 (없으면 null). */
    private fun findCamera(context: Context): String? {
        cameraId?.let { return it }
        val cm = manager(context)
        val id = runCatching {
            cm.cameraIdList.firstOrNull { id ->
                val c = cm.getCameraCharacteristics(id)
                c.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true &&
                    c.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
            }
        }.getOrNull()
        cameraId = id
        if (id != null && !callbackRegistered) {
            callbackRegistered = true
            cm.registerTorchCallback(object : CameraManager.TorchCallback() {
                override fun onTorchModeChanged(camera: String, enabled: Boolean) {
                    if (camera == id) on = enabled
                }
            }, Handler(Looper.getMainLooper()))
        }
        return id
    }

    fun toggle(context: Context) = set(context, !on)

    /** 앱을 나갈 때 (배터리·발열을 생각해 끔). */
    fun turnOff(context: Context) {
        if (on) set(context, false)
    }

    private fun set(context: Context, value: Boolean) {
        val app = context.applicationContext
        val id = findCamera(app)
        if (id == null) {
            Toast.makeText(app, R.string.flashlight_none, Toast.LENGTH_SHORT).show()
            return
        }
        try {
            manager(app).setTorchMode(id, value)
            on = value
        } catch (e: Exception) { // 다른 앱이 카메라를 쓰는 중 등
            Toast.makeText(app, R.string.flashlight_failed, Toast.LENGTH_SHORT).show()
        }
    }
}
