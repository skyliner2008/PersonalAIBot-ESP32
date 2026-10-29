/**
 * @file WebTools.cpp
 * @brief Weather / fuel / market / FX / news tools: HTTPS fetch, parse, card + model response
 */

#include "WebTools.h"

#include "ThaiProvinces.h"
#include "WebToolsCommon.h"
#include "../system/PsramTask.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <new>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>

#include <algorithm>

#include "../ai/GoogleRootCA.h"
#include "../app/Settings.h"
#include "../ui/CardScreen.h"
#include "../util/JsonAlloc.h"
#include "../util/Units.h"
#include "../util/Utf8.h"

namespace webtools {

namespace {

portMUX_TYPE gClockWxMux = portMUX_INITIALIZER_UNLOCKED;
ClockWeather gClockWx;
volatile bool gClockWxRefreshReq = true;

void storeClockWeather(const CardData::WeatherInfo& w, const char* city) {
    ClockWeather snap;
    snap.valid = true;
    snap.temp = w.temp;
    snap.humidity = w.humidity;
    snap.pm25 = w.pm25;
    snap.code = w.code;
    snap.isDay = w.isDay;
    strncpy(snap.city, city ? city : "", sizeof(snap.city) - 1);
    snap.city[sizeof(snap.city) - 1] = '\0';
    portENTER_CRITICAL(&gClockWxMux);
    gClockWx = snap;
    portEXIT_CRITICAL(&gClockWxMux);
}

constexpr UBaseType_t kQueueDepth = 4;
constexpr UBaseType_t kTaskPriority = 2;  // idle / background (price alerts); see JobPriority

const char* const kWebTools[] = {"get_weather",     "get_oil_price",   "get_market_quote",  "convert_currency",
                                 "get_news",        "calculate",       "convert_units",     "get_world_time",
                                 "get_location",    "search_knowledge", "market_overview",  "market_analysis",
                                 "market_scan",     "economic_calendar", "position_size",   "notes",
                                 "price_alert",     "water"};

constexpr uint32_t kAlertCheckMs = 120000;

// ---------------------------------------------------------------- short-lived result cache
// Every lookup is 1-3 TLS handshakes (~1.3 s of CPU each, get_weather up to 3) and the model often asks
// the same thing again for a follow-up question ("แล้วพรุ่งนี้ล่ะ"): the same tool + arguments within the
// data's own refresh time gets the stored answer and card (T069). Errors are never stored.
uint32_t cacheTtlMs(const char* name) {
    struct Ttl {
        const char* name;
        uint32_t ms;
    };
    static const Ttl kTtl[] = {
        {"get_weather", 10 * 60000},       {"get_oil_price", 30 * 60000},    {"get_news", 5 * 60000},
        {"economic_calendar", 30 * 60000}, {"search_knowledge", 60 * 60000}, {"get_market_quote", 60000},
        {"convert_currency", 60000},       {"market_overview", 60000},
        {"water", 10 * 60000},  // ThaiWater telemetry reports every 10-60 min (T100)
    };
    for (const Ttl& t : kTtl) {
        if (!strcmp(name, t.name)) return t.ms;
    }
    return 0;  // live state (notes, alerts), pure math, or scans that should run fresh
}

struct CacheEntry {
    PsString key;  // "<tool>\n<args json>"
    PsString json;
    CardData* card = nullptr;  // PSRAM
    uint32_t at = 0, ttl = 0;
};
constexpr int kCacheSlots = 4;
CacheEntry gCache[kCacheSlots];

CacheEntry* cacheFind(const PsString& key, uint32_t now) {
    for (CacheEntry& e : gCache) {
        if (e.ttl && e.key == key && now - e.at < e.ttl) return &e;
    }
    return nullptr;
}

void cacheStore(const PsString& key, uint32_t ttl, const PsString& json, const CardData& card, uint32_t now) {
    CacheEntry* slot = nullptr;
    for (CacheEntry& e : gCache) {  // the same key, else an empty / expired slot, else the oldest
        if (e.key == key) {
            slot = &e;
            break;
        }
    }
    for (CacheEntry& e : gCache) {
        if (slot) break;
        if (!e.ttl || now - e.at >= e.ttl) slot = &e;
    }
    if (!slot) {
        slot = &gCache[0];
        for (CacheEntry& e : gCache) {
            if (now - e.at > now - slot->at) slot = &e;
        }
    }
    if (!slot->card) {
        void* mem = heap_caps_malloc(sizeof(CardData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) return;
        slot->card = new (mem) CardData();
    }
    slot->key = key;
    slot->json = json;
    *slot->card = card;
    slot->at = now;
    slot->ttl = ttl;
}

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
    if (const thaiplace::Province* p = thaiplace::findProvince(name)) {  // no request for the 77 provinces
        lat = p->lat;
        lon = p->lon;
        label = p->th;
        return true;
    }
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
    const SettingsData cfg = gSettings.get();
    float lat = 13.7540f, lon = 100.5014f;
    String label = "กรุงเทพมหานคร";
    if (*loc || *locEn) {
        if (!geocode(loc, lat, lon, label) && !geocode(locEn, lat, lon, label)) {
            setError(out, "ไม่พบสถานที่นี้ ลองระบุชื่อจังหวัด หรือส่ง location_en เป็นภาษาอังกฤษ");
            return;
        }
    } else if (cfg.weatherCity[0] && geocode(cfg.weatherCity, lat, lon, label)) {
        // user selected a home/weather city in Settings
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
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=pm2_5,us_aqi_pm2_5"
             "&timezone=Asia%%2FBangkok",
             lat, lon);
    String aq;
    if (httpGet(url, aq) == 200) {
        JsonDocument a(JsonPsramAllocator::instance());
        if (!deserializeJson(a, aq)) {
            const float pm = a["current"]["pm2_5"] | -1.0f;
            w.pm25 = pm < 0 ? -1 : (int16_t)lroundf(pm);
            // us_aqi is the worst of all pollutants (often a modelled ozone peak: 201 while PM2.5 is 25 ug/m3)
            w.aqi = a["current"]["us_aqi_pm2_5"] | -1;
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
        res["us_aqi_pm25"] = w.aqi;
    }
    if (!(*loc || *locEn) || (cfg.weatherCity[0] && !strcmp(loc, cfg.weatherCity)) || !getClockWeather().valid) {
        storeClockWeather(w, label.c_str());
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
    res["note"] = "จอแสดงประกอบ (ผู้ใช้อาจไม่ได้มอง) เล่าให้ครบเป็นธรรมชาติ: ตอนนี้ อุณหภูมิ โอกาสฝน ฝุ่น และวันที่ผู้ใช้ถาม";
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
    // "PriceToday" is the price in force now; "PriceYesterday" is the price BEFORE THE LAST ADJUSTMENT
    // (which may be a week ago), not yesterday's. When it took effect: "ราคามีผล ณ วันที่ 9 ต.ค. 69
    // เวลา 05.00 น." (OilRemark2), else the announcement date (OilPriceDate) + 1 day.
    const char* remark = root["OilRemark2"] | "";
    datadates::Ymd eff;
    bool haveEff = datadates::parseThaiDate(remark, eff);
    if (!haveEff && datadates::parseSlashDate(root["OilPriceDate"] | "", nullptr, eff)) {
        const timefmt::Civil c = timefmt::toCivil((uint32_t)((eff.day() + 1) * 86400), 0);
        eff.y = c.year, eff.m = c.month, eff.d = c.day, eff.hh = 5, eff.mm = 0;
        haveEff = true;
    }
    const uint32_t now = epochNow();
    const int64_t today = now ? timefmt::localDay(now, TIME_UTC_OFFSET_SEC) : 0;
    const int daysSince = haveEff && now ? (int)(today - eff.day()) : -1;

    card.kind = CardData::Oil;
    copyStr(card.title, sizeof(card.title), "ราคาน้ำมัน (บางจาก)");
    if (haveEff) {  // the remark itself is longer than the subtitle
        snprintf(card.subtitle, sizeof(card.subtitle), "มีผล %d %s %02d.%02d น.%s", eff.d, datadates::thaiMonthAbbr(eff.m),
                 eff.hh, eff.mm, daysSince > 0 ? " • ส่วนต่าง=ตอนปรับ" : "");
    } else {
        copyStr(card.subtitle, sizeof(card.subtitle), remark);
    }
    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "บางจาก (ราคาขายปลีก กทม.)";
    res["effective"] = remark;
    if (haveEff) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", eff.y, eff.m, eff.d, eff.hh, eff.mm);
        res["price_effective_since"] = buf;
    }
    if (daysSince >= 0) res["days_since_last_change"] = daysSince;
    JsonArray items = res["prices_baht_per_litre"].to<JsonArray>();
    bool tomorrowAny = false;
    for (JsonObjectConst o : list.as<JsonArrayConst>()) {
        if (card.rows >= CardData::kMaxRows) break;
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), o["OilName"] | "");
        r.value = o["PriceToday"] | 0.0f;
        const float before = o["PriceYesterday"] | r.value;  // PriceDif* fields are always 0.00: compute it
        r.change = roundf((r.value - before) * 100.0f) / 100.0f;
        r.next = o["PriceTomorrow"] | 0.0f;
        JsonObject it = items.add<JsonObject>();
        it["name"] = r.name;
        it["price_now"] = r.value;
        it["change_at_last_adjustment"] = r.change;
        if (r.next > 0 && fabsf(r.next - r.value) > 0.001f) {
            it["tomorrow"] = r.next;
            tomorrowAny = true;
        }
    }
    String note = "ตารางราคาแสดงบนจอแล้ว ตอบเฉพาะชนิดที่ผู้ใช้ถาม price_now คือราคาที่ใช้อยู่ตอนนี้ ";
    if (daysSince > 0) {
        char since[48];  // daysSince > 0 implies haveEff
        snprintf(since, sizeof(since), "%d %s %02d.%02d น.", eff.d, datadates::thaiMonthAbbr(eff.m), eff.hh, eff.mm);
        note += "ราคานี้ใช้มาตั้งแต่ ";
        note += since;
        note += " (" + String(daysSince) + " วันก่อน) change_at_last_adjustment คือส่วนต่างตอนปรับราคาครั้งนั้น "
                "ไม่ใช่การเปลี่ยนแปลงเมื่อวานหรือวันนี้ ";
    } else if (daysSince == 0) {
        note += "ราคาเพิ่งปรับวันนี้ change_at_last_adjustment คือส่วนต่างจากราคาเดิม ";
    }
    if (tomorrowAny) note += "มีประกาศราคาพรุ่งนี้ (tomorrow) แล้ว";
    res["note"] = note;
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
    "description,close,change,change_abs,high,low,open,Perf.W,Perf.1M,Perf.3M,Perf.Y,RSI,Recommend.All,currency,"
    "current_session,last_bar_update_time,update_mode";

// When the price is from: market open or closed (weekend / night: the last close), last update, delay
void addFreshness(const JsonDocument& q, JsonObject res) {
    const char* session = q["current_session"] | "";
    if (*session) {
        res["market_open"] = !strcmp(session, "market");
        if (strcmp(session, "market") != 0) res["session"] = session;  // out_of_session / pre_market / post_market
    }
    const uint32_t last = q["last_bar_update_time"] | 0u;
    if (last) res["price_time"] = thaiDateTime(last);
    const char* mode = q["update_mode"] | "";
    if (!strncmp(mode, "delayed_streaming_", 18)) res["delayed_min"] = atoi(mode + 18) / 60;
}

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
        addFreshness(q, res);
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
    out["result"]["note"] = "จอแสดงกราฟประกอบ เล่าราคา (ตัวเลขตรงตามผล) การเปลี่ยนแปลงวันนี้ ช่วงราคา และแนวโน้มที่สำคัญ แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ตั้งแจ้งเตือนราคา วิเคราะห์เพิ่ม เทียบตัวอื่น ข่าวที่กระทบ) ห้ามปิดด้วยคำเตือนการลงทุน "
                            "ถ้า market_open=false ให้บอกว่าเป็นราคาปิดล่าสุด (price_time) ไม่ใช่ราคาตอนนี้";
    // T110: a Thai user's "ราคาทอง" is Thai gold (baht per baht weight) - estimated here, so the model needs no
    // second lookup or arithmetic of its own (with only the news it said "no price announced yet, cannot compute")
    float xau = 0, thb = 0;
    for (JsonObjectConst q : arr) {
        const char* s = q["symbol"] | "";
        if (strstr(s, "XAUUSD")) xau = q["price"] | 0.0f;
        if (strstr(s, "USDTHB")) thb = q["price"] | 0.0f;
    }
    if (xau > 0 && thb <= 0) {
        JsonDocument q(JsonPsramAllocator::instance());
        if (fetchQuote("FX_IDC:USDTHB", q)) thb = q["close"] | 0.0f;
    }
    const double est = units::thaiGoldPerBahtWeight(xau, thb);
    if (est > 0) {
        JsonObject g = out["result"]["thai_gold_estimate"].to<JsonObject>();
        g["bar_baht_per_baht_weight"] = (long)(est / 10.0 + 0.5) * 10;  // the association quotes in 10s / 50s
        g["usd_thb"] = thb;
        // T111: once it was left out of the answer ("4,022 ดอลลาร์ต่อออนซ์" only)
        g["note"] = "ทองคำแท่ง 96.5% บาทละ ประมาณจากทองโลก x ค่าเงินบาท - เล่าราคานี้ด้วยทุกครั้ง (คนไทยถามราคาทองมักหมายถึงราคานี้) "
                    "บอกว่าเป็นราคาประมาณ (ราคาสมาคมค้าทองคำจริงต่างได้เล็กน้อย)";
    }
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
    addFreshness(q, res);

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

struct NewsItem {
    String title, source;
    uint32_t published;
};

// Google News RSS: up to `max` items (title split from " - source", pubDate -> epoch)
void parseNews(const String& body, std::vector<NewsItem>& items, size_t max) {
    int pos = 0;
    while (items.size() < max) {
        const int item = body.indexOf("<item>", pos);
        if (item < 0) break;
        const int t0 = body.indexOf("<title>", item);
        const int t1 = body.indexOf("</title>", t0);
        if (t0 < 0 || t1 < 0) break;
        pos = t1;
        const int next = body.indexOf("<item>", t1);
        const int d0 = body.indexOf("<pubDate>", t1);
        const int d1 = d0 >= 0 ? body.indexOf("</pubDate>", d0) : -1;
        NewsItem n{body.substring(t0 + 7, t1), String(), 0};
        if (d0 >= 0 && d1 > d0 && (next < 0 || d0 < next)) {
            n.published = datadates::parseRfc822(body.substring(d0 + 9, d1).c_str());
        }
        decodeEntities(n.title);
        const int dash = n.title.lastIndexOf(" - ");
        if (dash > 0) {
            n.source = n.title.substring(dash + 3);
            n.title = n.title.substring(0, dash);
        }
        items.push_back(n);
    }
}

void runNews(JsonObjectConst args, JsonDocument& out, CardData& card) {
    const char* topic = args["topic"] | "";
    // A topic search is ordered by relevance and reaches back months ("latest Man Utd result" gave a
    // February match): ask for the last 7 days first (Google News "when:7d"), all time only if empty.
    std::vector<NewsItem> items;
    bool recent = *topic != 0;
    for (int attempt = 0; attempt < 2 && items.empty(); attempt++) {
        String url = *topic ? String("https://news.google.com/rss/search?hl=th&gl=TH&ceid=TH:th&q=") +
                                  urlEncode((String(topic) + (recent ? " when:7d" : "")).c_str())
                            : String("https://news.google.com/rss?hl=th&gl=TH&ceid=TH:th");
        String body;
        if (httpGet(url, body) != 200) {
            setError(out, "ดึงข่าวไม่สำเร็จ");
            return;
        }
        parseNews(body, items, CardData::kMaxRows);  // the most relevant ones...
        if (!*topic) break;
        if (items.empty()) recent = false;
    }
    if (items.empty()) {
        setError(out, "ไม่พบข่าว");
        return;
    }
    if (*topic) {  // ...newest first (sorting all 100 by date let generic newer news push the topic out)
        std::stable_sort(items.begin(), items.end(),
                         [](const NewsItem& a, const NewsItem& b) { return a.published > b.published; });
    }
    card.kind = CardData::News;
    snprintf(card.title, sizeof(card.title), "%s%s", *topic ? "ข่าว: " : "ข่าวล่าสุด", topic);
    copyStr(card.subtitle, sizeof(card.subtitle), *topic && recent ? "Google News • 7 วันล่าสุด" : "Google News");
    JsonArray arr = out["result"]["headlines"].to<JsonArray>();
    const uint32_t now = epochNow();
    for (const NewsItem& n : items) {
        if (card.rows >= CardData::kMaxRows) break;
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), n.title.c_str());
        copyStr(r.extra, sizeof(r.extra), n.source.c_str());
        JsonObject h = arr.add<JsonObject>();
        h["title"] = n.title;
        h["source"] = n.source;
        if (n.published) {
            h["published"] = thaiDateTime(n.published);
            if (now > n.published) h["age_hours"] = (now - n.published) / 3600;
        }
    }
    if (*topic) out["result"]["range"] = recent ? "7 วันล่าสุด (ใหม่สุดก่อน)" : "ทุกช่วงเวลา (ไม่พบข่าวใน 7 วัน)";
    // T111: only headlines come back - "ขอรายละเอียดข่าวแรก" was answered with details that were not in them
    out["result"]["note"] = "จอแสดงหัวข่าวประกอบ เล่าข่าวเด่นทุกข่าวที่เกี่ยวกับที่ถาม ข่าวละ 1-2 ประโยค ข่าวที่ age_hours มากกว่า 24 "
                            "ให้บอกวันที่ (published) ด้วย อย่าเล่าเหมือนเป็นข่าววันนี้ · ผลมีแค่หัวข่าว: ผู้ใช้ขอรายละเอียดข่าวไหน "
                            "ให้เรียก get_news อีกครั้งโดยใส่ topic เป็นคำสำคัญของข่าวนั้น (ได้หัวข่าวจากหลายสำนัก) แล้วเล่ารวม "
                            "ห้ามแต่งรายละเอียดที่ไม่มีในหัวข่าว";
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
    _jobs = psramQueue(kQueueDepth, sizeof(Job));
    _results = psramQueue(kQueueDepth, sizeof(Result));
    if (!_jobs || !_results) return false;
    // core 1: TLS handshakes + big JSON are CPU-heavy bursts; core 0 keeps audio + radio + Wi-Fi
    // (the idle task of core 0 is watchdog-checked: a long burst there next to the radio resets the board)
    // notes / price alerts are files in the agent dir: on LittleFS that is a flash write, which needs an
    // internal stack; on the SD card (or no storage) the 12 KB stack can live in PSRAM (system/PsramTask.h)
    const bool writesFlash = strncmp(_dir, "/littlefs", 9) == 0;
    if (writesFlash) return xTaskCreatePinnedToCore(taskEntry, "webTools", 12288, this, kTaskPriority, nullptr, 1) == pdPASS;
    return startPsramTask(taskEntry, "webTools", 12288, this, kTaskPriority, nullptr, 1);
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

namespace {
// A job someone waits for (the model, the console) runs above the eyes' animation and the wake word (ui /
// wakefeed / wakeword, priority 3 on this core; the ui redraws every 16 ms): at priority 2 the TLS crypto
// only got the gaps between frames - a handshake took 2.4 s awake, 1.2 s at 4 (T072). It still yields to
// the agent loop and the Live API I/O (5); waiting on the network costs the others nothing. The idle-time
// price-alert check stays at 2. (The core-1 idle task is not watchdog-checked.)
constexpr UBaseType_t kJobPriority = 4;
struct JobPriority {
    JobPriority() { vTaskPrioritySet(nullptr, kJobPriority); }
    ~JobPriority() { vTaskPrioritySet(nullptr, kTaskPriority); }
};
}  // namespace

ClockWeather getClockWeather() {
    ClockWeather snap;
    portENTER_CRITICAL(&gClockWxMux);
    snap = gClockWx;
    portEXIT_CRITICAL(&gClockWxMux);
    return snap;
}

void requestClockWeatherRefresh() { gClockWxRefreshReq = true; }

void WebTools::loop() {
    // ~3 KB: not on the task stack, and not in internal RAM either (wiki 14: the wake word needs it)
    void* cardMem = heap_caps_malloc(sizeof(CardData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    CardData& card = *new (cardMem ? cardMem : malloc(sizeof(CardData))) CardData();  // the task never ends
    const ToolEnv env{_dir};
    uint32_t nextAlertCheck = millis() + 30000;
    uint32_t nextWxCheck = millis() + 6000;
    char lastWxCity[48] = "\x01";
    bool alertsKnown = hasPriceAlerts(env);
    Job j;
    for (;;) {
        if (xQueueReceive(_jobs, &j, pdMS_TO_TICKS(5000)) != pdTRUE) {
            const uint32_t nowMs = millis();
            // idle: refresh screensaver weather (on boot/city change or every 20 min) without popping up a card
            if (WiFi.status() == WL_CONNECTED) {
                const SettingsData cfg = gSettings.get();
                const bool cityChanged = strcmp(lastWxCity, cfg.weatherCity) != 0;
                if (gClockWxRefreshReq || cityChanged || (int32_t)(nowMs - nextWxCheck) >= 0) {
                    gClockWxRefreshReq = false;
                    strncpy(lastWxCity, cfg.weatherCity, sizeof(lastWxCity) - 1);
                    lastWxCity[sizeof(lastWxCity) - 1] = '\0';
                    JsonDocument a(JsonPsramAllocator::instance());
                    JsonDocument out(JsonPsramAllocator::instance());
                    if (cfg.weatherCity[0]) a["location"] = cfg.weatherCity;
                    else a.to<JsonObject>();
                    card = CardData();
                    setStage("clock_weather");
                    runWeather(a.as<JsonObjectConst>(), out, card);
                    setStage("idle");
                    if (card.kind == CardData::Weather && out["error"].isNull()) {
                        const char* locLabel = out["result"]["location"] | cfg.weatherCity;
                        storeClockWeather(card.w, locLabel);
                        Serial.printf("[Weather] clock face: %s %.1f C, PM2.5 %d\n", locLabel, card.w.temp,
                                      (int)card.w.pm25);
                        nextWxCheck = millis() + 20 * 60000;
                    } else {
                        Serial.printf("[Weather] clock face refresh failed (%s) - again in 1 min\n",
                                      out["error"] | "no weather card");
                        nextWxCheck = millis() + 60000;
                    }
                }
            }
            // idle: watch the stored price alerts (one TradingView batch request every 2 min)
            if (alertsKnown && (int32_t)(millis() - nextAlertCheck) >= 0 && WiFi.status() == WL_CONNECTED) {
                nextAlertCheck = millis() + kAlertCheckMs;
                String msg;
                if (checkPriceAlerts(env, msg)) {
                    Result r{};
                    copyStr(r.name, sizeof(r.name), "_price_alert");
                    r.tag = kAlertTag;
                    r.json = new PsString();
                    JsonDocument m(JsonPsramAllocator::instance());
                    m["message"] = msg;
                    serializeJson(m, *r.json);
                    if (xQueueSend(_results, &r, pdMS_TO_TICKS(2000)) != pdTRUE) delete r.json;
                }
                alertsKnown = hasPriceAlerts(env);
            }
            continue;
        }
        JobPriority boost;
        const uint32_t t0 = millis();
        setStage(j.name);  // T105: the agent reports a job that stops moving
        const uint32_t ttl = cacheTtlMs(j.name);
        PsString key;
        if (ttl) {
            key = j.name;
            key += '\n';
            key += j.args;
        }
        if (const CacheEntry* hit = ttl ? cacheFind(key, t0) : nullptr) {
            psramFree(j.args);
            if (hit->card->kind != CardData::None) gCard.show(*hit->card, 30000);
            Result r{};
            copyStr(r.id, sizeof(r.id), j.id);
            copyStr(r.name, sizeof(r.name), j.name);
            r.tag = j.tag;
            r.elapsedMs = millis() - t0;
            r.json = new PsString(hit->json);  // as_of = when it was fetched
            Serial.printf("[Tool] %s: answered from the last %lu s\n", j.name, (unsigned long)((t0 - hit->at) / 1000));
            if (xQueueSend(_results, &r, pdMS_TO_TICKS(2000)) != pdTRUE) delete r.json;
            _busy = uxQueueMessagesWaiting(_jobs) > 0;
            continue;
        }
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
        else if (!strcmp(j.name, "water")) runWater(a, out, card);
        else setError(out, "unknown tool");
        if (out.isNull() || out.size() == 0) setError(out, "no data");
        // live data: "now" for the model (its prompt date is from the session start, or missing if
        // the clock was not synced yet), so "today" / "yesterday" in its answer match the data
        const bool pure = !strcmp(j.name, "calculate") || !strcmp(j.name, "convert_units") ||
                          !strcmp(j.name, "position_size") || !strcmp(j.name, "notes");
        if (!pure && out["result"].is<JsonObject>() && epochNow()) out["result"]["as_of"] = thaiDateTime(epochNow());
        if (card.kind != CardData::None && out["error"].isNull()) {
            gCard.show(card, 30000);
        }

        Result r{};
        copyStr(r.id, sizeof(r.id), j.id);
        copyStr(r.name, sizeof(r.name), j.name);
        r.tag = j.tag;
        r.elapsedMs = millis() - t0;
        r.json = new PsString();
        serializeJson(out, *r.json);
        if (ttl && out["error"].isNull()) cacheStore(key, ttl, *r.json, card, t0);
        if (xQueueSend(_results, &r, pdMS_TO_TICKS(2000)) != pdTRUE) delete r.json;
        if (!strcmp(j.name, "price_alert")) {
            alertsKnown = hasPriceAlerts(env);
            nextAlertCheck = millis() + 20000;
        }
        setStage("idle");
        _busy = uxQueueMessagesWaiting(_jobs) > 0;
    }
}

}  // namespace webtools
