/**
 * @file SimEngine.cpp
 * @brief Vehicle simulator (see SimEngine.h)
 */

#include "SimEngine.h"

#include <math.h>

namespace vehicle {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kAmbient = 32.0f;  // a Thai afternoon
constexpr float kAir = 1.18f;      // kg/m3

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void SimEngine::reset(Kind kind) {
    _kind = kind;
    if (kind == Kind::Motorcycle) {  // a 110-125 cc commuter with a 4-speed semi-automatic box
        _p = {170, 0.267f, 1500, 9000, 10.0f, 5500, 4500, 0.60f, 0.015f, 6.5f, 3500, 4,
              {2.83f, 1.71f, 1.24f, 0.96f, 0, 0}, 8.7f, 104};
    } else {  // a 1.5 l petrol hatchback with a 5-speed box
        _p = {1250, 0.312f, 800, 6500, 140, 4200, 4200, 0.65f, 0.012f, 8.0f, 2200, 5,
              {3.58f, 2.02f, 1.35f, 1.03f, 0.84f, 0}, 4.06f, 90};
    }
    _t = _v = _throttle = _brake = _input = _shiftTimer = 0;
    _rpm = 0;
    _gear = 1;
    _coolant = 33;
    _battery = 12.5f;
    _crank = 1.2f;  // starter first: the voltage dips, then it fires
}

float SimEngine::torque(float rpm) const {
    if (rpm >= _p.redline + 150) return 0;  // rev limiter
    const float x = (rpm - _p.torqueRpm) / _p.torqueSpan;
    return _p.torquePeak * clampf(1.0f - 0.55f * x * x, 0.25f, 1.0f);
}

// scripted 60 s drive for SimAuto: -1..1 like setThrottle
float SimEngine::scriptInput(float t) const {
    const float s = fmodf(t, 60.0f);
    if (s < 4) return 0;        // idle
    if (s < 12) return 0.35f;   // city
    if (s < 20) return 1.0f;    // full pull through the shift light
    if (s < 25) return 0;       // lift off
    if (s < 30) return -0.5f;   // brake
    if (s < 42) return 0.28f;   // cruise
    if (s < 49) return 0.9f;    // overtake
    if (s < 52) return 0;
    return -0.8f;               // brake to a stop
}

void SimEngine::step(float dt) {
    if (dt <= 0) return;
    if (dt > 0.1f) dt = 0.1f;
    _t += dt;
    const float in = _auto ? scriptInput(_t) : _input;
    const float wantThrottle = in > 0 ? in : 0;
    _brake = in < 0 ? -in : 0;
    _throttle += (wantThrottle - _throttle) * clampf(dt / 0.08f, 0, 1);  // pedal / twist grip response

    if (_crank > 0) {  // starting: cranking speed, battery under the starter's load
        _crank -= dt;
        _rpm = 250;
        _battery = 10.7f;
        return;
    }

    // ---- driveline ----
    const float overall = _p.ratio[_gear - 1] * _p.finalDrive;
    const float lockedRpm = _v / _p.wheelR / (2 * kPi) * 60.0f * overall;
    const float engage = _p.idle * 1.25f;
    float driveTorque;  // at the engine
    if (_shiftTimer > 0) {  // clutch in while shifting: no drive, the engine falls toward the next gear
        _shiftTimer -= dt;
        driveTorque = 0;
        _rpm += (fmaxf(lockedRpm, _p.idle) - _rpm) * clampf(dt / 0.12f, 0, 1);
    } else if (lockedRpm >= engage) {  // clutch closed: engine and wheels turn together
        _rpm = lockedRpm;
        const float engineBrake = _p.torquePeak * 0.12f * (_rpm / _p.redline);
        driveTorque = torque(_rpm) * _throttle - engineBrake * (1.0f - _throttle);
    } else {  // pulling away (or rolling slowly): the clutch slips at the launch rpm
        const float target = _p.idle + _throttle * _p.launchRise;
        _rpm += (target - _rpm) * clampf(dt / 0.25f, 0, 1);
        driveTorque = _throttle > 0.02f ? torque(_rpm) * _throttle * 0.9f : 0;
    }
    if (_rpm < _p.idle && lockedRpm < engage) _rpm = _p.idle;

    const float drive = driveTorque * overall * 0.9f / _p.wheelR;  // N at the tyre
    const float aero = 0.5f * kAir * _p.cda * _v * _v;
    const float rolling = _v > 0.05f ? _p.crr * _p.mass * 9.81f : 0;
    const float brake = _v > 0.01f ? _brake * _p.brakeDecel * _p.mass : 0;
    float a = (drive - aero - rolling - brake) / _p.mass;
    _v += a * dt;
    if (_v < 0) _v = 0;

    // ---- automatic shifting ----
    if (_shiftTimer <= 0) {
        const float up = _throttle > 0.6f ? _p.redline * 0.93f : _p.idle + (_p.redline - _p.idle) * 0.38f;
        const float down = _p.idle * 1.35f;
        if (_rpm >= up && _gear < _p.gears) {
            _gear++;
            _shiftTimer = 0.25f;
        } else if (_gear > 1 && lockedRpm < down && _v > 1.0f) {
            _gear--;
            _shiftTimer = 0.2f;
        } else if (_gear > 1 && _throttle > 0.9f && _rpm < _p.redline * 0.45f) {  // kick-down
            const float lower = _v / _p.wheelR / (2 * kPi) * 60.0f * _p.ratio[_gear - 2] * _p.finalDrive;
            if (lower < _p.redline * 0.85f) {
                _gear--;
                _shiftTimer = 0.2f;
            }
        }
        if (_v < 0.5f) _gear = 1;
    }
    if (_rpm > _p.redline + 200) _rpm = _p.redline + 200;

    // ---- warm-up and charging ----
    const float load = 0.25f + 0.75f * _throttle * (_rpm / _p.redline);
    const float heat = (_p.coolTarget + 12.0f * load - _coolant) * 0.02f;   // toward a load-dependent level
    const float airflow = 1.0f + _v / 15.0f;
    const float thermostat = _coolant > _p.coolTarget ? 0.03f * airflow : 0.0f;
    _coolant += dt * (heat - thermostat * (_coolant - _p.coolTarget));
    if (_coolant < kAmbient - 5) _coolant = kAmbient - 5;
    const float charging = _rpm > _p.idle * 0.8f ? 14.2f - 0.25f * _throttle : 12.4f;
    _battery += (charging - _battery) * clampf(dt / 1.5f, 0, 1);
}

void SimEngine::write(Snapshot& s, uint32_t now) const {
    s.set(kRpm, _rpm, now);
    s.set(kSpeed, _v * 3.6f, now);
    s.set(kCoolant, _coolant, now);
    s.set(kIntake, kAmbient + 10.0f * (1.0f - clampf(_v / 15.0f, 0, 1)), now);  // heat soak when standing
    s.set(kBattery, _battery, now);
    s.set(kThrottle, _throttle * 100.0f, now);
    s.set(kMap, 25.0f + 75.0f * _throttle, now);
}

}  // namespace vehicle
