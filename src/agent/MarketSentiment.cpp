/**
 * @file MarketSentiment.cpp
 * @brief market_sentiment (T143): stocks = a fear & greed score from the CNN components (S&P 500 momentum,
 *        VIX, put/call, safe haven, junk bonds) computed here; crypto = Fear & Greed (alternative.me), the
 *        whole market (CoinGecko) and Binance futures long / short; economy = US / Thai inflation, rates,
 *        jobs, GDP (TradingView ECONOMICS: / FRED: series)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Ported from the PersonalAIBot app's sentiment / FRED / crypto-overview tools. CNN's own index is not
 * used: its API answers a device with "you're a bot" (418) and only serves a browser. FRED's CSV stalls any
 * client that is not curl; the same series come from TradingView over the chart websocket instead.
 */

#include <math.h>

#include "../util/MarketSignals.h"
#include "MarketHistory.h"
#include "WebTools.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr uint16_t kUp = 0x2FE8, kDown = 0xF9E7, kWarn = 0xFD20;

uint16_t moodColor(double v) { return isnan(v) ? 0x9CD3 : (v < 45 ? kDown : (v > 55 ? kUp : kWarn)); }

/** Several GETs to one host over one TLS connection (keep-alive): a handshake costs ~1 s here. */
int getMany(const String& host, const char* const* paths, int n, String* bodies) {
    if (WiFi.status() != WL_CONNECTED) return -10;
    WiFiClientSecure tls;
    tls.setCACert(WEB_ROOT_CA_PEM);
    tls.setHandshakeTimeout(kTlsHandshakeSec);
    HTTPClient http;
    http.setReuse(true);
    http.setTimeout(kHttpTimeoutMs);
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setUserAgent("Mozilla/5.0 (ESP32-AI personal assistant)");
    int ok = 0;
    const uint32_t t0 = millis();
    for (int i = 0; i < n; i++) {
        bodies[i] = "";
        if (!http.begin(tls, "https://" + host + paths[i])) continue;
        const int code = http.GET();
        if (code == 200) {
            bodies[i] = http.getString();
            ok++;
        }
        http.end();
    }
    Serial.printf("[Web] %s x%d -> %d ok in %lu ms\n", host.c_str(), n, ok, (unsigned long)(millis() - t0));
    return ok;
}

// ============================================================ stocks: fear & greed

