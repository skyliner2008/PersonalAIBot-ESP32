/**
 * @file ES8311.cpp
 * @brief ES8311 codec initialisation and control
 */

#include "ES8311.h"

#include <Arduino.h>

namespace {
// Register map (ES8311 datasheet / Espressif es8311_reg.h)
constexpr uint8_t REG00_RESET = 0x00;
constexpr uint8_t REG01_CLK_MANAGER = 0x01;
constexpr uint8_t REG02_CLK_MANAGER = 0x02;
constexpr uint8_t REG03_CLK_MANAGER = 0x03;
constexpr uint8_t REG04_CLK_MANAGER = 0x04;
constexpr uint8_t REG05_CLK_MANAGER = 0x05;
constexpr uint8_t REG06_CLK_MANAGER = 0x06;
constexpr uint8_t REG07_CLK_MANAGER = 0x07;
constexpr uint8_t REG08_CLK_MANAGER = 0x08;
constexpr uint8_t REG09_SDPIN = 0x09;
constexpr uint8_t REG0A_SDPOUT = 0x0A;
constexpr uint8_t REG0D_SYSTEM = 0x0D;
constexpr uint8_t REG0E_SYSTEM = 0x0E;
constexpr uint8_t REG12_SYSTEM = 0x12;
constexpr uint8_t REG13_SYSTEM = 0x13;
constexpr uint8_t REG14_SYSTEM = 0x14;
constexpr uint8_t REG16_ADC = 0x16;
constexpr uint8_t REG17_ADC = 0x17;
constexpr uint8_t REG1C_ADC = 0x1C;
constexpr uint8_t REG31_DAC = 0x31;
constexpr uint8_t REG32_DAC = 0x32;
constexpr uint8_t REG37_DAC = 0x37;
constexpr uint8_t REGFD_CHIP_ID1 = 0xFD;

// Clock coefficients for MCLK = 256 * fs (identical for 8..48 kHz)
constexpr uint8_t kPreDiv = 1, kPreMulti = 0, kAdcDiv = 1, kDacDiv = 1, kFsMode = 0;
constexpr uint8_t kLrckH = 0x00, kLrckL = 0xFF, kBclkDiv = 4, kAdcOsr = 0x10, kDacOsr = 0x10;
}  // namespace

bool ES8311::write(uint8_t reg, uint8_t value) { return _bus && _bus->writeReg(kAddress, reg, value); }

uint8_t ES8311::read(uint8_t reg) {
    uint8_t v = 0;
    if (_bus) _bus->readReg(kAddress, reg, v);
    return v;
}

bool ES8311::begin(I2cBus* bus, uint32_t sampleRate) {
    _bus = bus;
    _ready = false;
    if (!_bus || !_bus->probe(kAddress)) {
        Serial.printf("[ES8311] not found at 0x%02X\n", kAddress);
        return false;
    }
    Serial.printf("[ES8311] chip id 0x%02X, configuring for %lu Hz\n", read(REGFD_CHIP_ID1),
                  (unsigned long)sampleRate);

    bool ok = true;
    // Reset, then power on the state machine (CSM_ON). Leaving REG00 at 0 keeps the codec off.
    ok &= write(REG00_RESET, 0x1F);
    delay(20);
    ok &= write(REG00_RESET, 0x00);
    ok &= write(REG00_RESET, 0x80);

    // Clocks: MCLK from the MCLK pin, all internal clocks enabled
    ok &= write(REG01_CLK_MANAGER, 0x3F);
    uint8_t r = read(REG02_CLK_MANAGER) & 0x07;
    r |= (uint8_t)((kPreDiv - 1) << 5) | (uint8_t)(kPreMulti << 3);
    ok &= write(REG02_CLK_MANAGER, r);
    ok &= write(REG03_CLK_MANAGER, (uint8_t)((kFsMode << 6) | kAdcOsr));
    ok &= write(REG04_CLK_MANAGER, kDacOsr);
    ok &= write(REG05_CLK_MANAGER, (uint8_t)(((kAdcDiv - 1) << 4) | (kDacDiv - 1)));
    r = read(REG06_CLK_MANAGER) & 0xE0;  // keep SCLK polarity (normal)
    r &= (uint8_t)~0x20;
    r |= (uint8_t)(kBclkDiv - 1);
    ok &= write(REG06_CLK_MANAGER, r);
    ok &= write(REG07_CLK_MANAGER, (uint8_t)((read(REG07_CLK_MANAGER) & 0xC0) | kLrckH));
    ok &= write(REG08_CLK_MANAGER, kLrckL);

    // Serial port: codec is I2S slave, 16-bit, standard I2S, both directions
    ok &= write(REG00_RESET, (uint8_t)(read(REG00_RESET) & 0xBF));
    ok &= write(REG09_SDPIN, 0x0C);
    ok &= write(REG0A_SDPOUT, 0x0C);

    // Power up analog, ADC modulator, DAC and output driver
    ok &= write(REG0D_SYSTEM, 0x01);
    ok &= write(REG0E_SYSTEM, 0x02);
    ok &= write(REG12_SYSTEM, 0x00);
    ok &= write(REG13_SYSTEM, 0x10);
    ok &= write(REG1C_ADC, 0x6A);  // ADC equalizer bypass, DC offset cancel
    ok &= write(REG37_DAC, 0x08);  // bypass DAC equalizer

    // Analog microphone on MIC1P/MIC1N with PGA, ADC volume ~+4.5 dB
    ok &= write(REG17_ADC, 0xC8);
    ok &= write(REG14_SYSTEM, 0x1A);

    _ready = ok;
    if (!ok) Serial.println("[ES8311] I2C write failed during init");
    return ok;
}

bool ES8311::setVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    const uint8_t reg = volume == 0 ? 0 : (uint8_t)((volume * 256) / 100 - 1);
    return write(REG32_DAC, reg);
}

bool ES8311::setMicGain(uint8_t step) {
    if (step > 7) step = 7;
    return write(REG16_ADC, step);
}

bool ES8311::setMute(bool mute) {
    uint8_t r = read(REG31_DAC) & 0x9F;
    if (mute) r |= 0x60;
    return write(REG31_DAC, r);
}
