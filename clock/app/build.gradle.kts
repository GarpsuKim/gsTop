import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

// ---- 버전 정보 (앱의 [정보] 창에 표시) ----
// 버전 이름 = 빌드 날짜 (예: 2026.11.26), 버전 번호 = yyMMdd (예: 261126, 날마다 커짐).
// git 커밋 번호 (예: a99d6bf, 커밋 안 한 변경이 있으면 끝에 +). git 이 없으면 "unknown".
val buildNow: LocalDateTime = LocalDateTime.now()
fun gitOutput(vararg args: String): String = try {
    providers.exec {
        commandLine("git", *args)
        isIgnoreExitValue = true
    }.standardOutput.asText.get().trim()
} catch (e: Exception) {
    ""
}
val gitHash: String = gitOutput("rev-parse", "--short", "HEAD").ifBlank { "unknown" } +
    (if (gitOutput("status", "--porcelain", "--untracked-files=no").isNotBlank()) "+" else "")

// ---- 도움말: 프로젝트 맨 위의 README.md 를 앱 안(assets)에 넣음 → README 를 고치면 다음 빌드에 자동 반영 ----
abstract class CopyReadmeTask : DefaultTask() {
    @get:InputFile
    abstract val readme: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val out = outputDir.get().asFile
        out.mkdirs()
        readme.get().asFile.copyTo(File(out, "README.md"), overwrite = true)
    }
}
val copyReadme = tasks.register<CopyReadmeTask>("copyReadme") {
    readme.set(rootProject.layout.projectDirectory.file("README.md"))
}

// 앱 ID: Play 스토어·폰이 이 앱을 구분하는 고유 이름. Play 에 한 번 올리면 바꿀 수 없음.
// (2026-11-26 com.example.clock → io.github.garpsukim.clock: Play 는 com.example 을 받지 않음.
//  코드 위치 namespace 는 com.example.clock 그대로 → 화면 이름은 io.github.garpsukim.clock/com.example.clock.MainActivity)
val baseAppId = "io.github.garpsukim.clock"

android {
    namespace = "com.example.clock"
    compileSdk = 34

    defaultConfig {
        applicationId = baseAppId
        minSdk = 26
        targetSdk = 34
        versionCode = buildNow.format(DateTimeFormatter.ofPattern("yyMMdd")).toInt()
        versionName = buildNow.format(DateTimeFormatter.ofPattern("yyyy.MM.dd"))
        buildConfigField("String", "GIT_HASH", "\"$gitHash\"")
        buildConfigField("String", "BUILD_TIME", "\"${buildNow.format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm"))}\"")

        // 앱 이름 + 앱 ID 꼬리 (빌드 bat 마다 다르게 → 한 폰에 두 앱을 따로 설치 가능).
        // 빌드 bat 의 'set "AppIcon=..."' (앱 이름), 'set "AppIdSuffix=..."' (앱 ID 꼬리) 줄을 이 파일이 직접(UTF-8) 읽음.
        // bat → 환경 변수 → gradle 데몬으로 한글을 넘기면 깨지거나 데몬이 예전 값을 쓰는 일이 있어서,
        // bat 은 자기 파일 이름(영문)만 -PappNameBat=... 으로 넘기고, 값은 여기서 그 파일을 읽어 얻음.
        //   _BUILD_WIFI.BAT     (← _RUN_WIFI.BAT)     → io.github.garpsukim.clock      (꼬리 없음)
        //   _BUILD_Telegram.BAT (← _RUN_TELEGRAM.BAT) → io.github.garpsukim.clock.kja  (꼬리 .kja)
        // -PappNameBat 없이 빌드하면 (gradlew 직접, Android Studio) _BUILD_WIFI.BAT. 파일이 없으면 기본 이름·꼬리 없음.
        val appNameBat = (project.findProperty("appNameBat") as String?)?.takeIf { it.isNotBlank() } ?: "_BUILD_WIFI.BAT"
        val buildBat = rootProject.file(appNameBat)
        val batLines = if (buildBat.exists()) buildBat.readLines(Charsets.UTF_8) else emptyList()
        fun batValue(key: String): String? =
            Regex("""^\s*set\s+"?\s*$key\s*=\s*"?([^"\r\n]*?)"?\s*$""", RegexOption.IGNORE_CASE)
                .let { re -> batLines.firstNotNullOfOrNull { re.find(it)?.groupValues?.get(1) } }
                ?.trim()
                ?.takeIf { it.isNotEmpty() }

        val appLabel = batValue("AppIcon") ?: "영원한 시계"
        // 꼬리는 ".영문소문자/숫자" 만 허용 (잘못 적으면 설치가 안 되므로 빌드를 멈춤)
        val idSuffix = batValue("AppIdSuffix")?.let { if (it.startsWith(".")) it else ".$it" }.orEmpty()
        require(idSuffix.isEmpty() || Regex("""(\.[a-z][a-z0-9_]*)+""").matches(idSuffix)) {
            "$appNameBat 의 AppIdSuffix 가 잘못됨: '$idSuffix' (예: .kja — 영문 소문자로 시작, 영문 소문자·숫자·_ 만)"
        }
        applicationIdSuffix = idSuffix
        logger.lifecycle("앱 이름(AppIcon) = $appLabel, 앱 ID = $baseAppId$idSuffix  ($appNameBat)")
        resValue("string", "app_name", appLabel)
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true // BuildConfig.GIT_HASH, BUILD_TIME ([정보] 창)
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(copyReadme, CopyReadmeTask::outputDir)
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.material.icons.extended)
    implementation(libs.androidx.media3.exoplayer) // 배경(동영상) 재생
    implementation(libs.androidx.media3.ui)
    implementation(libs.commonmark) // 도움말: README.md → HTML
    implementation(libs.commonmark.ext.gfm.tables)
    debugImplementation(libs.androidx.ui.tooling)
}
