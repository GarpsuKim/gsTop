package com.example.clock.ui

import android.Manifest
import android.annotation.SuppressLint
import android.app.TimePickerDialog
import android.bluetooth.BluetoothManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.AutoMode
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FlipToBack
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Speaker
import androidx.compose.material.icons.filled.Watch
import androidx.compose.material.icons.filled.Work
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.runtime.key
import com.example.clock.R
import com.example.clock.auto.AUTO_RUN_COUNT
import com.example.clock.auto.AutoRun
import com.example.clock.auto.AutoSettings
import com.example.clock.auto.AutoStore
import com.example.clock.auto.Place
import com.example.clock.auto.PlaceSetting
import com.example.clock.auto.RelaxKind
import com.example.clock.auto.WifiMonitor
import com.example.clock.auto.GreetingPlayer
import androidx.compose.foundation.Image
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import com.example.clock.auto.hasBluetoothPermission
import com.example.clock.chime.hour12

/** 출근 인사: 직장 공유기(MAC)는 3개까지 등록. */
private const val WORK_MAX_BSSIDS = 3

/** 자동 기능 창이 열려 있는지 (창은 시계 화면이 띄움). */
object AutoDialogUi {
    var open by mutableStateOf(false)
}

/** [시보·알람] 메뉴 오른쪽 값: 켜 둔 자동 기능 수 ("4개 켜짐") 또는 "끔". */
@Composable
fun autoValue(): String {
    val context = LocalContext.current
    val s = remember(AutoStore.version) { AutoStore.load(context) }
    val n = listOf(
        s.flipMute, s.watchBt.isNotBlank(), s.work.registered, s.home.registered, s.speakerBt.isNotBlank(),
    ).count { it } + s.runs.count { it.ready }
    return if (n == 0) stringResource(R.string.chime_state_off) else stringResource(R.string.auto_count_on, n)
}

/**
 * 시계 화면 왼쪽 위 '업무중' / 'Home' (등록한 Wi-Fi 에 연결돼 있는 동안).
 * 앱이 떠 있는 동안 Wi-Fi 연결을 지켜봄 (위치 권한이 없으면 공유기를 알 수 없어 안 보임).
 */
@Composable
fun rememberCurrentPlace(): Place? {
    val context = LocalContext.current
    val s = remember(AutoStore.version) { AutoStore.load(context) }
    var place by remember { mutableStateOf<Place?>(null) }
    DisposableEffect(s) {
        if (!s.work.registered && !s.home.registered) {
            place = null
            return@DisposableEffect onDispose { }
        }
        val cm = context.getSystemService(ConnectivityManager::class.java)
        fun refresh() {
            place = WifiMonitor.placeOf(s, WifiMonitor.currentBssid(context))
        }
        refresh()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = refresh()
            override fun onLost(network: Network) = refresh()
            override fun onCapabilitiesChanged(network: Network, nc: NetworkCapabilities) = refresh()
        }
        runCatching {
            cm?.registerNetworkCallback(NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build(), cb)
        }
        onDispose { runCatching { cm?.unregisterNetworkCallback(cb) } }
    }
    return place
}

/** 왼쪽 위 장소 표시. */
@Composable
fun PlaceBadge(place: Place, modifier: Modifier = Modifier) {
    Row(
        modifier
            .clip(RoundedCornerShape(50))
            .background(Color(0x66000000))
            .padding(horizontal = 10.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            if (place == Place.Work) Icons.Filled.Work else Icons.Filled.Home,
            contentDescription = null, tint = Color.White, modifier = Modifier.size(16.dp),
        )
        Spacer(Modifier.width(6.dp))
        Text(
            stringResource(if (place == Place.Work) R.string.auto_place_work else R.string.auto_place_home),
            color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold,
        )
    }
}

