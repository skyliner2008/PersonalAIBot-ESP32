/**
 * @file test_market.cpp
 * @brief market_chart math (T142): TradingView framing, timeframes, Yahoo symbols, indicators, levels, correlation;
 *        SMC, strategy signals, fear & greed, indicator alerts (T143)
 */

#include <math.h>
#include <stdio.h>

#include "test_framework.h"
#include "util/IndicatorAlert.h"
#include "util/MarketMath.h"
#include "util/MarketSignals.h"
#include "util/SmcMath.h"

namespace {

mkt::Bars ramp(int n, double start = 100, double step = 1) {
    mkt::Bars b;
    for (int i = 0; i < n; i++) {
        mkt::Bar x;
        x.t = 1790000000u + i * 3600u;
        x.c = start + step * i;
        x.o = x.c - step / 2;
        x.h = (x.o > x.c ? x.o : x.c) + 0.5;
        x.l = (x.o < x.c ? x.o : x.c) - 0.5;
        x.v = 1000;
        b.push_back(x);
    }
    return b;
}

bool near(double a, double b, double eps = 1e-6) { return fabs(a - b) <= eps; }

}  // namespace

TEST(market_timeframes_and_symbols) {
    CHECK(mkt::timeframe("1h") && !strcmp(mkt::timeframe("1h")->tv, "60"));
    CHECK(!strcmp(mkt::timeframe("H4")->tv, "240"));
    CHECK(!strcmp(mkt::timeframe("")->key, "1d"));  // default
    CHECK(!strcmp(mkt::timeframe("week")->tv, "1W"));
    CHECK(!strcmp(mkt::timeframe("m15")->yInterval, "15m"));
    CHECK(mkt::timeframe("3d") == nullptr);

    CHECK(mkt::yahooSymbol("SET:PTT") == "PTT.BK");
    CHECK(mkt::yahooSymbol("NASDAQ:AAPL") == "AAPL");
    CHECK(mkt::yahooSymbol("BINANCE:BTCUSDT") == "BTC-USD");
    CHECK(mkt::yahooSymbol("BITSTAMP:ETHUSD") == "ETH-USD");
    CHECK(mkt::yahooSymbol("OANDA:XAUUSD") == "GC=F");
    CHECK(mkt::yahooSymbol("FX_IDC:USDTHB") == "USDTHB=X");
    CHECK(mkt::yahooSymbol("SET:SET") == "^SET.BK");
    CHECK(mkt::yahooSymbol("NYMEX:CL1!") == "CL=F");
    CHECK(mkt::yahooSymbol("COMEX:GC1!") == "");  // other futures: no fallback
    CHECK(mkt::yahooSymbol("PTT") == "");
}

TEST(market_tv_framing) {
    // two messages + a heartbeat in one websocket message
    const std::string a = "{\"m\":\"series_loading\"}", h = "~h~12";
    std::string buf = mkt::tvWrap(a) + mkt::tvWrap(h) + mkt::tvWrap("{\"m\":\"x\"}");
    size_t pos = 0, start = 0, n = 0;
    CHECK(mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && buf.substr(start, n) == a);
    CHECK(mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && buf.substr(start, n) == h);
    CHECK(mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && buf.substr(start, n) == "{\"m\":\"x\"}");
    CHECK(!mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n));

    // the length is in UTF-16 units: "ไทย" = 3 units, 9 UTF-8 bytes
    const std::string thai = "{\"d\":\"ไทย\"}";
    CHECK(mkt::tvWrap(thai) == "~m~" + std::to_string(thai.size() - 6) + "~m~" + thai);
    buf = mkt::tvWrap(thai) + mkt::tvWrap(a);
    pos = 0;
    CHECK(mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && buf.substr(start, n) == thai);
    CHECK(mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && buf.substr(start, n) == a);

    // a header whose payload has not arrived
    buf = "~m~50~m~{\"m\":";
    pos = 0;
    CHECK(!mkt::nextTvMessage(buf.data(), buf.size(), pos, start, n) && pos == 0);
}

