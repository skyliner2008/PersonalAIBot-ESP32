/**
 * @file DeviceConfig.h
 * @brief Devices the user wires to the free ports, described in agent/devices.json (header-only, host tested)
 * @see wiki/15-Expansion-Ports.md
 *
 * {"devices": [
 *   {"name": "พัดลม",  "type": "switch", "pin": 21, "active_low": false},   relay / LED / buzzer: on-off
 *   {"name": "ไฟโต๊ะ", "type": "dimmer", "pin": 2},                        PWM 0-100 % (LED strip via MOSFET)
 *   {"name": "ประตู",  "type": "input",  "pin": 3, "pull": "up", "notify": true},  switch / PIR / door contact
 *   {"name": "แสง",    "type": "analog", "pin": 2}                         0-3.3 V (LDR, potentiometer, soil)
 * ]}
 *
 * Only the free pins are accepted (the screen, SD card, audio and touch use the rest - a wrong number there
 * would break them): 2, 3, 14, 21 on the Expand header and 43/44 (the UART header, when nothing else uses it).
 * Not GPIO 0: the BOOT key is already a UI button (tap / long press = settings). Analog needs ADC1 - only GPIO 2 and 3: ADC2 (GPIO 14) stops while
 * Wi-Fi runs. Up to kMaxDevices entries; a bad entry is skipped with a reason, the rest still load.
 */

#pragma once

#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace periph {

enum class Kind : uint8_t { Switch, Dimmer, Input, Analog };

struct Spec {
    char name[40];
    Kind kind;
    int8_t pin;
    bool activeLow;  // switch: LOW = on (most relay boards) · input: pressed / triggered = LOW
    bool pullUp;     // input: internal pull-up (buttons and contacts to GND)
    bool pullDown;
    bool notify;     // input: tell the assistant when it changes
};

constexpr int kMaxDevices = 8;

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::Switch: return "switch";
        case Kind::Dimmer: return "dimmer";
        case Kind::Input: return "input";
        default: return "analog";
    }
}

inline bool parseKind(const char* s, Kind& out) {
    static const char* const kNames[] = {"switch", "dimmer", "input", "analog"};
    for (int i = 0; i < 4; i++) {
        if (s && !strcmp(s, kNames[i])) {
            out = (Kind)i;
            return true;
        }
    }
    return false;
}

/** nullptr = this pin may be used for this kind, else why not. */
inline const char* pinProblem(Kind k, int pin) {
#if defined(BOARD_JC4880P443C) || defined(CONFIG_IDF_TARGET_ESP32P4)
    switch (pin) {
        case 20:  // ADC1 (GPIO 16..23: 16..19 are the ESP32-C6 SDIO link, 21..23 touch + backlight)
        case 49:
        case 50:
        case 51:
        case 52: return nullptr;  // ADC2 (49..54; 53 = battery, 54 = C6 reset) - no Wi-Fi radio on the P4 to share it
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 54: return "GPIO 14..19 and 54 link the ESP32-C6 (Wi-Fi / Bluetooth, SDIO) - using one cuts the network";
        case 1:
        case 2:
        case 3:
        case 4:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33:
        case 36:
        case 46:
        case 47:
            return k == Kind::Analog ? "analog only on GPIO 20 and 49..52 (ADC)" : nullptr;
        case 35: return "GPIO 35 is the BOOT key, already a button of the screen (tap / settings)";
        default: return "not a free pin on JC4880P443C (use JP1 expansion pins 1..4, 20, 26..33, 36, 46, 47, 49..52)";
    }
#else
    switch (pin) {
        case 2:
        case 3: return nullptr;  // Expand header, ADC1 (analog works with Wi-Fi on)
        case 14:
        case 21:
        case 43:
        case 44:
            return k == Kind::Analog ? "analog only on GPIO 2 or 3 (ADC1; ADC2 stops while Wi-Fi runs)" : nullptr;
        case 0: return "GPIO 0 is the BOOT key, already a button of the screen (tap / settings)";
        default: return "not a free pin (free: 2, 3, 14, 21 on the Expand header, 43/44 on the UART header)";
    }
#endif
}