/** 화면 위쪽 안내 (분실 폰 찾기·출근·귀가 등). 누르면 닫힘. */
@Composable
fun AutoBanner(text: String, onDismiss: () -> Unit, modifier: Modifier = Modifier) {
    Text(
        text,
        color = Color.White,
        fontSize = 17.sp,
        fontWeight = FontWeight.Bold,
        modifier = modifier
            .padding(horizontal = 16.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(Color(0xFF1565C0))
            .clickable(onClick = onDismiss)
            .padding(horizontal = 18.dp, vertical = 12.dp),
    )
}

// ─────────────────────────────── 설정 창 ───────────────────────────────

/**
 * 자동 기능 설정 창 (전체 화면). 바꾸는 즉시 저장되고 감시·알람도 다시 맞춤.
 *  권한 · 뒤집으면 무음 · 이벤트 때 화면 켜기 · 분실 폰 찾기 · 출근 인사 · 귀가 인사 · relax 모드 · 자동 실행 1~5
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun AutoDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var s by remember { mutableStateOf(AutoStore.load(context)) }
    fun update(new: AutoSettings) {
        s = new
        AutoStore.save(context, new)
    }

    // 권한 상태 (설정 화면에서 돌아올 때마다 다시 읽음)
    var permTick by remember { mutableIntStateOf(0) }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    DisposableEffect(lifecycle) {
        val obs = LifecycleEventObserver { _, e -> if (e == Lifecycle.Event.ON_RESUME) permTick++ }
        lifecycle.addObserver(obs)
        onDispose { lifecycle.removeObserver(obs) }
    }

    // 고르기 창들
    var btPickFor by remember { mutableStateOf<String?>(null) }      // "watch" / "speaker"
    var appPickFor by remember { mutableStateOf<String?>(null) }     // "work" / "home" / "run0".."run4"
    var musicFor by remember { mutableStateOf<String?>(null) }       // "work" / "home" / "relax"
    // [들어보기] 로 틀던 음악은 설정 창을 닫으면 멈춤
    DisposableEffect(Unit) { onDispose { GreetingPlayer.stop() } }

    val pickOne = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val f = musicFor ?: return@rememberLauncherForActivityResult
        if (uri == null) return@rememberLauncherForActivityResult
        runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        val name = queryDisplayName(context, uri)
        when (f) {
            "work" -> update(s.copy(work = s.work.copy(musicUri = uri.toString(), musicName = name)))
            "home" -> update(s.copy(home = s.home.copy(musicUri = uri.toString(), musicName = name)))
        }
    }
    val pickMany = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult
        uris.forEach { runCatching { context.contentResolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION) } }
        val first = queryDisplayName(context, uris.first())
        val name = if (uris.size > 1) context.getString(R.string.auto_music_many, first, uris.size - 1) else first
        update(s.copy(relaxMusic = uris.map { it.toString() }, relaxMusicName = name, relaxKind = RelaxKind.Music))
    }
    val btPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        permTick++
        if (!ok) btPickFor = null
    }
    val locationPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { permTick++ }

    fun hasFineLocation() = ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
        PackageManager.PERMISSION_GRANTED
    fun hasBgLocation() = Build.VERSION.SDK_INT < Build.VERSION_CODES.Q ||
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_BACKGROUND_LOCATION) == PackageManager.PERMISSION_GRANTED

    /** 위치 권한: 먼저 "앱 사용 중", 그다음 "항상 허용" (Android 11+ 은 설정 화면으로 감). */
    fun askLocation() {
        if (!hasFineLocation()) locationPermission.launch(Manifest.permission.ACCESS_FINE_LOCATION)
        else if (!hasBgLocation() && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            locationPermission.launch(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
        }
    }

    fun openBtPicker(target: String) {
        if (!hasBluetoothPermission(context) && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            btPermission.launch(Manifest.permission.BLUETOOTH_CONNECT)
        }
        btPickFor = target
    }

    /** 지금 연결된 Wi-Fi 를 그 장소에 등록 (MAC 추가). */
    fun registerWifi(place: Place) {
        if (!hasFineLocation()) {
            askLocation()
            Toast.makeText(context, R.string.auto_need_location, Toast.LENGTH_LONG).show()
            return
        }
        val bssid = WifiMonitor.currentBssid(context)
        if (bssid == null) {
            Toast.makeText(context, R.string.auto_no_wifi, Toast.LENGTH_LONG).show()
            return
        }
        val ssid = WifiMonitor.currentSsid(context) ?: ""
        // 직장 공유기는 3개까지 (이미 등록한 것을 다시 누르면 그대로)
        if (place == Place.Work && bssid !in s.work.bssids && s.work.bssids.size >= WORK_MAX_BSSIDS) {
            Toast.makeText(context, context.getString(R.string.auto_wifi_full, WORK_MAX_BSSIDS), Toast.LENGTH_LONG).show()
            return
        }
        fun add(p: PlaceSetting) = p.copy(bssids = p.bssids + bssid, ssid = ssid.ifBlank { p.ssid })
        update(if (place == Place.Work) s.copy(work = add(s.work)) else s.copy(home = add(s.home)))
        Toast.makeText(context, context.getString(R.string.auto_wifi_added, ssid, bssid), Toast.LENGTH_SHORT).show()
        if (!hasBgLocation()) askLocation()
    }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
                // 위쪽 막대
                Row(Modifier.fillMaxWidth().padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = onDismiss) { Icon(Icons.Filled.Close, contentDescription = null) }
                    Icon(Icons.Filled.AutoMode, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.auto_title), fontSize = 20.sp, fontWeight = FontWeight.ExtraBold)
                }
                Column(
                    Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp, vertical = 4.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    // ── 권한 ── (설정에서 돌아오면 permTick 이 바뀌어 다시 확인)
                    key(permTick) {
                    Section(Icons.Filled.Security, R.string.auto_perm_title) {
                        PermRow(R.string.auto_perm_overlay, Settings.canDrawOverlays(context)) {
                            context.startActivity(
                                Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:${context.packageName}")),
                            )
                        }
                        PermRow(R.string.auto_perm_location, hasFineLocation() && hasBgLocation()) { askLocation() }
                        PermRow(R.string.auto_perm_bluetooth, hasBluetoothPermission(context)) {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) btPermission.launch(Manifest.permission.BLUETOOTH_CONNECT)
                        }
                        Hint(R.string.auto_perm_hint)
                    }
                    }

                    // ── 1) 뒤집으면 무음 · 2) 이벤트 때 화면 켜기 ──
                    Section(Icons.Filled.FlipToBack, R.string.auto_basic_title) {
                        SwitchRow(R.string.auto_flip_mute, R.string.auto_flip_mute_desc, s.flipMute) { update(s.copy(flipMute = it)) }
                        SwitchRow(R.string.auto_wake_event, R.string.auto_wake_event_desc, s.wakeOnEvent) { update(s.copy(wakeOnEvent = it)) }
                    }

                    // ── 3) 분실 폰 찾기 ──
                    Section(Icons.Filled.Watch, R.string.auto_watch_title) {
                        Hint(R.string.auto_watch_desc)
                        ValueRow(
                            R.string.auto_bt_device, s.watchBtName.ifBlank { stringResource(R.string.auto_none) },
                            onPick = { openBtPicker("watch") },
                            onClear = if (s.watchBt.isNotBlank()) ({ update(s.copy(watchBt = "", watchBtName = "")) }) else null,
                        )
                    }

                    // ── 4) 출근 인사 · 5) 귀가 인사 ──
                    PlaceSection(
                        icon = Icons.Filled.Work, title = R.string.auto_work_title, desc = R.string.auto_work_desc, p = s.work,
                        maxBssids = WORK_MAX_BSSIDS,
                        onRegisterWifi = { registerWifi(Place.Work) },
                        onChange = { update(s.copy(work = it)) },
                        onPickMusic = { musicFor = "work"; pickOne.launch(arrayOf("audio/*")) },
                        onPickApp = { appPickFor = "work" },
                    )
                    PlaceSection(
                        icon = Icons.Filled.Home, title = R.string.auto_home_title, desc = R.string.auto_home_desc, p = s.home,
                        onRegisterWifi = { registerWifi(Place.Home) },
                        onChange = { update(s.copy(home = it)) },
                        onPickMusic = { musicFor = "home"; pickOne.launch(arrayOf("audio/*")) },
                        onPickApp = { appPickFor = "home" },
                    )

                    // ── 6) relax 모드 ──
                    Section(Icons.Filled.Speaker, R.string.auto_relax_title) {
                        Hint(R.string.auto_relax_desc)
                        ValueRow(
                            R.string.auto_bt_speaker, s.speakerBtName.ifBlank { stringResource(R.string.auto_none) },
                            onPick = { openBtPicker("speaker") },
                            onClear = if (s.speakerBt.isNotBlank()) ({ update(s.copy(speakerBt = "", speakerBtName = "")) }) else null,
                        )
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            FilterChip(
                                selected = s.relaxKind == RelaxKind.Music,
                                onClick = { update(s.copy(relaxKind = RelaxKind.Music)) },
                                label = { Text(stringResource(R.string.auto_relax_music)) },
                            )
                            FilterChip(
                                selected = s.relaxKind == RelaxKind.YouTube,
                                onClick = { update(s.copy(relaxKind = RelaxKind.YouTube)) },
                                label = { Text(stringResource(R.string.auto_relax_youtube)) },
                            )
                        }
                        if (s.relaxKind == RelaxKind.Music) {
                            ValueRow(
                                R.string.auto_music, s.relaxMusicName.ifBlank { stringResource(R.string.auto_none) },
                                onPick = { pickMany.launch(arrayOf("audio/*")) },
                                onClear = if (s.relaxMusic.isNotEmpty()) ({ update(s.copy(relaxMusic = emptyList(), relaxMusicName = "")) }) else null,
                            )
                            // 여러 곡이면 첫 곡으로 들어보기
                            MusicTestButton(s.relaxMusic.firstOrNull().orEmpty())
                        } else {
                            var url by remember(s.relaxYoutube) { mutableStateOf(s.relaxYoutube) }
                            OutlinedTextField(
                                value = url,
                                onValueChange = { url = it; update(s.copy(relaxYoutube = it.trim())) },
                                label = { Text(stringResource(R.string.auto_youtube_url)) },
                                singleLine = true,
                                modifier = Modifier.fillMaxWidth(),
                            )
                        }
                    }

                    // ── 7) 자동 실행 ──
                    Section(Icons.Filled.Schedule, R.string.auto_run_title) {
                        Hint(R.string.auto_run_desc)
                        s.runs.forEachIndexed { i, r ->
                            if (i > 0) HorizontalDivider(Modifier.padding(vertical = 4.dp))
                            RunRow(
                                index = i, r = r,
                                onChange = { nr -> update(s.copy(runs = s.runs.toMutableList().also { it[i] = nr })) },
                                onPickApp = { appPickFor = "run$i" },
                            )
                        }
                    }
                    Spacer(Modifier.height(24.dp))
                }
            }
        }
    }

    // 블루투스 기기 고르기 (페어링된 기기 목록)
    btPickFor?.let { target ->
        key(permTick) {
        BluetoothPicker(
            onPick = { address, name ->
                update(if (target == "watch") s.copy(watchBt = address, watchBtName = name) else s.copy(speakerBt = address, speakerBtName = name))
                btPickFor = null
            },
            onDismiss = { btPickFor = null },
        )
        }
    }
    // 앱 고르기
    appPickFor?.let { target ->
        AppPicker(
            onPick = { component, label ->
                when {
                    target == "work" -> update(s.copy(work = s.work.copy(app = component, appLabel = label)))
                    target == "home" -> update(s.copy(home = s.home.copy(app = component, appLabel = label)))
                    target.startsWith("run") -> {
                        val i = target.removePrefix("run").toInt()
                        update(s.copy(runs = s.runs.toMutableList().also { it[i] = it[i].copy(app = component, appLabel = label, isApp = true) }))
                    }
                }
                appPickFor = null
            },
            onDismiss = { appPickFor = null },
        )
    }
}

