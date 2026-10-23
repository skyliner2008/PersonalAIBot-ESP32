package com.skyliner2008.jarvis.data.embedding

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import android.content.Context
import android.util.JsonReader
import android.util.JsonToken
import com.skyliner2008.jarvis.logDebug
import com.skyliner2008.jarvis.logError
import io.ktor.client.*
import io.ktor.client.engine.android.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.*
import io.ktor.utils.io.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.FloatBuffer
import java.nio.LongBuffer

/**
 * AndroidLocalOnnxManager — ดาวน์โหลดและรัน EmbeddingGemma 2 (ส่วนข้อความ, q4) ด้วย ONNX Runtime บน Android
 *
 * ไฟล์ (pin commit ของ onnx-community/embeddinggemma-2-ONNX ไว้ — repo เปลี่ยนแล้วแอปไม่พัง):
 *   - onnx/model_q4.onnx       graph (~0.5 MB)
 *   - onnx/model_q4.onnx_data  น้ำหนัก (~174 MB) — graph อ้างไฟล์นี้ด้วยชื่อ จึงต้องเก็บชื่อเดิมไว้ข้างกัน
 *   - tokenizer.json           (~32 MB) — แปลงเป็น tokenizer.bin (~6 MB) ครั้งแรกแล้วลบทิ้ง
 * ไม่โหลด vision/audio encoder — ช่อง *_features ของโมเดลส่ง tensor ว่าง [0, 512]
 */