TEST(market_indicators) {
    // a linear ramp: an SMA-seeded EMA lags by exactly (n - 1) / 2
    const mkt::Bars r = ramp(260);
    const mkt::Series e10 = mkt::ema(r, 10);
    CHECK(isnan(e10[8]) && near(e10[9], r[9].c - 4.5) && near(e10.back(), r.back().c - 4.5));
    const mkt::Series e200 = mkt::ema(r, 200);
    CHECK(isnan(e200[198]) && !isnan(e200[199]));
    CHECK(mkt::ema(ramp(5), 10).size() == 5 && isnan(mkt::ema(ramp(5), 10)[4]));

    CHECK(near(mkt::rsi(r), 100));                    // only gains
    CHECK(near(mkt::rsi(ramp(40, 200, -1)), 0));      // only losses
    CHECK(near(mkt::rsi(ramp(40, 100, 0)), 50));      // flat
    CHECK(isnan(mkt::rsi(ramp(10))));

    // MACD of a ramp: 12/26 lags differ by 7 -> line 7, signal 7, histogram 0
    const mkt::Macd m = mkt::macd(r);
    CHECK(near(m.macd, 7, 1e-6) && near(m.signal, 7, 1e-6) && near(m.hist, 0, 1e-6));
    // speeding up (histogram > 0), then down: it turns negative a few bars after the top
    mkt::Bars ud = ramp(80);
    for (int i = 0; i < 80; i++) ud[i].c = 100 + 0.02 * i * i, ud[i].o = ud[i].c - 0.1;
    CHECK(mkt::macd(ud).hist > 0 && mkt::macd(ud).barsSinceCross == -1);
    for (int i = 0; i < 15; i++) {
        mkt::Bar x = ud.back();
        x.t += 3600;
        x.c -= 2;
        x.o = x.c + 1;
        x.h = x.o + 0.5;
        x.l = x.c - 0.5;
        ud.push_back(x);
    }
    const mkt::Macd m2 = mkt::macd(ud);
    CHECK(m2.hist < 0 && m2.barsSinceCross >= 0 && m2.barsSinceCross < 15);

    // ATR: every bar's true range is 1.5 (high = close + 0.5, low = open - 0.5 = previous close)
    CHECK(near(mkt::atr(r), 1.5, 1e-9));

    const mkt::Bollinger flat = mkt::bollinger(ramp(60, 50, 0));
    CHECK(near(flat.mid, 50) && near(flat.widthPct, 0) && near(flat.percentB, 0.5));
    const mkt::Bollinger up = mkt::bollinger(r);
    CHECK(up.upper > up.mid && up.lower < up.mid && up.percentB > 0.8);

    CHECK(mkt::avgVolume(r) == 1000);
}

TEST(market_levels_and_trend) {
    // a range: swings between ~100 and ~110 (triangle wave, period 20), the last close at 105 heading up
    mkt::Bars b;
    for (int i = 0; i < 125; i++) {
        mkt::Bar x;
        x.t = 1790000000u + i * 86400u;
        const int ph = i % 20;
        x.c = 100 + (ph <= 10 ? ph : 20 - ph);
        x.o = x.c + (ph <= 10 ? -0.5 : 0.5);
        x.h = (x.o > x.c ? x.o : x.c) + 0.3;
        x.l = (x.o < x.c ? x.o : x.c) - 0.3;
        b.push_back(x);
    }
    const double atr = mkt::atr(b);
    const mkt::Levels lv = mkt::levels(b, atr);
    CHECK(lv.support[0].touches >= 3 && fabs(lv.support[0].price - 99.2) < 1.0);
    CHECK(lv.resistance[0].touches >= 3 && fabs(lv.resistance[0].price - 110.3) < 1.0);
    CHECK(lv.support[0].price < b.back().c && lv.resistance[0].price > b.back().c);
    CHECK(mkt::levels(ramp(5), 1).support[0].touches == 0);  // too short

    CHECK(mkt::trend(120, 115, 110, 100, 109) == mkt::Trend::StrongUp);
    CHECK(mkt::trend(120, 115, 110, NAN, 109) == mkt::Trend::Up);  // no EMA200 yet
    CHECK(mkt::trend(80, 85, 90, 100, 91) == mkt::Trend::StrongDown);
    CHECK(mkt::trend(100, 99, 100, 100, 100) == mkt::Trend::Sideways);
    CHECK(!strcmp(mkt::trendTh(mkt::Trend::Down), "ขาลง"));
}

