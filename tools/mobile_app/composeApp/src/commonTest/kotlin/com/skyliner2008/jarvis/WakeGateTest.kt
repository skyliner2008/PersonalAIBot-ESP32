package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.voice.WakeGate
import com.skyliner2008.jarvis.voice.WakeWordConfig
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** The phone's wake word and the Live session (T091): when the session closes itself, when the wake word counts. */
class WakeGateTest {
    private val chunk = ByteArray(2560)

    @BeforeTest
    fun setUp() = WakeGate.resetForTest()

    @AfterTest
    fun tearDown() = WakeGate.resetForTest()

    @Test
    fun aQuietSessionClosesAfterTheChosenMinutes() {
        WakeGate.config = WakeWordConfig(sleepAfterMinutes = 2)
        WakeGate.touch(1_000)                                  // the session started
        assertFalse(WakeGate.idleExpired(1_000 + 119_000))
        assertTrue(WakeGate.idleExpired(1_000 + 120_000))
    }

    @Test
    fun aConversationKeepsItOpen() {
        WakeGate.config = WakeWordConfig(sleepAfterMinutes = 1)
        WakeGate.touch(0)
        WakeGate.touch(50_000)                                 // a reply / a user turn
        assertFalse(WakeGate.idleExpired(100_000))
        assertTrue(WakeGate.idleExpired(110_000))
    }

    @Test
    fun neverMeansAtLeastAMinute() {
        // an old setting "never" (0) still closes: the wake word must get the mic back
        WakeGate.config = WakeWordConfig(sleepAfterMinutes = 0)
        WakeGate.touch(0)
        assertTrue(WakeGate.idleExpired(60_000))
    }

    @Test
    fun theWakeWordCountsOnlyWhenOnAndNotLinkedToTheBoard() {
        var hit = true
        WakeGate.detector = { hit }
        assertTrue(WakeGate.detectWake(chunk))
        WakeGate.suppressed = { true }                         // the ESP32 board answers "Jarvis"
        assertFalse(WakeGate.detectWake(chunk))
        WakeGate.suppressed = { false }
        WakeGate.config = WakeWordConfig(enabled = false)      // off in Settings
        assertFalse(WakeGate.detectWake(chunk))
        WakeGate.config = WakeWordConfig()
        hit = false
        assertFalse(WakeGate.detectWake(chunk))
    }

    @Test
    fun noModelsNoWake() {
        assertFalse(WakeGate.detectWake(chunk))
    }
}
