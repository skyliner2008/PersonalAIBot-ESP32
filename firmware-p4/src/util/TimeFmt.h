/**
 * @file TimeFmt.h
 * @brief Timezone-independent date formatting (fixed UTC offset)
 *
 * The device runs in Thailand (UTC+7, no DST), so a fixed offset is exact
 * and keeps the memory code deterministic on the host as well.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

namespace timefmt {

/** Unix epochs before this are treated as "clock not synced yet". */
constexpr uint32_t kMinValidEpoch = 1700000000;  // 2023-11-14

struct Civil {
    int year, month, day, hour, minute, second, weekday;  // weekday: 0 = Sunday
};

/** Howard Hinnant's civil_from_days, applied to (epoch + offset). */
inline Civil toCivil(uint32_t epoch, int32_t utcOffsetSec) {
    const int64_t t = (int64_t)epoch + utcOffsetSec;
    int64_t days = t / 86400;
    int64_t secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days -= 1;
    }
    Civil c;
    c.hour = (int)(secs / 3600);
    c.minute = (int)((secs % 3600) / 60);
    c.second = (int)(secs % 60);
    c.weekday = (int)((days + 4) % 7);  // 1970-01-01 was a Thursday
    if (c.weekday < 0) c.weekday += 7;

    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    c.day = (int)(doy - (153 * mp + 2) / 5 + 1);
    c.month = (int)(mp < 10 ? mp + 3 : mp - 9);
    c.year = (int)(yoe + era * 400 + (c.month <= 2 ? 1 : 0));
    return c;
}

/** Days since 1970-01-01 for a civil date (inverse of toCivil's date part). */
inline int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/** Local day number (days since epoch in the given offset). */
inline int64_t localDay(uint32_t epoch, int32_t utcOffsetSec) {
    const int64_t t = (int64_t)epoch + utcOffsetSec;
    return t >= 0 ? t / 86400 : (t - 86399) / 86400;
}

/** "YYYY-MM-DD" (buf >= 11 bytes). Returns false if epoch is not valid. */
inline bool formatDate(uint32_t epoch, int32_t utcOffsetSec, char* buf, size_t cap) {
    if (epoch < kMinValidEpoch) {
        snprintf(buf, cap, "unknown");
        return false;
    }
    const Civil c = toCivil(epoch, utcOffsetSec);
    snprintf(buf, cap, "%04d-%02d-%02d", c.year, c.month, c.day);
    return true;
}

/** "YYYY-MM-DD HH:MM" (buf >= 17 bytes). */
inline bool formatDateTime(uint32_t epoch, int32_t utcOffsetSec, char* buf, size_t cap) {
    if (epoch < kMinValidEpoch) {
        snprintf(buf, cap, "unknown");
        return false;
    }
    const Civil c = toCivil(epoch, utcOffsetSec);
    snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d", c.year, c.month, c.day, c.hour, c.minute);
    return true;
}

/**
 * "2026-10-25 22:30" -> "25/10 22:30" (a card's short time); a date alone -> "25/10"; anything else is copied.
 */
inline void shortStamp(const char* when, char* buf, size_t cap) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    const int got = when ? sscanf(when, "%4d-%2d-%2d %2d:%2d", &y, &m, &d, &hh, &mm) : 0;
    if (got >= 5) snprintf(buf, cap, "%d/%d %02d:%02d", d, m, hh, mm);
    else if (got >= 3) snprintf(buf, cap, "%d/%d", d, m);
    else snprintf(buf, cap, "%s", when ? when : "");
}

/** Thai weekday name (UTF-8). */
inline const char* thaiWeekday(int weekday) {
    static const char* const names[] = {"อาทิตย์", "จันทร์", "อังคาร", "พุธ",
                                        "พฤหัสบดี", "ศุกร์", "เสาร์"};
    return (weekday >= 0 && weekday < 7) ? names[weekday] : "";
}

}  // namespace timefmt
