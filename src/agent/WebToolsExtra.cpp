/**
 * @file WebToolsExtra.cpp
 * @brief Tools ported from the PersonalAIBot app: calculator, units, world clock, location, Wikipedia,
 *        market overview / analysis / scan, economic calendar, position size, notes, price alerts
 * @see wiki/11-Data-Tools-and-Cards.md
 */

#include <math.h>
#include <time.h>

#include "../app/Settings.h"
#include "../util/Calc.h"
#include "../util/JsonFile.h"
#include "../util/NoteSearch.h"
#include "../util/TimeFmt.h"
#include "../util/Units.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr uint16_t kUp = 0x2FE8, kDown = 0xF9E7, kOrange = 0xFD20, kDim = 0x9CD3;

uint16_t ratingColor(double r) { return r >= 0.1 ? kUp : (r <= -0.1 ? kDown : kDim); }

uint32_t nowEpoch() {
    const time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

String lowerNoSpace(const char* s) {
    String out;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (*p == ' ' || *p == '.' || *p == '-' || *p == '_') continue;
        out += (char)(*p < 0x80 ? tolower(*p) : *p);
    }
    return out;
}

using timefmt::daysFromCivil;
int64_t epochOf(int y, int m, int d, int hh, int mm) { return daysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60; }
int nthSunday(int y, int m, int n) {  // n = 1.. ; 0 = last
    if (n > 0) {
        const int first = weekday(y, m, 1);
        return 1 + (7 - first) % 7 + (n - 1) * 7;
    }
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int last = kDays[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 || y % 400 == 0)));
    return last - weekday(y, m, last);
}

// ---------------------------------------------------------------- world clock

enum Dst : uint8_t { kNoDst, kEu, kUs, kAu, kNz };
struct City {
    const char* names;  // '|' separated, normalised
    const char* label;
    int16_t offsetMin;
    Dst dst;
};
const City kCities[] = {
    {"bangkok|กรุงเทพ|กรุงเทพมหานคร|ไทย|thailand", "กรุงเทพฯ", 420, kNoDst},
    {"tokyo|โตเกียว|ญี่ปุ่น|japan|osaka|โอซาก้า", "โตเกียว", 540, kNoDst},
    {"seoul|โซล|เกาหลี|korea", "โซล", 540, kNoDst},
    {"beijing|ปักกิ่ง|จีน|china|shanghai|เซี่ยงไฮ้", "ปักกิ่ง", 480, kNoDst},
    {"hongkong|ฮ่องกง", "ฮ่องกง", 480, kNoDst},
    {"taipei|ไทเป|ไต้หวัน|taiwan", "ไทเป", 480, kNoDst},
    {"singapore|สิงคโปร์", "สิงคโปร์", 480, kNoDst},
    {"kualalumpur|กัวลาลัมเปอร์|มาเลเซีย|malaysia", "กัวลาลัมเปอร์", 480, kNoDst},
    {"manila|มะนิลา|ฟิลิปปินส์|philippines", "มะนิลา", 480, kNoDst},
    {"perth|เพิร์ท", "เพิร์ท", 480, kNoDst},
    {"jakarta|จาการ์ตา|อินโดนีเซีย|indonesia", "จาการ์ตา", 420, kNoDst},
    {"hanoi|ฮานอย|เวียดนาม|vietnam|hochiminh|โฮจิมินห์", "ฮานอย", 420, kNoDst},
    {"vientiane|เวียงจันทน์|ลาว|laos", "เวียงจันทน์", 420, kNoDst},
    {"phnompenh|พนมเปญ|กัมพูชา|cambodia", "พนมเปญ", 420, kNoDst},
    {"yangon|ย่างกุ้ง|พม่า|เมียนมา|myanmar", "ย่างกุ้ง", 390, kNoDst},
    {"delhi|newdelhi|เดลี|นิวเดลี|อินเดีย|india|mumbai|มุมไบ", "นิวเดลี", 330, kNoDst},
    {"kathmandu|กาฐมาณฑุ|เนปาล|nepal", "กาฐมาณฑุ", 345, kNoDst},
    {"dubai|ดูไบ|uae|ยูเออี", "ดูไบ", 240, kNoDst},
    {"riyadh|ริยาด|ซาอุ|ซาอุดีอาระเบีย|saudi", "ริยาด", 180, kNoDst},
    {"moscow|มอสโก|รัสเซีย|russia", "มอสโก", 180, kNoDst},
    {"istanbul|อิสตันบูล|ตุรกี|turkey", "อิสตันบูล", 180, kNoDst},
    {"johannesburg|โจฮันเนสเบิร์ก|แอฟริกาใต้", "โจฮันเนสเบิร์ก", 120, kNoDst},
    {"london|ลอนดอน|อังกฤษ|uk|england", "ลอนดอน", 0, kEu},
    {"paris|ปารีส|ฝรั่งเศส|france", "ปารีส", 60, kEu},
    {"berlin|เบอร์ลิน|เยอรมนี|เยอรมัน|germany", "เบอร์ลิน", 60, kEu},
    {"rome|โรม|อิตาลี|italy", "โรม", 60, kEu},
    {"madrid|มาดริด|สเปน|spain", "มาดริด", 60, kEu},
    {"amsterdam|อัมสเตอร์ดัม|เนเธอร์แลนด์|ฮอลแลนด์", "อัมสเตอร์ดัม", 60, kEu},
    {"zurich|ซูริค|สวิตเซอร์แลนด์|สวิส", "ซูริค", 60, kEu},
    {"stockholm|สตอกโฮล์ม|สวีเดน", "สตอกโฮล์ม", 60, kEu},
    {"newyork|นิวยอร์ก|washington|วอชิงตัน|อเมริกา|usa|toronto|โทรอนโต|แคนาดา", "นิวยอร์ก", -300, kUs},
    {"chicago|ชิคาโก", "ชิคาโก", -360, kUs},
    {"denver|เดนเวอร์", "เดนเวอร์", -420, kUs},
    {"losangeles|ลอสแองเจลิส|แอลเอ|la|sanfrancisco|ซานฟรานซิสโก|seattle|ซีแอตเทิล|vancouver|แวนคูเวอร์",
     "ลอสแองเจลิส", -480, kUs},
    {"honolulu|ฮาวาย|hawaii", "โฮโนลูลู", -600, kNoDst},
    {"saopaulo|เซาเปาโล|บราซิล|brazil", "เซาเปาโล", -180, kNoDst},
    {"sydney|ซิดนีย์|melbourne|เมลเบิร์น|ออสเตรเลีย|australia", "ซิดนีย์", 600, kAu},
    {"brisbane|บริสเบน", "บริสเบน", 600, kNoDst},
    {"auckland|โอ๊คแลนด์|นิวซีแลนด์|newzealand", "โอ๊คแลนด์", 720, kNz},
};

