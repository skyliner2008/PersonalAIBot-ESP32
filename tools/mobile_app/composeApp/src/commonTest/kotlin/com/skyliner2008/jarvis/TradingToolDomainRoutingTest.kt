package com.skyliner2008.jarvis

import com.skyliner2008.jarvis.tools.trading.TradingToolRouter
import kotlin.test.Test
import kotlin.test.assertEquals

class TradingToolDomainRoutingTest {
    @Test
    fun marketAndTechnicalTools_routeCorrectly() {
        val marketTools = listOf("trading_price", "trading_market_snapshot", "trading_top_gainers", "trading_top_losers")
        marketTools.forEach { tool ->
            assertEquals(TradingToolRouter.Domain.MARKET, TradingToolRouter.domainOf(tool), "Failed on tool: $tool")
        }

        val technicalTools = listOf(
            "trading_technical_analysis", "trading_multi_timeframe", "trading_bollinger_scan",
            "trading_oversold_scan", "trading_overbought_scan", "trading_volume_breakout",
            "trading_harmonic_scan", "trading_elliot_modern_analysis"
        )
        technicalTools.forEach { tool ->
            assertEquals(TradingToolRouter.Domain.TECHNICAL, TradingToolRouter.domainOf(tool), "Failed on tool: $tool")
        }

        val sentimentTools = listOf("trading_sentiment", "trading_news", "trading_macro_calendar", "trading_fear_greed")
        sentimentTools.forEach { tool ->
            assertEquals(TradingToolRouter.Domain.SENTIMENT, TradingToolRouter.domainOf(tool), "Failed on tool: $tool")
        }
    }

    @Test
    fun researchTools_routeToResearchDomain() {
        val researchTools = listOf(
            "trading_deep_analysis_suite", "trading_backtest", "trading_backtest_optimize",
            "trading_backtest_evolve", "trading_mix_config", "trading_strategy_signal",
            "trading_signal_stats", "trading_fundamental_analysis", "trading_crypto_overview",
            "trading_position_sizing", "trading_correlation_matrix", "trading_economic_data",
            "trading_combined"
        )
        researchTools.forEach { tool ->
            assertEquals(TradingToolRouter.Domain.RESEARCH, TradingToolRouter.domainOf(tool), "Failed on tool: $tool")
        }
    }

    @Test
    fun smcTools_routeToSmcDomain() {
        val smcTools = listOf(
            "trading_smc_flow", "trading_smc_analysis", "trading_smc_sweeps",
            "trading_smc_liquidity", "trading_smc_orderblocks", "trading_smc_structure"
        )
        smcTools.forEach { tool ->
            assertEquals(TradingToolRouter.Domain.SMC, TradingToolRouter.domainOf(tool), "Failed on tool: $tool")
        }
    }

    @Test
    fun blankOrUnknownToolName_isUnknown() {
        assertEquals(TradingToolRouter.Domain.UNKNOWN, TradingToolRouter.domainOf("   "))
        assertEquals(TradingToolRouter.Domain.UNKNOWN, TradingToolRouter.domainOf("non_existent_tool"))
        // the MT5 bridge tools were removed with the bridge
        assertEquals(TradingToolRouter.Domain.UNKNOWN, TradingToolRouter.domainOf("trading_mt5_order"))
    }
}