TEST(market_candles_and_correlation) {
    mkt::Bars b = ramp(3);
    // bearish bar, then a bigger bullish bar that covers it
    b[1].o = 105, b[1].c = 102, b[1].h = 106, b[1].l = 101;
    b[2].o = 101.5, b[2].c = 106, b[2].h = 106.5, b[2].l = 101;
    CHECK(strstr(mkt::candleTh(b), "bullish engulfing") != nullptr);
    b[2].o = 104, b[2].c = 104.05, b[2].h = 105, b[2].l = 103;  // tiny body
    CHECK(strstr(mkt::candleTh(b), "โดจิ") != nullptr);
    b[2].o = 104, b[2].c = 104.6, b[2].h = 104.7, b[2].l = 102;  // long lower wick
    CHECK(strstr(mkt::candleTh(b), "ค้อน") != nullptr);

    // correlation of returns: the same moves -> 1, mirrored -> -1, aligned on shared timestamps
    mkt::Bars x, y, z;
    for (int i = 0; i < 60; i++) {
        const double ret = ((i * 7) % 5 - 2) * 0.01;
        mkt::Bar p;
        p.t = 1000 + i * 60;
        p.c = (x.empty() ? 100 : x.back().c) * (1 + ret);
        x.push_back(p);
        p.c = (y.empty() ? 50 : y.back().c) * (1 + ret);
        y.push_back(p);
        p.c = (z.empty() ? 80 : z.back().c) * (1 - ret);
        z.push_back(p);
    }
    size_t used = 0;
    CHECK(near(mkt::correlation(x, y, 100, used), 1, 1e-9) && used == 59);
    CHECK(mkt::correlation(x, z, 100, used) < -0.99);
    mkt::Bars ys(y.begin() + 30, y.end());  // only the second half exists for y
    CHECK(near(mkt::correlation(x, ys, 100, used), 1, 1e-9) && used == 29);
    CHECK(isnan(mkt::correlation(x, mkt::Bars(y.begin(), y.begin() + 5), 100, used)));
    // daily bars opening at different hours (gold 22:00 UTC = the next Thai day, bitcoin 00:00 UTC)
    mkt::Bars gold = x, coin = y;
    for (int i = 0; i < 60; i++) {
        coin[i].t = 1790035200u + i * 86400u;  // 00:00 UTC
        gold[i].t = coin[i].t - 7200;          // 22:00 UTC the evening before = 05:00 Thai the same day
    }
    CHECK(isnan(mkt::correlation(gold, coin, 100, used)) && used == 0);  // no shared timestamp
    CHECK(near(mkt::correlation(gold, coin, 100, used, 86400, 7 * 3600), 1, 1e-9) && used == 59);
    CHECK(!strcmp(mkt::correlationTh(0.85), "ไปทางเดียวกันมาก") && !strcmp(mkt::correlationTh(-0.5), "สวนทางกัน"));

    // Yahoo 1 h -> 4 h bars
    mkt::Bars h = ramp(8);  // starts at 1790000000 = 08:53:20 UTC -> slots of 4 h
    for (mkt::Bar& q : h) q.t -= 1790000000u % 14400u;  // align the first bar to a 4 h boundary
    mkt::mergeHours(h, 4);
    CHECK(h.size() == 2 && near(h[0].o, 99.5) && near(h[0].c, 103) && near(h[0].v, 4000) && near(h[1].c, 107));
}

// ============================================================ T143: SMC, strategy signals, fear & greed, indicator alerts

namespace {

/** Recorded TradingView bars (host_tests/data): "t,o,h,l,c,v" per line. */
mkt::Bars loadBars(const char* name) {
    std::string dir = __FILE__;
    const size_t slash = dir.find_last_of("/\\");
    dir = slash == std::string::npos ? "." : dir.substr(0, slash);
    mkt::Bars b;
    FILE* f = fopen((dir + "/data/" + name).c_str(), "r");
    if (!f) return b;
    unsigned t;
    double o, h, l, c, v;
    while (fscanf(f, "%u,%lf,%lf,%lf,%lf,%lf", &t, &o, &h, &l, &c, &v) == 6) b.push_back({t, o, h, l, c, v});
    fclose(f);
    return b;
}

}  // namespace

