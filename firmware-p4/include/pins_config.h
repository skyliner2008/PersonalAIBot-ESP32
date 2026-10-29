/**
 * @file pins_config.h
 * @brief GPIO pins of the Guition JC4880P443C_I_W (ESP32-P4 + ESP32-C6, 4.3" 480x800 ST7701S MIPI-DSI, GT911,
 *        ES8311 + NS4150B, OV02C10 MIPI-CSI camera, RS485)
 * @see ../wiki/19-ESP32-P4-JC4880P443C.md
 *
 * firmware-p4/ is its own firmware (the ES3C28P / ESP32-S3 one is ../src): pins here are for this board only.
 * ESP32-C6 (Wi-Fi 6 / BLE 5 through ESP-Hosted): SDIO CLK 18, CMD 19, D0..D3 14..17, reset 54 (Arduino variant
 * esp32p4) - never use those pins for anything else.
 */

#pragma once

#ifndef BOARD_JC4880P443C
#define BOARD_JC4880P443C 1
#endif
#define BOARD_NAME "JC4880P443C (ESP32-P4)"

// ============================================================
// LCD Display — ST7701S (480x800, 2-Lane MIPI-DSI)
// ============================================================
#define LCD_RST                 5       // GPIO 5 (active LOW hardware reset)
#define LCD_BL                  23      // Backlight PWM (MP3202) — Active HIGH
#define LCD_WIDTH               480
#define LCD_HEIGHT              800
#define MIPI_DSI_LANE_NUM       2
#define MIPI_DSI_LANE_MBPS      500
#define MIPI_DSI_DPI_FREQ_MHZ   34
#define MIPI_DSI_PHY_LDO_CHAN   3       // VDD_MIPI_DPHY internal LDO channel 3
#define MIPI_DSI_PHY_LDO_MV     2500    // 2.5 V

// ============================================================
// Capacitive Touch — GT911 (I2C)
// ============================================================
#define TOUCH_I2C_ADDR          0x5D
#define TOUCH_I2C_ADDR_ALT      0x14
#define TOUCH_SDA               7       // Shared I2C bus with ES8311
#define TOUCH_SCL               8       // Shared I2C bus with ES8311
#define TOUCH_INT               21      // Interrupt
#define TOUCH_RST               22      // Hardware reset
#define TOUCH_I2C_FREQ          400000  // 400 kHz Fast Mode

// ============================================================
// Audio Codec — ES8311 (I2C + I2S) + NS4150B Amplifier
// ============================================================
#define ES8311_I2C_ADDR         0x18
#define ES8311_SDA              7       // Shared I2C bus with GT911
#define ES8311_SCL              8       // Shared I2C bus with GT911

#define I2S_MCLK                13      // Master Clock
#define I2S_BCLK                12      // Bit Clock (SCK)
#define I2S_LRCK                10      // Word Select (WS)
#define I2S_DOUT                9       // MCU data out -> ES8311 DSDIN (playback)
#define I2S_DIN                 48      // ES8311 ASDOUT -> MCU data in (microphone)

// Speaker Amplifier — NS4150B (CTRL pin has 10K pull-down R19; Active HIGH)
#define PA_ENABLE               11
#define PA_ACTIVE_LEVEL         HIGH    // HIGH = Speaker ON, LOW = Speaker OFF

// ============================================================
// Status LED (Single LED on GPIO 26; no WS2812 RGB LED onboard)
// ============================================================
#define RGB_LED_PIN             -1
#define RGB_LED_COUNT           0
#define BUILTIN_LED_PIN         -1      // the red LED is on GPIO 26 = VEHICLE_UART_TX (J5): off until
                                        // confirmed on the board (blinking it broke the OBD UART)

// ============================================================
// Battery Monitoring — ADC (IP5306 + R52=68K / R57=100K divider)
// ============================================================
#define VBAT_ADC_PIN            53
#define VBAT_DIVIDER            1.68f   // (100K + 68K) / 100K = 1.68

// ============================================================
// BOOT Button (SW1 BOOTMODE on ESP32-P4)
// ============================================================
#define BOOT_BUTTON             35      // Active LOW, 10K pull-up R34

// ============================================================
// MicroSD Card — 4-bit SDMMC Slot 0 (IOMUX, LDO VO4 ch 4, PWR GPIO 45)
// ============================================================
#define SD_CLK                  43
#define SD_CMD                  44
#define SD_D0                   39
#define SD_D1                   40
#define SD_D2                   41
#define SD_D3                   42

// ============================================================
// Expansion GPIOs (JP1 2x13 header & J5 UART connector)
// ============================================================
#define VEHICLE_UART_TX         26
#define VEHICLE_UART_RX         27
#define EXP_GPIO_1              1
#define EXP_GPIO_2              2
#define EXP_GPIO_3              3
#define EXP_GPIO_4              4


// ============================================================
// Audio Configuration Constants
// ============================================================
#define AUDIO_SAMPLE_RATE_IN    16000   // Mic input: 16 kHz (Gemini input)
#define AUDIO_SAMPLE_RATE_OUT   24000   // Speaker output: 24 kHz (Gemini output)
#define AUDIO_BITS_PER_SAMPLE   16
#define AUDIO_CHANNELS          1       // Mono
#define I2S_DMA_BUF_COUNT       8
#define I2S_DMA_BUF_LEN         1024

// ============================================================
// Ring Buffer Sizes (PSRAM)
// ============================================================
#define TX_RING_BUFFER_SIZE     (96 * 1024)     // 96 KB playback buffer
#define RX_RING_BUFFER_SIZE     (48 * 1024)     // 48 KB capture buffer