bool inDst(const City& c, int64_t t, int year) {
    switch (c.dst) {
        case kEu: return t >= epochOf(year, 3, nthSunday(year, 3, 0), 1, 0) && t < epochOf(year, 10, nthSunday(year, 10, 0), 1, 0);
        case kUs: {
            const int64_t start = epochOf(year, 3, nthSunday(year, 3, 2), 2, 0) - c.offsetMin * 60;
            const int64_t end = epochOf(year, 11, nthSunday(year, 11, 1), 2, 0) - (c.offsetMin + 60) * 60;
            return t >= start && t < end;
        }
        case kAu: {  // southern hemisphere: Oct -> Apr
            const int64_t end = epochOf(year, 4, nthSunday(year, 4, 1), 3, 0) - (c.offsetMin + 60) * 60;
            const int64_t start = epochOf(year, 10, nthSunday(year, 10, 1), 2, 0) - c.offsetMin * 60;
            return t < end || t >= start;
        }
        case kNz: {
            const int64_t end = epochOf(year, 4, nthSunday(year, 4, 1), 3, 0) - (c.offsetMin + 60) * 60;
            const int64_t start = epochOf(year, 9, nthSunday(year, 9, 0), 2, 0) - c.offsetMin * 60;
            return t < end || t >= start;
        }
        default: return false;
    }
}

