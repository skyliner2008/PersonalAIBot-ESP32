package io.github.skyliner2008.pabflasher

import java.security.MessageDigest
import java.util.zip.Deflater

/**
 * Talks to the ESP32-S3 ROM bootloader over the board's own USB port (USB-Serial-JTAG, 303A:1001):
 * reset into the download mode, sync, write compressed data and check it with the ROM's MD5.
 * No flasher stub is uploaded: the S3 ROM already takes compressed (zlib) blocks of 1 KB.
 *
 * The timing follows esptool (reset pulse train, timeouts per MB) so the board behaves the same as with it.
 */
class EspLoader(
    private val port: SerialPort,
    private val sleep: (Long) -> Unit = { Thread.sleep(it) },
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
) {
    private val decoder = Slip.Decoder()
    private val rx = ByteArray(4096)
    private var rxLen = 0
    private var rxPos = 0
    private var dtr = false
    private var rts = false

    private fun setDtr(v: Boolean) { dtr = v; port.setLines(dtr, rts) }
    private fun setRts(v: Boolean) { rts = v; port.setLines(dtr, rts) }

    /** esptool's USB-Serial-JTAG sequence: it walks through (DTR, RTS) = (1,1), never (0,0) in the middle. */
    fun resetToBootloader() {
        setRts(false); setDtr(false)  // idle
        sleep(100)
        setDtr(true); setRts(false)  // GPIO0 low
        sleep(100)
        setRts(true); setDtr(false); setRts(true)  // reset
        sleep(100)
        setDtr(false); setRts(false)  // out of reset, the ROM waits for download
    }

    /** Leave the download mode and run the new firmware (RTS = chip reset with GPIO0 high). */
    fun hardReset() {
        setDtr(false)
        setRts(true)
        sleep(200)
        setRts(false)
        sleep(200)
    }

    /** Reset (unless the board already waits after BOOT + RESET) and sync with the ROM. */
    fun connect(attempts: Int = 4, reset: Boolean = true): Boolean {
        repeat(attempts) {
            if (reset) resetToBootloader()
            flushInput()
            if (sync()) return true
        }
        return false
    }

    fun sync(): Boolean {
        repeat(5) {
            try {
                command(Esp.SYNC, Esp.SYNC_DATA, timeoutMs = SYNC_TIMEOUT)
                while (readFrame(50) != null) Unit  // the ROM answers a sync 8 times
                return true
            } catch (_: EspException) {
            }
        }
        return false
    }

    /** 9 = ESP32-S3; null = the ROM did not say (the flash still works, the caller decides). */
    fun chipId(): Int? = try {
        Esp.u32(check("read the chip type", Esp.GET_SECURITY_INFO, ByteArray(0), respLen = 20).data, 12)
    } catch (_: EspException) {
        flushInput()
        null
    }

    fun attachFlash(sizeBytes: Int) {
        check("attach the flash", Esp.SPI_ATTACH, Esp.words(0, 0))
        check("set the flash size", Esp.SPI_SET_PARAMS, Esp.words(0, sizeBytes, 64 * 1024, 4 * 1024, 256, 0xFFFF))
    }

    /** Erase + write [image] at [offset] and verify it with the ROM's MD5. [progress] gets (sent, total) bytes. */
    fun writeFlash(offset: Int, image: ByteArray, progress: (Int, Int) -> Unit = { _, _ -> }) {
        val data = pad4(image)
        val z = deflate(data)
        val w = Esp.FLASH_WRITE_SIZE
        val blocks = (z.size + w - 1) / w
        val eraseSize = (data.size + w - 1) / w * w  // the ROM wants it rounded up to the block size
        check("erase 0x%x".format(offset), Esp.FLASH_DEFL_BEGIN, Esp.words(eraseSize, blocks, w, offset, 0),
            timeoutMs = perMb(ERASE_MS_PER_MB, eraseSize))
        // a block's timeout follows what it inflates to (1 KB of erased padding can be ~1 MB), like esptool
        val inflated = java.util.zip.Inflater()
        val scratch = ByteArray(64 * 1024)
        for (seq in 0 until blocks) {
            val from = seq * w
            val to = minOf(from + w, z.size)
            val packet = Esp.words(to - from, seq, 0, 0) + z.copyOfRange(from, to)
            inflated.setInput(z, from, to - from)
            var out = 0
            while (true) {
                val n = inflated.inflate(scratch)
                if (n == 0) break
                out += n
            }
            val timeout = perMb(ERASE_MS_PER_MB, out) + 1000
            var attempt = 0
            while (true) {
                try {
                    check("write block $seq", Esp.FLASH_DEFL_DATA, packet, Esp.checksum(z, from, to), timeoutMs = timeout)
                    break
                } catch (e: EspException) {
                    if (++attempt >= WRITE_ATTEMPTS) throw e
                }
            }
            progress(to, z.size)
        }
        inflated.end()
        val want = md5Hex(data)
        val got = md5(offset, data.size)
        if (got != want) throw EspException("ตรวจสอบไม่ผ่านที่ 0x%x (MD5 %s ≠ %s)".format(offset, got, want))
    }

    fun md5(offset: Int, size: Int): String {
        val r = check("read MD5", Esp.SPI_FLASH_MD5, Esp.words(offset, size, 0, 0), respLen = 32,
            timeoutMs = perMb(MD5_MS_PER_MB, size))
        return String(r.data, 0, 32, Charsets.US_ASCII).lowercase()
    }

    fun command(op: Int, data: ByteArray, chk: Int = 0, timeoutMs: Long = DEFAULT_TIMEOUT): Esp.Response {
        port.write(Slip.encode(Esp.request(op, data, chk)), WRITE_TIMEOUT)
        val deadline = now() + timeoutMs
        while (true) {
            val left = deadline - now()
            val frame = (if (left > 0) readFrame(left) else null)
                ?: throw EspException("บอร์ดไม่ตอบ (คำสั่ง 0x%02x)".format(op))
            val r = Esp.parseResponse(frame) ?: continue
            if (r.op == op) return r
            if (r.data.size >= 2 && r.data[0].toInt() != 0 && r.data[1].toInt() == Esp.ROM_INVALID_RECV_MSG) {
                throw EspException("บอร์ดไม่รู้จักคำสั่ง 0x%02x".format(op))
            }
        }
    }

    private fun check(what: String, op: Int, data: ByteArray, chk: Int = 0, respLen: Int = 0,
                      timeoutMs: Long = DEFAULT_TIMEOUT): Esp.Response {
        val r = command(op, data, chk, timeoutMs)
        if (r.data.size < respLen + Esp.STATUS_BYTES) throw EspException("$what: short reply")
        if (r.data[respLen].toInt() != 0) {
            throw EspException("$what failed (error 0x%02x)".format(r.data[respLen + 1].toInt() and 0xFF))
        }
        return r
    }

    private fun readFrame(timeoutMs: Long): ByteArray? {
        val deadline = now() + timeoutMs
        while (true) {
            while (rxPos < rxLen) {
                decoder.feed(rx[rxPos++])?.let { return it }
            }
            val left = deadline - now()
            if (left <= 0) return null
            rxLen = port.read(rx, left.coerceIn(1, 1000).toInt())
            rxPos = 0
        }
    }

    private fun flushInput() {
        rxLen = 0
        rxPos = 0
        decoder.reset()
        val deadline = now() + 200
        while (now() < deadline && port.read(rx, 20) > 0) Unit
    }

    companion object {
        const val DEFAULT_TIMEOUT = 3000L
        const val SYNC_TIMEOUT = 100L
        const val WRITE_TIMEOUT = 3000
        const val ERASE_MS_PER_MB = 40_000L
        const val MD5_MS_PER_MB = 8_000L
        const val WRITE_ATTEMPTS = 3

        fun perMb(msPerMb: Long, bytes: Int): Long = maxOf(DEFAULT_TIMEOUT, msPerMb * bytes / 1_000_000)

        fun pad4(b: ByteArray): ByteArray {
            val pad = (4 - b.size % 4) % 4
            return if (pad == 0) b else b + ByteArray(pad) { 0xFF.toByte() }
        }

        fun deflate(data: ByteArray): ByteArray {
            val d = Deflater(9)
            d.setInput(data)
            d.finish()
            val out = java.io.ByteArrayOutputStream(data.size / 2 + 64)
            val buf = ByteArray(64 * 1024)
            while (!d.finished()) out.write(buf, 0, d.deflate(buf))
            d.end()
            return out.toByteArray()
        }

        fun md5Hex(b: ByteArray): String =
            MessageDigest.getInstance("MD5").digest(b).joinToString("") { "%02x".format(it) }
    }
}
