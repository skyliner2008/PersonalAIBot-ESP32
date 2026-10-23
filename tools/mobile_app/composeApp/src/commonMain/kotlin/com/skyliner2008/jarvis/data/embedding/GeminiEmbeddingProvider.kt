package com.skyliner2008.jarvis.data.embedding

import com.skyliner2008.jarvis.logDebug
import com.skyliner2008.jarvis.logError
import io.ktor.client.*
import io.ktor.client.call.*
import io.ktor.client.request.*
import io.ktor.http.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/**
 * GeminiEmbeddingProvider — Cloud embedding ผ่าน Gemini REST API
 *
 * Model: gemini-embedding-001 → 3072 native dims, Matryoshka-truncated to 768
 * (text-embedding-004 ถูกปิดเมื่อ 14 ม.ค. 2026 — และ vector ของมันอยู่คนละ space อยู่แล้ว)
 *
 * Returns vector that is ALWAYS L2-normalized to 768 dims via fitToTargetDimension().
 * ขนาดเท่ากับ provider อื่นแต่เทียบ cosine ข้ามโมเดลไม่ได้ — vector ถูกบันทึกพร้อม [vectorSpace]
 *
 * รองรับ 100+ ภาษารวมภาษาไทย — ต้องมี internet + API key
 *
 * @since 2026-04-29 (last-fix 2026-10-13 — single model, vectorSpace tag)
 */
class GeminiEmbeddingProvider(
    private val client: HttpClient,
    apiKey: String
) : EmbeddingProvider {

    /**
     * API key เป็น mutable — Orchestrator.updateConfig() จะเรียก updateApiKey()
     * ทุกครั้งที่ผู้ใช้บันทึก key ใหม่ (เดิม key ถูก capture ตอน init = "" ทำให้
     * cloud embedding ไม่เคยทำงานหลังผู้ใช้ใส่ key ทีหลัง)
     */
    @Volatile
    private var currentApiKey: String = apiKey

    fun updateApiKey(newKey: String) {
        currentApiKey = newKey
    }

    override val providerId = "gemini"
    override val displayName = "Gemini (Cloud)"
    override val isLocal = false
    /** After fitToTargetDimension we always return 768 dims. */
    override val nativeDimensions = 768
    override val vectorSpace = MODEL
    override val relevanceFloor = 0.65f

    @Serializable
    private data class EmbeddingContent(
        val parts: List<EmbeddingPart>
    )

    @Serializable
    private data class EmbeddingPart(val text: String)

    @Serializable
    private data class EmbedRequest(
        val content: EmbeddingContent,
        val taskType: String? = null
    )

    @Serializable
    private data class EmbedResponse(
        val embedding: EmbedValues = EmbedValues()
    )

    @Serializable
    private data class EmbedValues(val values: List<Float> = emptyList())

    private val json = Json { ignoreUnknownKeys = true; isLenient = true }

    override suspend fun embed(text: String, taskType: String): List<Float> {
        if (currentApiKey.isBlank() || text.isBlank()) return emptyList()
        val raw = tryEmbed(MODEL, text, taskType)
        if (raw.isEmpty()) return emptyList()
        logDebug("GeminiEmbedding", "OK model=$MODEL nativeDims=${raw.size}")
        return raw.fitToTargetDimension(nativeDimensions)
    }

    private suspend fun tryEmbed(model: String, text: String, taskType: String): List<Float> {
        return try {
            val url = "https://generativelanguage.googleapis.com/v1beta/models/$model:embedContent?key=$currentApiKey"
            val res = client.post(url) {
                contentType(ContentType.Application.Json)
                setBody(json.encodeToString(EmbedRequest.serializer(), EmbedRequest(
                    content = EmbeddingContent(parts = listOf(EmbeddingPart(text = text))),
                    taskType = taskType
                )))
            }
            if (res.status.isSuccess()) {
                val resp: EmbedResponse = json.decodeFromString(res.body<String>())
                resp.embedding.values
            } else {
                val body = try { res.body<String>().take(240) } catch (_: Exception) { "" }
                logError("GeminiEmbedding", "model=$model HTTP ${res.status.value}: $body")
                emptyList()
            }
        } catch (e: Exception) {
            logError("GeminiEmbedding", "model=$model failed: ${e.message}", e)
            emptyList()
        }
    }

    override suspend fun isAvailable(): Boolean {
        return currentApiKey.isNotBlank()
    }

    private companion object {
        const val MODEL = "gemini-embedding-001"
    }
}
