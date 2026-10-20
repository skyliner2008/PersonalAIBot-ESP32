package io.github.skyliner2008.pabflasher

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File
import java.io.IOException
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.Base64
import kotlin.random.Random

class SlipTest {
    @Test
    fun roundTripWithEscapesAndNoise() {
        val payload = byteArrayOf(1, 0xC0.toByte(), 2, 0xDB.toByte(), 0xDC.toByte(), 0xDD.toByte(), 3)
        val enc = Slip.encode(payload)
        assertEquals(0xC0.toByte(), enc.first())
        assertEquals(0xC0.toByte(), enc.last())
        assertFalse(enc.copyOfRange(1, enc.size - 1).contains(0xC0.toByte()))
        val d = Slip.Decoder()
        var got: ByteArray? = null
        for (b in "boot log text\r\n".toByteArray() + enc) d.feed(b)?.let { got = it }
        assertArrayEquals(payload, got)
    }

    @Test
    fun backToBackDelimitersStartANewFrame() {
        val d = Slip.Decoder()
        val frames = mutableListOf<ByteArray>()
        for (b in byteArrayOf(0xC0.toByte(), 0xC0.toByte(), 5, 6, 0xC0.toByte())) d.feed(b)?.let { frames += it }
        assertEquals(1, frames.size)
        assertArrayEquals(byteArrayOf(5, 6), frames[0])
    }
}

class ProtocolTest {
    @Test
    fun requestLayout() {
        val p = Esp.request(Esp.FLASH_DEFL_DATA, byteArrayOf(9, 8, 7), 0x1234)
        assertEquals(0, p[0].toInt())
        assertEquals(Esp.FLASH_DEFL_DATA, p[1].toInt())
        assertEquals(3, Esp.u16(p, 2))
        assertEquals(0x1234, Esp.u32(p, 4))
        assertEquals(11, p.size)
    }

    @Test
    fun checksumIsXorFromMagic() {
        assertEquals(0xEF, Esp.checksum(ByteArray(0)))
        assertEquals(0xEF xor 0x01 xor 0xFF, Esp.checksum(byteArrayOf(1, 0xFF.toByte())))
        assertEquals(0xEF xor 0xFF, Esp.checksum(byteArrayOf(1, 0xFF.toByte()), 1, 2))
    }

    @Test
    fun timeoutsScaleLikeEsptool() {
        assertEquals(3000L, EspLoader.perMb(EspLoader.ERASE_MS_PER_MB, 4096))
        assertEquals(440_000L, EspLoader.perMb(EspLoader.ERASE_MS_PER_MB, 11_000_000))
    }

    @Test
    fun pad4UsesErasedBytes() {
        assertEquals(4, EspLoader.pad4(byteArrayOf(1)).size)
        assertEquals(0xFF.toByte(), EspLoader.pad4(byteArrayOf(1))[3])
        assertEquals(8, EspLoader.pad4(ByteArray(8)).size)
    }
}

/** Synthetic firmware with the headers the checks look at. */
object Images {
    fun app(size: Int, seed: Int = 1): ByteArray {
        val r = Random(seed)
        val b = ByteArray(size) { if (it % 7 == 0) r.nextInt().toByte() else (it / 512).toByte() }
        header(b, 0)
        return b
    }

    fun factory(appSize: Int): ByteArray {
        val b = ByteArray(Layout.APP0 + appSize) { 0xFF.toByte() }
        header(b, 0)  // bootloader
        b[Layout.PARTITION_TABLE] = 0xAA.toByte()
        b[Layout.PARTITION_TABLE + 1] = 0x50
        FlashPlan.bootApp0().copyInto(b, Layout.OTADATA)
        app(appSize, 7).copyInto(b, Layout.APP0)
        return b
    }

    fun header(b: ByteArray, at: Int, chip: Int = Esp.ESP32S3_CHIP_ID) {
        b[at] = 0xE9.toByte()
        b[at + 1] = 3
        b[at + 12] = chip.toByte()
        b[at + 13] = 0
    }
}

