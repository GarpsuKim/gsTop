package com.example.clock.ui

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * 2단계 메뉴의 영역 하나.
 * [title]: 영역 이름. [icon]: 이름 앞 아이콘. [value]: 첫 팝업에서 이름 오른쪽에 보일 지금 값 (없으면 null).
 * [items]: 영역 팝업에 나올 항목들. [close] 를 부르면 팝업이 닫힘.
 * [action]: 항목이 하나뿐인 영역 (예: [시보 설정], [알람 열기]) — 영역 팝업 없이 누르면 바로 실행.
 *           있으면 [items] 는 쓰지 않음.
 */
class MenuSection(
    @StringRes val title: Int,
    val icon: ImageVector? = null,
    val value: (@Composable () -> String)? = null,
    val action: (() -> Unit)? = null,
    val items: @Composable (close: () -> Unit) -> Unit = {},
)

/**
 * 영역이 여러 개인 메뉴 버튼 (2단계):
 *  1. 버튼을 누르면 → 영역 이름만 있는 팝업 (오른쪽에 지금 값과 ›)
 *  2. 영역을 누르면 → 그 영역의 항목만 있는 팝업 (맨 위 [← 영역 이름] = 첫 팝업으로 돌아가기)
 *     (항목이 하나뿐인 영역은 [MenuSection.action] 으로 바로 실행)
 * 한 팝업에 여러 영역의 항목을 한꺼번에 늘어놓지 않음.
 * [뒤로] 버튼·팝업 바깥 누르기 = 열린 팝업 닫기.
 * [button]: 버튼 안 내용 (아이콘 + 글자). 끝에 ▾ 는 자동으로 붙음.
 */
@Composable
fun SectionedMenuButton(
    sections: List<MenuSection>,
    button: @Composable RowScope.() -> Unit,
) {
    var listOpen by remember { mutableStateOf(false) }
    var openSection by remember { mutableIntStateOf(-1) } // -1 = 영역 팝업 닫힘

    Box {
        TextButton(
            onClick = { listOpen = true },
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        ) {
            button()
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp))
        }

        // 1단계: 영역 이름만
        InlineDropdown(expanded = listOpen, onDismissRequest = { listOpen = false }) {
            sections.forEachIndexed { i, s ->
                DropdownMenuItem(
                    text = { Text(stringResource(s.title), style = MaterialTheme.typography.bodyMedium) },
                    leadingIcon = s.icon?.let { icon -> { Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp)) } },
                    trailingIcon = {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            s.value?.let { value ->
                                Text(
                                    value(),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.primary,
                                )
                                Spacer(Modifier.width(4.dp))
                            }
                            Icon(
                                Icons.AutoMirrored.Filled.KeyboardArrowRight,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                            )
                        }
                    },
                    onClick = {
                        listOpen = false
                        val action = s.action
                        if (action != null) action() else openSection = i
                    },
                )
            }
        }

        // 2단계: 고른 영역의 항목만
        InlineDropdown(expanded = openSection >= 0, onDismissRequest = { openSection = -1 }) {
            val s = sections.getOrNull(openSection) ?: return@InlineDropdown
            // 맨 위: [← 영역 이름] → 영역 목록으로 돌아가기
            DropdownMenuItem(
                text = { Text(stringResource(s.title), style = MaterialTheme.typography.titleSmall) },
                leadingIcon = {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null, modifier = Modifier.size(18.dp))
                },
                onClick = {
                    openSection = -1
                    listOpen = true
                },
            )
            HorizontalDivider(Modifier.padding(bottom = 4.dp))
            s.items { openSection = -1 }
        }
    }
}
