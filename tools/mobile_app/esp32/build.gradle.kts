plugins {
    alias(libs.plugins.androidLibrary)
    alias(libs.plugins.kotlinAndroid)
}

// tools/mobile_app -> the firmware repo
val repoRoot = rootDir.parentFile.parentFile

/** The OTA public key the firmware is built with (src/app/OtaPublicKey.h, "" = releases are not signed yet). */
fun otaPublicKeyPem(): String {
    val header = File(repoRoot, "src/app/OtaPublicKey.h")
    if (!header.exists()) return ""
    val define = Regex("#define\\s+OTA_SIGNING_PUBKEY_PEM[\\s\\\\]+((?:\"(?:[^\"\\\\]|\\\\.)*\"[\\s\\\\]*)+)")
        .find(header.readText()) ?: return ""
    return Regex("\"((?:[^\"\\\\]|\\\\.)*)\"").findAll(define.groupValues[1])
        .joinToString("") { it.groupValues[1] }
        .replace("\\n", "\n")
}

/**
 * The ESP32 board screen of the app (wiki 17/18): console (status, log, settings, chat), USB flasher and phone
 * GPS navigation for the board. A library of its own so its code, R and tests stay as they were in the standalone
 * ESP32-AI Flasher; composeApp opens it with [io.github.skyliner2008.pabflasher.MainActivity].
 */
android {
    namespace = "io.github.skyliner2008.pabflasher"
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        minSdk = 26
        val pem = otaPublicKeyPem().replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
        buildConfigField("String", "OTA_PUBKEY_PEM", "\"$pem\"")
        buildConfigField("String", "REPO", "\"skyliner2008/PersonalAIBot-ESP32\"")
        // a library's BuildConfig has no version: the app's (gradle.properties)
        buildConfigField("String", "VERSION_NAME", "\"${property("mobileVersionName")}\"")
    }

    buildFeatures {
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
