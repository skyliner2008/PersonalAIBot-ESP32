package com.skyliner2008.jarvis.voice

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import kotlin.concurrent.thread

/**
 * The phone's wake word on openWakeWord (wiki 17, T091): [startListening] runs its own AudioRecord while no Live
 * session holds the mic (WakeWordController decides); [WakeScorer] makes the call (averaged score, the user's own
 * recordings for modes 1 / 2). Also records the enrolment takes ([WakeGate.Enroller]).
 * Models: assets/models/openwakeword (CC BY-NC-SA 4.0, see the README there). ~6 small ONNX runs per 80 ms.
 */
object WakeWordService {
    private const val TAG = "WakeWord"
    /** After a hit: no second one for this long (the same "Jarvis" spans a few chunks). */
    private const val REFRACTORY_MS = 2_000L

    private val lock = Any()
    private var engine: WakeScorer? = null
    @Volatile private var loadFailed = false
    private var lastHitMs = 0L

    @Volatile private var listening = false
    private var listenThread: Thread? = null

    /** The models are loaded (or [load] has not been tried yet and may still work). */
    val available: Boolean get() = !loadFailed

    /** Loads the models (a background thread: ~0.3 s) and hands the detector to [WakeGate]. Safe to call again. */
    fun load(context: Context) {
        if (engine != null || loadFailed) return
        val app = context.applicationContext
        appContext = app
        thread(name = "wakeword-load", isDaemon = true) {
            synchronized(lock) {
                if (engine != null) return@thread
                try {
                    fun asset(name: String) = app.assets.open("models/openwakeword/$name").use { it.readBytes() }
                    engine = WakeScorer(
                        OnnxWakeWordModels(asset("melspectrogram.onnx"), asset("embedding_model.onnx"), asset("hey_jarvis_v0.1.onnx"))
                    )
                    WakeGate.detector = { chunk -> feed(chunk) }
                    WakeGate.enroller = enroller
                    Log.i(TAG, "openWakeWord ready (hey jarvis)")
                } catch (e: Throwable) {
                    loadFailed = true
                    Log.e(TAG, "wake word models failed to load - the old energy wake stays", e)
                }
            }
        }
    }

    // logcat (tag WakeWord): the best score every 5 s while listening, and every near miss - to tune the threshold
    private var windowBest = 0f
    private var windowStart = 0L

    private var windowBestDistance = Float.MAX_VALUE

    /** One chunk of 16 kHz PCM16 -> true when the wake word just ended in it. */
    fun feed(chunk: ByteArray): Boolean = synchronized(lock) {
        val e = engine ?: return false
        val cfg = WakeGate.config
        val profile = if (cfg.mode == WakeMode.CUSTOM) WakeGate.personalCustom else WakeGate.personalJarvis
        val hit = e.process(chunk, cfg, WakeGate.personalJarvis, WakeGate.personalCustom)
        val score = e.lastScore
        val dist = e.lastDistance
        val now = System.currentTimeMillis()
        if (score > windowBest) windowBest = score
        if (dist < windowBestDistance) windowBestDistance = dist
        val need = cfg.threshold * WakeScorer.AVERAGE_SCALE
        if (cfg.mode != WakeMode.CUSTOM && score >= 0.2f && score < need) Log.d(TAG, "near miss: average score %.2f (need %.2f)".format(score, need))
        if (now - windowStart >= 5_000) {
            val d = if (windowBestDistance == Float.MAX_VALUE || profile == null) "-" else
                "%.3f (match <= %.3f)".format(windowBestDistance, profile.threshold * (1.5f - cfg.threshold))
            Log.d(TAG, "listening (mode ${cfg.mode}) - best in the last 5 s: average score %.3f (need %.2f), voice distance %s"
                .format(windowBest, need, d))
            windowBest = 0f
            windowBestDistance = Float.MAX_VALUE
            windowStart = now
        }
        if (hit && now - lastHitMs >= REFRACTORY_MS) {
            lastHitMs = now
            // no reset here: it re-runs 4 s of start-up audio (~0.2 s) on the mic thread and the words right after
            // "Jarvis" would be lost; the refractory time keeps the same word from firing twice
            Log.i(TAG, "wake word heard (mode ${cfg.mode}, average score %.2f, voice distance %s)".format(
                score, if (dist == Float.MAX_VALUE) "-" else "%.3f".format(dist)))
            true
        } else false
    }

