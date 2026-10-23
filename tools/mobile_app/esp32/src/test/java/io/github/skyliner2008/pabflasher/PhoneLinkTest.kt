package io.github.skyliner2008.pabflasher

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.CopyOnWriteArrayList

class MapsParserTest {
    @Test
    fun thaiDistanceTitleInstructionText() {
        val s = MapsParser.parse(listOf("350 ม.", "เลี้ยวขวาเข้า ถ.พระราม 4", "12 นาที · 5.4 กม. · ถึง 16:40"))!!
        assertEquals("right", s.icon)
        assertEquals("เลี้ยวขวาเข้า ถ.พระราม 4", s.text)
        assertEquals(350, s.distM)
        assertEquals("16:40", s.eta)
        assertEquals(5.4f, s.leftKm, 0.01f)
        assertEquals(12, s.leftMin)
    }

    @Test
    fun englishInstructionTitleDistanceText() {
        val s = MapsParser.parse(listOf("Turn right onto Rama IV Rd", "1.2 km", "1 hr 5 min · 54 km · 4:40 PM ETA"))!!
        assertEquals("right", s.icon)
        assertEquals("Turn right onto Rama IV Rd", s.text)
        assertEquals(1200, s.distM)
        assertEquals("16:40", s.eta)
        assertEquals(54f, s.leftKm, 0.01f)
        assertEquals(65, s.leftMin)
    }

    @Test
    fun distanceAndInstructionInOneField() {
        val s = MapsParser.parse(listOf("อีก 200 ม. เลี้ยวซ้าย", "ซ.สุขุมวิท 24"))!!
        assertEquals(200, s.distM)
        assertEquals("left", s.icon)
        assertEquals("เลี้ยวซ้าย ซ.สุขุมวิท 24", s.text)  // a bare road name joins the manoeuvre
    }

    @Test
    fun roadNamesWithNumbersAreNotDistances() {
        val s = MapsParser.parse(listOf("80 m", "Head north on Sukhumvit 71 toward Soi 3"))!!
        assertEquals(80, s.distM)
        assertEquals("straight", s.icon)
        val t = MapsParser.parse(listOf("มุ่งหน้าไปทางทิศเหนือ", "500 ม."))!!
        assertEquals(500, t.distM)
        assertEquals("มุ่งหน้าไปทางทิศเหนือ", t.text)
    }

    @Test
    fun iconsFromWords() {
        assertEquals("uturn", MapsParser.icon("กลับรถที่ ถ.วิภาวดีรังสิต"))
        assertEquals("roundabout", MapsParser.icon("ที่วงเวียน ใช้ทางออกที่ 2"))
        assertEquals("keep_right", MapsParser.icon("ชิดขวาเพื่อขึ้นทางด่วน"))
        assertEquals("slight_left", MapsParser.icon("Slight left toward Bang Na"))
        assertEquals("sharp_right", MapsParser.icon("หักขวา"))
        assertEquals("arrive", MapsParser.icon("ถึงที่หมาย เซ็นทรัลเวิลด์ อยู่ทางซ้าย"))
        assertEquals("merge", MapsParser.icon("Merge onto Motorway 7"))
        assertEquals("none", MapsParser.icon("ถ.พระราม 4"))
        // the board knows every key the parser can send
        for (k in listOf("straight", "left", "right", "slight_left", "slight_right", "sharp_left", "sharp_right",
                "uturn", "keep_left", "keep_right", "roundabout", "merge", "arrive", "none")) {
            assertTrue(k, k in setOf("none", "straight", "left", "right", "slight_left", "slight_right", "sharp_left",
                "sharp_right", "uturn", "keep_left", "keep_right", "roundabout", "merge", "arrive", "depart"))
        }
    }

    @Test
    fun notNavigation() {
        assertNull(MapsParser.parse(listOf(null, "", "  ")))
        assertNull(MapsParser.parse(emptyList()))
        // a Maps notification without a step (e.g. "Location sharing") still parses as text - the listener only
        // takes ongoing navigation notifications
        assertNotNull(MapsParser.parse(listOf("กำลังแชร์ตำแหน่ง")))
    }

    @Test
    fun unitsInMetres() {
        assertEquals(1500, MapsParser.metres("1,5", "km"))
        assertEquals(1609, MapsParser.metres("1", "mi"))
        assertEquals(30, MapsParser.metres("100", "ft"))
        assertEquals(800, MapsParser.metres("800", "เมตร"))
    }
}

