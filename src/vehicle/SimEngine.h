/**
 * @file SimEngine.h
 * @brief A small vehicle simulator for trying the dash without a car: engine torque, clutch, gearbox with
 *        automatic shifting, drag, brakes, warm-up and charging voltage (host tested, no Arduino)
 * @see wiki/16-Vehicle-Dash.md
 *
 * SimAuto drives a scripted 60 s loop (idle, city, a full-throttle pull through the shift light, braking,
 * cruising); SimTouch takes the throttle from the screen (setThrottle). Not a model of any real vehicle.
 */

#pragma once

#include <stdint.h>

#include "VehicleData.h"

namespace vehicle {

class SimEngine {
public:
    void reset(Kind kind);
    /** -1..1: positive = throttle, negative = brake. Ignored while autoDrive is on. */
    void setThrottle(float t) { _input = t < -1 ? -1 : (t > 1 ? 1 : t); }
    void setAutoDrive(bool on) { _auto = on; }
    void step(float dt);
    void write(Snapshot& s, uint32_t now) const;

    float rpm() const { return _rpm; }
    float speedKmh() const { return _v * 3.6f; }
    int gear() const { return _gear; }
    float coolant() const { return _coolant; }
    float battery() const { return _battery; }
    float throttle() const { return _throttle; }
    float time() const { return _t; }

private:
    struct Params {
        float mass, wheelR, idle, redline, torquePeak, torqueRpm, torqueSpan, cda, crr, brakeDecel, launchRise;
        int gears;
        float ratio[6], finalDrive;
        float coolTarget;  // where the temperature settles under load
    } _p{};
    Kind _kind = Kind::Car;
    float _t = 0, _v = 0, _rpm = 0, _throttle = 0, _brake = 0, _input = 0;
    float _coolant = 33, _battery = 12.5f, _shiftTimer = 0, _crank = 1.2f;
    int _gear = 1;
    bool _auto = true;

    float torque(float rpm) const;
    float scriptInput(float t) const;
};

}  // namespace vehicle
