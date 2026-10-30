/**
 * @file MarketViews.cpp
 * @brief market_chart view "smc" (Smart Money Concepts: structure, order blocks, FVG, liquidity, confluence)
 *        and view "signals" (five strategies with a regime-weighted vote, one or four timeframes) (T143)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * The math is util/SmcMath.h (the user's own indicator "SMC & Multi-TF Order Blocks Sweeps V8.3 / V11.29" as
 * the mobile app runs it) and util/MarketSignals.h (the app's StrategySignalProvider). The candles come
 * from the same TradingView websocket as the chart (MarketHistory.cpp).
 */

#include <math.h>

#include <algorithm>

#include "../util/MarketSignals.h"
#include "../util/SmcMath.h"
#include "MarketHistory.h"
#include "WebTools.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr uint16_t kUp = 0x2FE8, kDown = 0xF9E7, kDim = 0x9CD3;

bool intradayTf(const mkt::Timeframe& tf) { return strcmp(tf.key, "1d") && strcmp(tf.key, "1w"); }

String stampOf(const mkt::Timeframe& tf, uint32_t t) {
    char d[20];
    if (intradayTf(tf) || !timefmt::formatDate(t, TIME_UTC_OFFSET_SEC, d, sizeof(d))) return thaiDateTime(t);
    return String(d);
}

/** Symbol + timeframe -> bars (one TradingView request for every timeframe). False = error set. */
bool loadOne(const String& symbol, const mkt::Timeframe* const* tfs, int ntf, std::vector<Wanted>& want,
             JsonDocument& out) {
    if (symbol.isEmpty()) {
        setError(out, "ต้องระบุ symbol");
        return false;
    }
    want.assign(ntf, Wanted());
    for (int i = 0; i < ntf; i++) {
        want[i].token = symbol;
        want[i].tf = tfs[i];
        want[i].bars = tfs[i]->bars;
    }
    loadAll(want);
    for (const Wanted& w : want) {
        if (w.ok) return true;
    }
    setError(out, ("ดึงแท่งเทียนไม่ได้: " + want[0].err).c_str());
    return false;
}

void header(JsonObject r, const Wanted& w) {
    r["symbol"] = w.used;
    r["name"] = w.name;
    r["source"] = w.info.source;
    if (w.info.delayMin) r["delayed_min"] = w.info.delayMin;
}

void fillCandles(CardData::Chart& k, const mkt::Bars& b, bool intraday) {
    const size_t n = b.size();
    const int window = n < (size_t)CardData::Chart::kMax ? (int)n : CardData::Chart::kMax;
    k.n = window;
    for (int i = 0; i < window; i++) {
        const mkt::Bar& x = b[n - window + i];
        k.o[i] = x.o;
        k.h[i] = x.h;
        k.l[i] = x.l;
        k.c[i] = x.c;
        k.ema20[i] = k.ema50[i] = NAN;
    }
    k.from = b[n - window].t;
    k.to = b.back().t;
    k.intraday = intraday;
}

}  // namespace

// ============================================================ view "smc"

