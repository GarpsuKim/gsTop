package com.example.clock.auto

import android.Manifest
import android.app.AlarmManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.example.clock.MainActivity
import com.example.clock.R
import org.json.JSONArray
import org.json.JSONObject
import java.time.LocalDate
import java.time.ZonedDateTime
import java.time.temporal.ChronoUnit

/*
 * 자동 기능 ([시보·알람] → 자동 기능)
 *  1) 뒤집으면 무음        : 폰을 엎으면 이 앱의 소리(시보·약 알림·음악·백색 소음·동영상·유튜브·인사 음악)를 끔
 *  2) 이벤트 때 화면 켜기   : 시보·약 먹을 시간 등이 오면 시계 화면을 켬 (그 뒤 화면 꺼짐은 시스템 설정대로)
 *  3) 분실 폰 찾기            : 등록한 손목시계(블루투스)가 연결되거나 끊기면 알람음 + 시계 화면
 *  4) 출근 인사 / 5) 귀가 인사 : 등록한 Wi-Fi 공유기(MAC)에 연결되면 왼쪽 위 '업무중' / 'Home', 인사 음악, 앱 실행, 시계 화면
 *  6) relax 모드           : 등록한 스피커(블루투스)가 연결되면 [배경] 음악 또는 유튜브 + 시계 화면
 *  7) 자동 실행 (최대 5개)  : 매일 정한 시각에 인터넷 주소(기본 브라우저) 또는 앱을 엶
 */

// ─────────────────────────────── 설정 ───────────────────────────────

/** 출근·귀가 장소 하나: Wi-Fi 공유기 MAC 들(2.4/5GHz 따로일 수 있어 여러 개), 인사 음악, 실행할 앱. */
data class PlaceSetting(
    val bssids: Set<String> = emptySet(),
    val ssid: String = "",
    val musicUri: String = "",
    val musicName: String = "",
    val app: String = "",       // ComponentName.flattenToString()
    val appLabel: String = "",
) {
    val registered get() = bssids.isNotEmpty()
}

enum class RelaxKind { Music, YouTube }

/** 자동 실행 한 칸 (매일 [minutes] 에 인터넷 주소 또는 앱). */
data class AutoRun(
    val enabled: Boolean = false,
    val minutes: Int = 9 * 60,
    val isApp: Boolean = false,
    val url: String = "",
    val app: String = "",
    val appLabel: String = "",
) {
    val ready get() = enabled && (if (isApp) app.isNotBlank() else url.isNotBlank())
}

const val AUTO_RUN_COUNT = 5

data class AutoSettings(
    val flipMute: Boolean = false,
    val wakeOnEvent: Boolean = true,
    val watchBt: String = "",
    val watchBtName: String = "",
    val work: PlaceSetting = PlaceSetting(),
    val home: PlaceSetting = PlaceSetting(),
    val speakerBt: String = "",
    val speakerBtName: String = "",
    val relaxKind: RelaxKind = RelaxKind.Music,
    val relaxMusic: List<String> = emptyList(),
    val relaxMusicName: String = "",
    val relaxYoutube: String = "",
    val runs: List<AutoRun> = List(AUTO_RUN_COUNT) { AutoRun() },
)

/** 자동 기능 설정 저장 (SharedPreferences "auto", JSON 한 덩어리). */
object AutoStore {
    private const val PREFS = "auto"

    internal fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** 설정이 바뀔 때마다 늘어남 → 화면이 다시 읽음. */
    var version by mutableIntStateOf(0)
        private set

    fun load(context: Context): AutoSettings {
        val raw = prefs(context).getString("json", null) ?: return AutoSettings()
        return runCatching { fromJson(JSONObject(raw)) }.getOrElse { AutoSettings() }
    }

    /** 저장하고 Wi-Fi 감시·자동 실행 알람도 다시 맞춤. */
    fun save(context: Context, s: AutoSettings) {
        prefs(context).edit().putString("json", toJson(s).toString()).apply()
        version++
        WifiMonitor.register(context)
        AutoRunScheduler.update(context)
    }

    private fun placeJson(p: PlaceSetting) = JSONObject()
        .put("bssids", JSONArray(p.bssids.toList()))
        .put("ssid", p.ssid).put("musicUri", p.musicUri).put("musicName", p.musicName)
        .put("app", p.app).put("appLabel", p.appLabel)