// The port gives what the mobile app's SmcEngine gives on the same bars (dumped from the Kotlin code, 2026-10-30)
TEST(smc_matches_the_app) {
    const mkt::Bars gold = loadBars("smc_gold_4h.csv");
    CHECK_EQ(gold.size(), (size_t)300);
    const smc::Snapshot g = smc::analyze(gold);
    CHECK(g.valid && g.st.dir == smc::Dir::Bearish && g.st.last == smc::Event::BMS && g.st.lastSide == -1);
    CHECK(near(g.st.high, 4687.125, 0.01) && near(g.st.low, 3965.15, 0.01) && g.st.highBar == 9 && g.st.lowBar == 298);
    CHECK_EQ(g.st.streak, 6);
    CHECK(g.zone == smc::Zone::Discount && near(g.zonePct, 2.2, 0.06));
    CHECK(g.fvgs.empty() && g.bullObs.empty());
    CHECK_EQ(g.bearObs.size(), (size_t)12);
    CHECK(near(g.bearObs[0].top, 4209.1, 0.01) && g.bearObs[0].bar == 212 && !g.bearObs[0].mitigated);
    CHECK_EQ(g.liquidity.size(), (size_t)9);
    CHECK(near(g.liquidity[0].price, 4184.39, 0.01) && g.liquidity[0].high && g.liquidity[0].stars == 4);
    CHECK_EQ(g.walls.size(), (size_t)3);
    CHECK(g.walls[0].ob && !g.walls[0].bull && g.walls[0].bar == 212 && g.walls[0].confluence == 5);
    CHECK(g.bull.score == 3 && g.bear.score == 5 && g.bear.stars == 4);
    CHECK(!g.sweepBull && !g.sweepBear && g.attack.empty());

    const smc::Snapshot p = smc::analyze(loadBars("smc_ptt_1d.csv"));
    CHECK(p.valid && p.st.dir == smc::Dir::Init && p.st.last == smc::Event::None);  // no CHoCH yet (inducement)
    CHECK(near(p.st.high, 37.25) && near(p.st.low, 30) && p.zone == smc::Zone::Premium);
    CHECK(p.fvgs.size() == 2 && p.bullObs.size() == 4 && p.bearObs.empty());
    CHECK(near(p.bullObs[0].top, 41.75) && p.bullObs[0].bar == 260 && p.bullObs[0].mitigated);
    CHECK(p.liquidity.size() == 10 && near(p.liquidity[0].price, 37) && !p.liquidity[0].high && p.liquidity[0].stars == 4);
    CHECK(p.walls.size() == 4 && !p.walls[0].ob && p.walls[0].bull && p.walls[0].bar == 295);
    CHECK(p.bull.score == 6 && p.bear.score == 5);
}

TEST(smc_parts) {
    // a fair value gap: bar i-2 high 101, a displacement bar, bar i low 103 -> bullish gap 101..103
    mkt::Bars b = ramp(40, 100, 0);
    for (mkt::Bar& x : b) x.h = x.c + 0.3, x.l = x.c - 0.3, x.o = x.c - 0.1;
    b[30].h = 101;
    b[31].o = 100.5, b[31].c = 105, b[31].h = 105.2, b[31].l = 100.4;
    for (int i = 32; i < 40; i++) b[i].o = 104, b[i].c = 104.2, b[i].h = 104.5, b[i].l = 103;
    PsVector<smc::Fvg> f;
    smc::fvgs(b, 30, f);
    CHECK(f.size() == 1 && f[0].bull && near(f[0].top, 103) && near(f[0].bottom, 101) && f[0].bar == 32 && !f[0].mitigated);
    b.back().l = 100.9;  // back into the gap
    smc::fvgs(b, 30, f);
    CHECK(f.size() == 1 && f[0].mitigated);

    // equal highs (within 0.1 %) among the last 10 bars -> buy-side liquidity, the anchor counted
    mkt::Bars e = ramp(30, 100, 0);
    for (mkt::Bar& x : e) x.h = 100.5, x.l = 99.5;
    e[25].h = 110, e[27].h = 110.05;
    PsVector<smc::LiquidityZone> z;
    smc::liquidity(e, z);
    bool eqh = false;
    for (const smc::LiquidityZone& q : z) eqh = eqh || (q.high && q.equal && near(q.price, 110.05) && q.strength == 2);
    CHECK(eqh);

    // a sweep: the last bar dips under the nearest bearish body and closes back over its middle
    mkt::Bars s = ramp(20, 100, 0);
    for (mkt::Bar& x : s) x.o = 100, x.c = 100.2, x.h = 100.4, x.l = 99.9;
    s[18].o = 102, s[18].c = 100, s[18].h = 102.2, s[18].l = 99.8;  // bearish body 100..102
    s[19].o = 100.1, s[19].c = 101.5, s[19].h = 101.6, s[19].l = 98.5;
    bool bull = false, bear = false;
    smc::sweeps(s, bull, bear);
    CHECK(bull && !bear);

    // the last CHoCH stays known after later breaks (indicator alerts)
    const smc::Structure st = smc::structure(loadBars("smc_gold_4h.csv"));
    CHECK(st.chochBar >= 0 && st.chochBar < st.lastBar && st.chochSide == -1);
}

