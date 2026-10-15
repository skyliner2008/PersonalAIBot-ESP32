---
tags: [troubleshooting, debug, issues, solutions]
created: 2026-10-14
status: active
---

# 🔧 Troubleshooting Log

> **Related:** [[00-Home-Dashboard]] | [[01-Hardware-ES3C28P-Pinout]] | [[02-Installation-and-Setup]]

This page documents common issues encountered during development and their solutions. Add new entries at the top with the date.

---

## Issue Index

| # | Issue | Category | Status |
|---|-------|----------|--------|
| T027 | [[#T027 สลับโมเดลวนไปมาไม่หยุด - ทุกโมเดลติด quota]] | AI | ✅ Fixed (หยุดพัก + ไม่นับซ้ำ) |
| T028 | [[#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]] | AI | ✅ Fixed (ปิด Search อัตโนมัติ) — verified |
| T043 | [[#T043 บอร์ดรีบูต task_wdt IDLE0 ขณะเปิดวิทยุ + เรียก tool ออนไลน์]] | Tasks/CPU | ✅ Fixed (webTools/textModel → core 1) |
| T042 | [[#T042 รหัส Wi-Fi และ API key เก็บเป็นข้อความธรรมดาในแฟลช]] | Security | ✅ Fixed (AES-256-GCM ผูกกับชิป + ล้างสำเนาเก่า) |
| T041 | [[#T041 TLS ล้มเหลว -0x7F00 เมื่อเปิดหลายการเชื่อมต่อ - RAM ภายในไม่พอ]] | Network/Memory | ✅ Fixed (buffer TLS ไป PSRAM) |
| T040 | [[#T040 พอร์ต tools จาก PersonalAIBot - field TradingView และข้อมูลที่ผ่านไปแล้ว]] | Tools | ✅ Fixed |
| T039 | [[#T039 บอร์ดรีบูตเอง CORRUPT HEAP หลัง error เครือข่าย - race ใน esp_websocket_client]] | Network | ✅ Fixed (I/O task เดียวถือการเชื่อมต่อ) |
| T036 | [[#T036 ไอคอนไมค์ขึ้นแต่ AI เงียบ - 3.8 Live ไม่ค่อยตอบ]] | Audio/AI | 🔄 ปรับ VAD + AGC + log `[Mic]` + เครื่องมืออัดเสียง — รอทดสอบพูดจริง |
| T037 | [[#T037 ภาพจับหน้าจอใน app "เขินอาย" แต่บอร์ดปกติ]] | Tooling | ✅ Explained + จับภาพซ้ำหลังท่าจบ |
| T038 | [[#T038 "failed to send setup" หลังบันทึกค่าหลายค่าจาก PC app]] | Agent | ✅ Fixed (debounce 1.5 วิ) |
| T035 | [[#T035 quota ทุกโมเดลแม้ปิด Search - session ค้างบนเซิร์ฟเวอร์]] | AI | ✅ Fixed (ปิดแบบ close handshake) |
| T034 | [[#T034 "session not found or expired" ถูกตีความเป็นโมเดลหาย - 2 โมเดลโดนพัก 24 ชม.]] | AI | ✅ Fixed |
| T029 | [[#T029 จอพื้นขาว ตัวอักษรดำ - จอ IPS ต้องเปิด color inversion]] | Display | ✅ ยืนยันแล้ว: แผงนี้ต้อง `invert = 1` |
| T030 | [[#T030 เครื่องหลับทันทีหลังแตะจอหรือพิมพ์ - unsigned underflow]] | Agent | ✅ Fixed — verified |
| T031 | [[#T031 เซิร์ฟเวอร์รับ socket แต่ไม่ตอบ setup]] | Network | ✅ Fixed (timeout 15 วิ) |
| T032 | [[#T032 Wi-Fi ต่อไม่ได้ reason 2 / 39 - เราเตอร์ปฏิเสธ]] | Network | ⚠️ ฝั่งเราเตอร์ — เพิ่ม back-off + แสดงเหตุผล |
| T033 | [[#T033 แตะหน้าจอแล้วไมค์ปิดเอง]] | UI | ✅ Fixed (ปุ่มไมค์แยก) |
| T022 | [[#T022 แตะเฟืองไม่ได้ - แกนทัชไม่ตรงกับจอแนวนอน]] | Touch | ✅ Fixed (calibration 3 จุด) |
| T023 | [[#T023 Build error - macro _max ของ Arduino]] | Build | ✅ Fixed |
| T024 | [[#T024 ข้อความค้างจอไม่แสดง - UINT32_MAX กับการเทียบเวลาแบบ signed]] | UI | ✅ Fixed |
| T025 | [[#T025 Screenshot มีรอยฉีก]] | Tooling | ✅ Fixed |
| T026 | [[#T026 Gemini 3.8 Extended Thinking ปฏิเสธ BLOCKING tools]] | AI | ✅ Handled per model |
| T013 | [[#T013 I2S DIN/DOUT สลับกัน - ไมค์อ่านได้ 0 ตลอด]] | Audio | ✅ Fixed & verified on board |
| T014 | [[#T014 ES8311 ไม่ทำงาน - ลำดับ init ไม่ครบ]] | Audio | ✅ Fixed & verified on board |
| T015 | [[#T015 WebSocket หลุด code 1009 เมื่อข้อความใหญ่กว่า 15 KB]] | Network | ✅ Fixed (esp_websocket_client) |
| T016 | [[#T016 เสียง AI ช้าและทุ้ม - sample rate ไม่ตรง]] | Audio | ✅ Fixed (I2S 24 kHz + resampler) |
| T017 | [[#T017 AI ขัดจังหวะตัวเอง - เสียงลำโพงย้อนเข้าไมค์]] | Audio | ✅ Mitigated (half-duplex gate) |
| T018 | [[#T018 ฟอนต์ไทยบรรทัดสูงเกิน - ซับไตเติลล้น]] | Display | ✅ Fixed & verified on board |
| T019 | [[#T019 Gemini ไม่เรียก tool / ตอบก่อนได้ผลความจำ]] | AI | ✅ Fixed |
| T020 | [[#T020 Serial console ข้อมูลหาย]] | Tooling | ✅ Fixed |
| T021 | [[#T021 ปากยิ้มกลับหัว]] | Display | ✅ Fixed (เห็นจาก screenshot) |
| T011 | [[#T011 GeminiLiveClient ตัวแปร Config ไม่ครบและ Deprecated Warning]] | Build | ✅ Solved (v1) |
| T012 | [[#T012 LVGL Build Error - Dropdown Dependency ใน Calendar Widget]] | Build | ⚪ Obsolete — LVGL ถูกถอดออกใน v2 |
| T001 | [[#T001 PSRAM ไม่ทำงาน / ตรวจไม่พบ]] | Hardware | 📘 Reference (บอร์ดจริงพบ PSRAM 8 MB ✓) |
| T002 | [[#T002 จอมืด - ไม่มีภาพแสดงบนหน้าจอ]] | Display | 📘 Reference |
| T003 | [[#T003 เสียงไม่ออกลำโพง]] | Audio | 📘 Reference (ดู T013, T014 ด้วย) |
| T004 | [[#T004 ทัชสกรีนไม่ตอบสนอง]] | Touch | 📘 Reference (บอร์ดจริงพบ 0x38 ✓) |
| T005 | [[#T005 WebSocket SSL หลุดบ่อย]] | Network | 📘 Reference (ดู T015) |
| T006 | [[#T006 I2C Bus Conflict - ทัชกับ Audio ค้าง]] | Hardware | ✅ Implemented (`I2cBus`) |
| T007 | [[#T007 Build Error - PSRAM Related]] | Build | 📘 Reference |
| T008 | [[#T008 Audio เสียงแตก / Crackling]] | Audio | 📘 Reference (ดู T016) |
| T009 | [[#T009 Guru Meditation Error (Panic)]] | System | 📘 Reference |
| T010 | [[#T010 Wi-Fi เชื่อมต่อไม่ได้]] | Network | 📘 Reference |

> 📘 Reference = แนวทางทั่วไปที่เขียนไว้ก่อนทดสอบบนบอร์ดจริง ไม่ใช่ปัญหาที่เกิดขึ้นแล้ว

---

## T027: สลับโมเดลวนไปมาไม่หยุด - ทุกโมเดลติด quota

**วันที่:** 2026-10-14 · **รายงานโดยผู้ใช้:** ตั้ง API key แล้วจอขึ้น "สลับไปใช้ ..." วนไปมา
**Log:** ทุกโมเดลถูกปิดด้วย `1011: You exceeded your current quota, please check your plan and billing details`
**สาเหตุ (2 บั๊กใน fallback):**
1. เมื่อ**ทุกโมเดล**อยู่ใน cool-down `ModelSelector::active()` คืนตัวที่ใกล้หายก่อน แล้วเชื่อมต่อทันที → วนไม่จบ (~20 setup/นาที ยิ่งเปลืองโทเคน)
2. หลัง close frame จะมี `onDisconnected()` ตามมาและถูกนับเป็น Transient ซ้ำกับโมเดลเดิม → ครบ 3 ครั้งได้ cool-down 2 นาที **ทับ** cool-down quota 10 นาที → โมเดลที่เพิ่งล้มถูกเลือกซ้ำ ("keeps failing -> ตัวเดิม")

**แก้:** `allCoolingDown()` / `nextRecoveryIn()` → agent **หยุดพัก** (`enterHold`) จนโมเดลแรกพ้น cool-down พร้อมข้อความบนจอ
"โควตา API key หมดทุกโมเดล • ลองใหม่ HH:MM หรือแตะจอ" · cool-down ไม่ถูกย่อให้สั้นลง · ไม่นับ disconnect ที่ตามหลัง close frame ·
เปลี่ยน key = ล้าง cool-down ทั้งหมด · test `model_fallback_key_wide_quota`

## T028: "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search

**วันที่:** 2026-10-14 · ผู้ใช้: "ขึ้นติดลิมิต ทั้งที่ยังไม่ถึงลิมิต"
**วินิจฉัย (ทีละขั้น บนบอร์ดจริง):**
1. `apitest` (REST, header `x-goog-api-key`): key ยาว 53 ตัว ตรงกับที่ผู้ใช้ให้ • มองเห็นทั้ง 5 โมเดล • `gemini-3.5-flash-lite` เรียกได้ (200) → key/billing ใช้ได้
2. เงียบ >8 นาทีแล้วลองใหม่ยังติด → ไม่ใช่ TPM จากการวนลูป
3. `keymode header` (ส่ง key แบบ SDK) → ติดเหมือนเดิม
4. `setupmode nosearch` → **`gemini-3.8-live` setup complete ✓** · `setupmode min` ✓

→ ที่หมดคือโควตา **Grounding with Google Search** (แยกจากโควตาโมเดล) แต่เซิร์ฟเวอร์ปิด session ทั้งหมดด้วยข้อความ quota เดียวกัน (reason ถูกตัดที่ 123 ไบต์ จึงไม่เห็นรายละเอียด)

**แก้ (auto fallback ระดับ tool):** ถ้าได้ quota ตอนที่ setup มี `googleSearch` → ลองโมเดลเดิมใหม่**โดยไม่มี Search** ทันที (ไม่ลงโทษโมเดล)
ปิด Search 6 ชม. แล้วลองใหม่เอง · system prompt บอก AI ว่า "ตอนนี้ค้นหาเว็บไม่ได้" · ตั้งปิดถาวรได้ที่เมนู "ค้นหาเว็บ (Google Search)" / `set search 0`
ผลจริง: quota → retry without search → **session ready ใน ~2 วิ** บนโมเดลหลัก

**ยืนยันซ้ำ 2026-10-15 (`apitest` v3.1.1):** `gemini-3.5-flash-lite` ไม่มี tool = 200 แต่ใส่ `tools:[{googleSearch:{}}]` = **429 RESOURCE_EXHAUSTED**
→ โมเดลรองรับ Search grounding แต่ **โควตา grounding ของโปรเจกต์หมด** (ไม่มี QuotaFailure detail) — ตรวจหน้า rate limit / billing ใน AI Studio
· ลองเปิดใหม่ทันทีได้ด้วย `searchretry`, `set search 1` หรือปุ่ม "ลองเปิด Search ใหม่" ใน PC app (ไม่ต้องรอ 6 ชม.)

## T029: จอพื้นขาว ตัวอักษรดำ - จอ IPS ต้องเปิด color inversion

**วันที่:** 2026-10-14 · ผู้ใช้: "พื้นหลังยังเป็นสีขาว" (ภาพ screenshot จาก framebuffer เป็นพื้นดำ — ภาพบนจอจริงต่างจาก buffer)
**สาเหตุ:** แผง 2.8" IPS ที่ใช้ ILI9341V ต้องส่งคำสั่ง INVON (0x21) แต่ config ตั้ง `invert = false` → 0x0000 (ดำ) แสดงเป็นขาว
**แก้:** ตั้งค่า `lcdInvert` (NVS, ค่าเริ่มต้น **เปิด**) → `lcd.invertDisplay()` ตอนบูต · เมนู "พื้นจอเป็นสีขาว? แตะเพื่อสลับสี" · `set invert 0|1` · PC app
ถ้าบอร์ดรุ่นอื่นสีเพี้ยนกลับด้าน ให้สลับค่านี้

**อัปเดต 2026-10-15:** พบว่าบนบอร์ดค่า `invert` ถูกปิดจากเมนูภายหลัง (ผู้ใช้อาจกดเพราะชื่อเมนูเดิม "กลับสีจอ (พื้นดำ)" ชวนให้กดเพื่อให้พื้นดำ)
→ เปลี่ยนชื่อเป็น **"พื้นจอเป็นสีขาว? แตะเพื่อสลับสี"** ค่า "แบบ 1/แบบ 2" — ให้ผู้ใช้แตะจนพื้นเป็นสีดำ

**ยืนยัน 2026-10-15:** ผู้ใช้เทียบบอร์ดกับ PC app — ตอน `invert = 0` บอร์ดเป็น **พื้นขาว + ตาน้ำเงิน** ส่วนภาพ framebuffer ใน app เป็น
พื้นดำ + ตาเหลือง (amber) = สีกลับกันพอดี → แผง ES3C28P ตัวนี้ **ต้องเปิด INVON (`invert = 1`, "แบบ 1")** · ตั้งกลับเป็น 1 แล้ว
PC app มีปุ่ม **🌓 สลับสีจอบอร์ด** ในแท็บหน้าจอ ไว้แก้ทันทีถ้าภาพบอร์ดกับ app ไม่ตรงกัน

## T043: บอร์ดรีบูต task_wdt IDLE0 ขณะเปิดวิทยุ + เรียก tool ออนไลน์

**วันที่:** 2026-10-15 · log: `E task_wdt: ... - IDLE0 (CPU 0) ... CPU 0: webTools ... Aborting.` → `rst:0xc (RTC_SW_CPU_RST)`

**สาเหตุ:** Arduino-ESP32 เปิด `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` (5 วิ) แต่ไม่เฝ้า CPU1 — core 0 มี audioOut/audioIn (prio 6), radio (4), webTools (2), textModel (1), LED, Wi-Fi
ตอนวิทยุเติมบัฟเฟอร์ ตัวถอดรหัสทำงานรวดไม่รอ และ webTools ทำ TLS handshake (คณิตศาสตร์หนัก บัฟเฟอร์ TLS อยู่ PSRAM ช้าลง) + แยก JSON ใหญ่ — idle task ของ core 0 ไม่ได้รันนานเกิน 5 วิ

**แก้:** `RadioPlayer` เรียก `vTaskDelay(1)` หลังถอดรหัสทุกเฟรม และย้าย `webTools` + `textModel` ไป **core 1** (prio 2/1 ต่ำกว่า agent 4, ui 3 จึงใช้เวลาว่างของ core 1) — core 0 เหลือเสียง วิทยุ Wi-Fi
ทดสอบ: วิทยุ + search_knowledge ×2 + market_overview + get_weather ซ้อนกัน 2 นาที ไม่รีบูต · รายละเอียด [[12-Internet-Radio#ทดสอบบนบอร์ด (2026-10-15)]]

## T042: รหัส Wi-Fi และ API key เก็บเป็นข้อความธรรมดาในแฟลช

**วันที่:** 2026-10-15 · **พบจาก:** อ่านพาร์ติชัน NVS (0x9000, 20 KB) ด้วย `esptool read_flash` — สคริปต์นับเฉพาะจำนวน ไม่แสดงค่า

| ก่อนแก้ | พบ |
|---|---|
| API key แบบข้อความ (`cfg/key`) | 1 |
| รหัส Wi-Fi ที่**ไดรเวอร์ Wi-Fi เก็บเอง** (`nvs.net80211/sta.pswd`) — Arduino ตั้ง `WiFi.persistent(true)` เป็นค่าเริ่มต้น | 4 จุด |

**แก้ (v3.2):**
1. `src/app/SecretBox.*` — AES-256-GCM; กุญแจ = SHA-256(label + MAC โรงงาน + eFuse `OPTIONAL_UNIQUE_ID`) ชื่อ key ใน NVS เป็น AAD (สลับช่องกันไม่ได้), IV สุ่มทุกครั้ง
2. `Settings` เก็บเป็น `pass_e` / `key_e` / `mt5tok_e` และเขียนใหม่เฉพาะเมื่อค่าเปลี่ยน (ลดการสึกของแฟลช)
3. `WiFi.persistent(false)` ก่อนใช้ Wi-Fi ทุกครั้ง
4. ย้ายข้อมูลเดิมอัตโนมัติครั้งเดียว: self-test การเข้ารหัส → `nvs_flash_erase()` ทั้งพาร์ติชัน (การลบ key ปกติแค่ทำเครื่องหมาย ข้อความยังค้างในแฟลช) → เขียนค่าทั้งหมดกลับแบบเข้ารหัส → อ่านกลับมาเทียบ ถ้าไม่ตรงจะคืนค่าแบบเดิมเพื่อไม่ให้ข้อมูลหาย
5. `config` มี `secretsSealed`, `secretsMigratedAtBoot`; Studio แสดง 🔒 ใต้ช่อง API key

| หลังแก้ | พบ |
|---|---|
| API key แบบข้อความ | 0 |
| `sta.pswd` | 0 |
| blob ที่เข้ารหัส `pass_e` / `key_e` | มี |

บูตซ้ำ 2 รอบ: `secretsSealed=true`, ไม่ย้าย/ล้างซ้ำ, Wi-Fi ต่อได้, เซสชัน AI พร้อม, key ยาว 53 ตัวเท่าเดิม

**ต่อยอด (v3.2, ตามที่ผู้ใช้ต้องการ):** ย้ายที่เก็บไปการ์ด SD — `Settings::attachSecretStore("/sdcard")` หลัง mount การ์ด
- มีการ์ด → ความลับอยู่ใน `/sdcard/agent/secrets.json` (blob เดิมเป็น base64, AAD ชื่อเดิม) และลบ `pass_e`/`key_e`/`mt5tok_e` ออกจาก NVS; ไม่มีการ์ด → NVS
- ลำดับความใหม่: NVS ได้ความลับเฉพาะตอนไม่มีการ์ด จึงถือว่าใหม่กว่า → ย้ายขึ้นการ์ดเมื่อเสียบการ์ด
- `ai-setup.txt` ที่รากการ์ด (`src/util/SetupFile.h`, host test `sd_setup_file`) → นำเข้า → เขียน 0 ทับ → ลบ; ข้ามเฉพาะค่าตัวอย่าง `<...>`
- ไฟล์ของบอร์ดอื่น (ถอดรหัสไม่ได้) จะไม่ถูกเขียนทับจนกว่าบอร์ดนี้จะได้รับค่าใหม่ — ย้ายการ์ดกลับไปบอร์ดเดิมยังใช้ได้
- ทดสอบบนบอร์ด: `config` → `secretsStore: "sd"`, `secretsMovedFromNvs` true ครั้งแรก/false ครั้งต่อไป, Wi-Fi + AI ทำงาน, key 53 ตัวครบ

**ข้อจำกัด:** ไม่ใช่ flash encryption — คนที่มีทั้งบอร์ดและเฟิร์มแวร์นี้ยังคำนวณกุญแจได้ (MAC/eFuse อ่านได้เมื่อไม่ได้เปิด eFuse protection)
Flash encryption ของจริงต้องเผา eFuse (ย้อนกลับไม่ได้) และใช้ ESP-IDF build เอง จึงไม่ทำบนบอร์ดทดลอง

## T041: TLS ล้มเหลว -0x7F00 เมื่อเปิดหลายการเชื่อมต่อ - RAM ภายในไม่พอ

**วันที่:** 2026-10-15 · **อาการ:** กด "ทดสอบ API key" ระหว่าง AI กำลังต่อใหม่ → log `E esp-tls-mbedtls: mbedtls_ssl_setup returned -0x7F00`
(`MBEDTLS_ERR_SSL_ALLOC_FAILED`) และ `[Health] heap … (min 15 KB)` — ปุ่ม/tool ที่ใช้อินเทอร์เน็ตล้มเหลวเป็นครั้งคราว

**สาเหตุ:** SDK ที่คอมไพล์มากับ Arduino-ESP32 2.0.17 ตั้ง `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` → ทุกการเชื่อมต่อ TLS
(Gemini Live, web tools, REST probe, จัดความจำ) จอง ~40 KB จาก RAM ภายใน (มีแค่ ~130 KB) เปิดพร้อมกัน 2–3 ตัวก็หมด

**แก้:** `src/main.cpp` เรียก `mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free)` ก่อนเชื่อมต่ออะไร —
ก้อน ≥ 512 B ไป PSRAM, ก้อนเล็กอยู่ RAM ภายใน (AES/SHA hardware คัดลอกผ่าน bounce buffer ให้เองบน S3)

| | ก่อน | หลัง |
|---|---|---|
| RAM ภายในว่าง (idle) | 44–88 KB | ~127 KB |
| ต่ำสุดตอน apitest + ต่อ AI ใหม่ + 2 web tools พร้อมกัน | 15 KB + TLS ล้มเหลว | 85 KB, สำเร็จทั้งหมด |

เสียง/ความเร็ว: ถาม AI + `get_weather` ตอบปกติ (8.3 วิ เท่าเดิม)

## T040: พอร์ต tools จาก PersonalAIBot - field TradingView และข้อมูลที่ผ่านไปแล้ว

**วันที่:** 2026-10-15 · **เกี่ยวข้อง:** [[11-Data-Tools-and-Cards]]

| อาการ | สาเหตุ | แก้ |
|---|---|---|
| การ์ดวิเคราะห์หุ้นไม่มีแถว "ปันผล" | field `dividend_yield_recent` ของ scanner คืน `null` | ใช้ `dividends_yield_current` (PTT = 5.22%) |
| การ์ดวิเคราะห์ + `fundamentals` ตัดแถว P/E ทิ้ง | การ์ดมีได้ 8 แถว | ถ้าขอปัจจัยพื้นฐาน แสดงเทคนิคแค่ RSI/MACD/EMA50/EMA200 |
| สัญญาณ "กลางๆ" เป็นสีเขียว/แดง | ใช้สีตามเครื่องหมาย (score 0.06) | สีเทาเมื่อ -0.1 < score < 0.1 (เกณฑ์เดียวกับข้อความ) |
| ปฏิทินเศรษฐกิจ "สัปดาห์นี้" มีแต่ข่าวที่ประกาศไปแล้ว | feed เป็นทั้งสัปดาห์ | การ์ดแสดงเฉพาะที่ยังไม่ถึง (เผื่อ 2 ชม.), JSON ยังมีครบพร้อม `done: true` |
| ชื่อเรื่องยาวเกินการ์ด ("ทองคำ (USD/ออนซ์) • หลายกรอบเวลา") | ชื่อ alias ยาว | ย้าย "ทุกกรอบเวลา" ไปบรรทัดรอง |

ข้อสังเกต: ระหว่างทดสอบเห็น `Server close 22549 (transient): <ข้อความอ่านไม่ออก>` 1 ครั้ง หลังส่ง toolResponse — ระบบ resume
เซสชันต่อเองและตอบครบ ทดสอบซ้ำ 3 บทสนทนาไม่เกิดอีก (เฝ้าดูต่อ ถ้าเจอบ่อยให้เก็บ log เต็ม `[Gemini]`)

## T039: บอร์ดรีบูตเอง CORRUPT HEAP หลัง error เครือข่าย - race ใน esp_websocket_client

**วันที่:** 2026-10-15 · log จากผู้ใช้ (Wi-Fi "MyHome 1" −72…−74 dBm):
```
E TRANSPORT_WS: Error transport_poll_write
E WEBSOCKET_CLIENT: Network error: esp_transport_write() returned 0, errno=0      <- agent task (ส่งเสียงไมค์)
E TRANSPORT_BASE: poll_read select error 0 ... esp_transport_poll_read() returned -1  <- websocket task
CORRUPT HEAP: Bad head at 0x3fcbd800 ... assert failed: multi_heap_free  -> Rebooting
```
**สาเหตุ:** ส่งเสียงด้วย timeout 200 ms → Wi-Fi อ่อนส่งไม่ทัน (write คืน 0) → `esp_websocket_client` ปิดการเชื่อมต่อ **จาก task ที่ส่ง**
ขณะที่ task ของมันเองอยู่ใน `esp_transport_poll_read()` (นอก lock) บน TLS session เดียวกัน → session ถูกใช้หลัง free / free ซ้ำ → heap พัง
(บั๊ก race ของ IDF 4.4 — ยิ่งส่งเสียงต่อเนื่องมากยิ่งเกิดบ่อย)
**แก้:** เขียน `GeminiLiveClient` ใหม่บน `esp_transport_ws` + SSL โดยมี **task `geminiIO` ตัวเดียว** ที่ connect / เขียน / อ่าน / ping / close
- task อื่น (agent) แค่ `enqueue` ข้อความ (copy ลง PSRAM, outbox 64 ช่อง ≈ 2.5 วิ) — คิวเต็ม = ทิ้งเสียง 40 ms แทนการบล็อก
- write timeout 5 วิ; เขียนไม่ครบ = reconnect (จาก task เดียวกัน จึงไม่มีใครใช้ TLS อยู่) · back-off 1→2→…→15 วิ
- ping ทุก 15 วิ, ไม่มีข้อมูล 45 วิ = ลิงก์ตาย · TCP keep-alive · ข้อความแรกของทุกลิงก์ต้องเป็น setup (ทิ้งของค้างจากลิงก์เก่า)
- ปิดด้วย close handshake เสมอ (T035) · อ่าน close reason เอง (`propagate_control_frames`)
**ผลบนบอร์ด:** ทั้ง 3 โมเดล setup ได้, `ask` ตอบปกติ, พัก↔ตื่น 4 รอบ resume ได้ทุกรอบ quota 0, ไม่แครช

## T036: ไอคอนไมค์ขึ้นแต่ AI เงียบ - 3.8 Live ไม่ค่อยตอบ

**วันที่:** 2026-10-15 · ผู้ใช้: "Gemini 3.8 Live ไม่ค่อยตอบสนอง • 3.1 Flash Live ดีกว่าแต่ต้องพูดหลายรอบ • ไอคอนไมค์ตอบสนองแต่ AI เงียบ"
**หลักฐานจาก log:** 3.1 ได้ transcript เพี้ยน (`user: o`, `user: Sou`) แต่ยังตอบเรื่องวันที่ถูก · 3.8 ไม่มี transcript เลย 4 นาที
**วิเคราะห์:**
1. 3.8 Live เปิด **proactive audio ถาวร** — โมเดลเลือก "ไม่ตอบ" ถ้าไม่แน่ใจว่าพูดกับมัน/เสียงไม่ชัด → ไวต่อคุณภาพเสียงมากกว่า 3.1
2. Local VAD เดิมส่งเสียงเฉพาะช่วงที่เกิน threshold (min RMS 0.006, onset 2 chunk, pre-roll 320 ms, hangover 1.2 วิ)
   → ต้นคำ/คำเบาถูกตัด ประโยคไทยที่เว้นจังหวะถูกแบ่งเป็นหลาย turn (Google แนะนำ *hybrid VAD*: ให้ server หาจุดเริ่มเอง)
3. ไอคอนฟ้า = chunk เดียวเกิน threshold ก็ขึ้น แต่การส่งต้องเกิน 2 chunk ติดกัน → "ไอคอนขึ้นแต่ไม่ได้ส่ง"
4. เลือกช่อง mic L/R อัตโนมัติจากพลังงาน — ถ้าอีกช่องมีสัญญาณลำโพง (DAC loopback) จะสลับช่องผิดช่วง ~0.7 วิแรกหลัง AI พูดจบ

**แก้ (v3.1, รอยืนยันด้วยการพูดจริง):**
- VAD: min 0.004, noise ×2.5, pre-roll 600 ms, hangover 1.5 วิ · **ฟังต่อ 6 วิหลัง AI พูดจบ** (ส่งต่อเนื่องไม่รอ onset — คนมักตอบทันที/เบา)
- **AGC** ก่อนส่ง: ยกเสียงพูดเบาไปที่ RMS 0.08 (สูงสุด ×6, ลดเร็ว/เพิ่มช้า, ตัดยอดกัน clip)
- Server VAD: `startOfSpeechSensitivity HIGH`, `endOfSpeechSensitivity LOW`, `prefixPaddingMs 300`, `silenceDurationMs 900`
- เลือกช่อง mic เฉพาะตอนลำโพงเงียบ >1.5 วิ
- **log `[Mic]`**: เริ่มส่ง / จบประโยค (ส่งกี่วิ ดังแค่ไหน ขยายกี่เท่า) / เสียงสั้นไม่ถึงเกณฑ์ / **⚠ ส่งแล้วแต่ AI ไม่ได้ยิน-ไม่ตอบใน 8 วิ** (+ ข้อความบนจอ)
- **`rec <วิ>`** อัดสิ่งที่ส่งให้ AI จริง (หลัง AGC) พร้อมธง VAD ต่อ 40 ms → PC app บันทึก WAV ใน `recordings/` + เล่น + กราฟ (ฟ้า = ส่ง, ส้ม = ได้ยินแต่ไม่ส่ง)
- ทั้ง 3 โมเดลรับ `realtimeInputConfig` (ทดสอบ 2026-10-15) · ถ้าโมเดลใดปฏิเสธ (1007/not supported) จะลองใหม่โดยไม่ใส่ แทนการพักโมเดล

**วัดจริง (ห้องเงียบ ไม่มีคนพูด):** ช่อง L = R เท่ากันทุกครั้ง (ES8311 ส่ง ADC ทั้งสองช่อง, reg 0x44 = 0) → การเลือกช่องไม่ใช่สาเหตุ ·
พื้นหลัง −76 dBFS (PGA 30 dB `0x14=0x1A`, ADC scale 18 dB `0x16=3`, ADC vol +4.5 dB `0x17=0xC8`)
**ผู้ใช้ยืนยัน 2026-10-15:** "พูดเสียงปกติต้องพูดใกล้มากเกือบติดบอร์ด" → ADC scale 18 → **36 dB** (`micGain` ค่าเริ่มต้น 6, ปรับได้ 0–7
ที่เมนู "ความไวไมค์" / `set micgain` / PC app) · พื้นหลังหลังปรับ ≈ −56 dBFS (+18 dB ตามคาด) · AGC สูงสุด ×8
→ ผู้ใช้ทดลองแล้ว **ระดับ 5 (30 dB) ดีที่สุด** พูดปกติระยะ ~40 cm → ตั้งเป็นค่าเริ่มต้น · log `[Mic]` ยืนยัน: "สวัสดี" ส่งแล้วได้ transcript + คำตอบทุกครั้ง

**พบเพิ่มจาก log เดียวกัน:**
- `1008 "The operation was aborted."` (เซิร์ฟเวอร์ตัดกลางคัน) ถูกจัดเป็น NotFound → ไปปิด VAD tuning ผิดๆ ·
  แก้: 1008 ที่ไม่มีข้อความบอกชนิด = Transient · ถือว่า "ปฏิเสธ VAD tuning" เฉพาะตอนยังไม่ ready และ code 1007 / reason พูดถึง realtime/activity
- คำตอบ 3.1 Flash Live หลังเรียก tool หลายตัวพร้อมกันมี `functionreponsetextfunction` ปนใน transcript (และบางครั้งพูดซ้ำ 2 รอบ — ฝั่งโมเดล)
  → กรองคำภาษาอังกฤษที่มี "function" ออกจาก transcript ก่อนแสดง/บันทึก

## T037: ภาพจับหน้าจอใน app "เขินอาย" แต่บอร์ดปกติ

ไม่ใช่ภาพผิด: การคลิกบนภาพใน app = แตะหน้าตาบนบอร์ด → ตาทำท่าดีใจ + แก้มแดง ~1.5 วิ (ดูคล้ายเขิน) และ app จับภาพ 0.5 วิหลังแตะ
→ ได้ภาพตอนกำลังทำท่า ขณะที่บอร์ดกลับเป็นปกติแล้ว · **แก้:** จับภาพอีกครั้งที่ 2.3 วิ (หลังท่าจบ) + แสดงเวลาของภาพ
(ดวงตาขยับตลอด — ภาพนิ่งอาจจับตอนกะพริบ/มองข้าง)

## T038: "failed to send setup" หลังบันทึกค่าหลายค่าจาก PC app

PC app ส่ง `set` หลายบรรทัดติดกัน → แต่ละค่าสั่ง restart session ซ้อนกัน → Connected ของรอบก่อนถูกประมวลผลหลังรอบใหม่เริ่ม → ส่ง setup ไม่ได้ (แล้วค่อยหายเอง)
**แก้:** รอให้ค่าหยุดเปลี่ยน 1.5 วิ แล้วค่อย restart ครั้งเดียว (`_restartAt`)

## T035: quota ทุกโมเดลแม้ปิด Search - session ค้างบนเซิร์ฟเวอร์

**วันที่:** 2026-10-15 · หลังทดสอบหนัก (รีบูต/แฟลชหลายครั้ง, พัก-ตื่น) ทั้ง 3 โมเดลได้ `1011 ... exceeded your current quota` แม้ setup ไม่มี Google Search
ไม่กี่นาทีต่อมาเชื่อมต่อได้เอง → สอดคล้องกับลิมิต **session พร้อมกัน** ของ Live API (สันนิษฐาน — ข้อความ error ไม่บอกชนิดโควตา)
**สาเหตุที่ทำให้ session ค้าง:** `esp_websocket_client_stop()` ปิดแค่ TCP **ไม่ส่ง close frame** → เซิร์ฟเวอร์ถือว่า session ยังอยู่จน timeout
และเราเรียกมันทุกครั้งที่พัก (auto-sleep ทุก 3 นาทีเงียบ), สลับโมเดล, reconnect และรีบูต
**แก้:** `GeminiLiveClient::closeConnection()` ใช้ `esp_websocket_client_close()` (close handshake, timeout 1.5 วิ) แล้วค่อย stop ถ้าไม่สำเร็จ ·
`reboot` (console) และเมนู "รีสตาร์ทเครื่อง" ปิด session + flush ความจำก่อน `ESP.restart()`
**ผล:** พัก→ตื่น 5 รอบติด = resume สำเร็จ 5/5, quota error 0 · (แฟลชผ่าน USB ยังตัดแบบไม่ handshake — หลังแฟลชบ่อยๆ อาจต้องรอไม่กี่นาที; fallback จะหยุดพักแล้วลองใหม่เอง)

## T034: "session not found or expired" ถูกตีความเป็นโมเดลหาย - 2 โมเดลโดนพัก 24 ชม.

**วันที่:** 2026-10-15 · `diag` พบ `last_close: 1008 not_found: BidiGenerateContent session not found or expired` และ
`gemini-3.8-live` + `extended-thinking` มี cool-down ~24 ชม. (กำลังใช้ 3.1 Flash Live แทน)
**สาเหตุ:** ปลุกจากพักแล้วต่อด้วย resumption handle ที่หมดอายุ → เซิร์ฟเวอร์ปิด 1008 พร้อมคำว่า "not found" → `classifyText` จับเป็น NotFound (โมเดลไม่มี)
**แก้:** `Failure::SessionExpired` (ตรวจก่อนกฎ "not found") → ทิ้ง handle แล้วเริ่ม session ใหม่ทันทีกับโมเดลเดิม ไม่ลงโทษโมเดล · test ใน `model_fallback_key_wide_quota`

## T030: เครื่องหลับทันทีหลังแตะจอหรือพิมพ์ - unsigned underflow

**อาการ:** ส่งข้อความ (`ask`) แล้วเครื่อง "Sleeping" ภายใน 1 วิ
**สาเหตุ:** loop สุ่ม `now = millis()` ตอนต้นรอบ แล้ว handler ในรอบเดียวกันตั้ง `_lastActivity = millis()` (ใหม่กว่า now) →
`now - _lastActivity` เป็น uint32 ติดลบ = ค่ามหาศาล > เวลาพัก → `enterSleep()` (แตะจอขณะเชื่อมต่อก็โดน)
**แก้:** สุ่ม `now` หลัง handler ที่ประทับเวลา + `sinceMs(now, t)` คืน 0 ถ้า t ใหม่กว่า (ใช้กับ sleep, busy, listening, thinking, consolidation)

## T031: เซิร์ฟเวอร์รับ socket แต่ไม่ตอบ setup

ระหว่างติด quota บางครั้งส่ง setup ช้า (10 วิ) แล้วเงียบ จนกว่า ping timeout 60 วิ → เพิ่ม **setup timeout 15 วิ** ใน `GeminiLiveClient::poll()` → reconnect และนับเป็น transient

## T032: Wi-Fi ต่อไม่ได้ reason 2 / 39 - เราเตอร์ปฏิเสธ

**วันที่:** 2026-10-14 · หลังรีบูตหลายสิบครั้งระหว่างทดสอบ บอร์ดต่อ "MyHome 2" ไม่ได้ (เคยต่อได้ใน 1–4 วิ) ทั้งที่สแกนเห็น −64 dBm
`[WiFi] Disconnect reason 2` (AUTH_EXPIRE) แล้วเป็น `39` (TIMEOUT) — เราเตอร์ไม่ตอบ/ปฏิเสธ (band steering หรือกันเครื่องที่ต่อถี่)
**แก้ฝั่ง firmware:** ปิด auto-reconnect ทุกวินาทีของ Arduino → retry แบบ back-off 3→6→12→…→60 วิ · log reason · แสดงบนจอ
"ต่อ Wi-Fi "…" ไม่ได้: <เหตุผล> (รหัส N)" หลังล้ม 3 ครั้ง
**ผู้ใช้:** เลือกเครือข่าย 2.4 GHz โดยเฉพาะ (เช่น `MyHome_2.4G`) ในเมนู Wi-Fi หรือ PC app / รีสตาร์ทเราเตอร์

| reason | ความหมาย |
|---|---|
| 2 | AUTH_EXPIRE — เราเตอร์ปฏิเสธ |
| 15 / 204 / 202 | handshake / auth fail — รหัสผ่านผิด |
| 39 / 205 | timeout / connection fail — เราเตอร์ไม่ตอบ |
| 200 | beacon timeout — สัญญาณอ่อน |
| 201 | ไม่พบ SSID |

## T033: แตะหน้าจอแล้วไมค์ปิดเอง

แตะที่ใดก็ตามบนหน้าตา = เปิด/ปิดไมค์ ทำให้ผู้ใช้ปิดไมค์โดยไม่ตั้งใจ (log `[Agent] Mic off` ทุกครั้งที่แตะ)
**แก้:** ปุ่มไมค์แยกมุมซ้ายบน (เทา = เปิด, ฟ้า = กำลังได้ยิน, แดง+ขีด = ปิด) · แตะที่อื่น = หยุด AI พูด / ปลุก / ลองเชื่อมต่อใหม่ / ตาเด้งดีใจ

---

## T022: แตะเฟืองไม่ได้ - แกนทัชไม่ตรงกับจอแนวนอน

**วันที่:** 2026-10-14 · **รายงานโดยผู้ใช้:** แตะตรงไหนก็เป็นเปิด/ปิดไมค์ แตะรูปเฟืองไม่ติด
**สาเหตุ:** การแปลงพิกัดตั้งสมมติฐานว่า FT6336 รายงานพิกัดแนวตั้งตรงกับ panel (แบบ LovyanGFX `convertRawXY`) แต่บนบอร์ดจริงแกน
สลับ/กลับด้าน → จุดที่แตะไปตกนอกพื้นที่เฟือง (การทดสอบด้วย `ui tap` ใช้พิกัดจอโดยตรงจึงไม่เจอปัญหานี้)
**แก้:**
1. หน้า **Calibration 3 จุด** (ซ้ายบน → ขวาบน → ซ้ายล่าง) ขึ้นเองตอนบูตครั้งแรก → คำนวณ `x = raw[a]·s + o`, `y = raw[b]·s + o` เก็บ NVS
2. **กดปุ่ม BOOT ค้าง 1.5 วิ = เปิดเมนูตั้งค่า** (เข้าได้เสมอแม้ทัชเพี้ยน) → "ปรับการแตะหน้าจอ"
3. log `[Touch] raw x,y -> screen x,y` ทุกครั้งที่แตะ ช่วยวินิจฉัย

**ผลวัดจริง (ผู้ใช้แตะ 3 จุด):** `x = 324.9 − 1.021·raw_y`, `y = 1.083·raw_x − 15.6`
→ ทัชถูกติดตั้ง **หมุน 180° เทียบกับ LCD** · แก้ค่าเริ่มต้นใน `UiManager::mapTouch()` เป็น `rotation ^ 2`
(บอร์ดที่ยังไม่ calibrate ก็แตะได้ถูกตำแหน่ง; calibration ยังใช้ชดเชยความคลาดเคลื่อนของแต่ละบอร์ด)

## T023: Build error - macro `_max` ของ Arduino

`error: macro "_max" requires 2 arguments` — Arduino-ESP32 นิยาม `_max(a,b)` เป็น macro จึงชนกับชื่อตัวแปรสมาชิก `_max` → เปลี่ยนเป็น `_maxItems`

## T024: ข้อความค้างจอไม่แสดง - `UINT32_MAX` กับการเทียบเวลาแบบ signed

`setSystemText(text, 0)` ตั้งเวลาหมดเป็น `UINT32_MAX` แต่ตรวจด้วย `(int32_t)(until - now) > 0` ซึ่งกลายเป็นค่าลบ → ข้อความไม่ขึ้น
แก้: ใช้ `now + 0x7FFFFFF0` (~24 วัน) แทน "ตลอดไป"

## T025: Screenshot มีรอยฉีก

คำสั่ง `screenshot` อ่าน sprite จาก loopTask ขณะ UI task กำลังวาดเฟรมถัดไป → ภาพเป็นแถบสลับเฟรม (เห็นตอนทดสอบ wink/shy)
แก้: ส่งคำสั่งผ่านคิวให้ UI task ถ่ายหลังเฟรมเสร็จพอดี

## T026: Gemini 3.8 Extended Thinking ปฏิเสธ BLOCKING tools

ตามเอกสาร `gemini-3.8-live-extended-thinking` รองรับเฉพาะ `NON_BLOCKING` (BLOCKING = hard error) และไม่รองรับ scheduling;
`gemini-3.1-flash-live-preview` ไม่รองรับ async เลย → `AgentTools::addDeclarations()` ใส่ `behavior` ตามโมเดลที่ใช้จริง (รวมกรณี auto-fallback)

---

## T013: I2S DIN/DOUT สลับกัน - ไมค์อ่านได้ 0 ตลอด

**วันที่:** 2026-10-14 · **พบจาก:** ทดสอบบนบอร์ดจริงผ่าน Serial console
**อาการ:** I2S RX ทำงาน (นับ block ได้ ~50/วินาที) แต่ค่า RMS ทั้งช่อง L และ R = `0.00000` เสมอ
**สาเหตุ:** ตารางของผู้ผลิตตั้งชื่อขาจากฝั่ง **codec** — `pins_config.h` เดิมตั้ง `I2S_DOUT=6`, `I2S_DIN=8` ซึ่งกลับกัน
ESP32 จึงส่งเสียงเข้าขา output ของ codec (ลำโพงเงียบ) และอ่านจากขาที่ไม่มีใครขับ (ไมค์ = 0)
**พิสูจน์:**
```text
> pins 8 6          → raw RMS L 0.00034 R 0.00034   (มี noise ของไมค์จริง)
> reg 44 60         → ES8311 ส่งข้อมูล DAC กลับออก ADC (loopback)
> tone 1000 2000    → raw RMS 0.12946  (ทฤษฎี 6000/32768/√2 = 0.1295 ✓)
```
**แก้:** `include/pins_config.h` → `I2S_DOUT 8` (MCU→codec), `I2S_DIN 6` (codec→MCU) และแก้ [[01-Hardware-ES3C28P-Pinout]]

---

## T014: ES8311 ไม่ทำงาน - ลำดับ init ไม่ครบ

**วันที่:** 2026-10-14
**สาเหตุ (โค้ด v1):** หลัง reset ปล่อย `REG00 = 0x00` (state machine ปิด), `REG01 = 0x30` (เปิดแค่ MCLK/BCLK ไม่เปิด clock ของ ADC/DAC),
ไม่ตั้ง `0x0D/0x0E` (analog power), `0x13` (output), `0x14` (เลือกไมค์ + PGA) และตั้งชื่อ `0x0F/0x10` ผิดเป็น ADC
**แก้:** `src/audio/ES8311.cpp` ใช้ลำดับของ Espressif: `00←1F → 00 → 80`, `01←3F`, clock coeff สำหรับ 256·fs, `09/0A←0C`,
`0D←01, 0E←02, 12←00, 13←10, 1C←6A, 37←08`, `17←C8, 14←1A`, `16←gain`, `32←volume`
**ตรวจ:** console `regs` แสดงค่าตรงตามนี้, chip id `FD:83`

---

## T015: WebSocket หลุด code 1009 เมื่อข้อความใหญ่กว่า 15 KB

**สาเหตุ:** `links2004/WebSockets` กำหนด `WEBSOCKETS_MAX_DATA_SIZE = 15*1024` แบบไม่มี `#ifndef` และเรียก `clientDisconnect(1009)` เมื่อ frame ใหญ่กว่า
ข้อความเสียงจาก Gemini (Base64) ใหญ่ได้หลายสิบ KB · นอกจากนี้ `beginSSL()` ที่ไม่ใส่ CA จะ `setInsecure()`
**แก้:** เปลี่ยนเป็น ESP-IDF `esp_websocket_client` + ประกอบข้อความเองใน PSRAM + ตรวจ TLS กับ Google roots — ดู [[05-Gemini-Live-API-Protocol]]

---

## T016: เสียง AI ช้าและทุ้ม - sample rate ไม่ตรง

**สาเหตุ:** I2S ตั้ง 16 kHz ทั้ง TX/RX (codec ใช้ LRCK ร่วม) แต่ Gemini ส่ง 24 kHz และไม่เคยเรียก `setPlaybackSampleRate()`
→ เสียงเล่นช้าลงเหลือ 2/3 และคิวเต็มจนเสียงหาย
**แก้:** I2S 24 kHz ทั้งสองทาง; ไมค์ผ่าน `Downsampler24to16` (polyphase FIR, ทดสอบ: DC gain 1, 1 kHz ±3%, 10 kHz ถูกลด > 34 dB); คิวเล่น 1 MB

---

## T017: AI ขัดจังหวะตัวเอง - เสียงลำโพงย้อนเข้าไมค์

**สาเหตุ:** ไมค์ส่งเสียงตลอดเวลา ไม่มี AEC → server ได้ยินเสียง AI เองแล้วส่ง `interrupted`
**แก้:** `AGENT_HALF_DUPLEX` — ไม่ส่งไมค์ระหว่างลำโพงเล่น + 350 ms, ส่ง `audioStreamEnd` ตอนปิด gate, หยุด AI ด้วยการแตะจอ/ปุ่ม BOOT
(ถ้าอนาคตใส่ AEC เช่น ESP-SR ค่อยเปิด barge-in ด้วยเสียง)

---

## T018: ฟอนต์ไทยบรรทัดสูงเกิน - ซับไตเติลล้น

**อาการ:** log `[AvatarUI] Ready (Thai font OK, line 34 px)` → 3 บรรทัด = 102 px > กรอบ 94 px
**สาเหตุ:** LovyanGFX ใช้ max(ascent/descent ใน header, ขอบ glyph) — ค่า TTF ของ Sarabun เผื่อพื้นที่มาก
**แก้:** `gen_thai_font.py` เขียน ascent/descent จากขอบ glyph จริง (22/6 → 28 px) + คำนวณจำนวนบรรทัดตามความสูงจริง

---

## T019: Gemini ไม่เรียก tool / ตอบก่อนได้ผลความจำ

**สาเหตุ:** v1 จัดการ `control_led` ใน `onToolCall` แต่ไม่เคยประกาศ `tools` ใน setup; และ `gemini-3.8-live` ใช้ NON_BLOCKING เป็นค่า default
**แก้:** ประกาศ 8 tools ทุกครั้งพร้อม `"behavior": "BLOCKING"`, ตอบ `toolResponse` ทุก call (รวมที่ไม่รู้จัก), ตรวจ args ก่อนใช้ (v1 `strcmp(NULL)` จะ crash)

---

## T020: Serial console ข้อมูลหาย

**อาการ:** `screenshot` ได้ 12 KB จาก 150 KB; คำสั่ง `tool` ภาษาไทยยาวๆ ถูกตัด
**สาเหตุ:** USB CDC (HWCDC) ทิ้งข้อมูลเมื่อ buffer ส่งเต็ม; buffer รับ default 256 B
**แก้:** ส่งทีละ 256 B + `Serial.flush()`; `Serial.setRxBufferSize(2048)` ก่อน `Serial.begin()`

---

## T021: ปากยิ้มกลับหัว

**อาการ:** จาก screenshot หน้าจอ ERROR ปากเป็น ∪ (ยิ้ม)
**สาเหตุ:** แกน y ของจอชี้ลง สูตร `y = i²/(2w)` ใน v1 ให้รูป ∩ (หน้าบึ้ง) แต่ถูกใช้เป็น "ยิ้ม"
**แก้:** ยิ้ม `y = (w² − i²)/(2w)`, บึ้ง `y = i²/(2w)`

---

## T011: GeminiLiveClient ตัวแปร Config ไม่ครบและ Deprecated Warning

**วันที่:** 2026-10-14  
**อาการ:** คอมไพล์ `src/ai/GeminiLiveClient.cpp` ไม่ผ่าน ฟ้อง error:
`'GEMINI_WS_PATH' was not declared in this scope`, `'GEMINI_HOST' was not declared in this scope` พร้อมทั้งคำเตือน deprecation จาก ArduinoJson v7 (`containsKey` is deprecated)

**สาเหตุ:**
1. `GeminiLiveClient.h` ไม่ได้รวมไฟล์ `secrets.h` หรือไม่มีค่า default fallback สำหรับโมเดลและ WebSocket URL
2. ไลบรารี `bblanchon/ArduinoJson` v7 แนะนำให้ใช้ `!doc["key"].isNull()` แทนฟังก์ชัน `doc.containsKey("key")`

**วิธีแก้ไข:**
1. เพิ่มการ include แบบปลอดภัยและประกาศค่า default ใน [`src/ai/GeminiLiveClient.h`](../src/ai/GeminiLiveClient.h):
```cpp
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef GEMINI_MODEL
#define GEMINI_MODEL    "models/gemini-3.8-live"
#endif

#ifndef GEMINI_HOST
#define GEMINI_HOST     "generativelanguage.googleapis.com"
#endif

#ifndef GEMINI_PORT
#define GEMINI_PORT     443
#endif

#ifndef GEMINI_WS_PATH
#define GEMINI_WS_PATH  "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
#endif
```
2. แก้ไขใน [`src/ai/GeminiLiveClient.cpp`](../src/ai/GeminiLiveClient.cpp) เปลี่ยนจากการเรียก `containsKey(...)` เป็น `!doc["key"].isNull()` หรือ `!serverContent["key"].isNull()`

**เอกสารที่เกี่ยวข้อง:** [[03-Libraries-and-Dependencies]] | [[05-Gemini-Live-API-Protocol]]

---

## T012: LVGL Build Error - Dropdown Dependency ใน Calendar Widget

**วันที่:** 2026-10-14  
**อาการ:** คอมไพล์ LVGL ไม่ผ่าน ฟ้อง error:
`lv_calendar_header_dropdown.c:94:38: error: 'lv_dropdown_class' undeclared`

**สาเหตุ:**
ใน `include/lv_conf.h` มีการปิดใช้งาน Dropdown (`#define LV_USE_DROPDOWN 0`) แต่คอมโพเนนต์ Calendar Header Dropdown ของ LVGL ถูก compile เข้ามาด้วยและต้องการ class นี้ นอกจากนี้ LVGL ยังต้องการ path include `-I include` เพื่อค้นหา `lv_conf.h` อย่างถูกต้อง

**วิธีแก้ไข:**
1. ใน [`platformio.ini`](../platformio.ini) เพิ่ม `-I include` ใน `build_flags`:
```ini
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DLV_CONF_INCLUDE_SIMPLE=1
    -DLV_TICK_CUSTOM=1
    -I include
```
2. ใน [`include/lv_conf.h`](../include/lv_conf.h) เปิดใช้งาน Dropdown และปิดการคอมไพล์ Calendar ที่ไม่ได้ใช้งาน:
```c
#define LV_USE_DROPDOWN         1
#define LV_USE_CALENDAR         0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_CALENDAR_HEADER_ARROW    0
```

**เอกสารที่เกี่ยวข้อง:** [[03-Libraries-and-Dependencies]] | [[06-Avatar-and-Thai-UI]]

---

## T001: PSRAM ไม่ทำงาน / ตรวจไม่พบ

**อาการ:** `ESP.getPsramSize()` คืนค่า 0 หรือ `psramFound()` คืนค่า `false`

**สาเหตุที่เป็นไปได้:**
1. ไม่ได้ตั้ง build flag `-DBOARD_HAS_PSRAM`
2. ตั้งค่า `board_build.arduino.memory_type` ผิด (ต้องเป็น `qio_opi` สำหรับ N16R8)
3. PSRAM เป็นแบบ OPI ต้องใช้โหมด OPI ไม่ใช่ QPI

**วิธีแก้ไข:**
```ini
# platformio.ini
build_flags = -DBOARD_HAS_PSRAM
board_build.arduino.memory_type = qio_opi
```

ตรวจสอบ:
```cpp
void setup() {
    Serial.begin(115200);
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
}
```

**ผลลัพธ์ที่ถูกต้อง:** PSRAM size: 8,388,608 bytes

**เอกสารที่เกี่ยวข้อง:** [[01-Hardware-ES3C28P-Pinout]] | [[03-Libraries-and-Dependencies]]

---

## T002: จอมืด - ไม่มีภาพแสดงบนหน้าจอ

**อาการ:** โปรแกรมรันปกติ (Serial Monitor แสดงข้อมูล) แต่หน้าจอดำมืดไม่แสดงผล

**สาเหตุที่เป็นไปได้:**
1. **ไม่ได้เปิด Backlight (GPIO 45)** — สาเหตุพบบ่อยที่สุด!
2. SPI pin configuration ผิด
3. ไม่ได้ตั้ง DC pin (GPIO 46)
4. ILI9341 initialization sequence ไม่ถูกต้อง

**วิธีแก้ไข:**
```cpp
// ต้องเปิด Backlight ด้วยตนเอง!
void setup() {
    // วิธีที่ 1: Direct GPIO
    pinMode(45, OUTPUT);
    digitalWrite(45, HIGH);  // Backlight ON

    // วิธีที่ 2: PWM สำหรับปรับความสว่าง
    ledcSetup(0, 5000, 8);     // Channel 0, 5kHz, 8-bit
    ledcAttachPin(45, 0);
    ledcWrite(0, 200);          // ค่า 0-255 (200 = ~78%)
}
```

> [!WARNING] ข้อควรระวัง  
> GPIO 45 ของ ES3C28P เป็น Active HIGH และค่าเริ่มต้นเป็น LOW (ปิด)  
> ดู [[01-Hardware-ES3C28P-Pinout#📺 LCD Display — ILI9341V]] สำหรับรายละเอียด

**เอกสารที่เกี่ยวข้อง:** [[01-Hardware-ES3C28P-Pinout]]

---

## T003: เสียงไม่ออกลำโพง

**อาการ:** I2S ทำงานปกติ, ES8311 ตอบรับ I2C แต่ไม่มีเสียงออกลำโพง

**สาเหตุที่เป็นไปได้:**
1. **ไม่ได้เปิดแอมป์ลำโพง FM8002E (GPIO 1)** — สาเหตุพบบ่อยที่สุด!
2. PA_ENABLE เป็น Active LOW แต่ตั้ง HIGH ไว้
3. ES8311 DAC output muted
4. I2S pin configuration ผิด

**วิธีแก้ไข:**
```cpp
void enableSpeaker() {
    // FM8002E Amplifier Enable: Active LOW!
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);    // LOW = เปิดลำโพง
}

void disableSpeaker() {
    digitalWrite(1, HIGH);   // HIGH = ปิดลำโพง
}
```

**ตรวจสอบ I2S:**
```cpp
// ตรวจสอบว่า I2S ส่งข้อมูลออกถูกต้อง
size_t bytes_written;
int16_t test_tone[256];
for (int i = 0; i < 256; i++) {
    test_tone[i] = (int16_t)(sin(2.0 * PI * i / 256.0) * 16000);
}
i2s_write(I2S_NUM_0, test_tone, sizeof(test_tone), &bytes_written, portMAX_DELAY);
Serial.printf("I2S wrote %d bytes\n", bytes_written);
```

> [!CAUTION] Critical  
> GPIO 1 เป็น Active LOW! ตรงข้ามกับสัญชาตญาณ  
> `LOW = ON` (เปิดลำโพง), `HIGH = OFF` (ปิดลำโพง)  
> ดู [[01-Hardware-ES3C28P-Pinout#🔊 Audio System — ES8311 Codec + FM8002E Amplifier]]

**เอกสารที่เกี่ยวข้อง:** [[01-Hardware-ES3C28P-Pinout]] | [[04-System-Architecture]]

---

## T004: ทัชสกรีนไม่ตอบสนอง

**อาการ:** FT6336G ไม่ตอบรับบน I2C scan หรือ touch event ไม่ทำงาน

**วิธีแก้ไข:**
1. Reset touch controller:
   ```cpp
   pinMode(18, OUTPUT);
   digitalWrite(18, LOW);
   delay(10);
   digitalWrite(18, HIGH);
   delay(300);  // Wait for FT6336G boot
   ```
2. ตรวจ I2C address: ต้องเป็น `0x38`
3. ตรวจ I2C bus: SDA=16, SCL=15
4. ตรวจ Interrupt pin: GPIO 17 (Active LOW)

**I2C Scanner:**
```cpp
#include <Wire.h>
void scanI2C() {
    Wire.begin(16, 15);
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("Found device at 0x%02X\n", addr);
        }
    }
}
// Expected: 0x18 (ES8311), 0x38 (FT6336G)
```

**เอกสารที่เกี่ยวข้อง:** [[01-Hardware-ES3C28P-Pinout]] | [[04-System-Architecture]]

---

## T005: WebSocket SSL หลุดบ่อย

**อาการ:** WebSocket เชื่อมต่อได้แต่หลุดหลังจากไม่กี่นาที หรือ SSL handshake ล้มเหลว

**สาเหตุที่เป็นไปได้:**
1. Heap memory ไม่พอสำหรับ SSL buffer (~40 KB per connection)
2. Wi-Fi signal อ่อน
3. ไม่ได้ตั้ง ping/pong keep-alive
4. Certificate ไม่ถูกต้องหรือหมดอายุ
5. Gemini Live API session timeout (15 นาที)

**วิธีแก้ไข:**
```cpp
// 1. ใช้ PSRAM สำหรับ SSL buffers
WiFiClientSecure client;
client.setBufferSizes(4096, 4096);  // Reduce from default 16384

// 2. ตั้ง Keep-alive
webSocket.enableHeartbeat(15000, 3000, 2);  // ping every 15s, timeout 3s, 2 retries

// 3. Auto-reconnect with backoff
void onDisconnect() {
    static int backoff = 1000;
    delay(backoff);
    backoff = min(backoff * 2, 30000);  // Max 30s
    connectWebSocket();
}

// 4. Monitor free heap
Serial.printf("Free heap: %d, Min heap: %d\n",
    ESP.getFreeHeap(), ESP.getMinFreeHeap());
```

> [!TIP] Heap Watch  
> If free heap drops below ~50 KB, SSL connections will start failing.  
> Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` to move large allocations to PSRAM.

**เอกสารที่เกี่ยวข้อง:** [[05-Gemini-Live-API-Protocol]] | [[04-System-Architecture]]

---

## T006: I2C Bus Conflict - ทัชกับ Audio ค้าง

**อาการ:** ระบบค้างเมื่อเข้าถึง FT6336G และ ES8311 พร้อมกัน, I2C timeout

**สาเหตุ:** ทั้ง FT6336G (0x38) และ ES8311 (0x18) ใช้ I2C bus เดียวกัน (SDA=16, SCL=15) แต่อยู่คนละ FreeRTOS task

**วิธีแก้ไข:** ใช้ Mutex ป้องกัน concurrent access
```cpp
SemaphoreHandle_t i2cMutex = xSemaphoreCreateMutex();

// ในทุก I2C transaction:
if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
    Wire.beginTransmission(addr);
    // ... I2C operations ...
    Wire.endTransmission();
    xSemaphoreGive(i2cMutex);
} else {
    Serial.println("I2C mutex timeout!");
}
```

**เอกสารที่เกี่ยวข้อง:** [[01-Hardware-ES3C28P-Pinout]] | [[04-System-Architecture]]

---

## T007: Build Error - PSRAM Related

**อาการ:** Build error เกี่ยวกับ PSRAM allocation หรือ `ps_malloc` not found

**วิธีแก้ไข:**
```ini
# platformio.ini ต้องมีค่าเหล่านี้:
build_flags = -DBOARD_HAS_PSRAM
board_build.arduino.memory_type = qio_opi
```

ใช้ `heap_caps_malloc` แทน `ps_malloc`:
```cpp
// แบบเก่า (อาจมีปัญหา):
void* buf = ps_malloc(size);

// แบบใหม่ (แนะนำ):
void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (buf == NULL) {
    Serial.println("PSRAM allocation failed!");
}
```

**เอกสารที่เกี่ยวข้อง:** [[03-Libraries-and-Dependencies]] | [[04-System-Architecture]]

---

## T008: Audio เสียงแตก / Crackling

**อาการ:** เสียงจากลำโพงแตกๆ, มีเสียง click/pop, หรือเสียงตัดเป็นช่วงๆ

**สาเหตุที่เป็นไปได้:**
1. I2S DMA buffer เล็กเกินไป
2. Ring buffer underflow (อ่านเร็วกว่าเขียน)
3. Sample rate mismatch (Gemini ส่ง 24kHz แต่ I2S ตั้ง 16kHz)
4. FreeRTOS task priority ไม่เหมาะสม

**วิธีแก้ไข:**
```cpp
// 1. เพิ่ม DMA buffer size
i2s_config_t i2s_config = {
    .dma_buf_count = 8,      // จาก 4 เป็น 8
    .dma_buf_len = 1024,     // จาก 256 เป็น 1024
};

// 2. ตรวจ buffer level ก่อน play
if (ringBuffer.count < MIN_PLAY_THRESHOLD) {
    // Buffer too low, wait for more data
    memset(silence_buf, 0, buf_size);  // Play silence
}

// 3. ตั้ง sample rate ให้ตรงกับ Gemini output
i2s_set_sample_rates(I2S_NUM_0, 24000);  // Gemini sends 24kHz

// 4. Audio task ต้องมี priority สูงสุด
xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 5, NULL, 0);
```

**เอกสารที่เกี่ยวข้อง:** [[04-System-Architecture]] | [[05-Gemini-Live-API-Protocol]]

---

## T009: Guru Meditation Error (Panic)

**อาการ:** ESP32 restart พร้อม error "Guru Meditation Error: Core X panic'ed (LoadProhibited)" หรือ stack overflow

**วิธีแก้ไข:**
1. เปิด Exception Decoder:
   ```ini
   monitor_filters = esp32_exception_decoder
   ```
2. เพิ่ม stack size ของ FreeRTOS task:
   ```cpp
   // เพิ่มจาก 4096 เป็น 8192 หรือมากกว่า
   xTaskCreatePinnedToCore(myTask, "task", 8192, NULL, 1, NULL, 0);
   ```
3. ตรวจ stack high watermark:
   ```cpp
   UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
   Serial.printf("Stack remaining: %d bytes\n", stackLeft * 4);
   ```
4. ใช้ `heap_caps_check_integrity_all(true)` เพื่อตรวจ heap corruption

**เอกสารที่เกี่ยวข้อง:** [[04-System-Architecture]]

---

## T010: Wi-Fi เชื่อมต่อไม่ได้

**อาการ:** Wi-Fi connection loop, ไม่ได้ IP address, หรือ disconnect ตลอด

**วิธีแก้ไข:**
1. ตรวจ credentials ใน `secrets.h`
2. ใช้ 2.4 GHz network เท่านั้น (ESP32-S3 ไม่รองรับ 5 GHz)
3. ตรวจ signal strength:
   ```cpp
   Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
   // < -70 dBm = signal อ่อน
   ```
4. ใช้ static IP ถ้า DHCP มีปัญหา:
   ```cpp
   IPAddress ip(192, 168, 1, 100);
   IPAddress gateway(192, 168, 1, 1);
   IPAddress subnet(255, 255, 255, 0);
   IPAddress dns(8, 8, 8, 8);
   WiFi.config(ip, gateway, subnet, dns);
   ```
5. ใน `platformio.ini` ถ้าใช้ Native USB:
   ```ini
   build_flags = -DARDUINO_USB_CDC_ON_BOOT=1
   ```

**เอกสารที่เกี่ยวข้อง:** [[02-Installation-and-Setup]] | [[05-Gemini-Live-API-Protocol]]

---

## Template สำหรับบันทึกปัญหาใหม่

```markdown
## TXXX: [ชื่อปัญหา]

**วันที่:** YYYY-MM-DD  
**อาการ:** [อธิบายอาการที่พบ]

**สาเหตุที่เป็นไปได้:**
1. ...

**วิธีแก้ไข:**
```code
// solution code
```

**เอกสารที่เกี่ยวข้อง:** [[related-doc]]
```

---

*Back to [[00-Home-Dashboard]]*