void runStocks(JsonDocument& out, CardData& card) {
    static const mkt::Timeframe kDaily = {"1d", "1D", "1d", "2y", 420, "รายวัน"};
    // S&P 500, VIX, CBOE put/call, long treasuries, high yield, 7-10 year treasuries
    static const char* const kSyms[] = {"SP:SPX", "TVC:VIX", "USI:PCC", "NASDAQ:TLT", "AMEX:HYG", "NASDAQ:IEF"};
    std::vector<Wanted> want(6);
    for (int i = 0; i < 6; i++) {
        want[i].token = kSyms[i];
        want[i].tf = &kDaily;
        want[i].bars = kDaily.bars;
    }
    loadAll(want, true);
    if (!want[0].ok) {
        setError(out, ("ดึงข้อมูล S&P 500 ไม่ได้: " + want[0].err).c_str());
        return;
    }
    setStage("market_sentiment: stocks");
    static const mkt::Bars kNone;
    auto bars = [&](int i) -> const mkt::Bars& { return want[i].ok ? *want[i].chosen : kNone; };
    const mkt::FearGreed f = mkt::fearGreed(bars(0), bars(1), bars(2), bars(3), bars(4), bars(5));
    if (isnan(f.score)) {
        setError(out, "ข้อมูลย้อนหลังไม่พอสำหรับคำนวณดัชนี");
        return;
    }
    JsonObject r = out["result"].to<JsonObject>();
    r["market"] = "หุ้นสหรัฐ (S&P 500)";
    r["fear_greed"] = (int)lround(f.score);
    r["mood"] = mkt::fearGreedTh(f.score);
    r["method"] = "คำนวณบนบอร์ดแบบเดียวกับ CNN Fear & Greed: แต่ละองค์ประกอบ = ตำแหน่งของค่าวันนี้เทียบ 1 ปีที่ผ่านมา "
                  "(0 = กลัวสุด, 100 = โลภสุด) แล้วเฉลี่ย";
    JsonObject parts = r["components"].to<JsonObject>();
    struct Part {
        const char* key;
        const char* th;
        double v;
    } const p[] = {
        {"momentum_sp500_vs_125d", "โมเมนตัม S&P 500", f.momentum},
        {"volatility_vix_vs_50d", "ความผันผวน (VIX)", f.volatility},
        {"put_call_5d", "สัดส่วน Put/Call", f.putCall},
        {"safe_haven_stocks_vs_bonds", "หุ้นเทียบพันธบัตร", f.safeHaven},
        {"junk_bond_demand", "หุ้นกู้เสี่ยงสูง", f.junkBond},
    };
    card.kind = CardData::Bars;
    for (const Part& x : p) {
        if (isnan(x.v)) continue;
        parts[x.key] = (int)lround(x.v);
        if (card.rows < CardData::kMaxRows) {
            CardData::Row& row = card.row[card.rows++];
            copyStr(row.name, sizeof(row.name), x.th);
            row.value = x.v;
            snprintf(row.extra, sizeof(row.extra), "%d", (int)lround(x.v));
            row.color = moodColor(x.v);
        }
    }
    const mkt::Bars& spx = bars(0);
    r["sp500"] = sig6(spx.back().c);
    if (want[1].ok) r["vix"] = sig6(bars(1).back().c);
    if (want[2].ok) r["put_call"] = round(bars(2).back().c * 100) / 100;
    copyStr(card.title, sizeof(card.title), "ดัชนีกลัว/โลภ หุ้นสหรัฐ");
    snprintf(card.subtitle, sizeof(card.subtitle), "%d • %s", (int)lround(f.score), mkt::fearGreedTh(f.score));
    card.g.ref[0] = 50;   // the neutral line
    card.g.ref[1] = 100;  // full scale
    r["note"] = "จอแสดงแถบองค์ประกอบ 0-100 (เส้นประ = 50 เป็นกลาง) เล่าค่ารวม อารมณ์ตลาด และองค์ประกอบที่ดึงขึ้น/ลง "
                "เป็นภาษาพูด แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ภาพรวมคริปโต / ข้อมูลเศรษฐกิจ / ดูกราฟ S&P 500) "
                "ห้ามปิดด้วยคำเตือนการลงทุน";
}

// ============================================================ crypto

String coinPair(String s) {
    s.toUpperCase();
    const int colon = s.indexOf(':');
    if (colon >= 0) s = s.substring(colon + 1);
    for (const char* t : {".P", "PERP", "-USD", "/USDT", "USDT", "USDC", "USD"}) {
        if (s.endsWith(t)) {
            s = s.substring(0, s.length() - strlen(t));
            break;
        }
    }
    if (s == "BITCOIN" || s == "บิทคอยน์" || s.isEmpty()) s = "BTC";
    if (s == "ETHEREUM" || s == "อีเธอเรียม") s = "ETH";
    return s + "USDT";
}

String usdBig(double v) {
    char b[24];
    if (v >= 1e12) snprintf(b, sizeof(b), "$%.2fT", v / 1e12);
    else if (v >= 1e9) snprintf(b, sizeof(b), "$%.1fB", v / 1e9);
    else snprintf(b, sizeof(b), "$%.0f", v);
    return String(b);
}

