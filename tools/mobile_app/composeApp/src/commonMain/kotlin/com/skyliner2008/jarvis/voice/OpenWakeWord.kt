package com.skyliner2008.jarvis.voice

import kotlin.random.Random

/**
 * The three ONNX models of openWakeWord (https://github.com/dscripka/openWakeWord): mel spectrogram, Google's
 * speech embedding and the wake word classifier ("hey jarvis"). Android: OnnxWakeWordModels (assets/models/openwakeword).
 */
interface WakeWordModels {
    /** int16 samples (as floats, not scaled) -> mel frames x 32, raw (before openWakeWord's x / 10 + 2). */
    fun melspectrogram(samples: FloatArray): Array<FloatArray>

    /** One window of 76 x 32 mel frames -> a 96-value embedding. */
    fun embedding(window: Array<FloatArray>): FloatArray

    /** The last [featureFrames] embeddings -> the wake word score 0..1. */
    fun score(features: Array<FloatArray>): Float

    /** How many embeddings the classifier takes (16 for hey_jarvis). */
    val featureFrames: Int

    fun close()
}

/**
 * openWakeWord's streaming pipeline, ported from openwakeword/utils.py (AudioFeatures) and model.py (predict) 0.6:
 * 16 kHz mono PCM16 in 80 ms chunks (1280 samples) -> mel on the chunk plus 3 hops of history -> one embedding over
 * the last 76 mel frames -> the classifier over the last [WakeWordModels.featureFrames] embeddings. The first five
 * predictions after a reset are 0, as in the reference. Not thread-safe: one caller (the mic thread).
 * Checked against the official Python package on the same audio (OpenWakeWordTest).
 */
class OpenWakeWord(private val models: WakeWordModels, private val seed: Int = 1) {
    companion object {
        const val SAMPLE_RATE = 16000
        const val CHUNK = 1280              // 80 ms
        private const val MEL_HISTORY = 160 * 3
        private const val WINDOW = 76
        private const val MEL_MAX = 10 * 97
        private const val FEATURE_MAX = 120
        private const val WARMUP_PREDICTIONS = 5
    }

    private val history = ShortArray(MEL_HISTORY)  // the samples before the current chunk
    private var historyLen = 0
    private val pending = ShortArray(CHUNK)
    private var pendingLen = 0
    private val mel = ArrayDeque<FloatArray>()
    private val features = ArrayDeque<FloatArray>()
    private var predictions = 0

    /** The score of the last completed chunk. */
    var lastScore = 0f
        private set

    init {
        reset()
    }

    /** Back to the state of a fresh model: buffers of ones / noise embeddings, warm-up again. */
    fun reset() {
        historyLen = 0
        pendingLen = 0
        predictions = 0
        lastScore = 0f
        mel.clear()
        repeat(WINDOW) { mel.addLast(FloatArray(32) { 1f }) }
        features.clear()
        // the reference starts its feature buffer with the embeddings of 4 s of random noise in [-1000, 1000)
        val rnd = Random(seed)
        val noise = FloatArray(SAMPLE_RATE * 4) { (rnd.nextInt(2000) - 1000).toFloat() }
        val spec = models.melspectrogram(noise).map { transform(it) }
        var i = 0
        while (i + WINDOW <= spec.size) {
            features.addLast(models.embedding(Array(WINDOW) { spec[i + it] }))
            i += 8
        }
        trimFeatures()
    }

    /** 16 kHz mono PCM16 little endian. Returns the highest score of the chunks it completed (0 = none). */
    fun process(pcm: ByteArray, offset: Int = 0, length: Int = pcm.size - offset): Float {
        var best = 0f
        var i = offset
        val end = offset + (length and 1.inv())
        while (i < end) {
            pending[pendingLen++] = ((pcm[i].toInt() and 0xFF) or (pcm[i + 1].toInt() shl 8)).toShort()
            i += 2
            if (pendingLen == CHUNK) {
                val s = step()
                if (s > best) best = s
                pendingLen = 0
            }
        }
        return best
    }

    fun process(samples: ShortArray): Float {
        var best = 0f
        for (s in samples) {
            pending[pendingLen++] = s
            if (pendingLen == CHUNK) {
                val v = step()
                if (v > best) best = v
                pendingLen = 0
            }
        }
        return best
    }

    private fun transform(frame: FloatArray) = FloatArray(frame.size) { frame[it] / 10f + 2f }

    private fun step(): Float {
        // mel over (up to) 3 hops of history + the new chunk, as raw_data_buffer[-1280 - 480:]
        val input = FloatArray(historyLen + CHUNK)
        for (k in 0 until historyLen) input[k] = history[k].toFloat()
        for (k in 0 until CHUNK) input[historyLen + k] = pending[k].toFloat()
        for (frame in models.melspectrogram(input)) mel.addLast(transform(frame))
        while (mel.size > MEL_MAX) mel.removeFirst()
        // keep the last 480 samples for the next chunk
        keepTail(pending, history)
        historyLen = MEL_HISTORY

        if (mel.size >= WINDOW) {
            val from = mel.size - WINDOW
            features.addLast(models.embedding(Array(WINDOW) { mel[from + it] }))
            trimFeatures()
        }
        val n = models.featureFrames
        val score = if (features.size >= n) {
            val from = features.size - n
            models.score(Array(n) { features[from + it] })
        } else 0f
        predictions++
        lastScore = if (predictions <= WARMUP_PREDICTIONS) 0f else score
        onChunk?.invoke(lastScore, features.last(), rms(pending))
        return lastScore
    }

    /**
     * Every completed 80 ms chunk: its score, the speech embedding that ends with it (96 values, also what the personal
     * voice profile matches) and the chunk's loudness (RMS 0..1). WakeScorer listens here.
     */
    var onChunk: ((score: Float, embedding: FloatArray, rms: Float) -> Unit)? = null

    private fun rms(s: ShortArray): Float {
        var sum = 0.0
        for (v in s) sum += (v / 32768.0) * (v / 32768.0)
        return kotlin.math.sqrt(sum / s.size).toFloat()
    }

    private fun trimFeatures() {
        while (features.size > FEATURE_MAX) features.removeFirst()
    }

    private fun keepTail(src: ShortArray, dst: ShortArray) {
        val off = src.size - dst.size
        for (k in dst.indices) dst[k] = src[off + k]
    }

    fun close() = models.close()
}