class FlashPlanTest {
    @Test
    fun detectsByContent() {
        assertEquals(Kind.APP, FlashPlan.detect(Images.app(4096)))
        assertEquals(Kind.FACTORY, FlashPlan.detect(Images.factory(4096)))
        assertNull(FlashPlan.detect("hello".toByteArray()))
        assertNull(FlashPlan.detect(ByteArray(100000)))
    }

    @Test
    fun refusesWrongFiles() {
        assertNull(FlashPlan.problem(Kind.APP, Images.app(100_000)))
        assertNull(FlashPlan.problem(Kind.FACTORY, Images.factory(100_000)))
        assertNotNull(FlashPlan.problem(Kind.APP, Images.factory(100_000)))
        assertNotNull(FlashPlan.problem(Kind.FACTORY, Images.app(100_000)))
        assertNotNull(FlashPlan.problem(Kind.APP, Images.app(Layout.APP_SIZE + 4)))
        assertNotNull(FlashPlan.problem(Kind.FACTORY, Images.factory(Layout.LITTLEFS - Layout.APP0 + 4)))
        val c3 = Images.app(4096).also { Images.header(it, 0, chip = 5) }
        assertNotNull(FlashPlan.problem(Kind.APP, c3))
        assertNotNull(FlashPlan.problem(Kind.APP, "not firmware".toByteArray()))
    }

    @Test
    fun updateKeepsSettingsAndBootsApp0() {
        val r = FlashPlan.regions(Kind.APP, Images.app(4096))
        assertEquals(listOf(Layout.OTADATA, Layout.APP0), r.map { it.offset })
        val ota = r[0].data
        assertEquals(Layout.OTADATA_SIZE, ota.size)
        assertEquals(1, Esp.u32(ota, 0))
        assertEquals(0, Esp.u32(ota, 4096))
        assertEquals(listOf(0), FlashPlan.regions(Kind.FACTORY, Images.factory(4096)).map { it.offset })
    }

    /** The release files, when they are around: REAL_RELEASE_DIR=<dir with app.bin + factory.bin>. */
    @Test
    fun realReleaseFiles() {
        val dir = System.getenv("REAL_RELEASE_DIR") ?: return
        val app = File(dir, "app.bin").readBytes()
        val factory = File(dir, "factory.bin").readBytes()
        assertEquals(Kind.APP, FlashPlan.detect(app))
        assertEquals(Kind.FACTORY, FlashPlan.detect(factory))
        assertNull(FlashPlan.problem(Kind.APP, app))
        assertNull(FlashPlan.problem(Kind.FACTORY, factory))
        // our otadata is byte for byte the framework's boot_app0.bin the factory image carries
        assertArrayEquals(factory.copyOfRange(Layout.OTADATA, Layout.OTADATA + Layout.OTADATA_SIZE), FlashPlan.bootApp0())
        assertArrayEquals(app, factory.copyOfRange(Layout.APP0, Layout.APP0 + app.size))
    }
}

class FlashJobTest {
    private class Recorder : FlashJob.Listener {
        val statuses = mutableListOf<String>()
        var last = 0L to 0L
        override fun status(text: String) { statuses += text }
        override fun progress(done: Long, total: Long) {
            assertTrue("progress goes forward", done >= last.first || total != last.second)
            last = done to total
        }
    }

    private fun job(rom: FakeRom, rec: Recorder = Recorder(), provider: PortProvider = PortProvider { rom }) =
        FlashJob(provider, rec, sleep = { rom.clock += it }, now = { rom.clock })

    @Test
    fun resetSequenceIsEsptools() {
        val rom = FakeRom()
        EspLoader(rom, sleep = { rom.clock += it }, now = { rom.clock }).resetToBootloader()
        // usb_jtag_bootloader_reset: idle, GPIO0 low, reset through (1,1), release
        assertEquals(listOf(false to false, false to false, true to false, true to false, true to true,
            false to true, false to true, false to true, false to false), rom.lines)
        assertTrue(rom.inDownload)
    }

    @Test
    fun factoryImageLandsByteForByte() {
        val rom = FakeRom()
        val image = Images.factory(300_000)
        val rec = Recorder()
        job(rom, rec).run(Kind.FACTORY, image)
        assertArrayEquals(image, rom.flash.copyOfRange(0, image.size))
        assertEquals(image.size.toLong() to image.size.toLong(), rec.last)
        assertFalse("left the download mode and booted the app", rom.inDownload)
        assertEquals(1, rom.appResets)
        assertTrue(rom.ops.contains(Esp.SPI_FLASH_MD5))
    }