/**
 * Parse doc["devices"]. Returns how many specs were filled; each rejected entry appends "name: reason; " to
 * `errors` (truncated to errCap). Duplicate pins and names are rejected.
 */
inline int parse(JsonDocument& doc, Spec* out, char* errors, size_t errCap) {
    if (errCap) errors[0] = '\0';
    auto fail = [&](const char* who, const char* why) {
        const size_t len = strlen(errors);
        if (len + 4 < errCap) snprintf(errors + len, errCap - len, "%s: %s; ", who && *who ? who : "?", why);
    };
    int n = 0;
    for (JsonObjectConst d : doc["devices"].as<JsonArrayConst>()) {
        const char* name = d["name"] | "";
        if (n >= kMaxDevices) {
            fail(name, "too many devices");
            break;
        }
        Spec s{};
        if (!*name || strlen(name) >= sizeof(s.name)) {
            fail(name, "name missing or too long");
            continue;
        }
        if (!parseKind(d["type"] | "", s.kind)) {
            fail(name, "type must be switch, dimmer, input or analog");
            continue;
        }
        if (!d["pin"].is<int>()) {
            fail(name, "pin missing");
            continue;
        }
        const int pin = d["pin"].as<int>();
        if (const char* why = pinProblem(s.kind, pin)) {
            fail(name, why);
            continue;
        }
        bool clash = false;
        for (int i = 0; i < n; i++) clash |= out[i].pin == pin || !strcmp(out[i].name, name);
        if (clash) {
            fail(name, "pin or name used twice");
            continue;
        }
        strncpy(s.name, name, sizeof(s.name) - 1);
        s.pin = (int8_t)pin;
        s.activeLow = d["active_low"] | false;
        const char* pull = d["pull"] | (s.kind == Kind::Input ? "up" : "none");
        s.pullUp = !strcmp(pull, "up");
        s.pullDown = !strcmp(pull, "down");
        s.notify = s.kind == Kind::Input && (d["notify"] | false);
        out[n++] = s;
    }
    return n;
}

constexpr int kNoMatch = -1;
constexpr int kAmbiguous = -2;

/**
 * Index of the device called `name`: the exact name first, else the one device whose name contains it
 * ("ไฟ" for "ไฟโต๊ะ"). kNoMatch, or kAmbiguous when the part fits several ("ไฟ" with "ไฟโต๊ะ" and
 * "ไฟห้องนอน": guessing the first one switched the wrong relay - T069).
 */
template <class NameAt>  // NameAt: int -> const char* (name of device i)
int findByName(int n, const char* name, NameAt nameAt) {
    if (!name || !*name) return kNoMatch;
    for (int i = 0; i < n; i++) {
        if (!strcmp(nameAt(i), name)) return i;
    }
    int found = kNoMatch;
    for (int i = 0; i < n; i++) {
        if (!strstr(nameAt(i), name)) continue;
        if (found != kNoMatch) return kAmbiguous;
        found = i;
    }
    return found;
}

/** "on", "off", "50", "50%", true/false, numbers -> 0-100 (-1 = not understood). */
inline int parseLevel(JsonVariantConst v) {
    if (v.is<bool>()) return v.as<bool>() ? 100 : 0;
    if (v.is<int>() || v.is<float>()) {
        const float f = v.as<float>();
        return f < 0 ? 0 : (f > 100 ? 100 : (int)(f + 0.5f));
    }
    const char* s = v | "";
    if (!strcmp(s, "on") || !strcmp(s, "เปิด") || !strcmp(s, "true")) return 100;
    if (!strcmp(s, "off") || !strcmp(s, "ปิด") || !strcmp(s, "false")) return 0;
    if (*s >= '0' && *s <= '9') {
        int x = 0;
        for (; *s >= '0' && *s <= '9'; s++) x = x * 10 + (*s - '0');
        return x > 100 ? 100 : x;
    }
    return -1;
}

}  // namespace periph
