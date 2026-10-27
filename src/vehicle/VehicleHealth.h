/**
 * @file VehicleHealth.h
 * @brief "How is the car": trends and findings from the live readings of any source, plus the trip
 *        (distance, time, fuel used, km per litre) - host tested, no Arduino (T112)
 * @see wiki/16-Vehicle-Dash.md
 *
 * The readings are linked together rather than read one by one:
 *  - fuel flow: PID 5E, else MAF / 14.7 / 745 g/l, else MAP + intake + rpm + engine size (speed-density, the
 *    K-Line bikes too) -> litres used, km per litre now and for the trip (not for a diesel without 5E)
 *  - engine temperature: level and its rise per minute (rising fast near the warning = trouble before the red)
 *  - battery: charging while running (held 30 s, smart alternators sit near 12.6-13 V: only < 12.3 is a fault),
 *    voltage parked (+ rough state of charge), the dip while cranking (a weak battery shows there first)
 *  - fuel trims short + long per bank, averaged while warm: lean / rich, lean at idle only = an air leak
 *  - idle steadiness, fuel level, intake heat; the trouble codes of an OBD-II source
 * A finding must hold a while before it counts: one odd reading says nothing.
 */

#pragma once

#include <stdint.h>

#include "Obd.h"
#include "VehicleData.h"

namespace vehicle {

enum class Finding : uint8_t {
    MilOn,
    StoredCodes,
    PendingCodes,
    Overheat,
    OverheatRising,
    ColdEngine,
    NotCharging,
    Overcharge,
    LowBatteryParked,
    WeakStart,
    LeanB1,
    RichB1,
    LeanB2,
    RichB2,
    LeanAtIdle,
    RoughIdle,
    LowFuel,
    HotIntake,
    Count
};

enum Severity : uint8_t { kSevOk, kSevInfo, kSevWarn, kSevCrit };

/** English (the AI's tool result). */
const char* findingKey(Finding f);
/** Thai, short (the screen, spoken alerts). */
const char* findingTh(Finding f);
/** Minutes between two spoken alerts of it (0 = never said out loud, only shown / told when asked). */
uint16_t findingSpeakMin(Finding f);
const char* severityKey(uint8_t s);

struct Issue {
    Finding id;
    uint8_t sev;
    float value;  // the number behind it (deg C, V, %, rpm spread, code count)
};

struct Trip {
    float km = 0, runMin = 0, idleMin = 0, maxKmh = 0;
    float fuelL = 0;
    uint8_t fuelFrom = 0;  // 0 = no fuel data, 1 = PID 5E, 2 = MAF, 3 = estimate from MAP
    float lph = 0;         // fuel flow now
    float instKmL = 0;     // km per litre now (0 = standing / unknown)
    float avgKmL() const { return fuelL > 0.05f && km > 0.2f ? km / fuelL : 0; }
    float avgKmh() const { return runMin > 0.5f ? km / (runMin / 60.0f) : 0; }
};

struct Health {
    uint8_t n = 0;
    Issue issue[8];
    uint8_t worst = kSevOk;
    Trip trip;
    bool running = false;
    bool coolantRateValid = false;
    float coolantRate = 0;  // deg C per minute over the last minute
    bool restValid = false;
    float restV = 0;        // battery with the engine off (a minute after stopping)
    uint8_t restPct = 0;    // rough state of charge from restV
    float startMinV = 0;    // the lowest voltage while cranking the last start (0 = not seen)
    bool trimValid[2] = {false, false};
    float trim[2] = {0, 0};  // total fuel trim (short + long) per bank, averaged
};

struct HealthConfig {
    Kind kind = Kind::Car;
    float engineL = 1.5f;  // displacement: fuel from MAP when the car has no MAF / fuel-rate PID
    bool diesel = false;   // no fuel from air flow (a diesel runs lean by varying amounts)
    float coolantWarn = 105, coolantCrit = 112;
};

class HealthMonitor {
public:
    void reset(const HealthConfig& c);
    void setConfig(const HealthConfig& c) { _cfg = c; }
    const HealthConfig& config() const { return _cfg; }
    /** Latest readings, a few times a second (more often is ignored). diag: nullptr if the source has none. */
    void update(const Snapshot& s, const obd::Diag* diag, uint32_t now);
    const Health& health() const { return _h; }
    /** Start a new trip (distance, fuel...). */
    void resetTrip() { _h.trip = Trip(); }

    /** Fuel flow in l/h from the readings and what it came from (0 = none). */
    static float fuelFlow(const Snapshot& s, uint32_t now, const HealthConfig& c, uint8_t& from);
    /** Rough state of charge (%) of a lead-acid battery at rest. */
    static uint8_t restPercent(float volts);

private:
    HealthConfig _cfg;
    Health _h;
    uint32_t _last = 0;
    uint32_t _since[(int)Finding::Count] = {};  // the condition has held since (0 = not now)
    uint32_t _runSince = 0, _offSince = 0;
    float _offMin = 99;
    uint32_t _offMinAt = 0;
    float _maxCoolant = -100;
    float _coolRing[7] = {};
    uint32_t _coolRingAt[7] = {};
    uint8_t _coolN = 0, _coolPos = 0;
    uint32_t _coolNext = 0;
    float _trimAvg[2] = {0, 0}, _trimIdle[2] = {0, 0}, _trimCruise[2] = {0, 0};
    uint16_t _trimN[2] = {0, 0}, _trimIdleN[2] = {0, 0}, _trimCruiseN[2] = {0, 0};
    float _idleMean = 0, _idleVar = 0;
    uint16_t _idleN = 0;

    bool held(Finding f, bool cond, uint32_t now, uint32_t holdMs);
    void add(Finding f, uint8_t sev, float value);
};

}  // namespace vehicle
