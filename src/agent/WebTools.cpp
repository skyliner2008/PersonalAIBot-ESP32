/**
 * @file WebTools.cpp
 * @brief Weather / fuel / market / FX / news tools: HTTPS fetch, parse, card + model response
 */

#include "WebTools.h"

#include "WebToolsCommon.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>

#include "../ai/GoogleRootCA.h"
#include "../ui/CardScreen.h"
#include "../util/JsonAlloc.h"
#include "../util/Utf8.h"

namespace webtools {

namespace {

constexpr UBaseType_t kQueueDepth = 4;

const char* const kWebTools[] = {"get_weather",     "get_oil_price",   "get_market_quote",  "convert_currency",
                                 "get_news",        "calculate",       "convert_units",     "get_world_time",
                                 "get_location",    "search_knowledge", "market_overview",  "market_analysis",
                                 "market_scan",     "economic_calendar", "position_size",   "notes",
                                 "price_alert",     "mt5_account",     "mt5_trade"};

constexpr uint32_t kAlertCheckMs = 120000;

// ---------------------------------------------------------------- weather

// Each HTTPS round trip costs 2-3 s (TLS handshake), so places already looked up are remembered
struct GeoCache {
    String key, label;
    float lat, lon;
};
GeoCache gGeo[8];
uint8_t gGeoNext = 0;

bool geocodeFetch(const char* name, float& lat, float& lon, String& label) {
    String body;
    const String url = String("https://geocoding-api.open-meteo.com/v1/search?count=1&language=th&format=json&name=") +
                       urlEncode(name);
    if (httpGet(url, body) != 200) return false;
    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, body)) return false;
    JsonObjectConst r = doc["results"][0];
    if (r.isNull()) return false;
    lat = r["latitude"] | 0.0f;
    lon = r["longitude"] | 0.0f;
    label = (const char*)(r["name"] | name);
    const char* admin = r["admin1"] | "";
    if (*admin && !strstr(admin, label.c_str()) && !strstr(label.c_str(), admin)) label += String(" ") + admin;
    return true;
}

bool geocode(const char* name, float& lat, float& lon, String& label) {
    if (!name || !*name) return false;
    for (const GeoCache& g : gGeo) {
        if (g.key.length() && g.key == name) {
            lat = g.lat;
            lon = g.lon;
            label = g.label;
            return true;
        }
    }
    if (!geocodeFetch(name, lat, lon, label)) return false;
    GeoCache& g = gGeo[gGeoNext++ % 8];
    g.key = name;
    g.label = label;
    g.lat = lat;
    g.lon = lon;
    return true;
}