@Composable
private fun Section(icon: ImageVector, @StringRes title: Int, content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant)
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(22.dp))
            Spacer(Modifier.width(8.dp))
            Text(stringResource(title), fontSize = 17.sp, fontWeight = FontWeight.ExtraBold)
        }
        content()
    }
}

@Composable
private fun Hint(@StringRes text: Int) {
    Text(stringResource(text), fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun PermRow(@StringRes label: Int, granted: Boolean, onFix: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            (if (granted) "✅ " else "⚠️ ") + stringResource(label),
            modifier = Modifier.weight(1f), fontSize = 15.sp,
        )
        if (!granted) OutlinedButton(onClick = onFix) { Text(stringResource(R.string.auto_perm_fix)) }
    }
}

@Composable
private fun SwitchRow(@StringRes label: Int, @StringRes desc: Int, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(stringResource(label), fontSize = 16.sp, fontWeight = FontWeight.Bold)
            Text(stringResource(desc), fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

/** "이름: 값  [고르기] [지우기]". */
@Composable
private fun ValueRow(
    @StringRes label: Int,
    value: String,
    onPick: () -> Unit,
    onClear: (() -> Unit)?,
    icon: ImageBitmap? = null, // 앱이면 그 앱 아이콘
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) {
            Image(icon, contentDescription = null, modifier = Modifier.size(36.dp))
            Spacer(Modifier.width(10.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(stringResource(label), fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(value, fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 2)
        }
        TextButton(onClick = onPick) { Text(stringResource(R.string.auto_pick)) }
        if (onClear != null) TextButton(onClick = onClear) { Text(stringResource(R.string.auto_clear)) }
    }
}

@Composable
private fun PlaceSection(
    icon: ImageVector,
    @StringRes title: Int,
    @StringRes desc: Int,
    p: PlaceSetting,
    onRegisterWifi: () -> Unit,
    onChange: (PlaceSetting) -> Unit,
    onPickMusic: () -> Unit,
    onPickApp: () -> Unit,
    maxBssids: Int? = null,
) {
    Section(icon, title) {
        Hint(desc)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    stringResource(R.string.auto_wifi) + (maxBssids?.let { " (${p.bssids.size}/$it)" } ?: ""),
                    fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    if (p.registered) stringResource(R.string.auto_wifi_value, p.ssid.ifBlank { "Wi-Fi" }, p.bssids.size)
                    else stringResource(R.string.auto_none),
                    fontSize = 16.sp, fontWeight = FontWeight.Bold,
                )
            }
            if (p.registered) TextButton(onClick = { onChange(p.copy(bssids = emptySet(), ssid = "")) }) {
                Text(stringResource(R.string.auto_clear_all))
            }
        }
        // 등록한 공유기 MAC 하나씩 (✕ 로 그것만 지우기)
        p.bssids.sorted().forEach { b ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("· $b", fontSize = 14.sp, modifier = Modifier.weight(1f))
                IconButton(onClick = {
                    val left = p.bssids - b
                    onChange(p.copy(bssids = left, ssid = if (left.isEmpty()) "" else p.ssid))
                }) {
                    Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.auto_clear), modifier = Modifier.size(18.dp))
                }
            }
        }
        val full = maxBssids != null && p.bssids.size >= maxBssids
        OutlinedButton(onClick = onRegisterWifi, enabled = !full, modifier = Modifier.fillMaxWidth()) {
            Text(
                if (full) stringResource(R.string.auto_wifi_full, maxBssids!!)
                else stringResource(R.string.auto_wifi_register),
            )
        }
        ValueRow(
            R.string.auto_greeting_music, p.musicName.ifBlank { stringResource(R.string.auto_none) },
            onPick = onPickMusic,
            onClear = if (p.musicUri.isNotBlank()) ({ onChange(p.copy(musicUri = "", musicName = "")) }) else null,
        )
        MusicTestButton(p.musicUri)
        ValueRow(
            R.string.auto_open_app, p.appLabel.ifBlank { stringResource(R.string.auto_none) },
            onPick = onPickApp,
            onClear = if (p.app.isNotBlank()) ({ onChange(p.copy(app = "", appLabel = "")) }) else null,
            icon = rememberAppIcon(p.app),
        )
    }
}