    private fun place(o: JSONObject?): PlaceSetting {
        if (o == null) return PlaceSetting()
        val arr = o.optJSONArray("bssids")
        return PlaceSetting(
            bssids = (0 until (arr?.length() ?: 0)).map { arr!!.getString(it) }.toSet(),
            ssid = o.optString("ssid"), musicUri = o.optString("musicUri"), musicName = o.optString("musicName"),
            app = o.optString("app"), appLabel = o.optString("appLabel"),
        )
    }

    private fun toJson(s: AutoSettings) = JSONObject()
        .put("flipMute", s.flipMute).put("wakeOnEvent", s.wakeOnEvent)
        .put("watchBt", s.watchBt).put("watchBtName", s.watchBtName)
        .put("work", placeJson(s.work)).put("home", placeJson(s.home))
        .put("speakerBt", s.speakerBt).put("speakerBtName", s.speakerBtName)
        .put("relaxKind", s.relaxKind.name).put("relaxMusic", JSONArray(s.relaxMusic))
        .put("relaxMusicName", s.relaxMusicName).put("relaxYoutube", s.relaxYoutube)
        .put(
            "runs",
            JSONArray(
                s.runs.map {
                    JSONObject().put("enabled", it.enabled).put("minutes", it.minutes).put("isApp", it.isApp)
                        .put("url", it.url).put("app", it.app).put("appLabel", it.appLabel)
                },
            ),
        )

    private fun fromJson(o: JSONObject): AutoSettings {
        val music = o.optJSONArray("relaxMusic")
        val runs = o.optJSONArray("runs")
        return AutoSettings(
            flipMute = o.optBoolean("flipMute", false),
            wakeOnEvent = o.optBoolean("wakeOnEvent", true),
            watchBt = o.optString("watchBt"), watchBtName = o.optString("watchBtName"),
            work = place(o.optJSONObject("work")), home = place(o.optJSONObject("home")),
            speakerBt = o.optString("speakerBt"), speakerBtName = o.optString("speakerBtName"),
            relaxKind = runCatching { RelaxKind.valueOf(o.optString("relaxKind")) }.getOrDefault(RelaxKind.Music),
            relaxMusic = (0 until (music?.length() ?: 0)).map { music!!.getString(it) },
            relaxMusicName = o.optString("relaxMusicName"),
            relaxYoutube = o.optString("relaxYoutube"),
            runs = List(AUTO_RUN_COUNT) { i ->
                val r = runs?.optJSONObject(i) ?: return@List AutoRun()
                AutoRun(
                    enabled = r.optBoolean("enabled"), minutes = r.optInt("minutes", 9 * 60), isApp = r.optBoolean("isApp"),
                    url = r.optString("url"), app = r.optString("app"), appLabel = r.optString("appLabel"),
                )
            },
        )
    }
}

// ─────────────────────────────── 이벤트 → 시계 화면 ───────────────────────────────

/** 시계 화면을 띄우는 이유 (MainActivity 가 받아서 처리). */
object AutoEvent {
    const val EXTRA = "auto_event"
    const val CHIME = "chime"
    const val MEDICINE = "medicine"
    const val WATCH_ON = "watch_on"
    const val WATCH_OFF = "watch_off"
    const val WORK = "work"
    const val HOME = "home"
    const val RELAX = "relax"
}

/** 화면에 보여 줄 것 (MainActivity·시계 화면이 읽음). */
object AutoUi {
    /** 처리할 이벤트 (MainActivity 가 꺼내 쓰고 null 로). */
    var pending by mutableStateOf<String?>(null)

    /** 화면 위쪽 안내 (누르면 닫힘). */
    var banner by mutableStateOf<String?>(null)
}

object ClockWaker {
    private const val TAG = "ClockWaker"
    private const val CHANNEL_ID = "auto_events"

    /** 화면 켜기: 잠깐 켜고 놓아 줌 (ON_AFTER_RELEASE → 그 뒤 화면 꺼짐은 시스템 설정대로). */
    @Suppress("DEPRECATION")
    fun wakeScreen(context: Context) {
        val pm = context.getSystemService(PowerManager::class.java) ?: return
        if (pm.isInteractive) return
        runCatching {
            pm.newWakeLock(
                PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP or PowerManager.ON_AFTER_RELEASE,
                "clock:event",
            ).acquire(3_000L)
        }
    }

