package com.skyliner2008.jarvis.data.embedding

import java.io.DataOutputStream
import java.io.File
import java.nio.ByteBuffer

/**
 * GemmaBpeTokenizer — tokenizer ของ EmbeddingGemma 2 แบบ Kotlin ล้วน ให้ id ตรงกับ HF `tokenizers`
 * สำหรับ tokenizer.json ของโมเดลนี้:
 *   normalizer แทน ' ' ด้วย '▁' (ไม่เติม ▁ นำหน้า) · ไม่มีการแบ่งคำก่อน (ทั้งข้อความคือคำเดียว) ·
 *   BPE ตามลำดับ merges · ตัวอักษรที่ไม่มีใน vocab แตกเป็น byte token `<0xXX>` · ห่อด้วย <bos> … <eos>
 *
 * tokenizer.json ใหญ่ 32 MB (vocab 262k, merges 515k) — ถ้าเก็บเป็น JSON object จะกิน RAM หลายร้อย MB
 * จึงเก็บแค่ที่การ encode ต้องใช้เป็น primitive array: id ของตัวอักษรเดี่ยว, byte token 256 ตัว และตาราง merge
 * ([Builder] สร้างจาก tokenizer.json ครั้งแรก, [writeTo]/[readFrom] เป็น cache ไฟล์เล็ก ~6 MB)
 */
