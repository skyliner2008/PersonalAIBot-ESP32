package io.github.skyliner2008.pabflasher

import java.io.IOException
import java.util.zip.Inflater

/**
 * A simulated ESP32-S3 behind its USB-Serial-JTAG port: the app (ignores the protocol) until the
 * DTR/RTS bootloader sequence, then the ROM loader with a 16 MB flash. Time is virtual ([clock]).
 */
class FakeRom(var chipId: Int = Esp.ESP32S3_CHIP_ID) : SerialPort {
    val flash = ByteArray(Layout.FLASH_SIZE) { 0xFF.toByte() }
    var clock = 0L
    var inDownload = false
    val lines = mutableListOf<Pair<Boolean, Boolean>>()
    val ops = mutableListOf<Int>()
    var bootResets = 0
    var appResets = 0

    /** Fail this FLASH_DEFL_DATA sequence number once (bad checksum reply) - the loader must resend it. */
    var rejectSeqOnce = -1
    /** Drop off USB after the next bootloader reset (some phones see the port re-enumerate). */
    var dropOnBootReset = false
    var gone = false
    /** The reset lines do nothing (a board whose USB is busy): only BOOT + RESET by hand helps. */
    var ignoreLines = false

    private val out = ArrayDeque<Byte>()
    private val decoder = Slip.Decoder()
    private var dtr = false
    private var rts = false
    private var io0Low = false
    private var bootIntoRom = false
    private var flashSize = 0
    private var inflater: Inflater? = null
    private var writePos = 0
    private var eraseEnd = 0
    private var nextSeq = 0
    /** The reply to the last command is ready at this time (erase / write take a while, like on the chip). */
    private var readyAt = 0L

    override fun setLines(dtr: Boolean, rts: Boolean) {
        if (gone) throw IOException("gone")
        lines += dtr to rts
        val wasReset = this.rts
        this.dtr = dtr
        this.rts = rts
        if (ignoreLines) return
        if (dtr && !rts) io0Low = true
        if (!wasReset && rts) bootIntoRom = io0Low  // EN goes low: what GPIO0 is now decides the boot
        if (wasReset && !rts) {  // EN released
            inDownload = bootIntoRom
            io0Low = false
            out.clear()
            decoder.reset()
            inflater = null
            if (inDownload) {
                bootResets++
                emit("ESP-ROM:esp32s3-20210327\r\nboot:0x0 (DOWNLOAD(USB/UART0))\r\nwaiting for download\r\n")
                if (dropOnBootReset) { dropOnBootReset = false; gone = true }
            } else {
                appResets++
                emit("ESP-ROM:esp32s3-20210327\r\nboot:0x8 (SPI_FAST_FLASH_BOOT)\r\nPersonalAIBot booting\r\n")
            }
        }
    }

    override fun write(data: ByteArray, timeoutMs: Int) {
        if (gone) throw IOException("gone")
        for (b in data) {
            val frame = decoder.feed(b) ?: continue
            if (inDownload) handle(frame)
        }
    }

    override fun read(buf: ByteArray, timeoutMs: Int): Int {
        if (gone) throw IOException("gone")
        if (out.isEmpty() || clock < readyAt) {
            clock += if (out.isEmpty()) timeoutMs.toLong() else minOf(timeoutMs.toLong(), readyAt - clock)
            return 0
        }
        var n = 0
        while (n < minOf(buf.size, 64) && out.isNotEmpty()) buf[n++] = out.removeFirst()  // USB packets of 64
        return n
    }

    override fun close() = Unit

    private fun emit(text: String) = text.toByteArray().forEach { out.addLast(it) }

    private fun reply(op: Int, value: Int = 0, payload: ByteArray = ByteArray(0), status: Int = 0, error: Int = 0) {
        val data = payload + byteArrayOf(status.toByte(), error.toByte(), 0, 0)
        val p = ByteArray(8 + data.size)
        p[0] = 1
        p[1] = op.toByte()
        Esp.putU16(p, 2, data.size)
        Esp.putU32(p, 4, value)
        data.copyInto(p, 8)
        Slip.encode(p).forEach { out.addLast(it) }
    }

