/**
 * @file MarketHistory.h
 * @brief Candle history (TradingView chart websocket, Yahoo Finance fallback) for market_chart (T142)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Only called from the webTools task (MarketChart.cpp).
 */

#pragma once

#include <Arduino.h>

#include <vector>

#include "../util/MarketMath.h"

namespace webtools {

struct BarsInfo {
    String name;           // "Bitcoin / TetherUS", "PTT Public Co., Ltd."
    String currency;       // "USDT", "THB"
    const char* source = "";
    int delayMin = 0;      // TradingView without login: SET / some exchanges are 15 min late
};

/** One symbol asked for in a TradingView request. */
struct TvJob {
    String symbol;                       // EXCHANGE:SYMBOL
    const mkt::Timeframe* tf = nullptr;
    uint16_t bars = 0;
    mkt::Bars* out = nullptr;            // filled (owned by the caller)
    BarsInfo info;
    String err;
    bool badSymbol = false;              // TradingView: no such symbol
    bool done = false;
};

/**
 * All jobs over one TradingView websocket (one chart session each). False = TradingView could not be
 * reached or refused the connection (err); a job's own failure is in its err / badSymbol.
 */
bool fetchTradingView(std::vector<TvJob>& jobs, String& err);
/** The same from Yahoo Finance (no fallback symbol, HTTP error or no bars = false). */
bool fetchYahoo(const String& tvSymbol, const mkt::Timeframe& tf, uint16_t bars, mkt::Bars& out, BarsInfo& info,
                String& err);

}  // namespace webtools
