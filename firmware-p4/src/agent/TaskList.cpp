/**
 * @file TaskList.cpp
 * @brief To-do list with JSON persistence
 */

#include "TaskList.h"

#include <ArduinoJson.h>
#include <string.h>

#include <algorithm>

#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"
#include "../util/Utf8.h"

namespace todo {

namespace {
PsString clampTitle(const char* s) {
    if (!s) return PsString();
    while (*s == ' ') s++;
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') len--;
    return PsString(s, utf8::clampToBoundary(s, len, 200));
}
}  // namespace

TaskList::TaskList(size_t maxItems) : _maxItems(maxItems) {}

bool TaskList::open(const char* path) {
    _items.clear();
    _nextId = 1;
    _path = path ? path : "";
    if (_path.empty()) return true;
    JsonDocument doc(JsonPsramAllocator::instance());
    if (!jsonfile::load(_path.c_str(), doc)) return true;
    _nextId = doc["next"] | 1u;
    for (JsonObjectConst o : doc["items"].as<JsonArrayConst>()) {
        Task t;
        t.id = o["id"] | 0u;
        t.title = o["title"] | "";
        t.done = o["done"] | false;
        t.priority = o["pri"] | 2;
        t.created = o["c"] | 0u;
        t.due = o["due"] | 0u;
        t.doneAt = o["dt"] | 0u;
        if (!t.id || t.title.empty()) continue;
        if (t.id >= _nextId) _nextId = t.id + 1;
        _items.push_back(std::move(t));
    }
    return true;
}

bool TaskList::save() const {
    if (_path.empty()) return true;
    JsonDocument doc(JsonPsramAllocator::instance());
    doc["next"] = _nextId;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const Task& t : _items) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = t.id;
        o["title"] = t.title.c_str();
        if (t.done) o["done"] = true;
        o["pri"] = t.priority;
        o["c"] = t.created;
        if (t.due) o["due"] = t.due;
        if (t.doneAt) o["dt"] = t.doneAt;
    }
    return jsonfile::save(_path.c_str(), doc);
}

Task* TaskList::findMutable(uint32_t id) {
    for (Task& t : _items) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const Task* TaskList::find(uint32_t id) const {
    for (const Task& t : _items) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

uint32_t TaskList::add(const char* title, uint8_t priority, uint32_t due, uint32_t now) {
    Task t;
    t.title = clampTitle(title);
    if (t.title.empty()) return 0;
    if (_items.size() >= _maxItems) {
        // make room: drop the oldest completed task, otherwise refuse
        auto oldest = _items.end();
        for (auto it = _items.begin(); it != _items.end(); ++it) {
            if (it->done && (oldest == _items.end() || it->doneAt < oldest->doneAt)) oldest = it;
        }
        if (oldest == _items.end()) return 0;
        _items.erase(oldest);
    }
    t.id = _nextId++;
    t.priority = (uint8_t)(priority < 1 ? 1 : (priority > 3 ? 3 : priority));
    t.created = now;
    t.due = due;
    const uint32_t id = t.id;
    _items.push_back(std::move(t));
    save();
    return id;
}

bool TaskList::setDone(uint32_t id, bool done, uint32_t now) {
    Task* t = findMutable(id);
    if (!t) return false;
    t->done = done;
    t->doneAt = done ? now : 0;
    save();
    return true;
}

bool TaskList::rename(uint32_t id, const char* title) {
    Task* t = findMutable(id);
    PsString nt = clampTitle(title);
    if (!t || nt.empty()) return false;
    t->title = std::move(nt);
    save();
    return true;
}

bool TaskList::remove(uint32_t id) {
    for (auto it = _items.begin(); it != _items.end(); ++it) {
        if (it->id == id) {
            _items.erase(it);
            save();
            return true;
        }
    }
    return false;
}

size_t TaskList::purgeDone(uint32_t now, uint32_t days) {
    const size_t before = _items.size();
    _items.erase(std::remove_if(_items.begin(), _items.end(),
                                [&](const Task& t) {
                                    return t.done && t.doneAt && now > t.doneAt && now - t.doneAt > days * 86400u;
                                }),
                 _items.end());
    const size_t removed = before - _items.size();
    if (removed) save();
    return removed;
}

std::vector<const Task*> TaskList::list(bool includeDone) const {
    std::vector<const Task*> out;
    for (const Task& t : _items) {
        if (includeDone || !t.done) out.push_back(&t);
    }
    std::sort(out.begin(), out.end(), [](const Task* a, const Task* b) {
        if (a->done != b->done) return !a->done;
        if (a->done) return a->doneAt > b->doneAt;
        if (a->priority != b->priority) return a->priority < b->priority;
        const uint32_t da = a->due ? a->due : UINT32_MAX, db = b->due ? b->due : UINT32_MAX;
        if (da != db) return da < db;
        return a->created < b->created;
    });
    return out;
}

size_t TaskList::openCount() const {
    size_t n = 0;
    for (const Task& t : _items) n += t.done ? 0 : 1;
    return n;
}

}  // namespace todo