    /** Enrolment: a take is recorded on its own AudioRecord (the listener lets go), analysed by the same engine. */
    private val enroller = object : WakeGate.Enroller {
        override suspend fun record(ms: Int): ByteArray? = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            val ctx = appContext ?: return@withContext null
            if (ctx.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return@withContext null
            WakeGate.liveListener?.liveStarting("enrol")
            try {
                delay(300)  // the listener's AudioRecord is released
                recordPcm(ms)?.also { keepTake(ctx, it) }
            } finally {
                WakeGate.liveListener?.liveStopped("enrol")
            }
        }

        /**
         * The last 5 takes as raw 16 kHz PCM16 in the app's own external folder (Android/data/<app>/files/wake_enrol,
         * `adb pull` - deleted with the app): when the wake word misbehaves, the exact audio can be checked on a PC.
         */
        private var takeNo = 0

        private fun keepTake(ctx: Context, pcm: ByteArray) {
            runCatching {
                val dir = java.io.File(ctx.getExternalFilesDir(null), "wake_enrol").apply { mkdirs() }
                java.io.File(dir, "take_${takeNo++ % 5}.pcm").writeBytes(pcm)
            }
        }

        override fun analyse(pcm: ByteArray): EnrolTake? = synchronized(lock) {
            val t = engine?.enrol(pcm) ?: return@synchronized null
            Log.i(TAG, "enrol take: speech %d ms, %d embeddings, stock model average score %.2f"
                .format(t.speechMs, t.template.size, t.stockScore))
            t
        }

        override fun hold(on: Boolean) {
            if (on) WakeGate.liveListener?.liveStarting("enrol-dialog") else WakeGate.liveListener?.liveStopped("enrol-dialog")
        }

        override fun saved(mode: Int, profile: PersonalWake?) = logProfile("saved", mode, profile)
    }

    /** One line per profile (load / save): what the user said, how many takes, the learned match threshold. */
    fun logProfile(what: String, mode: Int, profile: PersonalWake?) {
        val name = if (mode == WakeMode.CUSTOM) "own word (mode 2)" else "Hey Jarvis + my voice (mode 1)"
        if (profile == null) Log.i(TAG, "voice profile $what: $name - none")
        else Log.i(TAG, "voice profile $what: $name \"${profile.label}\", ${profile.templates.size} takes " +
            "(${profile.templates.joinToString("/") { it.size.toString() }} embeddings), match threshold %.3f".format(profile.threshold))
    }

    @SuppressLint("MissingPermission")
    private fun recordPcm(ms: Int): ByteArray? {
        val rate = OpenWakeWord.SAMPLE_RATE
        val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val record = try {
            AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, rate, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT, maxOf(min, rate * 2))
        } catch (e: Exception) {
            return null
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            return null
        }
        val out = ByteArray(rate * 2 * ms / 1000)
        var got = 0
        try {
            record.startRecording()
            while (got < out.size) {
                val n = record.read(out, got, minOf(4096, out.size - got))
                if (n <= 0) break
                got += n
            }
        } finally {
            runCatching { record.stop() }
            record.release()
        }
        return if (got < out.size / 2) null else out.copyOf(got)
    }

    private suspend fun delay(ms: Long) = kotlinx.coroutines.delay(ms)
    @Volatile private var appContext: Context? = null

    /**
     * Listens on its own (the Live mic is off) until [stopListening]. [onHeard] runs on the listener thread.
     * False = no models, no permission or the mic is busy (the caller keeps the old detector).
     */
    @SuppressLint("MissingPermission")
    fun startListening(context: Context, onHeard: () -> Unit): Boolean {
        if (listening) return true
        if (engine == null) return false
        if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false
        val rate = OpenWakeWord.SAMPLE_RATE
        val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val record = try {
            AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, rate, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT, maxOf(min, OpenWakeWord.CHUNK * 2 * 4))
        } catch (e: Exception) {
            Log.w(TAG, "AudioRecord failed", e)
            return false
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            return false
        }
        listening = true
        listenThread = thread(name = "wakeword-listen", isDaemon = true) {
            val buf = ByteArray(OpenWakeWord.CHUNK * 2)
            try {
                synchronized(lock) { engine?.reset() }  // what Live heard last is not part of what comes now
                record.startRecording()
                Log.i(TAG, "listening for the wake word (own mic)")
                when (WakeGate.config.mode) {  // which voice profile this listener uses
                    WakeMode.PERSONAL_JARVIS -> logProfile("in use", WakeMode.PERSONAL_JARVIS, WakeGate.personalJarvis)
                    WakeMode.CUSTOM -> logProfile("in use", WakeMode.CUSTOM, WakeGate.personalCustom)
                }
                while (listening) {
                    val n = record.read(buf, 0, buf.size)
                    if (n <= 0) continue
                    if (WakeGate.detectWake(if (n == buf.size) buf else buf.copyOf(n))) {
                        listening = false
                        onHeard()
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "wake word listener stopped", e)
            } finally {
                runCatching { record.stop() }
                record.release()
                listening = false
            }
        }
        return true
    }

    fun stopListening() {
        listening = false
        val t = listenThread
        listenThread = null
        if (t != null && t !== Thread.currentThread()) runCatching { t.join(500) }  // the mic is free for Live
    }

    val isListening: Boolean get() = listening
}
