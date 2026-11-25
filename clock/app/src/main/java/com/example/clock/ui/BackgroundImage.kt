package com.example.clock.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.Build
import android.os.SystemClock
import android.util.Log
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.max

/** 슬라이드쇼: 사진 한 장이 보이는 간격 (밀리초). */
private const val SLIDE_INTERVAL_MS = 3_000L

/** 슬라이드쇼: 사진이 바뀔 때 겹쳐지며 전환되는 시간 (밀리초). */
private const val SLIDE_FADE_MS = 1_000

/**
 * 배경 사진 저장소.
 * 고른 사진들을 화면에 충분한 크기(긴 변 2560px)로 줄여 앱 내부 저장소에 복사해 둔다.
 * 그래서 원본 사진을 지우거나 옮겨도 배경은 그대로 유지된다.
 */
object BackgroundStore {
    private const val MAX_SIDE = 2560
    private const val NAMES_FILE = "names.txt"

    private fun dir(context: Context) = File(context.filesDir, "backgrounds")
    private fun legacyFile(context: Context) = File(context.filesDir, "background.jpg") // 예전 1장짜리

    /**
     * 사진들을 복사해 저장하고 저장된 장수를 돌려준다.
     * 새 사진을 다른 폴더에 다 저장한 뒤에 바꿔치기하므로, 실패해도 기존 배경은 남는다.
     */
    suspend fun saveAll(context: Context, uris: List<Uri>): Int = withContext(Dispatchers.IO) {
        val tmp = File(context.filesDir, "backgrounds_new").apply {
            deleteRecursively()
            mkdirs()
        }
        var saved = 0
        val names = mutableListOf<String>()
        uris.forEach { uri ->
            runCatching {
                val bitmap = decodeScaled(context, uri) ?: return@runCatching
                File(tmp, "%03d.jpg".format(saved)).outputStream().use {
                    bitmap.compress(Bitmap.CompressFormat.JPEG, 90, it)
                }
                bitmap.recycle()
                names += queryDisplayName(context, uri).replace('\n', ' ')
                saved++
            }
        }
        if (saved > 0) {
            // 복사본 이름은 000.jpg… 이므로 원본 파일 이름을 따로 기록 (메뉴에 표시)
            runCatching { File(tmp, NAMES_FILE).writeText(names.joinToString("\n")) }
            clear(context)
            tmp.renameTo(dir(context))
        } else {
            tmp.deleteRecursively()
        }
        saved
    }

    fun clear(context: Context) {
        dir(context).deleteRecursively()
        legacyFile(context).delete()
    }

    /** 저장된 사진 파일들 (이름순). */
    suspend fun files(context: Context): List<File> = withContext(Dispatchers.IO) {
        val list = dir(context).listFiles { f -> f.extension == "jpg" }?.sortedBy { it.name }.orEmpty()
        list.ifEmpty { listOfNotNull(legacyFile(context).takeIf { it.exists() }) }
    }

    /** 저장된 사진들의 원본 파일 이름 (사진 순서대로). 기록이 없으면 빈 목록. */
    suspend fun names(context: Context): List<String> = withContext(Dispatchers.IO) {
        runCatching { File(dir(context), NAMES_FILE).readLines() }.getOrDefault(emptyList())
    }

    /** 슬라이드쇼에서 지금 보이는 사진 번호 (메뉴에 파일 이름 표시용). */
    var shownIndex by mutableIntStateOf(0)

    suspend fun load(file: File): ImageBitmap? = withContext(Dispatchers.IO) {
        runCatching { BitmapFactory.decodeFile(file.path)?.asImageBitmap() }.getOrNull()
    }

    private fun decodeScaled(context: Context, uri: Uri): Bitmap? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // ImageDecoder 는 사진의 회전 정보(EXIF)를 자동 반영
            val source = ImageDecoder.createSource(context.contentResolver, uri)
            return ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
                decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE // JPEG 저장을 위해 소프트웨어 비트맵
                val longest = max(info.size.width, info.size.height)
                if (longest > MAX_SIDE) {
                    val scale = MAX_SIDE.toFloat() / longest
                    decoder.setTargetSize(
                        (info.size.width * scale).toInt(),
                        (info.size.height * scale).toInt(),
                    )
                }
            }
        }
        // Android 8.x: 크기만 줄여서 디코드
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        var sample = 1
        while (max(bounds.outWidth, bounds.outHeight) / (sample * 2) >= MAX_SIDE) sample *= 2
        val opts = BitmapFactory.Options().apply { inSampleSize = sample }
        return context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
    }
}

/**
 * 배경: 사진 1장이면 고정, 여러 장이면 3초 간격 슬라이드쇼 (1초 동안 겹치며 전환).
 * 메모리를 아끼려고 지금 사진과 다음 사진만 불러 둔다.
 * [version] 이 바뀌면 (새 사진을 고르면) 처음부터 다시 불러온다.
 */
@Composable
fun BackgroundSlideshow(version: Long, repeat: Boolean, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val files by produceState(emptyList<File>(), version) {
        value = if (version == 0L) emptyList() else BackgroundStore.files(context)
    }
    var current by remember(files) { mutableStateOf<ImageBitmap?>(null) }
    // 지금 보이는 사진 번호: 반복 설정을 바꿔도 처음으로 돌아가지 않고 이어서 진행
    var index by remember(files) { mutableIntStateOf(0) }
    LaunchedEffect(files, index) { BackgroundStore.shownIndex = index }

    LaunchedEffect(files, repeat) {
        if (files.isEmpty()) return@LaunchedEffect
        if (current == null) current = BackgroundStore.load(files[index])
        if (files.size == 1) return@LaunchedEffect
        while (true) {
            val last = index == files.lastIndex
            if (last && !repeat) return@LaunchedEffect // 반복 끔: 마지막 사진에서 멈춤
            val shownAt = SystemClock.elapsedRealtime()
            val nextIndex = (index + 1) % files.size
            val next = BackgroundStore.load(files[nextIndex]) // 보여주는 동안 다음 사진 미리 불러오기
            delay((SLIDE_INTERVAL_MS - (SystemClock.elapsedRealtime() - shownAt)).coerceAtLeast(0))
            if (next != null) current = next
            index = nextIndex
            Log.d("ClockSlideshow", "slide ${index + 1}/${files.size} repeat=$repeat")
        }
    }

    Crossfade(
        targetState = current,
        animationSpec = tween(SLIDE_FADE_MS),
        modifier = modifier,
        label = "slideshow",
    ) { image ->
        if (image != null) {
            // 화면 전체(상태바 뒤까지)를 비율 유지하며 꽉 채움
            Image(
                bitmap = image,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}