const City* findCity(const char* name) {
    const String key = lowerNoSpace(name);
    if (key.isEmpty()) return &kCities[0];
    for (const City& c : kCities) {
        const char* p = c.names;
        while (*p) {
            const char* bar = strchr(p, '|');
            const size_t len = bar ? (size_t)(bar - p) : strlen(p);
            if (key.length() == len && !strncmp(p, key.c_str(), len)) return &c;
            p += len + (bar ? 1 : 0);
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- location (IP)

float gLat = 0, gLon = 0;
String gLocLabel;
uint32_t gLocAt = 0;

// ---------------------------------------------------------------- TradingView helpers

const char* tfSuffix(const String& tf) {
    String t = tf;
    t.toLowerCase();
    if (t == "15m" || t == "15") return "|15";
    if (t == "1h" || t == "60" || t == "h1") return "|60";
    if (t == "4h" || t == "240" || t == "h4") return "|240";
    if (t == "1w" || t == "w" || t == "week") return "|1W";
    if (t == "1m" || t == "month") return "|1M";
    return "";  // daily
}

const char* ratingTh(double r) {
    return r >= 0.5 ? "ซื้อแรง" : r >= 0.1 ? "ซื้อ" : r > -0.1 ? "กลางๆ" : r > -0.5 ? "ขาย" : "ขายแรง";
}

// GET scanner /symbol for the first candidate that exists
bool fetchSymbol(const String& token, const String& fields, JsonDocument& q, String& used, String& name) {
    String cands[3];
    const char* alias = nullptr;
    const int n = tickerCandidates(token, cands, &alias);
    for (int i = 0; i < n; i++) {
        String resp;
        const String url = String("https://scanner.tradingview.com/symbol?symbol=") + urlEncode(cands[i].c_str()) +
                           "&fields=" + fields;
        if (httpGet(url, resp) != 200) continue;
        q.clear();
        if (deserializeJson(q, resp) || !q["code"].isNull()) continue;
        used = cands[i];
        String upper = token;
        upper.trim();
        name = alias ? String(alias) : (token.indexOf(':') < 0 ? upper : String(q["description"] | cands[i].c_str()));
        return true;
    }
    return false;
}

}  // namespace

// ============================================================ calculate / units / time

void runCalculate(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const char* expr = a["expression"] | "";
    double v = 0;
    const char* err = nullptr;
    if (!calc::evaluate(expr, v, err)) {
        setError(out, err ? err : "คำนวณไม่ได้");
        return;
    }
    out["result"]["expression"] = expr;
    out["result"]["value"] = v;
    card.kind = CardData::Text;
    copyStr(card.title, sizeof(card.title), "เครื่องคิดเลข");
    if (fabs(v) >= 1e15 || (fabs(v) < 1e-4 && v != 0)) snprintf(card.big, sizeof(card.big), "%.6g", v);
    else snprintf(card.big, sizeof(card.big), "%.10g", v);
    snprintf(card.body, sizeof(card.body), "%s =", expr);
}

void runConvertUnits(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const double value = a["value"] | 1.0;
    const char* from = a["from"] | "";
    const char* to = a["to"] | "";
    double v = 0;
    const units::Unit *uf = nullptr, *ut = nullptr;
    const char* err = nullptr;
    if (!units::convert(value, from, to, v, uf, ut, err)) {
        setError(out, err);
        return;
    }
    JsonObject r = out["result"].to<JsonObject>();
    r["value"] = value;
    r["from"] = uf->label;
    r["to"] = ut->label;
    r["result"] = v;
    card.kind = CardData::Text;
    copyStr(card.title, sizeof(card.title), "แปลงหน่วย");
    snprintf(card.big, sizeof(card.big), "%.6g %s", v, ut->label);
    snprintf(card.body, sizeof(card.body), "%.6g %s เท่ากับ %.6g %s", value, uf->label, v, ut->label);
}

void runWorldTime(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const uint32_t now = nowEpoch();
    if (!now) {
        setError(out, "นาฬิกายังไม่ซิงก์เวลา");
        return;
    }
    const char* name = a["city"] | "";
    const City* c = findCity(name);
    if (!c) {
        setError(out, "ไม่รู้จักเมืองนี้ ลองชื่อเมืองหลักหรือประเทศ");
        return;
    }
    const timefmt::Civil utc = timefmt::toCivil(now, 0);
    const int offset = c->offsetMin + (inDst(*c, now, utc.year) ? 60 : 0);
    const timefmt::Civil local = timefmt::toCivil(now, offset * 60);
    const int diffMin = offset - 420;
    JsonObject r = out["result"].to<JsonObject>();
    r["city"] = c->label;
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", local.year, local.month, local.day, local.hour, local.minute);
    r["local_time"] = buf;
    r["utc_offset_hours"] = offset / 60.0;
    r["difference_from_thailand_hours"] = diffMin / 60.0;
    card.kind = CardData::Text;
    snprintf(card.title, sizeof(card.title), "เวลาที่%s", c->label);
    snprintf(card.big, sizeof(card.big), "%02d:%02d", local.hour, local.minute);
    char diff[48];
    if (diffMin == 0) snprintf(diff, sizeof(diff), "เท่ากับเวลาไทย");
    else snprintf(diff, sizeof(diff), "%s%s %.4g ชม.", diffMin > 0 ? "เร็วกว่า" : "ช้ากว่า", "ไทย", fabs(diffMin / 60.0));
    snprintf(card.body, sizeof(card.body), "วัน%s %d/%d/%d • UTC%+g • %s", timefmt::thaiWeekday(local.weekday), local.day,
             local.month, local.year + 543, offset / 60.0, diff);
}

// ============================================================ location

bool ipLocation(float& lat, float& lon, String& label) {
    const uint32_t now = millis();
    if (gLocLabel.length() && now - gLocAt < 3600000) {
        lat = gLat;
        lon = gLon;
        label = gLocLabel;
        return true;
    }
    String body;
    if (httpGet("https://ipwho.is/?fields=success,city,region,country,latitude,longitude", body) != 200) return false;
    JsonDocument d(JsonPsramAllocator::instance());
    if (deserializeJson(d, body) || !(d["success"] | false)) return false;
    gLat = d["latitude"] | 0.0f;
    gLon = d["longitude"] | 0.0f;
    gLocLabel = String((const char*)(d["city"] | "")) + ", " + (const char*)(d["region"] | "") + ", " +
                (const char*)(d["country"] | "");
    gLocAt = now;
    lat = gLat;
    lon = gLon;
    label = gLocLabel;
    return true;
}

void runLocation(JsonDocument& out, CardData& card) {
    float lat, lon;
    String label;
    if (!ipLocation(lat, lon, label)) {
        setError(out, "หาตำแหน่งจาก IP ไม่ได้");
        return;
    }
    JsonObject r = out["result"].to<JsonObject>();
    r["approx_location"] = label;
    r["latitude"] = lat;
    r["longitude"] = lon;
    r["note"] = "ตำแหน่งโดยประมาณจาก IP ของอินเทอร์เน็ต (บอร์ดไม่มี GPS)";
    card.kind = CardData::Text;
    copyStr(card.title, sizeof(card.title), "ตำแหน่งโดยประมาณ");
    copyStr(card.big, sizeof(card.big), label.substring(0, label.indexOf(',')).c_str());
    snprintf(card.body, sizeof(card.body), "%s • พิกัด %.4f, %.4f • จาก IP อินเทอร์เน็ต (ไม่มี GPS)", label.c_str(), lat,
             lon);
}

// ============================================================ Wikipedia

void runKnowledge(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const char* q = a["query"] | "";
    if (!*q) {
        setError(out, "ต้องระบุ query");
        return;
    }
    for (const char* lang : {"th", "en"}) {
        String url = String("https://") + lang +
                     ".wikipedia.org/w/api.php?action=query&format=json&generator=search&gsrlimit=1&prop=extracts"
                     "&exintro=1&explaintext=1&exchars=1000&redirects=1&gsrsearch=" +
                     urlEncode(q);
        String body;
        if (httpGet(url, body) != 200) continue;
        JsonDocument d(JsonPsramAllocator::instance());
        if (deserializeJson(d, body)) continue;
        for (JsonPairConst p : d["query"]["pages"].as<JsonObjectConst>()) {
            const char* title = p.value()["title"] | "";
            const char* extract = p.value()["extract"] | "";
            if (!*extract) continue;
            JsonObject r = out["result"].to<JsonObject>();
            r["title"] = title;
            r["summary"] = extract;
            r["source"] = String(lang) + ".wikipedia.org";
            r["note"] = "สรุปจาก Wikipedia (จอแสดงประกอบ) เล่าสาระสำคัญให้ครบตามที่ผู้ใช้ถาม";
            card.kind = CardData::Text;
            snprintf(card.title, sizeof(card.title), "Wikipedia (%s)", lang);
            copyStr(card.big, sizeof(card.big), title);
            copyStr(card.body, sizeof(card.body), extract);
            return;
        }
    }
    setError(out, "ไม่พบข้อมูลใน Wikipedia");
}

// ============================================================ market overview

void runMarketOverview(JsonObjectConst a, JsonDocument& out, CardData& card) {
    (void)a;
    struct Item {
        const char* sym;
        const char* name;
    };
    static const Item kItems[] = {
        {"SET:SET", "ดัชนี SET"},        {"SP:SPX", "S&P 500"},          {"NASDAQ:NDX", "Nasdaq 100"},
        {"OANDA:XAUUSD", "ทองคำ"},       {"NYMEX:CL1!", "น้ำมันดิบ WTI"}, {"BINANCE:BTCUSDT", "บิทคอยน์"},
        {"FX_IDC:USDTHB", "ดอลลาร์/บาท"},
    };
    std::vector<String> tickers;
    for (const Item& i : kItems) tickers.push_back(i.sym);
    JsonDocument d(JsonPsramAllocator::instance());
    if (!scanTickers(tickers, "[\"close\",\"change\",\"current_session\"]", d)) {
        setError(out, "ดึงข้อมูลตลาดไม่สำเร็จ");
        return;
    }
    card.kind = CardData::Quotes;
    copyStr(card.title, sizeof(card.title), "ภาพรวมตลาด");
    JsonArray arr = out["result"]["markets"].to<JsonArray>();
    for (const Item& i : kItems) {
        for (JsonObjectConst row : d["data"].as<JsonArrayConst>()) {
            if (strcmp(row["s"] | "", i.sym) != 0) continue;
            CardData::Row& r = card.row[card.rows++];
            copyStr(r.name, sizeof(r.name), i.name);
            r.value = row["d"][0] | 0.0f;
            r.changePct = row["d"][1] | 0.0f;
            JsonObject o = arr.add<JsonObject>();
            o["name"] = i.name;
            o["price"] = r.value;
            o["change_pct"] = r.changePct;
            const char* session = row["d"][2] | "";
            if (*session) o["market_open"] = !strcmp(session, "market");  // closed: the last close
        }
    }
    // crypto sentiment (alternative.me Fear & Greed)
    String body;
    if (httpGet("https://api.alternative.me/fng/?limit=1", body) == 200) {
        JsonDocument f(JsonPsramAllocator::instance());
        if (!deserializeJson(f, body)) {
            const char* v = f["data"][0]["value"] | "";
            const char* cls = f["data"][0]["value_classification"] | "";
            if (*v) {
                out["result"]["crypto_fear_greed"] = atoi(v);
                out["result"]["crypto_fear_greed_label"] = cls;
                snprintf(card.subtitle, sizeof(card.subtitle), "TradingView • ดัชนีกลัว/โลภคริปโต %s (%s)", v, cls);
            }
        }
    }
    if (!card.subtitle[0]) copyStr(card.subtitle, sizeof(card.subtitle), "TradingView • เปลี่ยนแปลงวันนี้");
    out["result"]["note"] = "จอแสดงภาพรวมประกอบ เล่าครบทุกรายการพร้อมตัวเลข ตัวที่เปลี่ยนแรงสุดก่อน แล้วสรุปภาพรวมตลาด "
                            "ตลาดที่ market_open=false คือราคาปิดล่าสุด";
}

// ============================================================ technical analysis

void runMarketAnalysis(JsonObjectConst a, JsonDocument& out, CardData& card) {
    String symbol = a["symbol"] | "";
    String tf = a["timeframe"] | "1d";
    const bool fundamentals = a["fundamentals"] | false;
    if (symbol.isEmpty()) {
        setError(out, "ต้องระบุ symbol");
        return;
    }
    JsonDocument q(JsonPsramAllocator::instance());
    String used, name;
    tf.toLowerCase();
    if (tf == "multi" || tf == "all") {  // rating per timeframe
        static const char* const kTf[] = {"|15", "|60", "|240", "", "|1W"};
        static const char* const kLabel[] = {"15 นาที", "1 ชั่วโมง", "4 ชั่วโมง", "รายวัน", "รายสัปดาห์"};
        String fields = "close,change";
        for (const char* t : kTf) fields += String(",Recommend.All") + t + ",RSI" + t;
        if (!fetchSymbol(symbol, fields, q, used, name)) {
            setError(out, "ไม่พบสัญลักษณ์นี้ใน TradingView");
            return;
        }
        card.kind = CardData::Analysis;
        copyStr(card.title, sizeof(card.title), name.c_str());
        copyStr(card.subtitle, sizeof(card.subtitle), "สัญญาณเทคนิครวมทุกกรอบเวลา • TradingView");
        JsonObject r = out["result"].to<JsonObject>();
        r["symbol"] = used;
        r["price"] = q["close"] | 0.0;
        JsonArray arr = r["timeframes"].to<JsonArray>();
        for (int i = 0; i < 5; i++) {
            const String key = String("Recommend.All") + kTf[i];
            if (q[key].isNull()) continue;
            const double v = q[key].as<double>();
            CardData::Row& row = card.row[card.rows++];
            copyStr(row.name, sizeof(row.name), kLabel[i]);
            copyStr(row.extra, sizeof(row.extra), ratingTh(v));
            row.color = ratingColor(v);
            JsonObject o = arr.add<JsonObject>();
            o["timeframe"] = kLabel[i];
            o["rating"] = ratingTh(v);
            o["score"] = v;
            o["rsi"] = q[String("RSI") + kTf[i]] | 0.0;
            if (!kTf[i][0]) card.gauge = v;
        }
        snprintf(card.big, sizeof(card.big), "%.5g (%+.2f%%)", q["close"] | 0.0, q["change"] | 0.0);
        r["note"] = "จอแสดงประกอบ เล่าแนวโน้มทุกกรอบเวลาพร้อมตัวเลขสำคัญ แล้วสรุปภาพรวม แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ตั้งแจ้งเตือนราคา วิเคราะห์เพิ่ม เทียบตัวอื่น ข่าวที่กระทบ) ห้ามปิดด้วยคำเตือนการลงทุน";
        return;
    }

    const char* sfx = tfSuffix(tf);
    static const char* const kInd[] = {"close", "change", "RSI", "MACD.macd", "MACD.signal", "Stoch.K", "EMA20",
                                       "EMA50", "EMA200", "ADX", "BB.upper", "BB.lower", "Recommend.All",
                                       "Recommend.MA", "Recommend.Other"};
    String fields = "description";
    for (const char* f : kInd) fields += String(",") + f + sfx;
    if (fundamentals) {
        fields += ",price_earnings_ttm,earnings_per_share_basic_ttm,market_cap_basic,price_book_fq,"
                  "dividends_yield_current,sector";
    }
    if (!fetchSymbol(symbol, fields, q, used, name)) {
        setError(out, "ไม่พบสัญลักษณ์นี้ใน TradingView");
        return;
    }
    auto f = [&](const char* k) -> double { return q[String(k) + sfx] | NAN; };
    const double close = f("close"), rsi = f("RSI"), macd = f("MACD.macd"), sig = f("MACD.signal"), stoch = f("Stoch.K");
    const double e20 = f("EMA20"), e50 = f("EMA50"), e200 = f("EMA200"), adx = f("ADX");
    const double bbU = f("BB.upper"), bbL = f("BB.lower"), rec = f("Recommend.All");

    JsonObject r = out["result"].to<JsonObject>();
    r["symbol"] = used;
    r["name"] = name;
    r["timeframe"] = tf;
    r["price"] = close;
    r["change_pct"] = f("change");
    if (!isnan(rec)) {
        r["rating"] = ratingTh(rec);
        r["rating_score"] = rec;
        r["rating_ma"] = f("Recommend.MA");
        r["rating_oscillators"] = f("Recommend.Other");
    }
    if (!isnan(rsi)) r["rsi"] = rsi;
    if (!isnan(macd)) r["macd"] = macd, r["macd_signal"] = sig;
    if (!isnan(stoch)) r["stoch_k"] = stoch;
    if (!isnan(e20)) r["ema20"] = e20, r["ema50"] = e50, r["ema200"] = e200;
    if (!isnan(adx)) r["adx"] = adx;
    if (!isnan(bbU)) r["bb_upper"] = bbU, r["bb_lower"] = bbL;

    card.kind = CardData::Analysis;
    card.gauge = isnan(rec) ? -9 : rec;
    snprintf(card.title, sizeof(card.title), "%s • วิเคราะห์ %s", name.c_str(), tf.c_str());
    copyStr(card.subtitle, sizeof(card.subtitle), "TradingView • ตัวชี้วัดทางเทคนิค");
    snprintf(card.big, sizeof(card.big), "%.6g (%+.2f%%)", close, f("change"));
    auto add = [&](const char* label, const char* text, uint16_t color) {
        if (card.rows >= CardData::kMaxRows) return;
        CardData::Row& row = card.row[card.rows++];
        copyStr(row.name, sizeof(row.name), label);
        copyStr(row.extra, sizeof(row.extra), text);
        row.color = color;
    };
    char buf[32];
    if (!isnan(rsi)) {
        snprintf(buf, sizeof(buf), "%.0f%s", rsi, rsi >= 70 ? " ซื้อมาก" : rsi <= 30 ? " ขายมาก" : "");
        add("RSI", buf, rsi >= 70 ? kDown : rsi <= 30 ? kUp : 0xFFFF);
    }
    if (!isnan(macd)) add("MACD", macd >= sig ? "เหนือสัญญาณ" : "ใต้สัญญาณ", macd >= sig ? kUp : kDown);
    if (!isnan(stoch) && !fundamentals) {
        snprintf(buf, sizeof(buf), "%.0f", stoch);
        add("Stoch", buf, stoch >= 80 ? kDown : stoch <= 20 ? kUp : 0xFFFF);
    }
    if (!isnan(e20) && !fundamentals) add("EMA20", close >= e20 ? "ราคาเหนือ" : "ราคาใต้", close >= e20 ? kUp : kDown);
    if (!isnan(e50)) add("EMA50", close >= e50 ? "ราคาเหนือ" : "ราคาใต้", close >= e50 ? kUp : kDown);
    if (!isnan(e200)) add("EMA200", close >= e200 ? "ราคาเหนือ" : "ราคาใต้", close >= e200 ? kUp : kDown);
    if (!isnan(adx) && !fundamentals) {
        snprintf(buf, sizeof(buf), "%.0f %s", adx, adx >= 25 ? "มีเทรนด์" : "ไซด์เวย์");
        add("ADX", buf, adx >= 25 ? kOrange : kDim);
    }
    if (fundamentals) {
        JsonObject fu = r["fundamentals"].to<JsonObject>();
        if (!q["price_earnings_ttm"].isNull()) {
            fu["pe"] = q["price_earnings_ttm"];
            snprintf(buf, sizeof(buf), "%.2f", q["price_earnings_ttm"].as<double>());
            add("P/E", buf, 0xFFFF);
        }
        if (!q["price_book_fq"].isNull()) {
            fu["pb"] = q["price_book_fq"];
            snprintf(buf, sizeof(buf), "%.2f", q["price_book_fq"].as<double>());
            add("P/BV", buf, 0xFFFF);
        }
        if (!q["dividends_yield_current"].isNull()) {
            fu["dividend_yield_pct"] = q["dividends_yield_current"];
            snprintf(buf, sizeof(buf), "%.2f%%", q["dividends_yield_current"].as<double>());
            add("ปันผล", buf, 0xFFFF);
        }
        if (!q["market_cap_basic"].isNull()) {
            fu["market_cap"] = q["market_cap_basic"];
            fmtNum(buf, sizeof(buf), q["market_cap_basic"].as<double>());
            add("มูลค่าตลาด", buf, 0xFFFF);
        }
        if (!q["earnings_per_share_basic_ttm"].isNull()) fu["eps"] = q["earnings_per_share_basic_ttm"];
        if (!q["sector"].isNull()) fu["sector"] = q["sector"];
    }
    r["note"] = "จอแสดงประกอบ เล่าภาพรวมพร้อมตัวเลขสำคัญ (ราคา สัญญาณ ตัวชี้วัด) แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ตั้งแจ้งเตือนราคา วิเคราะห์เพิ่ม เทียบตัวอื่น ข่าวที่กระทบ) ห้ามปิดด้วยคำเตือนการลงทุน";
}

// ============================================================ scanner

void runMarketScan(JsonObjectConst a, JsonDocument& out, CardData& card) {
    String market = a["market"] | "thailand";
    String type = a["type"] | "gainers";
    int limit = a["limit"] | 6;
    if (limit < 1) limit = 1;
    if (limit > 8) limit = 8;
    market.toLowerCase();
    if (market != "thailand" && market != "america" && market != "crypto") market = "thailand";
    String filter = "[";
    if (market == "thailand") {
        filter += "{\"left\":\"close\",\"operation\":\"egreater\",\"right\":1},"
                  "{\"left\":\"volume\",\"operation\":\"greater\",\"right\":200000},"
                  "{\"left\":\"type\",\"operation\":\"equal\",\"right\":\"stock\"}";
    } else if (market == "america") {
        filter += "{\"left\":\"close\",\"operation\":\"egreater\",\"right\":2},"
                  "{\"left\":\"volume\",\"operation\":\"greater\",\"right\":1000000},"
                  "{\"left\":\"type\",\"operation\":\"equal\",\"right\":\"stock\"}";
    } else {
        filter += "{\"left\":\"exchange\",\"operation\":\"equal\",\"right\":\"BINANCE\"},"
                  "{\"left\":\"name\",\"operation\":\"match\",\"right\":\"USDT$\"},"
                  "{\"left\":\"volume\",\"operation\":\"greater\",\"right\":100000}";
    }
    String sortBy = "change", order = "desc";
    const char* label = "ขึ้นแรง";
    if (type == "losers") order = "asc", label = "ลงแรง";
    else if (type == "volume") sortBy = "relative_volume_10d_calc", label = "วอลุ่มพุ่ง",
                                filter += ",{\"left\":\"change\",\"operation\":\"greater\",\"right\":2}";
    else if (type == "oversold") sortBy = "RSI", order = "asc", label = "RSI ขายมากเกิน",
                                  filter += ",{\"left\":\"RSI\",\"operation\":\"less\",\"right\":30}";
    else if (type == "overbought") sortBy = "RSI", label = "RSI ซื้อมากเกิน",
                                    filter += ",{\"left\":\"RSI\",\"operation\":\"greater\",\"right\":70}";
    filter += "]";
    const String body = String("{\"filter\":") + filter + ",\"options\":{\"lang\":\"en\"},\"markets\":[\"" + market +
                        "\"],\"columns\":[\"name\",\"close\",\"change\",\"RSI\",\"description\"],\"sort\":{\"sortBy\":\"" +
                        sortBy + "\",\"sortOrder\":\"" + order + "\"},\"range\":[0," + String(limit * 3) + "]}";
    String resp;
    if (httpRequest(String("https://scanner.tradingview.com/") + market + "/scan", resp, body.c_str()) != 200) {
        setError(out, "สแกนตลาดไม่สำเร็จ");
        return;
    }
    JsonDocument d(JsonPsramAllocator::instance());
    if (deserializeJson(d, resp)) {
        setError(out, "ข้อมูลสแกนอ่านไม่ได้");
        return;
    }
    static const char* const kMarketTh[] = {"หุ้นไทย", "หุ้นสหรัฐ", "คริปโต"};
    const int mi = market == "thailand" ? 0 : market == "america" ? 1 : 2;
    card.kind = CardData::Quotes;
    snprintf(card.title, sizeof(card.title), "%s %s", kMarketTh[mi], label);
    copyStr(card.subtitle, sizeof(card.subtitle), "TradingView scanner • วันนี้");
    JsonArray arr = out["result"]["symbols"].to<JsonArray>();
    for (JsonObjectConst row : d["data"].as<JsonArrayConst>()) {
        if (card.rows >= limit) break;
        const char* nm = row["d"][0] | "";
        if (strchr(nm, '.')) continue;  // NVDR / foreign board duplicates (PTT.R, PTT.F)
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), nm);
        r.value = row["d"][1] | 0.0f;
        r.changePct = row["d"][2] | 0.0f;
        JsonObject o = arr.add<JsonObject>();
        o["symbol"] = row["s"] | "";
        o["name"] = row["d"][4] | nm;
        o["price"] = r.value;
        o["change_pct"] = r.changePct;
        if (!row["d"][3].isNull()) o["rsi"] = row["d"][3];
    }
    if (!card.rows) {
        card.kind = CardData::None;
        setError(out, "ไม่พบรายการตามเงื่อนไข");
        return;
    }
    out["result"]["note"] = "จอแสดงรายการประกอบ เล่าทุกตัวในรายการพร้อมตัวเลข (อันดับต้นก่อน) แล้วชวนต่อด้วยข้อเสนอ 1 อย่าง (ตั้งแจ้งเตือนราคา วิเคราะห์เพิ่ม เทียบตัวอื่น ข่าวที่กระทบ) ห้ามปิดด้วยคำเตือนการลงทุน";
}