void runWeather(JsonObjectConst args, JsonDocument& out, CardData& card) {
    const char* loc = args["location"] | "";
    const char* locEn = args["location_en"] | "";
    float lat = 13.7540f, lon = 100.5014f;
    String label = "กรุงเทพมหานคร";
    if (*loc || *locEn) {
        if (!geocode(loc, lat, lon, label) && !geocode(locEn, lat, lon, label)) {
            setError(out, "ไม่พบสถานที่นี้ ลองระบุชื่อจังหวัด หรือส่ง location_en เป็นภาษาอังกฤษ");
            return;
        }
    } else {  // no place given: approximate location from the internet IP (Bangkok if that fails)
        float ilat, ilon;
        String ilabel;
        if (ipLocation(ilat, ilon, ilabel)) {
            lat = ilat;
            lon = ilon;
            label = ilabel.substring(0, ilabel.indexOf(','));
        }
    }
    char url[420];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,"
             "wind_speed_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&forecast_days=5&timezone=Asia%%2FBangkok",
             lat, lon);
    String body;
    const int code = httpGet(url, body);
    if (code != 200) {
        setError(out, "ดึงข้อมูลสภาพอากาศไม่สำเร็จ");
        return;
    }
    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, body)) {
        setError(out, "ข้อมูลสภาพอากาศอ่านไม่ได้");
        return;
    }
    JsonObjectConst cur = doc["current"];
    JsonObjectConst daily = doc["daily"];

    card.kind = CardData::Weather;
    snprintf(card.title, sizeof(card.title), "สภาพอากาศ %s", label.c_str());
    CardData::WeatherInfo& w = card.w;
    w.temp = cur["temperature_2m"] | 0.0f;
    w.feels = cur["apparent_temperature"] | w.temp;
    w.humidity = cur["relative_humidity_2m"] | 0;
    w.wind = cur["wind_speed_10m"] | 0.0f;
    w.rain = cur["precipitation"] | 0.0f;
    w.code = cur["weather_code"] | 0;
    w.isDay = (cur["is_day"] | 1) != 0;

    // PM2.5 (separate API; the card still works without it)
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=pm2_5,us_aqi",
             lat, lon);
    String aq;
    if (httpGet(url, aq) == 200) {
        JsonDocument a(JsonPsramAllocator::instance());
        if (!deserializeJson(a, aq)) {
            const float pm = a["current"]["pm2_5"] | -1.0f;
            w.pm25 = pm < 0 ? -1 : (int16_t)lroundf(pm);
            w.aqi = a["current"]["us_aqi"] | -1;
        }
    }

    static const char* const kDay[] = {"อา.", "จ.", "อ.", "พ.", "พฤ.", "ศ.", "ส."};
    JsonArrayConst times = daily["time"];
    w.days = 0;
    JsonObject res = out["result"].to<JsonObject>();
    res["location"] = label;
    JsonObject now = res["now"].to<JsonObject>();
    now["condition"] = wx::describe(w.code);
    now["temp_c"] = w.temp;
    now["feels_like_c"] = w.feels;
    now["humidity_pct"] = w.humidity;
    now["wind_kmh"] = w.wind;
    now["rain_mm"] = w.rain;
    if (w.pm25 >= 0) {
        res["pm25"] = w.pm25;
        res["us_aqi"] = w.aqi;
    }
    JsonArray fc = res["forecast"].to<JsonArray>();
    for (size_t i = 0; i < times.size() && i < 5; i++) {
        CardData::Day& d = w.day[w.days++];
        const char* date = times[i] | "";
        int y = 0, m = 0, dd = 0;
        sscanf(date, "%d-%d-%d", &y, &m, &dd);
        copyStr(d.label, sizeof(d.label), i == 0 ? "วันนี้" : (y ? kDay[weekday(y, m, dd)] : "-"));
        d.code = daily["weather_code"][i] | 0;
        d.tmax = daily["temperature_2m_max"][i] | 0.0f;
        d.tmin = daily["temperature_2m_min"][i] | 0.0f;
        d.pop = daily["precipitation_probability_max"][i] | -1;
        JsonObject f = fc.add<JsonObject>();
        f["date"] = date;
        f["condition"] = wx::describe(d.code);
        f["max_c"] = d.tmax;
        f["min_c"] = d.tmin;
        f["rain_chance_pct"] = d.pop;
    }
    const char* when = cur["time"] | "";  // "2026-10-15T02:30" (Asia/Bangkok)
    snprintf(card.subtitle, sizeof(card.subtitle), "ข้อมูลเวลา %s น. • Open-Meteo",
             strlen(when) >= 16 ? when + 11 : "-");
    res["note"] = "รายละเอียดแสดงบนจอแล้ว พูดสรุปสั้นๆ เป็นธรรมชาติ";
}

// ---------------------------------------------------------------- fuel (Bangchak)

