/**
 * @file TaskList.h
 * @brief Personal to-do list persisted on storage
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 */

#pragma once

#include <stdint.h>

#include <vector>

#include "../util/PsramAlloc.h"

namespace todo {

struct Task {
    uint32_t id = 0;
    PsString title;
    bool done = false;
    uint8_t priority = 2;  // 1 high, 2 normal, 3 low
    uint32_t created = 0;
    uint32_t due = 0;      // 0 = no due date
    uint32_t doneAt = 0;
};

class TaskList {
public:
    explicit TaskList(size_t maxItems = 200);

    /** Load from JSON (missing file = empty). nullptr keeps it in RAM only. */
    bool open(const char* path);

    uint32_t add(const char* title, uint8_t priority, uint32_t due, uint32_t now);
    bool setDone(uint32_t id, bool done, uint32_t now);
    bool rename(uint32_t id, const char* title);
    bool remove(uint32_t id);
    /** Drop completed tasks finished more than `days` ago. Returns removed count. */
    size_t purgeDone(uint32_t now, uint32_t days);

    const Task* find(uint32_t id) const;
    /** Open tasks first (priority, due, created), then done ones (newest first). */
    std::vector<const Task*> list(bool includeDone) const;
    size_t openCount() const;
    size_t size() const { return _items.size(); }

private:
    size_t _maxItems;
    std::vector<Task> _items;
    uint32_t _nextId = 1;
    PsString _path;

    bool save() const;
    Task* findMutable(uint32_t id);
};

}  // namespace todo
