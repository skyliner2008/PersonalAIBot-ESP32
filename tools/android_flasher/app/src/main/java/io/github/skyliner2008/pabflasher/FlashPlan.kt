package io.github.skyliner2008.pabflasher

/**
 * What goes where on the 16 MB flash (partitions_sr.csv of the firmware):
 *   0x0 bootloader · 0x8000 partition table · 0x9000 nvs (settings, encrypted secrets) · 0xe000 otadata
 *   0x10000 app0 (5 MB) · 0x510000 app1 · 0xA10000 model (wake word) · 0xD50000 LittleFS (memory) · 0xFF0000 coredump
 *
 * UPDATE  writes the -app.bin to app0 and otadata = boot_app0.bin (boot app0) - settings and memory stay.
 * FACTORY writes the -factory.bin at 0x0 (bootloader ... model): settings are reset, LittleFS memory stays.
 */
object Layout {
    const val FLASH_SIZE = 16 * 1024 * 1024
    const val PARTITION_TABLE = 0x8000
    const val OTADATA = 0xE000
    const val OTADATA_SIZE = 0x2000
    const val APP0 = 0x10000
    const val APP_SIZE = 0x500000
    const val LITTLEFS = 0xD50000
}

enum class Kind { APP, FACTORY }

class Region(val offset: Int, val data: ByteArray, val label: String)

object FlashPlan {
    private const val IMAGE_MAGIC = 0xE9

    /** By content: a factory image holds the partition table (AA 50) at 0x8000, an app starts with E9. */
    fun detect(b: ByteArray): Kind? = when {
        b.size > Layout.APP0 + 16 && u8(b, Layout.PARTITION_TABLE) == 0xAA && u8(b, Layout.PARTITION_TABLE + 1) == 0x50 &&
            u8(b, 0) == IMAGE_MAGIC -> Kind.FACTORY
        b.size >= 24 && u8(b, 0) == IMAGE_MAGIC -> Kind.APP
        else -> null
    }

    /** null = ok, otherwise why the file must not be flashed (Thai, for the screen). */
    fun problem(kind: Kind, b: ByteArray): String? {
        val found = detect(b) ?: return "ไฟล์นี้ไม่ใช่เฟิร์มแวร์ ESP32 (.bin)"
        if (found != kind) {
            return if (found == Kind.FACTORY) "นี่คือไฟล์ factory (ติดตั้งใหม่ทั้งหมด) ไม่ใช่ไฟล์ app"
            else "นี่คือไฟล์ app (อัปเดต) ไม่ใช่ไฟล์ factory"
        }
        return when (kind) {
            Kind.APP -> when {
                b.size > Layout.APP_SIZE -> "ไฟล์ app ใหญ่เกิน partition (5 MB)"
                chipId(b, 0) != Esp.ESP32S3_CHIP_ID -> "เฟิร์มแวร์นี้ไม่ได้ build สำหรับ ESP32-S3"
                else -> null
            }
            Kind.FACTORY -> when {
                b.size > Layout.LITTLEFS -> "ไฟล์ factory ยาวเกินจนทับพื้นที่ความจำ (LittleFS)"
                chipId(b, 0) != Esp.ESP32S3_CHIP_ID || chipId(b, Layout.APP0) != Esp.ESP32S3_CHIP_ID ->
                    "เฟิร์มแวร์นี้ไม่ได้ build สำหรับ ESP32-S3"
                else -> null
            }
        }
    }

    fun regions(kind: Kind, b: ByteArray): List<Region> = when (kind) {
        Kind.APP -> listOf(
            Region(Layout.OTADATA, bootApp0(), "otadata"),
            Region(Layout.APP0, b, "app"),
        )
        Kind.FACTORY -> listOf(Region(0, b, "factory"))
    }

    /** The framework's boot_app0.bin (what the factory image carries at 0xe000): sequence 1 = boot app0. */
    fun bootApp0(): ByteArray {
        val b = ByteArray(Layout.OTADATA_SIZE) { 0xFF.toByte() }
        Esp.putU32(b, 0, 1)
        Esp.putU32(b, 28, 0x4743989A.toInt())  // CRC32 of the sequence number
        Esp.putU32(b, 4096, 0)
        return b
    }

    /** chip_id of the image's extended header (u16 at +12): 9 = ESP32-S3; -1 = no image there. */
    fun chipId(b: ByteArray, at: Int): Int =
        if (b.size < at + 16 || u8(b, at) != IMAGE_MAGIC) -1 else u8(b, at + 12) or (u8(b, at + 13) shl 8)

    private fun u8(b: ByteArray, i: Int) = b[i].toInt() and 0xFF
}
