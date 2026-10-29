/**
 * @file Scheduler.h
 * @brief Timers and reminders (one-shot or repeating), persisted on storage
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 *
 * Times are unix epochs; calendar math uses a fixed UTC offset (Thailand,
 * UTC+7, no DST). The agent checks collectDue() every second; fired
 * reminders ring on the device and are announced through the Live session.
 */

#pragma once

#include <stdint.h>

#include <vector>

#include "../util/PsramAlloc.h"

namespace sched {

enum class Repeat : uint8_t { None, Daily, Weekdays, Weekly };

struct Reminder {
    uint32_t id = 0;
    uint32_t due = 0;   // unix epoch
    Repeat repeat = Repeat::None;
    bool timer = false; // created by set_timer (relative)
    PsString label;
};

const char* repeatName(Repeat r);
Repeat parseRepeat(const char* s);  // none/daily/weekdays/weekly (+ Thai words)

/**
 * Parse "YYYY-MM-DD HH:MM", "HH:MM" (next occurrence, today or tomorrow) or
 * "tomorrow HH:MM" / "พรุ่งนี้ HH:MM". Returns 0 if invalid or in the past.
 */
uint32_t parseWhen(const char* s, uint32_t now, int32_t utcOffsetSec);

/** Next due time strictly after `due` for a repeating reminder (0 for Repeat::None). */
uint32_t nextOccurrence(uint32_t due, Repeat r, int32_t utcOffsetSec);

class Scheduler {
public:
    explicit Scheduler(int32_t utcOffsetSec = 7 * 3600, size_t maxItems = 50);

    /** Load from a JSON file (missing file = empty). Pass nullptr to keep it in RAM only. */
    bool open(const char* path);

    uint32_t addTimer(uint32_t now, uint32_t seconds, const char* label);
    uint32_t addReminder(uint32_t due, Repeat repeat, const char* label);
    bool cancel(uint32_t id);
    size_t cancelAll();

    /** Items sorted by due time. */
    const std::vector<Reminder>& items() const { return _items; }
    const Reminder* find(uint32_t id) const;

    /**
     * Remove/reschedule everything due at or before `now` and return copies of
     * what fired. A repeating reminder that was missed for a long time fires once.
     */
    size_t collectDue(uint32_t now, std::vector<Reminder>& fired);

    uint32_t nextDue() const { return _items.empty() ? 0 : _items.front().due; }

private:
    int32_t _utcOffset;
    size_t _maxItems;
    std::vector<Reminder> _items;
    uint32_t _nextId = 1;
    PsString _path;

    void sortAndSave();
    bool save() const;
};

}  // namespace sched
