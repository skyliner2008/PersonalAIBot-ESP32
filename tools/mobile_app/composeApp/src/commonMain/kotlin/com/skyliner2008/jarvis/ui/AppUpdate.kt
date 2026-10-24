package com.skyliner2008.jarvis.ui

import androidx.compose.runtime.Composable
import kotlinx.coroutines.flow.StateFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull

/**
 * The app updates itself from the project's GitHub releases (T097), the way the board does over Wi-Fi.
 * Every release carries PersonalAIBot-Mobile.apk + PersonalAIBot-Mobile.json (tools/build_mobile_apk.py): the json
 * holds the app's own version (the release tag is the firmware's), the APK's sha256 and its signing certificate.
 */
object AppUpdateLogic {
    const val REPO = "skyliner2008/PersonalAIBot-ESP32"
    const val LATEST_URL = "https://api.github.com/repos/$REPO/releases/latest"
    const val MANIFEST_NAME = "PersonalAIBot-Mobile.json"
    const val APK_NAME = "PersonalAIBot-Mobile.apk"

    private val json = Json { ignoreUnknownKeys = true }

    /** The latest release: its tag and the download URLs of the update manifest and the APK (null = not attached). */
    data class Release(val tag: String, val manifestUrl: String?, val apkUrl: String?)

    /** What PersonalAIBot-Mobile.json says. */
    data class Manifest(
        val versionName: String,
        val versionCode: Long,
        val sha256: String,
        val size: Long,
        val signerSha256: String?,
    )

    fun parseRelease(text: String): Release {
        val o = json.parseToJsonElement(text).jsonObject
        var manifest: String? = null
        var apk: String? = null
        for (a in o["assets"]?.jsonArray.orEmpty()) {
            val asset = a.jsonObject
            val url = asset.str("browser_download_url") ?: continue
            when (asset.str("name")) {
                MANIFEST_NAME -> manifest = url
                APK_NAME -> apk = url
            }
        }
        return Release(o.str("tag_name") ?: "?", manifest, apk)
    }

    /** null = not a usable manifest (missing fields, a sha256 that is not 64 hex digits). */
    fun parseManifest(text: String): Manifest? = runCatching {
        val o = json.parseToJsonElement(text).jsonObject
        val sha = o.str("sha256")?.lowercase() ?: return null
        if (!HEX64.matches(sha)) return null
        Manifest(
            versionName = o.str("versionName") ?: return null,
            versionCode = o["versionCode"]?.jsonPrimitive?.longOrNull ?: return null,
            sha256 = sha,
            size = o["size"]?.jsonPrimitive?.longOrNull ?: 0L,
            signerSha256 = o.str("signerSha256")?.let(::normalizeHex),
        )
    }.getOrNull()

    /** "AB:CD:…" / "abcd…" -> "abcd…" (apksigner and Android print certificate digests differently). */
    fun normalizeHex(s: String): String = s.replace(":", "").replace(" ", "").lowercase()

    /**
     * Same signer = Android installs it over this app. Unknown on either side (an old manifest, an API that did not
     * answer) counts as the same: the installer is still the final judge.
     */
    fun sameSigner(manifest: Manifest, installedSigners: Set<String>): Boolean =
        manifest.signerSha256 == null || installedSigners.isEmpty() || manifest.signerSha256 in installedSigners

    fun decide(manifest: Manifest, installedCode: Long, installedSigners: Set<String>): AppUpdateState =
        if (manifest.versionCode <= installedCode) AppUpdateState.UpToDate(manifest.versionName)
        else AppUpdateState.Available(manifest.versionName, manifest.size, sameSigner(manifest, installedSigners))

    private val HEX64 = Regex("[0-9a-f]{64}")
    private fun JsonObject.str(key: String): String? = this[key]?.jsonPrimitive?.contentOrNull
}

/** What the settings card shows. */
sealed interface AppUpdateState {
    data object Idle : AppUpdateState
    data object Checking : AppUpdateState
    data class UpToDate(val latest: String) : AppUpdateState
    /** [sameSigner] false: built and signed on another PC - Android refuses it over this app, uninstall first. */
    data class Available(val version: String, val sizeBytes: Long, val sameSigner: Boolean) : AppUpdateState
    data class Downloading(val done: Long, val total: Long) : AppUpdateState
    /** Handed to Android's installer, which asks the user to confirm. */
    data object Installing : AppUpdateState
    data class Failed(val message: String) : AppUpdateState
}

/** @property supported false where the app cannot install itself (iOS): the card is hidden. */
class AppUpdater(
    val supported: Boolean,
    val currentVersion: String,
    val state: StateFlow<AppUpdateState>,
    val check: () -> Unit,
    val update: () -> Unit,
    val cancel: () -> Unit,
)

@Composable
expect fun rememberAppUpdater(): AppUpdater
