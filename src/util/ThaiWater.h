/**
 * @file ThaiWater.h
 * @brief Parsing ThaiWater (คลังข้อมูลน้ำแห่งชาติ, thaiwater.net) answers for the `water` tool (T100)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Source: the public API the thaiwater.net pages call themselves (no key), base
 * https://api-v3.thaiwater.net/api/v1/thaiwater30/ - not a documented API, so every field is read defensively:
 *   public/waterlevel?province_code=50        telemetry water level of a province (~80 KB)
 *   public/waterlevel_graph?station_type=tele_waterlevel&station_id=..&start_date=..&end_date=..  (~15 KB/day)
 *   public/rain_24h?province_code=50          24 h rain per station (~350 KB)
 *   provinces/rain7d?province_code=50         7-day rain per station (~300 KB)
 *   public/storm_data                         storms near Thailand (~1 KB)
 * Situation by % of the bank (ThaiWater's own scale): >100 overflow, >70 high, >30 normal, >10 low, <=10 critical low.
 * Portable: host tests run it on trimmed real answers.
 */

#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "TimeFmt.h"

namespace thaiwater {

constexpr const char* kBase = "https://api-v3.thaiwater.net/api/v1/thaiwater30/";

/** 0 = unknown, 1 critical low .. 5 overflowing the bank. */
inline int levelFromPercent(float pct) {
    if (isnan(pct)) return 0;
    if (pct > 100) return 5;
    if (pct > 70) return 4;
    if (pct > 30) return 3;
    if (pct > 10) return 2;
    return 1;
}

inline const char* levelTh(int level) {
    static const char* const kText[] = {"ไม่มีข้อมูล", "น้ำน้อยวิกฤติ", "น้ำน้อย", "น้ำปกติ", "น้ำมาก", "น้ำล้นตลิ่ง"};
    return kText[level < 0 || level > 5 ? 0 : level];
}

/** A number that may come as 12.5, "12.50" or null -> NAN when missing. */
inline float num(JsonVariantConst v) {
    if (v.is<float>() || v.is<int>()) return v.as<float>();
    const char* s = v.as<const char*>();
    if (!s || !*s) return NAN;
    char* end = nullptr;
    const float f = strtof(s, &end);
    return end == s ? NAN : f;
}

/** An integer that may come as 50 or "50" (0 = missing). */
inline int intOf(JsonVariantConst v) {
    if (const char* s = v.as<const char*>()) return atoi(s);
    return v | 0;
}

/**
 * Keep the `max` best items, best first: insert `item` into `out` (holding `n`) by `key(item)` (higher = better,
 * equal keys keep arrival order). Returns the new count.
 */
template <typename T, typename Key>
inline size_t insertTop(T* out, size_t n, size_t max, const T& item, Key key) {
    const float k = key(item);
    size_t pos = n;
    while (pos > 0 && key(out[pos - 1]) < k) pos--;
    if (pos >= max) return n;
    const size_t last = n < max ? n : max - 1;
    for (size_t i = last; i > pos; i--) out[i] = out[i - 1];
    out[pos] = item;
    return n < max ? n + 1 : n;
}

inline void copyText(char* dst, size_t cap, const char* src) {
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) {  // never split a UTF-8 character
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/** The Thai name of a {"th":..,"en":..} object (English when there is no Thai). */
inline const char* nameOf(JsonVariantConst o) {
    const char* th = o["th"] | "";
    return *th ? th : (o["en"] | "");
}

/**
 * "จอมทอง" -> "อ.จอมทอง"; a full "เขตบางรัก" / "อำเภอ…" stays. ThaiWater names Bangkok's districts without
 * "เขต" ("บางขุนเทียน"): `bangkok` gives them "เขต" instead of "อ.".
 */
inline void districtLabel(char* dst, size_t cap, const char* amphoe, bool bangkok = false) {
    if (!*amphoe || !strncmp(amphoe, "เขต", strlen("เขต")) || !strncmp(amphoe, "อำเภอ", strlen("อำเภอ"))) {
        copyText(dst, cap, amphoe);
        return;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), bangkok ? "เขต%s" : "อ.%s", amphoe);
    copyText(dst, cap, buf);
}

/** Bangkok's province code: its districts are "เขต", not "อ.". */
constexpr int kBangkok = 10;

/** Case-sensitive (Thai has no case) substring test on several fields; empty query = matches. */
inline bool matches(const char* query, const char* a, const char* b = "", const char* c = "", const char* d = "") {
    if (!query || !*query) return true;
    return strstr(a, query) || strstr(b, query) || strstr(c, query) || strstr(d, query);
}

// ---------------------------------------------------------------- water level of a province

struct WaterLevel {
    long id = 0;            // station id for public/waterlevel_graph
    char name[64] = "";     // "บ้านสบสอย"
    char code[16] = "";     // "P.73"
    char river[96] = "";    // basin: "ลุ่มน้ำปิง" ("ลุ่มน้ำภาคใต้ฝั่งตะวันตก" = 72 bytes)
    char place[64] = "";    // "อ.จอมทอง"
    char time[17] = "";     // "2026-10-25 18:00"
    float msl = NAN;        // m above mean sea level (ม.รทก.)
    float percent = NAN;    // % of the bank
    float toBank = NAN;     // m below the bank (+) / above it (-)
    int level = 0;          // levelFromPercent
};

/**
 * Stations of `data` (public/waterlevel -> "data") whose name / code / basin / district contain `query`
 * ("" = all), most filled first (overflowing ones lead), at most `max`. `matched` = how many matched.
 */
inline size_t pickWaterLevels(JsonArrayConst data, const char* query, WaterLevel* out, size_t max, size_t& matched,
                              bool bangkok = false) {
    matched = 0;
    size_t n = 0;
    // a station named or coded exactly like the query is the one meant ("P.1", not also P.13 / P.17)
    bool exact = false;
    for (JsonObjectConst s : data) {
        if (query && *query && (!strcmp(s["station"]["tele_station_oldcode"] | "", query) ||
                                !strcmp(nameOf(s["station"]["tele_station_name"]), query))) {
            exact = true;
        }
    }
    for (JsonObjectConst s : data) {
        JsonObjectConst st = s["station"];
        const char* name = nameOf(st["tele_station_name"]);
        const char* code = st["tele_station_oldcode"] | "";
        const char* basin = nameOf(s["basin"]["basin_name"]);
        const char* amphoe = nameOf(s["geocode"]["amphoe_name"]);
        const char* tumbon = nameOf(s["geocode"]["tumbon_name"]);
        if (!*name || (!matches(query, name, code, basin, amphoe) && !matches(query, tumbon))) continue;
        if (exact && strcmp(code, query) && strcmp(name, query)) continue;
        matched++;
        WaterLevel w;
        w.id = st["id"] | 0L;
        copyText(w.name, sizeof(w.name), name);
        copyText(w.code, sizeof(w.code), code);
        copyText(w.river, sizeof(w.river), basin);
        districtLabel(w.place, sizeof(w.place), amphoe, bangkok);
        copyText(w.time, sizeof(w.time), s["waterlevel_datetime"] | "");
        w.msl = num(s["waterlevel_msl"]);
        w.percent = num(s["storage_percent"]);
        w.toBank = num(s["diff_wl_bank"]);
        w.level = levelFromPercent(w.percent);
        // keep the `max` most filled (unknown % sorts last)
        n = insertTop(out, n, max, w, [](const WaterLevel& x) { return isnan(x.percent) ? -1e9f : x.percent; });
    }
    return n;
}

// ---------------------------------------------------------------- graph of one station

/** "2026-10-25 18:00" (a station's last measurement) -> "2026-10-24" for back = 1. false = not a date. */
inline bool dayBefore(const char* when, int back, char* out, size_t cap) {
    int y = 0, m = 0, d = 0;
    if (!when || sscanf(when, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    const int64_t day = timefmt::daysFromCivil(y, m, d) - back;
    const timefmt::Civil c = timefmt::toCivil((uint32_t)(day * 86400), 0);
    snprintf(out, cap, "%04d-%02d-%02d", c.year, c.month, c.day);
    return true;
}

/**
 * public/waterlevel_graph -> "data": graph_data [{datetime, value}] averaged into `max` buckets (the board
 * draws ~60 points). `first` / `last` get the times of the first and last measurement ("2026-10-25 22:30").
 * Returns the points written (0 = no measurement).
 */
inline size_t downsample(JsonArrayConst points, float* out, size_t max, char* first, char* last, size_t timeCap) {
    size_t total = 0;
    for (JsonObjectConst p : points) {
        if (!isnan(num(p["value"]))) total++;
    }
    first[0] = last[0] = '\0';
    if (!total || !max) return 0;
    const size_t buckets = total < max ? total : max;
    size_t i = 0, b = 0, inBucket = 0;
    float sum = 0;
    for (JsonObjectConst p : points) {
        const float v = num(p["value"]);
        if (isnan(v)) continue;
        if (i == 0) copyText(first, timeCap, p["datetime"] | "");
        copyText(last, timeCap, p["datetime"] | "");
        sum += v;
        inBucket++;
        i++;
        if (i * buckets >= (b + 1) * total) {  // bucket b is full
            out[b++] = sum / inBucket;
            sum = 0;
            inBucket = 0;
        }
    }
    return b;
}

// ---------------------------------------------------------------- rain of a province

struct Rain {
    char name[64] = "";
    char place[64] = "";
    float mm = NAN;
};

struct RainSummary {
    size_t stations = 0;  // with a value
    size_t wet = 0;       // > 0.1 mm
    float max = 0;
    float avg = 0;        // over the stations with a value
    char time[17] = "";   // rainfall_datetime of the first station
};

/**
 * Rain stations (public/rain_24h or provinces/rain7d -> "data"; `key` "rain_24h" / "rain_7d") matching `query`,
 * wettest first, at most `max`.
 */
inline size_t pickRain(JsonArrayConst data, const char* key, const char* query, Rain* out, size_t max, RainSummary& sum,
                       bool bangkok = false) {
    sum = RainSummary();
    size_t n = 0;
    double total = 0;
    for (JsonObjectConst s : data) {
        const float mm = num(s[key]);
        if (isnan(mm)) continue;
        const char* name = nameOf(s["station"]["tele_station_name"]);
        const char* amphoe = nameOf(s["geocode"]["amphoe_name"]);
        const char* tumbon = nameOf(s["geocode"]["tumbon_name"]);
        if (!matches(query, name, amphoe, tumbon)) continue;
        if (!sum.stations) copyText(sum.time, sizeof(sum.time), s["rainfall_datetime"] | "");
        sum.stations++;
        if (mm > 0.1f) sum.wet++;
        if (mm > sum.max) sum.max = mm;
        total += mm;
        Rain r;
        copyText(r.name, sizeof(r.name), name);
        districtLabel(r.place, sizeof(r.place), amphoe, bangkok);
        r.mm = mm;
        n = insertTop(out, n, max, r, [](const Rain& x) { return x.mm; });
    }
    sum.avg = sum.stations ? (float)(total / sum.stations) : 0;
    return n;
}

// ---------------------------------------------------------------- whole country: one row per province

/** One province of a nationwide ranking: its highest station (rain mm or % of the bank). */
struct ProvinceRank {
    int code = 0;              // province code (10 = Bangkok)
    char name[48] = "";        // "สุพรรณบุรี"
    float value = NAN;         // the highest station's mm / % of the bank
    char station[64] = "";     // that station
    char place[64] = "";       // its district
    char river[96] = "";       // its basin (water level)
    uint16_t stations = 0;     // stations with a value in the province
    uint16_t count = 0;        // rain: > 0.1 mm; water level: > 70 % (high or overflowing)
    uint16_t over = 0;         // water level: > 100 % (overflowing the bank)
};

constexpr int kProvinceSlots = 100;  // province codes are 10..96

/**
 * Nationwide ranking fed one station at a time (T100): the nationwide answer is parsed station by station into a
 * small reused document - a whole 1.3-4.8 MB document made ArduinoJson 7's string de-duplication quadratic
 * (1.3 MB took 21 s on the board). Each slot keeps copies of its highest station, so nothing points into the
 * parsed JSON. `slots` = kProvinceSlots ProvinceRank (~25 KB: the caller puts it in PSRAM).
 * `key` = "rain_24h" / "rain_7d" / "storage_percent"; `isLevel` counts > 70 % and > 100 % instead of wet stations;
 * `query` ("" = all) keeps the stations whose name / code / basin / district contain it.
 */
class ProvinceRanker {
public:
    ProvinceRanker(ProvinceRank* slots, const char* key, bool isLevel, const char* query)
        : _slots(slots), _key(key), _level(isLevel), _query(query ? query : "") {
        for (int i = 0; i < kProvinceSlots; i++) _slots[i] = ProvinceRank();
    }

    void add(JsonObjectConst s) {
        const float v = num(s[_key]);
        if (isnan(v)) return;
        const int code = intOf(s["geocode"]["province_code"]);  // "50" today, read 50 too
        if (code <= 0 || code >= kProvinceSlots) return;  // Laos / Myanmar / "other" stations
        JsonObjectConst st = s["station"];
        const char* name = nameOf(st["tele_station_name"]);
        const char* basin = nameOf(s["basin"]["basin_name"]);
        const char* amphoe = nameOf(s["geocode"]["amphoe_name"]);
        if (*_query && !matches(_query, name, st["tele_station_oldcode"] | "", basin, amphoe)) return;
        ProvinceRank& p = _slots[code];
        _stations++;
        p.stations++;
        if (_level) {
            if (v > 70) p.count++;
            if (v > 100) p.over++;
        } else if (v > 0.1f) {
            p.count++;
        }
        if (isnan(p.value) || v > p.value) {
            p.code = code;
            p.value = v;
            copyText(p.name, sizeof(p.name), nameOf(s["geocode"]["province_name"]));
            copyText(p.station, sizeof(p.station), name);
            districtLabel(p.place, sizeof(p.place), amphoe, code == kBangkok);
            copyText(p.river, sizeof(p.river), basin);
        }
    }

    size_t stations() const { return _stations; }

    /** The `max` provinces with the highest station, highest first; `provinces` = provinces with data. */
    size_t top(ProvinceRank* out, size_t max, size_t& provinces) const {
        provinces = 0;
        size_t n = 0;
        for (int code = 0; code < kProvinceSlots; code++) {
            const ProvinceRank& p = _slots[code];
            if (isnan(p.value)) continue;
            provinces++;
            n = insertTop(out, n, max, p, [](const ProvinceRank& x) { return x.value; });
        }
        return n;
    }

private:
    ProvinceRank* _slots;
    const char* _key;
    bool _level;
    const char* _query;
    size_t _stations = 0;
};

/** ThaiWater's rain colours: what one day of rain means ("ฝนตกหนัก" from 35.1 mm, Thai Met Dept scale). */
inline const char* rainTh(float mm24) {
    if (mm24 <= 0.1f) return "ไม่มีฝน";
    if (mm24 <= 10) return "ฝนเล็กน้อย";
    if (mm24 <= 35) return "ฝนปานกลาง";
    if (mm24 <= 90) return "ฝนหนัก";
    return "ฝนหนักมาก";
}

}  // namespace thaiwater
