/**
 * @file SimEngine.h
 * @brief A small vehicle simulator for trying the dash without a car: engine torque, clutch, gearbox with
 *        automatic shifting, drag, brakes, warm-up and charging voltage (host tested, no Arduino)
 * @see wiki/16-Vehicle-Dash.md
 *
 * SimAuto drives a scripted 60 s loop (idle, city, a full-throttle pull through the shift light, braking,
 * cruising); SimTouch takes the throttle from the screen (setThrottle). Not a model of any real vehicle.
 * T112: also load, MAF, fuel trims (a little lean), timing, fuel level, and - the car - a diagnosis like an older
 * car's (P0420 stored + check-engine light, P0171 pending) that a scan reads and a clear removes, so the
 * trouble-code path can be tried by voice without a car.
 */

#pragma once

#include <stdint.h>

#include "Obd.h"
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
    /** The simulated diagnosis (car: codes + light; motorcycle: none, like the K-Line). */
    void diag(obd::Diag& d) const;
    void clearCodes() { _cleared = true; }
    /** Engine size in litres (for the fuel estimate). */
    float engineL() const { return _kind == Kind::Car ? 1.5f : 0.125f; }
    float fuelLevel() const { return _fuel; }

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
    float _fuel = 62;  // tank %
    int _gear = 1;
    bool _auto = true, _cleared = false;

    float torque(float rpm) const;
    float mapKpa() const { return 25.0f + 75.0f * _throttle; }
    float intakeC() const;
    float airGs() const;
    float scriptInput(float t) const;
};

}  // namespace vehicle