    @Test
    fun updateKeepsNvsAndMemory() {
        val rom = FakeRom()
        val nvs = ByteArray(0x5000) { (it * 3).toByte() }
        nvs.copyInto(rom.flash, 0x9000)
        val memory = "memory on LittleFS".toByteArray()
        memory.copyInto(rom.flash, Layout.LITTLEFS)
        Images.app(50_000, seed = 3).copyInto(rom.flash, 0x510000)  // an older OTA in app1
        val app = Images.app(777_777)
        job(rom).run(Kind.APP, app)
        assertArrayEquals(app, rom.flash.copyOfRange(Layout.APP0, Layout.APP0 + app.size))
        assertArrayEquals(FlashPlan.bootApp0(), rom.flash.copyOfRange(Layout.OTADATA, Layout.OTADATA + Layout.OTADATA_SIZE))
        assertArrayEquals(nvs, rom.flash.copyOfRange(0x9000, 0xE000))
        assertArrayEquals(memory, rom.flash.copyOfRange(Layout.LITTLEFS, Layout.LITTLEFS + memory.size))
    }

    @Test
    fun blocksOfErasedPaddingGetTimeToWrite() {
        // a factory image ends in megabytes of 0xFF (gaps, the model partition): one 1 KB block inflates to ~1 MB
        val rom = FakeRom()
        val image = Images.factory(200_000).copyOf(3 * 1024 * 1024)
        image.fill(0xFF.toByte(), Layout.APP0 + 200_000)
        job(rom).run(Kind.FACTORY, image)
        assertArrayEquals(image, rom.flash.copyOfRange(0, image.size))
        assertTrue("virtual time passed for the writes", rom.clock > 20_000)
    }

    @Test
    fun rejectedBlockIsSentAgain() {
        val rom = FakeRom().apply { rejectSeqOnce = 3 }
        val app = Images.app(200_000)
        job(rom).run(Kind.APP, app)
        assertArrayEquals(app, rom.flash.copyOfRange(Layout.APP0, Layout.APP0 + app.size))
        assertEquals(-1, rom.rejectSeqOnce)
    }

    @Test
    fun boardThatReEnumeratesAfterTheReset() {
        val rom = FakeRom().apply { dropOnBootReset = true }
        var opens = 0
        val app = Images.app(20_000)
        job(rom, provider = { opens++; rom.gone = false; rom }).run(Kind.APP, app)
        assertEquals(2, opens)
        assertArrayEquals(app, rom.flash.copyOfRange(Layout.APP0, Layout.APP0 + app.size))
    }

    @Test
    fun alreadyInDownloadModeByHand() {
        val rom = FakeRom().apply { inDownload = true; ignoreLines = true }
        val app = Images.app(20_000)
        job(rom).run(Kind.APP, app)
        assertArrayEquals(app, rom.flash.copyOfRange(Layout.APP0, Layout.APP0 + app.size))
    }

    @Test
    fun boardThatNeverEntersDownloadMode() {
        val rom = FakeRom().apply { ignoreLines = true }
        try {
            job(rom).run(Kind.APP, Images.app(20_000))
            fail("must not pretend it flashed")
        } catch (e: EspException) {
            assertEquals(FlashJob.NOT_IN_DOWNLOAD_MODE, e.message)
        }
        assertTrue(rom.flash.copyOfRange(Layout.APP0, Layout.APP0 + 16).all { it == 0xFF.toByte() })
    }

    @Test
    fun otherChipIsRefusedBeforeAnyErase() {
        val rom = FakeRom(chipId = 5)  // ESP32-C3
        try {
            job(rom).run(Kind.APP, Images.app(20_000))
            fail("flashed an S3 image into another chip")
        } catch (e: EspException) {
            assertTrue(e.message!!.contains("ESP32-S3"))
        }
        assertFalse(rom.ops.contains(Esp.FLASH_DEFL_BEGIN))
    }

