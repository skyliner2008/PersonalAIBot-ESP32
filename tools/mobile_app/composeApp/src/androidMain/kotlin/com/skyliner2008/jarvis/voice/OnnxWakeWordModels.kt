package com.skyliner2008.jarvis.voice

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import java.nio.FloatBuffer

/**
 * openWakeWord's models on ONNX Runtime (the same runtime as the local embeddings). Built from the model bytes:
 * assets/models/openwakeword/{melspectrogram, embedding_model, hey_jarvis_v0.1}.onnx in the app, files in tests.
 * One thread, single-threaded sessions: ~3 small inferences per 80 ms of audio.
 */
class OnnxWakeWordModels(mel: ByteArray, embedding: ByteArray, wakeWord: ByteArray) : WakeWordModels {
    private val env = OrtEnvironment.getEnvironment()
    private val options = OrtSession.SessionOptions().apply {
        setIntraOpNumThreads(1)
        setInterOpNumThreads(1)
    }
    private val melSession = env.createSession(mel, options)
    private val embSession = env.createSession(embedding, options)
    private val wakeSession = env.createSession(wakeWord, options)
    private val melInput = melSession.inputNames.first()
    private val embInput = embSession.inputNames.first()
    private val wakeInput = wakeSession.inputNames.first()

    override val featureFrames: Int =
        (wakeSession.inputInfo.values.first().info as ai.onnxruntime.TensorInfo).shape[1].toInt()

    override fun melspectrogram(samples: FloatArray): Array<FloatArray> {
        OnnxTensor.createTensor(env, FloatBuffer.wrap(samples), longArrayOf(1, samples.size.toLong())).use { t ->
            melSession.run(mapOf(melInput to t)).use { r ->
                // [time, 1, frames, 32] with time = 1
                @Suppress("UNCHECKED_CAST")
                val out = r[0].value as Array<Array<Array<FloatArray>>>
                return out[0][0]
            }
        }
    }

    override fun embedding(window: Array<FloatArray>): FloatArray {
        val buf = FloatBuffer.allocate(window.size * 32)
        for (frame in window) buf.put(frame)
        buf.flip()
        OnnxTensor.createTensor(env, buf, longArrayOf(1, window.size.toLong(), 32, 1)).use { t ->
            embSession.run(mapOf(embInput to t)).use { r ->
                // [1, 1, 1, 96]
                @Suppress("UNCHECKED_CAST")
                val out = r[0].value as Array<Array<Array<FloatArray>>>
                return out[0][0][0]
            }
        }
    }

    override fun score(features: Array<FloatArray>): Float {
        val buf = FloatBuffer.allocate(features.size * 96)
        for (f in features) buf.put(f)
        buf.flip()
        OnnxTensor.createTensor(env, buf, longArrayOf(1, features.size.toLong(), 96)).use { t ->
            wakeSession.run(mapOf(wakeInput to t)).use { r ->
                @Suppress("UNCHECKED_CAST")
                val out = r[0].value as Array<FloatArray>
                return out[0][0]
            }
        }
    }

    override fun close() {
        runCatching { melSession.close() }
        runCatching { embSession.close() }
        runCatching { wakeSession.close() }
        runCatching { options.close() }
    }
}
