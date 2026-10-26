/**
 * @file WaterTools.cpp
 * @brief `water` tool: water level, rain and storms from ThaiWater (คลังข้อมูลน้ำแห่งชาติ) with graphic cards (T100)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Runs in the webTools task like the other live-data tools. Parsing lives in util/ThaiWater.h (host tests).
 * Answers are filtered while they are parsed (ArduinoJson Filter, PSRAM): a province's 24 h rain is ~350 KB
 * of JSON of which ~10 KB is kept.
 */

#include "../agent/ThaiProvinces.h"
#include "../util/ThaiWater.h"
#include "GzipHttpStream.h"
#include "WebToolsCommon.h"

namespace webtools {

namespace {

constexpr uint32_t kWaterTimeoutMs = 20000;     // a province: ~45 KB gzip (350 KB JSON), 1-3 s on this board
constexpr uint32_t kNationTimeoutMs = 40000;    // the whole country: ~600 KB gzip (4.8 MB JSON)
constexpr size_t kMaxRows = CardData::kMaxRows;
constexpr int kWaterRetries = 2;

/**
 * The rows of a result (8 stations / provinces, up to ~2.5 KB with the T106 name lengths) in PSRAM: the webTools
 * stack (12 KB) had ~1 KB left at its deepest.
 */
template <class T>
struct PsRows {
    T* p = (T*)psramMalloc(sizeof(T) * kMaxRows);
    PsRows() {
        if (p) for (size_t i = 0; i < kMaxRows; i++) new (&p[i]) T();
    }
    ~PsRows() { psramFree(p); }  // the row types are plain data
    PsRows(const PsRows&) = delete;
    PsRows& operator=(const PsRows&) = delete;
    explicit operator bool() const { return p != nullptr; }
    T& operator[](size_t i) { return p[i]; }
};  // new connections after a stall / failure (T104: ~1 in 3 stalled on a phone hotspot)

uint16_t levelColor(int level) {
    switch (level) {
        case 5: return 0xF800;  // overflowing the bank: red (ThaiWater's colours, lighter where black needs it)
        case 4: return 0x3D7F;  // high: blue
        case 3: return 0x2FE8;  // normal: green
        case 2: return 0xFE00;  // low: yellow
        case 1: return 0xDC05;  // critically low: dark gold
        default: return ui::DIM;
    }
}

/** Rain over 1 / 3 / 5 / 7 days (T106: ThaiWater keeps 3- and 5-day totals too). */
struct RainSpan {
    int days;
    const char* key;      // field of each station
    const char* path;     // endpoint (+ "?province_code=" for one province)
    const char* period;   // for the model
    const char* titleTh;  // card: "ฝน 24 ชม." / "ฝน 3 วัน"
    const char* inTh;     // "ในรอบ 24 ชั่วโมง" / "ในรอบ 3 วัน"
};

RainSpan rainSpan(int days) {
    if (days >= 6) return {7, "rain_7d", "provinces/rain7d", "last 7 days", "ฝน 7 วัน", "ในรอบ 7 วัน"};
    if (days >= 4) return {5, "rain_5d", "provinces/rain5d", "last 5 days", "ฝน 5 วัน", "ในรอบ 5 วัน"};
    if (days >= 2) return {3, "rain_3d", "provinces/rain3d", "last 3 days", "ฝน 3 วัน", "ในรอบ 3 วัน"};
    return {1, "rain_24h", "public/rain_24h", "last 24 hours", "ฝน 24 ชม.", "ในรอบ 24 ชั่วโมง"};
}

uint16_t rainColor(float mm24) {
    if (mm24 <= 10) return 0x3D7F;
    if (mm24 <= 35) return 0x2FE8;
    if (mm24 <= 90) return 0xFD20;
    return 0xF800;
}

/**
 * GET (gzip) + parse while it streams in, keeping only what `filter` names. false = `out` holds the error.
 * The stream object (TLS client + buffers, ~4 KB) lives in PSRAM, not on the 12 KB task stack.
 */
struct OpenStream {
    GzipHttpStream* s = nullptr;
    int code = 0;
    uint32_t t0 = 0;
};

/** Connected and the body is next, or nullptr with the error in `out`. Close with closeStream(). */
GzipHttpStream* openStream(const String& url, uint32_t timeoutMs, OpenStream& os, JsonDocument& out) {
    void* mem = psramMalloc(sizeof(GzipHttpStream));
    if (!mem) {
        setError(out, "out of memory");
        return nullptr;
    }
    os.s = new (mem) GzipHttpStream();
    os.t0 = millis();
    os.code = os.s->open(url, timeoutMs);
    if (os.code != 200) {
        // -11 = sent, but no answer in time (the board's stalls, T104): not "no Internet"
        setError(out, os.code == HTTPC_ERROR_READ_TIMEOUT ? "ThaiWater did not answer in time - try again"
                      : os.code < 0                        ? "ThaiWater (thaiwater.net) unreachable - check the Internet"
                                                           : "ThaiWater answered with an error");
        return nullptr;
    }
    // Stream::find() re-polls read() until this timeout once it returns -1; read() itself already waits (up to
    // the stall limit), so -1 means the end or a stall - no point spinning on it for another 40 s
    os.s->setTimeout(1);
    return os.s;
}

void closeStream(OpenStream& os, const String& url) {
    if (!os.s) return;
    Serial.printf("[Web] thaiwater %s -> %d in %lu ms (net %lu, inflate %lu; %u B on the wire, %u B JSON)\n",
                  url.substring(strlen(thaiwater::kBase)).c_str(), os.code, (unsigned long)(millis() - os.t0),
                  (unsigned long)os.s->netMs(), (unsigned long)os.s->inflateMs(), (unsigned)os.s->wireBytes(),
                  (unsigned)os.s->bodyBytes());
    os.s->~GzipHttpStream();
    psramFree(os.s);
    os.s = nullptr;
}

/**
 * Another try after a stalled / failed connection, while there is time left (T104: downloads on the board stopped
 * dead at random - the same request went through in 5 s the next time).
 */
bool retryAfter(const OpenStream& os, bool stalled, uint32_t t0, uint32_t timeoutMs, const String& url) {
    if (!stalled && os.code >= 0) return false;  // a real answer (HTTP error / bad data): asking again won't help
    if (millis() - t0 > timeoutMs * 3 / 5) return false;  // a stall is noticed in ~6-8 s: room for 2 more tries
    Serial.printf("[Web] thaiwater %s %s - trying again\n", url.substring(strlen(thaiwater::kBase)).c_str(),
                  stalled ? "stalled" : "failed");
    return true;
}

/** A province-sized answer (<= ~400 KB of JSON) parsed whole, keeping only what `filter` names. */
bool fetchFiltered(const String& url, const JsonDocument& filter, JsonDocument& doc, JsonDocument& out,
                   uint32_t timeoutMs = kWaterTimeoutMs) {
    const uint32_t t0 = millis();
    for (int attempt = 0;; attempt++) {
        OpenStream os;
        bool ok = false, stalled = false;
        if (GzipHttpStream* s = openStream(url, timeoutMs, os, out)) {
            GzipHttpStream::Reader rd = s->reader();
            setStage("water: parse");
            const DeserializationError e = deserializeJson(doc, rd, DeserializationOption::Filter(filter));
            if (e) setError(out, s->timedOut() ? "ThaiWater is too slow right now - try again" : "ThaiWater sent data that could not be read");
            ok = !e;
            stalled = s->stalled();
        }
        closeStream(os, url);
        if (ok || attempt >= kWaterRetries || !retryAfter(os, stalled, t0, timeoutMs, url)) return ok;
        out.clear();
        doc.clear();
    }
}

/** "แม่น้ำปิง" -> "ปิง" (ThaiWater calls basins "ลุ่มน้ำปิง"), "อ.จอมทอง" -> "จอมทอง". */
void placeQuery(const char* place, char* out, size_t cap) {
    static const char* const kPrefixes[] = {"แม่น้ำ", "ลุ่มน้ำ", "ลำน้ำ", "สถานี", "อำเภอ", "อ.", "ตำบล", "ต.", "จังหวัด", "จ."};
    while (*place == ' ') place++;
    for (const char* p : kPrefixes) {
        const size_t n = strlen(p);
        if (!strncmp(place, p, n)) {
            place += n;
            while (*place == ' ') place++;
            break;
        }
    }
    thaiwater::copyText(out, cap, place);
    for (size_t n = strlen(out); n > 0 && out[n - 1] == ' '; n--) out[n - 1] = '\0';
}

/**
 * Remove a separate province word from the station query: "ตากใบ จ.นราธิวาส" -> "ตากใบ". A word that only starts
 * with the name stays ("เมืองเชียงใหม่" is a district; there "เมือง" comes before it, not a space).
 */
void dropProvinceWord(char* query, const char* provinceTh) {
    static const char* const kLead[] = {"จังหวัด", "จ."};
    const size_t n = strlen(provinceTh);
    for (char* at = strstr(query, provinceTh); at; at = strstr(at + 1, provinceTh)) {
        char* start = at;
        for (const char* lead : kLead) {
            const size_t ln = strlen(lead);
            if ((size_t)(start - query) >= ln && !strncmp(start - ln, lead, ln)) {
                start -= ln;
                break;
            }
        }
        const unsigned char* after = (const unsigned char*)at + n;
        const bool wordStart = start == query || start[-1] == ' ';
        const bool wordEnd = !*after || *after == ' ';
        if (!wordStart || !wordEnd) continue;
        memmove(start, at + n, strlen(at + n) + 1);
        break;
    }
    // tidy the spaces left behind
    char* w = query;
    for (const char* r = query; *r; r++) {
        if (*r == ' ' && (w == query || w[-1] == ' ')) continue;
        *w++ = *r;
    }
    while (w > query && w[-1] == ' ') w--;
    *w = '\0';
}

/** The province from `province`, or a province named (as a whole word) inside `place`. */
const thaiplace::Province* findProvinceIn(const char* province, const char* place) {
    if (const thaiplace::Province* p = thaiplace::findProvince(province)) return p;
    return thaiplace::findProvinceInText(place);
}

String cardTime(const char* when) {  // "2026-10-25 22:30" -> "25/10 22:30"
    char buf[24];
    timefmt::shortStamp(when, buf, sizeof(buf));
    return String(buf);
}

// ---------------------------------------------------------------- level (+ graph of one station)

/** One station's last 1-2 days -> card graph + result block. */
void stationGraph(const thaiwater::WaterLevel& w, const char* provinceTh, JsonObject res, CardData& card) {
    char start[12], end[12];
    if (!thaiwater::dayBefore(w.time, 1, start, sizeof(start)) || !thaiwater::dayBefore(w.time, 0, end, sizeof(end))) return;
    String url = String(thaiwater::kBase) + "public/waterlevel_graph?station_type=tele_waterlevel&station_id=" +
                 String(w.id) + "&start_date=" + start + "&end_date=" + end + "%2023:59";
    JsonDocument filter;
    filter["data"]["graph_data"][0]["datetime"] = true;
    filter["data"]["graph_data"][0]["value"] = true;
    filter["data"]["min_bank"] = true;
    filter["data"]["warning_level"] = true;
    filter["data"]["critical_level"] = true;
    JsonDocument doc(JsonPsramAllocator::instance());
    JsonDocument err;
    if (!fetchFiltered(url, filter, doc, err)) return;  // the list still answers
    JsonObjectConst data = doc["data"];
    CardData::Series& g = card.g;
    g.n = (uint8_t)thaiwater::downsample(data["graph_data"], g.v, CardData::Series::kMax, g.from, g.to, sizeof(g.from));
    if (g.n < 2) return;
    g.ref[0] = thaiwater::num(data["min_bank"]);
    g.ref[1] = thaiwater::num(data["warning_level"]);
    g.ref[2] = thaiwater::num(data["critical_level"]);
    copyStr(g.unit, sizeof(g.unit), "ม.รทก.");
    g.color = levelColor(w.level);
    card.kind = CardData::Graph;
    snprintf(card.title, sizeof(card.title), "ระดับน้ำ %s", w.name);
    // "ลุ่มน้ำปิง อ.เมืองเชียงใหม่ จ.เชียงใหม่" did not fit: "น้ำปิง • อ.เมืองเชียงใหม่ • P.1"
    const char* river = !strncmp(w.river, "ลุ่มน้ำ", strlen("ลุ่มน้ำ")) ? w.river + strlen("ลุ่ม") : w.river;
    snprintf(card.subtitle, sizeof(card.subtitle), "%s • %s%s%s", river, *w.place ? w.place : provinceTh,
             *w.code ? " • " : "", w.code);
    if (isnan(w.percent)) snprintf(card.big, sizeof(card.big), "%.2f ม.", g.v[g.n - 1]);
    else snprintf(card.big, sizeof(card.big), "%.0f%% %s", w.percent, thaiwater::levelTh(w.level));
    // the bank is often metres above a calm river - off the graph's scale, so it is said in words
    if (!isnan(w.toBank)) {
        snprintf(card.body, sizeof(card.body), w.toBank >= 0 ? "ต่ำกว่าตลิ่ง %.2f ม." : "สูงกว่าตลิ่ง %.2f ม.",
                 fabsf(w.toBank));
    }

    float lo = g.v[0], hi = g.v[0];
    for (int i = 1; i < g.n; i++) {
        lo = g.v[i] < lo ? g.v[i] : lo;
        hi = g.v[i] > hi ? g.v[i] : hi;
    }
    JsonObject gr = res["graph"].to<JsonObject>();
    gr["station"] = w.name;
    gr["from"] = g.from;
    gr["to"] = g.to;
    gr["now_m_msl"] = serialized(String(g.v[g.n - 1], 2));
    gr["lowest_m_msl"] = serialized(String(lo, 2));
    gr["highest_m_msl"] = serialized(String(hi, 2));
    const int back = g.n > 6 ? 6 : g.n - 1;  // ~5 h on a 2-day graph: rising or falling now
    gr["change_last_hours_m"] = serialized(String(g.v[g.n - 1] - g.v[g.n - 1 - back], 2));
    if (!isnan(g.ref[0])) gr["bank_m_msl"] = serialized(String(g.ref[0], 2));
    if (!isnan(g.ref[1])) gr["warning_m_msl"] = serialized(String(g.ref[1], 2));
    if (!isnan(g.ref[2])) gr["critical_m_msl"] = serialized(String(g.ref[2], 2));
}

void runLevel(const thaiplace::Province& prov, const char* query, JsonDocument& out, CardData& card) {
    JsonDocument filter;
    JsonObject f = filter["data"][0].to<JsonObject>();
    for (const char* k : {"waterlevel_datetime", "waterlevel_msl", "storage_percent", "diff_wl_bank"}) f[k] = true;
    f["station"]["id"] = true;
    f["station"]["tele_station_name"] = true;
    f["station"]["tele_station_oldcode"] = true;
    f["basin"]["basin_name"] = true;
    f["geocode"]["amphoe_name"] = true;
    f["geocode"]["tumbon_name"] = true;
    JsonDocument doc(JsonPsramAllocator::instance());
    if (!fetchFiltered(String(thaiwater::kBase) + "public/waterlevel?province_code=" + prov.code, filter, doc, out)) return;

    PsRows<thaiwater::WaterLevel> st;
    if (!st) return setError(out, "out of memory");
    size_t matched = 0;
    const size_t n = thaiwater::pickWaterLevels(doc["data"], query, st.p, kMaxRows, matched, prov.code == thaiwater::kBangkok);
    if (!n) {
        setError(out, *query ? "no water level station with that name in this province - try the district or river, "
                               "or leave place empty for the whole province"
                             : "no telemetry water level station reports in this province");
        return;
    }
    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "ThaiWater (คลังข้อมูลน้ำแห่งชาติ) telemetry";
    res["province"] = prov.th;
    res["stations_matched"] = matched;
    res["situation_scale"] = "percent of the river bank: >100 overflow, >70 high, >30 normal, >10 low, <=10 critically low";
    JsonArray arr = res["stations"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = st[i].name;
        if (st[i].code[0]) o["code"] = st[i].code;
        o["river"] = st[i].river;
        o["district"] = st[i].place;
        if (!isnan(st[i].msl)) o["water_level_m_msl"] = serialized(String(st[i].msl, 2));
        if (!isnan(st[i].percent)) o["percent_of_bank"] = serialized(String(st[i].percent, 0));
        o["situation"] = thaiwater::levelTh(st[i].level);
        if (!isnan(st[i].toBank)) o["below_bank_m"] = serialized(String(st[i].toBank, 2));  // negative = above it
        o["time"] = st[i].time;
    }

    // one station asked for (or only one there): its graph; otherwise the list as bars (% of the bank).
    // A word that is only part of the top station's name ("ปิง" for the whole basin) keeps the bars.
    if (matched == 1 || (*query && (!strcmp(st[0].name, query) || !strcmp(st[0].code, query)))) {
        stationGraph(st[0], prov.th, res, card);
    }
    if (card.kind == CardData::Graph) return;
    card.kind = CardData::Bars;
    card.g.ref[0] = 100;  // dashed line at the bank
    snprintf(card.title, sizeof(card.title), "ระดับน้ำ จ.%s", prov.th);
    snprintf(card.subtitle, sizeof(card.subtitle), "%s%s%u สถานี • %% ความจุลำน้ำ • %s", query, *query ? " • " : "",
             (unsigned)matched, cardTime(st[0].time).c_str());
    card.rows = (uint8_t)n;
    for (size_t i = 0; i < n; i++) {
        CardData::Row& r = card.row[i];
        copyStr(r.name, sizeof(r.name), st[i].name);
        r.value = isnan(st[i].percent) ? 0 : st[i].percent;
        r.color = levelColor(st[i].level);
        if (isnan(st[i].percent)) snprintf(r.extra, sizeof(r.extra), "-");
        else snprintf(r.extra, sizeof(r.extra), "%.0f%%", st[i].percent);
    }
}

// ---------------------------------------------------------------- rain

void runRain(const thaiplace::Province& prov, const char* query, int days, JsonDocument& out, CardData& card) {
    const RainSpan span = rainSpan(days);
    const bool multiDay = span.days > 1;  // several days: ThaiWater says which dates they are
    const char* key = span.key;
    JsonDocument filter;
    JsonObject f = filter["data"][0].to<JsonObject>();
    f[key] = true;
    f["rainfall_datetime"] = true;
    if (multiDay) {  // the days counted (T106: "last 7 days" is 2026-10-20..25 on the 26th - say which)
        f["rainfall_start_date"] = true;
        f["rainfall_end_date"] = true;
    }
    f["station"]["tele_station_name"] = true;
    f["geocode"]["amphoe_name"] = true;
    f["geocode"]["tumbon_name"] = true;
    JsonDocument doc(JsonPsramAllocator::instance());
    const String url = String(thaiwater::kBase) + span.path +
                       "?province_code=" + prov.code;
    if (!fetchFiltered(url, filter, doc, out)) return;

    PsRows<thaiwater::Rain> top;
    if (!top) return setError(out, "out of memory");
    thaiwater::RainSummary sum;
    const size_t n = thaiwater::pickRain(doc["data"], key, query, top.p, kMaxRows, sum, prov.code == thaiwater::kBangkok);
    if (!sum.stations) {
        setError(out, *query ? "no rain station with that name in this province - leave place empty for the whole province"
                             : "no rain station reports in this province");
        return;
    }
    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "ThaiWater (คลังข้อมูลน้ำแห่งชาติ) rain gauges";
    res["province"] = prov.th;
    res["period"] = span.period;
    if (multiDay) {
        res["period_from"] = doc["data"][0]["rainfall_start_date"] | "";
        res["period_to"] = doc["data"][0]["rainfall_end_date"] | "";
    }
    res["time"] = sum.time;
    res["stations"] = sum.stations;
    res["stations_with_rain"] = sum.wet;
    res["max_mm"] = serialized(String(sum.max, 1));
    res["average_mm"] = serialized(String(sum.avg, 1));
    if (!multiDay) res["max_class"] = thaiwater::rainTh(sum.max);
    JsonArray arr = res["wettest"].to<JsonArray>();
    for (size_t i = 0; i < n && top[i].mm > 0.1f; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["station"] = top[i].name;
        o["district"] = top[i].place;
        o["mm"] = serialized(String(top[i].mm, 1));
    }

    card.kind = CardData::Bars;
    snprintf(card.title, sizeof(card.title), "%s จ.%s", span.titleTh, prov.th);
    snprintf(card.subtitle, sizeof(card.subtitle), "เฉลี่ย %.1f มม. • ฝนตก %u/%u สถานี", sum.avg, (unsigned)sum.wet,
             (unsigned)sum.stations);
    if (multiDay) {  // + the dates covered
        const String dates = cardTime(doc["data"][0]["rainfall_start_date"] | "") + "–" +
                             cardTime(doc["data"][0]["rainfall_end_date"] | "");
        const size_t used = strlen(card.subtitle);
        snprintf(card.subtitle + used, sizeof(card.subtitle) - used, " • %s", dates.c_str());
    }
    for (size_t i = 0; i < n && top[i].mm > 0.1f; i++) {
        CardData::Row& r = card.row[card.rows++];
        copyStr(r.name, sizeof(r.name), top[i].name);  // with the district it no longer fits the row (board test)
        r.value = top[i].mm;
        r.color = rainColor(top[i].mm / span.days);  // the colour of an average day
        snprintf(r.extra, sizeof(r.extra), "%.1f", top[i].mm);
    }
    if (!card.rows) {  // a dry day: say so instead of an empty chart
        card.kind = CardData::Text;
        copyStr(card.big, sizeof(card.big), "ไม่มีฝน");
        snprintf(card.body, sizeof(card.body), "สถานีวัดฝน %u แห่งใน จ.%s ไม่มีฝน%s", (unsigned)sum.stations, prov.th,
                 span.inTh);
    }
}

// ---------------------------------------------------------------- whole country: the 8 provinces with the highest station

/**
 * No province given: rain (24 h / 7 days) or water level of every station in Thailand, grouped by province, the
 * 8 provinces whose highest station is highest. `query` (a river / station word) narrows it ("ปิง" = the Ping basin).
 */
void runRank(bool level, const char* query, int days, JsonDocument& out, CardData& card) {
    const RainSpan span = rainSpan(days);
    const bool multiDay = !level && span.days > 1;
    const char* key = level ? "storage_percent" : span.key;
    // one station's filter: the array is walked element by element (see thaiwater::ProvinceRanker)
    JsonDocument filter;
    filter[key] = true;
    filter[level ? "waterlevel_datetime" : "rainfall_datetime"] = true;
    if (multiDay) {
        filter["rainfall_start_date"] = true;
        filter["rainfall_end_date"] = true;
    }
    filter["station"]["tele_station_name"] = true;
    filter["geocode"]["province_code"] = true;
    filter["geocode"]["province_name"] = true;
    filter["geocode"]["amphoe_name"] = true;
    if (level || *query) {
        filter["station"]["tele_station_oldcode"] = true;
        filter["basin"]["basin_name"] = true;
    }
    void* slotMem = psramMalloc(sizeof(thaiwater::ProvinceRank) * thaiwater::kProvinceSlots);
    if (!slotMem) {
        setError(out, "out of memory");
        return;
    }
    thaiwater::ProvinceRanker ranker((thaiwater::ProvinceRank*)slotMem, key, level, query);
    const String url = String(thaiwater::kBase) + (level ? "public/waterlevel" : span.path);
    char when[17] = "", from[11] = "", to[11] = "";
    bool ok = false;
    const uint32_t t0 = millis();
    for (int attempt = 0;; attempt++) {
        OpenStream os;
        bool stalled = false;
        if (GzipHttpStream* s = openStream(url, kNationTimeoutMs, os, out)) {
            if (!s->find("\"data\":[")) {
                setError(out, s->timedOut() ? "ThaiWater is too slow right now - try again"
                                            : "ThaiWater sent data that could not be read");
            } else {
                JsonDocument one(JsonPsramAllocator::instance());
                setStage("water: parse stations");
                ok = true;
                do {
                    GzipHttpStream::Reader rd = s->reader();
                    const DeserializationError e = deserializeJson(one, rd, DeserializationOption::Filter(filter));
                    if (e) {
                        ok = false;
                        setError(out, s->timedOut() ? "ThaiWater is too slow right now - try again"
                                                    : "ThaiWater sent data that could not be read");
                        break;
                    }
                    ranker.add(one.as<JsonObjectConst>());
                    if (!when[0]) {
                        thaiwater::copyText(when, sizeof(when), one[level ? "waterlevel_datetime" : "rainfall_datetime"] | "");
                        thaiwater::copyText(from, sizeof(from), one["rainfall_start_date"] | "");
                        thaiwater::copyText(to, sizeof(to), one["rainfall_end_date"] | "");
                    }
                } while (s->findUntil(",", "]"));
            }
            stalled = s->stalled();
        }
        closeStream(os, url);
        if (ok || attempt >= kWaterRetries || !retryAfter(os, stalled, t0, kNationTimeoutMs, url)) break;
        out.clear();
        ranker = thaiwater::ProvinceRanker((thaiwater::ProvinceRank*)slotMem, key, level, query);  // start over
        when[0] = 0;
    }
    if (!ok) {
        psramFree(slotMem);
        return;
    }
    PsRows<thaiwater::ProvinceRank> top;
    if (!top) {
        psramFree(slotMem);
        return setError(out, "out of memory");
    }
    size_t provinces = 0;
    const size_t n = ranker.top(top.p, kMaxRows, provinces);
    const size_t stations = ranker.stations();
    psramFree(slotMem);
    if (!n) {
        setError(out, *query ? "no station matches that name anywhere in Thailand" : "ThaiWater has no data right now");
        return;
    }

    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "ThaiWater (คลังข้อมูลน้ำแห่งชาติ), every station in Thailand";
    if (level) res["ranking"] = "provinces by their most filled river station (% of the bank)";
    else res["ranking"] = String("provinces by their wettest station, ") + span.period;
    if (*query) res["filter"] = query;
    if (from[0]) res["period_from"] = (const char*)from;  // const char*: copied (a char array is kept by pointer)
    if (to[0]) res["period_to"] = (const char*)to;
    res["provinces_with_data"] = provinces;
    res["stations"] = stations;
    if (level) res["situation_scale"] = "percent of the river bank: >100 overflow, >70 high, >30 normal, >10 low, <=10 critically low";
    JsonArray arr = res["provinces"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["province"] = top[i].name;
        if (level) {
            o["max_percent_of_bank"] = serialized(String(top[i].value, 0));
            o["situation"] = thaiwater::levelTh(thaiwater::levelFromPercent(top[i].value));
            o["stations_overflowing"] = top[i].over;
            o["stations_high_or_overflowing"] = top[i].count;
            if (top[i].river[0]) o["river"] = top[i].river;
        } else {
            o["max_mm"] = serialized(String(top[i].value, 1));
            if (!multiDay) o["max_class"] = thaiwater::rainTh(top[i].value);
            o["stations_with_rain"] = top[i].count;
        }
        o["station"] = top[i].station;
        o["district"] = top[i].place;
        o["stations"] = top[i].stations;
    }

    card.kind = CardData::Bars;
    if (level) {
        card.g.ref[0] = 100;  // dashed line at the bank
        copyStr(card.title, sizeof(card.title), "ระดับน้ำสูงสุด 8 จังหวัด");
    } else {
        snprintf(card.title, sizeof(card.title), "%s สูงสุด 8 จังหวัด", span.titleTh);
    }
    // several days: the dates they cover ("21/10–25/10"), not the day it was fetched
    const String dates = from[0] ? cardTime(from) + "–" + cardTime(to) : cardTime(when);
    snprintf(card.subtitle, sizeof(card.subtitle), "%s%s%u สถานี • %s", query, *query ? " • " : "", (unsigned)stations,
             dates.c_str());
    card.rows = (uint8_t)n;
    for (size_t i = 0; i < n; i++) {
        CardData::Row& r = card.row[i];
        copyStr(r.name, sizeof(r.name), top[i].name);
        r.value = top[i].value > 0 ? top[i].value : 0;
        if (level) {
            r.color = levelColor(thaiwater::levelFromPercent(top[i].value));
            snprintf(r.extra, sizeof(r.extra), "%.0f%%", top[i].value);
        } else {
            r.color = rainColor(top[i].value / span.days);
            snprintf(r.extra, sizeof(r.extra), "%.1f", top[i].value);
        }
    }
}

// ---------------------------------------------------------------- storms

void runStorm(JsonDocument& out, CardData& card) {
    JsonDocument filter;
    filter["data"][0]["storm_name"] = true;
    JsonDocument doc(JsonPsramAllocator::instance());
    if (!fetchFiltered(String(thaiwater::kBase) + "public/storm_data", filter, doc, out)) return;
    JsonObject res = out["result"].to<JsonObject>();
    res["source"] = "ThaiWater (คลังข้อมูลน้ำแห่งชาติ)";
    JsonArray names = res["storms_near_thailand"].to<JsonArray>();
    String list;
    for (JsonObjectConst s : doc["data"].as<JsonArrayConst>()) {
        const char* name = s["storm_name"] | "";
        if (!*name) continue;
        names.add(name);
        if (list.length()) list += ", ";
        list += name;
    }
    card.kind = CardData::Text;
    copyStr(card.title, sizeof(card.title), "พายุใกล้ประเทศไทย");
    copyStr(card.subtitle, sizeof(card.subtitle), "คลังข้อมูลน้ำแห่งชาติ");
    if (names.size()) {
        snprintf(card.big, sizeof(card.big), "%u ลูก", (unsigned)names.size());
        copyStr(card.body, sizeof(card.body), list.c_str());
    } else {
        copyStr(card.big, sizeof(card.big), "ไม่มีพายุ");
        copyStr(card.body, sizeof(card.body), "ตอนนี้ไม่มีพายุเข้าใกล้ประเทศไทย");
        res["note"] = "no storm near Thailand right now";
    }
}

}  // namespace

void runWater(JsonObjectConst a, JsonDocument& out, CardData& card) {
    const char* action = a["action"] | "level";
    if (!strcmp(action, "storm")) return runStorm(out, card);
    const char* place = a["place"] | "";
    const thaiplace::Province* prov = findProvinceIn(a["province"] | "", place);
    char query[64];
    placeQuery(place, query, sizeof(query));
    if (!prov) {  // no province: the whole country, one row per province (T100)
        const char* asked = a["province"] | "";
        if (*asked && strcmp(asked, "ทั่วประเทศ") && strcasecmp(asked, "thailand") && strcmp(asked, "ประเทศไทย")) {
            setError(out, "unknown province - use a Thai province name, or leave province empty for all of Thailand");
            return;
        }
        return runRank(strcmp(action, "rain") != 0, query, a["days"] | 1, out, card);
    }
    // place = the province itself -> the whole province ("เมืองเชียงใหม่" is a district and stays)
    if (!strcmp(query, prov->th) || !strcasecmp(query, prov->en)) query[0] = '\0';
    dropProvinceWord(query, prov->th);  // "ตากใบ นราธิวาส" -> "ตากใบ" (board test: the name was searched for)
    if (!strcmp(action, "rain")) return runRain(*prov, query, a["days"] | 1, out, card);
    runLevel(*prov, query, out, card);
}

}  // namespace webtools
