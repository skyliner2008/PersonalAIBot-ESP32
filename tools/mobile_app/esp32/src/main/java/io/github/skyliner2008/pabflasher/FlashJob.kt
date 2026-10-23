package io.github.skyliner2008.pabflasher

import java.io.IOException

/** Opens the board's port: waits for it on USB and for the user's permission. */
fun interface PortProvider {
    fun open(): SerialPort
}

/** One flash, start to end: download mode -> chip check -> write + verify every region -> reboot. */
class FlashJob(
    private val ports: PortProvider,
    private val listener: Listener,
    private val sleep: (Long) -> Unit = { Thread.sleep(it) },
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
) {
    interface Listener {
        fun status(text: String)
        fun progress(done: Long, total: Long)
    }

    fun run(kind: Kind, image: ByteArray) {
        FlashPlan.problem(kind, image)?.let { throw EspException(it) }
        val regions = FlashPlan.regions(kind, image)
        listener.status("กำลังเชื่อมต่อบอร์ด…")
        var port = ports.open()
        try {
            var loader = EspLoader(port, sleep, now)
            val ok = try {
                loader.connect(attempts = 3, reset = true)
            } catch (e: EspException) {
                throw e
            } catch (_: IOException) {
                // the board dropped off USB while it reset: wait for it to come back, it is in download mode now
                port.close()
                listener.status("รอบอร์ดกลับมาที่ USB…")
                port = ports.open()
                loader = EspLoader(port, sleep, now)
                loader.connect(attempts = 3, reset = false)
            }
            if (!ok) throw EspException(NOT_IN_DOWNLOAD_MODE)
            val chip = loader.chipId()
            if (chip != null && chip != Esp.ESP32S3_CHIP_ID) {
                throw EspException("บอร์ดนี้ไม่ใช่ ESP32-S3 (chip id $chip) — เฟิร์มแวร์นี้ใช้ไม่ได้")
            }
            loader.attachFlash(Layout.FLASH_SIZE)
            val total = regions.sumOf { it.data.size.toLong() }
            var done = 0L
            for (r in regions) {
                listener.status("กำลังเขียน %s ที่ 0x%x (%,d ไบต์)…".format(r.label, r.offset, r.data.size))
                loader.writeFlash(r.offset, r.data) { sent, all ->
                    listener.progress(done + r.data.size.toLong() * sent / all, total)
                }
                done += r.data.size
                listener.progress(done, total)
            }
            listener.status("ตรวจสอบครบแล้ว กำลังรีสตาร์ตบอร์ด…")
            loader.hardReset()
        } finally {
            port.close()
        }
    }

    companion object {
        const val NOT_IN_DOWNLOAD_MODE =
            "บอร์ดไม่เข้าโหมดแฟลช — กดปุ่ม BOOT ค้างไว้ แตะ RESET แล้วปล่อย BOOT จากนั้นกด \"แฟลช\" อีกครั้ง"
    }
}
