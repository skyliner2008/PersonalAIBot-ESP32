/**
 * @file ES8311.h
 * @brief ES8311 mono audio codec driver (I2C control, slave I2S)
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * Register sequence follows Espressif's reference es8311 component
 * (esp-idf / esp-bsp): reset -> power on -> clocks (MCLK from pin, 256*fs)
 * -> 16-bit I2S slave -> analog/ADC/DAC power-up -> analog mic + PGA.
 */

#pragma once

#include <stdint.h>

#include "../system/I2cBus.h"

class ES8311 {
public:
    static constexpr uint8_t kAddress = 0x18;

    /** Initialise for MCLK = 256 * sampleRate, 16-bit I2S, codec as I2S slave. */
    bool begin(I2cBus* bus, uint32_t sampleRate);

    /** Speaker volume 0-100 (100 = +32 dB digital gain, ~75 = 0 dB). */
    bool setVolume(int volume);

    /** Mic ADC gain step 0-7 (0, 6, ... 42 dB). */
    bool setMicGain(uint8_t step);

    /** Mute/unmute the DAC output. */
    bool setMute(bool mute);

    bool ready() const { return _ready; }

    /** Raw register access (diagnostics console). */
    bool write(uint8_t reg, uint8_t value);
    uint8_t read(uint8_t reg);

private:
    I2cBus* _bus = nullptr;
    bool _ready = false;
};
