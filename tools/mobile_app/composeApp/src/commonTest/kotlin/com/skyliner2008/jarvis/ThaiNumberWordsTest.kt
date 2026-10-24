package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.tools.trading.ThaiNumberWords
import kotlin.test.Test
import kotlin.test.assertEquals

/** T094: Live said "สองพันเก้าร้อย…" for 3995.81 - the tool result now carries the Thai words to read. */
class ThaiNumberWordsTest {
    @Test
    fun `integers read the Thai way`() {
        assertEquals("ศูนย์", ThaiNumberWords.integer(0))
        assertEquals("หนึ่ง", ThaiNumberWords.integer(1))
        assertEquals("สิบ", ThaiNumberWords.integer(10))
        assertEquals("สิบเอ็ด", ThaiNumberWords.integer(11))
        assertEquals("ยี่สิบเอ็ด", ThaiNumberWords.integer(21))
        assertEquals("หนึ่งร้อยเอ็ด", ThaiNumberWords.integer(101))
        assertEquals("สามพันเก้าร้อยเก้าสิบห้า", ThaiNumberWords.integer(3995))
        assertEquals("สี่พันยี่สิบสี่", ThaiNumberWords.integer(4024))
        assertEquals("หนึ่งแสนสองหมื่นสามพันสี่ร้อยห้าสิบหก", ThaiNumberWords.integer(123_456))
        assertEquals("หนึ่งล้าน", ThaiNumberWords.integer(1_000_000))
        assertEquals("หนึ่งล้านเอ็ด", ThaiNumberWords.integer(1_000_001))
        assertEquals("สิบสองล้านสามแสน", ThaiNumberWords.integer(12_300_000))
        assertEquals("ลบห้า", ThaiNumberWords.integer(-5))
    }

    @Test
    fun `prices from the gold log read correctly`() {
        assertEquals("สามพันเก้าร้อยเก้าสิบห้าจุดแปดหนึ่ง", ThaiNumberWords.decimal(3995.81, 2))
        assertEquals("ลบยี่สิบแปดจุดเจ็ดศูนย์", ThaiNumberWords.decimal(-28.6988, 2))
        assertEquals("ลบศูนย์จุดเจ็ดหนึ่ง", ThaiNumberWords.decimal(-0.71, 2))
        assertEquals("สี่พันยี่สิบสี่จุดห้าหนึ่ง", ThaiNumberWords.decimal(4024.5088, 2))
        assertEquals("หนึ่งจุดศูนย์แปดสามสอง", ThaiNumberWords.decimal(1.0832, 4))
        assertEquals("ศูนย์จุดศูนย์ศูนย์", ThaiNumberWords.decimal(-0.001, 2))
    }

    @Test
    fun `shown numbers are grouped`() {
        assertEquals("3,995.81", ThaiNumberWords.grouped(3995.81, 2))
        assertEquals("-28.70", ThaiNumberWords.grouped(-28.6988, 2))
        assertEquals("67,123.40", ThaiNumberWords.grouped(67123.4, 2))
        assertEquals("1.0832", ThaiNumberWords.grouped(1.0832, 4))
        assertEquals(2, ThaiNumberWords.priceDecimals(3995.81))
        assertEquals(4, ThaiNumberWords.priceDecimals(1.08))
    }
}
