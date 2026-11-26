plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.example.clock"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.example.clock"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        // 앱 이름 + 앱 ID 꼬리 (빌드 bat 마다 다르게 → 한 폰에 두 앱을 따로 설치 가능).
        // 빌드 bat 의 'set "AppIcon=..."' (앱 이름), 'set "AppIdSuffix=..."' (앱 ID 꼬리) 줄을 이 파일이 직접(UTF-8) 읽음.
        // bat → 환경 변수 → gradle 데몬으로 한글을 넘기면 깨지거나 데몬이 예전 값을 쓰는 일이 있어서,
        // bat 은 자기 파일 이름(영문)만 -PappNameBat=... 으로 넘기고, 값은 여기서 그 파일을 읽어 얻음.
        //   _BUILD_WIFI.BAT     (← _RUN_WIFI.BAT)     → com.example.clock      (꼬리 없음)
        //   _BUILD_Telegram.BAT (← _RUN_TELEGRAM.BAT) → com.example.clock.kja  (꼬리 .kja)
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
        logger.lifecycle("앱 이름(AppIcon) = $appLabel, 앱 ID = com.example.clock$idSuffix  ($appNameBat)")
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
    debugImplementation(libs.androidx.ui.tooling)
}
