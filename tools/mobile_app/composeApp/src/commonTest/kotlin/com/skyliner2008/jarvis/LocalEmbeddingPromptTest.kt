package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.data.embedding.LocalOnnxEmbeddingProvider
import kotlinx.coroutines.runBlocking
import kotlin.math.abs
import kotlin.math.sqrt
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class LocalEmbeddingPromptTest {

    @Test
    fun queriesAndDocumentsGetTheirOwnPrefix() {
        assertEquals("task: search result | query: ", LocalOnnxEmbeddingProvider.promptFor("RETRIEVAL_QUERY"))
        assertEquals("title: none | text: ", LocalOnnxEmbeddingProvider.promptFor("RETRIEVAL_DOCUMENT"))
        assertEquals("task: sentence similarity | query: ", LocalOnnxEmbeddingProvider.promptFor("SEMANTIC_SIMILARITY"))
        // taskType ที่ไม่รู้จัก → ถือเป็นเอกสาร
        assertEquals("title: none | text: ", LocalOnnxEmbeddingProvider.promptFor("SOMETHING_ELSE"))
    }

    @Test
    fun modelReceivesPrefixedTextAndReturnsUnitVector() = runBlocking {
        val provider = LocalOnnxEmbeddingProvider()
        val seen = mutableListOf<String>()
        provider.setInferenceDelegate { text ->
            seen += text
            List(768) { if (it < 2) 3f else 0f }
        }

        val vector = provider.embed("ฉันชอบดื่มอะไร", "RETRIEVAL_QUERY")
        provider.embed("ผู้ใช้ชอบกาแฟดำ", "RETRIEVAL_DOCUMENT")

        assertEquals(
            listOf("task: search result | query: ฉันชอบดื่มอะไร", "title: none | text: ผู้ใช้ชอบกาแฟดำ"),
            seen
        )
        assertEquals(768, vector.size)
        assertTrue(abs(sqrt(vector.sumOf { (it * it).toDouble() }) - 1.0) < 1e-5)
    }

    @Test
    fun blankTextSkipsTheModel() = runBlocking {
        val provider = LocalOnnxEmbeddingProvider()
        var calls = 0
        provider.setInferenceDelegate { calls++; List(768) { 1f } }
        assertTrue(provider.embed("   ", "RETRIEVAL_QUERY").isEmpty())
        assertEquals(0, calls)
    }

    @Test
    fun vectorSpaceNamesTheModel() {
        assertEquals("embeddinggemma-2", LocalOnnxEmbeddingProvider().vectorSpace)
    }
}
