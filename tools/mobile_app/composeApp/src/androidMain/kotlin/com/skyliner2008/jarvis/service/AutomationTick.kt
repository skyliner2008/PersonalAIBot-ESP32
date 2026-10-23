package com.skyliner2008.jarvis.service

/**
 * When the automation loop runs: on the wall clock, at second 3 and 33 of every minute.
 *
 * The loop used to wait 30 s after each cycle's work, so a cycle came every 63–68 s and its place in the minute
 * drifted. A 15m bar that closed at 01:00 was seen at 01:00:18, the one at 03:00 at 03:01:03 (logcat 2026-10-14),
 * and a 1m bar was now and then never the latest closed bar of any scan. Ticks fixed to the clock see every closed
 * bar a few seconds after it closes.
 */
internal object AutomationTick {
    const val PERIOD_MS = 30_000L
    /** after the bar closes, so the closed bar is in the next fetch */
    const val OFFSET_MS = 3_000L
    /**
     * a job is due this much before its interval is up — its last run is stamped after its tick began (after the jobs
     * before it), so without slack a 1-minute job would miss the next minute's tick by a few ms and slip to the half
     */
    const val DUE_SLACK_MS = PERIOD_MS / 2
    /** no tick right after a cycle that ended just before a boundary */
    private const val MIN_WAIT_MS = 1_000L

    /** ms from [nowMs] to the next tick */
    fun delayToNext(nowMs: Long): Long {
        val wait = PERIOD_MS - Math.floorMod(nowMs - OFFSET_MS, PERIOD_MS)
        return if (wait < MIN_WAIT_MS) wait + PERIOD_MS else wait
    }

    /** whether a job last run at [lastRunAt] with [intervalMs] runs on the tick that began at [nowMs] */
    fun isDue(nowMs: Long, lastRunAt: Long, intervalMs: Long): Boolean =
        nowMs - lastRunAt >= intervalMs - DUE_SLACK_MS
}