void runCrypto(JsonObjectConst a, JsonDocument& out, CardData& card) {
    JsonObject r = out["result"].to<JsonObject>();
    card.kind = CardData::Table;
    int parts = 0;
    auto row = [&](const char* name, const String& extra, uint16_t color) {
        if (card.rows >= CardData::kMaxRows) return;
        CardData::Row& x = card.row[card.rows++];
        copyStr(x.name, sizeof(x.name), name);
        copyStr(x.extra, sizeof(x.extra), extra.c_str());
        x.color = color;
    };
    int fgNow = -1;
    {  // Fear & Greed (alternative.me): today, yesterday, a week ago
        String body;
        JsonDocument d(JsonPsramAllocator::instance());
        if (httpGet("https://api.alternative.me/fng/?limit=8&format=json", body) == 200 && !deserializeJson(d, body) &&
            d["data"].size()) {
            JsonArrayConst data = d["data"];
            fgNow = atoi(data[0]["value"] | "0");
            JsonObject fg = r["fear_greed"].to<JsonObject>();
            fg["value"] = fgNow;
            fg["mood"] = mkt::fearGreedTh(fgNow);
            if (data.size() > 1) fg["yesterday"] = atoi(data[1]["value"] | "0");
            if (data.size() > 7) fg["week_ago"] = atoi(data[7]["value"] | "0");
            row("Fear & Greed", String(fgNow) + (data.size() > 7 ? " (7 วันก่อน " + String(atoi(data[7]["value"] | "0")) + ")" : ""),
                moodColor(fgNow));
            parts++;
        }
    }
    {  // the whole market (CoinGecko /global)
        String body;
        JsonDocument filter, d(JsonPsramAllocator::instance());
        filter["data"]["total_market_cap"]["usd"] = true;
        filter["data"]["total_volume"]["usd"] = true;
        filter["data"]["market_cap_percentage"]["btc"] = true;
        filter["data"]["market_cap_percentage"]["eth"] = true;
        filter["data"]["market_cap_change_percentage_24h_usd"] = true;
        if (httpGet("https://api.coingecko.com/api/v3/global", body) == 200 &&
            !deserializeJson(d, body, DeserializationOption::Filter(filter))) {
            JsonObjectConst g = d["data"];
            const double mcap = g["total_market_cap"]["usd"] | 0.0, chg = g["market_cap_change_percentage_24h_usd"] | 0.0;
            JsonObject m = r["market"].to<JsonObject>();
            m["total_market_cap_usd"] = usdBig(mcap);
            m["change_24h_pct"] = pct2(chg);
            m["volume_24h_usd"] = usdBig(g["total_volume"]["usd"] | 0.0);
            m["btc_dominance_pct"] = round((g["market_cap_percentage"]["btc"] | 0.0) * 10) / 10;
            m["eth_dominance_pct"] = round((g["market_cap_percentage"]["eth"] | 0.0) * 10) / 10;
            char b[32];
            snprintf(b, sizeof(b), "%s %+.1f%%", usdBig(mcap).c_str(), chg);
            row("มูลค่าตลาดรวม", b, chg >= 0 ? kUp : kDown);
            snprintf(b, sizeof(b), "%.1f%%", g["market_cap_percentage"]["btc"] | 0.0);
            row("BTC dominance", b, 0xFFFF);
            parts++;
        }
    }
    // Binance futures positioning (1 h): retail accounts, top traders' positions, taker buy / sell
    const String pair = coinPair(a["symbol"] | "BTC");
    const String q = "?symbol=" + pair + "&period=1h&limit=1";
    const String p0 = "/futures/data/globalLongShortAccountRatio" + q, p1 = "/futures/data/topLongShortPositionRatio" + q,
                 p2 = "/futures/data/takerlongshortRatio" + q;
    const char* paths[] = {p0.c_str(), p1.c_str(), p2.c_str()};
    String bodies[3];
    getMany("fapi.binance.com", paths, 3, bodies);
    JsonDocument d0(JsonPsramAllocator::instance()), d1(JsonPsramAllocator::instance()), d2(JsonPsramAllocator::instance());
    const bool g0 = !deserializeJson(d0, bodies[0]) && d0.is<JsonArray>() && d0.size();
    const bool g1 = !deserializeJson(d1, bodies[1]) && d1.is<JsonArray>() && d1.size();
    const bool g2 = !deserializeJson(d2, bodies[2]) && d2.is<JsonArray>() && d2.size();
    if (g0) {
        JsonObject pos = r["binance_futures"].to<JsonObject>();
        pos["pair"] = pair;
        const double rl = atof(d0[0]["longAccount"] | "0.5") * 100, rr = atof(d0[0]["longShortRatio"] | "1");
        pos["retail_long_pct"] = round(rl * 10) / 10;
        pos["retail_long_short_ratio"] = pct2(rr);
        double tr = NAN, tk = NAN;
        if (g1) {
            pos["top_traders_long_pct"] = round(atof(d1[0]["longAccount"] | "0.5") * 1000) / 10;
            tr = atof(d1[0]["longShortRatio"] | "1");
            pos["top_traders_long_short_ratio"] = pct2(tr);
        }
        if (g2) {
            tk = atof(d2[0]["buySellRatio"] | "1");
            pos["taker_buy_sell_ratio"] = pct2(tk);
        }
        // the app's reading of the three ratios
        const char* sig = "สองฝั่งใกล้เคียงกัน";
        if (!isnan(tr) && rr < 0.95 && tr > 1.3) sig = "รายย่อย short หนักแต่รายใหญ่ถือ long: ลุ้น short squeeze (contrarian ขาขึ้น)";
        else if (!isnan(tr) && rr > 1.4 && tr < 0.9) sig = "รายย่อยไล่ long แต่รายใหญ่ short: เสี่ยง long squeeze (contrarian ขาลง)";
        else if (!isnan(tr) && rr > 1.2 && tr > 1.2) sig = "ทั้งรายย่อยและรายใหญ่ long ไปทางเดียวกัน";
        else if (!isnan(tr) && rr < 0.85 && tr < 0.85) sig = "ทั้งตลาดมองลงไปทางเดียวกัน";
        else if (!isnan(tk) && tk > 1.2) sig = "แรงซื้อเคาะขวานำตลาด";
        else if (!isnan(tk) && tk < 0.8) sig = "แรงขายเคาะซ้ายกดตลาด";
        pos["reading"] = sig;
        char b[32];
        snprintf(b, sizeof(b), "L %.0f%% (%.2f)", rl, rr);
        row("รายย่อย Long/Short", b, rr >= 1 ? kUp : kDown);
        if (!isnan(tr)) {
            snprintf(b, sizeof(b), "%.2f", tr);
            row("รายใหญ่ Long/Short", b, tr >= 1 ? kUp : kDown);
        }
        if (!isnan(tk)) {
            snprintf(b, sizeof(b), "%.2f", tk);
            row("แรงซื้อ/ขาย taker", b, tk >= 1 ? kUp : kDown);
        }
        parts++;
    } else {
        r["binance_futures"] = "ไม่มีสัญญา futures ของ " + pair + " หรือ Binance ไม่ตอบ";
    }
    if (!parts) {
        setError(out, "ดึงข้อมูล sentiment คริปโตไม่ได้");
        return;
    }
    copyStr(card.title, sizeof(card.title), "อารมณ์ตลาดคริปโต");
    snprintf(card.subtitle, sizeof(card.subtitle), "%s%s • %s", fgNow >= 0 ? "Fear & Greed " : "",
             fgNow >= 0 ? String(fgNow).c_str() : "", pair.c_str());
    r["note"] = "จอแสดงตารางอารมณ์ตลาดคริปโต เล่า Fear & Greed (เทียบเมื่อวาน/สัปดาห์ก่อน) มูลค่าตลาดรวม และสถานะ long/short "
                "ของ Binance เป็นภาษาพูด แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ดูกราฟ / SMC ของเหรียญ) ห้ามปิดด้วยคำเตือนการลงทุน";
}

