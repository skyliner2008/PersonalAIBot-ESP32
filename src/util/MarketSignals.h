/**
 * @file MarketSignals.h
 * @brief From candles: ADX / DI, the five strategy signals with a regime-weighted vote (momentum, trend,
 *        reversal, Donchian breakout, near the period high), and a stock-market fear & greed score made of the
 *        CNN components (S&P 500 momentum, VIX, put/call, safe haven, junk bonds) (T143)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * The strategies are the mobile app's StrategySignalProvider (default entry parameters, its closed-candle
 * confirmation gate and regime weights; no tuned parameters, no outcome tracking on the board). Pure C++.
 */

#pragma once

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <vector>

#include "MarketMath.h"

namespace mkt {

// ---------------------------------------------------------------- ADX (Wilder)

struct Adx {
    double adx = NAN, plusDi = NAN, minusDi = NAN;
};

inline Adx adx(const Bars& b, int n = 14) {
    Adx r;
    const int size = (int)b.size();
    if (size < 2 * n + 1) return r;
    PsVector<double> tr(size, 0), pdm(size, 0), ndm(size, 0);
    for (int i = 1; i < size; i++) {
        const double pc = b[i - 1].c;
        tr[i] = std::max(b[i].h - b[i].l, std::max(fabs(b[i].h - pc), fabs(b[i].l - pc)));
        const double up = b[i].h - b[i - 1].h, dn = b[i - 1].l - b[i].l;
        pdm[i] = up > dn && up > 0 ? up : 0;
        ndm[i] = dn > up && dn > 0 ? dn : 0;
    }
    double sTr = 0, sP = 0, sN = 0;
    for (int i = 1; i <= n; i++) sTr += tr[i], sP += pdm[i], sN += ndm[i];
    double adxV = 0, dxSum = 0;
    int dxCount = 0;
    for (int i = n; i < size; i++) {
        if (i > n) {
            sTr = sTr - sTr / n + tr[i];
            sP = sP - sP / n + pdm[i];
            sN = sN - sN / n + ndm[i];
        }
        const double pdi = sTr > 0 ? 100 * sP / sTr : 0, ndi = sTr > 0 ? 100 * sN / sTr : 0;
        const double dx = pdi + ndi > 0 ? 100 * fabs(pdi - ndi) / (pdi + ndi) : 0;
        if (dxCount < n) {
            dxSum += dx;
            if (++dxCount == n) adxV = dxSum / n;
        } else {
            adxV = (adxV * (n - 1) + dx) / n;
        }
        r.plusDi = pdi;
        r.minusDi = ndi;
    }
    if (dxCount >= n) r.adx = adxV;
    return r;
}

// ---------------------------------------------------------------- strategies

enum class Vote : int8_t { Sell = -1, None = 0, Buy = 1 };

struct Strategy {
    const char* key;    // MOM / TR / REV / DC / 52H
    Vote raw = Vote::None;
    Vote vote = Vote::None;  // after the candle confirmation gate
    double weight = 1;
    double value = NAN;      // ROC %, -, RSI, -, % of the period high
};

enum class Regime : uint8_t { TrendUpVolatile, TrendDownVolatile, TrendUp, TrendDown, LowVolRange, HighVol, Range };

struct Signals {
    bool valid = false;  // >= 60 bars
    Strategy s[5] = {{"MOM"}, {"TR"}, {"REV"}, {"DC"}, {"52H"}};
    Regime regime = Regime::Range;
    double score = 0;    // weighted, + = buy
    int buys = 0, sells = 0;
    int consensus = 0;   // -2 strong sell .. +2 strong buy
    double adxValue = NAN, bbWidthPct = NAN;
};

/** The app's closed-candle gate: a vote counts only when the last bar backs it. */
inline bool confirmVote(const Bars& b, Vote v, const char* key) {
    if (v == Vote::None || b.size() < 21) return false;
    const Bar& c = b.back();
    const double range = std::max(c.h - c.l, 1e-9), body = fabs(c.c - c.o), bodyRatio = body / range;
    double a = atr(b, 14);
    a = isnan(a) || a < 1e-9 ? 1e-9 : a;
    const double bodyAtr = body / a;
    const bool buy = v == Vote::Buy;
    const double closeLoc = buy ? (c.c - c.l) / range : (c.h - c.c) / range;
    if (!strcmp(key, "REV")) {
        const double wick = buy ? std::min(c.o, c.c) - c.l : c.h - std::max(c.o, c.c);
        return wick / range >= 0.25 && closeLoc >= 0.55 && bodyRatio >= 0.15;
    }
    if (!strcmp(key, "DC") || !strcmp(key, "52H")) return bodyAtr >= 0.25 && bodyRatio >= 0.35 && closeLoc >= 0.65;
    return bodyAtr >= 0.20 && bodyRatio >= 0.25 && closeLoc >= 0.55;  // MOM / TR
}

inline Regime regimeOf(double adxV, double plusDi, double minusDi, double bbWidth) {
    const bool trend = !isnan(adxV) && adxV >= 25;
    if (trend && plusDi > minusDi && bbWidth >= 2) return Regime::TrendUpVolatile;
    if (trend && minusDi > plusDi && bbWidth >= 2) return Regime::TrendDownVolatile;
    if (trend && plusDi > minusDi) return Regime::TrendUp;
    if (trend && minusDi > plusDi) return Regime::TrendDown;
    if (bbWidth <= 1.5) return Regime::LowVolRange;
    if (bbWidth >= 4) return Regime::HighVol;
    return Regime::Range;
}

inline const char* regimeTh(Regime r) {
    switch (r) {
        case Regime::TrendUpVolatile: return "เทรนด์ขึ้น ผันผวนสูง";
        case Regime::TrendDownVolatile: return "เทรนด์ลง ผันผวนสูง";
        case Regime::TrendUp: return "เทรนด์ขึ้น";
        case Regime::TrendDown: return "เทรนด์ลง";
        case Regime::LowVolRange: return "ไซด์เวย์ ผันผวนต่ำ";
        case Regime::HighVol: return "ผันผวนสูง ไม่มีทิศ";
        default: return "ไซด์เวย์";
    }
}

inline Signals signals(const Bars& b) {
    Signals r;
    const int n = (int)b.size();
    if (n < 60) return r;
    r.valid = true;
    const Bar& last = b.back();
    // 1) time-series momentum: 20-bar rate of change
    const double ref = b[n - 1 - 20].c;
    const double roc = ref ? (last.c - ref) / ref * 100 : 0;
    r.s[0].value = roc;
    r.s[0].raw = roc > 0 ? Vote::Buy : (roc < 0 ? Vote::Sell : Vote::None);
    // 2) trend following: EMA50 / EMA200 and the close against EMA200
    const Series e50 = ema(b, 50), e200 = ema(b, 200);
    const double f = e50.back(), s = e200.back();
    if (!isnan(f) && !isnan(s)) {
        if (last.c > s && f > s) r.s[1].raw = Vote::Buy;
        else if (last.c < s && f < s) r.s[1].raw = Vote::Sell;
    }
    // 3) short-term reversal: RSI 30 / 70 at the Bollinger band
    const double rs = rsi(b, 14);
    const Bollinger bb = bollinger(b, 20, 2);
    r.s[2].value = rs;
    if (!isnan(rs) && rs < 30 && last.c <= bb.lower) r.s[2].raw = Vote::Buy;
    else if (!isnan(rs) && rs > 70 && last.c >= bb.upper) r.s[2].raw = Vote::Sell;
    // 4) Donchian 20 breakout (the channel without the current bar)
    double up = -INFINITY, dn = INFINITY;
    for (int i = n - 1 - 20; i < n - 1; i++) up = std::max(up, b[i].h), dn = std::min(dn, b[i].l);
    r.s[3].raw = last.c > up ? Vote::Buy : (last.c < dn ? Vote::Sell : Vote::None);
    // 5) near the period high (up to 2000 bars before the current one): >= 98 % buy, <= 90 % sell
    double hi = 0;
    for (int i = std::max(0, n - 1 - 2000); i < n - 1; i++) hi = std::max(hi, b[i].h);
    const double prox = hi > 0 ? last.c / hi : 0;
    r.s[4].value = prox * 100;
    r.s[4].raw = prox >= 0.98 ? Vote::Buy : (prox <= 0.90 ? Vote::Sell : Vote::None);

    const Adx dmi = adx(b, 14);
    r.adxValue = dmi.adx;
    r.bbWidthPct = bb.widthPct;
    if (isnan(dmi.adx)) {  // no ADX: the trend strategy's state stands in (the app does the same)
        r.regime = r.s[1].raw == Vote::Buy ? Regime::TrendUp : (r.s[1].raw == Vote::Sell ? Regime::TrendDown : Regime::Range);
    } else {
        r.regime = regimeOf(dmi.adx, dmi.plusDi, dmi.minusDi, bb.widthPct);
    }
    static const double kTrend[5] = {1.2, 1.3, 0.5, 1.2, 0.9}, kRange[5] = {0.8, 0.6, 1.3, 0.7, 0.8},
                        kOther[5] = {1.0, 1.0, 0.8, 1.0, 0.9};
    const bool trending = r.regime == Regime::TrendUp || r.regime == Regime::TrendDown ||
                          r.regime == Regime::TrendUpVolatile || r.regime == Regime::TrendDownVolatile;
    const double* w = trending ? kTrend : (r.regime == Regime::Range || r.regime == Regime::LowVolRange ? kRange : kOther);
    for (int k = 0; k < 5; k++) {
        Strategy& st = r.s[k];
        st.weight = w[k];
        st.vote = confirmVote(b, st.raw, st.key) ? st.raw : Vote::None;
        r.score += (int)st.vote * st.weight;
        r.buys += st.vote == Vote::Buy;
        r.sells += st.vote == Vote::Sell;
    }
    const int agree = std::max(r.buys, r.sells);
    if (r.score >= 2.2 && agree >= 2) r.consensus = 2;
    else if (r.score >= 1.0 && agree >= 1) r.consensus = 1;
    else if (r.score <= -2.2 && agree >= 2) r.consensus = -2;
    else if (r.score <= -1.0 && agree >= 1) r.consensus = -1;
    return r;
}

inline const char* consensusTh(int c) {
    switch (c) {
        case 2: return "ซื้อแรง";
        case 1: return "ซื้อ";
        case -1: return "ขาย";
        case -2: return "ขายแรง";
        default: return "เป็นกลาง";
    }
}

// ---------------------------------------------------------------- fear & greed

/** Where the last value sits among the last `window` values: 0-100 (NAN entries skipped; NAN if < 20). */
inline double percentRank(const std::vector<double>& v, size_t window = 252) {
    if (v.empty() || isnan(v.back())) return NAN;
    const size_t from = v.size() > window ? v.size() - window : 0;
    size_t count = 0, below = 0;
    for (size_t i = from; i < v.size(); i++) {
        if (isnan(v[i])) continue;
        count++;
        below += v[i] <= v.back();
    }
    return count < 20 ? NAN : 100.0 * (below - 1) / (count - 1 > 0 ? count - 1 : 1);
}

/** Closes of a and b on the days both have (US daily bars: the same session date). */
inline void joinDays(const Bars& a, const Bars& b, std::vector<double>& ca, std::vector<double>& cb,
                     int32_t utcOffset = -5 * 3600) {
    ca.clear();
    cb.clear();
    size_t i = 0, j = 0;
    auto day = [&](uint32_t t) { return (uint32_t)(((int64_t)t + utcOffset) / 86400); };
    while (i < a.size() && j < b.size()) {
        const uint32_t da = day(a[i].t), db = day(b[j].t);
        if (da < db) i++;
        else if (db < da) j++;
        else ca.push_back(a[i++].c), cb.push_back(b[j++].c);
    }
}

struct FearGreed {
    // each 0 (fear) .. 100 (greed), NAN = no data
    double momentum = NAN, volatility = NAN, putCall = NAN, safeHaven = NAN, junkBond = NAN;
    double score = NAN;
    int parts = 0;
};

/** The CNN components, each as its percentile of the last year: S&P 500 against its 125-day average, VIX
 *  against its 50-day average (inverted), the 5-day put/call ratio (inverted), stocks minus treasuries over
 *  20 days, high-yield against treasury bonds over 20 days. */
inline FearGreed fearGreed(const Bars& spx, const Bars& vix, const Bars& pcr, const Bars& tlt, const Bars& hyg,
                           const Bars& ief) {
    FearGreed f;
    auto smaRatio = [](const Bars& b, int n, std::vector<double>& out) {
        out.assign(b.size(), NAN);
        double s = 0;
        for (size_t i = 0; i < b.size(); i++) {
            s += b[i].c;
            if (i >= (size_t)n) s -= b[i - n].c;
            if (i + 1 >= (size_t)n && s > 0) out[i] = b[i].c / (s / n) - 1;
        }
    };
    std::vector<double> v;
    smaRatio(spx, 125, v);
    f.momentum = percentRank(v);
    smaRatio(vix, 50, v);
    if (!isnan(percentRank(v))) f.volatility = 100 - percentRank(v);
    v.assign(pcr.size(), NAN);
    for (size_t i = 4; i < pcr.size(); i++) v[i] = (pcr[i].c + pcr[i - 1].c + pcr[i - 2].c + pcr[i - 3].c + pcr[i - 4].c) / 5;
    if (!isnan(percentRank(v))) f.putCall = 100 - percentRank(v);
    std::vector<double> a, b2;
    auto spread = [&](const Bars& x, const Bars& y, double& out) {
        joinDays(x, y, a, b2);
        v.assign(a.size(), NAN);
        for (size_t i = 20; i < a.size(); i++) {
            if (a[i - 20] > 0 && b2[i - 20] > 0) v[i] = (a[i] / a[i - 20] - 1) - (b2[i] / b2[i - 20] - 1);
        }
        out = percentRank(v);
    };
    spread(spx, tlt, f.safeHaven);
    spread(hyg, ief, f.junkBond);
    double s = 0;
    for (double x : {f.momentum, f.volatility, f.putCall, f.safeHaven, f.junkBond}) {
        if (!isnan(x)) s += x, f.parts++;
    }
    if (f.parts) f.score = s / f.parts;
    return f;
}

/** The app's bands: <= 24 extreme fear, <= 44 fear, <= 55 neutral, <= 75 greed, else extreme greed. */
inline const char* fearGreedTh(double score) {
    if (isnan(score)) return "ไม่มีข้อมูล";
    if (score <= 24) return "กลัวสุดขีด (Extreme Fear)";
    if (score <= 44) return "วิตกกังวล (Fear)";
    if (score <= 55) return "เป็นกลาง (Neutral)";
    if (score <= 75) return "โลภ (Greed)";
    return "โลภสุดขีด (Extreme Greed)";
}

}  // namespace mkt
