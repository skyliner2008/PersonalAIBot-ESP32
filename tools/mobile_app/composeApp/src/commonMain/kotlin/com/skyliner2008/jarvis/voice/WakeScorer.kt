package com.skyliner2008.jarvis.voice

/**
 * The decision "the wake word was just said" (T091), on top of openWakeWord:
 *
 *  - **Average score.** One engine cuts the audio into 80 ms chunks; where "Jarvis" falls inside them changes the score
 *    a lot (a borderline "Hey Jarvis" shifted by 0-70 ms: 0.19 ... 0.98), so the same recording woke sometimes and
 *    sometimes not. Two engines run 40 ms apart and the score is the mean of the newest score of each ([AVERAGE] = 2:
 *    0.47 ... 0.99 on the same test; 3 scores pulled it down to 0.32 - PersonalWakeTest).
 *  - **Mode 1 ([WakeMode.PERSONAL_JARVIS]).** "Hey Jarvis" as before (the stock model), or the user's own recordings of
 *    it ([PersonalWake]) - checked whenever someone talks, not only when the stock model hints at it: the user's own
 *    "Hey Jarvis" got 0.00-0.08 from the official openWakeWord too (T091), while it matched the other takes at
 *    0.016-0.052 against a threshold of ~0.08 (other speech: 0.21-0.25).
 *  - **Mode 2 ([WakeMode.CUSTOM]).** Any word (Thai too): only the user's recordings count, while someone is talking.
 *
 * One caller (the mic thread). [lastScore] / [lastDistance] are for the log.
 */
class WakeScorer(models: WakeWordModels) {
    companion object {
        const val AVERAGE = 2
        /** The averaged score sits lower than single-chunk peaks: the setting's threshold x this. */
        const val AVERAGE_SCALE = 0.8f
        private const val LIVE_EMBEDDINGS = 40
        private const val SPEECH_RMS = 0.015f
        private const val SPEECH_RECENT = 15  // chunks (1.2 s)
    }

    private val a = OpenWakeWord(models, seed = 1)
    private val b = OpenWakeWord(models, seed = 2)
    private var bStarted = false
    private val scores = ArrayDeque<Float>()
    private val live = ArrayDeque<FloatArray>()
    private var sinceSpeech = Int.MAX_VALUE
    private var fired = false
    private var cfg = WakeWordConfig()
    private var jarvis: PersonalWake? = null
    private var custom: PersonalWake? = null

    var lastScore = 0f
        private set
    var lastDistance = Float.MAX_VALUE
        private set

    init {
        a.onChunk = { score, emb, rms -> onA(score, emb, rms) }
        b.onChunk = { score, _, _ -> onScore(score) }
    }

    /** A clean start (the listener starts again). */
    fun reset() {
        a.reset()
        b.reset()
        bStarted = false
        scores.clear()
        live.clear()
        sinceSpeech = Int.MAX_VALUE
    }

    /**
     * 16 kHz PCM16 from the mic. True when the wake word was said in it. [personalJarvis] / [personalCustom]: the
     * user's recordings for modes 1 / 2 (null = none yet: mode 1 works as the stock model, mode 2 never fires).
     */
    fun process(pcm: ByteArray, config: WakeWordConfig, personalJarvis: PersonalWake?, personalCustom: PersonalWake?): Boolean {
        cfg = config
        jarvis = personalJarvis
        custom = personalCustom
        fired = false
        if (!bStarted) {  // engine b runs half a chunk behind: its chunks end between a's
            b.process(ShortArray(OpenWakeWord.CHUNK / 2))
            bStarted = true
        }
        a.process(pcm)
        b.process(pcm)
        return fired
    }

    private fun onA(score: Float, embedding: FloatArray, rms: Float) {
        live.addLast(embedding)
        while (live.size > LIVE_EMBEDDINGS) live.removeFirst()
        sinceSpeech = if (rms >= SPEECH_RMS) 0 else if (sinceSpeech == Int.MAX_VALUE) Int.MAX_VALUE else sinceSpeech + 1
        onScore(score)
        // the user's own recordings, compared once per 80 ms
        val mode = cfg.mode
        val profile = when (mode) {
            WakeMode.PERSONAL_JARVIS -> jarvis
            WakeMode.CUSTOM -> custom
            else -> null
        } ?: return
        if (sinceSpeech > SPEECH_RECENT) {  // nobody talking: nothing to compare (and no CPU spent)
            lastDistance = Float.MAX_VALUE
            return
        }
        lastDistance = profile.distance(live)
        if (profile.matches(lastDistance, cfg.threshold)) fired = true
    }

    private fun onScore(score: Float) {
        scores.addLast(score)
        while (scores.size > AVERAGE) scores.removeFirst()
        lastScore = scores.sum() / scores.size
        // the stock "hey jarvis" model (modes 0 and 1)
        if (cfg.mode != WakeMode.CUSTOM && lastScore >= cfg.threshold * AVERAGE_SCALE) fired = true
    }

    /**
     * One recording for a personal profile: the embeddings over the speech in it (plus the 0.24 s after it - an
     * embedding looks 0.78 s back) and the stock model's best averaged score (how well "Hey Jarvis" already works).
     */
    fun enrol(pcm: ByteArray): EnrolTake {
        reset()
        val embs = ArrayList<FloatArray>()
        val loud = ArrayList<Float>()
        var best = 0f
        val keep = a.onChunk
        a.onChunk = { score, emb, rms ->
            embs += emb
            loud += rms
            onScore(score)
            if (lastScore > best) best = lastScore
        }
        try {
            // a little silence before and after: the engine settles, and a word said right at the end is complete
            val pad = ByteArray(OpenWakeWord.SAMPLE_RATE / 2 * 2)
            b.process(ShortArray(OpenWakeWord.CHUNK / 2))
            bStarted = true
            for (part in listOf(pad, pcm, pad)) {
                a.process(part)
                b.process(part)
            }
        } finally {
            a.onChunk = keep
        }
        val quiet = loud.sorted().let { if (it.isEmpty()) 0f else it[it.size / 5] }
        val level = maxOf(SPEECH_RMS, quiet * 3f)
        val first = loud.indexOfFirst { it >= level }
        val last = loud.indexOfLast { it >= level }
        reset()
        if (first < 0 || last - first < 2) return EnrolTake(emptyArray(), best, 0)
        val from = (first + 1).coerceAtMost(embs.size - 1)
        val to = (last + 3).coerceAtMost(embs.size - 1)
        return EnrolTake(Array(to - from + 1) { embs[from + it] }, best, (last - first + 1) * 80)
    }

    fun close() {
        a.close()
    }
}

/** The wake word modes (Settings → คำปลุก). */
object WakeMode {
    /** "Hey Jarvis", the stock openWakeWord model. */
    const val STANDARD = 0
    /** "Hey Jarvis" helped by the user's own recordings of it. */
    const val PERSONAL_JARVIS = 1
    /** The user's own word (Thai too), from the user's recordings only. */
    const val CUSTOM = 2
}

/** One enrolment recording: [template] empty = no speech heard in it. */
class EnrolTake(val template: Array<FloatArray>, val stockScore: Float, val speechMs: Int)
