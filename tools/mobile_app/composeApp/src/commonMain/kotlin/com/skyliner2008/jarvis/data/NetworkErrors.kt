package com.skyliner2008.jarvis.data

/**
 * The phone cannot reach a host at all (no DNS, no route) — the network's fault, not the model's or the key's.
 *
 * At 03:01 on 2026-10-14 the phone lost DNS for three minutes. A wake tried three Live models and two chat models in
 * 25 ms, each "failed"; the Live models were rested for 2 minutes as if they were broken, the log said "no answer in
 * 30000ms" and "timeout=20000ms", and the user was told the AI was down. Callers use this to stop early, keep the
 * models, and say what really happened.
 */
object NetworkErrors {
    /** how long after the last unreachable failure [isDown] holds when nothing has got through since */
    const val DOWN_FOR_MS = 5 * 60_000L

    fun isUnreachable(e: Throwable?): Boolean {
        var c = e
        var depth = 0
        while (c != null && depth < 8) {
            if (isUnreachable(c::class.simpleName.orEmpty() + ": " + c.message.orEmpty())) return true
            c = c.cause
            depth++
        }
        return false
    }

    /** a failure reason as text (an exception's class and message) */
    fun isUnreachable(reason: String): Boolean {
        val r = reason.lowercase()
        return "unknownhost" in r ||
            "noroutetohost" in r ||
            "connectexception" in r ||
            "unable to resolve host" in r ||
            "no address associated with hostname" in r ||
            "network is unreachable" in r
    }

    @kotlin.jvm.Volatile private var lastUnreachableAt = 0L
    @kotlin.jvm.Volatile private var lastReachedAt = 0L

    fun markUnreachable(nowMs: Long = System.currentTimeMillis()) { lastUnreachableAt = nowMs }

    /** an answer came back from the network */
    fun markReached(nowMs: Long = System.currentTimeMillis()) { lastReachedAt = nowMs }

    /** the last network call failed for want of a network, recently, and nothing has got through since */
    fun isDown(nowMs: Long = System.currentTimeMillis()): Boolean {
        val down = lastUnreachableAt
        return down > 0 && down >= lastReachedAt && nowMs - down < DOWN_FOR_MS
    }

    internal fun resetForTesting() { lastUnreachableAt = 0L; lastReachedAt = 0L }
}