void runOil(JsonDocument& out, CardData& card) {
    String body;
    if (httpGet("https://oil-price.bangchak.co.th/ApiOilPrice2/th", body) != 200) {
        setError(out, "ดึงราคาน้ำมันไม่สำเร็จ");
        return;
    }
    JsonDocument doc(JsonPsramAllocator::instance());
    if (deserializeJson(doc, body)) {
        setError(out, "ข้อมูลราคาน้ำมันอ่านไม่ได้");
        return;
    }
    JsonObjectConst root = doc[0];
    JsonDocument list(JsonPsramAllocator::instance());  // OilList is JSON inside a string
    if (deserializeJson(list, (const char*)(root["OilList"] | "[]"))) {
        setError(out, "ข้อมูลราคาน้ำมันอ่านไม่ได้");
        return;
    }
    card.kind = CardData::Oil;
    copyStr(card.title, sizeof(card.title), "ราคาน้ำมันวันนี้ (บางจาก)");
    copyStr(card.subtitle, sizeof(card.subtitle), root["OilRemark2"] | "");
    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "บางจาก (ราคาขายปลีก กทม.)";
    res["effective"] = root["OilRemark2"] | "";
    JsonArray items = res["prices_baht_per_litre"].to<JsonArray>();
    for (JsonObjectConst o : list.as<JsonArrayConst>()) {
        if (card.rows >= CardData::kMaxRows) break;
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), o["OilName"] | "");
        r.value = o["PriceToday"] | 0.0f;
        // PriceDifYesterday is not reliable (0.00 while yesterday 42.19 -> today 42.94): compute it
        const float yesterday = o["PriceYesterday"] | r.value;
        r.change = roundf((r.value - yesterday) * 100.0f) / 100.0f;
        r.next = o["PriceTomorrow"] | 0.0f;
        JsonObject it = items.add<JsonObject>();
        it["name"] = r.name;
        it["today"] = r.value;
        it["vs_yesterday"] = r.change;
        if (r.next > 0 && fabsf(r.next - r.value) > 0.001f) it["tomorrow"] = r.next;
    }
    res["note"] = "ตารางราคาแสดงบนจอแล้ว ตอบเฉพาะชนิดที่ผู้ใช้ถาม หรือสรุปการขึ้นลง";
}

// ---------------------------------------------------------------- TradingView

struct Alias {
    const char* key;
    const char* symbol;
    const char* name;
};
const Alias kAliases[] = {
    {"ทองคำ", "OANDA:XAUUSD", "ทองคำ (USD/ออนซ์)"}, {"ทอง", "OANDA:XAUUSD", "ทองคำ (USD/ออนซ์)"},
    {"gold", "OANDA:XAUUSD", "ทองคำ (USD/ออนซ์)"},  {"xauusd", "OANDA:XAUUSD", "ทองคำ (USD/ออนซ์)"},
    {"เงิน", "TVC:SILVER", "โลหะเงิน"},           {"silver", "TVC:SILVER", "โลหะเงิน"},
    {"บิทคอยน์", "BINANCE:BTCUSDT", "บิทคอยน์"},     {"bitcoin", "BINANCE:BTCUSDT", "บิทคอยน์"},
    {"btc", "BINANCE:BTCUSDT", "บิทคอยน์"},         {"อีเธอเรียม", "BINANCE:ETHUSDT", "อีเธอเรียม"},
    {"ethereum", "BINANCE:ETHUSDT", "อีเธอเรียม"},   {"eth", "BINANCE:ETHUSDT", "อีเธอเรียม"},
    {"ดัชนีหุ้นไทย", "SET:SET", "ดัชนี SET"},          {"หุ้นไทย", "SET:SET", "ดัชนี SET"},
    {"set", "SET:SET", "ดัชนี SET"},                  {"set50", "SET:SET50", "SET50"},
    {"ดอลลาร์", "FX_IDC:USDTHB", "ดอลลาร์/บาท"},      {"ค่าเงินบาท", "FX_IDC:USDTHB", "ดอลลาร์/บาท"},
    {"usdthb", "FX_IDC:USDTHB", "ดอลลาร์/บาท"},       {"น้ำมันดิบ", "NYMEX:CL1!", "น้ำมันดิบ WTI"},
    {"wti", "NYMEX:CL1!", "น้ำมันดิบ WTI"},             {"brent", "ICEEUR:BRN1!", "น้ำมันดิบเบรนท์"},
    {"s&p500", "SP:SPX", "S&P 500"},                   {"sp500", "SP:SPX", "S&P 500"},
    {"nasdaq", "NASDAQ:NDX", "Nasdaq 100"},            {"dow", "DJ:DJI", "Dow Jones"},
};

