/**
 * @file MarketMath.h
 * @brief Candles (OHLCV) and what the market_chart tool reads from them: EMA, RSI, MACD, ATR, Bollinger,
 *        support / resistance from swing points, trend, the last candle's shape, correlation (T142)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Pure C++ (host tested). The candles come from the TradingView chart websocket (MarketHistory.cpp) or
 * Yahoo Finance as a fallback; the helpers for both formats (TradingView "~m~<len>~m~" framing, the
 * timeframe table, TradingView symbol -> Yahoo symbol) live here too.
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "PsramAlloc.h"

namespace mkt {

struct Bar {
    uint32_t t = 0;  // bar open, unix seconds (UTC)
    double o = 0, h = 0, l = 0, c = 0, v = 0;
};
using Bars = std::vector<Bar, PsramAllocator<Bar>>;
using Series = std::vector<double, PsramAllocator<double>>;

// ---------------------------------------------------------------- timeframes

struct Timeframe {
    const char* key;       // what the tool takes: "5m" .. "1w"
    const char* tv;        // TradingView resolution
    const char* yInterval; // Yahoo interval / range for the same history
    const char* yRange;
    uint16_t bars;         // history to ask for (enough for EMA200 + 60 candles on the card)
    const char* label;     // Thai
};

inline const Timeframe* timeframe(const char* key) {
    static const Timeframe kTf[] = {
        {"5m", "5", "5m", "5d", 300, "5 นาที"},        {"15m", "15", "15m", "1mo", 300, "15 นาที"},
        {"1h", "60", "60m", "3mo", 300, "1 ชั่วโมง"},   {"4h", "240", "60m", "6mo", 300, "4 ชั่วโมง"},
        {"1d", "1D", "1d", "2y", 300, "รายวัน"},       {"1w", "1W", "1wk", "10y", 260, "รายสัปดาห์"},
    };
    std::string k = key ? key : "";
    for (char& ch : k) ch = (char)tolower((unsigned char)ch);
    if (k.empty() || k == "d" || k == "day" || k == "1day" || k == "d1") k = "1d";
    else if (k == "w" || k == "week" || k == "w1") k = "1w";
    else if (k == "h1" || k == "60" || k == "60m") k = "1h";
    else if (k == "h4" || k == "240") k = "4h";
    else if (k == "m15" || k == "15") k = "15m";
    else if (k == "m5" || k == "5") k = "5m";
    for (const Timeframe& t : kTf) {
        if (k == t.key) return &t;
    }
    return nullptr;
}

/** Yahoo has no 4-hour bars: four 1-hour bars -> one (aligned to 00/04/08/... UTC). */
inline void mergeHours(Bars& b, int hours) {
    Bars out;
    for (const Bar& x : b) {
        const uint32_t slot = x.t - x.t % (3600u * hours);
        if (!out.empty() && out.back().t == slot) {
            Bar& m = out.back();
            m.h = x.h > m.h ? x.h : m.h;
            m.l = x.l < m.l ? x.l : m.l;
            m.c = x.c;
            m.v += x.v;
        } else {
            Bar m = x;
            m.t = slot;
            out.push_back(m);
        }
    }
    b.swap(out);
}

// ---------------------------------------------------------------- TradingView websocket framing

/**
 * Next "~m~<len>~m~<payload>" message in buf from pos. <len> counts UTF-16 units (JavaScript), so a payload
 * with non-ASCII text is longer in UTF-8 bytes: it ends at the next header found at or after start + len.
 * False when no complete message is left (a header without its payload stays for the next read).
 */
inline bool nextTvMessage(const char* buf, size_t len, size_t& pos, size_t& start, size_t& mlen) {
    auto header = [&](size_t at, size_t& payload, size_t& n) -> bool {
        if (at + 3 > len || memcmp(buf + at, "~m~", 3) != 0) return false;
        size_t i = at + 3, v = 0;
        if (i >= len || buf[i] < '0' || buf[i] > '9') return false;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') v = v * 10 + (size_t)(buf[i++] - '0');
        if (i + 3 > len || memcmp(buf + i, "~m~", 3) != 0) return false;
        payload = i + 3;
        n = v;
        return true;
    };
    size_t payload = 0, n = 0;
    if (!header(pos, payload, n)) return false;
    if (payload + n > len) return false;
    size_t end = payload + n;
    // non-ASCII text: move on to the next header (or the end of the data)
    size_t p2 = 0, n2 = 0;
    while (end < len && !header(end, p2, n2)) end++;
    start = payload;
    mlen = end - payload;
    pos = end;
    return true;
}