void runSmcView(JsonObjectConst a, JsonDocument& out, CardData& card) {
    // the indicator's main timeframe is 15 minutes (V11.29): the default when none is named
    const mkt::Timeframe* tf = mkt::timeframe(a["timeframe"] | "15m");
    if (!tf) {
        setError(out, "timeframe ต้องเป็น 5m 15m 1h 4h 1d หรือ 1w");
        return;
    }
    std::vector<Wanted> want;
    if (!loadOne(a["symbol"] | "", &tf, 1, want, out)) return;
    const mkt::Bars& b = *want[0].chosen;
    if (b.size() < 60) {
        setError(out, "ประวัติราคาสั้นเกินไปสำหรับ SMC");
        return;
    }
    setStage("market_chart: smc");
    const smc::Snapshot s = smc::analyze(b);
    const int n = (int)b.size(), last = n - 1;
    const double price = b.back().c;

    JsonObject r = out["result"].to<JsonObject>();
    header(r, want[0]);
    r["timeframe"] = tf->label;
    r["last_bar"] = stampOf(*tf, b.back().t);
    r["price"] = sig6(price);

    JsonObject st = r["structure"].to<JsonObject>();
    st["direction"] = s.st.dir == smc::Dir::Bullish ? "ขาขึ้น (bullish)"
                      : s.st.dir == smc::Dir::Bearish ? "ขาลง (bearish)" : "ยังไม่ชัด (ยังไม่เกิด CHoCH)";
    if (s.st.last != smc::Event::None) {
        st["last_event"] = smc::eventName(s.st.last);
        st["event_direction"] = s.st.lastSide > 0 ? "ทะลุขึ้น" : "หลุดลง";
        st["event_bars_ago"] = last - s.st.lastBar;
        if (s.st.streak > 0) st["bos_in_a_row"] = s.st.streak;
    }
    st["structure_high"] = sig6(s.st.high);
    st["structure_low"] = sig6(s.st.low);

    JsonObject pd = r["premium_discount"].to<JsonObject>();
    pd["zone"] = smc::zoneTh(s.zone);
    pd["position_pct"] = (int)lround(s.zonePct);  // 0 = structure low, 100 = high
    pd["equilibrium"] = sig6(s.equilibrium);
    // outside the range: the close broke it, but the indicator does not count a break against the structure
    // before the opposite inducement is swept (or no CHoCH happened yet)
    if (s.zonePct < 0) pd["note"] = "ราคาหลุดใต้ structure low แล้ว แต่ยังไม่นับเป็น BOS/CHoCH (ยังไม่กวาด inducement ฝั่งตรงข้าม)";
    if (s.zonePct > 100) pd["note"] = "ราคาทะลุเหนือ structure high แล้ว แต่ยังไม่นับเป็น BOS/CHoCH (ยังไม่กวาด inducement ฝั่งตรงข้าม)";

    // order blocks: one per bar (several broken swings can lead to the same bar), nearest to the price first
    auto distance = [&](double top, double bottom) {
        return price < bottom ? bottom - price : (price > top ? price - top : 0.0);
    };
    const smc::OrderBlock* nearest[2] = {nullptr, nullptr};
    JsonObject ob = r["order_blocks"].to<JsonObject>();
    for (int side = 0; side < 2; side++) {
        const PsVector<smc::OrderBlock>& list = side == 0 ? s.bullObs : s.bearObs;
        std::vector<const smc::OrderBlock*> uniq;
        for (const smc::OrderBlock& o : list) {
            bool dup = false;
            for (const smc::OrderBlock* u : uniq) dup = dup || u->bar == o.bar;
            if (!dup) uniq.push_back(&o);
        }
        std::sort(uniq.begin(), uniq.end(), [&](const smc::OrderBlock* x, const smc::OrderBlock* y) {
            return distance(x->top, x->bottom) < distance(y->top, y->bottom);
        });
        JsonArray arr = ob[side == 0 ? "demand" : "supply"].to<JsonArray>();
        for (size_t i = 0; i < uniq.size() && i < 3; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["top"] = sig6(uniq[i]->top);
            o["bottom"] = sig6(uniq[i]->bottom);
            o["bars_ago"] = last - uniq[i]->bar;
            o["tested"] = uniq[i]->mitigated;  // the price came back to it after the break
        }
        if (!uniq.empty()) nearest[side] = uniq[0];
    }

    std::vector<const smc::Fvg*> gaps;
    for (const smc::Fvg& f : s.fvgs) gaps.push_back(&f);
    std::sort(gaps.begin(), gaps.end(), [&](const smc::Fvg* x, const smc::Fvg* y) {
        return distance(x->top, x->bottom) < distance(y->top, y->bottom);
    });
    JsonArray fv = r["fvg"].to<JsonArray>();
    const smc::Fvg* nearGap[2] = {nullptr, nullptr};
    for (const smc::Fvg* f : gaps) {
        if (fv.size() < 4) {
            JsonObject o = fv.add<JsonObject>();
            o["side"] = f->bull ? "ขึ้น (bullish)" : "ลง (bearish)";
            o["top"] = sig6(f->top);
            o["bottom"] = sig6(f->bottom);
            o["bars_ago"] = last - f->bar;
        }
        if (!nearGap[f->bull ? 0 : 1]) nearGap[f->bull ? 0 : 1] = f;
    }

    JsonArray liq = r["liquidity"].to<JsonArray>();
    for (const smc::LiquidityZone& z : s.liquidity) {
        if (liq.size() >= 5) break;
        JsonObject o = liq.add<JsonObject>();
        o["price"] = sig6(z.price);
        o["side"] = z.high ? "เหนือราคา (buy-side)" : "ใต้ราคา (sell-side)";
        o["type"] = z.equal ? (z.high ? "equal highs" : "equal lows") : "swing";
        o["touches"] = z.strength;
        o["stars"] = z.stars;
    }

    auto sideJson = [&](JsonObject o, const smc::Side& sd) {
        o["score"] = sd.score;  // of 10
        String why;
        auto add = [&](const String& t) { why += (why.length() ? ", " : "") + t; };
        if (sd.obs) add("OB " + String(sd.obs));
        if (sd.fvgs) add("FVG " + String(sd.fvgs));
        if (sd.stars) add("liquidity " + String(sd.stars) + " ดาว");
        if (sd.overlap) add("OB ซ้อน FVG");
        if (sd.near) add("ราคาอยู่ที่โซน");
        if (sd.event == smc::Liq::Sweep) add("แท่งล่าสุด sweep liquidity แล้วดึงกลับ");
        if (sd.event == smc::Liq::Run) add("แท่งล่าสุดปิดทะลุ liquidity");
        o["reasons"] = why;
    };
    JsonObject cf = r["confluence"].to<JsonObject>();
    sideJson(cf["bullish"].to<JsonObject>(), s.bull);
    sideJson(cf["bearish"].to<JsonObject>(), s.bear);
    if (s.sweepBull) r["last_bar_sweep"] = "แท่งล่าสุดกวาดใต้แท่งแดงก่อนหน้าแล้วปิดกลับขึ้น (bullish sweep)";
    if (s.sweepBear) r["last_bar_sweep"] = "แท่งล่าสุดกวาดเหนือแท่งเขียวก่อนหน้าแล้วปิดกลับลง (bearish sweep)";
    if (!s.attack.empty()) {
        const smc::AttackForce& f = s.attack.back();
        char buf[120];
        snprintf(buf, sizeof(buf), "แท่งโมเมนตัมแรง%s %d แท่งก่อน (ตัวแท่ง %.1f เท่า ATR)", f.bull ? "ขาขึ้น" : "ขาลง",
                 last - f.bar, f.atrRatio);
        r["attack_force"] = buf;
    }

    // ---- the card: candles + the nearest order blocks / gaps as boxes, the structure high / low as lines ----
    card.kind = CardData::Candles;
    snprintf(card.title, sizeof(card.title), "%s • SMC %s", want[0].name.c_str(), tf->label);
    char sub[96], where[40];
    if (s.zonePct < 0) copyStr(where, sizeof(where), "ใต้ structure low");
    else if (s.zonePct > 100) copyStr(where, sizeof(where), "เหนือ structure high");
    else snprintf(where, sizeof(where), "%s %d%%", s.zone == smc::Zone::Premium ? "Premium" : s.zone == smc::Zone::Discount ? "Discount" : "EQ",
                  (int)lround(s.zonePct));
    snprintf(sub, sizeof(sub), "%s%s%s • %s",
             s.st.dir == smc::Dir::Bullish ? "โครงสร้างขาขึ้น" : s.st.dir == smc::Dir::Bearish ? "โครงสร้างขาลง" : "ยังไม่ชัด",
             s.st.last != smc::Event::None ? " • " : "", s.st.last == smc::Event::CHoCH ? "CHoCH" : (s.st.last != smc::Event::None ? "BOS" : ""),
             where);
    copyStr(card.subtitle, sizeof(card.subtitle), sub);
    char px[24];
    fmtPrice(px, sizeof(px), price);
    snprintf(card.big, sizeof(card.big), "%s  %+.2f%%", px, (price / b[n - 2].c - 1) * 100);
    CardData::Chart& k = card.k;
    fillCandles(k, b, intradayTf(*tf));
    k.smc = true;
    const int base = n - k.n;
    int z = 0;
    auto box = [&](double top, double bottom, int bar, bool bull, bool isOb) {
        if (z >= 4) return;
        CardData::Chart::Zone& q = k.zone[z++];
        q.top = top;
        q.bottom = bottom;
        q.from = bar > base ? bar - base : 0;
        q.bull = bull;
        q.ob = isOb;
    };
    if (nearest[0]) box(nearest[0]->top, nearest[0]->bottom, nearest[0]->bar, true, true);
    if (nearest[1]) box(nearest[1]->top, nearest[1]->bottom, nearest[1]->bar, false, true);
    if (nearGap[0]) box(nearGap[0]->top, nearGap[0]->bottom, nearGap[0]->bar, true, false);
    if (nearGap[1]) box(nearGap[1]->top, nearGap[1]->bottom, nearGap[1]->bar, false, false);
    k.resistance[0] = s.st.high;
    k.support[0] = s.st.low;

    r["note"] = "จอแสดงกราฟ SMC (กล่องเขียว = demand OB/FVG ขาขึ้น, แดง = supply, เส้นประ = structure high/low) "
                "เล่าเป็นภาษาพูด: โครงสร้าง (BOS/CHoCH ล่าสุด) ราคาอยู่โซน premium/discount, OB/FVG ที่ใกล้ราคา "
                "liquidity ที่น่าจะถูกกวาด และฝั่งที่ confluence สูงกว่า พร้อมตัวเลขสำคัญ แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง "
                "(ตั้งแจ้งเตือนที่โซน / CHoCH, ดูกรอบเวลาอื่น) ห้ามปิดด้วยคำเตือนการลงทุน";
}