const Alias* findAlias(const String& s) {
    String low = s;
    low.toLowerCase();
    for (const Alias& a : kAliases) {
        if (low == a.key) return &a;
    }
    return nullptr;
}

constexpr char kFields[] =
    "description,close,change,change_abs,high,low,open,Perf.W,Perf.1M,Perf.3M,Perf.Y,RSI,Recommend.All,currency";

bool fetchQuote(const String& symbol, JsonDocument& q) {
    String body;
    const String url = String("https://scanner.tradingview.com/symbol?symbol=") + urlEncode(symbol.c_str()) +
                       "&fields=" + kFields;
    if (httpGet(url, body) != 200) return false;
    q.clear();
    return !deserializeJson(q, body) && !q["close"].isNull();
}

// one symbol (alias / EXCHANGE:SYM / bare ticker tried on SET, NASDAQ, NYSE) -> card row + JSON
bool quoteOne(String token, CardData::Row& row, JsonObject res) {
    token.trim();
    const Alias* a = findAlias(token);
    String candidates[4];
    int n = 0;
    if (a) {
        candidates[n++] = a->symbol;
    } else if (token.indexOf(':') > 0) {
        candidates[n++] = token;
    } else {
        token.toUpperCase();
        candidates[n++] = "SET:" + token;  // a Thai user most often means a Thai stock
        candidates[n++] = "NASDAQ:" + token;
        candidates[n++] = "NYSE:" + token;
    }
    JsonDocument q(JsonPsramAllocator::instance());
    for (int i = 0; i < n; i++) {
        if (!fetchQuote(candidates[i], q)) continue;
        // alias -> Thai name; a ticker the user said ("PTT") stays short; else TradingView's description
        const bool bareTicker = !a && token.indexOf(':') < 0;
        copyStr(row.name, sizeof(row.name),
                a ? a->name : bareTicker ? token.c_str() : (const char*)(q["description"] | candidates[i].c_str()));
        snprintf(row.extra, sizeof(row.extra), "%s %s", candidates[i].c_str(), (const char*)(q["currency"] | ""));
        row.value = q["close"] | 0.0f;
        row.change = q["change_abs"] | 0.0f;
        row.changePct = q["change"] | 0.0f;
        row.high = q["high"] | 0.0f;
        row.low = q["low"] | 0.0f;
        row.open = q["open"] | 0.0f;
        row.rsi = q["RSI"] | -1.0f;
        row.rating = q["Recommend.All"].isNull() ? -9.0f : (float)(q["Recommend.All"] | 0.0f);
        row.perf[0] = q["Perf.W"] | 0.0f;
        row.perf[1] = q["Perf.1M"] | 0.0f;
        row.perf[2] = q["Perf.3M"] | 0.0f;
        row.perf[3] = q["Perf.Y"] | 0.0f;
        row.hasPerf = !q["Perf.W"].isNull();
        res["symbol"] = candidates[i];
        res["name"] = row.name;
        res["price"] = row.value;
        res["currency"] = q["currency"] | "";
        res["change"] = row.change;
        res["change_pct"] = row.changePct;
        res["day_high"] = row.high;
        res["day_low"] = row.low;
        res["perf_1w_pct"] = row.perf[0];
        res["perf_1m_pct"] = row.perf[1];
        res["perf_3m_pct"] = row.perf[2];
        res["perf_1y_pct"] = row.perf[3];
        if (row.rsi >= 0) res["rsi"] = row.rsi;
        if (row.rating > -2) res["tv_rating"] = row.rating;  // -1 strong sell .. +1 strong buy
        return true;
    }
    res["symbol"] = token;
    res["error"] = "ไม่พบสัญลักษณ์นี้ใน TradingView";
    return false;
}

