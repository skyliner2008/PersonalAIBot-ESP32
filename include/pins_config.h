/**
 * @file pins_config.h
 * @brief GPIO Pin Definitions for ES3C28P Board (ESP32-S3-N16R8)
 * @see wiki/01-Hardware-ES3C28P-Pinout.md
 *
 * IMPORTANT: Always reference the wiki pinout document when modifying
 * these definitions. Incorrect pin assignments can damage hardware.
 */

#pragma once

// ============================================================
// LCD Display — ILI9341V (240x320, 4-Wire SPI)
// ============================================================
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_MOSI        11
#define LCD_MISO        13
#define LCD_SCK         12
#define LCD_CS          10
#define LCD_DC          46
#define LCD_RST         -1      // Tied to CHIP_PU (hardware reset)
#define LCD_BL          45      // Backlight PWM — Active HIGH, default OFF!
#define LCD_WIDTH       240
#define LCD_HEIGHT      320
#define LCD_SPI_FREQ    40000000    // 40 MHz
#define LCD_SPI_READ_FREQ 16000000 // 16 MHz (read-back)

// ============================================================
// Capacitive Touch — FT6336G (I2C)
// ============================================================
#define TOUCH_I2C_ADDR  0x38
#define TOUCH_SDA       16      // Shared I2C bus with ES8311
#define TOUCH_SCL       15      // Shared I2C bus with ES8311
#define TOUCH_INT       17      // Interrupt — Active LOW
#define TOUCH_RST       18      // Hardware reset
#define TOUCH_I2C_FREQ  400000  // 400 kHz Fast Mode

// ============================================================
// Audio Codec — ES8311 (I2C + I2S)
// ============================================================
#define ES8311_I2C_ADDR 0x18
#define ES8311_SDA      16      // Shared I2C bus with FT6336G
#define ES8311_SCL      15      // Shared I2C bus with FT6336G

// I2S Audio Bus
// NOTE (verified on hardware 2026-10-14 with an ES8311 DAC->ADC loopback):
// the vendor table names the pins from the CODEC side. GPIO 8 carries
// MCU -> codec (DSDIN, playback) and GPIO 6 carries codec -> MCU (ASDOUT, mic).
// With 6/8 the other way round the speaker is silent and the mic reads zeros.
#define I2S_MCLK        4       // Master Clock
#define I2S_BCLK        5       // Bit Clock (SCK)
#define I2S_LRCK        7       // Word Select (WS)
#define I2S_DOUT        8       // MCU data out -> ES8311 DSDIN (playback)
#define I2S_DIN         6       // ES8311 ASDOUT -> MCU data in (microphone)

// Speaker Amplifier — FM8002E
// CRITICAL: Active LOW! Set LOW to enable, HIGH to disable
#define PA_ENABLE       1
#define PA_ACTIVE_LEVEL LOW     // LOW = Speaker ON, HIGH = Speaker OFF

// ============================================================
// RGB LED — WS2812 (NeoPixel)
// ============================================================
#define RGB_LED_PIN     42
#define RGB_LED_COUNT   1

// ============================================================
// Battery Monitoring — ADC
// ============================================================
#define VBAT_ADC_PIN    9
#define VBAT_DIVIDER    2.0f    // Voltage divider ratio (x2)

// ============================================================
// BOOT Button
// ============================================================
#define BOOT_BUTTON     0       // Active LOW, internal pull-up

// ============================================================
// MicroSD Card — 4-bit SDIO
// ============================================================
#define SD_CLK          38
#define SD_CMD          40
#define SD_D0           39
#define SD_D1           41
#define SD_D2           48
#define SD_D3           47

// ============================================================
// Expansion GPIOs
// ============================================================
#define EXP_GPIO_1      2
#define EXP_GPIO_2      3
#define EXP_GPIO_3      14
#define EXP_GPIO_4      21

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
