/**
 * @file IndicatorAlert.h
 * @brief price_alert conditions beyond a price (T143): RSI reaching a level, MACD crossing its signal line,
 *        EMA20 crossing EMA50, an SMC change of character - on closed bars of a chosen timeframe
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Each alert keeps a `state` (alerts.json) from the last check: an alert fires on the change into its
 * condition, not while the condition simply holds (RSI already under 30 when it is set waits for the next
 * time it goes under). Pure C++ (host tested); the web task fetches the candles (WebToolsExtra.cpp).
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MarketMath.h"
#include "SmcMath.h"

namespace ialert {

enum class Cond : uint8_t { Price, Rsi, MacdUp, MacdDown, EmaUp, EmaDown, ChochUp, ChochDown };

struct CondName {
    Cond c;
    const char* key;
};
inline const CondName* condNames() {
    static const CondName k[] = {{Cond::Price, "price"},       {Cond::Rsi, "rsi"},         {Cond::MacdUp, "macd_up"},
                                 {Cond::MacdDown, "macd_down"}, {Cond::EmaUp, "ema_up"},   {Cond::EmaDown, "ema_down"},
                                 {Cond::ChochUp, "choch_up"},   {Cond::ChochDown, "choch_down"}};
    return k;
}
constexpr int kConds = 8;

/** "" / unknown = price. */
inline Cond parse(const char* key, bool* known = nullptr) {
    if (known) *known = !key || !*key;
    if (!key || !*key) return Cond::Price;
    for (int i = 0; i < kConds; i++) {
        if (!strcmp(key, condNames()[i].key)) {
            if (known) *known = true;
            return condNames()[i].c;
        }
    }
    return Cond::Price;
}
inline const char* key(Cond c) { return condNames()[(int)c].key; }

/** Bars to fetch: the indicators settle in ~150; the market structure wants the full window. */
inline uint16_t barsFor(Cond c) { return c == Cond::ChochUp || c == Cond::ChochDown ? 300 : 150; }

struct Reading {
    bool ok = false;
    double value = NAN;  // RSI / the last closed price
    double state = 0;    // what is stored for the next check
    bool met = false;    // the condition holds now
    uint32_t eventT = 0; // CHoCH: the bar it happened on
};

/**
 * The condition on the closed bars (all but the last one, which is still forming). `level`: the RSI level;
 * `above`: RSI rises to it (else falls to it).
 */
inline Reading read(Cond c, const mkt::Bars& all, double level, bool above) {
    Reading r;
    if (all.size() < 40) return r;
    mkt::Bars b(all.begin(), all.end() - 1);
    r.value = b.back().c;
    switch (c) {
        case Cond::Rsi: {
            const double v = mkt::rsi(b, 14);
            if (isnan(v)) return r;
            r.value = v;
            r.met = above ? v >= level : v <= level;
            r.state = r.met ? 1 : 0;
            break;
        }
        case Cond::MacdUp:
        case Cond::MacdDown: {
            const mkt::Macd m = mkt::macd(b);
            if (isnan(m.hist)) return r;
            const int sign = m.hist > 0 ? 1 : (m.hist < 0 ? -1 : 0);
            r.state = sign;
            r.met = c == Cond::MacdUp ? sign > 0 : sign < 0;
            break;
        }
        case Cond::EmaUp:
        case Cond::EmaDown: {
            const mkt::Series e20 = mkt::ema(b, 20), e50 = mkt::ema(b, 50);
            if (isnan(e50.back())) return r;
            const double d = e20.back() - e50.back();
            const int sign = d > 0 ? 1 : (d < 0 ? -1 : 0);
            r.state = sign;
            r.met = c == Cond::EmaUp ? sign > 0 : sign < 0;
            break;
        }
        case Cond::ChochUp:
        case Cond::ChochDown: {
            const smc::Structure s = smc::structure(b);
            if (s.chochBar >= 0) {
                r.eventT = b[s.chochBar].t;
                r.met = s.chochSide == (c == Cond::ChochUp ? 1 : -1);
            }
            r.state = r.eventT;  // the newest CHoCH seen, either side
            break;
        }
        default:
            return r;
    }
    r.ok = true;
    return r;
}

/** Fires on the change: RSI / crosses from not-met to met; CHoCH: a newer one on the wanted side. */
inline bool fires(Cond c, double before, const Reading& now) {
    if (!now.ok || !now.met) return false;
    switch (c) {
        case Cond::Rsi: return before == 0;
        case Cond::MacdUp:
        case Cond::EmaUp: return before <= 0;
        case Cond::MacdDown:
        case Cond::EmaDown: return before >= 0;
        case Cond::ChochUp:
        case Cond::ChochDown: return now.eventT > (uint32_t)before;
        default: return false;
    }
}

/** "RSI 1 ชั่วโมง ลงถึง 30" - the alerts screen and the list. */
inline void describe(Cond c, const char* tfLabel, double level, bool above, char* out, size_t n) {
    switch (c) {
        case Cond::Rsi: snprintf(out, n, "RSI %s %s %.0f", tfLabel, above ? "ขึ้นถึง" : "ลงถึง", level); break;
        case Cond::MacdUp: snprintf(out, n, "MACD %s ตัดขึ้น", tfLabel); break;
        case Cond::MacdDown: snprintf(out, n, "MACD %s ตัดลง", tfLabel); break;
        case Cond::EmaUp: snprintf(out, n, "EMA20 ตัดขึ้น EMA50 %s", tfLabel); break;
        case Cond::EmaDown: snprintf(out, n, "EMA20 ตัดลง EMA50 %s", tfLabel); break;
        case Cond::ChochUp: snprintf(out, n, "CHoCH ขาขึ้น %s", tfLabel); break;
        case Cond::ChochDown: snprintf(out, n, "CHoCH ขาลง %s", tfLabel); break;
        default: snprintf(out, n, "%s %.6g", above ? "ขึ้นถึง" : "ลงถึง", level); break;
    }
}

}  // namespace ialert