    /**
     * 이벤트로 시계 화면을 켜고 앞으로 가져옴.
     * [다른 앱 위에 표시] 권한이 있으면 바로 열고, 없으면 전체 화면 알림으로 (화면이 꺼져 있을 때 열림).
     * [AutoSettings.wakeOnEvent] 를 끈 경우 시보·약 이벤트는 화면을 켜지 않음.
     */
    fun bring(context: Context, event: String) {
        val s = AutoStore.load(context)
        if (event == AutoEvent.CHIME || event == AutoEvent.MEDICINE) {
            if (!s.wakeOnEvent) return
            // 시보·약은 "화면 켜기": 이미 화면을 보고 있으면(다른 앱을 쓰는 중 등) 끼어들지 않음 (약은 알림이 따로 뜸)
            if (context.getSystemService(PowerManager::class.java)?.isInteractive == true) return
        }
        Log.i(TAG, "이벤트 $event: 화면 ${if (context.getSystemService(PowerManager::class.java)?.isInteractive == true) "켜져 있음" else "꺼져 있음 → 켬"}, 다른 앱 위에 표시=${Settings.canDrawOverlays(context)}")
        wakeScreen(context)
        val intent = Intent(context, MainActivity::class.java)
            .putExtra(AutoEvent.EXTRA, event)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        if (Settings.canDrawOverlays(context)) {
            try {
                context.startActivity(intent)
                return
            } catch (e: Exception) {
                Log.w(TAG, "시계 화면 열기 실패 → 알림으로", e)
            }
        }
        notifyFullScreen(context, eventTitle(context, event), PendingIntent.getActivity(
            context, 51, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        ))
    }

    /** 다른 화면(브라우저·앱)을 열기. 권한이 있으면 바로, 없으면 알림 (누르면 열림). */
    fun launch(context: Context, target: Intent, title: String) {
        wakeScreen(context)
        target.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (Settings.canDrawOverlays(context)) {
            try {
                context.startActivity(target)
                return
            } catch (e: Exception) {
                Log.w(TAG, "열기 실패 → 알림으로", e)
            }
        }
        notifyFullScreen(context, title, PendingIntent.getActivity(
            context, 52 + (title.hashCode() and 0xFF), target, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        ))
    }

    private fun notifyFullScreen(context: Context, title: String, pi: PendingIntent) {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, context.getString(R.string.auto_channel), NotificationManager.IMPORTANCE_HIGH)
                    .apply { setSound(null, null) },
            )
        }
        val n = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(title)
            .setContentText(context.getString(R.string.auto_tap_to_open))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setFullScreenIntent(pi, true)
            .setContentIntent(pi)
            .setAutoCancel(true)
            .setTimeoutAfter(5 * 60_000L)
            .build()
        try {
            nm.notify(9100 + (title.hashCode() and 0xFF), n)
        } catch (_: SecurityException) {
        }
    }

    fun eventTitle(context: Context, event: String): String = context.getString(
        when (event) {
            AutoEvent.WATCH_ON -> R.string.auto_watch_connected
            AutoEvent.WATCH_OFF -> R.string.auto_watch_disconnected
            AutoEvent.WORK -> R.string.auto_work_hello
            AutoEvent.HOME -> R.string.auto_home_hello
            AutoEvent.RELAX -> R.string.auto_relax_hello
            AutoEvent.MEDICINE -> R.string.med_now_title
            else -> R.string.app_name
        },
    )
}

// ─────────────────────────────── 소리 ───────────────────────────────

/** 분실 폰 찾기 알람음: 알람 소리를 반복 (최대 60초, 화면을 만지거나 뒤집으면 멈춤). */
object AlarmTone {
    private var player: MediaPlayer? = null
    private val handler = Handler(Looper.getMainLooper())

    fun start(context: Context) {
        stop()
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
            ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE) ?: return
        player = runCatching {
            MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build())
                setDataSource(context.applicationContext, uri)
                isLooping = true
                prepare()
                start()
            }
        }.getOrNull()
        handler.postDelayed({ stop() }, 60_000L)
    }

    fun stop() {
        handler.removeCallbacksAndMessages(null)
        player?.runCatching { stop(); release() }
        player = null
    }
}

