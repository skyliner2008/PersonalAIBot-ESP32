/**
 * @file Units.h
 * @brief Unit conversion for the `convert_units` tool, incl. Thai units (header-only, host-testable)
 *
 * Linear units are stored as a factor to the category's base unit; temperature is converted
 * through Celsius. Names are matched case-insensitively with spaces and dots removed, in
 * English or Thai ("km", "กิโลเมตร", "ไร่", "ตารางวา", "บาททอง", "กม./ชม.").
 */

#pragma once

#include <ctype.h>
#include <string.h>

namespace units {

enum class Cat : unsigned char { Length, Area, Mass, Volume, Speed, Time, Data, Energy, Temperature };

struct Unit {
    const char* names;  // '|' separated aliases (already normalised: lower case, no spaces / dots)
    Cat cat;
    double factor;      // to the base unit (m, m2, kg, l, m/s, s, B, J); temperature: 0=C 1=F 2=K
    const char* label;  // display name
};

inline const Unit* table(size_t& n) {
    static const Unit kUnits[] = {
        // length (m)
        {"mm|มิลลิเมตร|มม", Cat::Length, 0.001, "mm"},
        {"cm|เซนติเมตร|ซม|เซน", Cat::Length, 0.01, "cm"},
        {"m|meter|metre|เมตร|ม", Cat::Length, 1, "m"},
        {"km|kilometer|กิโลเมตร|กม|กิโล", Cat::Length, 1000, "km"},
        {"in|inch|inches|นิ้ว", Cat::Length, 0.0254, "inch"},
        {"ft|foot|feet|ฟุต", Cat::Length, 0.3048, "ft"},
        {"yd|yard|หลา", Cat::Length, 0.9144, "yd"},
        {"mi|mile|miles|ไมล์", Cat::Length, 1609.344, "mile"},
        {"nmi|nauticalmile|ไมล์ทะเล", Cat::Length, 1852, "nmi"},
        {"วา", Cat::Length, 2, "วา"},
        {"ศอก", Cat::Length, 0.5, "ศอก"},
        {"คืบ", Cat::Length, 0.25, "คืบ"},
        // area (m2)
        {"m2|sqm|ตารางเมตร|ตรม", Cat::Area, 1, "m²"},
        {"km2|ตารางกิโลเมตร|ตรกม", Cat::Area, 1e6, "km²"},
        {"ha|hectare|เฮกตาร์|เฮกแตร์", Cat::Area, 10000, "ha"},
        {"acre|acres|เอเคอร์", Cat::Area, 4046.8564224, "acre"},
        {"ft2|sqft|ตารางฟุต|ตรฟ", Cat::Area, 0.09290304, "ft²"},
        {"ไร่|rai", Cat::Area, 1600, "ไร่"},
        {"งาน|ngan", Cat::Area, 400, "งาน"},
        {"ตารางวา|ตรว|wah2|sqwa", Cat::Area, 4, "ตารางวา"},
        // mass (kg)
        {"mg|มิลลิกรัม", Cat::Mass, 1e-6, "mg"},
        {"g|gram|กรัม", Cat::Mass, 0.001, "g"},
        {"kg|kilogram|กิโลกรัม", Cat::Mass, 1, "kg"},
        {"t|ton|tonne|ตัน", Cat::Mass, 1000, "ton"},
        {"lb|lbs|pound|ปอนด์", Cat::Mass, 0.45359237, "lb"},
        {"oz|ounce|ออนซ์", Cat::Mass, 0.028349523125, "oz"},
        {"ozt|troyounce|ทรอยออนซ์", Cat::Mass, 0.0311034768, "troy oz"},
        {"บาททอง|บาท|goldbaht|baht", Cat::Mass, 0.015244, "บาท (ทองคำแท่ง 15.244 g)"},
        {"สลึง", Cat::Mass, 0.003811, "สลึง (ทอง)"},
        // volume (l)
        {"ml|มิลลิลิตร|มล|cc", Cat::Volume, 0.001, "ml"},
        {"l|liter|litre|ลิตร", Cat::Volume, 1, "l"},
        {"m3|ลูกบาศก์เมตร|คิว", Cat::Volume, 1000, "m³"},
        {"gal|gallon|แกลลอน|แกลอน", Cat::Volume, 3.785411784, "US gal"},
        {"cup|ถ้วย|ถ้วยตวง", Cat::Volume, 0.24, "cup"},
        {"tbsp|ช้อนโต๊ะ", Cat::Volume, 0.015, "tbsp"},
        {"tsp|ช้อนชา", Cat::Volume, 0.005, "tsp"},
        {"barrel|bbl|บาร์เรล", Cat::Volume, 158.987294928, "barrel"},
        {"ถัง", Cat::Volume, 20, "ถัง (20 l)"},
        // speed (m/s)
        {"m/s|mps|เมตรต่อวินาที", Cat::Speed, 1, "m/s"},
        {"km/h|kmh|kph|กม/ชม|กิโลเมตรต่อชั่วโมง", Cat::Speed, 1.0 / 3.6, "km/h"},
        {"mph|ไมล์ต่อชั่วโมง", Cat::Speed, 0.44704, "mph"},
        {"knot|knots|kn|นอต|น็อต", Cat::Speed, 0.514444444, "knot"},
        // time (s)
        {"s|sec|second|seconds|วินาที", Cat::Time, 1, "s"},
        {"min|minute|minutes|นาที", Cat::Time, 60, "min"},
        {"h|hr|hour|hours|ชั่วโมง|ชม", Cat::Time, 3600, "h"},
        {"day|days|วัน", Cat::Time, 86400, "day"},
        {"week|weeks|สัปดาห์|อาทิตย์", Cat::Time, 604800, "week"},
        {"year|years|ปี", Cat::Time, 31557600, "year"},
        // data (bytes)
        {"b|byte|bytes|ไบต์", Cat::Data, 1, "B"},
        {"kb|กิโลไบต์", Cat::Data, 1024.0, "KB"},
        {"mb|เมกะไบต์|เมก", Cat::Data, 1048576.0, "MB"},
        {"gb|กิกะไบต์|กิ๊ก", Cat::Data, 1073741824.0, "GB"},
        {"tb|เทราไบต์", Cat::Data, 1099511627776.0, "TB"},
        // energy (J)
        {"j|joule|จูล", Cat::Energy, 1, "J"},
        {"kj|กิโลจูล", Cat::Energy, 1000, "kJ"},
        {"cal|แคลอรี", Cat::Energy, 4.184, "cal"},
        {"kcal|กิโลแคลอรี|แคล", Cat::Energy, 4184, "kcal"},
        {"kwh|หน่วย|unit", Cat::Energy, 3.6e6, "kWh"},
        // temperature
        {"c|°c|celsius|องศาเซลเซียส|เซลเซียส|องศา", Cat::Temperature, 0, "°C"},
        {"f|°f|fahrenheit|ฟาเรนไฮต์", Cat::Temperature, 1, "°F"},
        {"k|kelvin|เคลวิน", Cat::Temperature, 2, "K"},
    };
    n = sizeof(kUnits) / sizeof(kUnits[0]);
    return kUnits;
}

/** lower-case ASCII, drop spaces and dots ("กม./ชม." -> "กม/ชม", "Km H" -> "kmh") */
inline void normalise(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && n + 1 < cap; p++) {
        if (*p == ' ' || *p == '.' || *p == '\t') continue;
        out[n++] = (char)(*p < 0x80 ? tolower(*p) : *p);
    }
    out[n] = 0;
}

