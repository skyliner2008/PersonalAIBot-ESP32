/**
 * @file SmcMath.h
 * @brief Smart Money Concepts from candles: market structure (BOS / CHoCH + inducement), fair value gaps,
 *        order blocks, liquidity (equal highs / lows + swings, 0-5 stars), sweeps, attack force, premium /
 *        discount and the OB / FVG "walls" with a confluence score per side (T143)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * A port of the user's own indicator "SMC & Multi-TF Order Blocks Sweeps V8.3 / V11.29" as the mobile app
 * runs it (composeApp automation/smc: MarketStructure, FvgDetection, OrderBlocks, LiquidityZones,
 * SweepDetection, SmcWalls, SmcEngine) - one timeframe, the same defaults. Pure C++ (host tested).
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>

#include "MarketMath.h"

namespace smc {

using mkt::Bar;
using mkt::Bars;

enum class Event : uint8_t { None, CHoCH, SMS, BMS };  // SMS = the first break in the same direction, BMS = more
enum class Dir : int8_t { Bearish = -1, Init = 0, Bullish = 1 };
enum class Zone : uint8_t { Discount, Equilibrium, Premium };
enum class Liq : uint8_t { None, Sweep, Run };  // the last bar at a liquidity level: pierce + reclaim / close beyond

struct Swing {
    int index;
    double price;
};

struct Fvg {
    bool bull;
    double top, bottom;
    int bar;
    bool mitigated = false;
};

struct OrderBlock {
    bool bull;
    double top, bottom;
    int bar;
    int breakBar;  // the bar that broke the structure (mitigation counts after it)
    bool mitigated = false, invalidated = false;
};

struct LiquidityZone {
    double price;
    int bar;
    bool high;            // above the price (equal highs / swing high)
    bool equal;           // equal highs / lows (else a swing point)
    int strength;         // touches, the anchor bar included
    int stars = 0;        // confluence 0-5
};

struct Structure {
    Dir dir = Dir::Init;
    double high = 0, low = 0;
    int highBar = 0, lowBar = 0;
    Event last = Event::None;
    int lastSide = 0;     // +1 up, -1 down, 0 none
    int lastBar = -1;     // the bar of the last event
    int streak = 0;       // breaks in the same direction since the last CHoCH
    int chochBar = -1;    // the last CHoCH (a later BOS does not hide it; indicator alerts, T143)
    int chochSide = 0;
};

struct Wall {
    bool ob;              // order block (else FVG)
    bool bull;
    double top, bottom;
    int bar;
    int confluence;       // 1-5
};

struct Side {
    int score = 0;        // 0-10
    int stars = 0, obs = 0, fvgs = 0;
    bool overlap = false, near = false;
    Liq event = Liq::None;
};

struct AttackForce {
    int bar;
    bool bull;
    double atrRatio, volumeRatio;
};

struct Snapshot {
    bool valid = false;   // >= 30 bars
    Structure st;
    Zone zone = Zone::Equilibrium;
    double zonePct = 50, equilibrium = 0;
    PsVector<Fvg> fvgs;            // active (not filled), the last 30 bars
    PsVector<OrderBlock> bullObs;  // newest first, invalidated ones dropped
    PsVector<OrderBlock> bearObs;
    PsVector<LiquidityZone> liquidity;  // most stars first
    PsVector<Wall> walls;          // highest confluence first
    Side bull, bear;
    bool sweepBull = false, sweepBear = false;  // the last bar swept the nearest opposite candle and reclaimed it
    PsVector<AttackForce> attack;  // the last 5 bars
    double atr = 0;
};

// ---------------------------------------------------------------- helpers (as the app's SmcUtils)

inline double bodyMax(const Bar& c) { return c.o > c.c ? c.o : c.c; }
inline double bodyMin(const Bar& c) { return c.o < c.c ? c.o : c.c; }
inline bool bearish(const Bar& c) { return c.c < c.o; }
inline bool bullish(const Bar& c) { return c.c > c.o; }

/** Wilder ATR; fewer than period + 1 bars: the mean range. */
inline double atrOf(const Bars& b, int period = 14) {
    if (b.empty()) return 0;
    if ((int)b.size() < period + 1) {
        double s = 0;
        for (const Bar& x : b) s += x.h - x.l;
        return s / b.size();
    }
    return mkt::atr(b, period);
}

