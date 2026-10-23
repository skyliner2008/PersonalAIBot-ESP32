package com.skyliner2008.jarvis.data.embedding

import com.skyliner2008.jarvis.logDebug
import com.skyliner2008.jarvis.logError
import io.ktor.client.*

/**
 * EmbeddingProviderRegistry — จัดการ singleton ของทุก EmbeddingProvider
 *
 * Resolve provider ตาม settings:
 *   1. User preference (ถ้าเลือกไว้)
 *   2. Local EmbeddingGemma 2 ผ่าน ONNX (ถ้าพร้อมใช้งาน — offline first)
 *   3. Gemini (cloud fallback)
 *
 * provider แต่ละตัวอยู่คนละ vector space — ความจำที่บันทึกด้วย provider หนึ่ง
 * ค้นด้วยอีก provider ไม่ได้จนกว่าจะ re-embed (JarvisMemoryManager.backfillEmbeddings)
 *
 * @since 2026-04-29
 */
class EmbeddingProviderRegistry(
    private val client: HttpClient,
    private val geminiApiKey: String
) {
    /** Lazy singleton providers */
    val gemini: GeminiEmbeddingProvider by lazy { GeminiEmbeddingProvider(client, geminiApiKey) }
    val localOnnx: LocalOnnxEmbeddingProvider by lazy { LocalOnnxEmbeddingProvider() }

    /**
     * อัปเดต API key ของ cloud provider — ต้องถูกเรียกทุกครั้งที่ผู้ใช้บันทึก key ใหม่
     * (provider เป็น lazy singleton key เดิมจะไม่ตามไปเอง)
     */
    fun updateGeminiKey(newKey: String) {
        gemini.updateApiKey(newKey)
    }

    /** ทุก provider ที่ลงทะเบียน */
    val allProviders: List<EmbeddingProvider>
        get() = listOf(localOnnx, gemini)

    /**
     * Resolve provider ที่เหมาะสม ตาม preference ของ user
     *
     * @param preferredId provider ID ที่ user เลือก (null = auto)
     * @return EmbeddingProvider ที่พร้อมใช้งาน
     */
    suspend fun resolve(preferredId: String? = null): EmbeddingProvider {
        // 1. User preference
        if (preferredId != null) {
            val preferred = allProviders.find { it.providerId == preferredId }
            if (preferred != null && preferred.isAvailable()) return preferred
            logDebug("EmbedRegistry", "Preferred '$preferredId' not available, falling back...")
        }

        // 2. Offline-first: try local ONNX
        if (localOnnx.isAvailable()) {
            logDebug("EmbedRegistry", "Using local ONNX (offline)")
            return localOnnx
        }

        // 3. Cloud fallback: Gemini
        if (gemini.isAvailable()) {
            logDebug("EmbedRegistry", "Using Gemini (cloud)")
            return gemini
        }

        // 4. No provider available — return a no-op
        logError("EmbedRegistry", "No embedding provider available!")
        return NoopEmbeddingProvider
    }

}

/** Universal target dimensionality used across all providers + memory store. */
const val EMBEDDING_TARGET_DIMS: Int = 768

/**
 * No-op provider — ใช้เมื่อไม่มี provider พร้อม
 */
private object NoopEmbeddingProvider : EmbeddingProvider {
    override val providerId = "noop"
    override val displayName = "None"
    override val isLocal = false
    override val nativeDimensions = EMBEDDING_TARGET_DIMS
    override val vectorSpace = "none"
    override val relevanceFloor = 1f

    override suspend fun embed(text: String, taskType: String): List<Float> = emptyList()
    override suspend fun isAvailable(): Boolean = false
}
