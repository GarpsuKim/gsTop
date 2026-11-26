package com.example.clock.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Wallpaper
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.layout.layout
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.example.clock.R
import kotlin.math.roundToInt

/**
 * 검은 배경 투명도 슬라이더. [horizontal] = false 면 세로(위로 올릴수록 투명),
 * true 면 가로(오른쪽으로 갈수록 투명). 투명할수록 폰 배경화면이 비쳐 보임.
 * [value]: 0 = 완전 검정, 1 = 완전 투명.
 */
@Composable
fun TransparencySlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    horizontal: Boolean = false,
    /** 슬라이더 막대 길이 (자리가 좁으면 줄여서 넘김). */
    length: Dp = SliderDefaultLength,
) {
    val icon = @Composable {
        Icon(
            Icons.Filled.Wallpaper,
            contentDescription = stringResource(R.string.transparency),
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(18.dp),
        )
    }
    val percent = @Composable {
        Text(
            "${(value * 100).roundToInt()}%",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.primary,
        )
    }

    if (horizontal) {
        // 가로: [아이콘] ──●── 50%
        Row(
            modifier = modifier.padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            icon()
            Slider(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier.width(length), // 가로 길이
            )
            percent()
        }
    } else {
        Column(
            modifier = modifier.padding(vertical = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            icon()
            Slider(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier
                    .height(length) // 세로 길이
                    .vertical(),
            )
            percent()
        }
    }
}

/**
 * 가로 컴포넌트를 반시계 방향으로 90도 돌려 세로로 세운다 (왼쪽 끝 → 아래, 오른쪽 끝 → 위).
 * 바깥에서 받은 높이를 슬라이더 길이로 쓰고, 터치 좌표도 함께 돌아가서 위로 끌면 값이 커진다.
 */
private fun Modifier.vertical(): Modifier = layout { measurable, constraints ->
    val placeable = measurable.measure(
        Constraints(
            minWidth = constraints.minHeight,
            maxWidth = constraints.maxHeight,
            minHeight = constraints.minWidth,
            maxHeight = constraints.maxWidth,
        ),
    )
    // 돌린 뒤 크기: 폭 = 원래 높이, 높이 = 원래 폭
    layout(placeable.height, placeable.width) {
        placeable.placeWithLayer(0, placeable.width) {
            rotationZ = -90f
            transformOrigin = TransformOrigin(0f, 0f)
        }
    }
}

/** 슬라이더 막대 기본 길이. */
val SliderDefaultLength = 220.dp

/** 막대 말고 아이콘·여백·"50%" 글자가 차지하는 길이 (막대 방향). 자리가 좁을 때 막대 길이 계산용. */
val SliderExtraLength = 70.dp

/** 자리가 아주 좁을 때도 이만큼은 남기는 막대 길이. */
val SliderMinLength = 60.dp

/** 가로 슬라이더 한 줄의 높이 (세로 화면에서 시계 아래 빈 칸에 들어가는지 판단). */
val SliderRowHeight = 48.dp
