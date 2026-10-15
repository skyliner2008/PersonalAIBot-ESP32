/**
 * @file RadioStations.cpp
 * @brief Station list storage and lookup (see RadioStations.h)
 */

#include "RadioStations.h"

#include "../app/Settings.h"
#include "../util/JsonFile.h"
#include "../util/Utf8.h"
#include "RadioPlayer.h"

RadioStations gStations;

namespace {

// Thai stations whose streams were checked on 2026-10-15: HTTP 200, MP3/AAC, audio flowing
// (Radio Browser candidates probed from a PC; edit the list from ESP32-AI Studio)
struct Default {
    const char* name;
    const char* url;
    const char* genre;
};
const Default kDefaults[] = {
    {"COOL Fahrenheit 93", "https://coolism-web.cdn.byteark.com/;stream/1", "เพลงฟังสบาย"},
    {"ลูกทุ่ง รักไทย 90", "http://radio11.plathong.net:8896/;stream.mp3", "ลูกทุ่ง"},
    {"มิติข่าว 90.5", "http://live3.smartbomb.co.th:8624/;stream.mp3", "ข่าว"},
    {"Smooth 105.5 (อสมท)", "http://rstream.mcot.net:8000/fm1055", "สากลฟังสบาย"},
    {"94 Bangkok Smile", "http://fm94.tv5.co.th:8000/fm94ch3", "เพลงไทย"},
    {"FM 103 คนลูกทุ่ง", "http://fm94.tv5.co.th:8000/armystream", "ลูกทุ่ง"},
    {"93.5 Top Radio", "https://a10.asurahosting.com:8250/radio.mp3", "เพลงเก่า"},
    {"JS100", "http://103.246.19.203:8004/stream", "จราจร ข่าว"},
    {"สวท. FM 92.5", "https://cdn-th2.login.in.th/shoutcast/8425", "ข่าว สาระ"},
    {"Radio Thailand FM 88", "https://cdn-th2.login.in.th/shoutcast/8420", "ข่าวภาษาอังกฤษ"},
    {"Isan FM 98", "http://radio11.plathong.net:8900/stream.mp3", "อีสาน"},
    {"106 Yes Radio", "https://4096.radio2.thzhost.com/;stream", "เพลงไทย"},
    {"Surf 102.5", "https://stream.surf.radio/surf1025", "ป๊อป ร็อก"},
    {"BKK.FM", "https://rsas.bkk.fm/radio", "อัลเทอร์เนทีฟ แดนซ์"},
    {"Radio Samui", "https://stream.mixadance.fm/radiosamui", "ชิลล์ แจ๊ส"},
    {"บ้านธัมมะ", "https://radio10.plathong.net/7238/;stream.mp3", "ธรรมะ"},
};

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
};

void copyStr(char* dst, size_t cap, const char* src) {
    if (!src) src = "";
    const size_t n = utf8::clampToBoundary(src, strlen(src), cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

String lower(const char* s) {
    String out = s;
    out.toLowerCase();  // ASCII only; Thai is compared as-is
    out.replace(" ", "");
    return out;
}

}  // namespace

void RadioStations::begin(const char* dir) {
    if (!_lock) _lock = xSemaphoreCreateMutex();
    if (dir && *dir) snprintf(_path, sizeof(_path), "%s/radio_stations.json", dir);
    JsonDocument doc;
    bool loaded = _path[0] && jsonfile::load(_path, doc) && doc.is<JsonArray>();
    Guard g(_lock);
    _list.clear();
    if (loaded) {
        for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
            RadioStation s;
            copyStr(s.name, sizeof(s.name), o["name"] | "");
            copyStr(s.url, sizeof(s.url), o["url"] | "");
            copyStr(s.genre, sizeof(s.genre), o["genre"] | "");
            if (!validate(s) && _list.size() < kMax) _list.push_back(s);
        }
    }
    if (!loaded) {
        loadDefaults();
        if (_path[0]) save();
    }
    Serial.printf("[Radio] %u stations (%s)\n", (unsigned)_list.size(), loaded ? _path : "defaults");
}

void RadioStations::loadDefaults() {
    _list.clear();
    for (const Default& d : kDefaults) {
        RadioStation s;
        copyStr(s.name, sizeof(s.name), d.name);
        copyStr(s.url, sizeof(s.url), d.url);
        copyStr(s.genre, sizeof(s.genre), d.genre);
        _list.push_back(s);
    }
}

// caller holds the lock
void RadioStations::save() {
    if (!_path[0]) return;
    JsonDocument doc;
    JsonArray a = doc.to<JsonArray>();
    for (const RadioStation& s : _list) {
        JsonObject o = a.add<JsonObject>();
        o["name"] = (const char*)s.name;
        o["url"] = (const char*)s.url;
        o["genre"] = (const char*)s.genre;
    }
    if (!jsonfile::save(_path, doc)) Serial.println("[Radio] ERROR: could not save the station list");
}

size_t RadioStations::count() const {
    Guard g(_lock);
    return _list.size();
}

bool RadioStations::get(size_t index, RadioStation& out) const {
    Guard g(_lock);
    if (index >= _list.size()) return false;
    out = _list[index];
    return true;
}

int RadioStations::indexOf(const char* name) const {
    Guard g(_lock);
    for (size_t i = 0; i < _list.size(); i++) {
        if (!strcmp(_list[i].name, name)) return (int)i;
    }
    return -1;
}

