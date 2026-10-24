package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.ui.AppUpdateLogic
import com.skyliner2008.jarvis.ui.AppUpdateState
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** T097: the app's own update check - what it reads from the release and how it decides. */
class AppUpdateLogicTest {
    private val sha = "45b321f6e6feb7947a642ef4795c06467beefe847adebcd10cdabdd55d1ae687"
    private val signer = "aa11bb22cc33dd44ee55ff66778899aabbccddeeff00112233445566778899aa"

    private fun release(vararg names: String) = """{"tag_name":"v4.4.2","assets":[${
        names.joinToString(",") { """{"name":"$it","browser_download_url":"https://github.com/x/$it","size":1}""" }
    }]}"""

    private fun manifest(code: Int, signerHex: String? = signer) =
        """{"versionName":"2.1.$code","versionCode":$code,"sha256":"${sha.uppercase()}","size":225895024,""" +
            """"file":"PersonalAIBot-Mobile.apk"${signerHex?.let { ",\"signerSha256\":\"$it\"" } ?: ""}}"""

    @Test
    fun `release assets are found by name`() {
        val r = AppUpdateLogic.parseRelease(release("PersonalAIBot-ESP32-v4.4.2-app.bin", "PersonalAIBot-Mobile.apk",
            "PersonalAIBot-Mobile.apk.sha256", "PersonalAIBot-Mobile.json"))
        assertEquals("v4.4.2", r.tag)
        assertEquals("https://github.com/x/PersonalAIBot-Mobile.json", r.manifestUrl)
        assertEquals("https://github.com/x/PersonalAIBot-Mobile.apk", r.apkUrl)
        // v4.4.1 and older: no manifest - the app says so instead of guessing from the firmware tag
        assertNull(AppUpdateLogic.parseRelease(release("PersonalAIBot-Mobile.apk")).manifestUrl)
    }

    @Test
    fun `manifest is read and checked`() {
        val m = assertNotNull(AppUpdateLogic.parseManifest(manifest(6)))
        assertEquals(6L, m.versionCode)
        assertEquals(sha, m.sha256)  // lowercased
        assertEquals(225895024L, m.size)
        assertNull(AppUpdateLogic.parseManifest("""{"versionName":"2.1.6","versionCode":6,"sha256":"abc"}"""))
        assertNull(AppUpdateLogic.parseManifest("not json"))
        assertNull(AppUpdateLogic.parseManifest("""{"versionCode":6,"sha256":"$sha"}"""))
    }

    @Test
    fun `newer version with the same signer can be installed`() {
        val m = AppUpdateLogic.parseManifest(manifest(6))!!
        assertEquals(AppUpdateState.Available("2.1.6", 225895024L, true), AppUpdateLogic.decide(m, 5, setOf(signer)))
        assertEquals(AppUpdateState.UpToDate("2.1.6"), AppUpdateLogic.decide(m, 6, setOf(signer)))
        assertEquals(AppUpdateState.UpToDate("2.1.6"), AppUpdateLogic.decide(m, 7, setOf(signer)))
    }

    @Test
    fun `another PC's key is reported before downloading`() {
        val m = AppUpdateLogic.parseManifest(manifest(6))!!
        val other = setOf("0".repeat(64))
        assertFalse((AppUpdateLogic.decide(m, 5, other) as AppUpdateState.Available).sameSigner)
        // apksigner prints "AA:11:…" on some versions; unknown on either side leaves it to Android's installer
        assertTrue(AppUpdateLogic.sameSigner(m, setOf(AppUpdateLogic.normalizeHex(signer.uppercase().chunked(2).joinToString(":")))))
        assertTrue(AppUpdateLogic.sameSigner(m, emptySet()))
        assertTrue(AppUpdateLogic.sameSigner(AppUpdateLogic.parseManifest(manifest(6, null))!!, other))
    }
}
