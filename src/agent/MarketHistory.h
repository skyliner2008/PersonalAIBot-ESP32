/**
 * @file MarketHistory.h
 * @brief Candle history (TradingView chart websocket, Yahoo Finance fallback) for market_chart (T142)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Only called from the webTools task (MarketChart.cpp).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "../ui/CardScreen.h"
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
    uint32_t doneMs = 0;                 // when its answer was complete (the log: which symbol was slow)
};

/**
 * All jobs over one TradingView websocket (one chart session each). False = TradingView could not be
 * reached or refused the connection (err); a job's own failure is in its err / badSymbol.
 */
bool fetchTradingView(std::vector<TvJob>& jobs, String& err);
/** The same from Yahoo Finance (no fallback symbol, HTTP error or no bars = false). */
bool fetchYahoo(const String& tvSymbol, const mkt::Timeframe& tf, uint16_t bars, mkt::Bars& out, BarsInfo& info,
                String& err);

// ---- shared by market_chart's views and market_sentiment (MarketChart.cpp) ----

/** One symbol the model named: its EXCHANGE:SYMBOL candidates (alias / ticker on SET, NASDAQ, NYSE) and bars. */
struct Wanted {
    String token;
    const mkt::Timeframe* tf = nullptr;
    uint16_t bars = 0;
    String cands[3];
    int nc = 0;
    const char* alias = nullptr;
    mkt::Bars got[3];
    // the outcome
    bool ok = false;
    String used, name, err;
    BarsInfo info;
    mkt::Bars* chosen = nullptr;
};
/** Every candidate of every symbol in one TradingView request (first with bars wins; Yahoo only when
 *  TradingView could not answer, not when it said the symbol does not exist). `exact`: no candidates, the
 *  token as it is (ECONOMICS:..., USI:PCC). */
void loadAll(std::vector<Wanted>& want, bool exact = false);

double sig6(double v);  // 6 significant digits
double pct2(double v);  // 2 decimals
void fmtPrice(char* out, size_t n, double v);  // 78,003.04 / 41.75 / 1.2345

/** market_chart view "smc" / "signals" (MarketViews.cpp, T143). */
void runSmcView(JsonObjectConst a, JsonDocument& out, CardData& card);
void runSignalsView(JsonObjectConst a, JsonDocument& out, CardData& card);

}  // namespace webtools