/** 출근·귀가 인사 음악: 한 번만 재생. 앱을 나가도 끝까지 (뒤집으면 멈춤). */
object GreetingPlayer {
    private var player: MediaPlayer? = null

    /** 지금 나오는 음악 (설정 창의 [들어보기] ↔ [멈추기] 표시용, null = 안 나옴). */
    var playingUri by mutableStateOf<String?>(null)
        private set

    fun play(context: Context, uri: String) {
        stop()
        if (uri.isBlank()) return
        playingUri = uri
        player = runCatching {
            MediaPlayer().apply {
                setAudioAttributes(
                    AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
                )
                setDataSource(context.applicationContext, Uri.parse(uri))
                setOnCompletionListener { stop() }
                setOnPreparedListener { it.start() }
                setOnErrorListener { _, _, _ -> stop(); true } // 파일이 지워졌거나 권한이 없어진 경우 등
                prepareAsync()
            }
        }.getOrNull()
        if (player == null) playingUri = null
    }

    fun stop() {
        player?.runCatching { stop(); release() }
        player = null
        playingUri = null
    }
}

// ─────────────────────────────── 뒤집기 감지 ───────────────────────────────

/**
 * 폰을 엎었는지 감지 (가속도 센서). 화면이 바닥을 향한 채 0.8초 → [onFaceDown] 한 번.
 * 다시 세우면(화면이 위를 향하면) 다음에 또 알림.
 */
class FaceDownDetector(context: Context, private val onFaceDown: () -> Unit) : SensorEventListener {
    private val sm = context.getSystemService(SensorManager::class.java)
    private var since = 0L
    private var fired = false

    fun start() {
        val acc = sm?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) ?: return
        sm.registerListener(this, acc, SensorManager.SENSOR_DELAY_UI)
    }

    fun stop() {
        sm?.unregisterListener(this)
    }

    override fun onSensorChanged(e: SensorEvent) {
        val (x, y, z) = Triple(e.values[0], e.values[1], e.values[2])
        val down = z < -8.0f && kotlin.math.abs(x) < 3f && kotlin.math.abs(y) < 3f
        val now = SystemClock.elapsedRealtime()
        when {
            down && !fired -> {
                if (since == 0L) since = now
                if (now - since > 800L) {
                    fired = true
                    onFaceDown()
                }
            }
            !down -> {
                since = 0L
                if (z > 3f) fired = false
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
}

// ─────────────────────────────── 블루투스 (분실 폰 찾기 · relax) ───────────────────────────────

/**
 * 블루투스 기기 연결·끊김 (시스템 방송). 등록한 손목시계 → 분실 폰 찾기, 등록한 스피커 연결 → relax 모드.
 * 같은 기기가 잠깐 끊겼다 붙는 경우가 많아 15초 안의 같은 알림은 한 번만.
 */
class BtReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // 디버그 빌드 전용 시험: adb 로 "이 주소가 연결/끊김" 을 흉내 (시스템 방송은 adb 로 보낼 수 없어서)
        //   adb shell am broadcast -n <앱>/com.example.clock.auto.BtReceiver -a com.example.clock.debug.BT_TEST --es address AA:BB:.. --ez connected true
        val testing = com.example.clock.BuildConfig.DEBUG && intent.action == DEBUG_ACTION
        // 시험: --ei delay 초 → 앱이 그만큼 뒤에 스스로 같은 시험을 보냄 (그 사이 화면을 꺼서 "화면 켜기" 를 시험)
        if (testing && intent.getIntExtra("delay", 0) > 0) {
            val again = Intent(intent).setComponent(ComponentName(context, BtReceiver::class.java)).putExtra("delay", 0)
            val pi = PendingIntent.getBroadcast(context, 61, again, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
            context.getSystemService(AlarmManager::class.java)?.setExactAndAllowWhileIdle(
                AlarmManager.RTC_WAKEUP, System.currentTimeMillis() + intent.getIntExtra("delay", 0) * 1000L, pi,
            )
            Log.i("ClockWaker", "시험 예약: ${intent.getIntExtra("delay", 0)}초 뒤")
            return
        }
        val connected = when {
            testing -> intent.getBooleanExtra("connected", true)
            intent.action == BluetoothDevice.ACTION_ACL_CONNECTED -> true
            intent.action == BluetoothDevice.ACTION_ACL_DISCONNECTED -> false
            else -> return
        }
        val address = if (testing) {
            intent.getStringExtra("address") ?: return
        } else {
            @Suppress("DEPRECATION")
            val device: BluetoothDevice = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE) ?: return
            device.address ?: return
        }
        val s = AutoStore.load(context)
        val event = when {
            address.equals(s.watchBt, ignoreCase = true) -> if (connected) AutoEvent.WATCH_ON else AutoEvent.WATCH_OFF
            address.equals(s.speakerBt, ignoreCase = true) && connected -> AutoEvent.RELAX
            else -> return
        }
        val p = AutoStore.prefs(context)
        val key = "bt_last_$event"
        val now = System.currentTimeMillis()
        if (now - p.getLong(key, 0L) < 15_000L) return
        p.edit().putLong(key, now).apply()
        ClockWaker.bring(context, event)
    }

    companion object {
        const val DEBUG_ACTION = "com.example.clock.debug.BT_TEST"
    }
}

