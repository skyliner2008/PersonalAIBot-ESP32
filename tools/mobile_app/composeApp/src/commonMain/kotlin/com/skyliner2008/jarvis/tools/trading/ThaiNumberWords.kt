package com.skyliner2008.jarvis.tools.trading

import kotlin.math.abs
import kotlin.math.roundToLong

/**
 * ตัวเลข → คำอ่านภาษาไทย ให้ Live อ่านตามตัวอักษร (T094): Live แปลงตัวเลขเป็นเสียงเองแล้วอ่านหลักพันผิด —
 * ผล tool "Price: 3995.8100" แต่พูดว่า "สองพันเก้าร้อยเก้าสิบห้าจุดแปดหนึ่ง" (logcat 2026-10-24 13:38:14)
 * ส่วนราคาปิด 4024.5088 ในผลเดียวกันอ่านถูก
 */
object ThaiNumberWords {
    private val DIGITS = arrayOf("ศูนย์", "หนึ่ง", "สอง", "สาม", "สี่", "ห้า", "หก", "เจ็ด", "แปด", "เก้า")
    private val PLACES = arrayOf("", "สิบ", "ร้อย", "พัน", "หมื่น", "แสน")

    /** จำนวนเต็ม: 3995 → "สามพันเก้าร้อยเก้าสิบห้า", 21 → "ยี่สิบเอ็ด", 1_000_001 → "หนึ่งล้านเอ็ด" */
    fun integer(n: Long): String = when {
        n < 0 -> "ลบ" + integer(-n)
        n == 0L -> DIGITS[0]
        else -> {
            val millions = n / 1_000_000
            val rest = n % 1_000_000
            buildString {
                if (millions > 0) append(integer(millions)).append("ล้าน")
                if (rest > 0) append(belowMillion(rest, hasHigher = millions > 0))
            }
        }
    }

    private fun belowMillion(n: Long, hasHigher: Boolean): String {
        val s = n.toString()
        return buildString {
            s.forEachIndexed { i, ch ->
                val d = ch - '0'
                val place = s.length - 1 - i
                if (d == 0) return@forEachIndexed
                when {
                    place == 1 && d == 1 -> append("สิบ")
                    place == 1 && d == 2 -> append("ยี่สิบ")
                    place == 0 && d == 1 && (s.length > 1 || hasHigher) -> append("เอ็ด")
                    else -> append(DIGITS[d]).append(PLACES[place])
                }
            }
        }
    }

    /**
     * ทศนิยม [decimals] ตำแหน่ง อ่านทีละหลักหลังจุด: 3995.81 → "สามพันเก้าร้อยเก้าสิบห้าจุดแปดหนึ่ง",
     * -28.70 → "ลบยี่สิบแปดจุดเจ็ดศูนย์" (ตำแหน่งเท่ากับที่แสดงเป็นตัวเลข)
     */
    fun decimal(v: Double, decimals: Int): String {
        var scale = 1L
        repeat(decimals) { scale *= 10 }
        val scaled = (abs(v) * scale).roundToLong()
        val whole = integer(scaled / scale)
        val frac = (scaled % scale).toString().padStart(decimals, '0')
        val sign = if (v < 0 && scaled != 0L) "ลบ" else ""
        return sign + whole + if (decimals > 0) "จุด" + frac.map { DIGITS[it - '0'] }.joinToString("") else ""
    }

    /** ทศนิยมที่ใช้แสดงราคา: ≥ 100 → 2 ตำแหน่ง (ทอง 3,995.81), ≥ 1 → 4 (EURUSD 1.0832), < 1 → 6 */
    fun priceDecimals(v: Double): Int = when {
        abs(v) >= 100 -> 2
        abs(v) >= 1 -> 4
        else -> 6
    }

    /** 3995.81 → "3,995.81" (คั่นหลักพัน ให้ตัวเลขที่โมเดลเห็นอ่านง่ายขึ้นด้วย) */
    fun grouped(v: Double, decimals: Int): String {
        var scale = 1L
        repeat(decimals) { scale *= 10 }
        val scaled = (abs(v) * scale).roundToLong()
        val whole = (scaled / scale).toString().reversed().chunked(3).joinToString(",").reversed()
        val frac = if (decimals > 0) "." + (scaled % scale).toString().padStart(decimals, '0') else ""
        val sign = if (v < 0 && scaled != 0L) "-" else ""
        return sign + whole + frac
    }
}
