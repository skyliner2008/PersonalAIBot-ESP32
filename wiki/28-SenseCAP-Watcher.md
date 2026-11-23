---
tags: [hardware, pinout, watcher, esp32-s3, board, spd2010, es7243e, tca9555, round-display]
created: 2026-11-22
status: built (env watcher) — NOT yet run on a board; screen / touch / knob / mic / speaker untested
---

# 28 — บอร์ด Seeed Studio SenseCAP Watcher (W1-A, จอกลม 1.45" 412×412)

> **Related:** [[27-Waveshare-S3-LCD-1.54]] (UI 240×240 ชุดเดียวกัน — หน้าคลื่นเสียงแทนดวงตา) · [[07-Troubleshooting-Log#T233]] ·
> [[26-JC3248W535-3.5inch]] (SD ต่อแบบ SPI → SDMMC 1-bit แบบเดียวกัน) · [[14-AEC-WakeWord-Plan]]

**SenseCAP Watcher W1-A** — ESP32-S3 (rev v0.2) + flash **32 MB** + PSRAM **8 MB Octal**, จอกลม IPS 1.45" **412×412 SPD2010** (QSPI) พร้อมทัชในตัว,
ลำโพงผ่าน **ES8311**, ไมค์ผ่าน ADC **ES7243E**, IO expander **TCA9555** คุมไฟเลี้ยงทุกส่วน, **ล้อหมุน (knob) + ปุ่มกด**, ไฟ RGB WS2813, ช่อง microSD,
แบต, และชิปกล้อง/AI **Himax WiseEye2 HX6538** (SPI — เฟิร์มแวร์นี้ยังไม่ใช้ แค่เปิดไฟเลี้ยงไว้เหมือน xiaozhi)

**ที่มา:** ผู้ใช้ขอเพิ่มเฟิร์มแวร์บอร์ดนี้ (2026-11-22, T233) → build แยก `pio run -e watcher` (ซอร์สเดียวกับ ES3C28P)
· release: `-factory-watcher.bin` / `-app-watcher.bin` · `config` "chip" = `watcher`

## ⚠️ ขาที่ผู้ใช้ส่งมา ≠ ขาจริง

โน้ตที่ผู้ใช้ให้มาระบุจอ QSPI = CS 45 / CLK 40 / D0 46 / D1 42 / D2 41 / D3 39, ทัช I2C ร่วม 47/48, knob A = 41, LED = 40 — **ขาชนกันเอง**
(41 เป็นทั้ง D2 และ knob, 40 เป็นทั้ง CLK และ LED) ตรวจกับซอร์สจริงของ xiaozhi-esp32 `main/boards/sensecap-watcher/config.h` +
`sensecap_watcher.cc` แล้วใช้ตามนั้น (ตารางด้านล่าง) — ถูกเฉพาะ I2C 47/48, I2S ทั้ง 5 ขา, ไฟจอ 8, จอ CS 45

## ขา (xiaozhi-esp32 board "sensecap-watcher")

| หน้าที่ | ขา |
|---|---|
| จอ SPD2010 (QSPI บน **SPI3**, mode 3, 40 MHz) | CS **45**, CLK **7**, D0 **9**, D1 **1**, D2 **14**, D3 **13** · ไม่มี RST (ไฟเลี้ยงจอผ่าน expander) · ไฟจอ **8** (PWM) |
| I2C0 (บัสหลัก) | **SDA 47 / SCL 48**: ES8311 0x18 · ES7243E 0x14 · TCA9555 **0x21** |
| I2C1 (ทัช) | **SDA 39 / SCL 38**: ทัช SPD2010 **0x53** · INT = expander P0.5 (ไม่ใช้ — อ่านแบบ poll) |
| I2S (standard 16-bit stereo, 24 kHz, MCLK 256 fs) | MCLK 10, BCLK 11, WS 12, **DOUT 16** → ES8311, **DIN 15** ← ES7243E (ไมค์ = ช่อง**ขวา**) |
| แอมป์ลำโพง | expander **P1.4** (เปิดตอนบูต) |
| Knob | A **41**, B **42** (ไม่มี pull-up บนบอร์ด — ใช้ของในชิป) · ปุ่มกด = expander **P0.3** (LOW = กด) |
| USB | Type-C → **CH342** → UART0 (TX 43 / RX 44) = console + แฟลช (T235) · USB ในตัว S3 ไม่ได้ต่อออก |
| SD (ต่อแบบ SPI บน SPI2) | CLK 4, MOSI 5, MISO 6, CS **46** → ใช้ SDMMC 1-bit (CLK 4 / CMD 5 / D0 6, D3=CS ยก HIGH) · Himax ใช้ 4/5/6 ร่วม (CS 21 ยก HIGH) |
| แบต | ADC **GPIO 3** ผ่าน 62k/20k (**×4.1**) — ตัวแบ่งแรงดันเปิดด้วย expander P1.7 · ชาร์จ = expander P0.0 (LOW) |
| RGB LED | **40** (WS2813-mini) |
| Expander INT | GPIO **2** (LOW) — ใช้ปลุกจาก deep sleep ตอนปิดเครื่องขณะเสียบ USB |
| Himax | CS 21 · RESET = expander P0.7 · SYNC = P0.6 · UART flasher 17/18 |

**TCA9555** (port 0 = อินพุต, port 1 = สวิตช์ไฟ ยกเว้น P1.5): P1.0 SD · P1.1 จอ · **P1.2 SYSTEM** (บนแบต = ตัวค้ำไฟทั้งบอร์ด) · P1.3 Himax ·
P1.4 แอมป์ · P1.6 Grove · P1.7 ตัวแบ่งแรงดันแบต — ตอนบูต (`Tca9555::begin`): SYSTEM → 100 ms → เปิดที่เหลือทั้งหมด (`0xDF`) → 50 ms
· ต่างจาก xiaozhi ตรงที่**ไม่ดึง SYSTEM ลงชั่วขณะ** (xiaozhi เขียนเอาต์พุตทั้งหมดเป็น 0 ก่อน) — เขียนค่าเอาต์พุตก่อนตั้งทิศทางขา

## เฟิร์มแวร์

- **UI:** ใช้ UI 240×240 แบบ smartwatch ของ Waveshare 1.54 ทั้งชุด (`LCD_SQUARE_PANEL` — หน้าคลื่นเสียงแทนดวงตา, เมนูไอคอนกลม, เครื่องเล่นเพลง)
- **ธีมหน้าหลัก (T236, 2026-11-23 — ผู้ใช้ขอดวงตาเป็นอีกแบบ):** เมนู ตั้งค่า → หน้าจอ → **หน้าหลัก** แตะสลับ **คลื่นเสียง / ดวงตา** ทันที
  (NVS `facetheme`, console `set facetheme wave|eyes` หรือ 0/1, `config` มี `facetheme`) · เฟิร์มแวร์ตัวเดียวมีทั้งสองแบบ: `Eyes.cpp`
  เพิ่มโค้ด ~6 KB — app เหลือที่ว่าง ~210 KB ในช่อง 5 MB · ดวงตาย่อให้พอดีจอ 240 (`Eyes::_k` = 0.72 — ขอบตาห่างกลางจอไม่เกิน ~115 px
  ไม่ชนขอบวงกลม) · โหมดดวงตา: แตะกลางจอ = แตะปกติ (หยุด / ปลุก / จิ้ม) ไม่ใช่ปุ่มไมค์ · ปิดไมค์อยู่ = ไอคอนไมค์แดงใต้ดวงตา ·
  ไม่ลดเฟรมเป็น 10 fps ตอนนิ่ง (กะพริบตาจะกระตุก) ยกเว้นตอนหลับ · ใช้กับ Waveshare 1.54 (ws154) ได้เหมือนกัน
  + `LCD_ROUND_PANEL`: `UiManager::present()` ขยายภาพ **1.35 เท่า (324 px)** วางกลางจอกลม (x/y 44–367) — วงกลมรัศมี 206 px เห็นภาพถึง
  152 px จากกลาง (ไอคอนมุมของเลย์เอาต์ 240×240 ยังอยู่ในวง) · หน้าต่าง SPD2010 เริ่ม/จบที่คอลัมน์ 4n…4n+3 (xiaozhi ปัดแบบนี้) ·
  ขอบดำรอบนอกวาดครั้งเดียวตอนบูต · ภาพแคปจอ (`screenshot`) = canvas 240×240 · ไม่มี IMU → ไม่หมุนจอเอง (`rotation 3` = กลับหัว)
- **จอ:** `LGFX_WATCHER.hpp` — `Panel_SPD2010` สืบจาก `Panel_ST77916` ของ LovyanGFX (QSPI framing เดียวกัน: คำสั่ง `02 00 cmd 00`,
  พิกเซล `32 00 2C 00`, CASET/RASET ทุกหน้าต่าง) + ตาราง init 402 รายการของ Espressif `esp_lcd_spd2010` (Apache-2.0) · SWRESET → ตาราง
  (จบด้วย SLPOUT 120 ms) → หน้า user → MADCTL 0x00 → COLMOD 0x55 → DISPON
- **ทัช:** `TouchSPD2010.h` — โปรโตคอลตาม Espressif `esp_lcd_touch_spd2010` (อ่านสถานะ `20 00`, รายงาน `00 03`, HDP `FC 02`, คำสั่ง clear INT /
  CPU start / point mode / start) · **xiaozhi ไม่ได้เปิดทัชเลย** (ดึงขา 38/39 ลง) → ยังไม่รู้ว่าทำงานบนบอร์ดจริงไหม; ถ้าไม่ตอบ จะลองใหม่ทุก 1 วิ
  และทุกอย่างยังใช้ได้ด้วย knob
- **เสียง:** `AUDIO_ES7243E` ใน `AudioManager` — ES8311 เล่น, ES7243E อัด (+27 dB เหมือน xiaozhi; `micgain` 0–7 = ×6 dB), echo reference =
  สิ่งที่ส่งไปลำโพง (`AUDIO_SOFT_ECHO_REF` ร่วมกับ ES7210 ของ Waveshare) · เลือกช่องไมค์อัตโนมัติ (เริ่มที่ช่องขวา)
- **Knob:** หมุน = เสียงผู้ช่วย ±5 % (ตัวนับ quadrature ใน interrupt, `kKnobEdgesPerStep` = 4 ขอบต่อ 1 คลิก) ·
  ปุ่ม: กดสั้น = คุย / ย้อนกลับ · ค้าง 1.5 วิ = เมนูตั้งค่า · ค้าง 3 วิ = เตือน · **ค้าง 5 วิ = ปิดเครื่อง** (ปิด Live + flush ความจำ → ปิดชิปเสียง/จอ →
  ตัดไฟทุกราง, SYSTEM สุดท้าย; ยังติดอยู่ = เสียบ USB → deep sleep รอกดปุ่ม knob ผ่าน INT ของ expander)
- **SD:** `SD_1BIT_ONLY` (เหมือน JC3248W535) + ยก CS ของ Himax (21) ขึ้นก่อน
- **Flash:** ใช้เป็น 16 MB (พาร์ทิชันเดียวกับบอร์ด S3 ทุกตัว — OTA ข้ามรุ่นได้เหมือนเดิม; 16 MB บนไม่ได้ใช้)
- ไม่มีหน้าปัดรถ (`VEHICLE_DASH=0`, ตัด `vehicle/*`, `DashScreen`, `Eyes` ออกเหมือน ws154)
- ขนาด (2026-11-22): Flash 93.8% (`firmware.bin` 5,014,224 B จาก app slot 5,242,880) · RAM static 92 KB · build `esp32s3` / `ws154` /
  `jc3248w535` ยังผ่าน, host tests 4,677 checks ผ่าน
- **Flasher (T233):** ESP32-AI-Flasher.exe **2.2.6** สแกนแล้วขึ้น "✔ ชิปบนบอร์ดตรงกับ SenseCAP Watcher" เมื่อ TCA9555 (0x21) หรือ ES8311 (0x18) ตอบที่
  SDA 47 / SCL 48 และตั้งชื่อ 0x14 = ES7243E, 0x53 = ทัช SPD2010 · .apk **2.1.6** มีรุ่นนี้ให้เลือกเอง (ไม่เดาจาก pull-up) · Studio (`esp_flash.py`) เลือกเอง ·
  ทั้งหมดทดสอบกับบอร์ดจำลองเท่านั้น (wiki 17)
- **สแกนบอร์ดของลูกค้า:** ให้ลูกค้ากด 🔍 สแกนบอร์ด แล้วส่งรายงาน (บรรทัด `i2c: sda47 scl48: 14 18 21` ฯลฯ) มาตรวจก่อน + สำรอง flash 32 MB ไว้ก่อนแฟลช

## ผลสแกนบอร์ดจริงของลูกค้า (T235, 2026-11-22, Flasher 2.2.6)

บอร์ดมี xiaozhi 2.5.2 (SKU sensecap-watcher) ต่อ USB ผ่าน **CH342 (1A86:55D2)** ไม่ใช่ USB ในตัว S3 · ESP32-S3 (QFN56) rev v0.2 ·
flash 32 MB Winbond (JEDEC ef 40 19) · PSRAM 8 MB octal 3.3 V — ตรงกับ build `watcher` (qio_opi, ใช้ flash 16 MB ล่าง)

| ผลสแกน | เทียบกับเฟิร์มแวร์ |
|---|---|
| pull-up: 0, 2, 4, 5, 17, 18, 39, 40, 47, 48 | ✔ BOOT 0 · INT expander 2 · SD/Himax CLK 4 / MOSI 5 · Grove UART 17/18 · ทัช SDA 39 · LED 40 · I2C0 47/48 |
| ถูกดึงลง: 15, 38 | ✔ 15 = DIN ของ ES7243E (ชิปขับต่ำ) · 38 = SCL ทัช — ⚠ อ่านได้ต่ำทั้งที่ SDA 39 สูง: ทัชอาจไม่ตอบ (xiaozhi ก็ไม่เปิดทัช) ใช้ knob แทนได้ |
| ไม่มีอะไรดึง: 41, 42 | ✔ knob ไม่มีตัวต้านทานบนบอร์ด — เฟิร์มแวร์เปิด pull-up ในชิปเอง (`INPUT_PULLUP`) |
| ไม่มีอะไรดึง: 1, 7, 9, 13, 14, 45, 8, 10–12, 16, 3, 21, 46, 6 | ✔ จอ QSPI / ไฟจอ / I2S / ADC แบต / CS Himax / SD (CS 46 เฟิร์มแวร์ยก HIGH, D0 ใช้ pull-up ในชิปของ SDMMC) |
| xiaozhi log `IO expander initialized: ffd9` (เสียบ USB) | port 0 = `0xD9`: P0.2 VBUS = **0 ทั้งที่เสียบ USB** → VBUS_IN_DET **active LOW** (xiaozhi: ชาร์จ = VBUS == 0) · CHRG 1 / STDBY 0 = แบตเต็ม · knob 1 = ไม่กด |

**แก้ตามผลสแกน (T235):**
- **Console = UART0 ผ่าน CH342** (`-DARDUINO_USB_CDC_ON_BOOT=0` เฉพาะ env `watcher`) — เดิมใช้ USB CDC ของ S3 ซึ่งไม่ได้ต่อออกช่อง Type-C:
  Studio / Flasher / แอปมือถือจะไม่เห็น log และตั้ง Wi-Fi / API key ผ่านสายไม่ได้ · 115200 เหมือนทุกแอป · บัฟเฟอร์รับ 12 KB ตั้งแต่บูต
  (UART ขยายตอนรันไม่ได้ — อัปโหลดเพลงใช้ได้) · แคปจอ 115 KB ที่ 115200 ใช้ ~10 วิ · การเปิดพอร์ตอาจรีสตาร์ตบอร์ด (วงจร auto-reset DTR/RTS)
- **`Tca9555::usbPower()` กลับขั้ว** (LOW = มี USB) และใช้เป็น "เสียบสาย" ของตัววัดแบต (เดิมใช้ `HWCDC::isPlugged()` ซึ่งไม่มีทางรู้บนบอร์ดนี้)
- **Flasher .exe 2.2.8:** สแกน I2C ลองคู่ขาของบอร์ดเราก่อน — ผ่าน CH342 คู่ละ ~1.5 วิ (USB ในตัว ~130 ms) 30 วิหมดที่ 19/90 คู่
  ก่อนถึง 47/48 เลยรายงาน "ไม่พบ I2C / ขาไม่ตรงบอร์ดที่รองรับ" · ทดสอบ `ASlowLinkStillReachesTheWatchersBusFirst` (ตกกับลำดับเดิม)
- ยังช้า: Flasher คุย ROM ที่ 115200 เสมอ (บอร์ด USB ในตัวไม่สนความเร็ว) → แฟลชผ่าน CH342 ช้ากว่าบอร์ดอื่นมาก (ยังไม่ได้เพิ่ม CHANGE_BAUDRATE)

## Console เฉพาะบอร์ด

- `knob` — ตัวนับขอบ quadrature ดิบ, ระดับขา A/B, ปุ่ม, อินพุต expander (P0/P1), ชาร์จ, VBUS → ใช้ปรับ `kKnobEdgesPerStep` / ทิศหมุน
- `rails [<hex>]` — ดู / ตั้ง port 1 ของ TCA9555 (สวิตช์ไฟ) · ระวัง: ปิดบิต 0x04 (SYSTEM) บนแบต = เครื่องดับ

## ยังไม่ได้ตรวจบนบอร์ดจริง (ต้องให้ผู้ใช้ลอง)

1. จอขึ้นภาพ / สีถูก (ถ้าสีกลับ: `set invert 1` · ถ้าแดง-น้ำเงินสลับ: `rgb_order`)
2. ทัชตอบไหม (log `[Touch] SPD2010 (0x53) found`) — แกน x/y ตรงไหม
3. ทิศหมุนของ knob (ตามเข็ม = เพิ่ม/ลด?) และ 4 ขอบ/คลิก ถูกไหม (`knob`)
4. ไมค์อยู่ช่องไหน (`audio`), ระดับเสียงลำโพง, echo
5. ~~ขั้ว VBUS_IN_DET~~ (T235: LOW = มี USB จาก log xiaozhi) · ขั้ว CHRG_DET ขณะกำลังชาร์จจริง และตัวคูณแบต 4.1 เทียบมิเตอร์
6. ปิดเครื่องบนแบต (ตัด SYSTEM) และปลุกจาก deep sleep บน USB
7. สำรองเฟิร์มแวร์เดิมของ Seeed ก่อนแฟลช: `esptool --chip esp32s3 read_flash 0 0x2000000 watcher-factory-32MB.bin`
