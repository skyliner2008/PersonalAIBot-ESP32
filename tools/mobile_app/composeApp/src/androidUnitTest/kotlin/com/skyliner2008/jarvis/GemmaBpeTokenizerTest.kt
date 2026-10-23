package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.data.embedding.GemmaBpeTokenizer
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import org.junit.Assume
import java.io.File
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals

class GemmaBpeTokenizerTest {

    private val byteBase = 4

    /** <pad> <eos> <bos> <unk>, byte tokens 4..259, ตัวอักษร a b c ▁, แล้วคำที่ merge ได้ */
    private fun tiny(): GemmaBpeTokenizer {
        val b = GemmaBpeTokenizer.Builder()
        listOf("<pad>", "<eos>", "<bos>", "<unk>").forEachIndexed { i, t -> b.vocab(t, i) }
        for (x in 0 until 256) b.vocab(GemmaBpeTokenizer.byteToken(x), byteBase + x)
        listOf("a", "b", "c", "▁", "ab", "bc", "abc", "▁a").forEachIndexed { i, t -> b.vocab(t, 260 + i) }
        b.merge("a", "b")   // rank 0 → ab (264)
        b.merge("ab", "c")  // rank 1 → abc (266)
        b.merge("b", "c")   // rank 2 → bc (265)
        b.merge("▁", "a")   // rank 3 → ▁a (267)
        return b.build()
    }

    private fun ids(tok: GemmaBpeTokenizer, text: String, max: Int = Int.MAX_VALUE) =
        tok.encode(text, max).map { it.toInt() }

    @Test
    fun mergesLowestRankFirst() {
        // ab (rank 0) ก่อน bc (rank 2) แล้ว ab+c (rank 1) → abc
        assertEquals(listOf(2, 266, 1), ids(tiny(), "abc"))
    }

    @Test
    fun equalRanksMergeLeftToRight() {
        assertEquals(listOf(2, 264, 264, 1), ids(tiny(), "abab"))
    }

    @Test
    fun spacesBecomeMetaspaceWithoutLeadingPrefix() {
        // "a bc" → a ▁ b c: ไม่มี merge ของ (a,▁) และ (▁,b) · bc rank 2
        assertEquals(listOf(2, 260, 263, 265, 1), ids(tiny(), "a bc"))
        // ช่องว่างนำหน้ามีจริงเท่านั้นถึงได้ ▁
        assertEquals(listOf(2, 267, 1), ids(tiny(), " a"))
        assertEquals(listOf(2, 260, 1), ids(tiny(), "a"))
    }

    @Test
    fun unknownCharactersFallBackToUtf8Bytes() {
        // é = C3 A9
        assertEquals(listOf(2, byteBase + 0xC3, byteBase + 0xA9, 1), ids(tiny(), "é"))
        // อักษรนอก BMP (4 bytes)
        assertEquals(6, ids(tiny(), "𝔘").size)
    }

    @Test
    fun emptyTextIsBosEos() {
        assertEquals(listOf(2, 1), ids(tiny(), ""))
    }

    @Test
    fun truncationKeepsBosAndEos() {
        assertEquals(listOf(2, 260, 1), ids(tiny(), "a bc", max = 3))
        assertEquals(listOf(2, 1), ids(tiny(), "abc", max = 1))
    }

    @Test
    fun cacheRoundTripEncodesTheSame() {
        val tok = tiny()
        val file = File.createTempFile("eg2tok", ".bin")
        try {
            tok.writeTo(file)
            val back = GemmaBpeTokenizer.readFrom(file)
            for (text in listOf("abc", "abab", "a bc", " a", "é𝔘", "")) {
                assertEquals(ids(tok, text), ids(back, text), text)
            }
        } finally {
            file.delete()
        }
    }

    @Test
    fun laterDuplicateMergeWins() {
        val b = GemmaBpeTokenizer.Builder()
        listOf("<pad>", "<eos>", "<bos>", "<unk>", "x", "y", "z", "xy", "yz").forEachIndexed { i, t -> b.vocab(t, i) }
        b.merge("x", "y")  // rank 0
        b.merge("y", "z")  // rank 1
        b.merge("x", "y")  // rank 2 — ทับ rank 0 (เหมือน HashMap ของ HF) → yz มาก่อน
        assertEquals(listOf(2, 4, 8, 1), b.build().encode("xyz").map { it.toInt() })
    }

    /**
     * เทียบกับ HF `tokenizers` บน tokenizer.json จริงของ EmbeddingGemma 2 — ไฟล์ใหญ่ 32 MB จึงไม่อยู่ใน repo
     * รันเอง: ตั้ง EG2_TOKENIZER_DIR ให้ชี้โฟลเดอร์ที่มี tokenizer.json และ reference.json
     * (reference.json = [{"text": …, "ids": [...]}] จาก tokenizers.Tokenizer.from_file(...).encode(text).ids)
     */
    @Test
    fun matchesHuggingFaceTokenizer() {
        val dir = System.getenv("EG2_TOKENIZER_DIR")
        Assume.assumeTrue("ตั้ง EG2_TOKENIZER_DIR เพื่อเทียบกับ tokenizer จริง", dir != null)
        val json = Json { ignoreUnknownKeys = true }
        val model = json.parseToJsonElement(File(dir, "tokenizer.json").readText()).jsonObject["model"]!!.jsonObject
        val builder = GemmaBpeTokenizer.Builder()
        for ((token, id) in model["vocab"]!!.jsonObject) builder.vocab(token, id.jsonPrimitive.int)
        for (m in model["merges"]!!.jsonArray) {
            val pair = m.jsonArray
            builder.merge(pair[0].jsonPrimitive.content, pair[1].jsonPrimitive.content)
        }
        val tok = builder.build()

        val cache = File.createTempFile("eg2tok", ".bin")
        val cached = try {
            tok.writeTo(cache)
            GemmaBpeTokenizer.readFrom(cache)
        } finally {
            cache.delete()
        }

        val refs = json.parseToJsonElement(File(dir, "reference.json").readText()) as JsonArray
        for (ref in refs) {
            val obj = ref.jsonObject
            val text = obj["text"]!!.jsonPrimitive.content
            val want = obj["ids"]!!.jsonArray.map { it.jsonPrimitive.long }.toLongArray()
            assertContentEquals(want, tok.encode(text), text)
            assertContentEquals(want, cached.encode(text), "cache: $text")
        }
    }
}