// ============================================================ economic calendar (ForexFactory feed)

namespace {
String gCalendar;
uint32_t gCalendarAt = 0;
}  // namespace

void runEconomicCalendar(JsonObjectConst a, JsonDocument& out, CardData& card) {
    String range = a["range"] | "week";
    String impact = a["impact"] | "high";
    const uint32_t now = nowEpoch();
    if (!now) {  // "today", "past" and the day names all need the date
        setError(out, "นาฬิกายังไม่ซิงก์เวลา ลองใหม่อีกครู่");
        return;
    }
    if (gCalendar.isEmpty() || millis() - gCalendarAt > 1800000) {  // the feed allows few requests: cache 30 min
        String body;
        if (httpGet("https://nfs.faireconomy.media/ff_calendar_thisweek.json", body) != 200) {
            if (gCalendar.isEmpty()) {
                setError(out, "ดึงปฏิทินเศรษฐกิจไม่สำเร็จ");
                return;
            }
        } else {
            gCalendar = body;
            gCalendarAt = millis();
        }
    }
    JsonDocument d(JsonPsramAllocator::instance());
    if (deserializeJson(d, gCalendar)) {
        setError(out, "ปฏิทินเศรษฐกิจอ่านไม่ได้");
        return;
    }
    const timefmt::Civil today = timefmt::toCivil(now, 7 * 3600);
    card.kind = CardData::Table;
    snprintf(card.title, sizeof(card.title), "ปฏิทินเศรษฐกิจ%s", range == "today" ? "วันนี้" : range == "tomorrow" ? "พรุ่งนี้" : "สัปดาห์นี้");
    copyStr(card.subtitle, sizeof(card.subtitle), "ForexFactory • เวลาไทย • แดง = สำคัญมาก");
    JsonArray arr = out["result"]["events"].to<JsonArray>();
    int pastCount = 0;
    for (JsonObjectConst e : d.as<JsonArrayConst>()) {
        const char* imp = e["impact"] | "";
        const bool high = !strcmp(imp, "High");
        if (!high && !(impact == "medium" && !strcmp(imp, "Medium"))) continue;
        int y, mo, dd, hh, mi, oh = 0, om = 0;
        char sign = '+';
        const char* date = e["date"] | "";
        if (sscanf(date, "%d-%d-%dT%d:%d:%*d%c%d:%d", &y, &mo, &dd, &hh, &mi, &sign, &oh, &om) < 5) continue;
        const int off = (oh * 60 + om) * (sign == '-' ? -1 : 1);
        const int64_t utc = epochOf(y, mo, dd, hh, mi) - off * 60;
        const bool past = utc + 7200 < (int64_t)now;  // 2-h grace
        if (past && range != "week") continue;
        const timefmt::Civil th = timefmt::toCivil((uint32_t)utc, 7 * 3600);
        const int dayDiff = (int)(daysFromCivil(th.year, th.month, th.day) - daysFromCivil(today.year, today.month, today.day));
        if (range == "today" && dayDiff != 0) continue;
        if (range == "tomorrow" && dayDiff != 1) continue;
        JsonObject o = arr.add<JsonObject>();
        char when[32];
        snprintf(when, sizeof(when), "%s %02d:%02d", thaiWeekdayShort(weekday(th.year, th.month, th.day)), th.hour, th.minute);
        char ymd[12];
        snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d", th.year, th.month, th.day);
        o["date"] = ymd;
        o["when_th"] = when;
        o["currency"] = e["country"] | "";
        o["event"] = e["title"] | "";
        o["impact"] = imp;
        o["forecast"] = e["forecast"] | "";
        o["previous"] = e["previous"] | "";
        if (past) {
            o["done"] = true;
            pastCount++;
            continue;  // the card lists what is still ahead
        }
        if (card.rows < CardData::kMaxRows) {
            CardData::Row& r = card.row[card.rows++];
            snprintf(r.name, sizeof(r.name), "%s %s", (const char*)(e["country"] | ""), (const char*)(e["title"] | ""));
            copyStr(r.extra, sizeof(r.extra), when);
            r.color = high ? kDown : kOrange;
        }
    }
    if (pastCount) {
        snprintf(card.subtitle, sizeof(card.subtitle), "ForexFactory • เวลาไทย • ผ่านไปแล้ว %d", pastCount);
    }
    if (!card.rows) {
        copyStr(card.body, sizeof(card.body), arr.size() ? "ข่าวสำคัญของสัปดาห์นี้ประกาศไปหมดแล้ว" : "ไม่มีข่าวเศรษฐกิจสำคัญในช่วงนี้");
        card.kind = CardData::Text;
        out["result"]["note"] = arr.size() ? "ข่าวสำคัญในช่วงนี้ประกาศไปแล้วทั้งหมด (done=true) ข้อมูลนี้ไม่มีตัวเลขจริง (actual) ห้ามเดาผล"
                                           : "ไม่มีข่าวเศรษฐกิจสำคัญในช่วงที่ถาม (ข้อมูลครอบคลุมเฉพาะสัปดาห์นี้ อาทิตย์-เสาร์)";
        return;
    }
    out["result"]["note"] = "จอแสดงตารางประกอบ บอกเหตุการณ์ทุกรายการที่เกี่ยวกับที่ถามพร้อมเวลาไทย (สำคัญมากก่อน) ข้อมูลนี้ไม่มีตัวเลขจริง (actual) "
                            "ของข่าวที่ประกาศแล้ว (done=true) ห้ามเดาผล ถ้าถามผลให้ใช้ Google Search";
}

