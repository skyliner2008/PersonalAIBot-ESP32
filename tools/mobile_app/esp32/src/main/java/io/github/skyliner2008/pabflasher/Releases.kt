package io.github.skyliner2008.pabflasher

import org.json.JSONObject
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

/** A GitHub release of the firmware: PersonalAIBot-ESP32-v<ver>-app.bin / -factory.bin (+ .sha256, -app.bin.sig). */
class Release(val tag: String, val assets: Map<String, String>) {
    fun fileName(kind: Kind): String? =
        assets.keys.firstOrNull { it.endsWith(if (kind == Kind.APP) "-app.bin" else "-factory.bin") }

    fun url(name: String): String? = assets[name]
}

object Releases {
    fun latestUrl(repo: String) = "https://api.github.com/repos/$repo/releases/latest"

    fun parse(json: String): Release {
        val o = JSONObject(json)
        val list = o.getJSONArray("assets")
        val assets = LinkedHashMap<String, String>()
        for (i in 0 until list.length()) {
            val a = list.getJSONObject(i)
            assets[a.getString("name")] = a.getString("browser_download_url")
        }
        return Release(o.getString("tag_name"), assets)
    }

    /** "<64 hex>  <file name>" (sha256sum format) -> the hash; null when it is not there or not for [name]. */
    fun parseSha256(text: String, name: String): String? {
        val line = text.lineSequence().map { it.trim() }.firstOrNull { it.isNotEmpty() } ?: return null
        val parts = line.split(Regex("\\s+"), limit = 2)
        val hash = parts[0].lowercase()
        if (!hash.matches(Regex("[0-9a-f]{64}"))) return null
        if (parts.size == 2 && parts[1].trimStart('*') != name) return null
        return hash
    }

    fun sha256Hex(b: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }

    /**
     * The same check the board does before an OTA (src/util/OtaVerify.h): ECDSA P-256 over SHA-256,
     * signature = base64 of DER, key = the PEM in src/app/OtaPublicKey.h compiled into this app.
     */
    fun signatureOk(data: ByteArray, sigBase64: String, publicPem: String): Boolean = try {
        val der = Base64.getMimeDecoder().decode(
            publicPem.replace(Regex("-----[A-Z ]+-----"), "").replace(Regex("\\s"), ""))
        val key = KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(der))
        Signature.getInstance("SHA256withECDSA").run {
            initVerify(key)
            update(data)
            verify(Base64.getDecoder().decode(sigBase64.trim()))
        }
    } catch (_: Exception) {
        false
    }
}
