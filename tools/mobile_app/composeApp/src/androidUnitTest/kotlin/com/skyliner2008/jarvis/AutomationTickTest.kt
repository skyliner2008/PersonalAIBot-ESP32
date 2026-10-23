package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.service.AutomationTick
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** The automation loop on the wall clock (logcat 2026-10-14: cycles every 63–68 s, a 15m close seen up to 63 s late). */
class AutomationTickTest {
    private val min = 60_000L
    private val t0 = 1_791_914_400_000L   // 2026-10-14 01:00:00 +07, on a minute

    @Test
    fun `the next tick is second 3 or 33 of the minute`() {
        assertEquals(3_000L, AutomationTick.delayToNext(t0))
        assertEquals(28_000L, AutomationTick.delayToNext(t0 + 5_000))
        assertEquals(30_000L, AutomationTick.delayToNext(t0 + 3_000))
        // a cycle that ends a hair before a tick waits for the one after, not 1 ms
        assertEquals(30_500L, AutomationTick.delayToNext(t0 + 2_500))
    }

    @Test
    fun `a 1-minute job runs once a minute, every minute, a few seconds after the close`() {
        var now = t0 + 3_000
        var lastRun = 0L
        val runs = mutableListOf<Long>()
        repeat(240) {   // two hours of 30 s ticks
            if (AutomationTick.isDue(now, lastRun, min)) {
                lastRun = now + 50          // stamped after the jobs before it
                runs += now
            }
            val work = 1_000L + (it % 5) * 900L   // 1–4.6 s of fetching, like the phone
            now = now + work + AutomationTick.delayToNext(now + work)
        }
        assertEquals(120, runs.size)
        assertTrue(runs.all { (it - t0) % min == 3_000L }, "every run at second 3")
        assertTrue(runs.zipWithNext().all { (a, b) -> b - a == min }, "no minute skipped")
    }

    @Test
    fun `a job is not due half a minute after it ran`() {
        assertFalse(AutomationTick.isDue(t0 + 30_000, t0 + 50, min))
        assertTrue(AutomationTick.isDue(t0 + 60_000, t0 + 50, min))
        assertTrue(AutomationTick.isDue(t0 + 5 * min, t0 + 50, 5 * min))
        assertFalse(AutomationTick.isDue(t0 + 4 * min + 30_000, t0 + 50, 5 * min))
    }
}