// ============================================================ view "signals"

namespace {

const char* strategyTh(const char* key) {
    if (!strcmp(key, "MOM")) return "โมเมนตัม 20 แท่ง";
    if (!strcmp(key, "TR")) return "ตามเทรนด์ EMA50/200";
    if (!strcmp(key, "REV")) return "กลับตัว RSI+Bollinger";
    if (!strcmp(key, "DC")) return "เบรก Donchian 20";
    return "ใกล้จุดสูงสุดของช่วง";
}

const char* voteTh(mkt::Vote v) { return v == mkt::Vote::Buy ? "ซื้อ" : (v == mkt::Vote::Sell ? "ขาย" : "ไม่มีสัญญาณ"); }

uint16_t consensusColor(int c) { return c > 0 ? kUp : (c < 0 ? kDown : 0xFFFF); }

}  // namespace

void runSignalsView(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const char* tfKey = a["timeframe"] | "";
    const mkt::Timeframe* tfs[4];
    int ntf = 0;
    if (*tfKey) {
        tfs[0] = mkt::timeframe(tfKey);
        if (!tfs[0]) {
            setError(out, "timeframe ต้องเป็น 5m 15m 1h 4h 1d หรือ 1w");
            return;
        }
        ntf = 1;
    } else {  // no timeframe: 15m, 1h, 4h and 1d side by side
        for (const char* k : {"15m", "1h", "4h", "1d"}) tfs[ntf++] = mkt::timeframe(k);
    }
    std::vector<Wanted> want;
    if (!loadOne(a["symbol"] | "", tfs, ntf, want, out)) return;
    setStage("market_chart: signals");
    JsonObject r = out["result"].to<JsonObject>();
    const Wanted* any = nullptr;
    for (const Wanted& w : want) any = any ? any : (w.ok ? &w : nullptr);
    header(r, *any);
    r["price"] = sig6(any->chosen->back().c);

    card.kind = CardData::Table;
    if (ntf == 1) {
        const mkt::Bars& b = *want[0].chosen;
        const mkt::Signals g = mkt::signals(b);
        if (!g.valid) {
            setError(out, "ประวัติราคาสั้นเกินไปสำหรับสัญญาณ (ต้อง 60 แท่งขึ้นไป)");
            return;
        }
        r["timeframe"] = tfs[0]->label;
        r["last_bar"] = stampOf(*tfs[0], b.back().t);
        r["consensus"] = mkt::consensusTh(g.consensus);
        r["weighted_score"] = round(g.score * 100) / 100;
        r["market_regime"] = mkt::regimeTh(g.regime);
        if (!isnan(g.adxValue)) r["adx14"] = round(g.adxValue * 10) / 10;
        JsonArray arr = r["strategies"].to<JsonArray>();
        for (const mkt::Strategy& s : g.s) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = strategyTh(s.key);
            o["signal"] = voteTh(s.raw);
            if (s.raw != mkt::Vote::None) o["confirmed_by_candle"] = s.vote != mkt::Vote::None;
            o["weight"] = s.weight;
            if (!strcmp(s.key, "MOM")) o["roc_pct"] = pct2(s.value);
            if (!strcmp(s.key, "REV") && !isnan(s.value)) o["rsi14"] = round(s.value * 10) / 10;
            if (!strcmp(s.key, "52H")) o["pct_of_period_high"] = round(s.value * 10) / 10;
            if (card.rows < CardData::kMaxRows) {
                CardData::Row& row = card.row[card.rows++];
                copyStr(row.name, sizeof(row.name), strategyTh(s.key));
                if (s.raw == mkt::Vote::None) {
                    copyStr(row.extra, sizeof(row.extra), "-");
                    row.color = kDim;
                } else if (s.vote == mkt::Vote::None) {  // the state says so, the last candle does not back it
                    snprintf(row.extra, sizeof(row.extra), "รอ:%s", voteTh(s.raw));
                    row.color = kDim;
                } else {
                    copyStr(row.extra, sizeof(row.extra), voteTh(s.vote));
                    row.color = s.vote == mkt::Vote::Buy ? kUp : kDown;
                }
            }
        }
        snprintf(card.title, sizeof(card.title), "สัญญาณกลยุทธ์ %s • %s", any->name.c_str(), tfs[0]->label);
        snprintf(card.subtitle, sizeof(card.subtitle), "รวม: %s (%+.1f) • %s", mkt::consensusTh(g.consensus), g.score,
                 mkt::regimeTh(g.regime));
    } else {
        JsonArray arr = r["timeframes"].to<JsonArray>();
        int total = 0, counted = 0;
        for (int i = 0; i < ntf; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["timeframe"] = tfs[i]->label;
            const mkt::Signals g = want[i].ok ? mkt::signals(*want[i].chosen) : mkt::Signals();
            if (!g.valid) {
                o["error"] = want[i].ok ? String("ประวัติสั้นเกินไป") : want[i].err;
                continue;
            }
            o["consensus"] = mkt::consensusTh(g.consensus);
            o["weighted_score"] = round(g.score * 100) / 100;
            o["buy_votes"] = g.buys;
            o["sell_votes"] = g.sells;
            o["market_regime"] = mkt::regimeTh(g.regime);
            String which;
            for (const mkt::Strategy& s : g.s) {
                if (s.vote == mkt::Vote::None) continue;
                which += (which.length() ? ", " : "") + String(strategyTh(s.key)) + "=" + voteTh(s.vote);
            }
            if (which.length()) o["votes"] = which;
            total += g.consensus;
            counted++;
            if (card.rows < CardData::kMaxRows) {
                CardData::Row& row = card.row[card.rows++];
                snprintf(row.name, sizeof(row.name), "%s • %s", tfs[i]->label, mkt::regimeTh(g.regime));
                snprintf(row.extra, sizeof(row.extra), "%s %+.1f", mkt::consensusTh(g.consensus), g.score);
                row.color = consensusColor(g.consensus);
            }
        }
        if (!counted) {
            setError(out, "คำนวณสัญญาณไม่ได้ทุกกรอบเวลา");
            return;
        }
        // agreement across timeframes: the sum of the consensus levels (-2..+2 each)
        r["overall"] = total >= 3 ? "ซื้อ (หลายกรอบเวลาตรงกัน)" : total <= -3 ? "ขาย (หลายกรอบเวลาตรงกัน)"
                       : total > 0 ? "เอียงซื้อ" : total < 0 ? "เอียงขาย" : "ไม่ชัด / ขัดกัน";
        snprintf(card.title, sizeof(card.title), "สัญญาณกลยุทธ์ %s", any->name.c_str());
        copyStr(card.subtitle, sizeof(card.subtitle), r["overall"] | "");
    }
    r["note"] = "จอแสดงตารางสัญญาณ (โหวตจาก 5 กลยุทธ์ ถ่วงน้ำหนักตามสภาพตลาด; นับเฉพาะเมื่อแท่งล่าสุดยืนยัน) "
                "เล่าผลรวมและกลยุทธ์ที่เห็นตรงกันเป็นภาษาพูด แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ดู SMC / กราฟ, "
                "ตั้งแจ้งเตือน) ห้ามปิดด้วยคำเตือนการลงทุน";
}

}  // namespace webtools