class PhoneProtocolTest {
    @Test
    fun handshakeProofMatchesTheBoard() {
        // the same vector as host_tests/test_phone.cpp (= Python hmac): both sides compute the same proof
        assertEquals("23a20f99625dd8c9080f6817134f977d7a6999cf45fade74777dacbd7e9e4e5c",
            PhoneProtocol.proof("0123456789abcdef0123", "phone", "00112233445566778899aabbccddeeff"))
        val tok = PhoneProtocol.newToken()
        assertEquals(32, tok.length)
        val n = PhoneProtocol.newNonce()
        assertTrue(PhoneProtocol.validNonce(n))
        val p = PhoneProtocol.proof(tok, "board", n)
        assertTrue(PhoneProtocol.proofOk(tok, "board", n, p))
        assertFalse(PhoneProtocol.proofOk(tok, "phone", n, p))
        assertFalse(PhoneProtocol.proofOk("", "board", n, p))
        assertFalse(PhoneProtocol.validNonce("0123456789ABCDEF"))
    }

    @Test
    fun messagesTheBoardParses() {
        val gps = JSONObject(PhoneProtocol.gps(13.7563123, 100.5018456, 42.46f, 270.4f, 4.2f).removePrefix("phone "))
        assertEquals("gps", gps.getString("t"))
        assertEquals(13.756312, gps.getDouble("lat"), 1e-9)
        assertEquals(42.5, gps.getDouble("spd"), 1e-9)
        val nav = JSONObject(PhoneProtocol.nav(MapsParser.Step("right", "เลี้ยวขวา", 350, "16:40", 5.4f, 12)).removePrefix("phone "))
        assertEquals("right", nav.getString("icon"))
        assertEquals(350, nav.getInt("dist_m"))
        assertEquals("เลี้ยวขวา", nav.getString("text"))
        val unknown = JSONObject(PhoneProtocol.nav(MapsParser.Step("none", "x", -1, "", -1f, -1)).removePrefix("phone "))
        assertFalse(unknown.has("dist_m") || unknown.has("eta") || unknown.has("left_km"))
        assertTrue(PhoneProtocol.navEnd().contains("\"end\":true"))
        val r = PhoneProtocol.parseRequest("{\"id\":3,\"do\":\"navigate\",\"to\":\"เซ็นทรัลเวิลด์\"}")!!
        assertEquals(3, r.id)
        assertEquals("navigate", r.what)
        assertEquals("เซ็นทรัลเวิลด์", r.to)
        assertNull(PhoneProtocol.parseRequest("nope"))
        assertFalse(PhoneProtocol.place("a".repeat(500)).length > 200)
    }
}

class PhoneHubTest {
    private class MemStore : PhoneHub.Store {
        var saved: String? = null
        override fun loadToken() = saved
        override fun saveToken(token: String) { saved = token }
        override var paired = false
    }

    private class FakeActions : PhoneHub.Actions {
        val calls = CopyOnWriteArrayList<String>()
        var fail: String? = null
        override fun navigate(to: String): String? { calls += "navigate:$to"; return fail }
        override fun stop(): String? { calls += "stop"; return null }
    }

    /** The board's side of the Wi-Fi handshake (v2) and sealed lines, as src/phone/PhoneLink.cpp does it. */
    private class FakeBoard(port: Int, token: String) : AutoCloseable {
        val sock = Socket()
        val reader: BufferedReader
        val ok: Boolean
        val helloHadProof: Boolean
        private var key = ByteArray(0)
        private var seqIn = 0L
        private var seqOut = 0L

        init {
            sock.connect(InetSocketAddress("127.0.0.1", port), 2000)
            sock.soTimeout = 3000
            reader = BufferedReader(InputStreamReader(sock.getInputStream(), Charsets.UTF_8))
            val nonce = "0011223344556677"
            write("hello {\"nonce\":\"$nonce\",\"fw\":\"4.3.4\",\"v\":2}")
            val hello = JSONObject(reader.readLine().removePrefix("phone "))
            helloHadProof = hello.has("proof")
            val phoneNonce = hello.getString("nonce")
            write("auth {\"proof\":\"${PhoneProtocol.proof(token, "board", phoneNonce)}\"}")
            val auth = reader.readLine()
            ok = auth != null && PhoneProtocol.proofOk(token, "phone", nonce,
                JSONObject(auth.removePrefix("phone ")).getString("proof"))
            if (ok) key = PhoneProtocol.sessionKey(token, nonce, phoneNonce)
        }

