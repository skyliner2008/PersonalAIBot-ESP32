package io.github.skyliner2008.pabflasher

import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.io.IOException

/**
 * The serial ROM bootloader protocol of the ESP32-S3 (what esptool speaks), written from the protocol
 * description: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/serial-protocol.html
 *
 * A packet is SLIP framed: 0xC0 ... 0xC0, with 0xC0 -> DB DC and 0xDB -> DB DD inside.
 *   request:  00 | op | size u16 | checksum u32 | data
 *   response: 01 | op | size u16 | value u32    | data (+ status: failed, error, 2 reserved bytes on the ROM)
 */
object Slip {
    const val END = 0xC0
    const val ESC = 0xDB
    const val ESC_END = 0xDC
    const val ESC_ESC = 0xDD

    fun encode(payload: ByteArray): ByteArray {
        val out = ByteArrayOutputStream(payload.size + 16)
        out.write(END)
        for (b in payload) {
            when (val v = b.toInt() and 0xFF) {
                END -> { out.write(ESC); out.write(ESC_END) }
                ESC -> { out.write(ESC); out.write(ESC_ESC) }
                else -> out.write(v)
            }
        }
        out.write(END)
        return out.toByteArray()
    }

    /** Feed bytes one at a time; returns a whole frame when its closing 0xC0 arrives. Text outside frames
     *  (the boot log) is dropped. */
    class Decoder {
        private val buf = ByteArrayOutputStream()
        private var inFrame = false
        private var escape = false

        fun feed(b: Byte): ByteArray? {
            val v = b.toInt() and 0xFF
            if (!inFrame) {
                if (v == END) { inFrame = true; buf.reset(); escape = false }
                return null
            }
            if (escape) {
                escape = false
                buf.write(when (v) { ESC_END -> END; ESC_ESC -> ESC; else -> v })
                return null
            }
            return when (v) {
                END -> if (buf.size() == 0) null else { inFrame = false; buf.toByteArray().also { buf.reset() } }
                ESC -> { escape = true; null }
                else -> { buf.write(v); null }
            }
        }

        fun reset() { buf.reset(); inFrame = false; escape = false }
    }
}

object Esp {
    const val SYNC = 0x08
    const val READ_REG = 0x0A
    const val SPI_SET_PARAMS = 0x0B
    const val SPI_ATTACH = 0x0D
    const val FLASH_DEFL_BEGIN = 0x10
    const val FLASH_DEFL_DATA = 0x11
    const val FLASH_DEFL_END = 0x12
    const val SPI_FLASH_MD5 = 0x13
    const val GET_SECURITY_INFO = 0x14

    const val CHECKSUM_MAGIC = 0xEF
    const val ROM_INVALID_RECV_MSG = 0x05
    /** What the ROM loader takes per FLASH_DEFL_DATA packet. */
    const val FLASH_WRITE_SIZE = 0x400
    const val STATUS_BYTES = 2  // failed, error (the ROM appends 2 reserved bytes after them)
    const val ESP32S3_CHIP_ID = 9

    val SYNC_DATA: ByteArray = byteArrayOf(0x07, 0x07, 0x12, 0x20) + ByteArray(32) { 0x55 }

    fun checksum(data: ByteArray, from: Int = 0, to: Int = data.size): Int {
        var c = CHECKSUM_MAGIC
        for (i in from until to) c = c xor (data[i].toInt() and 0xFF)
        return c
    }

    fun request(op: Int, data: ByteArray, chk: Int = 0): ByteArray {
        val p = ByteArray(8 + data.size)
        p[0] = 0
        p[1] = op.toByte()
        putU16(p, 2, data.size)
        putU32(p, 4, chk)
        data.copyInto(p, 8)
        return p
    }

    class Response(val op: Int, val value: Int, val data: ByteArray)

    fun parseResponse(frame: ByteArray): Response? {
        if (frame.size < 8 || frame[0].toInt() != 1) return null
        val size = u16(frame, 2)
        val data = frame.copyOfRange(8, minOf(frame.size, 8 + size))
        return Response(frame[1].toInt() and 0xFF, u32(frame, 4), data)
    }

    fun words(vararg v: Int): ByteArray {
        val b = ByteArray(v.size * 4)
        v.forEachIndexed { i, x -> putU32(b, i * 4, x) }
        return b
    }

    fun putU16(b: ByteArray, at: Int, v: Int) {
        b[at] = v.toByte(); b[at + 1] = (v ushr 8).toByte()
    }

    fun putU32(b: ByteArray, at: Int, v: Int) {
        for (i in 0 until 4) b[at + i] = (v ushr (8 * i)).toByte()
    }

    fun u16(b: ByteArray, at: Int) = (b[at].toInt() and 0xFF) or ((b[at + 1].toInt() and 0xFF) shl 8)

    fun u32(b: ByteArray, at: Int): Int {
        var v = 0
        for (i in 0 until 4) v = v or ((b[at + i].toInt() and 0xFF) shl (8 * i))
        return v
    }
}

/** A serial port as the loader needs it; the app uses USB CDC-ACM (UsbCdcPort), tests a simulated ROM. */
interface SerialPort : Closeable {
    /** Writes everything or throws IOException. */
    fun write(data: ByteArray, timeoutMs: Int)

    /** Reads what arrives within [timeoutMs]; 0 = nothing. Throws IOException when the device is gone. */
    fun read(buf: ByteArray, timeoutMs: Int): Int

    /** CDC SET_CONTROL_LINE_STATE: true = asserted, the way pyserial's setDTR / setRTS mean it. */
    fun setLines(dtr: Boolean, rts: Boolean)
}

class EspException(message: String) : IOException(message)