/** Pivot highs / lows confirmed by `len` bars on each side (strict: an equal neighbour cancels it). */
inline void swings(const Bars& b, int len, PsVector<Swing>& highs, PsVector<Swing>& lows) {
    highs.clear();
    lows.clear();
    for (int i = len; i + len < (int)b.size(); i++) {
        bool hi = true, lo = true;
        for (int j = i - len; j <= i + len; j++) {
            if (j == i) continue;
            if (b[j].h >= b[i].h) hi = false;
            if (b[j].l <= b[i].l) lo = false;
        }
        if (hi) highs.push_back({i, b[i].h});
        if (lo) lows.push_back({i, b[i].l});
    }
}

// ---------------------------------------------------------------- market structure

/** BOS / CHoCH on closes; a CHoCH needs the inducement (3-bar swing on the other side) swept first. */
inline Structure structure(const Bars& b, int idmLen = 3) {
    Structure s;
    const int n = (int)b.size();
    if (n < 30) return s;
    s.high = b[0].h;
    s.low = b[0].l;
    PsVector<Swing> ih, il;
    swings(b, idmLen, ih, il);
    PsVector<int8_t> mark(n, 0);  // 1 = idm high here, 2 = idm low, 3 = both
    for (const Swing& w : ih) mark[w.index] |= 1;
    for (const Swing& w : il) mark[w.index] |= 2;
    double idmHigh = NAN, idmLow = NAN;
    bool highSwept = false, lowSwept = false;
    for (int i = 1; i < n; i++) {
        const Bar& c = b[i];
        if (mark[i] & 1) idmHigh = c.h, highSwept = false;
        if (mark[i] & 2) idmLow = c.l, lowSwept = false;
        if (!highSwept && !isnan(idmHigh)) {
            if ((c.h > idmHigh && c.c < idmHigh) || (b[i - 1].h > idmHigh && b[i - 1].c < idmHigh)) highSwept = true;
        }
        if (!lowSwept && !isnan(idmLow)) {
            if ((c.l < idmLow && c.c > idmLow) || (b[i - 1].l < idmLow && b[i - 1].c > idmLow)) lowSwept = true;
        }
        // the extremes since the opposite structure point
        const int lbHigh = std::max(1, std::min(i - s.lowBar, i)), lbLow = std::max(1, std::min(i - s.highBar, i));
        double hiR = s.high, loR = s.low;
        int hiIdx = s.highBar, loIdx = s.lowBar;
        for (int j = std::max(0, i - lbHigh); j <= i; j++) {
            if (b[j].h > hiR) hiR = b[j].h, hiIdx = j;
        }
        for (int j = std::max(0, i - lbLow); j <= i; j++) {
            if (b[j].l < loR) loR = b[j].l, loIdx = j;
        }
        const bool lowBroken = c.c < s.low && i > s.lowBar;
        const bool highBroken = c.c > s.high && i > s.highBar;
        auto event = [&](bool reversal) {
            if (reversal) {
                s.last = Event::CHoCH;
                s.streak = 0;
                s.chochBar = i;
            } else {
                s.streak++;
                s.last = s.streak == 1 ? Event::SMS : Event::BMS;
            }
            s.lastBar = i;
        };
        if (lowBroken) {
            const bool reversal = s.dir != Dir::Bearish;
            if (!reversal || highSwept) {
                event(reversal);
                s.lastSide = -1;
                if (reversal) s.chochSide = -1;
                s.dir = Dir::Bearish;
                s.highBar = hiIdx;
                s.high = hiR;
                s.lowBar = i;
                s.low = c.l;
            }
        } else if (highBroken) {
            const bool reversal = s.dir != Dir::Bullish;
            if (!reversal || lowSwept) {
                event(reversal);
                s.lastSide = 1;
                if (reversal) s.chochSide = 1;
                s.dir = Dir::Bullish;
                s.lowBar = loIdx;
                s.low = loR;
                s.highBar = i;
                s.high = c.h;
            }
        } else if (s.dir == Dir::Bullish) {
            if (c.h > s.high) s.high = c.h, s.highBar = i;
        } else if (s.dir == Dir::Bearish) {
            if (c.l < s.low) s.low = c.l, s.lowBar = i;
        } else {
            if (c.h > s.high) s.high = c.h, s.highBar = i;
            if (c.l < s.low) s.low = c.l, s.lowBar = i;
        }
    }
    return s;
}