        fun write(s: String) = sock.getOutputStream().write((s + "\n").toByteArray())
        fun writeSealed(s: String) = write(PhoneProtocol.seal(key, 'b', seqOut++, s))
        fun nextRaw(): String = reader.readLine()
        fun next(): String = PhoneProtocol.open(key, 'p', seqIn++, reader.readLine()) ?: error("bad seal")
        override fun close() = sock.close()
    }

    /** The message type of a "phone {...}" line (key order is up to the JSON library). */
    private fun t(line: String) = JSONObject(line.removePrefix("phone ")).getString("t")

    private fun hub(store: MemStore = MemStore(), actions: PhoneHub.Actions = FakeActions(), clock: () -> Long = { System.currentTimeMillis() }) =
        PhoneHub(store, actions, "test", wifiPort = 0, announcePort = 0, now = clock)

    @Test
    fun tokenIsMadeOnceAndKept() {
        val store = MemStore()
        val a = hub(store)
        assertEquals(32, a.token.length)
        assertEquals(a.token, hub(store).token)
    }

    @Test
    fun usbPairsAndCarriesMessages() {
        val h = hub()
        val sent = CopyOnWriteArrayList<String>()
        h.usbConnected { sent += it }
        assertEquals("pair", t(sent[0]))
        assertTrue(sent[0].contains(h.token))
        assertTrue(h.paired)
        assertEquals("usb", h.via)
        h.onGps(13.7, 100.5, 40f, 90f, 5f)
        assertEquals("gps", t(sent.last()))
        h.usbGone()
        assertEquals("none", h.via)
    }

    @Test
    fun boardRequestsRunTheActionsAndAnswer() {
        val acts = FakeActions()
        val h = hub(actions = acts)
        val sent = CopyOnWriteArrayList<String>()
        h.usbConnected { sent += it }
        h.handleRequest("{\"id\":5,\"do\":\"navigate\",\"to\":\"สนามบินสุวรรณภูมิ\"}")
        assertEquals("navigate:สนามบินสุวรรณภูมิ", acts.calls.last())
        val ok = JSONObject(sent.last().removePrefix("phone "))
        assertEquals(5, ok.getInt("id"))
        assertTrue(ok.getBoolean("ok"))
        acts.fail = "ไม่พบ Google Maps บนมือถือ"
        h.handleRequest("{\"id\":6,\"do\":\"navigate\",\"to\":\"x\"}")
        val bad = JSONObject(sent.last().removePrefix("phone "))
        assertFalse(bad.getBoolean("ok"))
        assertEquals("ไม่พบ Google Maps บนมือถือ", bad.getString("text"))
        h.handleRequest("{\"id\":7,\"do\":\"navigate\",\"to\":\"\"}")
        assertFalse(JSONObject(sent.last().removePrefix("phone ")).getBoolean("ok"))
        h.handleRequest("{\"id\":8,\"do\":\"stop\"}")
        assertEquals("stop", acts.calls.last())
    }

    @Test
    fun stepsSentOnChangeAndKeptAlive() {
        var t = 1_000_000L
        val h = hub(clock = { t })
        val sent = CopyOnWriteArrayList<String>()
        h.usbConnected { sent += it }
        val s1 = MapsParser.Step("right", "เลี้ยวขวา", 350, "", -1f, -1)
        h.onStep(s1, "raw")
        val n = sent.size
        t += 1000
        h.onStep(s1, "raw")  // same step, 1 s later: nothing new
        assertEquals(n, sent.size)
        t += 5000
        h.keepAlive()        // 6 s: sent again so the board does not think Maps went quiet
        assertTrue(sent.size > n && t(sent.last()) == "nav")
        h.onStep(s1.copy(distM = 300), "raw")
        assertTrue(sent.last().contains("\"dist_m\":300"))
        h.onStep(null, "")
        assertTrue(sent.last().contains("\"end\":true"))
        val before = sent.size
        h.onStep(null, "")   // ended once
        assertEquals(before, sent.size)
        t += 3000
        h.keepAlive()        // nothing to say: a ping keeps the link
        assertEquals("ping", t(sent.last()))
    }

