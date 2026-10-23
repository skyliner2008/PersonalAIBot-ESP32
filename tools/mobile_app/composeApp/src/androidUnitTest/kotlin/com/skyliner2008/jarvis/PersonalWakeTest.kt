package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.voice.OnnxWakeWordModels
import com.skyliner2008.jarvis.voice.OpenWakeWord
import com.skyliner2008.jarvis.voice.PersonalWake
import com.skyliner2008.jarvis.voice.WakeMode
import com.skyliner2008.jarvis.voice.WakeScorer
import com.skyliner2008.jarvis.voice.WakeWordConfig
import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/**
 * The wake word in the user's own voice and the averaged score, on real models and real audio (T091).
 * Windows TTS stands in for the user: "Hey Jarvis" said fast (the stock model gives it 0.29, like a Thai accent's
 * 0.36-0.49) and "Hello robot" for a word of the user's own (no Thai TTS voice on the PC). David's voice enrols.
 */
class PersonalWakeTest {
    private val modelDir = File("src/androidMain/assets/models/openwakeword")

    private fun models() = OnnxWakeWordModels(
        File(modelDir, "melspectrogram.onnx").readBytes(),
        File(modelDir, "embedding_model.onnx").readBytes(),
        File(modelDir, "hey_jarvis_v0.1.onnx").readBytes()
    )

    private fun pcm(path: String) = javaClass.classLoader!!.getResourceAsStream("wakeword/$path")!!.readBytes()
    private val second = ByteArray(32000)

    private fun profile(scorer: WakeScorer, prefix: String, label: String): PersonalWake {
        val takes = (0 until 5).map { scorer.enrol(pcm("enrol/$prefix$it.pcm")) }
        takes.forEach { assertTrue(it.template.isNotEmpty(), "speech found in every take") }
        return assertNotNull(PersonalWake.build(takes.map { it.template }, label))
    }

    /** 1 s of silence, the clip, 1 s of silence - fed in odd-sized pieces as the mic does. */
    private fun fires(scorer: WakeScorer, clip: ByteArray, cfg: WakeWordConfig, jarvis: PersonalWake?, custom: PersonalWake?): Boolean {
        scorer.reset()
        val all = second + clip + second
        var hit = false
        var i = 0
        while (i < all.size) {
            val n = minOf(1234, all.size - i)
            if (scorer.process(all.copyOfRange(i, i + n), cfg, jarvis, custom)) hit = true
            i += n
        }
        return hit
    }

    @Test
    fun heyJarvisInTheUsersVoiceWakesWhereTheStockModelDoesNot() {
        val s = WakeScorer(models())
        val mine = profile(s, "fast_take", "Hey Jarvis")
        val fast = pcm("enrol/fast_pos.pcm")
        assertFalse(fires(s, fast, WakeWordConfig(mode = WakeMode.STANDARD), mine, null), "stock model: 0.29, too low")
        assertTrue(fires(s, fast, WakeWordConfig(mode = WakeMode.PERSONAL_JARVIS), mine, null), "with the user's voice")
        assertTrue(fires(s, pcm("pos_zira.pcm"), WakeWordConfig(mode = WakeMode.PERSONAL_JARVIS), mine, null),
            "a clear \"Hey Jarvis\" still wakes in mode 1")
        for (neg in listOf("enrol/custom_neg1.pcm", "enrol/custom_neg2.pcm", "enrol/custom_neg3.pcm", "neg_zira.pcm")) {
            assertFalse(fires(s, pcm(neg), WakeWordConfig(mode = WakeMode.PERSONAL_JARVIS), mine, null), neg)
        }
        s.close()
    }

    @Test
    fun mode1MatchesTheUsersVoiceEvenWhenTheStockModelGivesNothing() {
        // the user's own "Hey Jarvis" got 0.00-0.08 from the official model: mode 1 must not wait for it (T091).
        // "Hello robot" (stock score 0.00) stands in for such a recording here.
        val s = WakeScorer(models())
        val mine = profile(s, "custom_take", "Hey Jarvis")
        val cfg = WakeWordConfig(mode = WakeMode.PERSONAL_JARVIS)
        assertTrue(fires(s, pcm("enrol/custom_pos.pcm"), cfg, mine, null))
        for (neg in listOf("enrol/custom_neg1.pcm", "enrol/custom_neg2.pcm", "enrol/custom_neg3.pcm", "neg_zira.pcm")) {
            assertFalse(fires(s, pcm(neg), cfg, mine, null), neg)
        }
        s.close()
    }

    @Test
    fun theUsersOwnWordWakesAndOtherSpeechDoesNot() {
        val s = WakeScorer(models())
        val word = profile(s, "custom_take", "Hello robot")
        val cfg = WakeWordConfig(mode = WakeMode.CUSTOM)
        assertTrue(fires(s, pcm("enrol/custom_pos.pcm"), cfg, null, word), "in a sentence")
        assertTrue(fires(s, pcm("enrol/custom_pos_slow.pcm"), cfg, null, word), "said slowly")
        for (neg in listOf("enrol/custom_neg1.pcm", "enrol/custom_neg2.pcm", "enrol/custom_neg3.pcm", "pos_zira.pcm", "neg_zira.pcm")) {
            assertFalse(fires(s, pcm(neg), cfg, null, word), neg)
        }
        // nothing recorded yet: mode 2 never fires
        assertFalse(fires(s, pcm("enrol/custom_pos.pcm"), cfg, null, null))
        s.close()
    }

    @Test
    fun theProfileSurvivesSaving() {
        val s = WakeScorer(models())
        val word = profile(s, "custom_take", "จาวิส")
        val back = assertNotNull(PersonalWake.decode(word.encode()))
        assertEquals("จาวิส", back.label)
        assertEquals(word.templates.size, back.templates.size)
        assertEquals(word.threshold, back.threshold)
        assertTrue(fires(s, pcm("enrol/custom_pos.pcm"), WakeWordConfig(mode = WakeMode.CUSTOM), null, back))
        s.close()
    }

    @Test
    fun theAverageIsSteadyWhereverTheWordFallsInTheChunks() {
        // a borderline "Hey Jarvis" (stock peak ~0.45, like the user's), shifted by 0..70 ms: where the word falls in
        // the 80 ms chunks moves one engine's peak around; the 2-phase average moves less
        val clip = pcm("enrol/fast_take1.pcm")
        val single = ArrayList<Float>()
        val averaged = ArrayList<Float>()
        val s = WakeScorer(models())
        val one = OpenWakeWord(models())
        for (shift in 0 until 8) {
            val audio = ByteArray(shift * 160 * 2) + clip
            one.reset()
            var best = 0f
            var i = 0
            while (i < audio.size) {
                best = maxOf(best, one.process(audio, i, minOf(2560, audio.size - i)))
                i += 2560
            }
            single += best
            s.reset()
            var bestAvg = 0f
            i = 0
            while (i < audio.size) {
                val n = minOf(2560, audio.size - i)
                s.process(audio.copyOfRange(i, i + n), WakeWordConfig(), null, null)
                bestAvg = maxOf(bestAvg, s.lastScore)
                i += n
            }
            averaged += bestAvg
        }
        val spreadSingle = single.max() - single.min()
        val spreadAvg = averaged.max() - averaged.min()
        println("single-engine peaks $single (spread $spreadSingle) / averaged $averaged (spread $spreadAvg)")
        assertTrue(spreadAvg < spreadSingle, "the average swings less than one engine")
        s.close()
        one.close()
    }
}