void runQuote(JsonObjectConst args, JsonDocument& out, CardData& card) {
    String list = args["symbols"] | "";
    if (list.isEmpty()) {
        setError(out, "ต้องระบุ symbols");
        return;
    }
    card.kind = CardData::Quotes;
    JsonArray arr = out["result"]["quotes"].to<JsonArray>();
    int start = 0;
    while (start < (int)list.length() && card.rows < 6) {
        int comma = list.indexOf(',', start);
        if (comma < 0) comma = list.length();
        const String token = list.substring(start, comma);
        start = comma + 1;
        if (token.length() == 0) continue;
        JsonObject res = arr.add<JsonObject>();
        if (quoteOne(token, card.row[card.rows], res)) card.rows++;
    }
    if (!card.rows) {
        card.kind = CardData::None;
        return;  // the per-symbol errors explain it
    }
    if (card.rows == 1) {
        copyStr(card.title, sizeof(card.title), card.row[0].name);
        copyStr(card.subtitle, sizeof(card.subtitle), "TradingView • ช่วงราคาวันนี้และผลตอบแทนย้อนหลัง");
    } else {
        copyStr(card.title, sizeof(card.title), "ราคาตลาด");
        copyStr(card.subtitle, sizeof(card.subtitle), "TradingView • เปลี่ยนแปลงวันนี้");
    }
    out["result"]["note"] = "กราฟแสดงบนจอแล้ว พูดสรุปราคาและการเปลี่ยนแปลงสั้นๆ ไม่ใช่คำแนะนำการลงทุน";
}

void runFx(JsonObjectConst args, JsonDocument& out, CardData& card) {
    const float amount = args["amount"] | 1.0f;
    String from = args["from"] | "USD", to = args["to"] | "THB";
    from.toUpperCase();
    to.toUpperCase();
    JsonDocument q(JsonPsramAllocator::instance());
    float rate = 0;
    String sym = "FX_IDC:" + from + to;
    if (fetchQuote(sym, q)) {
        rate = q["close"] | 0.0f;
    } else if (fetchQuote("FX_IDC:" + to + from, q)) {
        const float inv = q["close"] | 0.0f;
        rate = inv > 0 ? 1.0f / inv : 0;
        sym = "FX_IDC:" + to + from;
    }
    if (rate <= 0) {
        setError(out, "ไม่พบอัตราแลกเปลี่ยนคู่นี้");
        return;
    }
    const float result = amount * rate;
    JsonObject res = out["result"].to<JsonObject>();
    res["amount"] = amount;
    res["from"] = from;
    res["to"] = to;
    res["rate"] = rate;
    res["converted"] = result;

    card.kind = CardData::Quotes;
    card.rows = 1;
    CardData::Row& r = card.row[0];
    snprintf(r.name, sizeof(r.name), "%s เป็น %s", from.c_str(), to.c_str());  // no arrow glyph in the Thai fonts
    r.value = result;
    r.changePct = q["change"] | 0.0f;
    r.change = r.value * r.changePct / 100.0f;
    r.high = r.low = r.open = 0;
    r.rsi = -1;
    r.rating = -9;
    snprintf(r.extra, sizeof(r.extra), "1 %s = %.4f %s", from.c_str(), rate, to.c_str());
    snprintf(card.title, sizeof(card.title), "%.2f %s เป็น %s", amount, from.c_str(), to.c_str());
    copyStr(card.subtitle, sizeof(card.subtitle), "อัตราแลกเปลี่ยนล่าสุด • TradingView");
}

// ---------------------------------------------------------------- news (Google News RSS)

void decodeEntities(String& s) {
    s.replace("&amp;", "&");
    s.replace("&quot;", "\"");
    s.replace("&#39;", "'");
    s.replace("&apos;", "'");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("<![CDATA[", "");
    s.replace("]]>", "");
}