class AndroidLocalOnnxManager(
    private val context: Context,
    private val provider: LocalOnnxEmbeddingProvider
) {
    private val baseUrl =
        "https://huggingface.co/onnx-community/embeddinggemma-2-ONNX/resolve/daa72c51243991dfcaf9f9137d2c573d8f7790c0"

    private val dir = File(context.filesDir, "embeddinggemma2")
    private val modelFile = File(dir, "model_q4.onnx")
    private val weightsFile = File(dir, "model_q4.onnx_data")
    private val tokenizerJson = File(dir, "tokenizer.json")
    private val tokenizerCache = File(dir, "tokenizer.bin")

    /** (url, ไฟล์, ขนาดโดยประมาณ — ใช้ถ่วงน้ำหนัก progress) */
    private val downloads = listOf(
        Triple("$baseUrl/onnx/model_q4.onnx", modelFile, 0.5f),
        Triple("$baseUrl/onnx/model_q4.onnx_data", weightsFile, 174f),
        Triple("$baseUrl/tokenizer.json", tokenizerJson, 32f)
    )

    /** โมเดลเดิม (MiniLM) — ลบเพื่อคืนพื้นที่ ~120 MB */
    private val legacyFiles = listOf(
        File(context.filesDir, "multilingual_mini_lm_q4.onnx"),
        File(context.filesDir, "multilingual_tokenizer.json")
    )

    private val loadLock = Mutex()
    @Volatile private var ortEnv: OrtEnvironment? = null
    @Volatile private var ortSession: OrtSession? = null
    @Volatile private var tokenizer: GemmaBpeTokenizer? = null
    /** ช่อง *_features ของโมเดล → ความกว้าง (512) สำหรับ tensor ว่าง */
    @Volatile private var featureInputs: Map<String, Long> = emptyMap()

    val downloadProgress = MutableStateFlow(-1f)

    private fun filesReady() =
        modelFile.exists() && weightsFile.exists() && (tokenizerCache.exists() || tokenizerJson.exists())

    suspend fun initOrDownload() {
        withContext(Dispatchers.IO) {
            removeLegacyFiles() // ไฟล์เก่า ~415 MB — ไม่ลบบน main thread
            if (filesReady()) {
                loadModel()
            } else {
                // ยังไม่โหลดอัตโนมัติ รอให้ UI กดดาวน์โหลดเอง
                logDebug("AndroidONNX", "EmbeddingGemma 2 files not found. Ready to download.")
            }
        }
    }

    suspend fun downloadModelFiles() {
        withContext(Dispatchers.IO) {
            try {
                removeLegacyFiles()
                dir.mkdirs()
                downloadProgress.value = 0f
                val total = downloads.sumOf { it.third.toDouble() }.toFloat()
                var done = 0f
                HttpClient(Android).use { client ->
                    for ((url, dest, weight) in downloads) {
                        val skip = dest.exists() || (dest == tokenizerJson && tokenizerCache.exists())
                        if (!skip) {
                            logDebug("AndroidONNX", "Downloading ${dest.name}: $url")
                            val base = done
                            downloadFile(client, url, dest) { p ->
                                downloadProgress.value = (base + p * weight) / total
                            }
                        }
                        done += weight
                        downloadProgress.value = done / total
                    }
                }
                downloadProgress.value = 1f
                loadModel()
            } catch (e: Exception) {
                logError("AndroidONNX", "Failed to download model", e)
                downloadProgress.value = -1f
            }
        }
    }

    /** เขียนลง .part แล้วค่อยเปลี่ยนชื่อ — ไฟล์ที่ขาดกลางทางจะไม่ถูกนับว่าโหลดเสร็จ */
    private suspend fun downloadFile(client: HttpClient, url: String, dest: File, onProgress: (Float) -> Unit) {
        val part = File(dest.path + ".part")
        client.prepareGet(url).execute { response ->
            if (!response.status.isSuccess()) error("HTTP ${response.status.value} for ${dest.name}")
            val contentLength = response.headers["Content-Length"]?.toLongOrNull()
            var bytesReadTotal = 0L

            val channel: ByteReadChannel = response.bodyAsChannel()
            part.outputStream().use { output ->
                val buffer = ByteArray(64 * 1024)
                while (!channel.isClosedForRead) {
                    val read = channel.readAvailable(buffer, 0, buffer.size)
                    if (read <= 0) break
                    output.write(buffer, 0, read)
                    bytesReadTotal += read
                    if (contentLength != null && contentLength > 0) {
                        onProgress(bytesReadTotal.toFloat() / contentLength.toFloat())
                    }
                }
            }
            if (contentLength != null && bytesReadTotal != contentLength) {
                part.delete()
                error("${dest.name}: got $bytesReadTotal of $contentLength bytes")
            }
        }
        if (!part.renameTo(dest)) error("Cannot move ${part.name} into place")
    }

    /**
     * โหลดครั้งเดียว — กด "✓ Ready" ซ้ำ (ปุ่มยังกดได้) เคยสร้าง session ใหม่แล้วปิดตัวเดิม
     * ขณะ re-embed อาจกำลังใช้อยู่ (native use-after-close)
     */
    private suspend fun loadModel() = loadLock.withLock {
        withContext(Dispatchers.IO) {
            if (ortSession != null && tokenizer != null) {
                logDebug("AndroidONNX", "EmbeddingGemma 2 already loaded")
                return@withContext
            }
            try {
                logDebug("AndroidONNX", "Loading EmbeddingGemma 2 tokenizer...")
                val tok = loadTokenizer() ?: return@withContext

                logDebug("AndroidONNX", "Loading ONNX model...")
                val env = OrtEnvironment.getEnvironment()
                val session = env.createSession(modelFile.absolutePath)
                featureInputs = session.inputInfo
                    .filterKeys { it.endsWith("_features") }
                    .mapValues { (_, info) -> ((info.info as? TensorInfo)?.shape?.lastOrNull() ?: 512L).coerceAtLeast(1L) }
                check("sentence_embedding" in session.outputNames) {
                    "model has no sentence_embedding output: ${session.outputNames}"
                }
                logDebug(
                    "AndroidONNX",
                    "EmbeddingGemma 2 loaded — inputs=${session.inputNames} outputs=${session.outputNames}"
                )

                ortEnv = env
                ortSession = session
                tokenizer = tok
                provider.setInferenceDelegate { text ->
                    withContext(Dispatchers.Default) { runInference(text) }
                }
            } catch (e: Throwable) {
                logError("AndroidONNX", "Failed to load model or tokenizer", e)
            }
        }
    }

    /** cache ก่อน, ไม่มีก็สร้างจาก tokenizer.json (ครั้งแรกหลังดาวน์โหลด) แล้วลบ json ทิ้ง */
    private fun loadTokenizer(): GemmaBpeTokenizer? {
        if (tokenizerCache.exists()) {
            try {
                return GemmaBpeTokenizer.readFrom(tokenizerCache)
            } catch (e: Exception) {
                logError("AndroidONNX", "Tokenizer cache unreadable — rebuilding", e)
                tokenizerCache.delete()
            }
        }
        if (!tokenizerJson.exists()) {
            logError("AndroidONNX", "tokenizer.json missing — download the model again")
            return null
        }
        val started = System.currentTimeMillis()
        val tok = readTokenizerJson(tokenizerJson)
        val tmp = File(tokenizerCache.path + ".part")
        tok.writeTo(tmp)
        if (tmp.renameTo(tokenizerCache)) tokenizerJson.delete()
        logDebug("AndroidONNX", "Tokenizer cache built in ${System.currentTimeMillis() - started} ms")
        return tok
    }

    /** อ่านเฉพาะ model.vocab กับ model.merges แบบ stream — ไม่โหลดทั้ง 32 MB เป็น object */
    private fun readTokenizerJson(file: File): GemmaBpeTokenizer {
        val builder = GemmaBpeTokenizer.Builder()
        JsonReader(file.bufferedReader(Charsets.UTF_8, 1 shl 16)).use { reader ->
            reader.beginObject()
            while (reader.hasNext()) {
                if (reader.nextName() != "model") { reader.skipValue(); continue }
                reader.beginObject()
                while (reader.hasNext()) {
                    when (reader.nextName()) {
                        "vocab" -> {
                            reader.beginObject()
                            while (reader.hasNext()) builder.vocab(reader.nextName(), reader.nextInt())
                            reader.endObject()
                        }
                        "merges" -> {
                            reader.beginArray()
                            while (reader.hasNext()) {
                                if (reader.peek() == JsonToken.BEGIN_ARRAY) {
                                    reader.beginArray()
                                    val left = reader.nextString()
                                    val right = reader.nextString()
                                    while (reader.hasNext()) reader.skipValue()
                                    reader.endArray()
                                    builder.merge(left, right)
                                } else {
                                    // รูปแบบเก่า "left right"
                                    val pair = reader.nextString()
                                    val cut = pair.indexOf(' ')
                                    if (cut > 0) builder.merge(pair.substring(0, cut), pair.substring(cut + 1))
                                }
                            }
                            reader.endArray()
                        }
                        else -> reader.skipValue()
                    }
                }
                reader.endObject()
            }
            reader.endObject()
        }
        return builder.build()
    }

    private fun runInference(text: String): List<Float> {
        val session = ortSession ?: return emptyList()
        val env = ortEnv ?: return emptyList()
        val tok = tokenizer ?: return emptyList()

        val tensors = mutableListOf<OnnxTensor>()
        val started = System.currentTimeMillis()
        return try {
            // 1. Tokenize (Pure Kotlin) — รวม <bos>/<eos>, ตัดที่ MAX_TOKENS
            val inputIds = tok.encode(text, MAX_TOKENS)
            val shape = longArrayOf(1, inputIds.size.toLong())

            // 2. Prepare Inputs — ส่วนภาพ/วิดีโอ/เสียงไม่ใช้: tensor ว่าง [0, width]
            val inputs = mutableMapOf<String, OnnxTensor>()
            inputs["input_ids"] = OnnxTensor.createTensor(env, LongBuffer.wrap(inputIds), shape).also(tensors::add)
            inputs["attention_mask"] = OnnxTensor.createTensor(env, LongBuffer.wrap(LongArray(inputIds.size) { 1L }), shape)
                .also(tensors::add)
            for ((name, width) in featureInputs) {
                inputs[name] = OnnxTensor.createTensor(env, FloatBuffer.allocate(0), longArrayOf(0, width)).also(tensors::add)
            }

            // 3. Run Inference — sentence_embedding [1, 768] (mean pool + projection + L2 ในตัวโมเดลแล้ว)
            session.run(inputs, setOf("sentence_embedding")).use { result ->
                @Suppress("UNCHECKED_CAST")
                val batch = result.get(0).value as Array<FloatArray>
                batch.firstOrNull()?.toList().orEmpty()
            }.also {
                logDebug("AndroidONNX", "embed ${inputIds.size} tokens in ${System.currentTimeMillis() - started} ms")
            }
        } catch (e: Exception) {
            logError("AndroidONNX", "Inference error: ${e.message}", e)
            emptyList()
        } finally {
            tensors.forEach { it.close() }
        }
    }

    private fun removeLegacyFiles() {
        legacyFiles.filter { it.exists() }.forEach {
            if (it.delete()) logDebug("AndroidONNX", "Removed old model file ${it.name}")
        }
    }

    private companion object {
        /** โมเดลรับได้ 8K แต่ความจำแต่ละชิ้นสั้น — จำกัดเพื่อคุม RAM/เวลาบนมือถือ */
        const val MAX_TOKENS = 2048
    }
}
