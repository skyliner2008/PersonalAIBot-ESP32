import org.jetbrains.compose.desktop.application.dsl.TargetFormat
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.util.Properties

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.androidApplication)
    alias(libs.plugins.composeMultiplatform)
    alias(libs.plugins.composeCompiler)
    alias(libs.plugins.sqlDelight)
    alias(libs.plugins.kotlinx.serialization)
    alias(libs.plugins.googleServices) apply false
}

// Firebase (the Vertex AI provider) needs composeApp/google-services.json, which is git-ignored: without it the app
// still builds, only that provider has no project to talk to
if (file("google-services.json").exists()) apply(plugin = "com.google.gms.google-services")

/** Optional release signing: ~/.pab-esp32/flasher-keystore.properties (tools/build_mobile_apk.py), never in the repo. */
val keystoreProps = Properties().apply {
    val f = File(System.getenv("PAB_FLASHER_KEYSTORE_PROPS")
        ?: "${System.getProperty("user.home")}/.pab-esp32/flasher-keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

// iOS target is temporarily disabled — re-enable with: ./gradlew build -PenableIos=true
val enableIos = project.findProperty("enableIos") == "true"

kotlin {
    androidTarget {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_11)
            freeCompilerArgs.add("-Xexpect-actual-classes")
        }
    }
    
    if (enableIos) {
        listOf(
            iosArm64(),
            iosSimulatorArm64()
        ).forEach { iosTarget ->
            iosTarget.binaries.framework {
                baseName = "ComposeApp"
                isStatic = true
                freeCompilerArgs += "-Xexpect-actual-classes"
            }
        }
    }
    
    sourceSets {
        all {
            languageSettings.optIn("kotlin.experimental.ExperimentalObjCName")
        }

        androidMain.dependencies {
            implementation(libs.compose.uiToolingPreview)
            implementation(libs.androidx.activity.compose)
            implementation(libs.sqldelight.android.driver)
            implementation(libs.ktor.client.okhttp)

            // CameraX
            implementation(libs.camerax.core)
            implementation(libs.camerax.camera2)
            implementation(libs.camerax.lifecycle)
            implementation(libs.camerax.view)

            // ML Kit Face Detection (Gaze tracking, Desk Sentry, Copycat, face profiles)
            implementation("com.google.android.gms:play-services-mlkit-face-detection:17.1.0")
            // MediaPipe: hands and gestures (assets/models/gesture_recognizer.task) and things in view
            // (assets/models/efficientdet_lite0.tflite) — the latter replaced ML Kit object detection's five classes
            implementation("com.google.mediapipe:tasks-vision:0.10.35")

            // ONNX Runtime for Local Embeddings (Android only)
            // Upgraded from 1.19.2 → 1.23.0 for 16 KB page size alignment (Google Play requirement Nov 2025)
            implementation("com.microsoft.onnxruntime:onnxruntime-android:1.23.0")

            // Ktor Android Client for downloading large files
            implementation(libs.ktor.client.android)

            // Firebase
            implementation(project.dependencies.platform(libs.firebase.bom))
            implementation(libs.firebase.vertexai.get().toString())

            // Avatar mode: the Godot engine (see the `dependencies` block below for the engine itself)
            implementation(libs.androidx.fragment)
            implementation(libs.androidx.documentfile)

            // the ESP32 board screen: console, USB flasher, GPS navigation for the board (tools/mobile_app/esp32)
            implementation(project(":esp32"))
        }
        commonMain.dependencies {
            implementation(libs.compose.runtime)
            implementation(libs.compose.foundation)
            implementation(libs.compose.material)
            implementation(libs.compose.material3)
            implementation(libs.compose.icons.core)
            implementation(libs.compose.icons.extended)
            implementation(libs.compose.ui)
            implementation(libs.compose.components.resources)
            implementation(libs.androidx.lifecycle.viewmodelCompose)
            implementation(libs.androidx.lifecycle.runtimeCompose)
            
            implementation(libs.kotlinx.datetime)
            implementation(libs.kotlinx.serialization.json)
            
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.contentNegotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
            implementation(libs.ktor.client.websockets)
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.multiplatform.webview)
            implementation(kotlin("reflect"))
        }
        if (enableIos) {
            iosMain.dependencies {
                implementation(libs.sqldelight.native.driver)
                implementation(libs.ktor.client.darwin)
            }
        }
        commonTest.dependencies {
            implementation(libs.kotlin.test)
        }
        // OpenWakeWordTest runs the wake word models on the PC: the desktop build of the same ONNX Runtime
        val androidUnitTest by getting {
            dependencies {
                implementation("com.microsoft.onnxruntime:onnxruntime:1.23.0")
            }
        }
    }
}