void runNews(JsonObjectConst args, JsonDocument& out, CardData& card) {
    const char* topic = args["topic"] | "";
    String url = *topic ? String("https://news.google.com/rss/search?hl=th&gl=TH&ceid=TH:th&q=") + urlEncode(topic)
                        : String("https://news.google.com/rss?hl=th&gl=TH&ceid=TH:th");
    String body;
    if (httpGet(url, body) != 200) {
        setError(out, "ดึงข่าวไม่สำเร็จ");
        return;
    }
    card.kind = CardData::News;
    snprintf(card.title, sizeof(card.title), "%s%s", *topic ? "ข่าว: " : "ข่าวล่าสุด", topic);
    copyStr(card.subtitle, sizeof(card.subtitle), "Google News");
    JsonArray arr = out["result"]["headlines"].to<JsonArray>();
    int pos = 0;
    while (card.rows < CardData::kMaxRows) {
        const int item = body.indexOf("<item>", pos);
        if (item < 0) break;
        const int t0 = body.indexOf("<title>", item);
        const int t1 = body.indexOf("</title>", t0);
        if (t0 < 0 || t1 < 0) break;
        pos = t1;
        String title = body.substring(t0 + 7, t1);
        decodeEntities(title);
        String source;
        const int dash = title.lastIndexOf(" - ");
        if (dash > 0) {
            source = title.substring(dash + 3);
            title = title.substring(0, dash);
        }
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), title.c_str());
        copyStr(r.extra, sizeof(r.extra), source.c_str());
        JsonObject h = arr.add<JsonObject>();
        h["title"] = title;
        h["source"] = source;
    }
    if (!card.rows) {
        setError(out, "ไม่พบข่าว");
        card.kind = CardData::None;
        return;
    }
    out["result"]["note"] = "หัวข้อข่าวแสดงบนจอแล้ว เล่าสรุป 2-3 ข่าวสั้นๆ";
}

}  // namespace

int tickerCandidates(String token, String out[3], const char** aliasName) {
    token.trim();
    if (aliasName) *aliasName = nullptr;
    if (token.isEmpty()) return 0;
    if (const Alias* a = findAlias(token)) {
        if (aliasName) *aliasName = a->name;
        out[0] = a->symbol;
        return 1;
    }
    token.toUpperCase();
    if (token.indexOf(':') > 0) {
        out[0] = token;
        return 1;
    }
    out[0] = "SET:" + token;  // a Thai user most often means a Thai stock
    out[1] = "NASDAQ:" + token;
    out[2] = "NYSE:" + token;
    return 3;
}

// ============================================================

bool isWebTool(const char* name) {
    if (!name) return false;
    for (const char* t : kWebTools) {
        if (!strcmp(name, t)) return true;
    }
    return false;
}

bool WebTools::begin(const char* storageDir) {
    copyStr(_dir, sizeof(_dir), storageDir);
    _jobs = xQueueCreate(kQueueDepth, sizeof(Job));
    _results = xQueueCreate(kQueueDepth, sizeof(Result));
    if (!_jobs || !_results) return false;
    return xTaskCreatePinnedToCore(taskEntry, "webTools", 12288, this, 2, nullptr, 0) == pdPASS;
}

bool WebTools::submit(const char* id, const char* name, const char* argsJson, uint32_t tag) {
    Job j{};
    copyStr(j.id, sizeof(j.id), id);
    copyStr(j.name, sizeof(j.name), name);
    j.tag = tag;
    const size_t n = strlen(argsJson ? argsJson : "{}");
    j.args = (char*)psramMalloc(n + 1);
    if (!j.args) return false;
    memcpy(j.args, argsJson ? argsJson : "{}", n + 1);
    if (!_jobs || xQueueSend(_jobs, &j, 0) != pdTRUE) {
        psramFree(j.args);
        return false;
    }
    _busy = true;
    return true;
}

bool WebTools::poll(Result& out) { return _results && xQueueReceive(_results, &out, 0) == pdTRUE; }

void WebTools::taskEntry(void* self) { static_cast<WebTools*>(self)->loop(); }

