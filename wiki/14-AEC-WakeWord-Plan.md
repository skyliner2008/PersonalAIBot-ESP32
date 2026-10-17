---
tags: [aec, wakeword, esp-sr, plan, esp32-ai]
created: 2026-10-16
updated: 2026-10-16
status: in-progress
---

# 14 — แผน: AEC + คำปลุกออฟไลน์ด้วย ESP-SR

> วิเคราะห์ 2026-10-16 (ยังไม่ได้ลงมือ) · อ้างอิง: [esp-sr](https://github.com/espressif/esp-sr) v2.x, [benchmark ESP32-S3](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html),
> Arduino `ESP_SR` (arduino-esp32 3.x), [pioarduino](https://github.com/pioarduino/platform-espressif32), ผลวัด ES8311 reg 0x44 ใน [[01-Hardware-ES3C28P-Pinout]]

## ได้อะไร (ปัญหาที่หายไป)
| ตอนนี้ | หลังทำ |
|---|---|
| ปลุกด้วย**ระดับเสียง** → ตื่นเองจากวิทยุ/ทีวี/พัดลม (T050, T051, T053) | ปลุกด้วย**คำปลุก** (WakeNet) — เสียงอื่นไม่ปลุก, false wake ≈ 1 ครั้ง/12 ชม. (ตัวเลข Espressif ของ Alexa) |
| **half duplex**: ปิดไมค์ตอน AI พูด ต้องแตะจอเพื่อขัด | AEC ตัดเสียงลำโพงออก → **พูดแทรก AI ได้**, ปลุกได้ขณะวิทยุดัง (อ้างอิงรวมเสียงวิทยุ) |
| VAD พลังงานอย่างเดียว | VAD ของ AFE (+ NS) หลัง AEC |

## สิ่งที่ตรวจแล้ว
1. **Arduino-ESP32 2.0.17 (ที่ใช้อยู่) ไม่มี ESP-SR** — ไม่มีไลบรารี `ESP_SR`, ไม่มี lib AFE/WakeNet (มีแค่ header ของ esp-dl) · PlatformIO ทางการหยุดที่ 2.0.17
   → ต้องย้ายไป **Arduino 3.3.x / IDF 5.5 ผ่าน pioarduino** (community fork ของ platform-espressif32) ซึ่งมี esp-sr ในตัวสำหรับ S3
2. **`ESP_SR` ของ Arduino ใช้ทำ AEC ไม่ได้ตรงๆ:** ช่องที่เกินจากไมค์ถูกเติมศูนย์ (ไม่มีช่อง reference) และผูกกับ MultiNet → เรียก API ของ esp-sr โดยตรง
   (`afe_config_init("MR", models, AFE_TYPE_SR/FD, AFE_MODE_LOW_COST)`, `feed()` / `fetch()`)
3. **ฮาร์ดแวร์พร้อม:** ES8311 reg 0x44 = `0x50` → L ไมค์ + R DAC loopback (วัดแล้ว) = input **"MR"** ที่อ้างอิงตรงกับไมค์ทุก sample ไม่ต้องหา delay เอง
   (xiaozhi ไม่ได้ใช้วิธีนี้ — บอร์ด ES8311 ล้วนของเขาปิด `input_reference_`)
4. **คำปลุกที่มีให้ฟรี (WakeNet9, S3):** `wn9_jarvis_tts` (**"Jarvis"** — ใกล้ชื่อ "จาวิส"), `wn9_hiesp`, `wn9_alexa`, `wn9_computer_tts`, `wn9_heywillow_tts`, `wn9_sophia_tts`,
   `wn9_mycroft_tts` ฯลฯ (96 โมเดล) · **ไม่มีภาษาไทย** — คำปลุกไทยต้องจ้าง Espressif (ค่าเทรนครั้งเดียว ติดต่อ sales, TTS-only หรืออัดเสียง 500 คน, 2–3 สัปดาห์)
5. **ทรัพยากร (benchmark S3):** AFE "MR" SR LOW_COST ≈ **60 KB RAM ภายใน + 740 KB PSRAM, CPU ~19 % ของ 1 core** (FD ~22 %) · WakeNet9 ≈ 16 KB + 324 KB PSRAM, 3 ms/เฟรม 32 ms
   ตอนนี้ RAM ภายในว่าง ~102 KB (ต่ำสุด 94) → เหลือ ~20 KB = **เสี่ยงที่สุด** (ใช้ `AFE_MEMORY_ALLOC_MORE_PSRAM`, ต้องวัดบน IDF 5)
6. **AFE รับ 16 kHz** แต่ I2S ใช้ 24 kHz (เสียง Gemini) → ลด 24→16 kHz ทั้งสองช่องด้วยตัวเดียวกัน (มีอยู่แล้วสำหรับไมค์) ตำแหน่งตรงกันคงเดิม
7. **โมเดลอยู่ใน partition "model"** (`esp_srmodel_init("model")`) → ตาราง partition ปัจจุบัน (`default_16MB.csv`) ไม่มี → **ต้องเปลี่ยนตาราง = แฟลช USB 1 ครั้ง** (OTA เปลี่ยน partition ไม่ได้)

## ผลกระทบต่อโค้ด (ย้ายไป IDF 5)
| ส่วน | งาน |
|---|---|
| mbedtls 3 | `mbedtls_sha256_*_ret` → ชื่อใหม่ (SecretBox, OtaUpdater) |
| I2S | driver แบบเก่า (`driver/i2s.h`) ยังใช้ได้ใน IDF 5 (deprecated) — ห้ามปนกับ driver ใหม่ · ภายหลังย้ายไป `i2s_std` |
| Gemini client | `esp_transport` / `esp_transport_ws` ยังอยู่ — ต้องทดสอบซ้ำจุดที่เคยพัง (T039 race, T044 frame reader, T047 keep-alive pointer) |
| WiFi / HTTPClient / WiFiClientSecure / Preferences / LovyanGFX | API เดิมเกือบทั้งหมด (LovyanGFX 1.2 รองรับ 3.x) — ทดสอบ |
| OTA | ใช้ต่อได้ (`verifyRollbackLater` ยังมี) · release แรกบน IDF 5 + partition ใหม่ = แฟลช USB · หลังจากนั้น OTA ตามปกติ |
| AudioManager / AgentMic | ไมค์ L + ref R → AFE → (AEC + VAD + WakeNet) → เสียงสะอาดให้ Gemini · คำปลุกแทน `wake(true)` จากพลังงาน · ยกเลิก half duplex เป็นตัวเลือก |

## แผนงาน (แต่ละเฟสจบที่จุดตัดสินใจ)
1. **เฟส 0 — ✅ เสร็จ (v3.5.1, T056):** reg 0x44 = 0x50 + เสียงอ้างอิงจริงในการปลุกด้วยเสียงตอนวิทยุเล่น (`util/EchoWakeGate.h`)
   วัดบนบอร์ด: วิทยุ 3.5 นาทีที่ระดับ 70 (สตรีมสะดุด 2 ครั้ง) ไม่ตื่นเอง · เปลี่ยนระดับเสียง 85 → 60 → 80 → 70 ไม่ตื่นเอง · peak ratio เพลงล้วนต่อ chunk 3–9× → 2.4–4.6×
   (ปลุกต้อง > 3× ต่อเนื่อง 8 chunk) · ผู้ใช้ทดสอบแล้ว**ปลุกไม่ติด** (เสียงพูดดังกว่าเสียงรั่วแค่ 1–3 เท่า)
   → **เฟส 0.5 (T057): AEC เล็กๆ เอง — NLMS 128 tap บนเสียงอ้างอิง ตัดได้ 14–42 dB บนบอร์ด** ใช้ CPU 7.8 ms/40 ms ตอนมีเสียงเล่น, RAM ภายใน 0 (อยู่ใน PSRAM)
   = ได้ผลหลักของเฟส 2 (ปลุกด้วยเสียงตอนลำโพงดัง + ไม่ส่งเสียงลำโพงให้ Gemini) โดยไม่ต้องย้ายแกน · เฟส 2 ยังคุ้มสำหรับคำปลุก (WakeNet) และพูดแทรก AI
2. **เฟส 1 — spike ย้ายแกน (branch แยก):** `platform = pioarduino stable` build เฟิร์มแวร์ปัจจุบัน (ยังไม่มี SR) แก้ compile, ทดสอบทุกฟีเจอร์บนบอร์ด, วัด RAM/flash/ความเสถียร Live 1 ชม.
   **ผ่านเมื่อ:** ทุก host test + เสียงไม่กระตุก + ไม่รีบูต + RAM ภายในว่าง ≥ 100 KB
   **ผล 2026-10-16 (branch `feature/esp-sr`, pioarduino 55.03.312 = Arduino 3.3.12 / IDF 5.5.5, T060):**
   - compile แก้ 2 จุด (mbedtls 3, `esp_mac.h`) · firmware 2.0 MB (30.6 %), static DRAM 96 KB · rollback ของ bootloader เปิดอยู่ · **esp-sr มากับ framework แล้ว** (AFE, WakeNet, `CONFIG_SR_WN_WN9_HIESP`)
   - RAM ภายในหาย ~35 KB → stack 3 task ไป PSRAM (`system/PsramTask.h`) → **บูต 102 KB, ใช้งาน 88–101, ต่ำสุด 59–83** (เท่า IDF 4) · คำสั่ง `mem`
   - host tests ✓ · Live/tools/OTA check/วิทยุ + AEC ✓ · ไม่รีบูต · เจอและแก้: ตื่นเองตอนหยุดวิทยุ, สตรีมค้างช่วงเริ่ม
   - ยังไม่ได้: ทดสอบ Live ต่อเนื่อง 1 ชม. · เสียงจริงจากผู้ใช้ (ปลุก/สั่งงาน) บน IDF 5
3. **เฟส 2 — AFE + คำปลุก:** partition ใหม่ (app 2 × 4 MB, model ~1 MB, LittleFS) · input "MR" · WakeNet `jarvis` (+ `hiesp` สำรอง) · ตั้งค่าในเมนู: วิธีปลุก (คำปลุก / ระดับเสียงแบบเดิม)
   **ผ่านเมื่อ:** ตื่นจาก "Jarvis" ≥ 9/10 ที่ 1–2 ม. (สำเนียงไทย), ไม่ตื่นเองจากวิทยุ 8 ชม., RAM ภายในว่าง ≥ 25 KB
   **2a (2026-10-16, กำลังทำ):**
   - `partitions_sr.csv`: nvs/otadata ที่เดิม (ค่าที่ตั้งไว้ไม่หาย) · app 2 × **5 MB** (firmware 3.4 MB — esp-dl ~0.9 MB มากับ AFE) · `model` 3.25 MB · LittleFS 2.6 MB (ย้ายที่ = ข้อมูลบน LittleFS หาย, SD ไม่กระทบ)
   - `tools/pio_sr_model.py` แฟลช `srmodels.bin` ลง `model` ทุกครั้งที่ upload ทาง USB (offset อ่านจากตาราง) · ใช้ `models/srmodels.bin` ถ้ามี ไม่งั้นใช้ชุดที่มากับ framework
     (esp-sr 2.5.3: **`wn9_hiesp` "Hi ESP"** + vadnet1 + mn7_en — ยังไม่มี Jarvis ต้องแพ็กเองจาก repo esp-sr)
   - `audio/WakeWord`: AFE "MR" (AEC + WakeNet, ไม่มี NS/VAD/AGC, MORE_PSRAM, task AFE **core 1**) ป้อนไมค์ดิบ + ref **เฉพาะตอนหลับ** แบบไม่บล็อก (stream buffer → task `wakefeed`, T062) · task `wakeword` (stack PSRAM) รอ `fetch()`
     · ไม่มี partition `model` = ปิดเอง ปลุกด้วยระดับเสียงเหมือนเดิม
   - ตั้งค่า `wakeMode` (NVS `wakemode`): 0 ระดับเสียง (เดิม), 1 คำปลุกอย่างเดียว, 2 ทั้งสองแบบ (ค่าเริ่มต้น) · `set wake level|word|both` · diag `wake_mode`, `wake_word`, `wake_word_heard`
   - static DRAM +14 KB จาก esp-sr (`sigmoid_table` 5.6 KB, runtime ของ esp-dl 5 KB, `subband_filter_coef` 3 KB)
   - **"Jarvis":** `models/srmodels.bin` (568 KB) = `wn9_jarvis_tts` + `wn9_hiesp` จาก github.com/espressif/esp-sr (master — ไฟล์ "Hi ESP" ตรงกับชุดของ 2.5.3 ทุกไบต์)
     แพ็กด้วย `tools/pack_sr_models.py` (เขียนเองตามรูปแบบของ `pack_model.py`) · ที่มาใน `models/README.md` · อยู่ใน git + `models/LICENSE-esp-sr.txt` (ESPRESSIF MIT)
   - **AFE กิน RAM ภายใน ~30–36 KB** → ครั้งแรกเหลือ 7 KB (เรียก `begin()` ซ้ำ = AFE 2 ชุด → Wi-Fi ต่อไม่ได้ จอดับ, T061) · แก้: AFE ชุดเดียว + สร้างเฉพาะ `wakeMode != 0`
     + ไม่ยอมเปิดถ้าเหลือ < 50 KB · หา RAM คืน: stack `webTools` ไป PSRAM (เมื่อข้อมูลอยู่บน SD), buffer อ่าน frame ของ Gemini 4 KB + การ์ดของ webTools 3 KB ไป PSRAM,
     stack `agent` 12 → 9 KB, `ui` 10 → 8 KB (จากค่าที่ไม่เคยใช้ใน soak 1 ชม.)
   - **วัดบนบอร์ด (AFE + Jarvis ทำงาน):** บูต 62 KB · หลับ + วิทยุ 53 KB · **ต่ำสุด 34 KB** (เกณฑ์ ≥ 25) · หลับขณะวิทยุ 2.5 นาที ไม่ตื่นเอง (`wake_word_heard` 0), ERLE ของเรา 10–34 dB
   - เมนูบนจอ "ปลุกด้วยเสียง (แตะ)" + ESP32-AI Studio (ช่อง "ปลุกด้วยเสียง") ✓
   - **ผลกับเสียงผู้ใช้ (T064):** "Jarvis" (โมเดล TTS) ไม่จับเสียงคนไทยเลย → ค่าเริ่มต้น **"Hi ESP"** + ความไว **0.50** (NVS) + WakeNet รับเสียงหลัง **AEC ของเรา**
     (AFE "M", RAM 23 KB) → วิทยุเพิ่งเปิด 2/3, เพิ่งเปลี่ยนสถานี 3/3 · AEC ของ AFE ต้องรอ ~1 นาทีหลังสตรีมเริ่ม (0/3)
   - **2 คำพร้อมกัน** `set wakeword both` (T066): AFE 39 KB, ต่ำสุด 35 KB · Jarvis ที่ 0.5 ปลุกเองจากวิทยุได้ → ค่าเริ่มต้นยัง Hi ESP คำเดียว
   - **ผู้ใช้ทดสอบเปิดวิทยุทิ้งไว้นาน (2026-10-17, 2 คำ ความไว 0.5): ไม่ตื่นเอง** ✅
   - ยังไม่ได้: คำปลุกภาษาไทย "จาวิส" (ต้องเทรนโมเดลเอง/จ้าง Espressif), factory bin ของ release ต้องรวม `srmodels.bin` (`make_release.py`)
4. **เฟส 3 — พูดแทรก (barge-in):** **3A ทำแล้ว (2026-10-17, T067): แทรกด้วยคำปลุก** — ตอน AI พูด ไมค์ยังเปิดให้ WakeNet อย่างเดียว
   (หลัง AEC ของเรา, ไม่ส่งให้ Gemini, ไม่แตะ VAD/noise floor) · จับได้ = เหมือนแตะจอ (หยุดเสียง, ทิ้งคำตอบที่เหลือ) + ฟังต่อทันที 6 วิ
   · กันขัดจังหวะตัวเอง: มี 2 คำ = แทรกได้เฉพาะ "Hi ESP" (Jarvis โมเดล TTS จับเศษเสียงสะท้อนของ AI ที่ −63 dBFS) + ต้องดัง ≥ −50 dBFS
   (`AGENT_BARGE_IN_WORD`, `AGENT_BARGE_IN_MIN_DBFS`) · ทดสอบ: AI อ่านข้อความที่มี "Hi ESP"/"Jarvis" 6 ครั้ง + นิทาน 30 วิ = ไม่หยุดตัวเอง
   แผนเดิม (3B, แทรกด้วยเสียงอะไรก็ได้): ส่งเสียงหลัง AEC ระหว่าง AI พูด (gate ด้วย VAD ของ AFE) · ทดสอบว่า Gemini ไม่ขัดตัวเองจากเสียงสะท้อนที่เหลือ · ตัวเลือกเปิด/ปิดได้
5. **เฟส 4 — release 4.0.0 (2026-10-17):** factory bin รวม `srmodels.bin` ที่ partition `model` (`make_release.py`) · แฟลช USB ครั้งเดียว เพราะ partition เปลี่ยน
   · บอร์ด v3.x อัปเดตผ่าน Wi-Fi ได้ (app 3.4 MB ลง slot 6.25 MB) แต่ไม่มี `model` = ปลุกด้วยระดับเสียง · OTA ตามปกติหลังจากนั้น · wiki/README

## ความเสี่ยง
> **งบ RAM ภายในล่าสุด (v3.5.1, ไม่มีวิทยุ):** ว่าง 90 KB, ต่ำสุด 76 KB (v3.4.x: 102 / 94) — ส่วนต่างคือ stack 10 KB ของ task `ota` (v3.5.0) + buffer คู่ไมค์/อ้างอิง ~6 KB (v3.5.1)
> → ก่อนเฟส 2: สร้าง task `ota` เฉพาะตอนใช้, ย้าย buffer static ไป PSRAM
> **บน IDF 5 (T060):** บูต 102 KB, ใช้งาน 88–101, ต่ำสุด 59 (TLS 3 ช่องพร้อมกัน: Gemini + วิทยุ https + OTA) — ยังย้ายได้อีก: stack `ota` 10 KB (ใช้ต่อเมื่อเรียก),
> `ui` 10 KB / `agent` 12 KB เหลือว่าง 7–9 KB (ลดขนาดได้) · AFE + WakeNet ต้องการ ~76 KB
- **RAM ภายใน** (AFE + WakeNet ~76 KB บน IDF 5 ที่กินมากกว่าเดิม) — ถ้าไม่พอ: ปิด NS, ใช้ LOW_COST + MORE_PSRAM, ย้าย buffer ของเราไป PSRAM
- โค้ด Live client ที่จูนกับ IDF 4.4 อาจมีพฤติกรรมต่างใน IDF 5 (TLS/transport)
- "Jarvis" กับสำเนียงไทย — ต้องลองจริง; คำปลุกไทยมีค่าใช้จ่าย
- เสียงสะท้อนแรง (-14 dB) — AEC อาจเหลือเศษจน Gemini ขัดตัวเอง → barge-in เป็นตัวเลือก
- เปลี่ยน partition = ผู้ใช้ทุกคนต้องแฟลช USB ครั้งหนึ่ง

## ทางเลือกถ้าเฟส 1 ไม่ผ่าน (ไม่ย้ายแกน)
reference จาก reg 0x44 + **SpeexDSP AEC** (BSD, C ล้วน, คอมไพล์บน 2.0.17 ได้) + คำปลุกด้วย **microWakeWord** (TFLite Micro, มีโมเดล "hey_jarvis")
→ ไม่ต้องเปลี่ยน partition (ส่งผ่าน OTA ได้) แต่คุณภาพ AEC ต่ำกว่า AFE และต้องประกอบเองหลายชิ้น