// ============================================================ position size

void runPositionSize(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const double balance = a["balance"] | 0.0, riskPct = a["risk_percent"] | 1.0;
    const double entry = a["entry"] | 0.0, stop = a["stop_loss"] | 0.0;
    String symbol = a["symbol"] | "";
    symbol.toUpperCase();
    double contract = a["contract_size"] | 0.0;
    if (balance <= 0 || entry <= 0 || stop <= 0 || entry == stop) {
        setError(out, "ต้องมี balance, entry, stop_loss (และ entry ต่างจาก stop_loss)");
        return;
    }
    if (contract <= 0) {
        if (symbol.indexOf("XAU") >= 0 || symbol.indexOf("GOLD") >= 0) contract = 100;       // oz per lot
        else if (symbol.indexOf("XAG") >= 0 || symbol.indexOf("SILVER") >= 0) contract = 5000;
        else if (symbol.length() == 6 && isalpha((unsigned char)symbol[0])) contract = 100000;  // FX pair
        else contract = 1;                                                                 // shares / coins
    }
    const double riskAmount = balance * riskPct / 100.0;
    const double distance = fabs(entry - stop);
    const double units = riskAmount / distance;
    const double lots = units / contract;
    JsonObject r = out["result"].to<JsonObject>();
    r["risk_amount"] = riskAmount;
    r["stop_distance"] = distance;
    r["units"] = units;
    r["contract_size"] = contract;
    r["lots"] = lots;
    r["note"] = "ประมาณการ: ราคาในสกุลเงินเดียวกับบัญชี ไม่รวม spread/ค่าธรรมเนียม";
    card.kind = CardData::Text;
    copyStr(card.title, sizeof(card.title), "ขนาดสัญญา (Position size)");
    if (contract > 1) snprintf(card.big, sizeof(card.big), "%.2f lot", lots);
    else snprintf(card.big, sizeof(card.big), "%.4g หน่วย", units);
    snprintf(card.body, sizeof(card.body), "ทุน %.2f • เสี่ยง %.2g%% = %.2f • เข้า %.5g ตัดขาดทุน %.5g (ห่าง %.5g) • 1 lot = %.0f หน่วย",
             balance, riskPct, riskAmount, entry, stop, distance, contract);
}

