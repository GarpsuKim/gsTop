package com.example.clock.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/*
 * 화면 "안"에 그리는 드롭다운 메뉴.
 *
 * Material3 DropdownMenu 는 메뉴를 별도의 팝업 창으로 띄우는데, 이 폰(갤럭시)에서는
 * 팝업 창이 보이기만 하고 터치는 뒤쪽 시계 화면으로 배달되어 항목을 고를 수 없는 문제가 있었다.
 * 그래서 메뉴를 같은 화면(창) 안의 맨 위 레이어에 그린다. 메뉴 바깥을 누르면 닫힌다.
 *
 * 사용법: 기존 DropdownMenu(expanded, onDismissRequest) { ... } 를
 *        InlineDropdown(expanded, onDismissRequest) { ... } 로 바꾸고 (버튼과 같은 Box 안),
 *        화면 최상단 Box 의 맨 마지막에 InlineMenuLayer(host) 를 둔다.
 */

/** 지금 열려 있는 메뉴 하나를 관리. */
class InlineMenuHost {
    internal class Entry(
        val token: Any,
        val anchor: Rect,
        val onDismiss: () -> Unit,
        val content: @Composable ColumnScope.() -> Unit,
    )

    internal var current by mutableStateOf<Entry?>(null)

    internal fun show(entry: Entry) {
        val c = current
        if (c == null || c.token !== entry.token || c.anchor != entry.anchor) current = entry
    }

    internal fun hide(token: Any) {
        if (current?.token === token) current = null
    }
}

val LocalInlineMenuHost = staticCompositionLocalOf<InlineMenuHost?> { null }

/** 버튼과 같은 Box 안에 둔다. Box 의 크기·위치(=버튼)를 기준으로 메뉴를 띄운다. */
@Composable
fun BoxScope.InlineDropdown(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    content: @Composable ColumnScope.() -> Unit,
) {
    val host = LocalInlineMenuHost.current ?: return
    var anchor by remember { mutableStateOf(Rect.Zero) }
    val latestDismiss by rememberUpdatedState(onDismissRequest)
    val latestContent by rememberUpdatedState(content)
    val token = remember { Any() }
    // 메뉴 내용은 항상 최신 값(체크 표시 등)을 그리도록 State 를 읽는 고정 람다로 넘김
    val stableContent: @Composable ColumnScope.() -> Unit = remember { { latestContent() } }
    val stableDismiss: () -> Unit = remember { { latestDismiss() } }

    // 버튼 영역 측정용 (화면에는 아무것도 그리지 않음)
    Box(Modifier.matchParentSize().onGloballyPositioned { anchor = it.boundsInRoot() })

    if (expanded) {
        SideEffect { host.show(InlineMenuHost.Entry(token, anchor, stableDismiss, stableContent)) }
        DisposableEffect(Unit) { onDispose { host.hide(token) } }
    }
}

/**
 * 열린 메뉴를 그리는 맨 위 레이어. 화면 최상단 Box 의 마지막 자식으로 둔다.
 * 버튼 아래 공간이 넉넉하면 아래로, 아니면 위로 펼친다. 너무 길면 메뉴 안에서 스크롤.
 */
@Composable
fun InlineMenuLayer(host: InlineMenuHost, modifier: Modifier = Modifier) {
    val entry = host.current ?: return
    var origin by remember { mutableStateOf(Offset.Zero) }

    // [뒤로] 버튼: 열린 메뉴만 닫음 (같은 화면 안에 그린 메뉴라, 이게 없으면 [뒤로] 가 앱을 닫아 버림)
    BackHandler { entry.onDismiss() }

    Box(
        modifier
            .fillMaxSize()
            .onGloballyPositioned { origin = it.positionInRoot() }
            // 메뉴 바깥을 누르면 닫기 (뒤쪽 화면으로 터치가 가지 않음)
            .pointerInput(entry.token) { detectTapGestures { entry.onDismiss() } },
    ) {
        val anchor = entry.anchor.translate(-origin)
        Layout(
            content = {
                Surface(
                    shape = MaterialTheme.shapes.extraSmall,
                    color = MaterialTheme.colorScheme.surfaceContainer,
                    tonalElevation = 3.dp,
                    shadowElevation = 3.dp,
                    // 메뉴 안을 누른 것은 바깥 누름(닫기)으로 처리되지 않게
                    modifier = Modifier.pointerInput(Unit) { detectTapGestures { } },
                ) {
                    Column(
                        Modifier
                            .width(IntrinsicSize.Max)
                            .verticalScroll(rememberScrollState())
                            .padding(vertical = 8.dp),
                        content = entry.content,
                    )
                }
            },
        ) { measurables, constraints ->
            val margin = 8.dp.roundToPx()
            val placeable = measurables.first().measure(
                constraints.copy(
                    minWidth = 0,
                    minHeight = 0,
                    maxHeight = (constraints.maxHeight - 2 * margin).coerceAtLeast(0),
                ),
            )
            layout(constraints.maxWidth, constraints.maxHeight) {
                val maxW = constraints.maxWidth
                val maxH = constraints.maxHeight
                val below = maxH - anchor.bottom
                val above = anchor.top
                val y = if (below >= placeable.height || below >= above) {
                    anchor.bottom
                } else {
                    anchor.top - placeable.height
                }
                val x = anchor.left
                placeable.place(
                    x.roundToInt().coerceIn(margin, (maxW - placeable.width - margin).coerceAtLeast(margin)),
                    y.roundToInt().coerceIn(margin, (maxH - placeable.height - margin).coerceAtLeast(margin)),
                )
            }
        }
    }
}