    @Test
    fun wrongFileNeverTouchesTheBoard() {
        val rom = FakeRom()
        try {
            job(rom).run(Kind.APP, Images.factory(20_000))
            fail()
        } catch (e: EspException) {
            assertTrue(rom.lines.isEmpty())
        }
    }

    @Test
    fun unpluggedMidFlashIsAnIoError() {
        val rom = FakeRom()
        var calls = 0
        val port = object : SerialPort by rom {
            override fun write(data: ByteArray, timeoutMs: Int) {
                if (++calls == 40) throw IOException("unplugged")
                rom.write(data, timeoutMs)
            }
        }
        try {
            FlashJob({ port }, Recorder(), sleep = { rom.clock += it }, now = { rom.clock }).run(Kind.APP, Images.app(300_000))
            fail()
        } catch (e: IOException) {
            assertEquals("unplugged", e.message)
        }
    }
}

class ReleasesTest {
    private val json = """
        {"tag_name":"v4.2.0","name":"PersonalAIBot-ESP32 v4.2.0","assets":[
          {"name":"PersonalAIBot-ESP32-v4.2.0-app.bin","browser_download_url":"https://github.com/x/releases/download/v4.2.0/PersonalAIBot-ESP32-v4.2.0-app.bin"},
          {"name":"PersonalAIBot-ESP32-v4.2.0-app.bin.sha256","browser_download_url":"https://github.com/x/a.sha256"},
          {"name":"PersonalAIBot-ESP32-v4.2.0-factory.bin","browser_download_url":"https://github.com/x/f.bin"},
          {"name":"PersonalAIBot-ESP32-v4.2.0-factory.bin.sha256","browser_download_url":"https://github.com/x/f.sha256"}]}
    """.trimIndent()

    @Test
    fun parsesTheLatestRelease() {
        val r = Releases.parse(json)
        assertEquals("v4.2.0", r.tag)
        assertEquals("PersonalAIBot-ESP32-v4.2.0-app.bin", r.fileName(Kind.APP))
        assertEquals("PersonalAIBot-ESP32-v4.2.0-factory.bin", r.fileName(Kind.FACTORY))
        assertEquals("https://github.com/x/a.sha256", r.url("PersonalAIBot-ESP32-v4.2.0-app.bin.sha256"))
        assertNull(Releases.parse("""{"tag_name":"v1","assets":[]}""").fileName(Kind.APP))
    }

    @Test
    fun sha256Files() {
        val h = "9faf0c08733d702b702e1472b80853d2e46e3f1fcaad9006c41fe1f8cb30205f"
        assertEquals(h, Releases.parseSha256("$h  PersonalAIBot-ESP32-v4.2.0-app.bin\n", "PersonalAIBot-ESP32-v4.2.0-app.bin"))
        assertEquals(h, Releases.parseSha256("$h *a.bin", "a.bin"))
        assertEquals(h, Releases.parseSha256(h.uppercase(), "a.bin"))
        assertNull(Releases.parseSha256("$h  other.bin", "a.bin"))
        assertNull(Releases.parseSha256("not a hash  a.bin", "a.bin"))
        assertNull(Releases.parseSha256("", "a.bin"))
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            Releases.sha256Hex("abc".toByteArray()))
    }

    @Test
    fun otaSignatureLikeTheBoard() {
        val kp = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()
        val pem = "-----BEGIN PUBLIC KEY-----\n" +
            Base64.getMimeEncoder(64, "\n".toByteArray()).encodeToString(kp.public.encoded) + "\n-----END PUBLIC KEY-----\n"
        val data = Images.app(10_000)
        val sig = Signature.getInstance("SHA256withECDSA").run { initSign(kp.private); update(data); sign() }
        val b64 = Base64.getEncoder().encodeToString(sig) + "\n"  // what make_release.py writes
        assertTrue(Releases.signatureOk(data, b64, pem))
        data[100] = (data[100] + 1).toByte()
        assertFalse(Releases.signatureOk(data, b64, pem))
        assertFalse(Releases.signatureOk(data, "garbage", pem))
        assertFalse(Releases.signatureOk(data, b64, "not a key"))
    }
}
