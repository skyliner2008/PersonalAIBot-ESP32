package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.data.NetworkErrors
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** No network ≠ a broken model (logcat 2026-10-14 03:01: DNS gone for 3 minutes). */
class NetworkErrorsTest {
    @AfterTest
    fun reset() = NetworkErrors.resetForTesting()

    private class UnknownHostException(msg: String) : Exception(msg)

    @Test
    fun `dns and route failures are no network, other failures are not`() {
        assertTrue(NetworkErrors.isUnreachable(
            "UnknownHostException: Unable to resolve host \"generativelanguage.googleapis.com\": No address associated with hostname"))
        assertTrue(NetworkErrors.isUnreachable("ConnectException: Network is unreachable"))
        assertFalse(NetworkErrors.isUnreachable("closed: 1011 RESOURCE_EXHAUSTED: You exceeded your current quota"))
        assertFalse(NetworkErrors.isUnreachable("SocketTimeoutException: timeout"))
        assertFalse(NetworkErrors.isUnreachable("no setupComplete in 8000ms"))
    }

    @Test
    fun `a wrapped dns failure is still no network`() {
        val wrapped = IllegalStateException("websocket failed", UnknownHostException("Unable to resolve host \"x\""))
        assertTrue(NetworkErrors.isUnreachable(wrapped))
        assertFalse(NetworkErrors.isUnreachable(IllegalStateException("bad frame")))
    }

    @Test
    fun `down until something gets through, and not for ever`() {
        val t = 1_000_000L
        assertFalse(NetworkErrors.isDown(t))
        NetworkErrors.markUnreachable(t)
        assertTrue(NetworkErrors.isDown(t + 1_000))
        assertFalse(NetworkErrors.isDown(t + NetworkErrors.DOWN_FOR_MS), "a stale failure says nothing about now")
        NetworkErrors.markReached(t + 2_000)
        assertFalse(NetworkErrors.isDown(t + 3_000))
    }
}
