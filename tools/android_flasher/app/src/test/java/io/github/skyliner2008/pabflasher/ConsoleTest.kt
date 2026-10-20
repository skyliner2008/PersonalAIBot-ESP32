package io.github.skyliner2008.pabflasher

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class ConsoleTest {
    @Test
    fun tags() {
        val d = Console.parseTag("DIAG {\"state\":\"idle\"}")!!
        assertEquals("DIAG", d.tag)
        assertEquals("{\"state\":\"idle\"}", d.json)
        val t = Console.parseTag("TOOL get_weather {\"ok\":true}")!!
        assertEquals("get_weather", t.name)
        assertEquals("[]", Console.parseTag("WIFISCAN []")!!.json)
        assertNull(Console.parseTag("[Agent] DIAG {x}"))
        assertNull(Console.parseTag("CONFIG"))
        assertNull(Console.parseTag("CONFIG not json"))
        assertNull(Console.parseTag("SOMETHING {}"))
    }

    @Test
    fun classifyLikeStudio() {
        assertEquals(Console.Kind.ERR, Console.classify("E (1234) wifi: failed"))
        assertEquals(Console.Kind.WARN, Console.classify("[Fallback] model x quota"))
        assertEquals(Console.Kind.USER, Console.classify("[Chat] user: สวัสดี"))
        assertEquals(Console.Kind.CHAT, Console.classify("[Chat] ai: สวัสดีค่ะ"))
        assertEquals(Console.Kind.GEMINI, Console.classify("[Gemini] setup complete"))
        assertEquals(Console.Kind.WIFI, Console.classify("[WiFi] got IP"))
        assertEquals(Console.Kind.TOUCH, Console.classify("[Touch] 10,20"))
        assertEquals(Console.Kind.HEALTH, Console.classify("[Health] heap 100"))
        assertEquals(Console.Kind.OK, Console.classify("OK set volume (saved to NVS)"))
        assertEquals(Console.Kind.MIC, Console.classify("[Mic] level -40 dBFS"))
        assertEquals(Console.Kind.WARN, Console.classify("[Mic] ⚠ clipping"))
        assertEquals(Console.Kind.REPLY, Console.classify("CONFIG {}"))
        assertEquals(Console.Kind.INFO, Console.classify("[Agent] state idle"))
    }

    @Test
    fun secretsNeverShown() {
        assertEquals("set key ***", Console.echo("set key AIzaSyABCDEFGHIJKLMNOPQRSTUVWX"))
        assertEquals("set pass ***", Console.echo("set  pass my secret pass"))
        assertEquals("set ssid Home", Console.echo("set ssid Home"))
        assertEquals("GET /v1?key=***&x=1", Console.mask("GET /v1?key=AIzaSy123&x=1"))
        assertEquals("ask hi there", Console.clean("ask hi\nthere"))
    }

    @Test
    fun chatLines() {
        assertEquals(true to "เปิดไฟ", Console.chat("[Chat] user: เปิดไฟ"))
        assertEquals(false to "ได้ค่ะ", Console.chat("[Chat] ai: ได้ค่ะ"))
        assertNull(Console.chat("[Chat] turn complete (tokens/min 10)"))
    }
}

class LineStreamTest {
    private fun lines(ev: List<LineStream.Event>) = ev.filterIsInstance<LineStream.Event.Line>().map { it.text }

    @Test
    fun splitsAcrossChunksAndThaiBytes() {
        val s = LineStream()
        val bytes = "[Chat] ai: สวัสดีค่ะ\r\nDIAG {\"a\":1}\n".toByteArray()
        val out = ArrayList<LineStream.Event>()
        for (b in bytes) out += s.feed(byteArrayOf(b))  // one byte at a time: a Thai character split in three
        assertEquals(listOf("[Chat] ai: สวัสดีค่ะ", "DIAG {\"a\":1}"), lines(out))
    }

    @Test
    fun skipsBinaryReplies() {
        val s = LineStream()
        val pixels = ByteArray(300) { (it % 256).toByte() }.also { it[10] = '\n'.code.toByte() }
        val data = "before\nSCREENSHOT 10 15 300\n".toByteArray() + pixels + "\nEND\nafter\n".toByteArray()
        val out = s.feed(data.copyOfRange(0, 50)) + s.feed(data.copyOfRange(50, data.size))
        assertEquals(listOf("before", "after"), lines(out))
        val blob = out.filterIsInstance<LineStream.Event.Blob>().single()
        assertTrue(blob.ok)
    }

