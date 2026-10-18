/**
 * @file DashModel.cpp
 * @brief Dash state from the readings (see DashModel.h)
 */

#include "DashModel.h"

#include <math.h>

namespace vehicle {

DashConfig defaultConfig(Kind kind) {
    DashConfig c;
    c.kind = kind;
    if (kind == Kind::Motorcycle) {
        c.rpmMax = 10000;
        c.shiftRpm = 8000;
        c.coolantCold = 50;   // air-cooled engines read oil / head temperature: hotter than a car's coolant
        c.coolantWarn = 120;
        c.coolantCrit = 135;
        c.batteryLow = 12.0f;
        c.batteryHigh = 15.0f;
        c.gears = 0;
        c.tireCircM = 1.68f;  // 80/90-17
    }
    return c;
}

namespace {

Level levelFor(float v, float cold, float warn, float crit) {
    if (v >= crit) return Level::Crit;
    if (v >= warn) return Level::Warn;
    if (v < cold) return Level::Cold;
    return Level::Normal;
}

}  // namespace

int DashModel::estimateGear(const DashConfig& c, float rpm, float speedKmh) {
    if (c.gears <= 0 || c.tireCircM <= 0 || c.finalDrive <= 0) return -1;
    if (speedKmh < 4.0f) return rpm < 1800 ? 0 : -1;  // standing still: neutral (or clutch in)
    // engine rpm = wheel rpm x ratio x final drive; wheel rpm = km/h x 1000 m / 60 min / circumference
    const float wheelRpm = speedKmh * 1000.0f / 60.0f / c.tireCircM;
    const float overall = rpm / wheelRpm;  // the gearing the engine sees right now
    int best = -1;
    float bestErr = 1e9f;
    for (int i = 0; i < c.gears && i < 6; i++) {
        const float g = c.ratio[i] * c.finalDrive;
        if (g <= 0) continue;
        const float err = fabsf(overall - g) / g;
        if (err < bestErr) {
            bestErr = err;
            best = i + 1;
        }
    }
    return bestErr <= 0.12f ? best : 0;  // slipping clutch / coasting in neutral: no gear fits
}

const DashView& DashModel::update(const Snapshot& s, uint32_t now) {
    const float dt = _last ? (float)(uint32_t)(now - _last) / 1000.0f : 0.0f;
    _last = now;
    DashView v;
    v.live = s.anyFresh(now);

    v.rpmValid = s.fresh(kRpm, now);
    if (v.rpmValid) {
        const float target = s.value(kRpm) < 0 ? 0 : s.value(kRpm);
        // quick to rise (the needle must not lag a blip), a little slower to fall: no flicker at ~10 Hz data
        const float tau = target > _smoothRpm ? 0.05f : 0.12f;
        const float a = dt <= 0 ? 1.0f : (dt >= tau ? 1.0f : dt / tau);
        _smoothRpm += (target - _smoothRpm) * a;
        if (target > _peak) _peak = target;
    } else {
        _smoothRpm = 0;
    }
    v.rpm = _smoothRpm;
    v.peakRpm = _peak;
    v.shift = v.rpmValid && s.value(kRpm) >= _cfg.shiftRpm;
    v.shiftOn = v.shift && ((now / 70) & 1) == 0;  // ~7 Hz flash

    v.speedValid = s.fresh(kSpeed, now);
    v.speed = v.speedValid ? s.value(kSpeed) : 0;

    if (s.fresh(kGear, now)) {
        v.gear = (int)lroundf(s.value(kGear));
    } else if (v.rpmValid && v.speedValid) {
        const int g = estimateGear(_cfg, s.value(kRpm), v.speed);
        // hold a new estimate 250 ms before showing it: a shift in progress reads as "no gear"
        if (g != _lastGear) {
            _lastGear = g;
            _gearSince = now;
        }
        v.gear = (uint32_t)(now - _gearSince) >= 250 ? g : _view.gear;
        v.gearEstimated = true;
    }

    if (s.fresh(kCoolant, now)) {
        v.coolant = s.value(kCoolant);
        v.coolantLevel = levelFor(v.coolant, _cfg.coolantCold, _cfg.coolantWarn, _cfg.coolantCrit);
    }
    if (s.fresh(kIntake, now)) {
        v.intake = s.value(kIntake);
        v.intakeLevel = levelFor(v.intake, -100, 60, 75);
    }
    if (s.fresh(kBattery, now)) {
        v.battery = s.value(kBattery);
        const bool running = v.rpmValid && s.value(kRpm) > 400;
        if (v.battery >= _cfg.batteryHigh) v.batteryLevel = Level::Crit;
        else if (running && v.battery < _cfg.batteryLow) v.batteryLevel = Level::Warn;
        else if (!running && v.battery < 11.6f) v.batteryLevel = Level::Warn;  // flat battery with the engine off
        else v.batteryLevel = Level::Normal;
    }
    if (s.fresh(kThrottle, now)) {
        v.throttle = s.value(kThrottle);
        v.throttleLevel = Level::Normal;
    }
    _view = v;
    return _view;
}

}  // namespace vehicle
