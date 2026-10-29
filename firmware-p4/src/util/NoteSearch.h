/**
 * @file NoteSearch.h
 * @brief Which entries of notes.json an id / query picks (header-only, host tested)
 */

#pragma once

#include <ArduinoJson.h>
#include <string.h>

namespace notebook {

/** By id when one is given, else every note whose title or text contains `query` (none for an empty query). */
inline bool matches(JsonObjectConst n, int id, const char* query) {
    if (id) return (n["id"] | 0) == id;
    if (!query || !*query) return false;
    return strstr(n["title"] | "", query) || strstr(n["text"] | "", query);
}

constexpr int kNone = -1;
constexpr int kAmbiguous = -2;

/**
 * The one note `delete` removes: the id, else the single note the query matches. A query that fits
 * several is kAmbiguous - deleting every match ("ซื้อ" took all the shopping lists) cannot be undone.
 */
inline int pickForDelete(JsonArrayConst notes, int id, const char* query) {
    int found = kNone;
    for (size_t i = 0; i < notes.size(); i++) {
        if (!matches(notes[i], id, query)) continue;
        if (found != kNone) return kAmbiguous;
        found = (int)i;
    }
    return found;
}

}  // namespace notebook
