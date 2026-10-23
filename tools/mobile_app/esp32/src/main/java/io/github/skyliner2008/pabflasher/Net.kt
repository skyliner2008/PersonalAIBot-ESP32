package io.github.skyliner2008.pabflasher

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** HTTPS GET (GitHub API + release downloads, which redirect to release-assets.githubusercontent.com). */
object Net {
    fun bytes(url: String, accept: String = "application/octet-stream", onProgress: (Long, Long) -> Unit = { _, _ -> }): ByteArray {
        require(url.startsWith("https://")) { "HTTPS only" }
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = 15_000
            c.readTimeout = 30_000
            c.instanceFollowRedirects = true
            c.setRequestProperty("Accept", accept)
            c.setRequestProperty("User-Agent", "PersonalAIBot-Mobile/${BuildConfig.VERSION_NAME}")
            val code = c.responseCode
            if (code != 200) {
                throw IOException(if (code == 403 || code == 429) "GitHub จำกัดจำนวนครั้ง ลองใหม่ภายหลัง (HTTP $code)"
                                  else "ดาวน์โหลดไม่สำเร็จ (HTTP $code)")
            }
            val total = c.contentLengthLong
            val out = ByteArrayOutputStream(if (total in 1..64_000_000) total.toInt() else 64 * 1024)
            val buf = ByteArray(64 * 1024)
            c.inputStream.use { input ->
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    out.write(buf, 0, n)
                    onProgress(out.size().toLong(), total)
                    if (out.size() > 32 * 1024 * 1024) throw IOException("ไฟล์ใหญ่ผิดปกติ")
                }
            }
            return out.toByteArray()
        } finally {
            c.disconnect()
        }
    }

    fun text(url: String, accept: String = "application/vnd.github+json") = String(bytes(url, accept), Charsets.UTF_8)

    /** An error the way a person reads it (no Java class names on the screen). */
    fun friendly(e: Throwable): String = when (e) {
        is java.net.UnknownHostException -> "ไม่มีอินเทอร์เน็ต (หา github.com ไม่เจอ)"
        is java.net.SocketTimeoutException -> "อินเทอร์เน็ตช้าเกินไป ลองใหม่"
        is javax.net.ssl.SSLException -> "เชื่อมต่อ GitHub แบบปลอดภัยไม่ได้ (Wi-Fi นี้ดักการเชื่อมต่อ? ลองใช้เน็ตมือถือ)"
        is java.net.ConnectException -> "เชื่อมต่อ GitHub ไม่ได้"
        else -> e.message ?: e.javaClass.simpleName
    }
}
