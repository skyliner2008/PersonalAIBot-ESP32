/**
 * @file Peripherals.cpp
 * @brief Devices on the free ports - see Peripherals.h
 */

#include "Peripherals.h"

#include "../util/JsonAlloc.h"
#include "../util/JsonFile.h"

Peripherals gPeriph;

namespace {
constexpr uint32_t kPwmHz = 5000;  // LED dimming without visible flicker
constexpr uint8_t kPwmBits = 10;

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) { if (m) xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { if (m) xSemaphoreGive(m); }
};
}  // namespace

void Peripherals::begin(const char* path) {
    if (!_lock) _lock = xSemaphoreCreateMutex();
    Guard g(_lock);
    for (int i = 0; i < _count; i++) {  // reload: release what the old list used
        if (_devs[i].spec.kind == periph::Kind::Dimmer) ledcDetach(_devs[i].spec.pin);
        pinMode(_devs[i].spec.pin, INPUT);
    }
    _count = 0;
    _errors[0] = '\0';
    JsonDocument doc(JsonPsramAllocator::instance());
    if (!path || !*path || !jsonfile::load(path, doc)) return;  // no file = no devices (nothing to say)
    periph::Spec specs[periph::kMaxDevices];
    const int n = periph::parse(doc, specs, _errors, sizeof(_errors));
    for (int i = 0; i < n; i++) {
        Dev& d = _devs[_count];
        d = Dev{};
        d.spec = specs[i];
        const int pin = d.spec.pin;
        switch (d.spec.kind) {
            case periph::Kind::Switch:
                pinMode(pin, OUTPUT);
                digitalWrite(pin, d.spec.activeLow ? HIGH : LOW);  // off at boot
                break;
            case periph::Kind::Dimmer:
                if (!ledcAttach(pin, kPwmHz, kPwmBits)) {
                    const size_t len = strlen(_errors);
                    snprintf(_errors + len, sizeof(_errors) - len, "%s: PWM not available; ", d.spec.name);
                    continue;
                }
                ledcWrite(pin, 0);
                break;
            case periph::Kind::Input:
                pinMode(pin, d.spec.pullUp ? INPUT_PULLUP : (d.spec.pullDown ? INPUT_PULLDOWN : INPUT));
                d.state = readInput(d);
                break;
            case periph::Kind::Analog:
                pinMode(pin, ANALOG);
                analogSetPinAttenuation(pin, ADC_11db);  // 0-3.1 V
                break;
        }
        _count++;
    }
    Serial.printf("[Devices] %d from %s%s%s\n", _count, path, _errors[0] ? " - skipped: " : "", _errors);
}

// "ไฟ" for "ไฟโต๊ะ": a part of the name is enough when only one device has it
Peripherals::Dev* Peripherals::find(const char* name, const char*& why) {
    const int i = periph::findByName(_count, name, [this](int k) { return (const char*)_devs[k].spec.name; });
    if (i >= 0) return &_devs[i];
    why = i == periph::kAmbiguous ? "several devices match that name: use the full name from the list"
                                  : "no device with that name (see the list)";
    return nullptr;
}

bool Peripherals::readInput(const Dev& d) const {
    const bool high = digitalRead(d.spec.pin) == HIGH;
    // pull-up wiring (button / contact to GND): pressed = LOW, unless the file says otherwise
    const bool activeLow = d.spec.activeLow || d.spec.pullUp;
    return activeLow ? !high : high;
}

void Peripherals::fill(const Dev& d, JsonObject out) const {
    out["name"] = d.spec.name;
    out["type"] = periph::kindName(d.spec.kind);
    out["pin"] = d.spec.pin;
    switch (d.spec.kind) {
        case periph::Kind::Switch: out["state"] = d.state ? "on" : "off"; break;
        case periph::Kind::Dimmer: out["level_percent"] = d.level; break;
        case periph::Kind::Input: out["state"] = readInput(d) ? "active" : "idle"; break;
        case periph::Kind::Analog: {
            const uint32_t mv = analogReadMilliVolts(d.spec.pin);
            out["volts"] = mv / 1000.0f;
            out["percent"] = (int)(mv * 100 / 3100);  // of the 0-3.1 V range
            break;
        }
    }
}

void Peripherals::describe(JsonArray out) {
    Guard g(_lock);
    for (int i = 0; i < _count; i++) fill(_devs[i], out.add<JsonObject>());
}

const char* Peripherals::read(const char* name, JsonObject out) {
    Guard g(_lock);
    const char* why = nullptr;
    Dev* d = find(name, why);
    if (!d) return why;
    fill(*d, out);
    return nullptr;
}

const char* Peripherals::set(const char* name, JsonVariantConst value, JsonObject out) {
    Guard g(_lock);
    const char* why = nullptr;
    Dev* d = find(name, why);
    if (!d) return why;
    const int level = periph::parseLevel(value);
    switch (d->spec.kind) {
        case periph::Kind::Switch:
            if (level < 0) return "value must be on or off";
            d->state = level > 0;
            digitalWrite(d->spec.pin, d->state != d->spec.activeLow ? HIGH : LOW);
            break;
        case periph::Kind::Dimmer:
            if (level < 0) return "value must be 0-100 (or on/off)";
            d->level = level;
            ledcWrite(d->spec.pin, (uint32_t)level * ((1u << kPwmBits) - 1) / 100);
            break;
        default:
            return "this device can only be read";
    }
    fill(*d, out);
    return nullptr;
}

bool Peripherals::usesPin(int pin) {
    Guard g(_lock);
    for (int i = 0; i < _count; i++) {
        if (_devs[i].spec.pin == pin) return true;
    }
    return false;
}

void Peripherals::summary(String& out) {
    Guard g(_lock);
    out = "";
    for (int i = 0; i < _count; i++) {
        if (i) out += ", ";
        out += _devs[i].spec.name;
        out += " (";
        out += periph::kindName(_devs[i].spec.kind);
        out += ")";
    }
}
