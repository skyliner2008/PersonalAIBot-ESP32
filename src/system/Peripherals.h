/**
 * @file Peripherals.h
 * @brief Devices on the free ports (relay, dimmer, button/sensor, analog), from agent/devices.json
 * @see wiki/15-Expansion-Ports.md, util/DeviceConfig.h (format, allowed pins)
 *
 * The assistant uses them through the `device` tool (declared only when at least one is configured).
 * Thread-safe (mutex): the agent task runs the tool and polls the inputs, the console may read/set.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../util/DeviceConfig.h"

class Peripherals {
public:
    /** Load `path` (agent/devices.json) and set the pins up. Safe to call again (reload). */
    void begin(const char* path);
    int count() const { return _count; }
    /** Problems found in the file ("" = none). */
    const char* errors() const { return _errors; }

    /** [{name, type, pin, state}] */
    void describe(JsonArray out);
    /** nullptr = ok (out: name, type, state / volts / percent), else why not. */
    const char* read(const char* name, JsonObject out);
    /** switch: on/off, dimmer: 0-100. nullptr = ok, else why not. */
    const char* set(const char* name, JsonVariantConst value, JsonObject out);
    /** One line for the system prompt: "พัดลม (switch, ปิด), ประตู (input, ...)" - "" without devices. */
    void summary(String& out);
    /** A device in the file uses this GPIO (the vehicle dash needs 43 / 44 for its UART sources). */
    bool usesPin(int pin);

    /**
     * Debounced inputs with "notify": calls `changed(name, active)` for each confirmed change (at most one
     * per device every kMinNotifyMs). Call often (agent loop).
     */
    template <typename F>
    void poll(uint32_t now, F changed) {
        if (!_count || now - _lastPoll < kPollMs) return;
        // the console's `devices reload` rewrites the list under the lock: skip this poll rather than read
        // half a device (and never hold up the agent loop waiting) - T069
        if (!_lock || xSemaphoreTake(_lock, 0) != pdTRUE) return;
        _lastPoll = now;
        struct Fired {
            char name[sizeof(periph::Spec::name)];
            bool active;
        } fired[periph::kMaxDevices];
        int nFired = 0;
        for (int i = 0; i < _count; i++) {
            Dev& d = _devs[i];
            if (d.spec.kind != periph::Kind::Input) continue;
            const bool active = readInput(d);
            if (active == d.state) {
                d.pending = 0;
                continue;
            }
            if (++d.pending < 2) continue;  // stable for two polls (~100 ms): not a bounce
            d.pending = 0;
            d.state = active;
            if (d.spec.notify && now - d.lastNotify >= kMinNotifyMs) {
                d.lastNotify = now;
                memcpy(fired[nFired].name, d.spec.name, sizeof(fired[nFired].name));
                fired[nFired++].active = active;
            }
        }
        xSemaphoreGive(_lock);
        // outside the lock, with copies of the names (a reload may rewrite the list right after)
        for (int i = 0; i < nFired; i++) changed(fired[i].name, fired[i].active);
    }

private:
    struct Dev {
        periph::Spec spec;
        bool state = false;  // switch on / input active
        int level = 0;       // dimmer 0-100
        uint8_t pending = 0;
        uint32_t lastNotify = 0;
    };
    static constexpr uint32_t kPollMs = 50;
    static constexpr uint32_t kMinNotifyMs = 30000;  // a busy PIR must not wake the assistant every second

    Dev* find(const char* name, const char*& why);  // why: not found / several match
    bool readInput(const Dev& d) const;
    void fill(const Dev& d, JsonObject out) const;

    Dev _devs[periph::kMaxDevices];
    int _count = 0;
    char _errors[240] = "";
    uint32_t _lastPoll = 0;
    SemaphoreHandle_t _lock = nullptr;
};

extern Peripherals gPeriph;