android {
    namespace = "com.skyliner2008.jarvis"
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "com.skyliner2008.jarvis"
        minSdk = 26  // the esp32 module (USB serial, notification listener) needs Android 8
        targetSdk = libs.versions.android.targetSdk.get().toInt()
        versionCode = property("mobileVersionCode").toString().toInt()
        versionName = property("mobileVersionName").toString()
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
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
        jniLibs {
            // the Godot engine is ~75 MB per ABI: phones only (Avatar mode checks for arm64 before starting it)
            excludes += listOf(
                "lib/armeabi-v7a/libgodot_android.so",
                "lib/x86/libgodot_android.so",
                "lib/x86_64/libgodot_android.so",
                // MediaPipe's hands (~10 MB per ABI) serve Avatar mode only — arm64 like the engine
                // (HandTracker logs "Hand model unavailable" elsewhere, and no hands are detected)
                "lib/armeabi-v7a/libmediapipe_tasks_jni.so",
                "lib/x86/libmediapipe_tasks_jni.so",
                "lib/x86_64/libmediapipe_tasks_jni.so"
            )
        }
    }
    sourceSets {
        // the robot exported from pet_godot/ (pet_godot/tools/export_to_app.ps1), at the assets root where
        // the engine looks for its project
        getByName("main").assets.srcDir("src/androidMain/godot-assets")
    }
    androidResources {
        // the export keeps its imported resources in a ".godot" folder, which aapt's default pattern drops
        ignoreAssetsPattern = "!.svn:!.git:!.gitignore:!.ds_store:!*.scc:!CVS:!thumbs.db:!picasa.ini:!*~"
    }
    // ติดตั้งเฉพาะผู้ใช้หลัก — `adb install` ปกติติดตั้งให้ทุก user profile
    // เครื่องที่เปิด Dual Messenger ของซัมซุง (user 95 = DUAL_APP) จึงได้ไอคอนแอปโคลนเพิ่มมาทุกครั้งที่ installDebug
    installation {
        installOptions += listOf("--user", "0")
    }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            // no keystore of your own: the Android SDK's debug key (~/.android/debug.keystore) - phones then take
            // updates only from APKs built on this PC (another PC: uninstall the app once, then install)
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    lint {
        checkReleaseBuilds = false
        abortOnError = false
    }
    applicationVariants.all {
        val variantName = name
        outputs.all {
            val outputImpl = this as? com.android.build.gradle.internal.api.BaseVariantOutputImpl
            outputImpl?.outputFileName = "PersonalAIBot-${variantName}.apk"
        }
    }
}

// Avatar mode's engine: Godot's Android library, the same version the robot is exported with (4.7.2).
// The local copy comes from the Godot export templates already on this PC (pet_godot/tools/export_to_app.ps1
// puts it here; it is too big for git); without it Gradle takes the same library from Maven Central.
val godotAar = file("libs/godot-lib.template_release.aar")
dependencies {
    if (godotAar.exists()) {
        implementation(files(godotAar))
    } else {
        implementation("org.godotengine:godot:4.7.2.stable")
    }
}

configurations.all {
    resolutionStrategy {
        force("androidx.core:core:1.15.0")
        force("androidx.core:core-ktx:1.15.0")
    }
}

sqldelight {
    databases {
        create("JarvisDatabase") {
            packageName.set("com.skyliner2008.jarvis.db")
        }
    }
}
