package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.ui.components.ChartCardConfig
import com.skyliner2008.jarvis.ui.components.parseChartConfig
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class ChartCardParseTest {

    @Test
    fun overlaysAsStringFromTheModel() {
        // คำตอบจริง 2026-10-13 01:20 / 01:23 — เคยกลายเป็น "(chart block อ่านไม่สำเร็จ)"
        assertEquals(
            ChartCardConfig("BTCUSDT", "15m", "single", setOf("smc")),
            parseChartConfig("""{"symbol":"BTCUSDT","interval":"15m","layout":"single","overlays":"smc"}""")
        )
    }

    @Test
    fun overlaysAsCommaSeparatedString() {
        assertEquals(
            setOf("ema20", "bb", "smc"),
            parseChartConfig("""{"symbol":"XAUUSD","overlays":"ema20, BB,smc"}""")?.overlays
        )
    }

    @Test
    fun overlaysAsArrayLikeThePromptExample() {
        assertEquals(
            ChartCardConfig("XAUUSD", "1h", "rsi_macd", setOf("ema50", "bb")),
            parseChartConfig("""{"symbol":"XAUUSD","interval":"1h","layout":"rsi_macd","overlays":["ema50","bb"]}""")
        )
    }

    @Test
    fun missingOrNullOverlaysMeansNone() {
        assertEquals(emptySet(), parseChartConfig("""{"symbol":"XAUUSD"}""")?.overlays)
        assertEquals(emptySet(), parseChartConfig("""{"symbol":"XAUUSD","overlays":null}""")?.overlays)
    }

    @Test
    fun brokenJsonOrNoSymbolIsRejected() {
        assertNull(parseChartConfig("""{"symbol":"XAUUSD","""))
        assertNull(parseChartConfig("""{"interval":"1h"}"""))
    }
}
