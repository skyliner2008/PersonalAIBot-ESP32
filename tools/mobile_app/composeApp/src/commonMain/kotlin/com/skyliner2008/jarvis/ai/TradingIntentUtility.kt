package com.skyliner2008.jarvis.ai

/**
 * Utility for detecting trading-related intent in user prompts.
 * Shared between GeminiService and JarvisOrchestrator to ensure consistent tool selection.
 */
object TradingIntentUtility {

    fun isTradingPrompt(prompt: String, intentAddon: String = ""): Boolean {
        val text = "$prompt $intentAddon".lowercase()
        val keywords = listOf(
            "trading", "trade", "xau", "xauusd", "gold", "forex", "smc",
            "liquidity", "sweep", "order block", "orderblock", "bos", "choch",
            "premium", "discount", "fvg", "price", "gc=f", "oanda", "tv:", "ทอง", "หุ้น", "คริปโต"
        )
        return keywords.any { text.contains(it) }
    }

    fun isSmcPrompt(prompt: String, intentAddon: String = ""): Boolean {
        val text = "$prompt $intentAddon".lowercase()
        val keywords = listOf(
            "smc", "sweep", "liquidity", "order block", "orderblock",
            "bos", "choch", "fvg", "premium", "discount", "ict",
            "trading_smc", "market structure",
            "สภาพคล่อง", "ออเดอร์บล็อก", "โครงสร้าง", "สวีป", "สมาร์ทมันนี่",
            "โซนแนวรับแนวต้านสถาบัน", "institutional"
        )
        return keywords.any { text.contains(it) }
    }

    /**
     * TradingView request profile. Unspecified trading requests use the fixed AI profile;
     * explicit indicator/level/value questions use the lightweight USER_QUERY profile.
     */
    enum class TradingAnalysisProfile { AI, USER_QUERY, COMPOSITE }

    fun tradingAnalysisProfile(prompt: String, intentAddon: String = ""): TradingAnalysisProfile {
        val text = "$prompt $intentAddon".lowercase().trim()
        val explicit = listOf(
            "เท่าไร", "เท่าไหร่", "ค่า", "อยู่ที่", "อยู่ตรงไหน", "ตรงไหน", "เทียบ", "เหนือ", "ต่ำกว่า", "บน", "ล่าง",
            "แนวรับ", "แนวต้าน", "support", "resistance", "pivot", "rsi", "ema", "sma", "macd", "atr", "adx",
            "stochastic", "stoch", "cci", "mfi", "vwap", "bollinger", "bbands", "fib", "fibonacci", "donchian",
            "ราคา ปัจจุบัน", "ราคาปัจจุบัน", "current price", "current value", "what is", "how much", "where is"
        )
        val analysis = listOf(
            "วิเคราะห์", "วิเคราะห์ smc", "วิเคราะห์ทอง", "วิเคราะห์ภาพรวม", "ภาพรวม", "5 มิติ", "confluence",
            "deep analysis", "market analysis", "market overview", "smc analysis", "trading analysis"
        )
        val hasExplicit = explicit.any { text.contains(it) }
        val hasAnalysis = analysis.any { text.contains(it) }
        return when {
            hasExplicit && hasAnalysis -> TradingAnalysisProfile.COMPOSITE
            hasExplicit -> TradingAnalysisProfile.USER_QUERY
            else -> TradingAnalysisProfile.AI
        }
    }

    fun isUserIndicatorQuery(prompt: String, intentAddon: String = ""): Boolean =
        tradingAnalysisProfile(prompt, intentAddon) != TradingAnalysisProfile.AI

    fun isCompositeTradingQuery(prompt: String, intentAddon: String = ""): Boolean =
        tradingAnalysisProfile(prompt, intentAddon) == TradingAnalysisProfile.COMPOSITE

    /** Signal-alert requests use the TradingView alert path. */
    fun isSignalAlertPrompt(prompt: String, intentAddon: String = ""): Boolean {
        val text = "$prompt $intentAddon".lowercase()
        val alertWords = listOf("แจ้งเตือน", "ตั้งแจ้งเตือน", "alert", "signal alert", "สัญญาณ", "signal")
        return alertWords.any { text.contains(it) } && isTradingPrompt(prompt, intentAddon)
    }

    fun isDeepAnalysisPrompt(prompt: String, intentAddon: String = ""): Boolean {
        val text = "$prompt $intentAddon".lowercase()
        val keywords = listOf(
            "วิเคราะห์เชิงลึก", "deep analysis", "confluence", "debate", "consensus",
            "วิเคราะห์ confluence", "วิเคราะห์รอบด้าน", "multi-agent", "วิเคราะห์ละเอียด"
        )
        return keywords.any { text.contains(it) }
    }
}