inline std::string tvWrap(const std::string& payload) {
    // the length TradingView expects is in UTF-16 units: count UTF-8 lead bytes (4-byte sequences = 2 units)
    size_t units = 0;
    for (unsigned char ch : payload) {
        if ((ch & 0xC0) != 0x80) units += (ch >= 0xF0) ? 2 : 1;
    }
    return "~m~" + std::to_string(units) + "~m~" + payload;
}

// ---------------------------------------------------------------- Yahoo fallback symbols

/** TradingView symbol -> Yahoo Finance symbol ("" = no fallback for it). */
inline std::string yahooSymbol(const std::string& tv) {
    struct Map {
        const char* tv;
        const char* y;
    };
    static const Map kMap[] = {
        {"OANDA:XAUUSD", "GC=F"}, {"TVC:GOLD", "GC=F"},     {"TVC:SILVER", "SI=F"},     {"OANDA:XAGUSD", "SI=F"},
        {"NYMEX:CL1!", "CL=F"},   {"TVC:USOIL", "CL=F"},    {"ICEEUR:BRN1!", "BZ=F"},   {"SET:SET", "^SET.BK"},
        {"SP:SPX", "^GSPC"},      {"NASDAQ:NDX", "^NDX"},   {"DJ:DJI", "^DJI"},         {"TVC:DXY", "DX-Y.NYB"},
    };
    for (const Map& m : kMap) {
        if (tv == m.tv) return m.y;
    }
    const size_t colon = tv.find(':');
    if (colon == std::string::npos) return "";
    const std::string ex = tv.substr(0, colon), sym = tv.substr(colon + 1);
    if (sym.empty() || sym.find('!') != std::string::npos) return "";
    if (ex == "SET") return sym + ".BK";
    if (ex == "NASDAQ" || ex == "NYSE" || ex == "AMEX" || ex == "NYSEARCA") return sym;
    if (ex == "FX" || ex == "FX_IDC" || ex == "OANDA" || ex == "FOREXCOM" || ex == "SAXO") {
        return sym.size() == 6 ? sym + "=X" : "";
    }
    if (ex == "BINANCE" || ex == "BITSTAMP" || ex == "COINBASE" || ex == "KRAKEN" || ex == "BYBIT" || ex == "OKX") {
        for (const char* q : {"USDT", "USDC", "USD"}) {
            const size_t ql = strlen(q);
            if (sym.size() > ql && sym.compare(sym.size() - ql, ql, q) == 0) return sym.substr(0, sym.size() - ql) + "-USD";
        }
    }
    return "";
}

// ---------------------------------------------------------------- indicators (NAN until enough bars)

inline Series ema(const Bars& b, int n) {
    Series out(b.size(), NAN);
    if (n < 1 || (int)b.size() < n) return out;
    double s = 0;
    for (int i = 0; i < n; i++) s += b[i].c;
    double e = s / n;  // seeded with the SMA of the first n closes
    out[n - 1] = e;
    const double k = 2.0 / (n + 1);
    for (size_t i = n; i < b.size(); i++) out[i] = e = b[i].c * k + e * (1 - k);
    return out;
}

/** Wilder's RSI of the last bar (NAN if fewer than n + 1 bars). */
inline double rsi(const Bars& b, int n = 14, size_t endAt = 0) {
    const size_t last = endAt ? endAt : b.size();
    if ((int)last < n + 1) return NAN;
    double up = 0, dn = 0;
    for (int i = 1; i <= n; i++) {
        const double d = b[i].c - b[i - 1].c;
        (d > 0 ? up : dn) += fabs(d);
    }
    up /= n;
    dn /= n;
    for (size_t i = n + 1; i < last; i++) {
        const double d = b[i].c - b[i - 1].c;
        up = (up * (n - 1) + (d > 0 ? d : 0)) / n;
        dn = (dn * (n - 1) + (d < 0 ? -d : 0)) / n;
    }
    if (dn == 0) return up == 0 ? 50 : 100;
    return 100 - 100 / (1 + up / dn);
}