inline const Unit* find(const char* name) {
    char key[48];
    normalise(name ? name : "", key, sizeof(key));
    if (!*key) return nullptr;
    size_t n;
    const Unit* t = table(n);
    for (size_t i = 0; i < n; i++) {
        const char* p = t[i].names;
        while (*p) {
            const char* bar = strchr(p, '|');
            const size_t len = bar ? (size_t)(bar - p) : strlen(p);
            if (len == strlen(key) && !strncmp(p, key, len)) return &t[i];
            p += len + (bar ? 1 : 0);
        }
    }
    return nullptr;
}

/** Convert; false (and err) for unknown units or units of different kinds. */
inline bool convert(double value, const char* from, const char* to, double& out, const Unit*& uf, const Unit*& ut,
                    const char*& err) {
    uf = find(from);
    ut = find(to);
    if (!uf || !ut) {
        err = !uf ? "unknown 'from' unit" : "unknown 'to' unit";
        return false;
    }
    if (uf->cat != ut->cat) {
        err = "units measure different things";
        return false;
    }
    if (uf->cat == Cat::Temperature) {
        double c = value;
        if (uf->factor == 1) c = (value - 32) * 5.0 / 9.0;
        else if (uf->factor == 2) c = value - 273.15;
        out = ut->factor == 1 ? c * 9.0 / 5.0 + 32 : (ut->factor == 2 ? c + 273.15 : c);
        return true;
    }
    out = value * uf->factor / ut->factor;
    return true;
}

}  // namespace units
