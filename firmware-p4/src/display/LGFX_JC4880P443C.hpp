/**
 * @file LGFX_JC4880P443C.hpp
 * @brief LovyanGFX MIPI-DSI display configuration for the Guition JC4880P443C_I_W board (ESP32-P4)
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * Display: ST7701S 480x800 4.3" IPS (2-Lane MIPI-DSI, RGB565 DPI @ 34 MHz)
 * Backlight: MP3202 PWM on GPIO 23 (Active HIGH)
 *
 * The GT911 touch controller shares the I2C bus (SDA=7, SCL=8) with the ES8311
 * codec and is read through I2cBus/Wire (see TouchGT911.h).
 */

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins_config.h"

#if SOC_MIPI_DSI_SUPPORTED
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

class Panel_ST7701S_JC4880P443C : public lgfx::Panel_DSI {
protected:
    size_t getResetDelayBeforeDpi(void) const override { return 120; }

    const uint8_t* getInitParams(size_t listno) const override {
        // Vendor ST7701S 480x800 MIPI-DSI init sequence from Guition JC4880P443C BSP
        // Format per entry: len(cmd + params), cmd, params..., terminated by 0
        static constexpr uint8_t list0[] = {
            6,  0xFF, 0x77, 0x01, 0x00, 0x00, 0x13,
            2,  0xEF, 0x08,
            6,  0xFF, 0x77, 0x01, 0x00, 0x00, 0x10,
            3,  0xC0, 0x63, 0x00,
            3,  0xC1, 0x0D, 0x02,
            3,  0xC2, 0x10, 0x08,
            2,  0xCC, 0x10,
            17, 0xB0, 0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09,
                      0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71,
            17, 0xB1, 0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08,
                      0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D,
            6,  0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
            2,  0xB0, 0x5D,
            2,  0xB1, 0x58,
            2,  0xB2, 0x87,
            2,  0xB3, 0x80,
            2,  0xB5, 0x4E,
            2,  0xB7, 0x85,
            2,  0xB8, 0x21,
            3,  0xB9, 0x10, 0x1F,
            2,  0xBB, 0x03,
            2,  0xBC, 0x00,
            2,  0xC1, 0x78,
            2,  0xC2, 0x78,
            2,  0xD0, 0x88,
            4,  0xE0, 0x00, 0x3A, 0x02,
            12, 0xE1, 0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40,
            14, 0xE2, 0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00,
            5,  0xE3, 0x00, 0x00, 0x33, 0x33,
            3,  0xE4, 0x44, 0x44,
            17, 0xE5, 0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0,
                      0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0,
            5,  0xE6, 0x00, 0x00, 0x33, 0x33,
            3,  0xE7, 0x44, 0x44,
            17, 0xE8, 0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0,
                      0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0,
            8,  0xEB, 0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00,
            3,  0xEC, 0x08, 0x01,
            17, 0xED, 0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF,
                      0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B,
            7,  0xEF, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
            6,  0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
            0,
        };
        static constexpr uint8_t list1[] = {
            1, CMD_SLPOUT,
            0,
        };
        static constexpr uint8_t list2[] = {
            1, CMD_DISPON,
            0,
        };
        switch (listno) {
            case 0: return list0;
            case 1: return list1;
            case 2: return list2;
            default: return nullptr;
        }
    }

    size_t getInitDelay(size_t listno) const override {
        switch (listno) {
            case 1: return 120;  // 120 ms after SLPOUT
            case 2: return 20;   // 20 ms after DISPON
            default: return 0;
        }
    }
};

class LGFX_JC4880P443C : public lgfx::LGFX_Device {
    lgfx::Bus_DSI _bus_instance;
    Panel_ST7701S_JC4880P443C _panel_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_JC4880P443C(void) {
        // ============================================
        // MIPI-DSI Bus Configuration
        // ============================================
        {
            auto cfg = _bus_instance.config();
            cfg.bus_id         = 0;
            cfg.lane_num       = MIPI_DSI_LANE_NUM;
            cfg.lane_mbps      = MIPI_DSI_LANE_MBPS;
            cfg.ldo_chan_id    = MIPI_DSI_PHY_LDO_CHAN;
            cfg.ldo_voltage_mv = MIPI_DSI_PHY_LDO_MV;
            cfg.lcd_cmd_bits   = 8;
            cfg.lcd_param_bits = 8;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ============================================
        // ST7701S MIPI-DSI Panel & DPI Timing Configuration
        // ============================================
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs          = -1;
            cfg.pin_rst         = LCD_RST;       // GPIO 5
            cfg.pin_busy        = -1;
            cfg.panel_width     = LCD_WIDTH;     // 480
            cfg.panel_height    = LCD_HEIGHT;    // 800
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = false;
            cfg.invert          = false;
            cfg.rgb_order       = true;          // RGB element order
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel_instance.config(cfg);

            auto det = _panel_instance.config_detail();
            det.dpi_freq_mhz      = MIPI_DSI_DPI_FREQ_MHZ;  // 34 MHz (60 Hz)
            det.hsync_pulse_width = 12;
            det.hsync_back_porch  = 42;
            det.hsync_front_porch = 42;
            det.vsync_pulse_width = 2;
            det.vsync_back_porch  = 8;
            det.vsync_front_porch = 166;
            _panel_instance.config_detail(det);
        }

        // ============================================
        // Backlight (PWM) Configuration — GPIO 23 Active HIGH
        // ============================================
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = LCD_BL;            // GPIO 23
            cfg.invert      = false;             // Active HIGH
            cfg.freq        = 5000;              // 5 kHz PWM
            cfg.pwm_channel = 0;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};
#endif  // SOC_MIPI_DSI_SUPPORTED