    private fun handle(frame: ByteArray) {
        require(frame[0].toInt() == 0) { "not a request" }
        val op = frame[1].toInt() and 0xFF
        val size = Esp.u16(frame, 2)
        val chk = Esp.u32(frame, 4)
        val data = frame.copyOfRange(8, frame.size)
        require(data.size == size) { "size field ${size} != ${data.size}" }
        ops += op
        when (op) {
            Esp.SYNC -> {
                if (!data.contentEquals(Esp.SYNC_DATA)) return reply(op, status = 1, error = 0x08)
                repeat(8) { reply(op, value = 0x20120707) }
            }
            Esp.GET_SECURITY_INFO -> {
                val info = ByteArray(20)
                Esp.putU32(info, 12, chipId)
                reply(op, payload = info)
            }
            Esp.SPI_ATTACH -> reply(op, status = if (data.size == 8) 0 else 1, error = 0x07)
            Esp.SPI_SET_PARAMS -> {
                if (data.size != 24) return reply(op, status = 1, error = 0x07)
                flashSize = Esp.u32(data, 4)
                reply(op)
            }
            Esp.FLASH_DEFL_BEGIN -> {
                // the S3 ROM takes 5 words: erase size, blocks, block size, offset, encrypted
                if (data.size != 20 || flashSize == 0) return reply(op, status = 1, error = 0x07)
                val erase = Esp.u32(data, 0)
                val offset = Esp.u32(data, 12)
                if (Esp.u32(data, 8) != Esp.FLASH_WRITE_SIZE || offset % 4096 != 0 || offset + erase > flashSize) {
                    return reply(op, status = 1, error = 0x07)
                }
                eraseEnd = offset + (erase + 4095) / 4096 * 4096
                flash.fill(0xFF.toByte(), offset, eraseEnd)
                readyAt = clock + 20L * erase / 1000  // 20 s per MB erased: under the loader's 40 s per MB
                inflater = Inflater()
                writePos = offset
                nextSeq = 0
                reply(op)
            }
            Esp.FLASH_DEFL_DATA -> {
                val inf = inflater ?: return reply(op, status = 1, error = 0x05)
                val len = Esp.u32(data, 0)
                val seq = Esp.u32(data, 4)
                if (data.size != 16 + len) return reply(op, status = 1, error = 0x07)
                if (Esp.checksum(data, 16, data.size) != chk) return reply(op, status = 1, error = 0x07)
                if (seq == rejectSeqOnce) { rejectSeqOnce = -1; return reply(op, status = 1, error = 0x07) }
                if (seq != nextSeq) return reply(op, status = 1, error = 0x07)
                nextSeq++
                inf.setInput(data, 16, len)
                val buf = ByteArray(64 * 1024)
                var written = 0L
                while (true) {
                    val n = inf.inflate(buf)
                    if (n == 0) break
                    if (writePos + n > eraseEnd) return reply(op, status = 1, error = 0x07)  // never past the erase
                    buf.copyInto(flash, writePos, 0, n)
                    writePos += n
                    written += n
                }
                readyAt = clock + 10L * written / 1000  // writing takes 10 s per MB of what the block inflates to
                reply(op)
            }
            Esp.SPI_FLASH_MD5 -> {
                val addr = Esp.u32(data, 0)
                val len = Esp.u32(data, 4)
                val md5 = EspLoader.md5Hex(flash.copyOfRange(addr, addr + len))
                reply(op, payload = md5.uppercase().toByteArray())  // the ROM answers in hex text
            }
            else -> reply(op, status = 1, error = Esp.ROM_INVALID_RECV_MSG)
        }
    }
}
