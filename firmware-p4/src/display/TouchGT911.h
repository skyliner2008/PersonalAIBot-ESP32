/**
 * @file TouchGT911.h
 * @brief Minimal GT911 capacitive touch reader on the shared I2C bus (Guition JC4880P443C)
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * Read through I2cBus (Arduino Wire + mutex) so the GT911 touch controller and
 * the ES8311 audio codec share the I2C bus safely across FreeRTOS tasks.
 */

#pragma once

#include <Arduino.h>

#include "../system/I2cBus.h"
#include "pins_config.h"

class TouchGT911 {
public:
    static constexpr uint8_t kAddrPrimary = 0x5D;
    static constexpr uint8_t kAddrAlt     = 0x14;
    static constexpr uint16_t kRegStatus  = 0x814E;
    static constexpr uint16_t kRegPoint1  = 0x8150;

    bool begin(I2cBus* bus, int rstPin, int intPin) {
        _bus = bus;
        _intPin = intPin;
        if (rstPin >= 0) {
            if (intPin >= 0) {
                pinMode(intPin, OUTPUT);
                digitalWrite(intPin, LOW);
            }
            pinMode(rstPin, OUTPUT);
            digitalWrite(rstPin, LOW);
            delay(10);
            digitalWrite(rstPin, HIGH);
            delay(10);
            if (intPin >= 0) pinMode(intPin, INPUT);
            delay(100);  // GT911 boot time
        } else if (intPin >= 0) {
            pinMode(intPin, INPUT);
        }
        if (_bus) {
            if (_bus->probe(kAddrPrimary)) {
                _addr = kAddrPrimary;
                _ok = true;
            } else if (_bus->probe(kAddrAlt)) {
                _addr = kAddrAlt;
                _ok = true;
            }
        }
        Serial.printf("[Touch] GT911 %s (0x%02X)\n", _ok ? "found" : "NOT found", _addr);
        return _ok;
    }

    /** Poll the controller. Returns true while a finger is on the panel. */
    bool read(int16_t& x, int16_t& y) {
        if (!_ok) return false;
        uint8_t status = 0;
        if (!_bus->readRegs16(_addr, kRegStatus, &status, 1)) return false;
        if ((status & 0x80) == 0) {
            // Buffer not updated yet: return the last held state within 30 ms (GT911 updates at ~100 Hz)
            if (_holding && (uint32_t)(millis() - _lastSampleMs) < 30) {
                x = _lastX;
                y = _lastY;
                return true;
            }
            _holding = false;
            return false;
        }
        const uint8_t points = status & 0x0F;
        uint8_t coord[4] = {};
        bool ok = false;
        if (points >= 1 && points <= 5) {
            ok = _bus->readRegs16(_addr, kRegPoint1, coord, sizeof(coord));
        }
        // Clear buffer-ready flag so GT911 produces the next sample
        _bus->writeReg16(_addr, kRegStatus, 0x00);
        if (!ok) {
            _holding = false;
            return false;
        }
        // GT911: the panel's own 480x800 (portrait). The UI is 320x240 drawn at 2x in the middle of the 800-px side
        // (UiManager::frame): back to a 240x320 portrait like the S3 board's FT6336, so the axis settings and the
        // 3-point calibration work the same - a touch in a side band clamps to the UI's edge
        const int rawX = coord[0] | (coord[1] << 8), rawY = coord[2] | (coord[3] << 8);
        _lastX = x = (int16_t)constrain(rawX / 2, 0, 239);
        _lastY = y = (int16_t)constrain((rawY - (LCD_HEIGHT - 640) / 2) / 2, 0, 319);
        _lastSampleMs = millis();
        _holding = true;
        return true;
    }

    bool ok() const { return _ok; }

private:
    I2cBus* _bus = nullptr;
    int _intPin = -1;
    uint8_t _addr = kAddrPrimary;
    bool _ok = false;
    bool _holding = false;
    int16_t _lastX = 0, _lastY = 0;
    uint32_t _lastSampleMs = 0;
};