int RadioStations::find(const char* query) const {
    if (!query) return -1;
    String q = query;
    q.trim();
    if (q.isEmpty()) return -1;
    Guard g(_lock);
    const long n = q.toInt();
    if (n >= 1 && (size_t)n <= _list.size() && String(n) == q) return (int)n - 1;
    const String lq = lower(q.c_str());
    for (size_t i = 0; i < _list.size(); i++) {  // name contains the words (or the other way round)
        const String ln = lower(_list[i].name);
        if (ln.indexOf(lq) >= 0 || (ln.length() >= 3 && lq.indexOf(ln) >= 0)) return (int)i;
    }
    for (size_t i = 0; i < _list.size(); i++) {  // then the genre ("ลูกทุ่ง", "ข่าว")
        if (lower(_list[i].genre).indexOf(lq) >= 0) return (int)i;
    }
    // any word of the query in a name: "เปิดคูลฟาเรนไฮต์ 93" -> "93"
    int best = -1;
    int start = 0;
    while (start < (int)q.length()) {
        int sp = q.indexOf(' ', start);
        if (sp < 0) sp = q.length();
        const String w = lower(q.substring(start, sp).c_str());
        if (w.length() >= 2) {
            for (size_t i = 0; i < _list.size() && best < 0; i++) {
                if (lower(_list[i].name).indexOf(w) >= 0) best = (int)i;
            }
        }
        start = sp + 1;
    }
    if (best >= 0) return best;
    // "สถานีที่ 4", "ช่อง 2": a list number inside a sentence (names were tried first, so "93" stays a name)
    int num = -1;
    for (int i = 0; i < (int)q.length(); i++) {
        if (isdigit((unsigned char)q[i]) && (i == 0 || !isdigit((unsigned char)q[i - 1]))) num = atoi(q.c_str() + i);
    }
    return num >= 1 && (size_t)num <= _list.size() ? num - 1 : -1;
}

const char* RadioStations::validate(const RadioStation& s) {
    if (!s.name[0]) return "ต้องมีชื่อสถานี";
    if (strncmp(s.url, "http://", 7) != 0 && strncmp(s.url, "https://", 8) != 0) return "URL ต้องขึ้นต้นด้วย http:// หรือ https://";
    for (const unsigned char* p = (const unsigned char*)s.url; *p; p++) {
        if (*p < 0x21 || *p > 0x7E) return "URL มีตัวอักษรที่ใช้ไม่ได้ (ช่องว่าง / ภาษาไทย)";
    }
    return nullptr;
}

const char* RadioStations::add(const RadioStation& s) {
    if (const char* e = validate(s)) return e;
    Guard g(_lock);
    if (_list.size() >= kMax) return "รายการเต็มแล้ว (สูงสุด 40 สถานี)";
    _list.push_back(s);
    save();
    return nullptr;
}

const char* RadioStations::update(size_t index, const RadioStation& s) {
    if (const char* e = validate(s)) return e;
    Guard g(_lock);
    if (index >= _list.size()) return "ไม่มีสถานีลำดับนี้";
    _list[index] = s;
    save();
    return nullptr;
}

bool RadioStations::remove(size_t index) {
    Guard g(_lock);
    if (index >= _list.size()) return false;
    _list.erase(_list.begin() + index);
    save();
    return true;
}

bool RadioStations::move(size_t from, size_t to) {
    Guard g(_lock);
    if (from >= _list.size() || to >= _list.size()) return false;
    const RadioStation s = _list[from];
    _list.erase(_list.begin() + from);
    _list.insert(_list.begin() + to, s);
    save();
    return true;
}

void RadioStations::resetDefaults() {
    Guard g(_lock);
    loadDefaults();
    save();
}

void RadioStations::toJson(JsonArray out) const {
    Guard g(_lock);
    for (const RadioStation& s : _list) {
        JsonObject o = out.add<JsonObject>();
        o["name"] = (const char*)s.name;
        o["url"] = (const char*)s.url;
        o["genre"] = (const char*)s.genre;
    }
}

void RadioStations::describe(String& out) const {
    Guard g(_lock);
    for (size_t i = 0; i < _list.size(); i++) {
        if (i) out += ", ";
        out += String(i + 1) + " " + _list[i].name;
        if (_list[i].genre[0]) out += String(" (") + _list[i].genre + ")";
    }
}

int RadioStations::lastIndex() const {
    const SettingsData s = gSettings.get();
    const int i = s.radioName[0] ? indexOf(s.radioName) : -1;
    if (i >= 0) return i;
    return count() ? 0 : -1;
}

bool RadioStations::play(size_t index) {
    RadioStation s;
    if (!get(index, s) || !gRadio.play(s.url, s.name)) return false;
    const SettingsData cur = gSettings.get();
    if (strcmp(cur.radioName, s.name) != 0) {
        gSettings.update([&](SettingsData& d) { copyStr(d.radioName, sizeof(d.radioName), s.name); });
    }
    Serial.printf("[Radio] Station %u: %s\n", (unsigned)index + 1, s.name);
    return true;
}

const char* RadioStations::parse(const String& line, RadioStation& out) {
    const int a = line.indexOf('|');
    if (a < 0) return "รูปแบบ: ชื่อ|URL|แนวเพลง";
    const int b = line.indexOf('|', a + 1);
    String name = line.substring(0, a), url = b < 0 ? line.substring(a + 1) : line.substring(a + 1, b);
    String genre = b < 0 ? String() : line.substring(b + 1);
    name.trim();
    url.trim();
    genre.trim();
    copyStr(out.name, sizeof(out.name), name.c_str());
    copyStr(out.url, sizeof(out.url), url.c_str());
    copyStr(out.genre, sizeof(out.genre), genre.c_str());
    return validate(out);
}
