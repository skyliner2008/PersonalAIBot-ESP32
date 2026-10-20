/**
 * @file VehicleData.h
 * @brief Readings of a car or motorcycle shared by every data source and the dash (header-only, host tested)
 * @see wiki/16-Vehicle-Dash.md
 *
 * A source (simulator, Honda K-Line, OBD-II ELM327 over BLE / UART, text lines over UART) writes the channels
 * it knows; the dash and the `vehicle` tool read them. A channel not refreshed for kStaleMs shows "--":
 * a value never freezes on the screen when a cable comes loose.
 */

#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// OBD-II over Bluetooth LE is built in only with -DVEHICLE_BLE=1 (platformio.ini). Linking the BLE stack puts
// ~22 KB of controller code in IRAM, which on the S3 comes out of the same SRAM as the internal heap: the
// assistant's TLS then runs out ("esp-aes: Failed to allocate memory") even with the dash off (T078).
#ifndef VEHICLE_BLE
#define VEHICLE_BLE 0
#endif

namespace vehicle {

constexpr bool kBleBuilt = VEHICLE_BLE != 0;

/** Case-insensitive compare of n characters (MSVC has no strncasecmp: the host tests build there too). */
inline bool ieqn(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
        if (!a[i]) return true;
    }
    return true;
}
inline bool ieq(const char* a, const char* b) { return strlen(a) == strlen(b) && ieqn(a, b, strlen(a)); }

enum Channel : uint8_t {
    kRpm,       // engine speed, 1/min
    kSpeed,     // km/h
    kCoolant,   // engine / coolant (or oil on an air-cooled bike) temperature, deg C
    kIntake,    // intake air temperature, deg C
    kBattery,   // 12 V system, volts
    kThrottle,  // throttle position, %
    kMap,       // manifold absolute pressure, kPa
    kGear,      // gear reported by the source (0 = neutral); otherwise the dash estimates it
    kChannelCount
};

constexpr uint32_t kStaleMs = 2500;

/** JSON / console key of a channel ("rpm", "speed_kmh", ...). */
inline const char* channelKey(Channel c) {
    static const char* const kKeys[kChannelCount] = {"rpm",       "speed_kmh",    "coolant_c", "intake_c",
                                                     "battery_v", "throttle_pct", "map_kpa",   "gear"};
    return c < kChannelCount ? kKeys[c] : "?";
}

struct Reading {
    float value = 0;
    uint32_t at = 0;  // millis() of the last update
    bool valid = false;
};

struct Snapshot {
    Reading ch[kChannelCount];

    void set(Channel c, float v, uint32_t now) {
        if (c >= kChannelCount) return;
        ch[c].value = v;
        ch[c].at = now;
        ch[c].valid = true;
    }
    /** Updated within kStaleMs (millis() wrap safe). */
    bool fresh(Channel c, uint32_t now) const {
        return c < kChannelCount && ch[c].valid && (uint32_t)(now - ch[c].at) <= kStaleMs;
    }
    float value(Channel c) const { return c < kChannelCount ? ch[c].value : 0.0f; }
    bool anyFresh(uint32_t now) const {
        for (int i = 0; i < kChannelCount; i++) {
            if (fresh((Channel)i, now)) return true;
        }
        return false;
    }
    void clear() { *this = Snapshot(); }
};

/** How the dash looks (order = NVS value: append only). Same readings, same warnings in every style. */
enum class DashStyle : uint8_t {
    Race,     // rpm bar across the top + big digits (racing dash)
    Digital,  // one round LED ring for the rpm, digital speed inside (sport)
    Classic,  // two round dials with needles: tachometer + speedometer
    Count
};

inline const char* styleKey(DashStyle s) {
    static const char* const kKeys[] = {"race", "digital", "classic"};
    return (uint8_t)s < (uint8_t)DashStyle::Count ? kKeys[(uint8_t)s] : "race";
}

inline const char* styleNameTh(DashStyle s) {
    static const char* const kNames[] = {"รถแข่ง", "วงกลมดิจิทัล สปอร์ต", "เข็มวงกลม คลาสสิก"};
    return (uint8_t)s < (uint8_t)DashStyle::Count ? kNames[(uint8_t)s] : kNames[0];
}

inline bool parseStyle(const char* text, DashStyle& out) {
    if (!text) return false;
    for (uint8_t i = 0; i < (uint8_t)DashStyle::Count; i++) {
        if (ieq(text, styleKey((DashStyle)i))) {
            out = (DashStyle)i;
            return true;
        }
    }
    if (ieq(text, "sport") || ieq(text, "ring")) return (out = DashStyle::Digital), true;
    if (ieq(text, "needle") || ieq(text, "analog")) return (out = DashStyle::Classic), true;
    if (ieq(text, "racing") || ieq(text, "bar")) return (out = DashStyle::Race), true;
    return false;
}

