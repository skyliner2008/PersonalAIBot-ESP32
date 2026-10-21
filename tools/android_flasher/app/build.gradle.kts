import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

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

/** Optional release signing: ~/.pab-esp32/flasher-keystore.properties (tools/build_flasher_apk.py), never in the repo. */
val keystoreProps = Properties().apply {
    val f = File(System.getenv("PAB_FLASHER_KEYSTORE_PROPS")
        ?: "${System.getProperty("user.home")}/.pab-esp32/flasher-keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "io.github.skyliner2008.pabflasher"
    compileSdk = 35
    buildToolsVersion = "35.0.0"

    defaultConfig {
        applicationId = "io.github.skyliner2008.pabflasher"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"
        val pem = otaPublicKeyPem().replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
        buildConfigField("String", "OTA_PUBKEY_PEM", "\"$pem\"")
        buildConfigField("String", "REPO", "\"skyliner2008/PersonalAIBot-ESP32\"")
    }

    signingConfigs {
        if (keystoreProps.getProperty("storeFile") != null) {
            create("release") {
                storeFile = File(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // no keystore of your own: the Android SDK's debug key (~/.android/debug.keystore) - phones then take
            // updates only from APKs built on this PC (another PC: uninstall the app once, then install)
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
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
