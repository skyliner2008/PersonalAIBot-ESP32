package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.ai.JarvisPersona
import com.skyliner2008.jarvis.pet.AlwaysLiveProfile
import com.skyliner2008.jarvis.tools.ToolRegistry
import kotlin.test.Test
import kotlin.test.assertTrue

/**
 * Live ใช้ความจำระยะยาว (EmbeddingGemma 2) ผ่าน recall_memory — เดิมคำอธิบาย tool เป็นอังกฤษบรรทัดเดียว และ prompt ของ Live
 * ไม่พูดถึงเลย: log 2026-10-13 ทั้งวันไม่มีการเรียก recall_memory สักครั้ง
 */
class LiveMemoryRulesTest {

    @Test
    fun memoryToolsAreInEveryLiveProfile() {
        for (profile in AlwaysLiveProfile.entries) {
            val names = ToolRegistry.getLiveGeminiTool(profile).functionDeclarations.map { it.name }
            assertTrue("recall_memory" in names && "remember_fact" in names, "memory tools missing in $profile")
        }
    }

    @Test
    fun recallDescriptionSaysWhenToUseIt() {
        val recall = ToolRegistry.getLiveGeminiTool(AlwaysLiveProfile.CONTROL).functionDeclarations
            .first { it.name == "recall_memory" }
        assertTrue("เรื่องในอดีต" in recall.description, recall.description)
        assertTrue("ห้ามใช้กับข้อมูลที่เปลี่ยนตลอด" in recall.description, recall.description)
    }

    @Test
    fun assistantAndPetPromptsTellLiveToRecallBeforeAnswering() {
        val wasPet = JarvisPersona.isPetMode
        try {
            JarvisPersona.isPetMode = false
            val assistant = JarvisPersona.LIVE_SYSTEM_PROMPT
            assertTrue("LONG-TERM MEMORY" in assistant && "recall_memory" in assistant && "remember_fact" in assistant)

            JarvisPersona.isPetMode = true
            val pet = JarvisPersona.LIVE_SYSTEM_PROMPT
            assertTrue("recall_memory" in pet && "remember_fact" in pet)
        } finally {
            JarvisPersona.isPetMode = wasPet
        }
    }
}