    @Test
    fun brokenBinaryResyncsOnTheTrailer() {
        val s = LineStream()
        val data = "RECORD 16000 8\n".toByteArray() + ByteArray(8) + "[Agent] interleaved\n\nEND\nnext\n".toByteArray()
        val out = s.feed(data)
        assertFalse(out.filterIsInstance<LineStream.Event.Blob>().single().ok)
        assertEquals(listOf("next"), lines(out))
    }

    @Test
    fun blobHeaders() {
        assertEquals(153600, LineStream.blobSize("SCREENSHOT 320 240 153600"))
        assertEquals(64000, LineStream.blobSize("RECORD 16000 64000"))
        assertEquals(1000, LineStream.blobSize("RECPAIRS 16000 1000"))
        assertNull(LineStream.blobSize("SCREENSHOT done"))
        assertNull(LineStream.blobSize("[Agent] RECORD 1 2"))
    }

    @Test
    fun endAndBlankLinesAreDropped() {
        assertEquals(listOf("x"), lines(LineStream().feed("\n\r\nEND\nx\n".toByteArray())))
    }
}

class SettingsFormTest {
    private val options = BoardOptions("""
        {"profiles":[{"id":"friend","name":"น้องฟ้า","voice":"Kore","gender":"female","personality":"warm","eye":"cyan","summary":"เพื่อนคุย"}],
         "voices":[{"name":"Kore","gender":"female","style":"สดใส"},{"name":"Puck","gender":"male","style":"ร่าเริง"}],
         "personalities":[{"id":"warm","name":"อบอุ่น"},{"id":"pro","name":"มืออาชีพ"}],
         "genders":[{"id":"female","name":"หญิง (ค่ะ)"},{"id":"male","name":"ชาย (ครับ)"}],
         "eyes":[{"id":"cyan","name":"ฟ้า","rgb":"#00FFFF"},{"id":"pink","name":"ชมพู","rgb":"#FF66CC"}],
         "liveModels":[{"id":"a","label":"Model A"},{"id":"b","label":"Model B"}],
         "textModels":["t1","t2"],"thinking":["off","low","high"]}
    """.trimIndent())

    private val config = JSONObject("""
        {"fw":"4.2.0","ssid":"Home","passSet":true,"keySet":true,"agent":"เอไอ","user":"พี่","voice":"Puck",
         "profile":"custom","gender":"male","personality":"pro","eye":"pink","model":1,"thinking":0,"textmodel":0,
         "volume":40,"aivolume":80,"brightness":70,"sleep":3,"clock":5,"dimlevel":8,"rotation":1,"consolidate":true,
         "search":false,"micgain":5,"wake":2,"finance":true}
    """.trimIndent())

    @Test
    fun optionsParse() {
        assertEquals("Kore — หญิง (ค่ะ) — สดใส", options.voices[0].label)
        assertEquals(2, options.liveModels.size)
        assertEquals(listOf("off", "low", "high"), options.thinking)
    }

    @Test
    fun nothingChangedSendsNothing() {
        val base = SettingsForm.fromConfig(config)
        assertEquals(emptyList<String>(), SettingsForm.commands(base, base, options))
        assertEquals("1", base["model"])
        assertEquals("0", base["search"])
    }

    @Test
    fun onlyChangedFieldsAreSent() {
        val base = SettingsForm.fromConfig(config)
        val now = base + mapOf("volume" to "55", "search" to "1", "user" to "")
        assertEquals(listOf("set search 1", "set volume 55"), SettingsForm.commands(base, now, options))
    }

    @Test
    fun aProfileGoesFirstAndCoversWhatItSets() {
        val base = SettingsForm.fromConfig(config)
        val now = base + mapOf("profile" to "friend", "agent" to "น้องฟ้า", "voice" to "Kore", "personality" to "warm",
            "gender" to "female", "eye" to "cyan")
        assertEquals(listOf("set profile friend"), SettingsForm.commands(base, now, options))
        // picked the profile, then changed the eye: the eye goes after it
        val eye = now + ("eye" to "pink")
        assertEquals(listOf("set profile friend", "set eye pink"), SettingsForm.commands(base, eye, options))
    }

