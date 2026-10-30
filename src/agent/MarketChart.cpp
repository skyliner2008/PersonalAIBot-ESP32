/**
 * @file MarketChart.cpp
 * @brief market_chart (T142): candle history -> trend, EMA, RSI, MACD, ATR, Bollinger, support / resistance,
 *        the last candle, volume, correlation with other symbols; a candlestick card on the screen
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Ported from the PersonalAIBot app's candle tools (trading_technical_analysis / correlation_matrix): the
 * indicators are computed here from the bars (util/MarketMath.h), not read from TradingView's scanner the
 * way market_analysis does - so the levels and the trend come from the real history.
 */

#include <math.h>

#include "MarketHistory.h"
#include "WebTools.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr uint16_t kUp = 0x2FE8, kDown = 0xF9E7;
constexpr int kCorrBars = 100;  // returns compared for the correlation
constexpr int kMaxCompare = 4;

/** 6 significant digits: enough for any price, short in the JSON the model reads. */
double sig6(double v) {
    if (!isfinite(v) || v == 0) return v;
    const double mag = pow(10.0, 5 - (int)floor(log10(fabs(v))));
    return round(v * mag) / mag;
}

double pct2(double v) { return round(v * 100) / 100; }

void fmtPrice(char* out, size_t n, double v) {
    const double a = fabs(v);
    if (a >= 1000) {  // 78,003.04
        const long long cents = llround(a * 100), whole = cents / 100;
        char digits[24], grouped[32];
        snprintf(digits, sizeof(digits), "%lld", whole);
        const int len = strlen(digits);
        int g = 0;
        for (int i = 0; i < len; i++) {
            if (i && (len - i) % 3 == 0) grouped[g++] = ',';
            grouped[g++] = digits[i];
        }
        grouped[g] = '\0';
        snprintf(out, n, "%s%s.%02d", v < 0 ? "-" : "", grouped, (int)(cents % 100));
    } else if (a >= 10) {
        snprintf(out, n, "%.2f", v);
    } else if (a >= 1) {
        snprintf(out, n, "%.4f", v);
    } else {
        snprintf(out, n, "%.5g", v);
    }
}

/** One symbol the model named: its EXCHANGE:SYMBOL candidates (alias / ticker on SET, NASDAQ, NYSE) and bars. */
struct Wanted {
    String token;
    String cands[3];
    int nc = 0;
    const char* alias = nullptr;
    mkt::Bars bars[3];
    // the outcome
    bool ok = false;
    String used, name, err;
    BarsInfo info;
    mkt::Bars* chosen = nullptr;
};

/**
 * Every candidate of every symbol in one TradingView request, the first candidate with bars wins; Yahoo
 * for a symbol only when TradingView could not answer for it (not when it said the symbol does not exist).
 */
void loadAll(std::vector<Wanted>& want, const mkt::Timeframe& tf, uint16_t mainBars, uint16_t otherBars) {
    std::vector<TvJob> jobs;
    for (size_t w = 0; w < want.size(); w++) {
        Wanted& x = want[w];
        x.nc = tickerCandidates(x.token, x.cands, &x.alias);
        for (int i = 0; i < x.nc; i++) {
            TvJob j;
            j.symbol = x.cands[i];
            j.tf = &tf;
            j.bars = w == 0 ? mainBars : otherBars;
            j.out = &x.bars[i];
            jobs.push_back(j);
        }
    }
    String connErr;
    fetchTradingView(jobs, connErr);
    size_t k = 0;
    for (size_t w = 0; w < want.size(); w++) {
        Wanted& x = want[w];
        bool allUnknown = x.nc > 0;
        for (int i = 0; i < x.nc; i++, k++) {
            const TvJob& j = jobs[k];
            allUnknown = allUnknown && j.badSymbol;
            if (!x.ok && !x.bars[i].empty()) {
                x.ok = true;
                x.used = x.cands[i];
                x.info = j.info;
                x.chosen = &x.bars[i];
            }
            if (x.err.isEmpty() && j.err.length()) x.err = j.err;
        }
        if (connErr.length()) x.err = connErr;
        for (int i = 0; i < x.nc && !x.ok && !allUnknown; i++) {
            String yErr;
            if (fetchYahoo(x.cands[i], tf, w == 0 ? mainBars : otherBars, x.bars[i], x.info, yErr)) {
                x.ok = true;
                x.used = x.cands[i];
                x.chosen = &x.bars[i];
            } else if (i == x.nc - 1) {
                x.err += " / " + yErr;
            }
        }
        if (x.ok) {
            x.name = x.alias ? String(x.alias) : (x.info.name.length() ? x.info.name : x.used);
        } else if (allUnknown || x.err.isEmpty()) {
            x.err = "ไม่พบสัญลักษณ์นี้";
        }
    }
}