    @Test
    fun wifiHandshakeThenRequestsBothWays() {
        val acts = FakeActions()
        val h = hub(actions = acts)
        h.startWifi()
        try {
            FakeBoard(h.wifi.boundPort, h.token).use { b ->
                assertFalse("v2: the phone proves nothing before the board did", b.helloHadProof)
                assertTrue("the phone proved the token", b.ok)
                val deadline = System.currentTimeMillis() + 3000
                while (!h.wifi.connected && System.currentTimeMillis() < deadline) Thread.sleep(10)
                assertEquals("wifi", h.via)
                h.onGps(13.7, 100.5, 30f, 0f, 3f)
                assertEquals("gps", t(b.next()))
                b.writeSealed("PHONE {\"id\":1,\"do\":\"navigate\",\"to\":\"บ้าน\"}")
                val result = JSONObject(b.next().removePrefix("phone "))
                assertEquals(1, result.getInt("id"))
                assertEquals("navigate:บ้าน", acts.calls.last())
                // a line someone on the Wi-Fi slipped in (no seal): the phone drops the connection, runs nothing
                b.write("PHONE {\"id\":2,\"do\":\"navigate\",\"to\":\"ที่อื่น\"}")
                val gone = System.currentTimeMillis() + 3000
                while (h.wifi.connected && System.currentTimeMillis() < gone) Thread.sleep(10)
                assertFalse(h.wifi.connected)
                assertEquals("navigate:บ้าน", acts.calls.last())
            }
        } finally {
            h.stopWifi()
        }
    }

    @Test
    fun wifiBoardWithAnotherTokenIsRefused() {
        val h = hub()
        h.startWifi()
        try {
            FakeBoard(h.wifi.boundPort, PhoneProtocol.newToken()).use { b ->
                assertFalse("a board paired elsewhere gets no proof from the phone", b.ok)
                Thread.sleep(300)
                assertFalse("and the phone does not accept the board", h.wifi.connected)
                assertTrue(h.recentEvents().any { it.contains("prove") })
            }
        } finally {
            h.stopWifi()
        }
    }

    @Test
    fun oldBoardFirmwareIsToldToUpdate() {
        val h = hub()
        h.startWifi()
        try {
            Socket().use { s ->
                s.connect(InetSocketAddress("127.0.0.1", h.wifi.boundPort), 2000)
                s.soTimeout = 3000
                s.getOutputStream().write("hello {\"nonce\":\"0011223344556677\",\"fw\":\"4.3.3\"}\n".toByteArray())
                // v1 boards expect a proof right away: the phone gives none and closes
                assertNull(BufferedReader(InputStreamReader(s.getInputStream())).readLine())
            }
            Thread.sleep(200)
            assertTrue(h.recentEvents().any { it.contains("too old") })
        } finally {
            h.stopWifi()
        }
    }

    @Test
    fun sealsMatchTheBoard() {
        // the same vectors as host_tests/test_phone.cpp phone_wifi_lines_are_sealed (= Python hmac)
        val key = PhoneProtocol.sessionKey("0123456789abcdef0123", "00112233445566778899aabbccddeeff",
            "ffeeddccbbaa99887766554433221100")
        assertEquals("2962748d6e1b37b70a5bf326e5251638e92a7d3888018b38b1c1142cb80116d2",
            key.joinToString("") { "%02x".format(it) })
        assertEquals("phone {\"t\":\"ping\"}\t0.a7942863f0ec065a44a29d4b0846c9d9",
            PhoneProtocol.seal(key, 'p', 0, "phone {\"t\":\"ping\"}"))
        val board = "PHONE {\"id\":3,\"do\":\"stop\"}\t7.2483b741d9fcb11a6d7efe97a8fd88cc"
        assertEquals("PHONE {\"id\":3,\"do\":\"stop\"}", PhoneProtocol.open(key, 'b', 7, board))
        assertNull(PhoneProtocol.open(key, 'b', 8, board))   // replayed / reordered
        assertNull(PhoneProtocol.open(key, 'p', 7, board))   // the wrong direction
        assertNull(PhoneProtocol.open(key, 'b', 7, board.replace("stop", "stap")))
        assertNull(PhoneProtocol.open(key, 'b', 0, "PHONE {}"))
    }
}
