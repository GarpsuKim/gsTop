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

        // 앱 이름 (홈 화면 아이콘 아래, 위젯 목록, 홈 화면 바로가기).
        // _BUILD.BAT 의 'set "AppIcon=..."' 줄을 이 파일이 직접(UTF-8) 읽음.
        // bat → 환경 변수 → gradle 데몬으로 넘기면 한글이 깨지거나 데몬이 예전 값을 쓰는 일이 있어서.
        // 어떤 bat 으로 빌드하든(_RUN_WIFI, _RUN_TELEGRAM, gradlew 직접) 같은 이름이 들어감. 없으면 기본 이름.
        val buildBat = rootProject.file("_BUILD.BAT")
        val appLabel = if (buildBat.exists()) {
            Regex("""^\s*set\s+"?\s*AppIcon\s*=\s*"?([^"\r\n]*?)"?\s*$""", RegexOption.IGNORE_CASE)
                .let { re -> buildBat.readLines(Charsets.UTF_8).firstNotNullOfOrNull { re.find(it)?.groupValues?.get(1) } }
                ?.trim()
                ?.takeIf { it.isNotEmpty() }
        } else {
            null
        } ?: "영원한 시계"
        logger.lifecycle("앱 이름(AppIcon) = $appLabel")
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
