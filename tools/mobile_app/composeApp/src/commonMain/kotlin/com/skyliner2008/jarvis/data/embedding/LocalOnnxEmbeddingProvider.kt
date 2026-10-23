package com.skyliner2008.jarvis.data.embedding

import com.skyliner2008.jarvis.logError
import kotlinx.coroutines.CancellationException

/**
 * LocalOnnxEmbeddingProvider — On-device embedding ด้วย ONNX Runtime
 *
 * Model: EmbeddingGemma 2 (ส่วนข้อความ, q4 ~175MB)
 * - 768 dims native (ตรงกับ EMBEDDING_TARGET_DIMS ไม่ต้อง pad)
 * - รองรับหลายภาษารวมภาษาไทย
 * - ทำงาน offline 100%
 * - ฝึกมาแบบ asymmetric: query กับ document ต้องเติมคำนำหน้าคนละแบบ ([promptFor])
 *
 * NOTE: ไฟล์นี้เป็น commonMain stub — actual ONNX inference อยู่ใน
 * androidMain (AndroidLocalOnnxManager) ผ่าน onnxruntime-android library
 *
 * @since 2026-04-29 (EmbeddingGemma 2 แทน MiniLM-L12-v2 — 2026-10-13)
 */
class LocalOnnxEmbeddingProvider : EmbeddingProvider {

    override val providerId = "local_onnx"
    override val displayName = "Local (EmbeddingGemma 2)"
    override val isLocal = true
    override val nativeDimensions = 768
    override val vectorSpace = VECTOR_SPACE
    /** ค่า cosine ของ EmbeddingGemma 2 ทั้งชุดสูงกว่า Gemini: เรื่องที่เกี่ยว ~0.72–0.84, ไม่เกี่ยว ~0.52–0.71 */
    override val relevanceFloor = 0.70f

    /** สถานะว่า model ถูกโหลดแล้วหรือยัง (set จาก IO thread, อ่านจากทุก thread) */
    @Volatile
    private var modelLoaded = false

    /**
     * Delegate สำหรับ platform-specific ONNX inference — รับข้อความที่เติมคำนำหน้าแล้ว
     * ถูก set จาก androidMain ผ่าน [setInferenceDelegate]
     */
    @Volatile
    private var inferenceDelegate: (suspend (String) -> List<Float>)? = null

    fun setInferenceDelegate(delegate: suspend (String) -> List<Float>) {
        inferenceDelegate = delegate
        modelLoaded = true
    }

    override suspend fun embed(text: String, taskType: String): List<Float> {
        val delegate = inferenceDelegate
        if (delegate == null) {
            logError("LocalOnnx", "Model not loaded — inference delegate not set")
            return emptyList()
        }
        if (text.isBlank()) return emptyList()

        return try {
            val raw = delegate(promptFor(taskType) + text)
            if (raw.isEmpty()) {
                logError("LocalOnnx", "Empty embedding returned")
                return emptyList()
            }
            raw.fitToTargetDimension(nativeDimensions)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            logError("LocalOnnx", "Inference failed: ${e.message}", e)
            emptyList()
        }
    }

    override suspend fun isAvailable(): Boolean = modelLoaded && inferenceDelegate != null

    companion object {
        const val VECTOR_SPACE = "embeddinggemma-2"

        /** คำนำหน้าตาม model card (config_sentence_transformers.json) */
        fun promptFor(taskType: String): String = when (taskType) {
            "RETRIEVAL_QUERY" -> "task: search result | query: "
            "SEMANTIC_SIMILARITY" -> "task: sentence similarity | query: "
            "CLASSIFICATION" -> "task: classification | query: "
            "CLUSTERING" -> "task: clustering | query: "
            "QUESTION_ANSWERING" -> "task: question answering | query: "
            "FACT_VERIFICATION" -> "task: fact checking | query: "
            "CODE_RETRIEVAL_QUERY" -> "task: code retrieval | query: "
            else -> "title: none | text: " // RETRIEVAL_DOCUMENT
        }
    }
}