    @Test
    fun valuesStayOneLine() {
        val base = SettingsForm.fromConfig(config)
        assertEquals(listOf("set agent ฟ้า ใส"), SettingsForm.commands(base, base + ("agent" to "ฟ้า\nใส"), options))
    }

    @Test
    fun statusCardInThai() {
        val d = JSONObject("""{"fw":"4.2.0","state":"idle","session":"ready","wifi":true,"rssi":-55,"ip":"192.168.1.9",
            "active_model":"b","preferred_model":"a","search_state":"quota","muted":true,"radio":"playing",
            "radio_station":"Cool 93","tokens_per_min":1200,"uptime_s":7300,"heap_kb":80}""")
        val card = BoardPage.describe(d).joinToString("\n")
        for (want in listOf("พร้อมคุย", "-55 dBm", "192.168.1.9", "โมเดล: b (หลัก a)", "โควตาหมด", "ปิดไมค์",
                "วิทยุ: กำลังเล่น — Cool 93", "1200", "2 ชม. 1 นาที", "RAM ว่าง 80 KB", "4.2.0")) {
            assertTrue("missing '$want' in:\n$card", card.contains(want))
        }
        assertTrue(BoardPage.describe(JSONObject("{\"wifi\":false}")).any { "ยังไม่เชื่อมต่อ" in it })
    }

    @Test
    fun apiTestInThai() {
        val r = SettingsPage.describeApiTest(JSONObject("""{"keyLen":39,"models":[{"id":"m1","http":200}],
            "generate":[{"id":"m1","http":200},{"id":"live-x","http":400},{"id":"m2","http":429,"error":{"status":"RESOURCE_EXHAUSTED","message":"quota"}}],
            "search":{"http":200}}"""))
        for (want in listOf("39 ตัวอักษร", "✔ มองเห็น m1", "✔ เรียก m1 ได้", "เฉพาะ Live", "✘ m2 HTTP 429 RESOURCE_EXHAUSTED", "Search ใช้ได้")) {
            assertTrue("missing '$want' in:\n$r", r.contains(want))
        }
        assertEquals("ทดสอบไม่ได้: no key", SettingsPage.describeApiTest(JSONObject("{\"error\":\"no key\"}")))
    }
}

class BoardLinkTest {
    /** A board on the other end of the cable: what it prints, what we wrote, unplugging. */
    private class FakeBoard : SerialPort {
        val incoming = LinkedBlockingQueue<ByteArray>()
        val written = StringBuilder()
        @Volatile var unplugged = false
        var lineSets = 0

        override fun write(data: ByteArray, timeoutMs: Int) {
            if (unplugged) throw IOException("gone")
            synchronized(written) { written.append(String(data)) }
        }

        override fun read(buf: ByteArray, timeoutMs: Int): Int {
            if (unplugged) throw IOException("gone")
            val b = incoming.poll(timeoutMs.toLong(), TimeUnit.MILLISECONDS) ?: return 0
            b.copyInto(buf)
            return b.size
        }

        override fun setLines(dtr: Boolean, rts: Boolean) { lineSets++ }
        override fun close() = Unit
    }

    @Test
    fun readsLinesWritesCommandsNeverResets() {
        val board = FakeBoard()
        val got = CopyOnWriteArrayList<String>()
        val lost = LinkedBlockingQueue<IOException>()
        val link = BoardLink(board, { if (it is LineStream.Event.Line) got += it.text }, { lost += it })
        link.start()
        board.incoming += "[Agent] boot\nCONF".toByteArray()
        board.incoming += "IG {\"fw\":\"4.2.0\"}\n".toByteArray()
        link.send("set ssid Home\nreboot")  // one line: the newline must not become a second command
        val deadline = System.currentTimeMillis() + 3000
        while (got.size < 2 && System.currentTimeMillis() < deadline) Thread.sleep(10)
        assertEquals(listOf("[Agent] boot", "CONFIG {\"fw\":\"4.2.0\"}"), got)
        assertEquals("set ssid Home reboot\n", synchronized(board.written) { board.written.toString() })
        assertEquals("opening the console must not touch DTR/RTS (it would reset the board)", 0, board.lineSets)
        board.unplugged = true
        assertTrue(lost.poll(3, TimeUnit.SECONDS) != null)
        link.close()
    }
}
