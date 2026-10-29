/**
 * @file VehicleHealth.cpp
 * @brief Vehicle health findings and the trip (see VehicleHealth.h)
 */

#include "VehicleHealth.h"

#include <math.h>
#include <string.h>

namespace vehicle {

namespace {

struct Text {
    const char* key;
    const char* th;
    uint16_t speakMin;
};

const Text kText[(int)Finding::Count] = {
    {"check-engine light on", "ไฟเช็คเครื่องยนต์ติด", 0},  // the new codes themselves are announced (Vehicle)
    {"trouble codes stored", "มีรหัสปัญหาในกล่อง ECU", 0},
    {"pending codes (seen once, not confirmed yet)", "มีรหัสปัญหารอยืนยัน", 0},
    {"engine overheating", "เครื่องยนต์ร้อนจัด", 5},
    {"engine temperature rising fast", "อุณหภูมิเครื่องขึ้นเร็วผิดปกติ", 10},
    {"engine never warms up (thermostat stuck open?)", "เครื่องไม่ถึงอุณหภูมิทำงาน (วาล์วน้ำ?)", 0},
    {"battery not charging while the engine runs (alternator / belt?)", "ไดชาร์จไม่ชาร์จ", 5},
    {"charging voltage too high (regulator?)", "ไฟชาร์จสูงเกิน", 5},
    {"battery low with the engine off", "แบตเตอรี่อ่อน", 30},
    {"battery dipped low while cranking (weak battery?)", "แบตอ่อนตอนสตาร์ท", 60},
    {"fuel mixture lean, bank 1 (ECU adding fuel)", "ส่วนผสมบาง (ฝั่ง 1)", 30},
    {"fuel mixture rich, bank 1 (ECU taking fuel away)", "ส่วนผสมหนา (ฝั่ง 1)", 30},
    {"fuel mixture lean, bank 2", "ส่วนผสมบาง (ฝั่ง 2)", 30},
    {"fuel mixture rich, bank 2", "ส่วนผสมหนา (ฝั่ง 2)", 30},
    {"lean at idle only: an intake / vacuum leak is likely", "บางเฉพาะรอบเดินเบา (อาจมีรอยรั่วอากาศ)", 0},
    {"rough idle (rpm unsteady: misfire, idle valve, mounts?)", "เดินเบาไม่นิ่ง", 0},
    {"fuel low", "น้ำมันใกล้หมด", 15},
    {"hot intake air (heat soak)", "อากาศเข้าร้อน", 0},
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

const char* findingKey(Finding f) { return (int)f < (int)Finding::Count ? kText[(int)f].key : "?"; }
const char* findingTh(Finding f) { return (int)f < (int)Finding::Count ? kText[(int)f].th : "?"; }
uint16_t findingSpeakMin(Finding f) { return (int)f < (int)Finding::Count ? kText[(int)f].speakMin : 0; }

const char* severityKey(uint8_t s) {
    static const char* const kKeys[] = {"ok", "info", "warning", "critical"};
    return s <= kSevCrit ? kKeys[s] : "ok";
}

uint8_t HealthMonitor::restPercent(float v) {
    static const float kV[] = {11.9f, 12.0f, 12.1f, 12.2f, 12.3f, 12.4f, 12.5f, 12.6f, 12.7f};
    static const uint8_t kPct[] = {0, 10, 25, 40, 55, 70, 80, 90, 100};
    if (v <= kV[0]) return 0;
    for (int i = 1; i < 9; i++) {
        if (v < kV[i]) return (uint8_t)lroundf(kPct[i - 1] + (kPct[i] - kPct[i - 1]) * (v - kV[i - 1]) / (kV[i] - kV[i - 1]));
    }
    return 100;
}

float HealthMonitor::fuelFlow(const Snapshot& s, uint32_t now, const HealthConfig& c, uint8_t& from) {
    from = 0;
    if (s.fresh(kFuelRate, now)) {
        from = 1;
        return s.value(kFuelRate) > 0 ? s.value(kFuelRate) : 0;
    }
    if (c.diesel || !s.fresh(kRpm, now) || s.value(kRpm) < 300) return 0;
    constexpr float kAfr = 14.7f, kDensity = 745.0f;  // petrol / gasohol: air-fuel ratio, grams per litre
    if (s.fresh(kMaf, now)) {
        from = 2;
        return s.value(kMaf) * 3600.0f / (kAfr * kDensity);
    }
    if (s.fresh(kMap, now) && c.engineL > 0) {
        // speed-density: the cylinders fill with air at MAP and intake temperature, 85 % volumetric efficiency
        const float iat = s.fresh(kIntake, now) ? s.value(kIntake) : 30.0f;
        const float grams = s.value(kMap) * 1000.0f * (c.engineL / 1000.0f) * (s.value(kRpm) / 120.0f) * 0.85f *
                            28.97f / (8.314f * (iat + 273.15f));
        from = 3;
        return grams * 3600.0f / (kAfr * kDensity);
    }
    return 0;
}

void HealthMonitor::reset(const HealthConfig& c) {
    *this = HealthMonitor();
    _cfg = c;
}

bool HealthMonitor::held(Finding f, bool cond, uint32_t now, uint32_t holdMs) {
    uint32_t& since = _since[(int)f];
    if (!cond) {
        since = 0;
        return false;
    }
    if (!since) since = now ? now : 1;
    return (uint32_t)(now - since) >= holdMs;
}

void HealthMonitor::add(Finding f, uint8_t sev, float value) {
    if (_h.n >= sizeof(_h.issue) / sizeof(_h.issue[0])) {  // full: a more serious one replaces the mildest
        int low = 0;
        for (int i = 1; i < _h.n; i++) {
            if (_h.issue[i].sev < _h.issue[low].sev) low = i;
        }
        if (_h.issue[low].sev >= sev) return;
        _h.issue[low] = {f, sev, value};
    } else {
        _h.issue[_h.n++] = {f, sev, value};
    }
    if (sev > _h.worst) _h.worst = sev;
}

void HealthMonitor::update(const Snapshot& s, const obd::Diag* d, uint32_t now) {
    if (_last && (uint32_t)(now - _last) < 250) return;
    const float dt = _last ? (float)(uint32_t)(now - _last) / 1000.0f : 0.0f;
    _last = now;
    const bool integrate = dt > 0 && dt < 3.0f;  // a pause (the source restarted): nothing in between is known
    Health& h = _h;
    h.n = 0;
    h.worst = kSevOk;
    const bool car = _cfg.kind == Kind::Car;

    const bool rpmOk = s.fresh(kRpm, now);
    const float rpm = rpmOk ? s.value(kRpm) : 0;
    const bool running = rpmOk && rpm > 400;
    const bool speedOk = s.fresh(kSpeed, now);
    const float speed = speedOk ? s.value(kSpeed) : 0;
    const bool coolOk = s.fresh(kCoolant, now);
    const float coolant = coolOk ? s.value(kCoolant) : 0;
    const bool warm = !coolOk || coolant >= 70;
    const bool battOk = s.fresh(kBattery, now);
    const float batt = battOk ? s.value(kBattery) : 0;

    // ---- starts and stops; the voltage dip while cranking ----
    if (running && !_runSince) {
        _runSince = now ? now : 1;
        h.startMinV = _offMinAt && (uint32_t)(now - _offMinAt) < 8000 && _offMin < 11.5f ? _offMin : 0;
        _offMin = 99;
    } else if (!running && _runSince) {
        _runSince = 0;
        _offSince = now ? now : 1;
    } else if (!running && !_offSince) {
        _offSince = now ? now : 1;
    }
    h.running = running;
    const float runS = running ? (float)(uint32_t)(now - _runSince) / 1000.0f : 0;
    if (battOk && !running && (batt < _offMin || (uint32_t)(now - _offMinAt) > 6000)) {
        _offMin = batt;  // the lowest of the last few seconds
        _offMinAt = now;
    }

    // ---- battery / charging ----
    if (battOk) {
        if (!running && _offSince && (uint32_t)(now - _offSince) > 60000) {
            h.restValid = true;
            h.restV = batt;
            h.restPct = restPercent(batt);
        }
        if (held(Finding::NotCharging, running && runS > 60 && batt < 12.3f, now, 30000)) {
            add(Finding::NotCharging, kSevCrit, batt);
        }
        if (held(Finding::Overcharge, batt > (car ? 15.2f : 15.0f), now, 5000)) add(Finding::Overcharge, kSevCrit, batt);
        if (held(Finding::LowBatteryParked, !running && h.restValid && batt < 12.2f, now, 30000)) {
            add(Finding::LowBatteryParked, batt < 11.9f ? kSevCrit : kSevWarn, batt);
        }
    } else {
        held(Finding::NotCharging, false, now, 0);
        held(Finding::Overcharge, false, now, 0);
        held(Finding::LowBatteryParked, false, now, 0);
    }
    if (h.startMinV > 0 && h.startMinV < 10.0f) add(Finding::WeakStart, h.startMinV < 9.6f ? kSevWarn : kSevInfo, h.startMinV);

    // ---- engine temperature: level and how fast it rises ----
    if (coolOk) {
        if (coolant > _maxCoolant) _maxCoolant = coolant;
        if ((int32_t)(now - _coolNext) >= 0) {  // a sample every 10 s, the rise over the last minute
            _coolNext = now + 10000;
            _coolRing[_coolPos] = coolant;
            _coolRingAt[_coolPos] = now;
            _coolPos = (uint8_t)((_coolPos + 1) % 7);
            if (_coolN < 7) _coolN++;
        }
        const uint8_t oldest = _coolN < 7 ? 0 : _coolPos;
        const float span = (float)(uint32_t)(now - _coolRingAt[oldest]) / 60000.0f;
        h.coolantRateValid = _coolN >= 3 && span >= 0.33f;
        h.coolantRate = h.coolantRateValid ? (coolant - _coolRing[oldest]) / span : 0;
        const bool crit = coolant >= _cfg.coolantCrit, warn = coolant >= _cfg.coolantWarn;
        if (held(Finding::Overheat, warn, now, 3000)) add(Finding::Overheat, crit ? kSevCrit : kSevWarn, coolant);
        if (held(Finding::OverheatRising,
                 !warn && h.coolantRateValid && coolant >= _cfg.coolantWarn - 8 && h.coolantRate >= 3.0f, now, 20000)) {
            add(Finding::OverheatRising, kSevWarn, h.coolantRate);
        }
        if (car && running && runS > 15 * 60 && _maxCoolant < 70) add(Finding::ColdEngine, kSevInfo, _maxCoolant);
    } else {
        h.coolantRateValid = false;
        held(Finding::Overheat, false, now, 0);
        held(Finding::OverheatRising, false, now, 0);
    }

    // ---- fuel trims: short + long, per bank, while warm and steady ----
    const bool trimNow = running && runS > 60 && warm && rpm < 4000;
    for (int b = 0; b < 2; b++) {
        const Channel st = b ? kStftB2 : kStftB1, lt = b ? kLtftB2 : kLtftB1;
        const bool sOk = s.fresh(st, now), lOk = s.fresh(lt, now);
        if (!trimNow || (!sOk && !lOk) || !integrate) continue;
        const float total = (sOk ? s.value(st) : 0) + (lOk ? s.value(lt) : 0);
        const float a = clampf(dt / 20.0f, 0, 1);  // ~20 s average
        _trimAvg[b] = _trimN[b] ? _trimAvg[b] + (total - _trimAvg[b]) * a : total;
        if (_trimN[b] < 60000) _trimN[b]++;
        if (speedOk && speed < 3) {
            _trimIdle[b] = _trimIdleN[b] ? _trimIdle[b] + (total - _trimIdle[b]) * a : total;
            if (_trimIdleN[b] < 60000) _trimIdleN[b]++;
        } else if (speedOk && speed > 30) {
            _trimCruise[b] = _trimCruiseN[b] ? _trimCruise[b] + (total - _trimCruise[b]) * a : total;
            if (_trimCruiseN[b] < 60000) _trimCruiseN[b]++;
        }
    }
    bool leakTold = false;
    for (int b = 0; b < 2; b++) {
        h.trimValid[b] = _trimN[b] >= 40;  // ~10 s of samples
        h.trim[b] = _trimAvg[b];
        if (!h.trimValid[b]) continue;
        const float t = _trimAvg[b];
        if (t > 12) add(b ? Finding::LeanB2 : Finding::LeanB1, t > 20 ? kSevWarn : kSevInfo, t);
        if (t < -12) add(b ? Finding::RichB2 : Finding::RichB1, t < -20 ? kSevWarn : kSevInfo, t);
        if (!leakTold && _trimIdleN[b] >= 40 && _trimCruiseN[b] >= 40 && _trimIdle[b] > 15 && _trimCruise[b] < 8) {
            add(Finding::LeanAtIdle, kSevInfo, _trimIdle[b]);
            leakTold = true;
        }
    }

    // ---- idle steadiness ----
    const bool idle = running && warm && rpm < 1500 && (!speedOk || speed < 2) &&
                      (!s.fresh(kThrottle, now) || s.value(kThrottle) < 5);
    if (idle && integrate) {
        const float a = clampf(dt / 10.0f, 0, 1);
        if (!_idleN) {
            _idleMean = rpm;
            _idleVar = 0;
        } else {
            const float diff = rpm - _idleMean;
            _idleMean += diff * a;
            _idleVar = (1 - a) * (_idleVar + a * diff * diff);
        }
        if (_idleN < 60000) _idleN++;
    }
    if (_idleN >= 40) {
        const float spread = sqrtf(_idleVar);
        if (spread > 100) add(Finding::RoughIdle, spread > 150 ? kSevWarn : kSevInfo, spread);
    }

    // ---- fuel level, intake ----
    if (s.fresh(kFuelLevel, now)) {
        const float f = s.value(kFuelLevel);
        if (held(Finding::LowFuel, f < 12, now, 10000)) add(Finding::LowFuel, f < 6 ? kSevCrit : kSevWarn, f);
    } else {
        held(Finding::LowFuel, false, now, 0);
    }
    if (held(Finding::HotIntake, s.fresh(kIntake, now) && s.value(kIntake) >= 65, now, 30000)) {
        add(Finding::HotIntake, kSevInfo, s.value(kIntake));
    }

    // ---- trouble codes ----
    if (d && d->available) {
        if (d->ready.valid && d->ready.mil) add(Finding::MilOn, kSevWarn, d->ready.count);
        if (d->stored.n) {
            uint8_t worst = 1;
            for (uint8_t i = 0; i < d->stored.n; i++) {
                const uint8_t sv = obd::describeDtc(d->stored.code[i]).severity;
                if (sv > worst) worst = sv;
            }
            add(Finding::StoredCodes, worst >= 3 ? kSevCrit : worst == 2 ? kSevWarn : kSevInfo, d->stored.n);
        }
        if (d->pending.n) add(Finding::PendingCodes, kSevInfo, d->pending.n);
    }

    // ---- the trip ----
    Trip& t = h.trip;
    uint8_t from = 0;
    t.lph = fuelFlow(s, now, _cfg, from);
    if (from) t.fuelFrom = from;
    if (integrate) {
        if (running) t.runMin += dt / 60.0f;
        if (running && speedOk && speed < 3) t.idleMin += dt / 60.0f;
        if (speedOk) {
            t.km += speed * dt / 3600.0f;
            if (speed > t.maxKmh) t.maxKmh = speed;
        }
        if (from) t.fuelL += t.lph * dt / 3600.0f;
        if (from && speedOk && speed > 5 && t.lph > 0.2f) {
            const float kml = speed / t.lph;
            t.instKmL = t.instKmL > 0 ? t.instKmL + (kml - t.instKmL) * clampf(dt / 3.0f, 0, 1) : kml;
        } else {
            t.instKmL = 0;
        }
    }

    // most serious first
    for (int i = 1; i < h.n; i++) {
        for (int j = i; j > 0 && h.issue[j].sev > h.issue[j - 1].sev; j--) {
            const Issue x = h.issue[j];
            h.issue[j] = h.issue[j - 1];
            h.issue[j - 1] = x;
        }
    }
}

}  // namespace vehicle
