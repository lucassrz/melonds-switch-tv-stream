plugins {
    id("com.android.application")
}

android {
    namespace = "com.multiscreends.tv"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.multiscreends.tv"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1"
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

    kotlin {
        jvmToolchain(17)
    }
}
