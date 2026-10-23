package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.automation.smc.MarketContextDigest
import com.skyliner2008.jarvis.automation.wake.AnticipationEngine
import com.skyliner2008.jarvis.automation.wake.WakeAiFallback
import com.skyliner2008.jarvis.automation.wake.WakePrompt
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

/**
 * logcat 2026-10-14: the AI wrote "every timeframe is falling" with H4 rising (00:50), "a SELL signal conflicts with the
 * H1/M15 downtrend" (01:50), and NEUTRAL with 70 % confidence (01:06).
 */
class WakePromptTrendLineTest {
    private fun line(tf: String, trend: Int) =
        MarketContextDigest.TfLine(tf, trend, "-", 4150.0, 50.0, 4150.0, 6.0, null, null)

    @Test
    fun `the trend line names every timeframe's direction`() {
        val s = WakePrompt.trendLine(listOf(line("D1", 1), line("H4", 1), line("H1", -1), line("M15", -1), line("M5", 0)))
        assertEquals("D1 ขาขึ้น · H4 ขาขึ้น · H1 ขาลง · M15 ขาลง · M5 ไม่มีทิศ", s)
    }

    @Test
    fun `the prompt carries the trend line and the rules that use it`() {
        val p = WakePrompt.build("XAUUSD", mapOf(
            "timeframe" to "15m", "close" to "4150",
            AnticipationEngine.K_TF_TRENDS to "H4 ขาขึ้น · H1 ขาลง · M15 ขาลง"
        ))
        assertTrue(p.contains("สรุปทิศโครงสร้าง") && p.contains("H4 ขาขึ้น · H1 ขาลง · M15 ขาลง"))
        assertTrue(p.contains("ห้ามเหมารวมว่า \"ทุกไทม์เฟรม\""))
        assertTrue(p.contains("SELL ในขาลงหรือ BUY ในขาขึ้น = ตามเทรนด์"))
        assertTrue(p.contains("BIAS เป็น NEUTRAL ให้ใส่ 0"))
        // no digest → no line and no rule pointing at it
        val bare = WakePrompt.build("XAUUSD", mapOf("timeframe" to "15m", "close" to "4150"))
        assertFalse(bare.contains("สรุปทิศโครงสร้าง"))
    }

    @Test
    fun `the alert voice reads generationComplete`() {
        // 3.1 Live holds turnComplete until the voice would have played out — every alert closed on the idle timeout
        val json = kotlinx.serialization.json.Json { ignoreUnknownKeys = true }
        val msg = json.decodeFromString(com.skyliner2008.jarvis.data.LiveServerMessage.serializer(),
            """{"serverContent":{"generationComplete":true}}""")
        assertEquals(true, msg.serverContent?.generationComplete)
        assertEquals(null, msg.serverContent?.turnComplete)
    }

    @Test
    fun `the quiet list says the network is down when it is`() {
        assertNotEquals(WakeAiFallback.noticeTh(offline = true), WakeAiFallback.noticeTh(offline = false))
        assertTrue(WakeAiFallback.noticeTh(offline = true).contains("อินเทอร์เน็ต"))
        assertEquals(WakeAiFallback.NOTICE_TH, WakeAiFallback.noticeTh(offline = false))
    }
}
