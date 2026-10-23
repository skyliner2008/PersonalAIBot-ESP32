package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.voice.OnnxWakeWordModels
import com.skyliner2008.jarvis.voice.OpenWakeWord
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import java.io.File
import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertTrue

/**
 * The Kotlin port of openWakeWord against the official Python package (openwakeword 0.6, onnx) on the same audio:
 * Windows TTS "Hey Jarvis ..." / other speech, 1 s of silence around it, 80 ms chunks. reference_scores.json and the
 * .pcm files come from that run (wiki 17, T091).
 */
class OpenWakeWordTest {
    private val modelDir = File("src/androidMain/assets/models/openwakeword")

    private fun engine() = OpenWakeWord(
        OnnxWakeWordModels(
            File(modelDir, "melspectrogram.onnx").readBytes(),
            File(modelDir, "embedding_model.onnx").readBytes(),
            File(modelDir, "hey_jarvis_v0.1.onnx").readBytes()
        )
    )

    private fun resource(name: String) = javaClass.classLoader!!.getResourceAsStream("wakeword/$name")!!.readBytes()

    private val reference by lazy {
        Json.parseToJsonElement(String(resource("reference_scores.json"))).jsonObject
    }

    private fun scores(name: String): List<Float> {
        val pcm = resource("$name.pcm")
        val e = engine()
        val out = ArrayList<Float>()
        var i = 0
        while (i < pcm.size) {
            e.process(pcm, i, OpenWakeWord.CHUNK * 2)
            out += e.lastScore
            i += OpenWakeWord.CHUNK * 2
        }
        e.close()
        return out
    }

    private fun check(name: String) {
        val got = scores(name)
        val want = reference[name]!!.jsonArray.map { it.jsonPrimitive.content.toFloat() }
        assertTrue(got.size == want.size, "$name: ${got.size} chunks, reference ${want.size}")
        // the first 1.3 s still see the random start-up features (different random numbers than Python's)
        for (k in 17 until got.size) {
            assertTrue(abs(got[k] - want[k]) < 0.05f, "$name chunk $k: ${got[k]} vs reference ${want[k]}")
        }
    }

    @Test
    fun heyJarvisMatchesTheReference() {
        check("pos_zira")
        check("pos_david")
        assertTrue(scores("pos_zira").max() > 0.9f)
    }

    @Test
    fun otherSpeechStaysQuiet() {
        check("neg_zira")
        assertTrue(scores("neg_zira").max() < 0.1f)
    }

    @Test
    fun oddChunkSizesGiveTheSameScores() {
        // the mic hands out whatever AudioRecord read: 1280-sample chunks must still come out the same
        val pcm = resource("pos_zira.pcm")
        val a = engine()
        val b = engine()
        var bestA = 0f
        var bestB = 0f
        var i = 0
        while (i < pcm.size) {
            bestA = maxOf(bestA, a.process(pcm, i, minOf(OpenWakeWord.CHUNK * 2, pcm.size - i)))
            i += OpenWakeWord.CHUNK * 2
        }
        i = 0
        while (i < pcm.size) {
            val n = minOf(1234, pcm.size - i)  // even, not a multiple of a chunk
            bestB = maxOf(bestB, b.process(pcm, i, n))
            i += n
        }
        assertTrue(abs(bestA - bestB) < 1e-4f, "$bestA vs $bestB")
        a.close()
        b.close()
    }
}
