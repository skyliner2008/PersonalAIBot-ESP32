package com.skyliner2008.jarvis.ai

import com.skyliner2008.jarvis.tools.ToolRegistry

/**
 * TradingToolPolicy — ศูนย์กลางการตัดสินใจเรื่อง trading tool selection
 *
 * รวม logic ที่เคยซ้ำกัน 3 จุด (JarvisOrchestrator x2, GeminiService x2)
 * ให้เป็น module เดียว — ทุก provider path ใช้ policy เดียวกัน
 * จึงได้ semantics ตรงกันเสมอ:
 *
 * - prompt ธรรมดา       → ซ่อน trading tools ออกจาก tool spec (ประหยัด token)
 * - prompt trading       → อนุญาต TradingView tools ให้ model เลือกเอง
 */
data class TradingToolPolicy(
    val isTradingContext: Boolean,
    val smcMode: Boolean,
    val deepAnalysis: Boolean,
    val signalAlertMode: Boolean,
    val analysisProfile: TradingIntentUtility.TradingAnalysisProfile = TradingIntentUtility.TradingAnalysisProfile.AI,
    val allowedTradingToolNames: Set<String>?,
    val policyLabel: String?
) {
    /**
     * tool นี้ควรถูกส่งใน tool spec หรือไม่
     * non-trading tool ผ่านเสมอ; trading tool ผ่านเมื่ออยู่ใน allowed set
     */
    fun isToolAllowed(name: String): Boolean {
        if (!ToolRegistry.isTradingTool(name)) return true
        val allowed = allowedTradingToolNames ?: return false
        return name in allowed
    }

    /** System prompt addon สำหรับ signal alert mode (external provider path) */
    fun signalAlertSystemPromptAddon(): String = if (signalAlertMode) buildString {
        appendLine("\n[IMPORTANT: TRADINGVIEW SIGNAL ALERT MODE ACTIVE]")
        appendLine("- User is asking to create/modify a trading signal alert.")
        appendLine("- Use automation_manage_alerts with tool_name='trading_signal_alert'.")
        appendLine("- Use TradingView-derived market data for the signal alert path.")
        appendLine("- If the user says all timeframes, create alerts for the configured canonical timeframes.")
    } else ""

    companion object {
        /** วิเคราะห์ user text (+ intent addon) แล้วคืน policy ที่เหมาะสม */
        fun evaluate(text: String, intentAddon: String = ""): TradingToolPolicy {
            val trading = TradingIntentUtility.isTradingPrompt(text, intentAddon)
            val smc     = TradingIntentUtility.isSmcPrompt(text, intentAddon)
            val deep    = TradingIntentUtility.isDeepAnalysisPrompt(text, intentAddon)
            val signalAlert = TradingIntentUtility.isSignalAlertPrompt(text, intentAddon)
            val profile = TradingIntentUtility.tradingAnalysisProfile(text, intentAddon)

            val isTradingContext = trading || smc || deep
            val allowed = if (isTradingContext) {
                ToolRegistry.tvOnlyTradingFunctionNames
            } else null

            val label = when {
                signalAlert -> "TradingView Signal Alert mode"
                deep -> "Deep Confluence Suite mode"
                profile == TradingIntentUtility.TradingAnalysisProfile.COMPOSITE -> "TradingView Composite Analysis + User Query mode"
                profile == TradingIntentUtility.TradingAnalysisProfile.USER_QUERY -> "TradingView User Query mode"
                isTradingContext -> "TradingView AI Analysis mode"
                else -> null
            }

            return TradingToolPolicy(
                isTradingContext = isTradingContext,
                smcMode = smc,
                deepAnalysis = deep,
                signalAlertMode = signalAlert,
                analysisProfile = profile,
                allowedTradingToolNames = allowed,
                policyLabel = label
            )
        }
    }
}