/** Car or motorcycle: scales (rpm range), gear estimate and labels follow it. */
enum class Kind : uint8_t { Car, Motorcycle };

/** Where the readings come from (order = NVS value: append only). */
enum class SourceId : uint8_t {
    Off,
    SimAuto,     // built-in simulator driving a loop by itself
    SimTouch,    // simulator, the finger on the screen is the throttle
    HondaK,      // Honda PGM-FI K-Line (Wave 110i / 125i, Click, PCX, ...) via an L9637D-type adapter on the UART pins
    ObdBle,      // OBD-II through an ELM327 Bluetooth LE adapter
    ObdUart,     // OBD-II through a wired ELM327 / STN on the UART pins
    SerialText,  // "rpm=3200 spd=86 ..." lines from another board on the UART pins
    PhoneGps,    // speed from the phone's GPS (the phone app over USB / Wi-Fi, wiki 18): any vehicle, no adapter
    Count
};

inline const char* sourceKey(SourceId s) {
    static const char* const kKeys[] = {"off", "sim", "touch", "kline", "obd_ble", "obd_uart", "serial", "phone"};
    return (uint8_t)s < (uint8_t)SourceId::Count ? kKeys[(uint8_t)s] : "off";
}

inline const char* sourceNameTh(SourceId s) {
    static const char* const kNames[] = {"ปิด",          "จำลอง (ขับเอง)", "จำลอง (แตะเป็นคันเร่ง)",
                                         "Honda K-Line", "OBD-II Bluetooth", "OBD-II สาย UART",
                                         "ข้อความผ่าน UART", "GPS มือถือ"};
    return (uint8_t)s < (uint8_t)SourceId::Count ? kNames[(uint8_t)s] : "ปิด";
}

/** Uses GPIO 43 / 44 (the UART header). */
inline bool usesUart(SourceId s) {
    return s == SourceId::HondaK || s == SourceId::ObdUart || s == SourceId::SerialText;
}

inline bool parseSource(const char* text, SourceId& out) {
    if (!text) return false;
    for (uint8_t i = 0; i < (uint8_t)SourceId::Count; i++) {
        if (ieq(text, sourceKey((SourceId)i))) {
            out = (SourceId)i;
            return true;
        }
    }
    struct Alias {
        const char* name;
        SourceId id;
    };
    static const Alias kAliases[] = {{"none", SourceId::Off},     {"simulator", SourceId::SimAuto},
                                     {"honda", SourceId::HondaK}, {"k-line", SourceId::HondaK},
                                     {"ble", SourceId::ObdBle},   {"obd", SourceId::ObdBle},
                                     {"elm", SourceId::ObdUart},  {"uart", SourceId::SerialText},
                                     {"gps", SourceId::PhoneGps}};
    for (const Alias& a : kAliases) {
        if (ieq(text, a.name)) {
            out = a.id;
            return true;
        }
    }
    return false;
}

/**
 * One text line from another board (SerialText source): "rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3"
 * (also "key:value", commas, any order, any subset) or a flat JSON object with the same or the long keys
 * ({"rpm":3200,"speed_kmh":86}). Returns how many channels were set.
 */
inline int parseTextLine(const char* line, Snapshot& s, uint32_t now) {
    struct Key {
        const char* name;
        Channel ch;
    };
    static const Key kKeys[] = {
        {"rpm", kRpm},           {"spd", kSpeed},          {"speed", kSpeed},      {"speed_kmh", kSpeed},
        {"kmh", kSpeed},         {"clt", kCoolant},        {"coolant", kCoolant},  {"coolant_c", kCoolant},
        {"temp", kCoolant},      {"oil", kCoolant},        {"iat", kIntake},       {"intake", kIntake},
        {"intake_c", kIntake},   {"volt", kBattery},       {"batt", kBattery},     {"battery", kBattery},
        {"battery_v", kBattery}, {"voltage", kBattery},    {"tps", kThrottle},     {"throttle", kThrottle},
        {"throttle_pct", kThrottle}, {"map", kMap},        {"map_kpa", kMap},      {"gear", kGear},
    };
    if (!line) return 0;
    int set = 0;
    const char* p = line;
    while (*p) {
        while (*p && !isalpha((unsigned char)*p)) p++;  // skips spaces, commas, quotes, braces
        const char* k = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
        const size_t klen = (size_t)(p - k);
        while (*p == '"' || *p == ' ') p++;
        if (!klen || (*p != '=' && *p != ':')) continue;
        p++;
        while (*p == ' ' || *p == '"') p++;
        char* end = nullptr;
        const float v = strtof(p, &end);
        if (end == p) continue;
        p = end;
        for (const Key& key : kKeys) {
            if (strlen(key.name) == klen && ieqn(k, key.name, klen)) {
                s.set(key.ch, v, now);
                set++;
                break;
            }
        }
    }
    return set;
}

}  // namespace vehicle
