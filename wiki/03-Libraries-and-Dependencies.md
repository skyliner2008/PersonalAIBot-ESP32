---
tags: [libraries, dependencies, platformio, setup]
created: 2026-10-14
updated: 2026-10-14
status: done
---

# 📦 Libraries & Dependencies (v2)

> **Related:** [[00-Home-Dashboard]] | [[02-Installation-and-Setup]] | [[04-System-Architecture]]

---

## สรุป

| Library | Version (pinned) | ใช้ทำอะไร |
|---|---|---|
| **Platform** pioarduino `platform-espressif32` | 55.03.312 | Arduino-ESP32 **3.3.12** (ESP-IDF 5.5.5) — ตั้งแต่ **v4.0.0** ([[14-AEC-WakeWord-Plan]]); v3.x ใช้ `espressif32 @ 7.1.3` = Arduino 2.0.17 / IDF 4.4 |
| `lovyan03/LovyanGFX` | 1.2.33 | จอ ILI9341V, sprite, ฟอนต์ VLW ภาษาไทย |
| `bblanchon/ArduinoJson` | 7.4.3 | JSON ของ Gemini (allocator ใน PSRAM), ไฟล์ความจำ JSONL |
| `adafruit/Adafruit NeoPixel` | 1.15.5 | WS2812 LED (GPIO 42) |

**ไม่ใช้แล้ว (ถอดออกใน v2):**
- `links2004/WebSockets` — ตัดการเชื่อมต่อเมื่อ frame > 15 KB และไม่ตรวจ TLS → ใช้ `esp_websocket_client` ของ ESP-IDF แทน
- `lvgl/lvgl` — ไม่ได้ถูกใช้วาดจริง (หน้าจอวาดด้วย LovyanGFX) → ลบ `include/lv_conf.h` และ build flag `LV_*`

---

## ส่วนที่มากับ framework (ไม่ต้องติดตั้ง)

| Component | ใช้ใน |
|---|---|
| `esp_websocket_client` (IDF) | `GeminiLiveClient` — WSS, ping, auto-reconnect, ข้อความขนาดใหญ่ |
| `driver/i2s.h` (legacy, IDF 4.4) | `AudioManager` — I2S 24 kHz stereo full-duplex |
| `SD_MMC`, `LittleFS`, `FS` | `Storage` — ไฟล์ความจำ |
| `Wire` | `I2cBus` — ทัช FT6336G + ES8311 |
| `WiFi`, `Preferences` (NVS), `configTzTime` (SNTP) | การเชื่อมต่อ, volume, นาฬิกา UTC+7 |

> `driver/i2s.h` ใน IDF 4.4 ยังเป็น API หลัก (ไม่มี deprecation warning); ถ้าย้ายไป Arduino-ESP32 3.x ควรเปลี่ยนเป็น `ESP_I2S`/`i2s_std`
>
> **บน IDF 5.5 (v4.0.0):** `driver/i2s.h` ยังใช้ได้ (มี warning deprecated) · mbedtls 3: `mbedtls_sha256_*_ret` → `mbedtls_sha256_*` ·
> `esp_efuse_mac_get_default` ย้ายไป `<esp_mac.h>` · RAM ภายในหายไป ~35 KB → stack ของ `geminiIO` / `textModel` / `radio` อยู่ใน PSRAM
> (`system/PsramTask.h`, task ที่ไม่เขียน flash เอง) · package `framework-arduinoespressif32` ของ pioarduino **ทับ**ของ 2.0.17 — สลับกลับ `main` ต้องดาวน์โหลดใหม่ (~11 นาที)

---

## platformio.ini (ปัจจุบัน)

```ini
[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312/platform-espressif32.zip
; v3.x: platform = espressif32 @ 7.1.3
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_mode = qio
board_build.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = partitions_sr.csv
board_build.filesystem = littlefs
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -I include
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
upload_speed = 921600
lib_deps =
    lovyan03/LovyanGFX @ 1.2.33
    bblanchon/ArduinoJson @ 7.4.3
    adafruit/Adafruit NeoPixel @ 1.15.5
```

### Partition table: `partitions_sr.csv` (v4.0.0, เดิม `default_16MB.csv`)
| Partition | ขนาด | หมายเหตุ |
|---|---|---|
| nvs / otadata | 20 KB / 8 KB | ที่อยู่เดิม: ค่าที่ตั้งไว้ไม่หายตอนเปลี่ยนตาราง |
| app0 / app1 | 5 MB × 2 | OTA; firmware 3.4 MB (ESP-SR + esp-dl) |
| model | 3.25 MB | โมเดลคำปลุก ESP-SR (`srmodels.bin`, แฟลชโดย `tools/pio_sr_model.py` / อยู่ในไฟล์ factory) |
| spiffs | 2.6 MB | ใช้เป็น **LittleFS** สำหรับความจำเมื่อไม่มี SD |

บอร์ด v3.x ที่อัปเดตผ่าน Wi-Fi ยังใช้ตารางเดิม (ไม่มี `model` → ปลุกด้วยระดับเสียง) จนกว่าจะแฟลชไฟล์ factory ผ่าน USB

(เดิม `huge_app.csv` ใช้ได้แค่ 4 MB จาก 16 MB และไม่มี OTA)

### `ARDUINO_USB_CDC_ON_BOOT=1`
พอร์ต Type-C ของบอร์ดนี้ต่อกับ **USB ในตัว ESP32-S3** (ตรวจแล้ว: VID:PID `303A:1001`, COM4) จึงต้องเปิดค่านี้เพื่อให้ `Serial` ออกทาง USB

---

## เครื่องมือในโปรเจกต์

| ไฟล์ | หน้าที่ |
|---|---|
| `tools/gen_thai_font.py` | สร้างฟอนต์ VLW ไทยจาก TTF (Pillow) |
| `tools/gen_root_ca.py` | ดึง root CA ของ Google จาก certifi → `src/ai/GoogleRootCA.h` |
| `host_tests/run_tests.bat` | คอมไพล์และรัน unit tests บน PC ด้วย MSVC (ใช้ ArduinoJson จาก `.pio/libdeps`) |