TEST(strategy_signals) {
    const mkt::Adx a = mkt::adx(ramp(80));
    CHECK(a.adx > 90 && a.plusDi > a.minusDi);
    CHECK(isnan(mkt::adx(ramp(20)).adx));

    // a steady rise: momentum, trend, Donchian and the period high agree; the ramp's bars back momentum /
    // trend (body 1/3 of the range) but are too small for a breakout candle
    const mkt::Signals up = mkt::signals(ramp(260));
    CHECK(up.valid && up.regime == mkt::Regime::TrendUpVolatile);
    CHECK(up.s[0].raw == mkt::Vote::Buy && up.s[1].raw == mkt::Vote::Buy && up.s[3].raw == mkt::Vote::Buy &&
          up.s[4].raw == mkt::Vote::Buy && up.s[2].raw == mkt::Vote::None);
    CHECK(up.s[0].vote == mkt::Vote::Buy && up.s[1].vote == mkt::Vote::Buy && up.s[3].vote == mkt::Vote::None);
    CHECK(up.buys == 2 && up.sells == 0 && near(up.score, 2.5) && up.consensus == 2);
    CHECK(!strcmp(mkt::consensusTh(up.consensus), "ซื้อแรง"));

    mkt::Bars dn = ramp(260, 400, -1);
    for (mkt::Bar& x : dn) x.o = x.c + 0.5, x.h = x.o + 0.5, x.l = x.c - 0.5;  // bearish bodies
    const mkt::Signals d = mkt::signals(dn);
    CHECK(d.regime == mkt::Regime::TrendDownVolatile && d.consensus == -2 && d.sells == 2);
    CHECK(d.s[4].raw == mkt::Vote::Sell);  // far under the period high
    CHECK(!mkt::signals(ramp(50)).valid);
}

TEST(fear_greed) {
    std::vector<double> v;
    for (int i = 0; i < 100; i++) v.push_back(i);
    CHECK(near(mkt::percentRank(v), 100));
    v.push_back(-1);
    CHECK(near(mkt::percentRank(v), 0));
    CHECK(isnan(mkt::percentRank(std::vector<double>(5, 1.0))));

    // stocks rising faster each day, VIX and put/call falling, bonds flat: every part greedy
    auto daily = [](int n, double start, double step, double accel) {
        mkt::Bars b;
        for (int i = 0; i < n; i++) {
            mkt::Bar x;
            x.t = 1700000000u + i * 86400u;
            x.c = x.o = x.h = x.l = start + step * i + accel * i * i;
            b.push_back(x);
        }
        return b;
    };
    const mkt::Bars spx = daily(420, 4000, 1, 0.02), vix = daily(420, 40, -0.02, -0.0001),
                    pcr = daily(420, 1.2, -0.0005, -0.000001), tlt = daily(420, 90, 0, 0),
                    hyg = daily(420, 70, 0.01, 0.0001), ief = daily(420, 95, 0, 0);
    const mkt::FearGreed f = mkt::fearGreed(spx, vix, pcr, tlt, hyg, ief);
    CHECK(f.parts == 5 && f.momentum > 90 && f.volatility > 90 && f.putCall > 90 && f.safeHaven > 90 && f.junkBond > 90);
    CHECK(f.score > 90 && !strcmp(mkt::fearGreedTh(f.score), "โลภสุดขีด (Extreme Greed)"));
    const mkt::FearGreed none = mkt::fearGreed(spx, mkt::Bars(), mkt::Bars(), mkt::Bars(), mkt::Bars(), mkt::Bars());
    CHECK(none.parts == 1 && near(none.score, none.momentum));
    CHECK(!strcmp(mkt::fearGreedTh(30), "วิตกกังวล (Fear)") && !strcmp(mkt::fearGreedTh(50), "เป็นกลาง (Neutral)"));

    // the same session date, timestamps an hour apart (index vs fund)
    mkt::Bars a = daily(5, 1, 1, 0), b2 = daily(5, 10, 1, 0);
    for (mkt::Bar& x : b2) x.t += 3600;
    b2.erase(b2.begin() + 2);
    std::vector<double> ca, cb;
    mkt::joinDays(a, b2, ca, cb);
    CHECK(ca.size() == 4 && near(ca[2], 4) && near(cb[2], 13));
}