// ============================================================ economy

struct Series {
    const char* alias;
    const char* symbol;
    const char* th;
    const char* unit;  // "%", "jobs"
};
const Series kEconomy[] = {
    {"us_inflation", "ECONOMICS:USIRYY", "เงินเฟ้อสหรัฐ", "%"},
    {"us_core_inflation", "ECONOMICS:USCIR", "เงินเฟ้อพื้นฐานสหรัฐ", "%"},
    {"fed_rate", "ECONOMICS:USINTR", "ดอกเบี้ย Fed", "%"},
    {"us_unemployment", "ECONOMICS:USUR", "ว่างงานสหรัฐ", "%"},
    {"us_nfp", "ECONOMICS:USNFP", "จ้างงานนอกภาคเกษตร (NFP)", "jobs"},
    {"us_gdp", "ECONOMICS:USGDPYY", "GDP สหรัฐ (YoY)", "%"},
    {"us10y", "TVC:US10Y", "บอนด์ยีลด์ 10 ปีสหรัฐ", "%"},
    {"th_inflation", "ECONOMICS:THIRYY", "เงินเฟ้อไทย", "%"},
    {"th_rate", "ECONOMICS:THINTR", "ดอกเบี้ยนโยบายไทย", "%"},
    {"th_gdp", "ECONOMICS:THGDPYY", "GDP ไทย (YoY)", "%"},
};
const mkt::Timeframe kMonthly = {"1M", "1M", "1mo", "5y", 24, "รายเดือน"};

