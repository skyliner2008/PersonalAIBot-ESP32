/**
 * @file Scheduler.cpp
 * @brief Timers / reminders with repeat rules and JSON persistence
 */

#include "Scheduler.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"
#include "../util/TimeFmt.h"
#include "../util/Utf8.h"

namespace sched {

const char* repeatName(Repeat r) {
    switch (r) {
        case Repeat::Daily: return "daily";
        case Repeat::Weekdays: return "weekdays";
        case Repeat::Weekly: return "weekly";
        default: return "none";
    }
}

Repeat parseRepeat(const char* s) {
    if (!s) return Repeat::None;
    if (!strcmp(s, "daily") || strstr(s, "ทุกวัน")) return Repeat::Daily;
    if (!strcmp(s, "weekdays") || strstr(s, "วันทำงาน") || strstr(s, "จันทร์-ศุกร์")) return Repeat::Weekdays;
    if (!strcmp(s, "weekly") || strstr(s, "ทุกสัปดาห์")) return Repeat::Weekly;
    return Repeat::None;
}

namespace {

// Local midnight (epoch) of the day containing `t`
int64_t localMidnight(uint32_t t, int32_t off) { return timefmt::localDay(t, off) * 86400 - off; }

bool parseHm(const char* s, int& h, int& m) {
    return sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60;
}

const char* skipSpaces(const char* s) {
    while (*s == ' ') s++;
    return s;
}

}  // namespace

uint32_t parseWhen(const char* s, uint32_t now, int32_t off) {
    if (!s || now < timefmt::kMinValidEpoch) return 0;
    s = skipSpaces(s);
    int y, mo, d, h, mi;
    if (sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
        if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return 0;
        const int64_t t = timefmt::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 - off;
        return t > (int64_t)now ? (uint32_t)t : 0;
    }

    bool tomorrow = false;
    if (!strncmp(s, "tomorrow", 8)) {
        tomorrow = true;
        s = skipSpaces(s + 8);
    } else if (!strncmp(s, "พรุ่งนี้", strlen("พรุ่งนี้"))) {
        tomorrow = true;
        s = skipSpaces(s + strlen("พรุ่งนี้"));
    }
    if (!parseHm(s, h, mi)) return 0;
    int64_t t = localMidnight(now, off) + h * 3600 + mi * 60;
    if (tomorrow) t += 86400;
    else if (t <= (int64_t)now) t += 86400;  // already passed today -> tomorrow
    return (uint32_t)t;
}

uint32_t nextOccurrence(uint32_t due, Repeat r, int32_t off) {
    switch (r) {
        case Repeat::Daily: return due + 86400;
        case Repeat::Weekly: return due + 7 * 86400;
        case Repeat::Weekdays: {
            uint32_t t = due + 86400;
            for (int i = 0; i < 7; i++) {
                const int wd = timefmt::toCivil(t, off).weekday;  // 0 = Sunday
                if (wd != 0 && wd != 6) return t;
                t += 86400;
            }
            return t;
        }
        default: return 0;
    }
}

Scheduler::Scheduler(int32_t utcOffsetSec, size_t maxItems) : _utcOffset(utcOffsetSec), _maxItems(maxItems) {}

bool Scheduler::open(const char* path) {
    _items.clear();
    _nextId = 1;
    _path = path ? path : "";
    if (_path.empty()) return true;

    JsonDocument doc(JsonPsramAllocator::instance());
    if (!jsonfile::load(_path.c_str(), doc)) return true;  // nothing saved yet
    _nextId = doc["next"] | 1u;
    for (JsonObjectConst o : doc["items"].as<JsonArrayConst>()) {
        Reminder r;
        r.id = o["id"] | 0u;
        r.due = o["due"] | 0u;
        r.repeat = parseRepeat(o["rep"] | "none");
        r.timer = o["timer"] | false;
        r.label = o["label"] | "";
        if (!r.id || !r.due) continue;
        if (r.id >= _nextId) _nextId = r.id + 1;
        _items.push_back(std::move(r));
    }
    std::sort(_items.begin(), _items.end(), [](const Reminder& a, const Reminder& b) { return a.due < b.due; });
    return true;
}

bool Scheduler::save() const {
    if (_path.empty()) return true;
    JsonDocument doc(JsonPsramAllocator::instance());
    doc["next"] = _nextId;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const Reminder& r : _items) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = r.id;
        o["due"] = r.due;
        o["rep"] = repeatName(r.repeat);
        if (r.timer) o["timer"] = true;
        o["label"] = r.label.c_str();
    }
    return jsonfile::save(_path.c_str(), doc);
}

void Scheduler::sortAndSave() {
    std::sort(_items.begin(), _items.end(), [](const Reminder& a, const Reminder& b) { return a.due < b.due; });
    save();
}

uint32_t Scheduler::addTimer(uint32_t now, uint32_t seconds, const char* label) {
    if (now < timefmt::kMinValidEpoch || seconds == 0 || seconds > 7 * 86400 || _items.size() >= _maxItems) return 0;
    Reminder r;
    r.id = _nextId++;
    r.due = now + seconds;
    r.timer = true;
    const char* l = label && *label ? label : "หมดเวลา";
    r.label = PsString(l, utf8::clampToBoundary(l, strlen(l), 150));
    const uint32_t id = r.id;
    _items.push_back(std::move(r));
    sortAndSave();
    return id;
}

uint32_t Scheduler::addReminder(uint32_t due, Repeat repeat, const char* label) {
    if (due == 0 || _items.size() >= _maxItems) return 0;
    Reminder r;
    r.id = _nextId++;
    r.due = due;
    r.repeat = repeat;
    const char* l = label && *label ? label : "แจ้งเตือน";
    r.label = PsString(l, utf8::clampToBoundary(l, strlen(l), 150));
    const uint32_t id = r.id;
    _items.push_back(std::move(r));
    sortAndSave();
    return id;
}

const Reminder* Scheduler::find(uint32_t id) const {
    for (const Reminder& r : _items) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

bool Scheduler::cancel(uint32_t id) {
    for (size_t i = 0; i < _items.size(); i++) {
        if (_items[i].id == id) {
            _items.erase(_items.begin() + (long)i);
            save();
            return true;
        }
    }
    return false;
}

size_t Scheduler::cancelAll() {
    const size_t n = _items.size();
    _items.clear();
    save();
    return n;
}

size_t Scheduler::collectDue(uint32_t now, std::vector<Reminder>& fired) {
    fired.clear();
    if (now < timefmt::kMinValidEpoch) return 0;
    bool changed = false;
    for (size_t i = 0; i < _items.size();) {
        Reminder& r = _items[i];
        if (r.due > now) {
            i++;
            continue;
        }
        fired.push_back(r);
        changed = true;
        if (r.repeat == Repeat::None) {
            _items.erase(_items.begin() + (long)i);
            continue;
        }
        while (r.due <= now) r.due = nextOccurrence(r.due, r.repeat, _utcOffset);
        i++;
    }
    if (changed) sortAndSave();
    return fired.size();
}

}  // namespace sched
