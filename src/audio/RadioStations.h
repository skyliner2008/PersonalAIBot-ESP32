/**
 * @file RadioStations.h
 * @brief The Internet-radio station list: curated defaults, stored as JSON next to the memory files
 * @see wiki/12-Internet-Radio.md
 *
 * "<agentDir>/radio_stations.json" = [{"name":"...","url":"http(s)://...","genre":"..."}, ...]
 * Edited from ESP32-AI Studio through the console (radiolist / radioadd / radioedit / radiodel /
 * radiomove / radioreset); the touch radio screen and the play_radio tool pick from it.
 * Thread-safe (UI task, agent task, console).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

struct RadioStation {
    char name[48] = "";
    char url[200] = "";
    char genre[40] = "";
};

class RadioStations {
public:
    static constexpr size_t kMax = 40;

    /** `dir` = agent data folder ("" = no storage: defaults kept in RAM only). */
    void begin(const char* dir);

    size_t count() const;
    bool get(size_t index, RadioStation& out) const;
    /** "3" (1-based), else a name / genre match (case-insensitive for Latin). -1 if none. */
    int find(const char* query) const;
    /** Index of the station called `name` (exact), -1 if not in the list. */
    int indexOf(const char* name) const;

    // ---- editing (saved at once) ----
    const char* add(const RadioStation& s);                  // nullptr = ok, else the reason
    const char* update(size_t index, const RadioStation& s);
    bool remove(size_t index);
    bool move(size_t from, size_t to);
    void resetDefaults();

    void toJson(JsonArray out) const;
    /** "1 COOL Fahrenheit 93 (เพลงฟังสบาย), 2 ..." for the assistant's instructions. */
    void describe(String& out) const;

    /** Play list entry `index`: starts the radio and remembers it as the last station. */
    bool play(size_t index);
    /** The last station played (by name), else the first; -1 if the list is empty. */
    int lastIndex() const;

    /** "name|url|genre" from the console. nullptr = ok, else the reason. */
    static const char* parse(const String& line, RadioStation& out);

private:
    std::vector<RadioStation> _list;
    mutable SemaphoreHandle_t _lock = nullptr;
    char _path[96] = "";

    void loadDefaults();
    void save();
    static const char* validate(const RadioStation& s);
};

extern RadioStations gStations;
