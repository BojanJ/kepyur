plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// shell.nix provides the NDK and the mipsel binutils wifi-sdr needs to assemble the chip's streamer
val ndkVer: String = System.getenv("KEPYUR_NDK_VERSION") ?: "28.2.13676358"
val mipsPrefix: String = System.getenv("KEPYUR_MIPS_PREFIX") ?: "mipsel-linux-gnu-"

android {
    namespace = "com.kepyur.app"
    compileSdk = 35
    buildToolsVersion = "35.0.1"
    ndkVersion = ndkVer

    defaultConfig {
        applicationId = "com.kepyur.app"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DMIPS_AS=${mipsPrefix}as",
                    "-DMIPS_LD=${mipsPrefix}ld",
                    "-DMIPS_OBJCOPY=${mipsPrefix}objcopy",
                    "-DDEVOURER_LOG_MAX_LEVEL=INFO",
                )
                targets += listOf("kepyur")
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("../../native/CMakeLists.txt")
            version = "3.31.6"
        }
    }
    buildTypes {
        debug {
            // the DSP must run optimised even in debug builds
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
