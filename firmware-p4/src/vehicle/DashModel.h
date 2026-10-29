/**
 * @file DashModel.h
 * @brief What the dash shows, derived from the readings: smoothed rpm, peak, shift light, warning levels,
 *        estimated gear (host tested, no Arduino)
 * @see wiki/16-Vehicle-Dash.md
 */

#pragma once

#include <stdint.h>

#include "VehicleData.h"

namespace vehicle {

struct DashConfig {
    Kind kind = Kind::Car;
    int rpmMax = 7000;       // full scale of the rpm bar
    int shiftRpm = 6000;     // shift light from here
    float coolantCold = 60;  // below = engine not warm yet (blue)
    float coolantWarn = 105;
    float coolantCrit = 112;
    float batteryLow = 11.8f;   // engine running and below = charging fault / weak battery
    float batteryHigh = 15.2f;  // regulator fault
    // gear estimate from rpm / speed (gears = 0: off - most motorcycle ECUs know no speed, CVT scooters have no gears)
    int gears = 5;
    float ratio[6] = {3.58f, 2.02f, 1.35f, 1.03f, 0.84f, 0.0f};
    float finalDrive = 4.06f;
    float tireCircM = 1.96f;  // rolling circumference, metres (195/65 R15)
};

/** Sensible defaults: a petrol car, or a Thai 110-125 cc motorcycle (Wave / Click class). */
DashConfig defaultConfig(Kind kind);

enum class Level : uint8_t { None, Normal, Cold, Warn, Crit };

struct DashView {
    bool rpmValid = false, speedValid = false;
    float rpm = 0;      // smoothed for the bar and the number
    float speed = 0;
    int gear = -1;      // -1 = unknown / hidden, 0 = N
    bool gearEstimated = false;
    float peakRpm = 0;
    bool shift = false;     // at or past the shift point
    bool shiftOn = false;   // the flashing phase of the shift light
    float coolant = 0, intake = 0, battery = 0, throttle = 0;
    Level coolantLevel = Level::None, intakeLevel = Level::None, batteryLevel = Level::None, throttleLevel = Level::None;
    bool live = false;      // any channel fresh
};

class DashModel {
public:
    void setConfig(const DashConfig& c) { _cfg = c; }
    const DashConfig& config() const { return _cfg; }
    void resetPeak() { _peak = 0; }

    /** Call every frame with the latest readings. */
    const DashView& update(const Snapshot& s, uint32_t now);
    const DashView& view() const { return _view; }

    /** Gear (1..gears) that fits rpm / speed, 0 = neutral or clutch in, -1 = unknown. */
    static int estimateGear(const DashConfig& c, float rpm, float speedKmh);

private:
    DashConfig _cfg;
    DashView _view;
    float _peak = 0;
    float _smoothRpm = 0;
    uint32_t _last = 0;
    int _lastGear = -1;
    uint32_t _gearSince = 0;
};

}  // namespace vehicle
