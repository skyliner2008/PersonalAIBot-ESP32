package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.memory.JarvisMemoryManager
import com.skyliner2008.jarvis.memory.chunkByBudget
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SleepCycleBatchTest {

    @Test
    fun keepsOrderAndStaysWithinBudget() {
        val items = listOf(40, 30, 50, 20, 10, 60)
        val batches = chunkByBudget(items, 100) { it }
        assertEquals(listOf(listOf(40, 30), listOf(50, 20, 10), listOf(60)), batches)
        assertEquals(items, batches.flatten())
        assertTrue(batches.all { b -> b.sum() <= 100 })
    }

    @Test
    fun oversizedItemGetsItsOwnBatch() {
        assertEquals(listOf(listOf(10), listOf(500), listOf(10)), chunkByBudget(listOf(10, 500, 10), 100) { it })
    }

    @Test
    fun emptyInputHasNoBatches() {
        assertEquals(emptyList(), chunkByBudget(emptyList<Int>(), 100) { it })
    }

    @Test
    fun oneCycleBringsTheChatBelowTheTrigger() {
        // sleep cycle เหลือข้อความไว้ SLEEP_KEEP_RECENT — ต้องต่ำกว่าเกณฑ์เรียก ไม่งั้นจะถูกเรียกซ้ำหลังทุกคำตอบ
        // (log 2026-10-13: ค้าง 365 ข้อความ รวบทีละ 100 → เรียก 00:52, 00:54, 01:01)
        assertTrue(JarvisMemoryManager.SLEEP_KEEP_RECENT < JarvisMemoryManager.SLEEP_TRIGGER_MESSAGES)
        // บทสนทนาจริง 265 ข้อความ ~55k ตัวอักษร → ส่ง LLM ครั้งเดียว
        val messages = List(265) { 208 }
        assertEquals(1, chunkByBudget(messages, JarvisMemoryManager.SLEEP_BATCH_CHARS) { it }.size)
    }
}
