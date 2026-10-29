/**
 * @file DataDates.h
 * @brief Dates found in the data sources: Thai "9 ต.ค. 69", "08/10/2569" (Buddhist Era), RSS pubDate
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * The tools give the model WHEN a figure was valid, not only the figure (a fuel price set on the 9th
 * is not "today's change" on the 15th). Header-only, no Arduino calls (host tested).
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "TimeFmt.h"

namespace datadates {

struct Ymd {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    bool ok() const { return y > 1900 && m >= 1 && m <= 12 && d >= 1 && d <= 31; }
    int64_t day() const { return timefmt::daysFromCivil(y, m, d); }
};

/** Buddhist Era year (2 or 4 digits) -> Gregorian: "69" -> 2026, "2569" -> 2026. */
inline int fromBuddhistYear(int be) {
    if (be < 100) be += 2500;
    return be - 543;
}

constexpr const char* kThaiMonths[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.",
                                       "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};

/** 1..12 -> "ต.ค." */
inline const char* thaiMonthAbbr(int m) { return kThaiMonths[(m >= 1 && m <= 12) ? m - 1 : 0]; }

/** Thai month abbreviation ("ต.ค.") -> 1..12, 0 if unknown. The whole token must match. */
inline int thaiMonth(const char* s, size_t n) {
    const char* const* kMonths = kThaiMonths;
    for (int i = 0; i < 12; i++) {
        if (strlen(kMonths[i]) == n && !strncmp(s, kMonths[i], n)) return i + 1;
    }
    return 0;
}

/**
 * Bangchak "OilRemark2": "ราคามีผล ณ วันที่ 9 ต.ค. 69 เวลา 05.00 น." -> 2026-10-09 05:00.
 * Looks for "<day> <Thai month> <BE year>" anywhere, then an optional "HH.MM" / "HH:MM" after it.
 */
inline bool parseThaiDate(const char* s, Ymd& out) {
    if (!s) return false;
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9' || (p > s && p[-1] >= '0' && p[-1] <= '9')) continue;
        char* end = nullptr;
        const long day = strtol(p, &end, 10);
        if (*end != ' ') continue;
        const char* mon = end + 1;
        const char* monEnd = strchr(mon, ' ');
        if (!monEnd) continue;
        const int m = thaiMonth(mon, (size_t)(monEnd - mon));
        if (!m) continue;
        char* yEnd = nullptr;
        const long be = strtol(monEnd + 1, &yEnd, 10);
        if (yEnd == monEnd + 1) continue;
        Ymd r;
        r.y = fromBuddhistYear((int)be);
        r.m = m;
        r.d = (int)day;
        for (const char* t = yEnd; *t; t++) {  // "เวลา 05.00 น."
            if (t[0] >= '0' && t[0] <= '9' && t[1] >= '0' && t[1] <= '9' && (t[2] == '.' || t[2] == ':') &&
                t[3] >= '0' && t[3] <= '9' && t[4] >= '0' && t[4] <= '9') {
                r.hh = (t[0] - '0') * 10 + (t[1] - '0');
                r.mm = (t[3] - '0') * 10 + (t[4] - '0');
                break;
            }
        }
        if (!r.ok()) continue;
        out = r;
        return true;
    }
    return false;
}

/** "08/10/2569" (day/month/BE year) + optional "20:40". */
inline bool parseSlashDate(const char* date, const char* time, Ymd& out) {
    int d = 0, m = 0, y = 0;
    if (!date || sscanf(date, "%d/%d/%d", &d, &m, &y) != 3) return false;
    Ymd r;
    r.y = y > 2400 ? y - 543 : y;
    r.m = m;
    r.d = d;
    if (time) sscanf(time, "%d:%d", &r.hh, &r.mm);
    if (!r.ok()) return false;
    out = r;
    return true;
}

/** RFC 822 date of RSS: "Thu, 15 Oct 2026 12:14:28 GMT" -> unix epoch (0 if not parsed). */
inline uint32_t parseRfc822(const char* s) {
    if (!s) return 0;
    const char* comma = strchr(s, ',');
    const char* p = comma ? comma + 1 : s;
    int d = 0, y = 0, hh = 0, mi = 0, ss = 0;
    char mon[4] = {0};
    char zone[8] = {0};
    if (sscanf(p, " %d %3s %d %d:%d:%d %7s", &d, mon, &y, &hh, &mi, &ss, zone) < 6) return 0;
    static const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int m = 0;
    for (int i = 0; i < 12; i++) {
        if (!strcmp(mon, kMon[i])) m = i + 1;
    }
    if (!m || y < 1970) return 0;
    int64_t t = timefmt::daysFromCivil(y, m, d) * 86400 + hh * 3600 + mi * 60 + ss;
    if ((zone[0] == '+' || zone[0] == '-') && strlen(zone) == 5) {  // "+0700"
        const int off = ((zone[1] - '0') * 10 + (zone[2] - '0')) * 60 + (zone[3] - '0') * 10 + (zone[4] - '0');
        t -= (zone[0] == '-' ? -1 : 1) * (int64_t)off * 60;
    }
    return t > 0 ? (uint32_t)t : 0;
}

}  // namespace datadates