class GemmaBpeTokenizer private constructor(
    private val charIds: Map<Int, Int>,
    private val byteIds: IntArray,
    private val merges: MergeTable,
    val bosId: Int,
    val eosId: Int,
    val unkId: Int
) {

    /** token ids รวม <bos>/<eos> — ตัดเนื้อหาให้ทั้งหมดไม่เกิน [maxTokens] */
    fun encode(text: String, maxTokens: Int = Int.MAX_VALUE): LongArray {
        val symbols = mergeAll(initialSymbols(text.replace(' ', '▁')))
        val keep = minOf(symbols.size, maxOf(0, maxTokens - 2))
        val out = LongArray(keep + 2)
        out[0] = bosId.toLong()
        for (i in 0 until keep) out[i + 1] = symbols[i].toLong()
        out[keep + 1] = eosId.toLong()
        return out
    }

    private fun initialSymbols(normalized: String): IntArray {
        val ids = IntArray(normalized.length * 4)
        var n = 0
        var i = 0
        while (i < normalized.length) {
            val cp = normalized.codePointAt(i)
            val id = charIds[cp]
            if (id != null) {
                ids[n++] = id
            } else {
                val bytes = String(Character.toChars(cp)).toByteArray(Charsets.UTF_8)
                if (bytes.all { byteIds[it.toInt() and 0xFF] >= 0 }) {
                    for (b in bytes) ids[n++] = byteIds[b.toInt() and 0xFF]
                } else {
                    ids[n++] = unkId
                }
            }
            i += Character.charCount(cp)
        }
        return ids.copyOf(n)
    }

    /** BPE แบบเดียวกับ HF `Word::merge_all`: รวมคู่ที่ rank ต่ำสุดก่อน (เสมอกันเอาตำแหน่งซ้ายสุด) */
    private fun mergeAll(start: IntArray): IntArray {
        val n = start.size
        if (n < 2) return start
        val sym = start.copyOf()
        val prev = IntArray(n) { it - 1 }
        val next = IntArray(n) { if (it + 1 < n) it + 1 else -1 }
        val alive = BooleanArray(n) { true }
        val heap = LongHeap(n)

        fun push(pos: Int) {
            val right = next[pos]
            if (right < 0) return
            val slot = merges.find(sym[pos], sym[right])
            if (slot >= 0) heap.push((merges.rankAt(slot).toLong() shl 32) or pos.toLong())
        }

        for (pos in 0 until n - 1) push(pos)
        while (heap.isNotEmpty()) {
            val top = heap.pop()
            val rank = (top ushr 32).toInt()
            val pos = top.toInt()
            if (!alive[pos]) continue
            val right = next[pos]
            if (right < 0) continue
            // คู่นี้อาจเปลี่ยนไปแล้วจากการ merge ก่อนหน้า — ใช้ได้เฉพาะเมื่อยังเป็น merge rank เดิม
            val slot = merges.find(sym[pos], sym[right])
            if (slot < 0 || merges.rankAt(slot) != rank) continue

            sym[pos] = merges.mergedAt(slot)
            alive[right] = false
            val after = next[right]
            next[pos] = after
            if (after >= 0) prev[after] = pos
            if (prev[pos] >= 0) push(prev[pos])
            push(pos)
        }

        val out = IntArray(n)
        var count = 0
        var p = 0
        while (p >= 0) {
            out[count++] = sym[p]
            p = next[p]
        }
        return out.copyOf(count)
    }

    /** cache: magic, version, bos/eos/unk, (code point, id)*, byte ids, (left, right, merged)* ตามลำดับ rank */
    fun writeTo(file: File) {
        DataOutputStream(file.outputStream().buffered(1 shl 16)).use { out ->
            out.writeInt(CACHE_MAGIC)
            out.writeInt(CACHE_VERSION)
            out.writeInt(bosId); out.writeInt(eosId); out.writeInt(unkId)
            out.writeInt(charIds.size)
            for ((cp, id) in charIds) { out.writeInt(cp); out.writeInt(id) }
            for (id in byteIds) out.writeInt(id)
            out.writeInt(merges.size)
            for (rank in 0 until merges.size) {
                val (left, right, merged) = merges.byRank(rank)
                out.writeInt(left); out.writeInt(right); out.writeInt(merged)
            }
        }
    }

    /**
     * สร้างจาก tokenizer.json แบบ stream — ต้องส่ง vocab ทั้งหมดก่อน merges (ลำดับเดียวกับในไฟล์)
     */
    class Builder {
        private val vocab = HashMap<String, Int>(300_000)
        private var lefts = IntArray(1 shl 19)
        private var rights = IntArray(1 shl 19)
        private var mergeds = IntArray(1 shl 19)
        private var mergeCount = 0

        fun vocab(token: String, id: Int) {
            vocab[token] = id
        }

        /** merge ถัดไปตามลำดับ rank — คู่ที่ไม่มีใน vocab ถูกข้าม (แต่ยังนับ rank เดิมไว้) */
        fun merge(left: String, right: String) {
            check(vocab.isNotEmpty()) { "tokenizer.json: merges came before vocab" }
            val l = vocab[left] ?: -1
            val r = vocab[right] ?: -1
            val m = vocab[left + right] ?: -1
            if (mergeCount == lefts.size) {
                lefts = lefts.copyOf(mergeCount * 2)
                rights = rights.copyOf(mergeCount * 2)
                mergeds = mergeds.copyOf(mergeCount * 2)
            }
            lefts[mergeCount] = l; rights[mergeCount] = r; mergeds[mergeCount] = m
            mergeCount++
        }

        fun build(): GemmaBpeTokenizer {
            val chars = HashMap<Int, Int>()
            for ((token, id) in vocab) {
                if (token.isNotEmpty() && token.codePointCount(0, token.length) == 1) chars[token.codePointAt(0)] = id
            }
            val bytes = IntArray(256) { vocab[byteToken(it)] ?: -1 }
            val table = MergeTable(mergeCount)
            for (rank in 0 until mergeCount) {
                if (lefts[rank] >= 0 && rights[rank] >= 0 && mergeds[rank] >= 0) {
                    table.put(lefts[rank], rights[rank], rank, mergeds[rank])
                }
            }
            return GemmaBpeTokenizer(
                charIds = chars,
                byteIds = bytes,
                merges = table,
                bosId = vocab["<bos>"] ?: 2,
                eosId = vocab["<eos>"] ?: 1,
                unkId = vocab["<unk>"] ?: 3
            )
        }
    }

    companion object {
        private const val CACHE_MAGIC = 0x45473254 // "EG2T"
        private const val CACHE_VERSION = 1

        fun byteToken(b: Int): String = "<0x%02X>".format(b)

        /** อ่าน cache จาก [writeTo] — ไฟล์เสีย/รุ่นไม่ตรง → exception */
        fun readFrom(file: File): GemmaBpeTokenizer {
            val buf = ByteBuffer.wrap(file.readBytes()).asIntBuffer()
            check(buf.get() == CACHE_MAGIC && buf.get() == CACHE_VERSION) { "tokenizer cache: bad header" }
            val bos = buf.get(); val eos = buf.get(); val unk = buf.get()
            val charCount = buf.get()
            val chars = HashMap<Int, Int>(charCount * 2)
            repeat(charCount) { chars[buf.get()] = buf.get() }
            val bytes = IntArray(256) { buf.get() }
            val mergeCount = buf.get()
            val table = MergeTable(mergeCount)
            for (rank in 0 until mergeCount) {
                val l = buf.get(); val r = buf.get(); val m = buf.get()
                if (l >= 0 && r >= 0 && m >= 0) table.put(l, r, rank, m)
            }
            return GemmaBpeTokenizer(chars, bytes, table, bos, eos, unk)
        }
    }

    /** (left id, right id) → (rank, merged id) — open addressing บน primitive array */
    private class MergeTable(expected: Int) {
        private val capacity = Integer.highestOneBit(maxOf(16, expected * 2)) shl 1
        private val mask = capacity - 1
        private val keys = LongArray(capacity) { EMPTY }
        private val ranks = IntArray(capacity)
        private val mergedIds = IntArray(capacity)
        private val slotByRank = IntArray(expected) { -1 }
        val size = expected

        fun put(left: Int, right: Int, rank: Int, merged: Int) {
            val key = key(left, right)
            var slot = hash(key)
            while (keys[slot] != EMPTY && keys[slot] != key) slot = (slot + 1) and mask
            // คู่ซ้ำ: rank หลังทับ rank ก่อน เหมือน HashMap ของ HF (byRank ของ rank เก่าจึงคืน -1)
            keys[slot] = key; ranks[slot] = rank; mergedIds[slot] = merged
            slotByRank[rank] = slot
        }

        fun find(left: Int, right: Int): Int {
            val key = key(left, right)
            var slot = hash(key)
            while (true) {
                val k = keys[slot]
                if (k == key) return slot
                if (k == EMPTY) return -1
                slot = (slot + 1) and mask
            }
        }

        fun rankAt(slot: Int) = ranks[slot]
        fun mergedAt(slot: Int) = mergedIds[slot]

        fun byRank(rank: Int): Triple<Int, Int, Int> {
            val slot = slotByRank[rank]
            if (slot < 0 || ranks[slot] != rank) return Triple(-1, -1, -1)
            val key = keys[slot]
            return Triple((key ushr 32).toInt(), key.toInt(), mergedIds[slot])
        }

        private fun key(left: Int, right: Int) = (left.toLong() shl 32) or (right.toLong() and 0xFFFFFFFFL)

        private fun hash(key: Long): Int {
            var h = key * -0x61c8864680b583ebL
            h = h xor (h ushr 29)
            return h.toInt() and mask
        }

        private companion object {
            const val EMPTY = -1L
        }
    }

    /** min-heap ของ Long (rank << 32 | pos) */
    private class LongHeap(initial: Int) {
        private var items = LongArray(maxOf(8, initial))
        private var size = 0

        fun isNotEmpty() = size > 0

        fun push(v: Long) {
            if (size == items.size) items = items.copyOf(size * 2)
            var i = size++
            items[i] = v
            while (i > 0) {
                val parent = (i - 1) / 2
                if (items[parent] <= items[i]) break
                swap(i, parent)
                i = parent
            }
        }

        fun pop(): Long {
            val top = items[0]
            items[0] = items[--size]
            var i = 0
            while (true) {
                val l = i * 2 + 1
                val r = l + 1
                var smallest = i
                if (l < size && items[l] < items[smallest]) smallest = l
                if (r < size && items[r] < items[smallest]) smallest = r
                if (smallest == i) break
                swap(i, smallest)
                i = smallest
            }
            return top
        }

        private fun swap(a: Int, b: Int) {
            val t = items[a]; items[a] = items[b]; items[b] = t
        }
    }
}