/**
 * 음악 [▶ 들어보기] / [■ 멈추기]: 등록한 음악을 한 번 틀어 봄 (실제 인사·relax 때와 같은 재생기).
 * 음악을 아직 고르지 않았으면 눌리지 않음.
 */
@Composable
private fun MusicTestButton(uri: String) {
    val context = LocalContext.current
    val playing = uri.isNotBlank() && GreetingPlayer.playingUri == uri
    OutlinedButton(
        onClick = { if (playing) GreetingPlayer.stop() else GreetingPlayer.play(context, uri) },
        enabled = uri.isNotBlank(),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Icon(if (playing) Icons.Filled.Stop else Icons.Filled.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(6.dp))
        Text(stringResource(if (playing) R.string.auto_test_stop else R.string.auto_test_play))
    }
}

/** 자동 실행 한 칸: [스위치] 오전 9:00  (인터넷 | 앱)  주소 또는 앱. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RunRow(index: Int, r: AutoRun, onChange: (AutoRun) -> Unit, onPickApp: () -> Unit) {
    val context = LocalContext.current
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("${index + 1}", fontSize = 15.sp, fontWeight = FontWeight.ExtraBold, modifier = Modifier.width(22.dp))
            val h = r.minutes / 60
            Text(
                stringResource(if (h < 12) R.string.med_am_time else R.string.med_pm_time, hour12(h), r.minutes % 60),
                fontSize = 18.sp, fontWeight = FontWeight.Bold,
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .clickable {
                        TimePickerDialog(context, { _, hh, mm -> onChange(r.copy(minutes = hh * 60 + mm)) }, h, r.minutes % 60, false).show()
                    }
                    .padding(horizontal = 6.dp, vertical = 4.dp)
                    .weight(1f),
            )
            Switch(checked = r.enabled, onCheckedChange = { onChange(r.copy(enabled = it)) })
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = !r.isApp, onClick = { onChange(r.copy(isApp = false)) }, label = { Text(stringResource(R.string.auto_run_url)) })
            FilterChip(selected = r.isApp, onClick = { onChange(r.copy(isApp = true)) }, label = { Text(stringResource(R.string.auto_run_app)) })
        }
        if (r.isApp) {
            ValueRow(
                R.string.auto_open_app, r.appLabel.ifBlank { stringResource(R.string.auto_none) },
                onPick = onPickApp, onClear = null,
                icon = rememberAppIcon(r.app),
            )
        } else {
            var url by remember(r.url) { mutableStateOf(r.url) }
            OutlinedTextField(
                value = url,
                onValueChange = { url = it; onChange(r.copy(url = it.trim())) },
                label = { Text(stringResource(R.string.auto_run_url_hint)) },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

/** 페어링된 블루투스 기기 목록에서 하나 고르기. */
@SuppressLint("MissingPermission")
@Composable
private fun BluetoothPicker(onPick: (address: String, name: String) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val devices = remember(hasBluetoothPermission(context)) {
        if (!hasBluetoothPermission(context)) emptyList()
        else runCatching {
            context.getSystemService(BluetoothManager::class.java)?.adapter?.bondedDevices.orEmpty()
                .map { it.address to (it.name ?: it.address) }
                .sortedBy { it.second.lowercase() }
        }.getOrDefault(emptyList())
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.auto_bt_pick_title)) },
        text = {
            if (devices.isEmpty()) {
                Text(stringResource(if (hasBluetoothPermission(context)) R.string.auto_bt_none else R.string.auto_bt_no_permission))
            } else {
                LazyColumn(Modifier.heightIn(max = 400.dp)) {
                    items(devices) { (address, name) ->
                        Column(
                            Modifier.fillMaxWidth().clickable { onPick(address, name) }.padding(vertical = 10.dp),
                        ) {
                            Text(name, fontSize = 16.sp, fontWeight = FontWeight.Bold)
                            Text(address, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.med_cancel)) } },
    )
}

