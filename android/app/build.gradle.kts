plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.papercatno1.notepad"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.papercatno1.notepad"
        minSdk = 26
        targetSdk = 34
        versionCode = 5
        versionName = "0.4.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

// 仅一个 androidx.activity：选图要用 registerForActivityResult
dependencies {
    implementation("androidx.activity:activity-ktx:1.9.0")
}