const char* posTh(double c, double e) { return c >= e ? "ราคาเหนือ" : "ราคาใต้"; }

}  // namespace

void runMarketChart(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const String symbol = a["symbol"] | "";
    const mkt::Timeframe* tf = mkt::timeframe(a["timeframe"] | "1d");
    if (symbol.isEmpty()) {
        setError(out, "ต้องระบุ symbol");
        return;
    }
    if (!tf) {
        setError(out, "timeframe ต้องเป็น 5m 15m 1h 4h 1d หรือ 1w");
        return;
    }
    // the main symbol + up to 4 to compare, all in one request
    std::vector<Wanted> want(1);
    want[0].token = symbol;
    const String list = a["compare"] | "";
    for (int from = 0; from < (int)list.length() && (int)want.size() <= kMaxCompare;) {
        int comma = list.indexOf(',', from);
        if (comma < 0) comma = list.length();
        String tok = list.substring(from, comma);
        tok.trim();
        from = comma + 1;
        if (tok.isEmpty()) continue;
        want.emplace_back();
        want.back().token = tok;
    }
    loadAll(want, *tf, tf->bars, kCorrBars + 40);
    if (!want[0].ok) {
        setError(out, ("ดึงแท่งเทียนไม่ได้: " + want[0].err).c_str());
        return;
    }
    const mkt::Bars& bars = *want[0].chosen;
    const BarsInfo& info = want[0].info;
    const String& used = want[0].used;
    const String& name = want[0].name;
    if (bars.size() < 30) {
        setError(out, "ประวัติราคาสั้นเกินไปสำหรับการวิเคราะห์");
        return;
    }
    setStage("market_chart: compute");
    const size_t n = bars.size();
    const mkt::Bar& last = bars.back();
    const mkt::Series e20 = mkt::ema(bars, 20), e50 = mkt::ema(bars, 50), e200 = mkt::ema(bars, 200);
    const double rsi = mkt::rsi(bars, 14), atr = mkt::atr(bars, 14);
    const mkt::Macd macd = mkt::macd(bars);
    const mkt::Bollinger bb = mkt::bollinger(bars);
    const mkt::Levels lv = mkt::levels(bars, atr);
    const mkt::Trend tr = mkt::trend(last.c, e20[n - 1], e50[n - 1], e200[n - 1], n > 10 ? e50[n - 11] : NAN);
    const char* candle = mkt::candleTh(bars);
    const double avgVol = mkt::avgVolume(bars);
    const int window = n < (size_t)CardData::Chart::kMax ? (int)n : CardData::Chart::kMax;
    double wHi = -INFINITY, wLo = INFINITY;
    for (size_t i = n - window; i < n; i++) {
        wHi = bars[i].h > wHi ? bars[i].h : wHi;
        wLo = bars[i].l < wLo ? bars[i].l : wLo;
    }
    const double prev = bars[n - 2].c, first = bars[n - window].c;

    JsonObject r = out["result"].to<JsonObject>();
    r["symbol"] = used;
    r["name"] = name;
    if (info.currency.length()) r["currency"] = info.currency;
    r["timeframe"] = tf->label;
    r["source"] = info.source;
    if (info.delayMin) r["delayed_min"] = info.delayMin;
    // daily / weekly bars carry the session's opening time ("09:55"): only the date means something there
    const bool intraday = strcmp(tf->key, "1d") && strcmp(tf->key, "1w");
    auto stamp = [&](uint32_t t) -> String {
        char d[20];
        if (intraday || !timefmt::formatDate(t, TIME_UTC_OFFSET_SEC, d, sizeof(d))) return thaiDateTime(t);
        return String(d);
    };
    r["last_bar"] = stamp(last.t);
    r["price"] = sig6(last.c);
    r["change_pct"] = pct2((last.c / prev - 1) * 100);
    JsonObject w = r["window"].to<JsonObject>();
    w["bars"] = window;
    w["from"] = stamp(bars[n - window].t);
    w["change_pct"] = pct2((last.c / first - 1) * 100);
    w["high"] = sig6(wHi);
    w["low"] = sig6(wLo);
    r["trend"] = mkt::trendTh(tr);
    JsonObject em = r["ema"].to<JsonObject>();
    em["ema20"] = sig6(e20[n - 1]);
    em["ema50"] = sig6(e50[n - 1]);
    if (!isnan(e200[n - 1])) em["ema200"] = sig6(e200[n - 1]);
    em["price_vs_ema50"] = posTh(last.c, e50[n - 1]);
    if (!isnan(e200[n - 1])) em["price_vs_ema200"] = posTh(last.c, e200[n - 1]);
    if (!isnan(rsi)) r["rsi14"] = round(rsi * 10) / 10;
    if (!isnan(macd.hist)) {
        JsonObject m = r["macd"].to<JsonObject>();
        m["histogram"] = sig6(macd.hist);
        m["above_signal"] = macd.hist >= 0;
        if (macd.barsSinceCross >= 0) m["crossed_bars_ago"] = macd.barsSinceCross;
    }
    if (!isnan(atr)) {
        r["atr14"] = sig6(atr);
        r["atr_pct"] = round(atr / last.c * 1000) / 10;
    }
    if (!isnan(bb.mid)) {
        JsonObject b = r["bollinger"].to<JsonObject>();
        b["upper"] = sig6(bb.upper);
        b["lower"] = sig6(bb.lower);
        b["width_pct"] = round(bb.widthPct * 100) / 100;
        b["position_pct"] = (int)lround(bb.percentB * 100);  // 0 = lower band, 100 = upper
        if (bb.squeeze) b["squeeze"] = "แบนด์บีบแคบสุดในรอบ 120 แท่ง: มักตามด้วยการเลือกทางแรง";
    }
    JsonArray sup = r["support"].to<JsonArray>(), res = r["resistance"].to<JsonArray>();
    for (int k = 0; k < 2; k++) {
        if (lv.support[k].touches) {
            JsonObject o = sup.add<JsonObject>();
            o["price"] = sig6(lv.support[k].price);
            o["touches"] = lv.support[k].touches;
        }
        if (lv.resistance[k].touches) {
            JsonObject o = res.add<JsonObject>();
            o["price"] = sig6(lv.resistance[k].price);
            o["touches"] = lv.resistance[k].touches;
        }
    }
    if (avgVol > 0) r["volume_vs_avg20"] = round(last.v / avgVol * 100) / 100;
    if (*candle) r["last_candle"] = candle;

    // ---- correlation with the symbols in `compare` ----
    int compared = 0;
    if (want.size() > 1) {
        JsonArray co = r["correlation"].to<JsonArray>();
        for (size_t w = 1; w < want.size(); w++) {
            const Wanted& x = want[w];
            JsonObject o = co.add<JsonObject>();
            o["symbol"] = x.token;
            if (!x.ok) {
                o["error"] = x.err;
                continue;
            }
            size_t usedN = 0;
            const double c = mkt::correlation(bars, *x.chosen, kCorrBars, usedN, intraday ? 0 : 86400,
                                              TIME_UTC_OFFSET_SEC);
            const String& oName = x.name;
            o["symbol"] = x.used;
            o["name"] = oName;
            if (!isnan(c)) o["r"] = round(c * 100) / 100;
            o["meaning"] = mkt::correlationTh(c);
            o["bars"] = (int)usedN;
            if (card.rows < CardData::kMaxRows) {
                CardData::Row& row = card.row[card.rows++];
                char buf[16];
                snprintf(buf, sizeof(buf), isnan(c) ? "-" : "%+.2f", c);
                copyStr(row.extra, sizeof(row.extra), buf);
                snprintf(row.name, sizeof(row.name), "%s • %s", oName.c_str(), mkt::correlationTh(c));
                row.color = isnan(c) ? 0x9CD3 : (c >= 0.4 ? kUp : (c <= -0.4 ? kDown : 0xFFFF));
            }
            compared++;
        }
    }

    // ---- the card: candles, or the correlation table when that was asked ----
    char price[24];
    fmtPrice(price, sizeof(price), last.c);
    if (compared) {
        card.kind = CardData::Table;
        copyStr(card.title, sizeof(card.title), "ความสัมพันธ์ของราคา");
        snprintf(card.subtitle, sizeof(card.subtitle), "เทียบกับ %s • %s %d แท่ง", name.c_str(), tf->label, kCorrBars);
        r["note"] = "จอแสดงตารางความสัมพันธ์ (r: 1 = ขึ้นลงไปทางเดียวกัน, -1 = สวนทางกัน, ใกล้ 0 = ไม่เกี่ยวกัน) "
                    "เล่าว่าแต่ละตัวสัมพันธ์กับตัวหลักแค่ไหนเป็นภาษาพูด แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ดูกราฟตัวใดตัวหนึ่ง "
                    "เทียบกรอบเวลาอื่น) ห้ามปิดด้วยคำเตือนการลงทุน";
        return;
    } else {
        card.kind = CardData::Candles;
        snprintf(card.title, sizeof(card.title), "%s • %s", name.c_str(), tf->label);
        char sub[96];
        snprintf(sub, sizeof(sub), "%s", mkt::trendTh(tr));
        if (!isnan(rsi)) snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), " • RSI %.0f", rsi);
        snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), " • %s", info.source);
        if (info.delayMin) snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), " (ช้า %d นาที)", info.delayMin);
        copyStr(card.subtitle, sizeof(card.subtitle), sub);
        snprintf(card.big, sizeof(card.big), "%s  %+.2f%%", price, (last.c / prev - 1) * 100);
        CardData::Chart& k = card.k;
        k.n = window;
        for (int i = 0; i < window; i++) {
            const size_t j = n - window + i;
            k.o[i] = bars[j].o;
            k.h[i] = bars[j].h;
            k.l[i] = bars[j].l;
            k.c[i] = bars[j].c;
            k.ema20[i] = e20[j];
            k.ema50[i] = e50[j];
        }
        for (int s = 0; s < 2; s++) {
            k.support[s] = lv.support[s].touches ? lv.support[s].price : NAN;
            k.resistance[s] = lv.resistance[s].touches ? lv.resistance[s].price : NAN;
        }
        k.from = bars[n - window].t;
        k.to = last.t;
        k.intraday = intraday;
    }
    r["note"] = "จอแสดงกราฟแท่งเทียนประกอบ (เส้นฟ้า EMA20 เส้นส้ม EMA50 เส้นประเขียว = แนวรับ แดง = แนวต้าน) "
                "เล่าแนวโน้ม ราคาเทียบ EMA แนวรับ/แนวต้านที่ใกล้ที่สุด RSI/MACD และสิ่งที่น่าสนใจ (แท่งล่าสุด ปริมาณ "
                "แบนด์บีบ) เป็นภาษาพูดพร้อมตัวเลขสำคัญ แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ตั้งแจ้งเตือนที่แนวรับ/ต้าน "
                "ดูกรอบเวลาอื่น เทียบตัวอื่น) ห้ามปิดด้วยคำเตือนการลงทุน";
}

}  // namespace webtools
