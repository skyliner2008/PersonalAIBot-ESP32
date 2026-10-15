/**
 * @file TouchFT6336.h
 * @brief Minimal FT6336G capacitive touch reader on the shared I2C bus
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * Read through I2cBus (Arduino Wire + mutex) instead of LovyanGFX's own
 * I2C driver, so the touch and the ES8311 codec never fight over the bus.
 */

#pragma once

#include <Arduino.h>

#include "../system/I2cBus.h"

class TouchFT6336 {
public:
    static constexpr uint8_t kAddress = 0x38;

    bool begin(I2cBus* bus, int rstPin, int intPin) {
        _bus = bus;
        _intPin = intPin;
        if (rstPin >= 0) {
            pinMode(rstPin, OUTPUT);
            digitalWrite(rstPin, LOW);
            delay(10);
            digitalWrite(rstPin, HIGH);
            delay(300);  // controller boot time
        }
        if (intPin >= 0) pinMode(intPin, INPUT_PULLUP);
        _ok = _bus && _bus->probe(kAddress);
        Serial.printf("[Touch] FT6336G %s\n", _ok ? "found" : "NOT found");
        return _ok;
    }

    /** Poll the controller. Returns true while a finger is on the panel. */
    bool read(int16_t& x, int16_t& y) {
        if (!_ok) return false;
        uint8_t d[5];
        if (!_bus->readRegs(kAddress, 0x02, d, sizeof(d))) return false;
        const uint8_t points = d[0] & 0x0F;
        if (points == 0 || points > 2) return false;
        x = (int16_t)(((d[1] & 0x0F) << 8) | d[2]);
        y = (int16_t)(((d[3] & 0x0F) << 8) | d[4]);
        return true;
    }

    bool ok() const { return _ok; }

private:
    I2cBus* _bus = nullptr;
    int _intPin = -1;
    bool _ok = false;
};