// ============================================================ notes (SD card)

namespace {
String filePath(const ToolEnv& env, const char* name) { return String(env.storageDir) + "/" + name; }

// A file that is there but cannot be read (damaged, card error) is not "empty": the next add used to save
// a new list over it and every old note / alert was gone (T073). It is left as it is for the user to fix.
bool loadStore(const String& path, JsonDocument& db, JsonDocument& out) {
    if (jsonfile::loadStatus(path.c_str(), db) != jsonfile::Status::Unreadable) return true;
    Serial.printf("[Tool] %s cannot be read - not touched (fix or remove it)\n", path.c_str());
    setError(out, "ไฟล์ข้อมูลเสียหรืออ่านไม่ได้ (ยังไม่ได้แก้ไขอะไร) บอกผู้ใช้ให้ตรวจการ์ด SD หรือไฟล์นี้");
    out["file"] = path;
    return false;
}
}  // namespace

void runNotes(const ToolEnv& env, JsonObjectConst a, JsonDocument& out, CardData& card) {
    if (!env.storageDir || !*env.storageDir) {
        setError(out, "ไม่มีที่เก็บข้อมูล (SD/แฟลช)");
        return;
    }
    const String path = filePath(env, "notes.json");
    JsonDocument db(JsonPsramAllocator::instance());
    if (!loadStore(path, db, out)) return;
    if (!db["notes"].is<JsonArray>()) db["notes"].to<JsonArray>();
    JsonArray notes = db["notes"];
    // titles cut by bytes before v3.4.2 could end in half a Thai character: invalid UTF-8 in the
    // toolResponse lets the server drop the session - repair them once
    bool repaired = false;
    for (JsonObject n : notes) {
        const char* t = n["title"] | "";
        const size_t len = strlen(t), ok = utf8::completeLength(t, len);
        if (ok < len) {
            n["title"] = String(t).substring(0, ok);
            repaired = true;
        }
    }
    if (repaired) jsonfile::save(path.c_str(), db);
    String action = a["action"] | "list";
    const char* title = a["title"] | "";
    const char* content = a["content"] | "";
    const char* query = a["query"] | "";
    const int id = a["id"] | 0;
    auto matches = [&](JsonObjectConst n) { return notebook::matches(n, id, query); };
    if (action == "add") {
        if (!*content) {
            setError(out, "ต้องมี content");
            return;
        }
        int next = db["next"] | 1;
        while (notes.size() >= 100) notes.remove(0);
        JsonObject n = notes.add<JsonObject>();
        n["id"] = next;
        // no title: the start of the text, cut on a character boundary (Thai = 3 bytes a character)
        n["title"] = *title ? String(title) : String(content).substring(0, utf8::clampToBoundary(content, strlen(content), 40));
        n["text"] = content;
        n["ts"] = nowEpoch();
        db["next"] = next + 1;
        jsonfile::save(path.c_str(), db);
        out["result"]["saved_id"] = next;
        card.kind = CardData::Text;
        copyStr(card.title, sizeof(card.title), "บันทึกโน้ตแล้ว");
        copyStr(card.big, sizeof(card.big), n["title"] | "");
        copyStr(card.body, sizeof(card.body), content);
        return;
    }
    if (action == "delete") {
        // one note at a time: a query used to delete every note that contained it (T073)
        if (!id && !*query) {
            setError(out, "ต้องระบุ id ของโน้ต (ดูจาก action list) หรือ query");
            return;
        }
        const int i = notebook::pickForDelete(notes, id, query);
        if (i == notebook::kNone) {
            setError(out, "ไม่พบโน้ต");
            return;
        }
        if (i == notebook::kAmbiguous) {
            setError(out, "หลายโน้ตตรงกับคำนี้ ถามผู้ใช้ว่าลบอันไหน แล้วลบด้วย id");
            JsonArray cand = out["candidates"].to<JsonArray>();
            for (JsonObjectConst n : notes) {
                if (!matches(n) || cand.size() >= 10) continue;
                JsonObject o = cand.add<JsonObject>();
                o["id"] = n["id"];
                o["title"] = n["title"];
            }
            return;
        }
        JsonObject gone = notes[(size_t)i];
        out["result"]["deleted_id"] = gone["id"] | 0;
        out["result"]["title"] = String(gone["title"] | "");
        notes.remove((size_t)i);
        if (!jsonfile::save(path.c_str(), db)) setError(out, "บันทึกไฟล์โน้ตไม่สำเร็จ");
        return;
    }
    if (action == "read" || action == "search") {
        JsonArray found = out["result"]["notes"].to<JsonArray>();
        card.kind = CardData::Table;
        snprintf(card.title, sizeof(card.title), "โน้ต: %s", *query ? query : "ที่เลือก");
        for (JsonObjectConst n : notes) {
            if (!matches(n)) continue;
            found.add(n);
            if (card.rows < CardData::kMaxRows) {
                CardData::Row& r = card.row[card.rows++];
                copyStr(r.name, sizeof(r.name), n["text"] | "");
                snprintf(r.extra, sizeof(r.extra), "#%d", n["id"] | 0);
            }
        }
        if (found.size() == 1) {  // one note: show it in full
            card.kind = CardData::Text;
            copyStr(card.big, sizeof(card.big), found[0]["title"] | "");
            copyStr(card.body, sizeof(card.body), found[0]["text"] | "");
        }
        if (!found.size()) setError(out, "ไม่พบโน้ต");
        return;
    }
    // list (newest first)
    JsonArray list = out["result"]["notes"].to<JsonArray>();
    card.kind = CardData::Table;
    copyStr(card.title, sizeof(card.title), "โน้ตของฉัน");
    snprintf(card.subtitle, sizeof(card.subtitle), "ทั้งหมด %u รายการ (ใหม่สุดก่อน)", (unsigned)notes.size());
    for (size_t i = notes.size(); i-- > 0;) {
        JsonObjectConst n = notes[i];
        if (list.size() < 20) {
            JsonObject o = list.add<JsonObject>();
            o["id"] = n["id"];
            o["title"] = n["title"];
        }
        if (card.rows < CardData::kMaxRows) {
            CardData::Row& r = card.row[card.rows++];
            copyStr(r.name, sizeof(r.name), n["title"] | "");
            snprintf(r.extra, sizeof(r.extra), "#%d", n["id"] | 0);
        }
    }
}

