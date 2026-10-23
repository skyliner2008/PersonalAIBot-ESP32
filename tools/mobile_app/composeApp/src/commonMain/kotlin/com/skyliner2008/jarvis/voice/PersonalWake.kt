package com.skyliner2008.jarvis.voice

import kotlin.io.encoding.Base64
import kotlin.io.encoding.ExperimentalEncodingApi
import kotlin.math.min
import kotlin.math.sqrt

/**
 * A wake word in the user's own voice (T091): a few recordings of it, each kept as the sequence of Google speech
 * embeddings openWakeWord computes anyway (96 values per 80 ms). Live audio matches when its last embeddings are close
 * to one of them under dynamic time warping (a slower / faster "จาวิส" still lines up). No training, no network, any
 * language - the price: it is tuned to the voice that recorded it.
 *
 * [threshold] is learned from the recordings themselves: how far apart the user's own takes are from each other.
 */
class PersonalWake(
    val templates: List<Array<FloatArray>>,
    val threshold: Float,
    /** What the user says, for the screen ("Hey Jarvis", "จาวิส"). */
    val label: String,
) {
    companion object {
        const val MIN_TAKES = 3
        private const val DIM = 96

        /** Builds the profile and its threshold from the takes (each: the embeddings of one recording). */
        fun build(takes: List<Array<FloatArray>>, label: String): PersonalWake? {
            val good = takes.filter { it.size >= 3 }
            if (good.size < MIN_TAKES) return null
            // each take against every other one: how much the user's own repetitions differ
            val d = ArrayList<Float>()
            for (i in good.indices) for (j in good.indices) if (i != j) d += dtw(good[i], good[j].toList())
            d.sort()
            // the second-worst pair (one odd take must not make every noise match) plus room for a new day's voice
            val ref = d[maxOf(0, d.size - 2)]
            return PersonalWake(good, ref * 1.15f, label)
        }

        /** 1 - cosine similarity. */
        internal fun cost(a: FloatArray, b: FloatArray): Float {
            var dot = 0f
            var na = 0f
            var nb = 0f
            for (k in a.indices) {
                dot += a[k] * b[k]
                na += a[k] * a[k]
                nb += b[k] * b[k]
            }
            if (na == 0f || nb == 0f) return 1f
            return 1f - dot / (sqrt(na) * sqrt(nb))
        }

        /**
         * Subsequence DTW: the template against the end of [live] (the match must end at the newest embedding, it may
         * start anywhere). The mean cost along the best path.
         */
        internal fun dtw(template: Array<FloatArray>, live: List<FloatArray>): Float {
            val m = template.size
            val n = live.size
            if (m == 0 || n == 0) return Float.MAX_VALUE
            val inf = Float.MAX_VALUE / 4
            // acc[i][j]: best total cost; len[i][j]: its path length
            val acc = Array(m) { FloatArray(n) { inf } }
            val len = Array(m) { IntArray(n) }
            for (j in 0 until n) {  // open start: the template may begin at any live frame
                acc[0][j] = cost(template[0], live[j])
                len[0][j] = 1
            }
            for (i in 1 until m) {
                for (j in 0 until n) {
                    val c = cost(template[i], live[j])
                    var best = acc[i - 1][j]
                    var bl = len[i - 1][j]
                    if (j > 0) {
                        if (acc[i - 1][j - 1] / len[i - 1][j - 1] < best / maxOf(bl, 1)) {
                            best = acc[i - 1][j - 1]
                            bl = len[i - 1][j - 1]
                        }
                        if (acc[i][j - 1] < inf && acc[i][j - 1] / len[i][j - 1] < best / maxOf(bl, 1)) {
                            best = acc[i][j - 1]
                            bl = len[i][j - 1]
                        }
                    }
                    if (best >= inf) continue
                    acc[i][j] = best + c
                    len[i][j] = bl + 1
                }
            }
            val end = n - 1
            return if (acc[m - 1][end] >= inf) Float.MAX_VALUE else acc[m - 1][end] / len[m - 1][end]
        }

        @OptIn(ExperimentalEncodingApi::class)
        fun decode(text: String?): PersonalWake? = try {
            if (text.isNullOrBlank()) null else {
                val parts = text.split('|')
                val label = parts[0]
                val threshold = parts[1].toFloat()
                val templates = parts.drop(2).map { b64 ->
                    val bytes = Base64.decode(b64)
                    val floats = FloatArray(bytes.size / 4) { i ->
                        val bits = (bytes[4 * i].toInt() and 0xFF) or ((bytes[4 * i + 1].toInt() and 0xFF) shl 8) or
                            ((bytes[4 * i + 2].toInt() and 0xFF) shl 16) or ((bytes[4 * i + 3].toInt() and 0xFF) shl 24)
                        Float.fromBits(bits)
                    }
                    Array(floats.size / DIM) { f -> floats.copyOfRange(f * DIM, f * DIM + DIM) }
                }
                if (templates.size < MIN_TAKES) null else PersonalWake(templates, threshold, label)
            }
        } catch (_: Exception) {
            null
        }
    }

    /** How close the newest live embeddings are to the user's recordings (smaller = closer). */
    fun distance(live: List<FloatArray>): Float {
        var best = Float.MAX_VALUE
        for (t in templates) {
            // the last 2x the template's length is plenty of room for a slower take
            val tail = if (live.size > 2 * t.size) live.subList(live.size - 2 * t.size, live.size) else live
            best = min(best, dtw(t, tail))
        }
        return best
    }

    /** A match at this sensitivity (threshold 0.35 = lenient ... 0.65 = strict, as the base wake word's). */
    fun matches(distance: Float, sensitivity: Float): Boolean = distance <= threshold * (1.5f - sensitivity)

    @OptIn(ExperimentalEncodingApi::class)
    fun encode(): String {
        val sb = StringBuilder()
        sb.append(label.replace("|", " ")).append('|').append(threshold)
        for (t in templates) {
            val bytes = ByteArray(t.size * DIM * 4)
            var p = 0
            for (frame in t) for (v in frame) {
                val bits = v.toRawBits()
                bytes[p++] = bits.toByte()
                bytes[p++] = (bits shr 8).toByte()
                bytes[p++] = (bits shr 16).toByte()
                bytes[p++] = (bits shr 24).toByte()
            }
            sb.append('|').append(Base64.encode(bytes))
        }
        return sb.toString()
    }
}
