package io.github.skyliner2008.pabflasher

import org.json.JSONObject
import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * The phone link's messages, the app side of src/phone/PhoneProtocol.h on the board (wiki 18).
 *   app -> board:  phone {"t":"gps",...} / {"t":"nav",...} / {"t":"place",...} / {"t":"result",...} / {"t":"pair",...}
 *   board -> app:  PHONE {"id":3,"do":"navigate","to":"..."} / {"id":4,"do":"stop"}
 * Wi-Fi handshake v2 (T090): board "hello {nonce}" -> app "hello {its nonce}" (no proof: anyone on the Wi-Fi can
 * connect to us, so we prove nothing first) -> board "auth {proof}" -> app checks it, then "auth {its proof}".
 * The token itself never crosses the air. After that every line ends with "\t<seq>.<mac>" ([seal] / [open]):
 * a relay in the middle can pass lines on, never forge, replay or reorder one.
 */
object PhoneProtocol {
    const val TCP_PORT = 47800
    const val UDP_PORT = 47801

    private val random = SecureRandom()

    fun newToken(): String = hex(ByteArray(16).also { random.nextBytes(it) })  // 32 hex: validToken() on the board
    fun newNonce(): String = hex(ByteArray(12).also { random.nextBytes(it) })   // 24 hex: validNonce()

    /** hex(HMAC-SHA256(token, role + "|" + nonce)) - the same as phone::proof() on the board. */
    fun proof(token: String, role: String, nonce: String): String {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(token.toByteArray(Charsets.US_ASCII), "HmacSHA256"))
        return hex(mac.doFinal("$role|$nonce".toByteArray(Charsets.US_ASCII)))
    }

    fun proofOk(token: String, role: String, nonce: String, got: String): Boolean =
        token.isNotEmpty() && got.length == 64 &&
            MessageDigest.isEqual(proof(token, role, nonce).toByteArray(), got.toByteArray())

    fun validNonce(n: String) = n.length in 16..32 && n.all { it in '0'..'9' || it in 'a'..'f' }

    /** The key of one Wi-Fi connection: HMAC(token, "session|<board nonce>|<phone nonce>") - phone::sessionKey(). */
    fun sessionKey(token: String, boardNonce: String, phoneNonce: String): ByteArray =
        hmac(token.toByteArray(Charsets.US_ASCII), "session|$boardNonce|$phoneNonce")

    /** First 32 hex of HMAC(key, "<dir>|<seq>|<line>"); dir 'p' = phone -> board, 'b' = board -> phone. */
    fun lineMac(key: ByteArray, dir: Char, seq: Long, line: String): String =
        hex(hmac(key, "$dir|$seq|$line")).substring(0, 32)

    /** line + "	<seq>.<mac>" - phone::sealLine(). */
    fun seal(key: ByteArray, dir: Char, seq: Long, line: String): String = "$line\t$seq.${lineMac(key, dir, seq, line)}"

    /** The plain line if the seal is right and its count is exactly [seq] (else null: drop the connection). */
    fun open(key: ByteArray, dir: Char, seq: Long, sealed: String): String? {
        val tab = sealed.lastIndexOf('\t')
        if (tab < 0) return null
        val tail = sealed.substring(tab + 1)
        val dot = tail.indexOf('.')
        if (dot <= 0 || tail.substring(0, dot) != seq.toString()) return null
        val line = sealed.substring(0, tab)
        val want = lineMac(key, dir, seq, line)
        return if (MessageDigest.isEqual(want.toByteArray(), tail.substring(dot + 1).toByteArray())) line else null
    }

    private fun hmac(key: ByteArray, msg: String): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(msg.toByteArray(Charsets.UTF_8))
    }

    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }

    private fun line(o: JSONObject) = "phone $o"

    fun gps(lat: Double, lon: Double, speedKmh: Float, heading: Float, accuracy: Float): String = line(JSONObject()
        .put("t", "gps").put("lat", round(lat, 6)).put("lon", round(lon, 6))
        .put("spd", round(speedKmh.toDouble(), 1)).put("hdg", round(heading.toDouble(), 0)).put("acc", round(accuracy.toDouble(), 0)))

    fun nav(s: MapsParser.Step): String {
        val o = JSONObject().put("t", "nav").put("icon", s.icon).put("text", s.text)
        if (s.distM >= 0) o.put("dist_m", s.distM)
        if (s.eta.isNotEmpty()) o.put("eta", s.eta)
        if (s.leftKm >= 0) o.put("left_km", round(s.leftKm.toDouble(), 1))
        if (s.leftMin >= 0) o.put("left_min", s.leftMin)
        return line(o)
    }

    fun navEnd(): String = line(JSONObject().put("t", "nav").put("end", true))
    fun place(name: String): String = line(JSONObject().put("t", "place").put("name", name.take(120)))
    fun ping(): String = line(JSONObject().put("t", "ping"))
    fun pair(token: String): String = line(JSONObject().put("t", "pair").put("token", token))
    fun result(id: Int, ok: Boolean, text: String): String =
        line(JSONObject().put("t", "result").put("id", id).put("ok", ok).put("text", text.take(120)))
    fun hello(app: String, nonce: String): String = line(JSONObject().put("t", "hello").put("app", app).put("nonce", nonce))
    fun auth(proof: String): String = line(JSONObject().put("t", "auth").put("proof", proof))

    /** A request from the board: "PHONE {...}" (console tag) -> id, what, to. */
    class Request(val id: Int, val what: String, val to: String)

    fun parseRequest(json: String): Request? = try {
        val o = JSONObject(json)
        Request(o.optInt("id"), o.optString("do"), o.optString("to"))
    } catch (_: Exception) {
        null
    }

    private fun round(v: Double, digits: Int): Double {
        var f = 1.0
        repeat(digits) { f *= 10 }
        return Math.round(v * f) / f
    }
}