// ============================================================ price alerts

bool hasPriceAlerts(const ToolEnv& env) {
    if (!env.storageDir || !*env.storageDir) return false;
    JsonDocument db(JsonPsramAllocator::instance());
    return jsonfile::load(filePath(env, "alerts.json").c_str(), db) && db["alerts"].size() > 0;
}

void runPriceAlert(const ToolEnv& env, JsonObjectConst a, JsonDocument& out, CardData& card) {
    if (!env.storageDir || !*env.storageDir) {
        setError(out, "ไม่มีที่เก็บข้อมูล (SD/แฟลช)");
        return;
    }
    const String path = filePath(env, "alerts.json");
    JsonDocument db(JsonPsramAllocator::instance());
    if (!loadStore(path, db, out)) return;
    if (!db["alerts"].is<JsonArray>()) db["alerts"].to<JsonArray>();
    JsonArray alerts = db["alerts"];
    String action = a["action"] | "list";
    if (action == "add") {
        const String sym = a["symbol"] | "";
        const double above = a["above"] | 0.0, below = a["below"] | 0.0;
        if (sym.isEmpty() || (above <= 0 && below <= 0)) {
            setError(out, "ต้องมี symbol และ above หรือ below");
            return;
        }
        JsonDocument q(JsonPsramAllocator::instance());
        String used, name;
        if (!fetchSymbol(sym, "close,description", q, used, name)) {
            setError(out, "ไม่พบสัญลักษณ์นี้ใน TradingView");
            return;
        }
        if (alerts.size() >= 10) {
            setError(out, "ตั้งได้สูงสุด 10 รายการ");
            return;
        }
        const int id = db["next"] | 1;
        JsonObject al = alerts.add<JsonObject>();
        al["id"] = id;
        al["symbol"] = used;
        al["name"] = name;
        if (above > 0) al["above"] = above;
        if (below > 0) al["below"] = below;
        db["next"] = id + 1;
        jsonfile::save(path.c_str(), db);
        JsonObject r = out["result"].to<JsonObject>();
        r["id"] = id;
        r["symbol"] = used;
        r["current_price"] = q["close"] | 0.0;
        r["note"] = "บอร์ดจะตรวจราคาทุก 2 นาทีและแจ้งเตือนด้วยเสียงเมื่อถึงเป้า";
        card.kind = CardData::Text;
        copyStr(card.title, sizeof(card.title), "ตั้งแจ้งเตือนราคาแล้ว");
        copyStr(card.big, sizeof(card.big), name.c_str());
        snprintf(card.body, sizeof(card.body), "ตอนนี้ %.6g • แจ้งเมื่อ%s%s%.6g • ตรวจทุก 2 นาที", q["close"] | 0.0,
                 above > 0 ? "ขึ้นถึง " : "ลงถึง ", "", above > 0 ? above : below);
        return;
    }
    if (action == "remove") {
        // no id used to mean "all": a model that passed the symbol instead of the id wiped every alert (T069)
        const int id = a["id"] | 0;
        if (!id) {
            setError(out, "ต้องระบุ id ของแจ้งเตือน (ดูจาก action list) หรือ id = -1 เพื่อลบทั้งหมด");
            return;
        }
        int removed = 0;
        for (size_t i = alerts.size(); i-- > 0;) {
            if (id < 0 || (alerts[i]["id"] | 0) == id) {
                alerts.remove(i);
                removed++;
            }
        }
        jsonfile::save(path.c_str(), db);
        out["result"]["removed"] = removed;
        return;
    }
    out["result"]["alerts"] = alerts;
    card.kind = CardData::Table;
    copyStr(card.title, sizeof(card.title), "แจ้งเตือนราคา");
    snprintf(card.subtitle, sizeof(card.subtitle), "%u รายการ • ตรวจทุก 2 นาที", (unsigned)alerts.size());
    for (JsonObjectConst al : alerts) {
        if (card.rows >= CardData::kMaxRows) break;
        CardData::Row& r = card.row[card.rows++];
        snprintf(r.name, sizeof(r.name), "#%d %s", al["id"] | 0, (const char*)(al["name"] | ""));
        if (!al["above"].isNull()) snprintf(r.extra, sizeof(r.extra), "≥ %.6g", al["above"].as<double>());
        else snprintf(r.extra, sizeof(r.extra), "≤ %.6g", al["below"].as<double>());
        r.color = al["above"].isNull() ? kDown : kUp;
    }
}