struct Macd {
    double macd = NAN, signal = NAN, hist = NAN;
    int barsSinceCross = -1;  // bars since the histogram changed sign (-1 = none seen)
};
inline Macd macd(const Bars& b, int fast = 12, int slow = 26, int sig = 9) {
    Macd m;
    const Series ef = ema(b, fast), es = ema(b, slow);
    double s = NAN;
    int count = 0, prevSign = 0;  // the histogram's last clear sign (rounding noise around 0 is not a cross)
    const double k = 2.0 / (sig + 1);
    double seed = 0;
    for (size_t i = 0; i < b.size(); i++) {
        if (isnan(es[i])) continue;
        const double line = ef[i] - es[i];
        if (count < sig) {
            seed += line;
            if (++count == sig) s = seed / sig;
            else continue;
        } else {
            s = line * k + s * (1 - k);
        }
        const double h = line - s;
        const int sign = fabs(h) <= 1e-9 * (fabs(b[i].c) + 1) ? 0 : (h > 0 ? 1 : -1);
        if (m.barsSinceCross >= 0) m.barsSinceCross++;
        if (sign && prevSign && sign != prevSign) m.barsSinceCross = 0;
        if (sign) prevSign = sign;
        m.macd = line;
        m.signal = s;
        m.hist = h;
    }
    return m;
}

/** Wilder's average true range of the last bar. */
inline double atr(const Bars& b, int n = 14) {
    if ((int)b.size() < n + 1) return NAN;
    auto tr = [&](size_t i) {
        const double pc = b[i - 1].c;
        double r = b[i].h - b[i].l;
        r = fabs(b[i].h - pc) > r ? fabs(b[i].h - pc) : r;
        return fabs(b[i].l - pc) > r ? fabs(b[i].l - pc) : r;
    };
    double a = 0;
    for (int i = 1; i <= n; i++) a += tr(i);
    a /= n;
    for (size_t i = n + 1; i < b.size(); i++) a = (a * (n - 1) + tr(i)) / n;
    return a;
}

struct Bollinger {
    double mid = NAN, upper = NAN, lower = NAN;
    double widthPct = NAN;   // (upper - lower) / mid, %
    double percentB = NAN;   // 0 = on the lower band, 1 = on the upper
    bool squeeze = false;    // width in the lowest 10 % of the last 120 bars
};
inline Bollinger bollinger(const Bars& b, int n = 20, double k = 2) {
    Bollinger r;
    if ((int)b.size() < n) return r;
    auto widthAt = [&](size_t end, double& mid, double& sd) {  // bars [end - n, end)
        double s = 0, s2 = 0;
        for (size_t i = end - n; i < end; i++) {
            s += b[i].c;
            s2 += b[i].c * b[i].c;
        }
        mid = s / n;
        const double var = s2 / n - mid * mid;
        sd = var > 0 ? sqrt(var) : 0;
        return mid != 0 ? 2 * k * sd / mid * 100 : 0;
    };
    double mid = 0, sd = 0;
    r.widthPct = widthAt(b.size(), mid, sd);
    r.mid = mid;
    r.upper = mid + k * sd;
    r.lower = mid - k * sd;
    r.percentB = r.upper > r.lower ? (b.back().c - r.lower) / (r.upper - r.lower) : 0.5;
    const size_t look = b.size() - n + 1 < 120 ? b.size() - n + 1 : 120;
    if (look >= 40) {
        int below = 0;
        for (size_t j = 0; j < look; j++) {
            double m2, s2;
            if (widthAt(b.size() - j, m2, s2) < r.widthPct) below++;
        }
        r.squeeze = below <= (int)(look / 10);
    }
    return r;
}

// ---------------------------------------------------------------- support / resistance