void WebTools::loop() {
    static CardData card;  // ~2 KB: not on the task stack
    const ToolEnv env{_dir};
    uint32_t nextAlertCheck = millis() + 30000;
    bool alertsKnown = hasPriceAlerts(env);
    Job j;
    for (;;) {
        if (xQueueReceive(_jobs, &j, pdMS_TO_TICKS(5000)) != pdTRUE) {
            // idle: watch the stored price alerts (one TradingView batch request every 2 min)
            if (alertsKnown && (int32_t)(millis() - nextAlertCheck) >= 0 && WiFi.status() == WL_CONNECTED) {
                nextAlertCheck = millis() + kAlertCheckMs;
                String msg;
                if (checkPriceAlerts(env, msg)) {
                    Result r{};
                    copyStr(r.name, sizeof(r.name), "_price_alert");
                    r.tag = kAlertTag;
                    r.json = new PsString();
                    JsonDocument m;
                    m["message"] = msg;
                    serializeJson(m, *r.json);
                    if (xQueueSend(_results, &r, pdMS_TO_TICKS(2000)) != pdTRUE) delete r.json;
                }
                alertsKnown = hasPriceAlerts(env);
            }
            continue;
        }
        const uint32_t t0 = millis();
        JsonDocument args(JsonPsramAllocator::instance());
        deserializeJson(args, j.args);
        psramFree(j.args);
        JsonDocument out(JsonPsramAllocator::instance());
        card = CardData();
        const JsonObjectConst a = args.as<JsonObjectConst>();
        if (!strcmp(j.name, "get_weather")) runWeather(a, out, card);
        else if (!strcmp(j.name, "get_oil_price")) runOil(out, card);
        else if (!strcmp(j.name, "get_market_quote")) runQuote(a, out, card);
        else if (!strcmp(j.name, "convert_currency")) runFx(a, out, card);
        else if (!strcmp(j.name, "get_news")) runNews(a, out, card);
        else if (!strcmp(j.name, "calculate")) runCalculate(a, out, card);
        else if (!strcmp(j.name, "convert_units")) runConvertUnits(a, out, card);
        else if (!strcmp(j.name, "get_world_time")) runWorldTime(a, out, card);
        else if (!strcmp(j.name, "get_location")) runLocation(out, card);
        else if (!strcmp(j.name, "search_knowledge")) runKnowledge(a, out, card);
        else if (!strcmp(j.name, "market_overview")) runMarketOverview(a, out, card);
        else if (!strcmp(j.name, "market_analysis")) runMarketAnalysis(a, out, card);
        else if (!strcmp(j.name, "market_scan")) runMarketScan(a, out, card);
        else if (!strcmp(j.name, "economic_calendar")) runEconomicCalendar(a, out, card);
        else if (!strcmp(j.name, "position_size")) runPositionSize(a, out, card);
        else if (!strcmp(j.name, "notes")) runNotes(env, a, out, card);
        else if (!strcmp(j.name, "price_alert")) runPriceAlert(env, a, out, card);
        else if (!strcmp(j.name, "mt5_account")) runMt5Account(a, out, card);
        else if (!strcmp(j.name, "mt5_trade")) runMt5Trade(a, out, card);
        else setError(out, "unknown tool");
        if (out.isNull() || out.size() == 0) setError(out, "no data");
        if (card.kind != CardData::None && out["error"].isNull()) gCard.show(card, 30000);

        Result r{};
        copyStr(r.id, sizeof(r.id), j.id);
        copyStr(r.name, sizeof(r.name), j.name);
        r.tag = j.tag;
        r.elapsedMs = millis() - t0;
        r.json = new PsString();
        serializeJson(out, *r.json);
        if (xQueueSend(_results, &r, pdMS_TO_TICKS(2000)) != pdTRUE) delete r.json;
        if (!strcmp(j.name, "price_alert")) {
            alertsKnown = hasPriceAlerts(env);
            nextAlertCheck = millis() + 20000;
        }
        _busy = uxQueueMessagesWaiting(_jobs) > 0;
    }
}

}  // namespace webtools