/** 블루투스 권한 (Android 12+ 는 BLUETOOTH_CONNECT). */
fun hasBluetoothPermission(context: Context): Boolean =
    Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

// ─────────────────────────────── Wi-Fi (출근 · 귀가) ───────────────────────────────

enum class Place { Work, Home }

object WifiMonitor {
    private fun pendingIntent(context: Context) = PendingIntent.getBroadcast(
        context, 31, Intent(context, WifiReceiver::class.java),
        // 시스템이 연결 정보를 덧붙이므로 바뀔 수 있어야 함
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
    )

    /** 출근·귀가 Wi-Fi 를 하나라도 등록했으면 Wi-Fi 연결을 백그라운드로 감시 (앱이 꺼져 있어도). */
    fun register(context: Context) {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return
        val pi = pendingIntent(context)
        runCatching { cm.unregisterNetworkCallback(pi) }
        val s = AutoStore.load(context)
        if (!s.work.registered && !s.home.registered) return
        val request = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        runCatching { cm.registerNetworkCallback(request, pi) }
            .onFailure { Log.w("WifiMonitor", "등록 실패", it) }
    }

    /** 지금 연결된 Wi-Fi 공유기 MAC (소문자). 위치 권한·위치 켜짐이 필요, 모르면 null. */
    @Suppress("DEPRECATION")
    fun currentBssid(context: Context): String? {
        val wm = context.applicationContext.getSystemService(WifiManager::class.java) ?: return null
        val b = wm.connectionInfo?.bssid?.lowercase() ?: return null
        return b.takeIf { it != "02:00:00:00:00:00" && it != "00:00:00:00:00:00" }
    }

    /** 지금 연결된 Wi-Fi 이름 (따옴표 없이). */
    @Suppress("DEPRECATION")
    fun currentSsid(context: Context): String? {
        val wm = context.applicationContext.getSystemService(WifiManager::class.java) ?: return null
        val s = wm.connectionInfo?.ssid?.trim('"') ?: return null
        return s.takeIf { it.isNotBlank() && it != "<unknown ssid>" }
    }

    fun placeOf(s: AutoSettings, bssid: String?): Place? = when {
        bssid == null -> null
        bssid in s.work.bssids -> Place.Work
        bssid in s.home.bssids -> Place.Home
        else -> null
    }
}

/**
 * Wi-Fi 연결 알림 (시스템이 보냄). 등록한 장소에 들어오면 인사.
 * 같은 장소는 하루 한 번만 (Wi-Fi 가 끊겼다 붙어도 다시 인사하지 않게). 장소가 바뀌면 그날 다시.
 */
class WifiReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val s = AutoStore.load(context)
        val place = WifiMonitor.placeOf(s, WifiMonitor.currentBssid(context)) ?: return
        val p = AutoStore.prefs(context)
        val today = LocalDate.now().toEpochDay()
        if (p.getString("last_place", null) == place.name && p.getLong("last_place_day", -1L) == today) return
        val event = if (place == Place.Work) AutoEvent.WORK else AutoEvent.HOME
        // "인사할 것이 남아 있음" 표시: 시계가 화면에 나오면(바로 열리든, 나중에 사용자가 열든) 그날 한 번 인사
        p.edit().putString("last_place", place.name).putLong("last_place_day", today)
            .putString(GREET_PENDING, event).putLong(GREET_PENDING_DAY, today).apply()
        ClockWaker.bring(context, event)
    }

    companion object {
        const val GREET_PENDING = "greet_pending"
        const val GREET_PENDING_DAY = "greet_pending_day"

        /**
         * 남아 있는 출근·귀가 인사를 꺼냄 (오늘 것만, 꺼내면 지움).
         * "다른 앱 위에 표시" 권한이 없어 시계가 바로 못 열린 경우에도, 사용자가 시계를 열면 그때 인사·앱 실행.
         */
        fun takePendingGreeting(context: Context): String? {
            val p = AutoStore.prefs(context)
            val event = p.getString(GREET_PENDING, null) ?: return null
            val day = p.getLong(GREET_PENDING_DAY, -1L)
            p.edit().remove(GREET_PENDING).remove(GREET_PENDING_DAY).apply()
            return event.takeIf { day == LocalDate.now().toEpochDay() }
        }

        /** 인사를 처리했으면 남은 표시를 지움 (같은 인사를 두 번 하지 않게). */
        fun clearPendingGreeting(context: Context) {
            AutoStore.prefs(context).edit().remove(GREET_PENDING).remove(GREET_PENDING_DAY).apply()
        }
    }
}

// ─────────────────────────────── 자동 실행 (시각) ───────────────────────────────

object AutoRunScheduler {
    private const val ACTION = "com.example.clock.action.AUTO_RUN"

    private fun pendingIntent(context: Context) = PendingIntent.getBroadcast(
        context, 41, Intent(context, AutoRunReceiver::class.java).setAction(ACTION),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    /** 다음 칸의 시각 (오늘 그 시각이 지났으면 내일). */
    private fun nextTime(minutes: Int): ZonedDateTime {
        val now = ZonedDateTime.now()
        val t = now.truncatedTo(ChronoUnit.DAYS).plusMinutes(minutes.toLong())
        return if (t.isAfter(now.plusSeconds(1))) t else t.plusDays(1)
    }

    fun update(context: Context) {
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        val pi = pendingIntent(context)
        val next = AutoStore.load(context).runs.filter { it.ready }
            .minOfOrNull { nextTime(it.minutes).toInstant().toEpochMilli() }
        if (next == null) {
            am.cancel(pi)
            return
        }
        val canExact = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || am.canScheduleExactAlarms()
        if (canExact) am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, next, pi)
        else am.setWindow(AlarmManager.RTC_WAKEUP, next, 60_000L, pi)
    }

    /**
     * 지금 실행할 칸들: 시각이 지금 ~ 3분 전 사이이고 오늘 아직 안 한 것.
     * (칸마다 "오늘 실행함" 을 기록 → 비슷한 시각의 칸이 여러 개여도 한 번씩만)
     */
    internal fun dueNow(context: Context, s: AutoSettings): List<AutoRun> {
        val now = ZonedDateTime.now()
        val nowMin = now.hour * 60 + now.minute
        val today = LocalDate.now().toEpochDay()
        val p = AutoStore.prefs(context)
        return s.runs.withIndex().filter { (i, r) ->
            r.ready && nowMin - r.minutes in 0..3 && p.getLong("run_day_$i", -1L) != today
        }.map { (i, r) ->
            p.edit().putLong("run_day_$i", today).apply()
            r
        }
    }

    /** 칸 하나 실행: 인터넷 주소 → 기본 브라우저, 앱 → 그 앱. */
    fun run(context: Context, r: AutoRun) {
        val intent = if (r.isApp) {
            val cn = ComponentName.unflattenFromString(r.app) ?: return
            Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setComponent(cn)
        } else {
            val url = r.url.trim().let { if (it.contains("://")) it else "https://$it" }
            Intent(Intent.ACTION_VIEW, Uri.parse(url))
        }
        ClockWaker.launch(context, intent, if (r.isApp) r.appLabel else r.url)
    }
}

class AutoRunReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        AutoRunScheduler.dueNow(context, AutoStore.load(context)).forEach { AutoRunScheduler.run(context, it) }
        AutoRunScheduler.update(context)
    }
}
