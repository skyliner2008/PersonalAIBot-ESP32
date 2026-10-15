/**
 * @file LGFX_ES3C28P.hpp
 * @brief LovyanGFX display configuration for the ES3C28P board
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * Display: ILI9341V 240x320 2.8" IPS (4-Wire SPI)
 *
 * The FT6336G touch controller is NOT attached here on purpose: it shares
 * the I2C bus with the ES8311 codec, so it is read through I2cBus/Wire
 * (see TouchFT6336.h) to keep a single I2C driver and one mutex.
 */

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins_config.h"

class LGFX_ES3C28P : public lgfx::LGFX_Device {
    // SPI bus instance for LCD
    lgfx::Bus_SPI _bus_instance;

    // LCD panel (ILI9341)
    lgfx::Panel_ILI9341 _panel_instance;

    // PWM backlight control
    lgfx::Light_PWM _light_instance;

public:
    LGFX_ES3C28P(void) {
        // ============================================
        // SPI Bus Configuration
        // ============================================
        {
            auto cfg = _bus_instance.config();

            cfg.spi_host    = LCD_SPI_HOST;     // SPI2_HOST
            cfg.spi_mode    = 0;
            cfg.freq_write  = LCD_SPI_FREQ;     // 40 MHz
            cfg.freq_read   = LCD_SPI_READ_FREQ;// 16 MHz
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            cfg.pin_sclk    = LCD_SCK;          // GPIO 12
            cfg.pin_mosi    = LCD_MOSI;         // GPIO 11
            cfg.pin_miso    = LCD_MISO;         // GPIO 13
            cfg.pin_dc      = LCD_DC;           // GPIO 46

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ============================================
        // LCD Panel Configuration
        // ============================================
        {
            auto cfg = _panel_instance.config();

            cfg.pin_cs      = LCD_CS;           // GPIO 10
            cfg.pin_rst     = LCD_RST;          // -1 (CHIP_PU)
            cfg.pin_busy    = -1;

            cfg.panel_width     = LCD_WIDTH;     // 240
            cfg.panel_height    = LCD_HEIGHT;    // 320
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable        = true;
            cfg.invert          = false;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;

            _panel_instance.config(cfg);
        }

        // ============================================
        // Backlight (PWM) Configuration
        // IMPORTANT: GPIO 45 is Active HIGH, default OFF!
        // ============================================
        {
            auto cfg = _light_instance.config();

            cfg.pin_bl      = LCD_BL;           // GPIO 45
            cfg.invert      = false;            // Active HIGH
            cfg.freq        = 5000;             // 5 kHz PWM
            cfg.pwm_channel = 0;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }


        setPanel(&_panel_instance);
    }
};
