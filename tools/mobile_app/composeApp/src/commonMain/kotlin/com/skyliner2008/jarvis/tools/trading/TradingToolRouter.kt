package com.skyliner2008.jarvis.tools.trading

/**
 * Stable routing boundary for trading tools.
 * The executor remains backward-compatible while domain handlers are extracted incrementally.
 */
internal object TradingToolRouter {
    enum class Domain {
        MARKET,
        TECHNICAL,
        SENTIMENT,
        RESEARCH,
        SMC,
        UNKNOWN
    }

    fun domainOf(toolName: String): Domain = when (toolName) {
        "trading_price", "trading_market_snapshot", "trading_top_gainers", "trading_top_losers" -> Domain.MARKET
        "trading_technical_analysis", "trading_multi_timeframe", "trading_bollinger_scan",
        "trading_oversold_scan", "trading_overbought_scan", "trading_volume_breakout",
        "trading_harmonic_scan", "trading_elliot_modern_analysis" -> Domain.TECHNICAL
        "trading_sentiment", "trading_news", "trading_macro_calendar", "trading_fear_greed" -> Domain.SENTIMENT
        "trading_deep_analysis_suite", "trading_backtest", "trading_backtest_optimize",
        "trading_backtest_evolve", "trading_mix_config", "trading_strategy_signal",
        "trading_signal_stats", "trading_signal_data_export", "trading_signal_config_import",
        "trading_fundamental_analysis", "trading_crypto_overview",
        "trading_position_sizing", "trading_correlation_matrix", "trading_economic_data",
        "trading_combined" -> Domain.RESEARCH
        "trading_smc_flow", "trading_smc_analysis", "trading_smc_sweeps", "trading_smc_liquidity",
        "trading_smc_orderblocks", "trading_smc_structure" -> Domain.SMC
        else -> Domain.UNKNOWN
    }
}