TEST(indicator_alerts) {
    bool known = false;
    CHECK(ialert::parse("macd_up", &known) == ialert::Cond::MacdUp && known);
    CHECK(ialert::parse("", &known) == ialert::Cond::Price && known);
    CHECK(ialert::parse("stoch", &known) == ialert::Cond::Price && !known);
    CHECK(!strcmp(ialert::key(ialert::Cond::ChochDown), "choch_down") && ialert::barsFor(ialert::Cond::Rsi) == 150);

    // RSI on a rise: 100 -> "rises to 70" holds; it fires only on the change into it
    const ialert::Reading r = ialert::read(ialert::Cond::Rsi, ramp(150), 70, true);
    CHECK(r.ok && r.met && near(r.value, 100) && r.state == 1);
    CHECK(ialert::fires(ialert::Cond::Rsi, 0, r) && !ialert::fires(ialert::Cond::Rsi, 1, r));
    CHECK(!ialert::read(ialert::Cond::Rsi, ramp(150), 30, false).met);

    // EMA20 over EMA50 on the rise: an "up" alert that last saw them the other way round fires
    const ialert::Reading e = ialert::read(ialert::Cond::EmaUp, ramp(150), 0, true);
    CHECK(e.ok && e.met && e.state == 1 && ialert::fires(ialert::Cond::EmaUp, -1, e) && !ialert::fires(ialert::Cond::EmaUp, 1, e));
    const ialert::Reading ed = ialert::read(ialert::Cond::EmaDown, ramp(150), 0, true);
    CHECK(ed.ok && !ed.met && !ialert::fires(ialert::Cond::EmaDown, 1, ed));

    // the last bar is still forming: a crash on it changes nothing until it closes
    mkt::Bars m = ramp(150);
    mkt::Bar crash = m.back();
    crash.t += 3600, crash.c = 10, crash.l = 9;
    m.push_back(crash);
    CHECK(ialert::read(ialert::Cond::Rsi, m, 70, true).met);

    // CHoCH: the newest one (gold 4h: down); a later one on the same side fires, the same one does not
    const ialert::Reading c = ialert::read(ialert::Cond::ChochDown, loadBars("smc_gold_4h.csv"), 0, true);
    CHECK(c.ok && c.met && c.eventT > 0 && c.state == c.eventT);
    CHECK(ialert::fires(ialert::Cond::ChochDown, c.eventT - 1, c) && !ialert::fires(ialert::Cond::ChochDown, c.eventT, c));
    CHECK(!ialert::read(ialert::Cond::ChochUp, loadBars("smc_gold_4h.csv"), 0, true).met);

    char buf[64];
    ialert::describe(ialert::Cond::Rsi, "1 ชั่วโมง", 30, false, buf, sizeof(buf));
    CHECK(!strcmp(buf, "RSI 1 ชั่วโมง ลงถึง 30"));
    ialert::describe(ialert::Cond::MacdUp, "4 ชั่วโมง", 0, true, buf, sizeof(buf));
    CHECK(!strcmp(buf, "MACD 4 ชั่วโมง ตัดขึ้น"));
}