// ---------------------------------------------------------------- fair value gaps

/** Gaps between bar i-2 and bar i larger than a quarter of ATR(200), the last `lookback` bars. */
inline void fvgs(const Bars& b, int lookback, PsVector<Fvg>& out) {
    out.clear();
    const int n = (int)b.size();
    if (n < 3) return;
    const double minGap = atrOf(b, std::max(1, std::min(200, n - 1))) * 0.25;
    for (int i = std::max(2, n - lookback); i < n; i++) {
        const Bar &c0 = b[i], &c1 = b[i - 1], &c2 = b[i - 2];
        if (c0.l > c2.h && c1.c > c2.h && c0.l - c2.h > minGap) out.push_back({true, c0.l, c2.h, i});
        if (c0.h < c2.l && c1.c < c2.l && c2.l - c0.h > minGap) out.push_back({false, c2.l, c0.h, i});
    }
    for (Fvg& f : out) {
        for (int j = f.bar + 1; j < n; j++) {
            if ((f.bull && b[j].l <= f.bottom) || (!f.bull && b[j].h >= f.top)) {
                f.mitigated = true;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- order blocks

/**
 * A broken swing high -> the first bearish bar after the lowest point before the break (bullish OB), and the
 * mirror; it needs an FVG within 5 bars (displacement) and is not made against the structure's direction.
 */
inline void orderBlocks(const Bars& b, const PsVector<Fvg>& allFvgs, Dir dir, PsVector<OrderBlock>& bull,
                        PsVector<OrderBlock>& bear, int swingLen = 5, int maxCount = 30) {
    bull.clear();
    bear.clear();
    const int n = (int)b.size();
    if (n < 20) return;
    PsVector<Swing> highs, lows;
    swings(b, swingLen, highs, lows);
    auto hasFvg = [&](bool up, int at) {
        for (const Fvg& f : allFvgs) {
            if (f.bull == up && abs(f.bar - at) <= 5) return true;
        }
        return false;
    };
    for (int pass = 0; pass < 2; pass++) {
        const bool up = pass == 0;
        if (dir == (up ? Dir::Bearish : Dir::Bullish)) continue;  // trend filter
        for (const Swing& sw : up ? highs : lows) {
            int brk = -1;
            for (int k = sw.index + 1; k < n; k++) {
                if (up ? b[k].c > sw.price : b[k].c < sw.price) {
                    brk = k;
                    break;
                }
            }
            if (brk < 0) continue;
            double ext = up ? bodyMin(b[sw.index]) : bodyMax(b[sw.index]);
            int extIdx = sw.index;
            for (int k = sw.index; k < brk; k++) {
                const double v = up ? bodyMin(b[k]) : bodyMax(b[k]);
                if (up ? v <= ext : v >= ext) ext = v, extIdx = k;
            }
            int ob = -1;
            for (int k = extIdx; k <= std::min(brk + 5, n - 1); k++) {
                if (up ? bearish(b[k]) : bullish(b[k])) {
                    ob = k;
                    break;
                }
            }
            if (ob < 0 || !hasFvg(up, ob)) continue;
            PsVector<OrderBlock>& list = up ? bull : bear;
            list.insert(list.begin(), OrderBlock{up, bodyMax(b[ob]), bodyMin(b[ob]), ob, brk});
        }
    }
    const Bar& last = b[n - 1];
    for (OrderBlock& o : bull) {
        for (int j = std::max(o.bar, o.breakBar) + 1; j < n; j++) {
            if (b[j].l <= o.top && b[j].h >= o.bottom) {
                o.mitigated = true;
                break;
            }
        }
        o.invalidated = bodyMin(last) < o.bottom;
    }
    for (OrderBlock& o : bear) {
        for (int j = std::max(o.bar, o.breakBar) + 1; j < n; j++) {
            if (b[j].h >= o.bottom && b[j].l <= o.top) {
                o.mitigated = true;
                break;
            }
        }
        o.invalidated = bodyMax(last) > o.top;
    }
    for (PsVector<OrderBlock>* list : {&bull, &bear}) {
        PsVector<OrderBlock> keep;
        for (const OrderBlock& o : *list) {
            if (!o.invalidated && (int)keep.size() < maxCount) keep.push_back(o);
        }
        list->swap(keep);
    }
}

// ---------------------------------------------------------------- liquidity

/** Equal highs / lows (0.1 %) among the last `lookback` bars + the last 5 swing points of `swingLen`. */
inline void liquidity(const Bars& b, PsVector<LiquidityZone>& out, int lookback = 10, double equalPct = 0.1,
                      int swingLen = 10) {
    out.clear();
    const int n = (int)b.size();
    if (n < 20) return;
    for (int pass = 0; pass < 2; pass++) {
        const bool high = pass == 0;
        for (int i = 1; i <= std::min(lookback, n - 1); i++) {
            const double v = high ? b[n - 1 - i].h : b[n - 1 - i].l, thr = v * equalPct / 100;
            int eq = 0;
            for (int j = i + 1; j <= std::min(i + lookback, n - 1); j++) {
                if (fabs((high ? b[n - 1 - j].h : b[n - 1 - j].l) - v) <= thr) eq++;
            }
            bool dup = false;
            for (const LiquidityZone& z : out) dup = dup || (z.high == high && fabs(z.price - v) <= thr);
            if (eq >= 1 && !dup) out.push_back({v, n - 1 - i, high, true, eq + 1});
        }
    }
    PsVector<Swing> highs, lows;
    swings(b, swingLen, highs, lows);
    for (int pass = 0; pass < 2; pass++) {
        const bool high = pass == 0;
        const PsVector<Swing>& list = high ? highs : lows;
        for (size_t k = list.size() > 5 ? list.size() - 5 : 0; k < list.size(); k++) {
            bool dup = false;
            for (const LiquidityZone& z : out) dup = dup || (z.high == high && fabs(z.price - list[k].price) < list[k].price * 0.001);
            if (!dup) out.push_back({list[k].price, list[k].index, high, false, 2});
        }
    }
    // pending only while the last bar has not taken it
    const Bar& last = b[n - 1];
    PsVector<LiquidityZone> keep;
    for (const LiquidityZone& z : out) {
        if (z.high ? last.h > z.price : last.l < z.price) continue;
        keep.push_back(z);
    }
    out.swap(keep);
}

/** 1 + an opposite OB on it (+2) + the structure high / low within half an ATR (+1) + premium / discount (+1)
 *  + the trend toward it (+1), at most 5. */
inline int stars(const LiquidityZone& z, const PsVector<OrderBlock>& bull, const PsVector<OrderBlock>& bear,
                 const Structure& st, Zone pd, double atrValue) {
    int s = 1;
    for (const OrderBlock& o : z.high ? bear : bull) {
        if (z.price <= o.top * 1.001 && z.price >= o.bottom * 0.999) {
            s += 2;
            break;
        }
    }
    if (fabs(z.price - st.high) < atrValue * 0.5 || fabs(z.price - st.low) < atrValue * 0.5) s++;
    if ((z.high && pd == Zone::Premium) || (!z.high && pd == Zone::Discount)) s++;
    if ((z.high && st.dir == Dir::Bearish) || (!z.high && st.dir == Dir::Bullish)) s++;
    return s < 5 ? s : 5;
}

// ---------------------------------------------------------------- the last bar

/** The last bar pierced the body of the nearest bearish (bull sweep) / bullish (bear sweep) bar by > 0.3 ATR
 *  of wick and closed back over half of it. */
inline void sweeps(const Bars& b, bool& bull, bool& bear) {
    bull = bear = false;
    const int n = (int)b.size();
    if (n < 10) return;
    const Bar& last = b[n - 1];
    const double a = atrOf(b);
    int bi = -1, si = -1;  // nearest bearish / bullish bar before the last
    for (int i = n - 2; i >= 0 && (bi < 0 || si < 0); i--) {
        if (bi < 0 && bearish(b[i])) bi = i;
        if (si < 0 && bullish(b[i])) si = i;
    }
    if (bi >= 0) {
        const double top = bodyMax(b[bi]), btm = bodyMin(b[bi]), w = fabs(top - btm);
        bull = w > 0 && last.l < btm && last.c > btm + 0.5 * w && btm - last.l > 0.3 * a;
    }
    if (si >= 0) {
        const double top = bodyMax(b[si]), btm = bodyMin(b[si]), w = fabs(top - btm);
        bear = w > 0 && last.h > top && last.c < top - 0.5 * w && last.h - top > 0.3 * a;
    }
}

/** Bars among the last 5 with a body > 2 ATR and > 2x the 10-bar average body, on 1.5x volume (if any). */
inline void attackForce(const Bars& b, PsVector<AttackForce>& out) {
    out.clear();
    const int n = (int)b.size();
    if (n < 11) return;
    const double a = atrOf(b);
    for (int i = std::max(10, n - 5); i < n; i++) {
        const double body = fabs(b[i].c - b[i].o);
        double bs = 0, vs = 0;
        for (int j = i - 10; j < i; j++) bs += fabs(b[j].c - b[j].o), vs += b[j].v;
        const double avgBody = bs / 10, avgVol = vs / 10;
        const bool volOk = avgVol <= 0 || b[i].v > avgVol * 1.5;
        if (body > avgBody * 2 && volOk && body > a * 2) {
            out.push_back({i, bullish(b[i]), a > 0 ? body / a : 0, avgVol > 0 ? b[i].v / avgVol : 0});
        }
    }
}

/** The last bar at the nearest liquidity below (first) / above: pierced by > 0.1 ATR and closed back = sweep,
 *  closed beyond = run. */
inline void liquidityEvents(const Bars& b, const PsVector<LiquidityZone>& zones, double atrValue, Liq& below,
                            Liq& above) {
    below = above = Liq::None;
    if (b.size() < 2 || zones.empty()) return;
    const Bar& c = b.back();
    const double tol = std::max(atrValue * 0.10, (c.h - c.l) * 0.05);
    const LiquidityZone *hi = nullptr, *lo = nullptr;
    for (const LiquidityZone& z : zones) {
        if (z.high && (!hi || fabs(z.price - c.h) < fabs(hi->price - c.h))) hi = &z;
        if (!z.high && (!lo || fabs(z.price - c.l) < fabs(lo->price - c.l))) lo = &z;
    }
    if (lo && c.l < lo->price - tol) below = c.c > lo->price ? Liq::Sweep : Liq::Run;
    if (hi && c.h > hi->price + tol) above = c.c < hi->price ? Liq::Sweep : Liq::Run;
}

// ---------------------------------------------------------------- walls + confluence

inline void walls(const Bars& b, const Snapshot& s, double price, PsVector<Wall>& out, Side& bull, Side& bear) {
    out.clear();
    if (b.empty()) return;
    const double tol = std::max(price * 0.001, s.atr * 0.25);
    for (const OrderBlock& o : s.bullObs) {
        if (!o.mitigated) out.push_back({true, true, o.top, o.bottom, o.bar, 0});
    }
    for (const OrderBlock& o : s.bearObs) {
        if (!o.mitigated) out.push_back({true, false, o.top, o.bottom, o.bar, 0});
    }
    for (const Fvg& f : s.fvgs) out.push_back({false, f.bull, f.top, f.bottom, f.bar, 0});
    auto overlaps = [&](const Wall& a, const Wall& c) {
        return std::min(a.top, c.top) + tol >= std::max(a.bottom, c.bottom);
    };
    for (Wall& w : out) {
        int ov = 0;
        for (const Wall& o : out) ov += (&o != &w && o.bull == w.bull && o.ob != w.ob && overlaps(w, o));
        int liq = 0;
        const double mid = (w.top + w.bottom) / 2;
        for (const LiquidityZone& z : s.liquidity) {
            if (z.high != w.bull && fabs(z.price - mid) <= tol && z.stars > liq) liq = z.stars;
        }
        w.confluence = std::min(5, 1 + std::min(ov, 2) + liq);
    }
    const int last = (int)b.size() - 1;
    std::stable_sort(out.begin(), out.end(), [&](const Wall& a, const Wall& c) {
        if (a.confluence != c.confluence) return a.confluence > c.confluence;
        return last - a.bar < last - c.bar;  // fresher first
    });
    Liq below, above;
    liquidityEvents(b, s.liquidity, s.atr, below, above);
    for (int pass = 0; pass < 2; pass++) {
        const bool up = pass == 0;
        Side& sd = up ? bull : bear;
        sd = Side();
        double near = INFINITY;
        int overlapCount = 0;
        for (const Wall& w : out) {
            if (w.bull != up) continue;
            (w.ob ? sd.obs : sd.fvgs)++;
            const double d = price < w.bottom ? w.bottom - price : (price > w.top ? price - w.top : 0);
            near = d < near ? d : near;
            bool ov = false;
            for (const Wall& o : out) ov = ov || (&o != &w && o.bull == up && o.ob != w.ob && overlaps(w, o));
            overlapCount += ov;
        }
        for (const LiquidityZone& z : s.liquidity) {
            if (z.high != up && z.stars > sd.stars) sd.stars = z.stars;
        }
        sd.overlap = overlapCount > 0;
        sd.near = near <= tol;
        sd.event = up ? below : above;
        // the app's Kotlin `if (near) 1 else 0 + if (event) 2 else 0` drops the event's 2 points when the price
        // is at a wall (precedence); both count here, as the indicator means
        sd.score = std::min(10, (sd.obs + sd.fvgs ? 2 : 0) + std::min(3, sd.stars) + std::min(overlapCount, 2) +
                                    std::min(2, sd.fvgs) + (sd.near ? 1 : 0) + (sd.event != Liq::None ? 2 : 0));
    }
}

// ---------------------------------------------------------------- everything

inline Snapshot analyze(const Bars& b) {
    Snapshot s;
    const int n = (int)b.size();
    if (n < 30) return s;
    s.valid = true;
    s.atr = atrOf(b);
    const double price = b.back().c;
    s.st = structure(b);
    PsVector<Fvg> recent, all;
    fvgs(b, 30, recent);
    for (const Fvg& f : recent) {
        if (!f.mitigated) s.fvgs.push_back(f);
    }
    // order blocks look for an FVG near them over the whole window (the last 30 bars only = no OB at all; the
    // app measured 0 vs 8 on 300 BTC bars)
    fvgs(b, n, all);
    orderBlocks(b, all, s.st.dir, s.bullObs, s.bearObs);
    if (s.st.high > 0 && s.st.low > 0 && s.st.high > s.st.low) {
        const double range = s.st.high - s.st.low;
        s.equilibrium = s.st.low + range / 2;
        s.zonePct = (price - s.st.low) / range * 100;
        s.zone = s.zonePct > 62 ? Zone::Premium : (s.zonePct < 38 ? Zone::Discount : Zone::Equilibrium);
    } else {
        s.equilibrium = price;
    }
    liquidity(b, s.liquidity);
    for (LiquidityZone& z : s.liquidity) z.stars = stars(z, s.bullObs, s.bearObs, s.st, s.zone, s.atr);
    std::stable_sort(s.liquidity.begin(), s.liquidity.end(),
                     [](const LiquidityZone& a, const LiquidityZone& c) { return a.stars > c.stars; });
    walls(b, s, price, s.walls, s.bull, s.bear);
    sweeps(b, s.sweepBull, s.sweepBear);
    attackForce(b, s.attack);
    return s;
}

inline const char* eventName(Event e) {
    switch (e) {
        case Event::CHoCH: return "CHoCH";
        case Event::SMS: return "BOS (SMS)";
        case Event::BMS: return "BOS (BMS)";
        default: return "";
    }
}

inline const char* zoneTh(Zone z) {
    return z == Zone::Premium ? "Premium (โซนแพง)" : (z == Zone::Discount ? "Discount (โซนถูก)" : "Equilibrium (กึ่งกลาง)");
}

}  // namespace smc