String econValue(double v, const char* unit) {
    char b[24];
    if (!strcmp(unit, "jobs")) snprintf(b, sizeof(b), "%+.0fK", v / 1000);
    else snprintf(b, sizeof(b), "%.2f%%", v);
    return String(b);
}

String monthOf(uint32_t t) {
    static const char* const kM[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.", "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};
    const time_t tt = (time_t)t + 12 * 3600;  // a month's bar opens at 00:00 UTC on the 1st
    struct tm tm;
    gmtime_r(&tt, &tm);
    return String(kM[tm.tm_mon]) + " " + String((tm.tm_year + 1900 + 543) % 100);
}

void runEconomy(JsonObjectConst a, JsonDocument& out, CardData& card) {
    String sym = a["symbol"] | "";
    sym.trim();
    const Series* one = nullptr;
    static Series custom;
    static char customSym[48];
    if (sym.length()) {
        String low = sym;
        low.toLowerCase();
        for (const Series& s : kEconomy) {
            if (low == s.alias || sym.equalsIgnoreCase(s.symbol)) one = &s;
        }
        if (!one) {  // any TradingView series: "FRED:UNRATE", "ECONOMICS:USCCI", or a bare "USCCI"
            String full = sym;
            full.toUpperCase();
            if (full.indexOf(':') < 0) full = "ECONOMICS:" + full;
            copyStr(customSym, sizeof(customSym), full.c_str());
            custom = {"", customSym, "", ""};
            one = &custom;
        }
    }
    std::vector<Wanted> want;
    const int n = one ? 1 : (int)(sizeof(kEconomy) / sizeof(kEconomy[0]));
    want.resize(n);
    for (int i = 0; i < n; i++) {
        want[i].token = one ? one->symbol : kEconomy[i].symbol;
        want[i].tf = &kMonthly;
        want[i].bars = one ? 24 : 4;
    }
    loadAll(want, true);
    setStage("market_sentiment: economy");
    JsonObject r = out["result"].to<JsonObject>();
    r["source"] = "TradingView (ECONOMICS / FRED)";
    if (one) {
        const Wanted& w = want[0];
        if (!w.ok || w.chosen->empty()) {
            setError(out, ("ไม่พบชุดข้อมูลนี้: " + w.err).c_str());
            return;
        }
        const mkt::Bars& b = *w.chosen;
        const char* unit = one->unit[0] ? one->unit : "";
        r["series"] = w.used;
        r["name"] = one->th[0] ? String(one->th) : w.name;
        r["latest"] = sig6(b.back().c);
        r["period"] = monthOf(b.back().t);
        if (b.size() > 1) r["previous"] = sig6(b[b.size() - 2].c);
        JsonArray h = r["history"].to<JsonArray>();
        for (size_t i = b.size() > 12 ? b.size() - 12 : 0; i < b.size(); i++) {
            JsonObject o = h.add<JsonObject>();
            o["period"] = monthOf(b[i].t);
            o["value"] = sig6(b[i].c);
        }
        // the card: the series as a line
        card.kind = CardData::Graph;
        snprintf(card.title, sizeof(card.title), "%s", (one->th[0] ? String(one->th) : w.name).c_str());
        snprintf(card.subtitle, sizeof(card.subtitle), "ล่าสุด %s (%s)", *unit ? econValue(b.back().c, unit).c_str() : String(sig6(b.back().c)).c_str(),
                 monthOf(b.back().t).c_str());
        CardData::Series& g = card.g;
        g.n = b.size() < (size_t)CardData::Series::kMax ? b.size() : CardData::Series::kMax;
        const bool jobs = !strcmp(unit, "jobs");
        for (int i = 0; i < g.n; i++) g.v[i] = b[b.size() - g.n + i].c / (jobs ? 1000 : 1);
        auto mmYyyy = [](char* dst, size_t cap, uint32_t t) {  // the axis font is ASCII: "10/2024"
            const time_t tt = (time_t)t + 12 * 3600;
            struct tm tm;
            gmtime_r(&tt, &tm);
            snprintf(dst, cap, "%d/%d", tm.tm_mon + 1, tm.tm_year + 1900);
        };
        mmYyyy(g.from, sizeof(g.from), b[b.size() - g.n].t);
        mmYyyy(g.to, sizeof(g.to), b.back().t);
        copyStr(g.unit, sizeof(g.unit), jobs ? "K" : unit);
    } else {
        card.kind = CardData::Table;
        JsonArray arr = r["indicators"].to<JsonArray>();
        int got = 0;
        for (int i = 0; i < n; i++) {
            const Wanted& w = want[i];
            if (!w.ok || w.chosen->empty()) continue;
            const mkt::Bars& b = *w.chosen;
            const Series& s = kEconomy[i];
            JsonObject o = arr.add<JsonObject>();
            o["name"] = s.th;
            o["key"] = s.alias;
            o["value"] = econValue(b.back().c, s.unit);
            o["period"] = monthOf(b.back().t);
            if (b.size() > 1) o["previous"] = econValue(b[b.size() - 2].c, s.unit);
            got++;
            // the screen: 8 rows (no core inflation / Thai GDP there)
            if (!strcmp(s.alias, "us_core_inflation") || !strcmp(s.alias, "th_gdp") || card.rows >= CardData::kMaxRows) continue;
            CardData::Row& row = card.row[card.rows++];
            snprintf(row.name, sizeof(row.name), "%s • %s", s.th, monthOf(b.back().t).c_str());
            copyStr(row.extra, sizeof(row.extra), econValue(b.back().c, s.unit).c_str());
            const double d = b.size() > 1 ? b.back().c - b[b.size() - 2].c : 0;
            row.color = d > 0 ? kUp : (d < 0 ? kDown : 0xFFFF);
        }
        if (!got) {
            setError(out, "ดึงข้อมูลเศรษฐกิจไม่ได้");
            return;
        }
        copyStr(card.title, sizeof(card.title), "ตัวเลขเศรษฐกิจ สหรัฐ / ไทย");
        copyStr(card.subtitle, sizeof(card.subtitle), "ค่าล่าสุด • สีเขียว = สูงขึ้นจากครั้งก่อน");
    }
    r["note"] = "จอแสดงตัวเลขเศรษฐกิจ เล่าค่าล่าสุดเทียบครั้งก่อนเป็นภาษาพูด (เดือน/ไตรมาสที่เป็นข้อมูล) แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง "
                "(ดูย้อนหลังตัวใดตัวหนึ่ง / ปฏิทินข่าวเศรษฐกิจ) ห้ามปิดด้วยคำเตือนการลงทุน";
}

}  // namespace

void runMarketSentiment(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const String topic = a["topic"] | "";
    if (topic == "stocks") runStocks(out, card);
    else if (topic == "crypto") runCrypto(a, out, card);
    else if (topic == "economy") runEconomy(a, out, card);
    else setError(out, "topic ต้องเป็น stocks, crypto หรือ economy");
}

}  // namespace webtools
