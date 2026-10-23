package com.skyliner2008.jarvis.voice

import kotlin.concurrent.Volatile

/** The wake word settings (Settings → คำปลุก, saved by SettingsController as wake_*). */
data class WakeWordConfig(
    /** The on-device wake word ("hey jarvis", openWakeWord) at all. Off = no wake word (the old energy wake in Always Live). */
    val enabled: Boolean = true,
    /** No conversation this long -> the Live session closes itself and the wake word listens again (minutes, >= 1). */
    val sleepAfterMinutes: Int = 5,
    /** Score 0..1 that counts as the wake word (openWakeWord's default is 0.5); also the personal match's leniency. */
    val threshold: Float = 0.5f,
    /** [WakeMode]: the stock "Hey Jarvis", "Hey Jarvis" + the user's voice, or the user's own word. */
    val mode: Int = WakeMode.STANDARD,
)

/**
 * Where the phone's wake word and the Live session meet (wiki 17, T091) - the user's flow:
 *  - no Live session (screen on or off): the wake word listens ([detector], on its own mic - Android WakeWordController);
 *  - "Jarvis" -> [SessionControl.startFromWake] opens the session; while it is open the wake word does not listen and
 *    the mic goes to Gemini as always ([LiveListener.liveStarting] frees the mic first);
 *  - no conversation for [WakeWordConfig.sleepAfterMinutes] ([lastActivityMs]: a reply, a user turn, the session
 *    start) -> [SessionControl.sleep] closes it, [LiveListener.liveStopped] -> the wake word listens again.
 * No Android code here (unit tested: WakeGateTest).
 */
object WakeGate {
    /** One mic chunk (16 kHz PCM16) -> true when the wake word ended in it. Null = no wake word on this device. */
    @Volatile
    var detector: ((ByteArray) -> Boolean)? = null

    /** True while another device answers "Jarvis" (the ESP32 board linked to this phone): the phone stays quiet. */
    @Volatile
    var suppressed: () -> Boolean = { false }

    @Volatile
    var config = WakeWordConfig()

    /** The user's recordings of "Hey Jarvis" (mode 1) and of their own word (mode 2) - SettingsController loads them. */
    @Volatile
    var personalJarvis: PersonalWake? = null

    @Volatile
    var personalCustom: PersonalWake? = null

    /** Records and analyses enrolment takes (Android: WakeWordService). Null = no wake word on this device. */
    interface Enroller {
        /** Records [ms] of the mic (the wake word listener lets go meanwhile). Null = no mic / permission. */
        suspend fun record(ms: Int): ByteArray?
        fun analyse(pcm: ByteArray): EnrolTake?
        /** The enrolment dialog is open (true) / closed: the listener stays off in between takes. */
        fun hold(on: Boolean)
        /** For the log: a profile was saved / loaded. */
        fun saved(mode: Int, profile: PersonalWake?)
    }

    @Volatile
    var enroller: Enroller? = null

    /** The Live session as the wake word sees it (VoiceController). */
    interface SessionControl {
        /** The Live session (its mic) is on. */
        val live: Boolean
        /** Close the session: nobody talked for [WakeWordConfig.sleepAfterMinutes]. */
        fun sleep()
        /** Open a session because the wake word was heard. */
        fun startFromWake()
    }

    @Volatile
    var sessionControl: SessionControl? = null

    /**
     * Told when something takes the mic and when it gives it back ([owner]: "live" = the Live session, "specialist" =
     * meeting / translate). Android: the wake word lets go of the mic, and listens again once no owner is left.
     */
    interface LiveListener {
        fun liveStarting(owner: String)
        fun liveStopped(owner: String)
    }

    @Volatile
    var liveListener: LiveListener? = null

    /** The last sign of a conversation: the session started, the user said something, the assistant spoke. */
    @Volatile
    var lastActivityMs = 0L
        private set

    fun touch(nowMs: Long) {
        lastActivityMs = nowMs
    }

    /** The session has been quiet long enough to close (only meaningful while it is live). */
    fun idleExpired(nowMs: Long): Boolean {
        val minutes = config.sleepAfterMinutes.coerceAtLeast(1)
        return nowMs - lastActivityMs >= minutes * 60_000L
    }

    /** Runs the wake word on a chunk. False when off in Settings or while the board answers. */
    fun detectWake(chunk: ByteArray): Boolean {
        val d = detector ?: return false
        if (!config.enabled) return false
        return d(chunk) && !suppressed()
    }

    /** Tests: back to the start. */
    internal fun resetForTest() {
        detector = null
        suppressed = { false }
        config = WakeWordConfig()
        personalJarvis = null
        personalCustom = null
        enroller = null
        sessionControl = null
        liveListener = null
        lastActivityMs = 0L
    }
}