/** 설치된 앱 (홈 화면 앱 서랍) 하나 고르기. */
@Composable
private fun AppPicker(onPick: (component: String, label: String) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    // 앱 목록 + 아이콘은 시간이 걸리므로 화면 밖 스레드에서 읽음 (그동안 빈 목록)
    val apps by produceState(initialValue = emptyList<PickApp>()) {
        value = withContext(Dispatchers.IO) { launcherApps(context) }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Filled.Apps, contentDescription = null) },
        title = { Text(stringResource(R.string.auto_app_pick_title)) },
        text = {
            LazyColumn(Modifier.heightIn(max = 460.dp)) {
                items(apps, key = { it.component }) { app ->
                    Row(
                        Modifier.fillMaxWidth().clickable { onPick(app.component, app.label) }.padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        if (app.icon != null) Image(app.icon, contentDescription = null, modifier = Modifier.size(40.dp))
                        else Spacer(Modifier.size(40.dp))
                        Spacer(Modifier.width(12.dp))
                        Text(app.label, fontSize = 16.sp)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.med_cancel)) } },
    )
}

private class PickApp(val component: String, val label: String, val icon: ImageBitmap?)

private fun launcherApps(context: Context): List<PickApp> {
    val pm = context.packageManager
    val px = (40 * context.resources.displayMetrics.density).toInt()
    return pm.queryIntentActivities(Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER), 0)
        .filter { it.activityInfo.packageName != context.packageName }
        .map {
            PickApp(
                component = ComponentName(it.activityInfo.packageName, it.activityInfo.name).flattenToString(),
                label = it.loadLabel(pm).toString(),
                icon = runCatching { it.loadIcon(pm).toBitmap(px, px).asImageBitmap() }.getOrNull(),
            )
        }
        .sortedBy { it.label.lowercase() }
}

/** 등록한 앱의 아이콘 (없거나 지워진 앱이면 null). */
@Composable
private fun rememberAppIcon(component: String): ImageBitmap? {
    val context = LocalContext.current
    val icon by produceState<ImageBitmap?>(initialValue = null, component) {
        value = if (component.isBlank()) null else withContext(Dispatchers.IO) {
            val cn = ComponentName.unflattenFromString(component) ?: return@withContext null
            val px = (36 * context.resources.displayMetrics.density).toInt()
            runCatching { context.packageManager.getActivityIcon(cn).toBitmap(px, px).asImageBitmap() }.getOrNull()
        }
    }
    return icon
}
