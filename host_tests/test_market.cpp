/**
 * @file test_market.cpp
 * @brief market_chart math (T142): TradingView framing, timeframes, Yahoo symbols, indicators, levels, correlation
 */

#include <math.h>

#include "test_framework.h"
#include "util/MarketMath.h"

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
