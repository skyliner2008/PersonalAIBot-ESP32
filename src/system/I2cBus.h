/**
 * @file I2cBus.h
 * @brief Mutex-protected access to the shared I2C bus (SDA=16, SCL=15)
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * FT6336G touch (0x38) and ES8311 codec (0x18) share one bus and are used
 * from different FreeRTOS tasks. Everything goes through Arduino `Wire`
 * (a single driver owns the peripheral) and this mutex.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class I2cBus {
public:
    bool begin(int sda, int scl, uint32_t freq) {
        _mutex = xSemaphoreCreateMutex();
        return _mutex && Wire.begin(sda, scl, freq);
    }

    bool probe(uint8_t addr) {
        Lock lock(_mutex);
        if (!lock.ok) return false;
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    }

    bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
        Lock lock(_mutex);
        if (!lock.ok) return false;
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

    bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
        Lock lock(_mutex);
        if (!lock.ok) return false;
        Wire.beginTransmission(addr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
        return true;
    }

    bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) { return readRegs(addr, reg, &value, 1); }

private:
    SemaphoreHandle_t _mutex = nullptr;

    struct Lock {
        SemaphoreHandle_t m;
        bool ok;
        explicit Lock(SemaphoreHandle_t mutex)
            : m(mutex), ok(mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {}
        ~Lock() {
            if (ok) xSemaphoreGive(m);
        }
    };
};