bool checkPriceAlerts(const ToolEnv& env, String& message) {
    const String path = filePath(env, "alerts.json");
    JsonDocument db(JsonPsramAllocator::instance());
    if (!jsonfile::load(path.c_str(), db) || !db["alerts"].size()) return false;
    std::vector<String> tickers;
    for (JsonObjectConst al : db["alerts"].as<JsonArrayConst>()) tickers.push_back(al["symbol"] | "");
    JsonDocument q(JsonPsramAllocator::instance());
    if (!scanTickers(tickers, "[\"close\"]", q)) return false;
    JsonArray alerts = db["alerts"];
    for (size_t i = 0; i < alerts.size(); i++) {
        JsonObject al = alerts[i];
        for (JsonObjectConst row : q["data"].as<JsonArrayConst>()) {
            if (strcmp(row["s"] | "", al["symbol"] | "") != 0) continue;
            const double close = row["d"][0] | 0.0;
            const bool up = !al["above"].isNull() && close >= al["above"].as<double>();
            const bool down = !al["below"].isNull() && close <= al["below"].as<double>();
            if (!up && !down) continue;
            char buf[200];
            snprintf(buf, sizeof(buf), "ราคา%s %s %.6g แล้ว (ตั้งเตือนไว้ที่ %.6g)", (const char*)(al["name"] | ""),
                     up ? "ขึ้นถึง" : "ลงถึง", close, up ? al["above"].as<double>() : al["below"].as<double>());
            message = buf;
            alerts.remove(i);
            jsonfile::save(path.c_str(), db);
            return true;
        }
    }
    return false;
}

}  // namespace webtools