struct Level {
    double price = 0;
    int touches = 0;   // swing points merged into it
    uint32_t last = 0; // the newest swing in it
};
struct Levels {
    Level support[2], resistance[2];  // nearest first; touches == 0 = none
};

/**
 * Swing highs / lows (a bar higher / lower than `side` bars on each side) of the last `lookback` bars,
 * merged when closer than half an ATR; the nearest two below and above the last close (each at least a
 * tenth of an ATR away, so the current bar is not its own level).
 */
inline Levels levels(const Bars& b, double atrValue, int lookback = 200, int side = 3) {
    Levels out;
    if ((int)b.size() < 2 * side + 1 || !(atrValue > 0)) return out;
    const size_t from = b.size() > (size_t)lookback ? b.size() - lookback : 0;
    std::vector<Level> pts;
    for (size_t i = from + side; i + side < b.size(); i++) {
        bool hi = true, lo = true;
        for (int k = 1; k <= side; k++) {
            hi = hi && b[i].h >= b[i - k].h && b[i].h > b[i + k].h;
            lo = lo && b[i].l <= b[i - k].l && b[i].l < b[i + k].l;
        }
        if (hi) pts.push_back({b[i].h, 1, b[i].t});
        if (lo) pts.push_back({b[i].l, 1, b[i].t});
    }
    // merge: the closest pair under the tolerance first, until none is left
    const double tol = atrValue * 0.5;
    for (bool merged = true; merged;) {
        merged = false;
        size_t bi = 0, bj = 0;
        double best = tol;
        for (size_t i = 0; i < pts.size(); i++) {
            for (size_t j = i + 1; j < pts.size(); j++) {
                const double d = fabs(pts[i].price - pts[j].price);
                if (d < best) best = d, bi = i, bj = j, merged = true;
            }
        }
        if (!merged) break;
        Level& a = pts[bi];
        const Level& c = pts[bj];
        a.price = (a.price * a.touches + c.price * c.touches) / (a.touches + c.touches);
        a.touches += c.touches;
        a.last = c.last > a.last ? c.last : a.last;
        pts.erase(pts.begin() + bj);
    }
    const double px = b.back().c, gap = atrValue * 0.1;
    for (const Level& p : pts) {
        Level* side2 = p.price < px - gap ? out.support : (p.price > px + gap ? out.resistance : nullptr);
        if (!side2) continue;
        const double d = fabs(p.price - px);
        for (int k = 0; k < 2; k++) {
            if (!side2[k].touches || d < fabs(side2[k].price - px)) {
                if (k == 0) side2[1] = side2[0];
                side2[k] = p;
                break;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- trend / candle / stats

enum class Trend : int8_t { StrongDown = -2, Down = -1, Sideways = 0, Up = 1, StrongUp = 2 };

/** From the close against EMA20 / 50 / 200 (200 may be NAN: short history) and EMA50's slope. */
inline Trend trend(double c, double e20, double e50, double e200, double e50Before) {
    if (isnan(e20) || isnan(e50)) return Trend::Sideways;
    const bool rising = !isnan(e50Before) && e50 > e50Before, falling = !isnan(e50Before) && e50 < e50Before;
    if (c > e20 && e20 > e50 && rising) return (!isnan(e200) && e50 > e200) ? Trend::StrongUp : Trend::Up;
    if (c < e20 && e20 < e50 && falling) return (!isnan(e200) && e50 < e200) ? Trend::StrongDown : Trend::Down;
    if (c > e50 && rising) return Trend::Up;
    if (c < e50 && falling) return Trend::Down;
    return Trend::Sideways;
}

inline const char* trendTh(Trend t) {
    switch (t) {
        case Trend::StrongUp: return "ขาขึ้นแข็งแรง";
        case Trend::Up: return "ขาขึ้น";
        case Trend::Down: return "ขาลง";
        case Trend::StrongDown: return "ขาลงแรง";
        default: return "ไซด์เวย์";
    }
}

/** The last bar's shape ("" = nothing special). */
inline const char* candleTh(const Bars& b) {
    if (b.size() < 2) return "";
    const Bar& x = b.back();
    const Bar& p = b[b.size() - 2];
    const double range = x.h - x.l, body = fabs(x.c - x.o);
    if (range <= 0) return "";
    const double upper = x.h - (x.c > x.o ? x.c : x.o), lower = (x.c < x.o ? x.c : x.o) - x.l;
    const bool bull = x.c > x.o, pBull = p.c > p.o;
    if (bull && !pBull && x.c >= p.o && x.o <= p.c && body > fabs(p.c - p.o)) return "กลืนกินขาขึ้น (bullish engulfing)";
    if (!bull && pBull && x.c <= p.o && x.o >= p.c && body > fabs(p.c - p.o)) return "กลืนกินขาลง (bearish engulfing)";
    if (body <= range * 0.1) return "โดจิ (ลังเล)";
    if (lower >= body * 2 && upper <= body * 0.6) return "ค้อน (ไส้ล่างยาว แรงซื้อรับ)";
    if (upper >= body * 2 && lower <= body * 0.6) return "ดาวตก (ไส้บนยาว แรงขายกด)";
    return "";
}

/** Average volume of the `n` bars before the last one (0 = no volume data). */
inline double avgVolume(const Bars& b, int n = 20) {
    if ((int)b.size() < n + 1) return 0;
    double s = 0;
    for (size_t i = b.size() - 1 - n; i + 1 < b.size(); i++) s += b[i].v;
    return s / n;
}

// ---------------------------------------------------------------- correlation

/**
 * Pearson correlation of the bar-to-bar returns of a and b, on the bars both have (the last `maxN` of them).
 * `daySec` = 0: the same timestamp; else the same local day (t + utcOffset) / daySec - daily bars start at
 * each market's own session open (gold 22:00 UTC, bitcoin 00:00, SET 02:55), so they never share a timestamp.
 * `used` = returns compared; NAN when fewer than 10.
 */
inline double correlation(const Bars& a, const Bars& b, size_t maxN, size_t& used, uint32_t daySec = 0,
                          int32_t utcOffset = 0) {
    auto key = [&](uint32_t t) -> uint32_t { return daySec ? (uint32_t)((int64_t)t + utcOffset) / daySec : t; };
    std::vector<double> ra, rb;
    size_t i = 0, j = 0;
    double pa = NAN, pb = NAN;
    while (i < a.size() && j < b.size()) {
        const uint32_t ka = key(a[i].t), kb = key(b[j].t);
        if (ka < kb) i++;
        else if (kb < ka) j++;
        else {
            if (!isnan(pa) && pa > 0 && pb > 0) {
                ra.push_back(a[i].c / pa - 1);
                rb.push_back(b[j].c / pb - 1);
            }
            pa = a[i].c;
            pb = b[j].c;
            i++;
            j++;
        }
    }
    const size_t n = ra.size() < maxN ? ra.size() : maxN, off = ra.size() - n;
    used = n;
    if (n < 10) return NAN;
    double ma = 0, mb = 0;
    for (size_t k = off; k < ra.size(); k++) ma += ra[k], mb += rb[k];
    ma /= n;
    mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t k = off; k < ra.size(); k++) {
        sab += (ra[k] - ma) * (rb[k] - mb);
        saa += (ra[k] - ma) * (ra[k] - ma);
        sbb += (rb[k] - mb) * (rb[k] - mb);
    }
    if (saa <= 0 || sbb <= 0) return NAN;
    return sab / sqrt(saa * sbb);
}

inline const char* correlationTh(double r) {
    if (isnan(r)) return "ข้อมูลไม่พอ";
    const double a = fabs(r);
    const char* dir = r >= 0 ? "ไปทางเดียวกัน" : "สวนทางกัน";
    if (a >= 0.7) return r >= 0 ? "ไปทางเดียวกันมาก" : "สวนทางกันมาก";
    if (a >= 0.4) return dir;
    if (a >= 0.2) return r >= 0 ? "ไปทางเดียวกันเล็กน้อย" : "สวนทางกันเล็กน้อย";
    return "แทบไม่สัมพันธ์กัน";
}

}  // namespace mkt
