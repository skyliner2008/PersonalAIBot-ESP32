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
| T085 | [[#T085 ปัญหาสิทธิ์ "แอปถูกปฏิเสธไม่ให้เข้าถึง" (Restricted Settings) บน Android 13+]] | Android/Permissions | ✅ แก้ UI ให้คำแนะนำ + ปุ่มข้อมูลแอปเด่นชัด ไม่หลุดขอบจอ (v4.3.2) |
| T084 | [[#T084 ออก release แล้วขึ้นเตือนเรื่องกุญแจ — ไม่มีแอปมือถือใน v4.3.0, "-app.bin is NOT signed"]] | Release/Build | ✅ แก้ให้ใช้ debug key เมื่อไม่มี keystore (v4.3.1) |
| T083 | [[#T083 เชื่อมมือถือ ↔ บอร์ด — GPS + นำทาง Google Maps + AI พูดเตือนก่อนเลี้ยว]] | Phone/Nav/Vehicle | 🔄 host tests + unit tests + ข้อความข้ามสองฝั่ง + build ผ่าน; ยังไม่ได้ลองกับบอร์ด/มือถือ/รถจริง |
| T082 | [[#T082 แอปมือถือล้นจอบน Android 15 — แถบแท็บอยู่ใต้ปุ่มนำทาง กดไม่ได้]] | Android/UI | ✅ ผู้ใช้ยืนยันบนมือถือจริง: แถบแท็บกดได้, log สดใช้งานได้ |
| T081 | [[#T081 แอปมือถือ — สถานะ, log, ตั้งค่า, แชท แบบ ESP32-AI Studio]] | Android/Console | ✅ ผู้ใช้ทดสอบกับบอร์ดจริง: เชื่อมต่อได้ พิมพ์คุยได้ (2026-10-20) |
| T080 | [[#T080 แอปมือถือ Android แฟลชเฟิร์มแวร์ผ่านสาย USB (OTG)]] | Tools/Flash/Android | ✅ แฟลช "อัปเดต" สำเร็จบนบอร์ดจริงจากมือถือ Android (MD5 ตรง); โหมด factory ยังไม่ได้ลอง |
| T079 | [[#T079 เปิดหน้าปัดรถไม่เป็น — ไม่มีปุ่ม และสั่ง AI แล้วตอบว่า "ไม่รู้จัก"]] | Vehicle/UI/Tools | ✅ Fixed — ปุ่มลัด + tool ทุก session, ผู้ใช้ลองแตะ/พูดแล้ว |
| T078 | [[#T078 หน้าปัดรถทำให้ Gemini ต่อไม่ได้ — "esp-aes: Failed to allocate memory" เพราะโค้ด BLE กิน IRAM 22 KB]] | Vehicle/Memory/TLS | ✅ Fixed — BLE เป็น build flag, ทดสอบบนบอร์ด |
| T077 | [[#T077 หน้าปัดรถ 3 รูปแบบ — วงกลมดิจิทัล สปอร์ต, เข็มวงกลม คลาสสิก, รถแข่ง]] | Vehicle/UI | ✅ host tests + build + ภาพ preview ทุกแบบ — รอดูบนบอร์ด |
| T076 | [[#T076 หน้าปัดรถ — ฟีเจอร์ใหม่ เขียนเองทั้งหมด (MIT) จากแนวคิดของ redline-gauge]] | Vehicle/UI/BLE | 🔄 host tests + build + ภาพ preview ผ่าน — **รอทดสอบบนบอร์ด/กับรถจริง** |
| T075 | [[#T075 สร้างกุญแจเซ็นอัปเดตจาก ESP32-AI Studio ด้วยปุ่มเดียว]] | Studio/OTA | ✅ Done — ทดสอบทุกปุ่มบน Xvfb (Linux); รอลองบน Windows |
| T074 | [[#T074 ปิดงานค้างจากรีวิว — ลายเซ็น OTA (ECDSA P-256), ย้าย I2S ไป i2s_std, redirect แบบ relative ของวิทยุ]] | OTA/Audio/Radio | 🔄 build + host tests ผ่าน — **รอทดสอบบนบอร์ด** (เสียง/ไมค์/AEC หลังย้าย I2S) |
| T073 | [[#T073 รีวิวรอบ 4 — icy-metaint ติดลบจากสถานีวิทยุเขียนเกิน buffer, ลบโน้ตด้วยคำค้นลบหลายอัน, ไฟล์โน้ตเสียถูกเขียนทับ]] | Radio/Tools/Storage | ✅ Fixed — host tests; รอทดสอบบนบอร์ด · งานค้างทำต่อใน T074 |
| T072 | [[#T072 get_weather ช้า 8–40 วิ — TLS ถูกแย่ง CPU (priority 2), จับมือซ้ำ, geocode ชื่อไทยพลาด]] | Tools/Network | ✅ Fixed — จังหวัด 2.8 วิ (เดิม 8–23 วิ) |
| T071 | [[#T071 ถามอากาศแล้ว AI ตอบข้อความเดิม 2 รอบ (3.8 Live) — scheduling SILENT อยู่ผิดที่ใน functionResponse]] | AI/Tools | ✅ Fixed — ทดสอบบนบอร์ด: ตอบครั้งเดียว 3/3 รอบ |
| T070 | [[#T070 สถานี AAC ทำให้บอร์ดรีบูต (task watchdog) — Helix AAC วนไม่รู้จบใน DecodeSectionData + ตรวจ/เพิ่มสถานีจาก Thailand.json]] | Radio | ✅ Fixed — ทดสอบบนบอร์ด: สถานีเดิมเล่นต่อเนื่อง ไม่รีบูต |
| T069 | [[#T069 รีวิวรอบ 3 — โหมดคำปลุกทำให้ไม่ฟังต่อหลัง AI ตอบ + "AI ไม่ตอบ" หลอก, frame สงวนล้น stack, อุปกรณ์ชื่อกำกวม]] | Audio/Wake/Devices | ✅ Fixed — ทดสอบบนบอร์ด: ฟังต่อ + แคชอากาศผ่าน (AI ตอบซ้ำหลัง web tool → แก้ใน T071) |
| T068 | [[#T068 แยกความดังเสียง AI กับวิทยุ — AI ล็อก 80%, ปรับเสียง = วิทยุอย่างเดียว]] | Audio | ✅ Done |
| T067 | [[#T067 เฟส 3A — พูดแทรก AI ด้วยคำปลุก (และกัน AI ขัดจังหวะตัวเอง)]] | Wake/Audio | ✅ ผู้ใช้ทดสอบพูดแทรกได้, AI ไม่ขัดตัวเอง |
| T066 | [[#T066 ใช้ 2 คำพร้อมกัน + เรียกคำปลุกแล้ว AI "พักหน้าจอ" ซ้ำทุกครั้ง]] | Wake/AI | ✅ Fixed (ตื่น = session ใหม่, agent priority 5, ปิดวิทยุปิดหน้าจอ) + ตัวเลือก 2 คำ |
| T065 | [[#T065 ปลุกด้วย "Hi ESP" ได้ แต่ลูปค้าง 10 วิ และ 45 วิ หลังตื่น]] | Serial/Tasks | ✅ Fixed (TX timeout 20 ms + wake tasks priority 3) — ผู้ใช้ยืนยัน |
| T064 | [[#T064 คำปลุกไม่ติดกับเสียงจริง — "Jarvis" เป็นโมเดล TTS, ค่าความไว, AEC ของ AFE ช้าหลังเปิดวิทยุ]] | Wake | ✅ "Hi ESP" + 0.5 + AEC ของเรา — วิทยุนานๆ ไม่ตื่นเอง |
| T063 | [[#T063 โหมด "คำปลุก" เรียก "จาวิส" (ใกล้มาก) ไม่มีอะไรเกิดขึ้น]] | Wake | 🔄 แก้ไมค์โหมดนาฬิกา + self-test ผ่าน — รอผู้ใช้ทดสอบเสียงจริง |
| T062 | [[#T062 เรียก "จาวิส" แล้ว AI มั่ว — ค้นหาคำว่า "jawit" / เปิดวิทยุเอง + ลูปค้าง 1.3 วิ]] | Wake/AI | ✅ Fixed (prompt + feed ไม่บล็อก) |
| T061 | [[#T061 แฟลชเฟิร์มแวร์คำปลุกแล้วจอดับ ไฟเขียวติด — RAM ภายในหมด]] | Memory/Build | ✅ Fixed |
| T060 | [[#T060 ย้ายแกนไป Arduino 3.3.12 / IDF 5.5 (เฟส 1) — RAM ภายในหาย 35 KB, หยุดวิทยุแล้วตื่นเอง, สตรีมค้างตอนเริ่ม]] | Framework | ✅ Fixed (branch `feature/esp-sr`) |
| T027 | [[#T027 สลับโมเดลวนไปมาไม่หยุด - ทุกโมเดลติด quota]] | AI | ✅ Fixed (หยุดพัก + ไม่นับซ้ำ) |
| T028 | [[#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]] | AI | ✅ Fixed (ปิด Search อัตโนมัติ) — verified |
| T044 | [[#T044 เสียง AI ตอบกลับกระตุก - jitter buffer เล็ก + WebSocket อ่าน header ผิดจังหวะ]] | Audio/Network | 🔄 บรรเทาแล้ว (บัฟเฟอร์ตามความเร็วเน็ตที่วัดได้ + ตัวอ่าน frame เอง) — ต้นเหตุ TCP window 5.7 KB ของ framework ยังอยู่ |
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

## T093: แก้จาก logcat แอปมือถือ — เลือกเสียงแล้ว key หาย, SQLite error, โหลดซ้ำ, log คำปลุก, ช่องว่างคำไทย

**วันที่:** 2026-10-24 · **ที่มา:** logcat ตอนตั้งค่าแอปครั้งแรก (SM-S908E) — ผู้ใช้ใช้คำปลุก "จาวิส"

| อาการใน log | สาเหตุ | แก้ |
|---|---|---|
| `Voice identity applied` แล้ว `key=BLANK` 4 วิ | `selectVoiceProfile()` เรียก `updateSettings(_apiKey.value, ...)` → บันทึก `api_key` จากหน้า Settings (key ที่ทดสอบแล้วแต่ยังไม่กดบันทึก = ว่าง) + `prefer_free_only` = false ทุกครั้ง | `SettingsController.updateVoice()` บันทึกแค่ `voice_name` + `JarvisOrchestrator.updateVoice()` (ใช้ key ที่ทำงานอยู่) — ใช้ทั้งหน้า Settings และตอน AI เปลี่ยนเสียงเอง · `updateSettings` ค่าเริ่ม `preferFree` = ค่าปัจจุบัน |
| `SQLiteLog E duplicate column name: features_json` ทุกครั้งที่เปิดแอป | `ALTER TABLE ... ADD COLUMN` รันทุกครั้ง | `addColumnIfMissing()` เช็ค `PRAGMA table_info` ก่อน |
| `PetVisionBridge installed` ×2 | `installBridge()` เรียกจาก MainActivity และ AlwaysLiveManager | ติดตั้งครั้งเดียว |
| `Create TextToSpeech` ×2 ตอนเปิดแอป | `LiveVoiceAlertEngine` (บริการแจ้งเตือน เริ่มทุกครั้งที่เปิดแอป ปิดเองหลัง ~4 นาที) สร้าง TTS ตอนเริ่ม | สร้างตอนพูดแจ้งเตือนครั้งแรก (รอ bind สูงสุด 4 วิ) |
| `Initializing AudioTrack (Speaker)` ×2 | `SpecialistSessionController` สร้าง `PcmAudioEngine` ตอนเปิดแอป | `lazy` — สร้างเมื่อเริ่มโหมดประชุม/แปลภาษา |
| `Updated dynamic models from API` 2–5 ครั้งติดกัน | `fetchModels()` ส่งรายชื่อที่กรองเหลือ generateContent ไปทับรายชื่อเต็มที่ `GeminiService.listModels()` ตั้งไว้แล้ว (ทับโมเดล Live ที่มีแค่ bidi) | ลบการเรียกซ้ำ + `ModelConfig` log เฉพาะเมื่อรายชื่อเปลี่ยน |
| `WakeWord listening ...` ทุก 5 วิ แม้ห้องเงียบ · `need 0.40` ในโหมดที่ไม่ใช้โมเดลเดิม | log ทุกช่วง 5 วิ | log เฉพาะช่วงที่ได้ยินเสียงพูด/โมเดลขยับ, โหมด 2 ไม่แสดงคะแนนโมเดลเดิม · `wake word heard by the stock model / own recordings` |
| `jarvis="สวัสดีค่ะบอส พร้อม คุย ตลอด..."` (แชทเว้นวรรคทุกคำ) | ชิ้นคำพูดของโมเดลจาก Live API ขึ้นต้นด้วยช่องว่างทุกชิ้น | `LiveProtocol.mergeTranscript(joinThai = true)` เฉพาะคำพูดโมเดล: ไทยต่อไทยตัดช่องว่างที่รอยต่อ (ข้อความผู้ใช้คงเดิม — guard อ่านอยู่) |

**คำปลุก "จาวิส":** ผู้ใช้ฝึกในโหมด 1 ("+ เสียงฉัน" = ฝึก "Hey Jarvis") ทั้งที่พูด "จาวิส" → โมเดลเดิมให้คะแนน 0.00 ทุกครั้ง ติดเพราะเทียบเสียงที่ฝึกอย่างเดียว
แต่โหมด 1 ยังฟังโมเดล "hey jarvis" ด้วย (คนอื่น/ทีวีพูด "Hey Jarvis" ก็ปลุก) → **ควรใช้โหมด 2 "คำของฉัน"** (ค่าเริ่มของคำ = "จาวิส") ·
หน้าฝึกโหมด 1 เตือนเมื่ออัด ≥ 3 ครั้งแล้วโมเดลเดิมได้ < 0.05 ทุกครั้ง

**ผล:** build ผ่าน, tests Jarvis 725 (ข้าม 17 ที่ต่อเน็ต) ผ่าน, esp32 ผ่าน · ยังไม่ได้ลองบนมือถือ

## T092: build แอปมือถือหลังลง Windows ใหม่ — เก็บ warning deprecated ให้ build สะอาด

**วันที่:** 2026-10-24 · **ผู้ใช้:** ลง Windows ใหม่ แล้ว `:composeApp:assembleDebug` ขึ้น warning เยอะ (build ผ่าน)

**สาเหตุ:** ไม่ใช่ error — cache Gradle หายจึงโหลด/คอมไพล์ใหม่ทั้งหมดและเห็น warning ที่มีอยู่เดิม · `Unable to strip ... .so` = ไลบรารีสำเร็จรูป
(onnxruntime/mediapipe/godot) ไม่มี NDK ไว้ strip — ปกติของ debug build

**แก้ (ทำงานเหมือนเดิมทุกรุ่น Android, minSdk 26):**
- `JarvisAccessibilityService.kt`: `recycle()` → `recycleCompat()` (เรียกเฉพาะ < Android 13 เพราะ 13+ เป็น no-op), `obtain(node)` → `copyCompat()` (13+ ใช้ constructor)
- `LocationProvider.kt`: `requestSingleUpdate` → `LocationManagerCompat.getCurrentLocation` (+ `android.os.CancellationSignal` ตอนยกเลิก/timeout) ·
  `Geocoder.getFromLocation` → Android 13+ ใช้ `GeocodeListener`
- `MainActivity.kt` (esp32): `setDecorFitsSystemWindows(false)` เรียกเฉพาะ Android 11–14 (15+ edge to edge เสมอ)
- `MeetingScreen.kt`: `Icons.AutoMirrored.Filled.Send` · `LiveGeminiService.kt`: ลบ `call.id != null` (`LiveFunctionCall.id` ไม่เป็น null)

**ผล:** build ไม่มี warning Kotlin, esp32 unit tests ผ่าน · ยังไม่ได้ลองบนมือถือจริง (ตำแหน่ง GPS / Accessibility)

**สิ่งที่ต้องตั้งเองหลังลง Windows ใหม่:** ติดตั้ง Python 3 (Studio, `build_mobile_apk.py`, `publish_release.py`) · `JAVA_HOME` ถ้าจะรัน
`gradlew` นอก Android Studio (ใช้ `C:\Program Files\Android\Android Studio\jbr` ได้) · `git config --global --add safe.directory E:/APP-JARVIS/ESP32-AI`
(SID ผู้ใช้ Windows เปลี่ยน → git ขึ้น "dubious ownership") · `include/secrets.h` จาก backup · **debug keystore ใหม่** (`~/.android/debug.keystore`
สร้างใหม่) → APK ที่ build หลังจากนี้ลงทับแอปเดิมบนมือถือไม่ได้ ต้องถอนแอปครั้งเดียว (ข้อมูลในแอปหาย) หรือคัดลอก `debug.keystore` เดิมจาก backup

**ตั้งเครื่องใหม่แล้ว (2026-10-24):**
- **PlatformIO ต้องใช้ Python 3.10–3.14** — pioarduino 55.03.312 ขึ้น `ERROR: Python version must be 3.10 ... 3.14` บน 3.15 → ติดตั้ง 3.14 คู่กัน
  (`py install 3.14`) แล้ว `py -V:3.14 -m pip install platformio pyserial cryptography` · PATH ผู้ใช้ชี้ `pythoncore-3.14-64\Scripts` (`pio`)
  · `python` ยังเป็น 3.15 (Studio/สคริปต์ใช้ได้) · ระวัง Python install manager สร้าง shortcut `pio.exe` ใน `%LOCALAPPDATA%\Python\bin` ชี้ไป 3.15 → ลบทิ้ง
- MSVC: `winget install Microsoft.VisualStudio.2022.BuildTools` (workload VCTools) สำหรับ `host_tests\run_tests.bat`
- `JAVA_HOME` = `C:\Program Files\Android\Android Studio\jbr`, `ANDROID_HOME` = `%LOCALAPPDATA%\Android\Sdk`, PATH เพิ่ม `jbr\bin` (keytool) + `platform-tools` (adb)
- `gh` (ไม่บังคับ — `publish_release.py` ใช้ `git credential`) และ Obsidian ผ่าน winget
- ผล: `pio run` ผ่าน (ครั้งแรก ~13 นาที), host tests 2,369 checks 0 failures, `build_mobile_apk.py` ผ่าน
- **"OpenJDK Platform binary" ค้างใน Task Manager** (2.8 GB + 350 MB): Gradle daemon + Kotlin daemon ที่รอ build ครั้งต่อไป (Gradle ค่าเริ่ม
  อยู่ต่อ 3 ชม.) → `tools/mobile_app/gradle.properties`: `org.gradle.daemon.idletimeout=900000` (15 นาที) · ปิดทันที `gradlew --stop`
  (Kotlin daemon ปิดตามเอง — ตรวจแล้ว java เหลือ 0) · ยังไม่ได้รอดูว่าปิดเองครบ 15 นาที
- `tools/publish_release.py` / `build_mobile_apk.py` ไม่มี `--help` (อาร์กิวเมนต์ที่ไม่รู้จักถูกข้าม → รันจริง) — ห้ามใช้ `--help` ทดสอบ

## T091: คำปลุก "Hey Jarvis" ในแอปมือถือ — openWakeWord, กั้นเสียง (A) และพักเซสชัน (B)

**วันที่:** 2026-10-23 · **ผู้ใช้:** ขอ wake word สำหรับแอปมือถือ (มีแต่บนบอร์ด) — แอปคุยในพื้นหลังตอนพักจอได้อยู่แล้ว → เลือก "ทำทั้งแบบ A และแบบ B ด้วย openWakeWord"

**เดิม:** จอดับ + Live เปิด = Gemini ฟังทุกเสียงตลอด (เปลืองโทเคน/เน็ต, ตอบเสียงทีวี) · Live ปิด = energy VAD + `SpeechRecognizer` (ช้า เปลืองแบต คำแรกหลุด)

**ทำ:**
- โมเดล openWakeWord v0.5.1 (mel + speech embedding + `hey_jarvis_v0.1`) ใน `assets/models/openwakeword` — รันบน ONNX Runtime ที่แอปมีอยู่แล้ว
- `OpenWakeWord.kt` (commonMain): port ของ `utils.py`/`model.py` 0.6 — chunk 80 ms, mel บน chunk + 3 hop ก่อนหน้า, embedding 76 เฟรม, classifier 16 embedding,
  5 ผลแรกเป็น 0 · `OnnxWakeWordModels.kt` (androidMain) · `WakeWordService` โหลดโมเดล + ตัวฟังด้วยไมค์ของตัวเอง (VOICE_RECOGNITION)
- `WakeGate` (commonMain): แบบ A ใน callback ไมค์ของ `VoiceController` — ปิดอยู่ไม่ส่งอะไรเลย (เงียบก็คิดโทเคน), เก็บเสียงย้อนหลัง 1.5 วิ, เปิด 8 วิหลังพูด/AI พูด
- แบบ B: `AlwaysLiveManager` เช็คทุก 10 วิตอนจอดับ — เงียบเกินเวลาที่ตั้ง → `sessionControl.sleep()` แล้วเปิดตัวฟังของ `WakeWordService` → ได้ยิน → `wake()` (จอยังดับ)
  · `VoiceController` จำว่าเป็นการพักโดยคำปลุก (`sleptForWakeWord`) — เซสชันที่ผู้ใช้กดปิดเองจะไม่ถูกปลุก
- ไม่ reset ตัวตรวจหลังได้ยินคำปลุก (reset ใช้ ~0.2 วิบน thread ไมค์ → คำสั่งต่อจาก "Jarvis" หาย) — ใช้ช่วงพัก 2 วิแทน
- `Hub.linkedToBoard` → `WakeGate.suppressed`: เชื่อมบอร์ดอยู่ มือถือไม่ตอบ
- ตั้งค่าใน Settings (`wake_enabled`, `wake_gate`, `wake_sleep_min`, `wake_threshold`)

**แก้ตามที่ผู้ใช้ลอง (2026-10-24):** พูด "Hey Jarvis" ตอนพักจอแล้วไม่มีอะไรเกิดขึ้น — log: ไม่ได้เปิด Always Live และไม่มี Live (กด Back ออกจากแอป)
แต่รอบแรกคำปลุกทำงานเฉพาะในสถานะ BACKGROUND_LISTEN ของ Always Live → ไม่มีอะไรฟังอยู่ · แก้: `WakeWordController` รับ screen off/on ของทั้งแอป
(Always Live ยังจัดการเองผ่าน `enter`/`leave` เดียวกัน) · ไม่มี Live → ตัวฟังของตัวเอง → ได้ยิน → `startFromWake()` เปิด Live ในพื้นหลัง ·
log คะแนนทุก 5 วิ + near miss · ทดสอบบนมือถือจริง: จอดับ → เปิดเสียง "Hey Jarvis" จากลำโพง PC → `wake word heard (score 0.55)` →
Live READY ใน ~1 วิ → AI ทัก "สวัสดีค่ะบอส จาวิสพร้อมคุย…" (จอยังดับ)

**เปลี่ยนตาม flow ที่ผู้ใช้กำหนด (2026-10-24):** ผู้ใช้เห็นใน log ว่าปลุกแล้ว WakeWord ยังตรวจต่อ (ผ่านไมค์ของ Live, แบบ A) และ Live ต่อค้างตลอด
(เวลาพักตั้งไว้ "ไม่พัก") — ต้องการ: *ไม่มี Live = ฟังคำปลุก (จอเปิด/ปิดก็ได้) · Live เปิด = หยุดฟัง · ไม่มีการคุยครบเวลา = Live ปิดเอง แล้วฟังใหม่*
→ ตัดแบบ A (กั้นเสียง) ออก, `WakeGate` เหลือการประสานงาน: `LiveListener.liveStarting/liveStopped(owner)` (Live / โหมดประชุม-แปล) ให้คำปลุกปล่อยไมค์ก่อน,
`touch()` เมื่อ AI พูด / ผู้ใช้พูดจบประโยค / เริ่มเซสชัน, `idleExpired()` (อย่างน้อย 1 นาที — ค่า "ไม่พัก" เดิมกลายเป็น 5) ·
`WakeWordController` เช็คทุก 5 วิ: Live เงียบเกินเวลา → `sleep()` (ปิด Live), ไม่มีใครถือไมค์ → เปิดตัวฟัง · Settings: ตัวเลือก 1/2/5/10 นาที ·
ทดสอบบนมือถือ (จอเปิด, ไม่มี Live): `no Live session - listening` → เสียง "Hey Jarvis" จากลำโพง PC → `wake word heard (score 0.63)` → Live READY 0.8 วิ

**สำเนียง / เสียงเดิมติดบ้างไม่ติดบ้าง (2026-10-24):** ผู้ใช้ต้องพูดโทนต่ำถึงจะติด, เล่นเสียงที่อัดไว้ซ้ำติดบ้างไม่ติดบ้าง · log: คะแนน 0.36–0.49
(เกณฑ์ 0.5) มีครั้งเดียว 0.96 · สาเหตุ: (1) โมเดล hey_jarvis ฝึกจากเสียงสังเคราะห์สำเนียงอังกฤษ (2) chunk 80 ms — วัดบน PC: "Hey Jarvis" เดียวกัน
เลื่อน 0–70 ms คะแนนสูงสุด 0.19…0.98 · แก้: `WakeScorer` 2 เฟสห่าง 40 ms + เฉลี่ย (2 ค่า: 0.47…0.99, เทียบ 3 ค่า 0.32…0.98 จึงเลือก 2) ·
โหมด "+ เสียงฉัน" / "คำของฉัน" (`PersonalWake`, DTW บน speech embedding, เกณฑ์เรียนจากเสียงที่ฝึก) · ทดสอบด้วยโมเดลจริงบน PC (`PersonalWakeTest`):
"Hey Jarvis" พูดเร็ว (โมเดลเดิมให้ 0.29) ไม่ติดโหมดปกติ แต่ติดเมื่อฝึกเสียง, "Hello robot" (แทนคำไทย — PC ไม่มีเสียง TTS ไทย) ติดทั้งในประโยค
และพูดช้า, ประโยคอื่น 5 ประโยค (รวม "Hello Roberta, the robot arm...") ไม่ติด, บันทึก/โหลดโปรไฟล์ได้ค่าเดิม · Jarvis tests 723 ผ่าน

**ฝึกแล้วยังไม่ติด (2026-10-24):** log ตอนฝึก: โมเดลเดิมให้เสียงฝึก 0.00–0.03 · ดึง `wake_enrol/take_*.pcm` มาตรวจ: เสียงครบคำ ~0.5 วิ
ไม่โดนตัด — package openWakeWord ทางการให้ 0.00–0.08 (ขยาย 8 เท่าก็ไม่ขึ้น) → โหมด 1 ที่รอคะแนนเดิม ≥ 0.12 จึงไม่เคยเทียบเสียงที่ฝึกเลย ·
เทียบแบบเว้นทีละไฟล์บน PC: ครั้งที่เว้นไว้ 0.016–0.052 (เกณฑ์ 0.073–0.089), ประโยคอื่น 0.21–0.25 → เอาด่าน 0.12 ออก
(เทียบทุกครั้งที่มีคนพูด) + test `mode1MatchesTheUsersVoiceEvenWhenTheStockModelGivesNothing` · บนมือถือ: เปิดเสียงฝึกของผู้ใช้ผ่านลำโพง PC →
`wake word heard (mode 1, average score 0.00, voice distance 0.076)` · เสียงของผู้ใช้ไม่เก็บใน repo (test ใช้ TTS แทน)

**ตรวจ:** สร้างเสียงทดสอบด้วย Windows TTS (David/Zira: "Hey Jarvis ..." และประโยคอื่น) → package `openwakeword` ทางการ (Python, onnx) ให้คะแนนอ้างอิง →
`OpenWakeWordTest` รันโมเดลจริงบน PC (onnxruntime desktop) ต่างไม่เกิน 0.05 ทุก chunk หลังช่วงเริ่ม, ป้อนทีละขนาดไม่เท่ากันได้ผลเท่ากัน ·
`WakeGateTest` 8 ข้อ · Jarvis tests 722 ผ่าน · ติดตั้งบน SM-S908E: log `openWakeWord ready (hey jarvis)`, ไม่มี crash ·
**ยังไม่ได้ลองพูดใส่มือถือจริงตอนพักจอ** (ต้องเปิด Always Live แล้วดับจอ)

## T090: แก้จากรีวิวทั้งโปรเจค — handshake Wi-Fi v2, การนำทางหลังปิดหน้าบอร์ด, คิวแจ้งเตือน

**วันที่:** 2026-10-23 · **ผู้ใช้:** "แก้ทุกข้อที่รีวิวเจอ ทั้งแอปมือถือและเฟิร์มแวร์ รวมถึงลำดับ handshake Wi-Fi"

| # | อาการ / สาเหตุ | แก้ |
|---|---|---|
| 1 | ปิดหน้าบอร์ด (✕ / back) ตอนต่อ USB → `onDestroy` ปิดพอร์ตแต่ไม่บอก `PhoneHub` → GPS/จุดเลี้ยวถูกส่งเข้าพอร์ตที่ปิดแล้ว ไม่สลับไป Wi-Fi (บอร์ดไม่ได้ข้อมูลนำทางเลย) | `Hub.get(this).usbGone()` ใน `onDestroy` |
| 2 | อยู่หน้า Jarvis แล้วบอร์ดสั่ง "นำทางไป…" → แอปตอบว่าไม่มีสิทธิ์แสดงทับแอปอื่น ทั้งที่แอปอยู่บนจอ (`Hub.uiVisible` ตั้งแค่หน้าบอร์ด) | Jarvis `MainActivity.onResume/onPause` ตั้ง `Hub.uiVisible` ด้วย |
| 3 | handshake v1: แอป (ฝั่งที่ใครก็ต่อเข้ามาได้) ตอบ proof ให้ทุก hello → relay ใน Wi-Fi เดียวกันหลอกบอร์ดได้ แล้วฉีด GPS/การนำทางปลอม (หลัง handshake บรรทัดไม่มีการยืนยัน) | v2: บอร์ดพิสูจน์ก่อน + session key + ตราประทับ `\t<seq>.<mac>` ทุกบรรทัด (wiki 18) — แอปเก่า/บอร์ดเก่าได้ข้อความให้อัปเดต |
| 4 | `_announcements` ไม่มีเพดาน: ออฟไลน์นานๆ แจ้งเตือน (PIR/ออด/รถ/วิทยุ/ราคา) สะสมใน PSRAM แล้วพอต่อได้ AI ไล่พูดของเก่าทีละอัน | `announce()`: เก็บสูงสุด 8 รายการ (ทิ้งเก่าสุด) + อายุ 15 นาที |
| 5 | คำขอใน `_out` ค้างตอน Wi-Fi หลุด → ต่อใหม่ (แม้หลายชั่วโมงต่อมา) แอปได้ "navigate" เก่า | `xQueueReset` ตอนหลุดและตอนยืนยันการเชื่อมต่อใหม่ |
| 6 | บรรทัด Wi-Fi ยาวเกิน 512 ไบต์: ส่วนท้ายถูกอ่านเป็นข้อความใหม่ | ทิ้งจนถึงขึ้นบรรทัด (`skipping`) |
| 7 | คำสั่ง console เกิน 1,500 ตัวอักษรถูกตัดแล้วยังรัน | ตอบ `ERR line too long` ไม่รัน |
| — | stack task `phone` 4 KB ตึงขึ้นจากบัฟเฟอร์ตราประทับ | 6 KB (PSRAM) |
| — | ไฟล์ที่ติดมาจาก PersonalAIBot (`composeApp/log.md`, `docs/`), `installOptions(...)` deprecated, ชื่อเก่า "ESP32-AI-Flasher" ใน User-Agent | ลบ / `installOptions += ...` / `PersonalAIBot-Mobile/` |

**ผล:** `pio run` ผ่าน ไม่มี warning · host tests 2,369 checks ผ่าน (+`phone_wifi_lines_are_sealed`) · esp32 58 ผ่าน (+`sealsMatchTheBoard`,
`oldBoardFirmwareIsToldToUpdate`, ฉีดบรรทัดไม่มีตรา → ตัดการเชื่อมต่อ) · Jarvis unit tests ผ่าน · ติดตั้งบน SM-S908E: ✕ กลับ Jarvis, ไม่มี crash ·
**ยังไม่ได้ลองกับบอร์ดจริง** (ต้องแฟลชเฟิร์มแวร์นี้ — แอปใหม่กับบอร์ด v4.3.3 ต่อ Wi-Fi ไม่ได้ ต้องอัปเดตคู่กัน; USB ใช้ได้เหมือนเดิม)

## T089: หน้าบอร์ด ESP32 ให้หน้าตาเหมือน Jarvis + ถอด MT5 ออกจากแอปมือถือ

**วันที่:** 2026-10-23 · **ผู้ใช้:** "เปลี่ยนหน้าตา ui ของหน้าบอร์ด esp32 ให้เป็นรูปแบบเดียวกับ app และนำส่วนของ mt5 ออกทั้งหมด"

**UI:** หน้าบอร์ดเป็น View (ไม่ใช่ Compose) — เปลี่ยนที่ resource ทั้งหมด ไม่แตะ logic: `Esp32Theme` เป็น `Theme.Material.NoActionBar` (มืด)
+ สีจาก `JarvisTheme` (`colors.xml`), ปุ่ม `JarvisButton` (`drawable/btn.xml`, มุม 12dp + เส้นขอบ), การ์ดมีเส้นขอบ, สี log ปรับให้อ่านบนพื้นมืด,
แถบหัว = ชื่อสีฟ้ากลางจอ + ปุ่ม ✕ (`closeButton` → `onBackPressed()` จึงยังกันออกระหว่างแฟลช), แถบแท็บสี Surface + แท็บที่เลือกสีฟ้า

**MT5:** ลบ `Mt5Controller`, `Mt5TerminalService`, `Mt5ToolHandler`, `Mt5LocalCache`, `Mt5RiskGate`, `BrokerSymbolCache`, `Mt5RiskSnapshotProvider`,
`auto/*` (Auto Trading ผ่าน mt5-core-server), หน้าจอ `TradingTerminalScreen` + `Terminal*Tab`, `AutoTradingScreen`, `SymbolPickerDialog`, `OfflineBanner`
· tool declarations `trading_mt5_*` + `mt5_place_order/close_position`, domain MT5 ใน router, `TradingToolPolicy.mt5Mode` / strict MT5 / `isMt5Prompt`,
การเติม endpoint/token ของ bridge ใน `ToolExecutor` · `TradingSignalMarketDataRouter` = TradingView อย่างเดียว · `trading_deep_analysis_suite` ใช้ engine ในเครื่อง
(เดิมยิง `127.0.0.1:8090/api/mt5/auto/deep-analysis` ก่อน) · prompt/persona ไม่พูดถึง MT5 · ตาราง `Mt5*`, `TradeSyncSnapshot`, `AutoTradeJournal` ออกจาก schema
และ `DROP TABLE IF EXISTS` ตอนเปิดแอป · "Show free models only" ย้ายจาก AutoTrading ไป `SettingsController.preferFreeOnly`
· คงไว้: TradingView/SMC/backtest/Demo broker (เป็นการคำนวณในเครื่อง) และคอมเมนต์ที่บอกว่าอัลกอริทึม SMC port มาจาก mt5-core-server

**ผล:** `:composeApp:testDebugUnitTest` 711 ผ่าน (17 skip) · `:esp32:testDebugUnitTest` 56 ผ่าน · ติดตั้งบน SM-S908E (Android 16): ทุกแท็บของหน้าบอร์ดเป็นธีมมืด,
แถบบน Jarvis ไม่มีปุ่ม MT5, ไม่มี crash · ยังไม่ได้ลองกับบอร์ดจริง

## T088: ใช้แอป Jarvis (PersonalAIBot composeApp) เป็นแอปมือถือของโปรเจค

**วันที่:** 2026-10-23 · **ผู้ใช้:** ต้องการดึงแอปมือถือจากโปรเจค `AndroidStudioProjects\PersonalAIBot\composeApp` มาใช้แทนแอปมือถือของโปรเจคนี้
(เลือกแบบ "ย้ายทั้งแอป + เติม ESP32")

**ปัญหา:** composeApp (Kotlin Multiplatform + Compose, ~430 ไฟล์ .kt, Godot, Firebase, ML Kit/MediaPipe/ONNX) ไม่มีส่วนที่คุยกับบอร์ดเลย —
ถ้าแทนตรงๆ ฟีเจอร์แฟลช / คอนโซลบอร์ด / นำทาง GPS (wiki 17/18) จะหายหมด

**วิธีทำ:** `tools/mobile_app/` = Gradle project ใหม่ (Gradle 8.14.5, AGP 8.8.2, Kotlin 2.1.20 ของ PersonalAIBot)
- `composeApp/` คัดลอกจากไฟล์ที่ git track ของ PersonalAIBot + ปุ่มไอคอนชิปในแถบบน (`App(onOpenBoard)`) → เปิด `pabflasher.MainActivity`
- `esp32/` = แอป ESP32-AI Flasher เดิมแปลงเป็น Android library (namespace เดิม → R/BuildConfig/โค้ด/test ไม่ต้องแก้) ·
  `BuildConfig.VERSION_NAME` ของ library ไม่มีแล้ว → ใส่เป็น buildConfigField จาก `mobileVersionName` ใน `gradle.properties` ·
  ย้ายของระดับแอปออก (ไอคอน, `app_name` → `esp32_title`, data extraction rules) · `usb.host` เป็น `required="false"` (มือถือไม่มี OTG ก็ติดตั้งได้)
- applicationId `com.skyliner2008.jarvis` (ทับแอป Jarvis เดิมได้), versionCode 3 / 2.0.0, minSdk 26 (ตาม esp32), release เซ็นด้วย debug key ถ้าไม่มี keystore เหมือนเดิม
- `google-services.json` / `godot-lib.*.aar` / `godot-assets/` git-ignore — google-services plugin ใช้เฉพาะเมื่อมีไฟล์ (ไม่มีก็ build ได้)
- `rootProject.name` ต้องเป็น `PersonalAIBot` — เป็นชื่อ package ของ compose resources (`personalaibot.composeapp.generated.resources`); เปลี่ยนแล้ว compile ไม่ผ่าน
- `tools/build_flasher_apk.py` → `tools/build_mobile_apk.py` → `dist/PersonalAIBot-Mobile.apk` (~224 MB) · `publish_release.py` แนบไฟล์นี้

**ผล:** `:esp32:testDebugUnitTest` 56/56 ผ่าน · `:composeApp:assembleRelease` ผ่าน (manifest รวม: pabflasher.MainActivity + USB_DEVICE_ATTACHED, NavService, MapsListener)
· **ยังไม่ได้ลองบนมือถือจริง/emulator** (ไม่มีอุปกรณ์ต่อ PC ตอนทำ)

## T087: หน้าปัดจำลองกระตุก + ข้อความสีเหลือง "กำลังรอข้อมูลจากรถ..." กระพริบขึ้นเป็นระยะ

**วันที่:** 2026-10-22 · **ผู้ใช้:** "เมื่อสั่งเปิดหน้าปัดจำลอง มีข้อความสีเหลืองกระพริบขึ้นมาเป็นระยะ และหน้าปัดกระตุก"

**อาการ:** ตัวจำลอง (SIM) ส่งค่าทุก 20 ms แต่บางเฟรมหน้าปัดเห็นว่า "ไม่มีข้อมูล" → กล่องเหลืองโผล่ 1 เฟรม,
จุดสถานะกระพริบเหลือง, เข็ม/ตัวเลขรอบตกเป็น 0 แล้วเด้งกลับ (กระตุก)

**สาเหตุ:** race ระหว่าง 2 core — UI task (core 1) จับ `now = millis()` ตอนต้นลูป แล้วค่อยอ่าน snapshot ทีหลัง
ระหว่างนั้น task รถ (core 0) อาจ publish ค่าที่ประทับเวลา `at` ใหม่กว่า `now` ไม่กี่ ms →
`Snapshot::fresh()` คำนวณ `(uint32_t)(now - at)` ได้ค่าติดลบ = ~4 พันล้าน > `kStaleMs` → ถือว่าข้อมูลเก่า
(`DashModel` จึงตั้ง `_smoothRpm = 0` ด้วย → เข็มกระตุก)

**วิธีแก้:** เปรียบเทียบแบบ signed `(int32_t)(now - at) <= kStaleMs` — ค่าที่ใหม่กว่า `now` นับว่าสด (ยัง wrap-safe)
ใน `src/vehicle/VehicleData.h` และ `phone::Gps/Nav::fresh()` ใน `src/phone/PhoneProtocol.h` (แถบนำทางบนหน้าปัดเสี่ยงแบบเดียวกัน)
+ host test ใน `host_tests/test_vehicle.cpp`

## T086: log ขึ้น `connect(): socket error ... errno: 104, "Connection reset by peer"` ทุก 4 วินาที

**วันที่:** 2026-10-22 · **ผู้ใช้:** "log ใน app บน pc ... แสดง error ตลอด พยายามหามือถือ"

**อาการ:** บอร์ดที่จับคู่มือถือแล้ว แต่ต่อ Wi-Fi บ้าน (ไม่ใช่ hotspot มือถือ) และไม่ได้เปิดแอปมือถือ →
log ขึ้น `[E][NetworkClient.cpp:278] connect(): socket error on fd 50, errno: 104` ทุก 4 วินาทีไม่หยุด

**สาเหตุ:** task Wi-Fi ของ `PhoneLink` ลองต่อ TCP ไป **gateway:47800** ทุก 4 วินาที (สมมติว่า gateway = มือถือที่เปิด hotspot)
บน Wi-Fi บ้าน gateway คือ router ซึ่งตอบ RST → Arduino core พิมพ์ error ทุกครั้ง (ไม่มีผลต่อการทำงาน แต่ log รก)

**วิธีแก้:** `src/phone/PhoneLink.cpp` — ลอง gateway แบบถอยเวลา 4 → 8 → 16 → ... → 256 วินาที เมื่อต่อไม่ติด
เริ่มนับใหม่ทันทีเมื่อ gateway เปลี่ยน (เปลี่ยนไปต่อ hotspot มือถือ) และต่อทันทีเมื่อได้ยิน UDP `PABPHONE` จากแอป
(แอปประกาศทุก 3 วินาทีอยู่แล้ว ทั้งบน Wi-Fi บ้านและ hotspot) — ระหว่างได้ยินแอปยังลองทุก 4 วินาทีเหมือนเดิม

## T085: ปัญหาสิทธิ์ "แอปถูกปฏิเสธไม่ให้เข้าถึง" (Restricted Settings) บน Android 13+

**วันที่:** 2026-10-21 · **ผู้ใช้:** "ตัว app มือถือ ยังทีปัญหาเรื่องสิทธิ์ การอนุญาติ" (แนบภาพหน้าจอเตือน "แอปถูกปฏิเสธไม่ให้เข้าถึง")

**อาการ:**
ในแท็บ **🧭 นำทาง** ของแอป ESP32-AI Flasher เมื่อกดเปิดสิทธิ์ "อ่านการแจ้งเตือน (จุดเลี้ยวจาก Google Maps)" ระบบพาไปหน้าการตั้งค่า "การเข้าถึงการแจ้งเตือน" แต่เมื่อแตะเลือกแอป ระบบ Android ขึ้นป๊อปอัปปฏิเสธ:
> "แอปถูกปฏิเสธไม่ให้เข้าถึง — การให้สิทธิ์นี้อาจทำให้ข้อมูลส่วนบุคคลและข้อมูลทางการเงินของคุณมีความเสี่ยง แอปอาจทำงานได้ไม่ถูกต้องหากไม่มีสิทธิ์ที่จำกัดนี้"

**สาเหตุ:**
นี่คือระบบรักษาความปลอดภัย **Restricted Settings (การตั้งค่าที่ถูกจำกัด)** ที่ Google นำมาใช้ตั้งแต่ Android 13 ขึ้นไป สำหรับแอปที่ติดตั้งจากไฟล์ APK นอก Google Play Store (sideloaded app):
- Android จะบล็อกสิทธิ์ที่มีความเสี่ยงสูงอัตโนมัติ ได้แก่ Notification Listener Service (อ่านการแจ้งเตือน) และ Accessibility Service
- เมนูสำหรับปลดล็อคถูกซ่อนไว้ในหน้า "ข้อมูลแอป (App info)" ที่ปุ่มจุด 3 จุด (⋮) มุมขวาบน และจะปรากฏขึ้นมาก็ต่อเมื่อผู้ใช้เคยกดเข้าไปในหน้าตั้งค่าสิทธิ์แล้วเจอป๊อปอัปเตือนอย่างน้อย 1 ครั้ง
- เดิมในแอป ปุ่ม "ข้อมูลแอป" วางอยู่ล่างสุดใต้ข้อความยาว 4 บรรทัด ทำให้บนมือถือส่วนใหญ่ปุ่มนี้หลุดขอบจอด้านล่าง (below the fold) ผู้ใช้มองไม่เห็นและไม่ทราบขั้นตอนการปลดล็อค

**วิธีแก้ (v4.3.2):**
1. **เพิ่ม Dialog แนะนำขั้นตอนทันที:** เมื่อแตะปุ่ม "เปิด" ข้าง "อ่านการแจ้งเตือน" บน Android 13+ หากสิทธิ์ยังไม่เปิด จะแสดง AlertDialog อธิบายวิธีปลดล็อค ⋮ ทีละขั้น พร้อมปุ่มกดตรงไปยัง "ข้อมูลแอป (ปลดล็อค ⋮)" และ "ไปเปิดสิทธิ์"
2. **ปรับ Layout ปุ่มข้อมูลแอป:** ย้ายปุ่ม `🔓 ข้อมูลแอป (กด ⋮ ขวาบน → อนุญาตการตั้งค่าที่ถูกจำกัด)` ขึ้นมาอยู่ติดกับรายการสิทธิ์ทันที ก่อนข้อความช่วยเหลือ เพื่อให้ผู้ใช้มองเห็นได้ทันทีโดยไม่ต้องเลื่อนหน้าจอ
3. **ขั้นตอนการปลดล็อคสำหรับผู้ใช้:**
   - ในป๊อปอัปเตือน ให้กด "ปิด"
   - ในแอป กดปุ่ม "🔓 ข้อมูลแอป"
   - แตะจุด 3 จุด (⋮) ที่มุมขวาบนของหน้าข้อมูลแอป → เลือก **"อนุญาตการตั้งค่าที่ถูกจำกัด"** (Allow restricted settings) → สแกนลายนิ้วมือหรือใส่รหัสผ่านหน้าจอ
   - กลับมาที่หน้าสิทธิ์ แตะเปิดสวิตช์การแจ้งเตือนได้ทันที

## T084: ออก release แล้วขึ้นเตือนเรื่องกุญแจ — ไม่มีแอปมือถือใน v4.3.0, "-app.bin is NOT signed"

**วันที่:** 2026-10-21 · **ผู้ใช้:** "เรื่องกุญแจ มันซับซ้อนเกินไป ... บอร์ด และ sd-card จะอยู่แต่ละผู้ใช้"

**อาการ:** `build_flasher_apk.py` หยุดเพราะไม่มี release keystore → v4.3.0 ไม่มี `ESP32-AI-Flasher.apk` (release immutable เพิ่มทีหลังไม่ได้)
และ `make_release.py` / `publish_release.py` เตือน `NOT signed` ทุกครั้ง ทั้งที่บอร์ดอัปเดตได้ปกติ

**สาเหตุ:** กุญแจทั้งสองเป็นของฝั่งคนออก release ไม่เกี่ยวกับการเก็บรหัสของผู้ใช้ — รหัส Wi-Fi / API key อยู่ใน `secrets.json`
บน SD แบบ SecretBox (AES-256-GCM, กุญแจจาก MAC + eFuse ของชิป) อยู่แล้ว: การ์ดอื่น/บอร์ดอื่น = ตั้งใหม่, OTA เขียนแค่ช่องโปรแกรม ค่าจึงอยู่ครบ

**แก้ (v4.3.1):**
- `build.gradle.kts`: ไม่มี keystore ของตัวเอง → release ใช้ debug signing ของ SDK · `build_flasher_apk.py` ไม่บังคับ keystore แล้ว
- `publish_release.py` build APK ให้ทุก release เมื่อพบ Android SDK (build ล้ม = ไม่ publish)
- ลายเซ็น OTA เป็นทางเลือก: เอาคำเตือน `NOT signed` ออกจากสองสคริปต์ (โค้ดเซ็น/ตรวจยังอยู่ เปิดได้ด้วย `ota_keygen.py`)
- พบระหว่างทาง: Android Studio อัปเดต `jbr` เป็น **JDK 25** → Gradle 8.11 ล้มทันที (ข้อความมีแค่ `25.0.3`) ·
  `build_flasher_apk.py` หา JDK 17–23 เอง (JAVA_HOME → jbr → `~/.gradle/jdks` → ...) แล้วตั้ง `JAVA_HOME` ให้ Gradle
- ตรวจ: APK release 749 KB เซ็นด้วย `CN=Android Debug` (apksigner) · แอป unit tests ผ่าน

## T083: เชื่อมมือถือ ↔ บอร์ด — GPS + นำทาง Google Maps + AI พูดเตือนก่อนเลี้ยว

**วันที่:** 2026-10-20 · **ผู้ใช้:** "เพิ่มระบบ gps นำทาง ใช้ gps มือถือ ส่งข้อมูลหลายช่องทาง usb, บลูทูธ, wifi สั่งงานด้วยเสียงผ่านบอร์ดควบคุมมือถือ"
→ ประเมินในแผน (wiki 18) แล้วผู้ใช้เลือก: Wi-Fi + USB, Google Maps บนมือถือ, AI พูดเตือนก่อนเลี้ยว, สั่งได้แค่ Google Maps, ทดสอบ HONOR X9d + Samsung S22U
- **ทำไมไม่ทำ BLE:** RAM ภายในตอนคุย AI ว่างแค่ 39 KB (ภาพสถานะจากผู้ใช้) แต่ NimBLE ต้อง ≥ 40 KB + ชนกับ OBD BLE + แชร์วิทยุกับ Wi-Fi (เสียงกระตุก)
- **ทำไมเส้นทางจาก Google Maps:** ฟรี ไม่ต้องมี key; แอปอ่าน "การแจ้งเตือนนำทาง" ของ Maps (NotificationListenerService) — รูปแบบไม่มีเอกสาร จึงจำแนกแต่ละช่อง
  ตามหน้าตา (ระยะ / คำสั่ง / ETA) ไม่ใช่ตามตำแหน่ง และเก็บข้อความดิบให้ผู้ใช้ส่งมาเมื่อผิด
- **ความปลอดภัย Wi-Fi:** ถ้าบอร์ดส่ง token ตรงๆ ใครใน Wi-Fi เดียวกันปลอมเป็นมือถือก็ได้ token ไป → challenge-response HMAC-SHA256 ทั้งสองทิศ (nonce + บทบาท
  "phone|"/"board|" กันสะท้อนกลับ), จับคู่ได้ทางสาย USB เท่านั้น, ทาง Wi-Fi รับแต่ข้อความ `phone` (ไม่ใช่คำสั่ง console)
- **เจอระหว่างทำ:** (1) ArduinoJson 7.4 เก็บทศนิยมสั้นเป็น float (คลาด ~10 ซม. — พอสำหรับรถ, ปรับ test) (2) ข้อความไทยยาวโดนตัดที่ 128 ไบต์ (ไทย 3 ไบต์/ตัว) — เห็นในภาพ preview
  วงเวียน → 192 ไบต์ (3) tool `navigate` แรกยาว 560 ไบต์เกินเกณฑ์ test 450 → ย่อเหลือ 364 (4) แอป: GPS มาบน UI thread แล้วเขียน socket ตรงนั้น =
  `NetworkOnMainThreadException` → เขียนใน thread ของ server (5) test JVM: org.json บน PC ไม่รักษาลำดับ key (Android รักษา) → test ไม่ผูกลำดับ
  (6) "±" ไม่มีในฟอนต์ไทยของบอร์ด → "GPS แม่นยำ (4 ม.)"
- **คำพูดเลี้ยว:** ประกาศปกติรอจน AI เงียบ/ไม่มีคนพูด → คำเลี้ยวลัดคิวก่อนประกาศอื่น, ใหม่แทนเก่าที่ยังไม่ได้พูด, ทิ้งเมื่อช้าเกิน 15 วิ (Maps บนมือถือก็พูดอยู่)
**ทดสอบ:** host tests 2,344 checks ✓ · แอป 56 tests ✓ (handshake Wi-Fi จริงกับบอร์ดจำลอง) · บรรทัดจริงจาก Kotlin 8 แบบ → parser C++ ✓ (รวม proof HMAC) ·
`pio run` ✓ (Flash 71.2 %) · ภาพจอจาก `dash_preview` · **ยังไม่ได้ลองกับบอร์ด/มือถือ/รถจริง**
**รวมกับ main v4.2.0 (ก่อนออก v4.3.0):** main แก้หน้าปัดเป็น T078/T079 (BLE เป็น build flag, ปุ่มลัด) ในช่วงเดียวกัน → เลขงานของ branch นี้เลื่อนเป็น
T080 (แฟลชมือถือ) · T081 (Studio บนมือถือ) · T082 (ล้นจอ) · T083 (นำทาง) · conflict: enum `source` ของ tool `vehicle` = ของ main (`obd_ble` เฉพาะ `VEHICLE_BLE`) + `phone`,
`Vehicle::run` ใส่ `PhoneGps` นอก `#if VEHICLE_BLE` · ตรวจบทเรียน T078 ซ้ำ: `.iram0.text` 99.5 KB เท่ากับ main (PhoneLink ไม่ลิงก์ไลบรารีใหม่เข้า IRAM),
RAM 32.7 %, Flash 66.5 % · host tests 2,350 checks ✓ (รวมเพดาน declaration 15,400 bytes ของ main)

## T082: แอปมือถือล้นจอบน Android 15 — แถบแท็บอยู่ใต้ปุ่มนำทาง กดไม่ได้

**วันที่:** 2026-10-20 · **ผู้ใช้ (หลังลอง T081 กับบอร์ดจริง):** "เชื่อมต่อได้ พิมพ์คุยได้ แต่ app ล้นจอ ปุ่มด้านล่างไปซ้อนอยู่ใต้ปุ่มนำทางของมือถือ กดไม่ได้"
- **สาเหตุ:** แอป `targetSdk 35` → Android 15 **บังคับ edge-to-edge**: หน้าต่างวาดใต้แถบสถานะและแถบนำทาง แอปต้องเว้นที่เอง (insets) แต่ไม่ได้ทำ
  → แถบแท็บอยู่ใต้ปุ่ม ◁ ○ □ และ title bar ของระบบทับด้านบนของหน้า (แถบสถานะการเชื่อมต่อหายไปครึ่งหนึ่งในภาพของผู้ใช้)
- **แก้:** `MainActivity.fitSystemBars()` (Android 11+ ทำแบบเดียวกันทุกเครื่อง: `setDecorFitsSystemWindows(false)` + `OnApplyWindowInsetsListener`)
  — padding ซ้าย/ขวา/ล่างตาม system bars + display cutout + **คีย์บอร์ด** · เลิกใช้ title bar ของระบบ (theme `NoActionBar`) แล้วใช้ title ของเราเอง
  ที่เว้นด้านบนเท่าแถบสถานะ (ลองครั้งแรกเว้นแค่ล่าง → title bar ของระบบยังทับหัวหน้าอยู่ใน emulator) · `adjustResize` สำหรับ Android 8–10
- **ตอนพิมพ์:** ลองแล้วช่องแชทหลุดจอ (การ์ดสถานะ + ปุ่ม + แถบแท็บกินที่หมด) → ตอนคีย์บอร์ดขึ้น ซ่อนแถบแท็บ และแท็บบอร์ดย่อเหลือแชท + ช่องพิมพ์
  (`BoardPage.compact`) — คีย์บอร์ดลงแล้วกลับมาครบ
**ทดสอบ:** emulator Android 11 ที่ตั้งโหมด edge-to-edge แบบเดียวกับ Android 15 (โค้ดเส้นทางเดียวกัน): ก่อนแก้ ล่างทับ/บนทับ, หลังแก้ ครบทั้งตอนปกติและตอนพิมพ์
· unit tests 41 ข้อ ✓ · lint 0 errors
· **มือถือจริง (ผู้ใช้):** "ทดสอบการทำงาน ใช้งานได้" — แถบแท็บอยู่เหนือปุ่มนำทาง, log สดขึ้นครบ (ปลุก → ต่อ Gemini → AI ตอบ)
  · ในภาพเห็นแถบบนยังเป็น "กำลังเชื่อมต่อ" ทั้งที่ session ready แล้ว: ขอ `diag` เฉพาะแท็บบอร์ด → แท็บอื่น (ยกเว้นแฟลช) ขอทุก 10 วิ

## T081: แอปมือถือ — สถานะ, log, ตั้งค่า, แชท แบบ ESP32-AI Studio

**วันที่:** 2026-10-20 · **ผู้ใช้:** "ต้องการเพิ่มฟีเจอร์ให้ app มือถือ คล้ายกับ esp32 ai studio ที่สามารถตั้งค่าต่างๆ แสดง log การทำงานของบอร์ดได้"
- **ไม่แก้เฟิร์มแวร์:** ใช้ serial console ชุดเดียวกับ Studio (`config` / `options` / `diag` / `wifiscan` / `apitest` / `set` / `ask` / `mute` ...) — บอร์ดที่เป็น
  v4.2.0 ใช้ได้เลย
- **ส่วนที่ทำ:** `Console.kt` (`Console` แยก TAG/สี/แชท/ซ่อนความลับ, `LineStream` แยกบรรทัด + ข้ามไบนารี `SCREENSHOT`/`RECORD`/`RECPAIRS` + resync ที่ `\nEND\n`,
  `BoardLink` thread อ่าน + เขียนทีละบรรทัด), `BoardSettings.kt` (`BoardOptions`, `SettingsForm.commands` = กติกา "บันทึกทั้งหมด" ของ Studio),
  `BoardPage` / `LogPage` / `SettingsPage`, ย้ายตัวแฟลชไป `FlashPage` · `MainActivity` เป็นเจ้าของพอร์ต: แท็บ, สิทธิ์ USB, ต่ออัตโนมัติ, ปิด console ก่อนแฟลช
- **ข้อควรระวังที่ออกแบบไว้:** เปิด console ห้ามส่ง `SET_CONTROL_LINE_STATE` (ลำดับ DTR/RTS ของ USB-Serial-JTAG รีเซ็ตชิปได้ — Studio ก็ตั้ง DTR/RTS = off)
  · คำสั่งต้องเป็นบรรทัดเดียว (ชื่อผู้ช่วยที่มีขึ้นบรรทัดใหม่จะกลายเป็น 2 คำสั่ง) · log บน UI รวมการวาดใหม่ทุก 80 ms (log ไหลเร็วตอนบูต)
- **ยังไม่มีบนมือถือ:** จอเสมือน, รัน tool, อัดเสียงไมค์, จัดการวิทยุ, OTA, ส่งออก/นำเข้าไฟล์ตั้งค่า
- **ลองบน emulator กับบอร์ดจำลอง** (build ชั่วคราว ไม่ commit — `SerialPort` ปลอมที่ตอบ config/options/diag/wifiscan/ask/set): ทุกแท็บแสดงข้อมูลถูก,
  แถบเลื่อนส่ง `set volume` ทันที, "บันทึกทั้งหมด" ส่งเฉพาะ `set search 0` ที่เปลี่ยน · ปรับ: "RAM ว่าง KB 82" → "RAM ว่าง 82 KB", ปุ่ม "บทสนทนาใหม่" ตัดบรรทัด → "แชทใหม่"
· รีวิวตัวเอง: connect ที่ค้างอยู่อาจแย่งพอร์ตตอนเริ่มแฟลช → `suspendLink()` รอ connect ที่ค้างให้จบ และ connect ตรวจซ้ำก่อนเก็บพอร์ต
**ทดสอบ:** unit tests 41 ข้อ ✓ (เพิ่ม 17: console 4, LineStream 5, ตั้งค่า/การ์ดสถานะ/ผล apitest 7, BoardLink 1) · lint 0 errors · emulator + บอร์ดจำลอง ✓
· **บอร์ดจริง (ผู้ใช้):** เชื่อมต่อได้, การ์ดสถานะขึ้นครบ (fw 4.2.0, Wi-Fi, โมเดล, RAM, แบต), พิมพ์คุยแล้ว AI ตอบ — เจอปัญหาล้นจอ → T082
· **ยังไม่ได้ลองกับบอร์ดจริง**

## T080: แอปมือถือ Android แฟลชเฟิร์มแวร์ผ่านสาย USB (OTG)

**วันที่:** 2026-10-20 · **ผู้ใช้:** "ต้องการเพิ่ม app มือถือที่ไว้ Flash code ใส่บอร์ด โดยการเสียบสาย usb เชื่อมบอร์ดเข้ากับมือถือ"
- **เลือกแอป native Android ไม่ใช่เว็บ:** ทดสอบแล้ว `release-assets.githubusercontent.com` (ปลายทาง redirect ของไฟล์ release) ไม่ส่ง
  `Access-Control-Allow-Origin` → หน้าเว็บ (esptool-js / Web Serial) ดาวน์โหลด .bin จาก GitHub เองไม่ได้ ต้องฝากไฟล์ที่ hosting อื่นทุก release
  · iOS ทำไม่ได้ทั้งเว็บและแอป (ไม่มี USB serial ให้แอป) → Android เท่านั้น
- **ส่วนที่ทำ:** `tools/android_flasher/` (Kotlin, framework ล้วน ไม่มี AndroidX/ไลบรารี USB) — `UsbCdcPort` (CDC-ACM ผ่าน USB host API),
  `Slip`/`Esp` (เฟรม + คำสั่ง ROM), `EspLoader` (รีเซ็ตเข้าโหมดแฟลช, sync, บีบอัด zlib, เขียนบล็อก 1 KB, ตรวจ MD5, hard reset),
  `FlashPlan` (ตำแหน่งตาม `partitions_sr.csv`, ตรวจไฟล์ก่อนแตะบอร์ด), `FlashJob`, `Releases` (GitHub API, sha256, ลายเซ็น OTA), `MainActivity` (ภาษาไทย)
  · `tools/build_flasher_apk.py` (+ `--create-keystore`, `--debug`) · `publish_release.py` แนบ `ESP32-AI-Flasher.apk` เมื่อมี
- **ค่าที่ตรวจกับ esptool 5.6 (ซอร์สใน cache ของ PlatformIO):** ลำดับ DTR/RTS ของ `usb_jtag_bootloader_reset`, `FLASH_DEFL_BEGIN` ของ ROM S3 มี word ที่ 5
  (encrypted) และ erase ต้องปัดขึ้นทีละ 1 KB, `SPI_ATTACH` ของ ROM ยาว 8 ไบต์, MD5 ของ ROM ตอบเป็นข้อความ hex 32 ตัว, ROM ไม่ต้องส่ง `FLASH_DEFL_END`,
  status อยู่หลังข้อมูลตอบ (ROM เติมอีก 2 ไบต์), chip id จาก `GET_SECURITY_INFO` ที่ offset 12
- **ตรวจกับไฟล์ release v4.2.0 จริง:** header ของ app/bootloader มี chip id 9, factory มี `AA 50` ที่ 0x8000, app ใน factory ตรงกับ -app.bin ทุกไบต์,
  NVS (0x9000–0xE000) ในไฟล์ factory เป็น 0xFF ทั้งหมด (= แฟลช factory ล้างการตั้งค่า), otadata ที่แอปเขียนตอน "อัปเดต" = boot_app0 ในไฟล์ factory ทุกไบต์
- **ปัญหาระหว่างทำ:** lint `UnspecifiedRegisterReceiverFlag` เตือนสาขา API < 33 ที่ไม่มี flag ให้ใช้ (suppress เฉพาะจุด) · mipmap ย้ายจาก `-v26` แล้ว build
  แบบ incremental หา `ic_launcher` ไม่เจอ (ต้อง clean) · ในเครื่อง build ของ Claude, Maven Central ตอบ 429 → ใช้ mirror ของ Google ผ่าน init script
  นอก repo (ไม่กระทบเครื่องผู้ใช้)
- **รีวิวตัวเองก่อน commit:** timeout ของบล็อกเดิมใช้อัตราบีบอัดเฉลี่ยทั้งไฟล์ แต่บล็อก 0xFF ของไฟล์ factory แตกได้ ~1 MB → แก้ให้คิดจากขนาดหลังแตก
  ของบล็อกนั้น (แบบ esptool) + test `blocksOfErasedPaddingGetTimeToWrite` (ROM จำลองใช้เวลาเขียนตามจริง) — ยืนยันว่า test นี้ fail กับโค้ดเดิม
  · อ่านไฟล์ที่ผู้ใช้เลือก (อาจอยู่บน cloud) ย้ายออกจาก UI thread
- **ลองบน Android emulator (API 30, ไม่มี KVM):** หน้าจอภาษาไทย, สถานะ "ยังไม่พบบอร์ด", กล่องยืนยัน factory ทำงาน · พบข้อความ error ภาษา Java ดิบ
  (`CertPathValidatorException ...`) → `Net.friendly()` แปลงเป็นภาษาคน (ไม่มีเน็ต / เน็ตช้า / Wi-Fi ดักการเชื่อมต่อ)
**ทดสอบ:** unit tests 24 ข้อ (JVM) ✓ รวม `FakeRom` จำลอง ROM ทั้งตัว + ไฟล์ release จริง · `lintDebug` 0 errors · APK release เซ็น v2 ตรวจด้วย apksigner ✓

**ทดสอบบนบอร์ดจริง 2026-10-20 (มือถือ Android จริง, debug APK):** โหมด "อัปเดต" → พบบอร์ด 303A:1001, ดึง v4.2.0 จาก GitHub, sha256 ✓, รีเซ็ตเข้าโหมดแฟลชเองได้ (ไม่ต้องกด BOOT), เขียน otadata + app 3,525,840 ไบต์, MD5 ตรง, รีสตาร์ต — ทั้งหมดไม่ถึง 4 นาทีรวมดาวน์โหลด
· ยังไม่ได้ลอง: โหมด "ติดตั้งใหม่ทั้งหมด" (factory 11 MB) และไฟล์ .bin ในเครื่อง

## T079: เปิดหน้าปัดรถไม่เป็น — ไม่มีปุ่ม และสั่ง AI แล้วตอบว่า "ไม่รู้จัก"

**วันที่:** 2026-10-19 · **ผู้ใช้:** "ฉันจะเปิด Smart Gauge บนบอร์ดยังไง ไม่มีปุ่มเปิด สั่งให้ ai เปิด มันบอกไม่รู้จัก"
→ "แก้ให้ AI เปิดหน้าปัดได้ และมีปุ่มทางลัด ด้านล่างมุมขวา"
**สาเหตุ:** tool `vehicle` ถูกประกาศเฉพาะเมื่อเลือกแหล่งข้อมูลแล้ว (ตั้งใจประหยัด token) — แหล่งเป็น `off` → โมเดลไม่มี tool นี้เลย ·
ทางเข้าเดียวคือ เฟือง → หน้าปัดรถ → แหล่งข้อมูล → เปิดหน้าปัด
**แก้:**
- `Vehicle::openDash()` — แหล่งเป็น `off` = เริ่ม `sim` แล้วขอเปิดหน้าปัด (ให้มีภาพทันที) · ใช้ทั้งปุ่มลัดและ AI
- tool `vehicle` ประกาศทุก session (`AgentController::vehicleEnabled()` = true) · คำอธิบายมี "หน้าปัด, Smart Gauge" ·
  `open` ที่เริ่มตัวจำลองตอบ `note` ว่าไม่ใช่รถจริง + บอกทางตั้งค่า · เอาการเริ่ม session ใหม่ตอนเปลี่ยนแหล่งออก (รายการ tool ไม่เปลี่ยนแล้ว)
- งบ declaration: base 14,928 + `vehicle` 384 bytes → เพดาน host test สำหรับชุดที่ส่งจริง 15,400 bytes (ไม่ย่อคำอธิบาย tool อื่นที่จูนไว้แล้ว)
- `FaceScreen`: ปุ่มกลมรูปหน้าปัด (ส่วนโค้ง + โซนแดง + เข็ม) ที่ (297, 216) r 17 · พื้นที่แตะ x ≥ 263, y ≥ 186 ·
  ซ่อนตอนมีซับไตเติลหรือพักจอ · บรรทัด "กำลังเล่น" ของวิทยุสั้นลงไม่ทับปุ่ม · กดค้างบนปุ่มไม่เริ่ม session ใหม่
**ทดสอบ:** host tests 2,230 checks ✓ (+ `open`, ชุดจริง < 15,400) · `pio run` (Flash 66.0%) · บนบอร์ด: setup 25,098 bytes ตั้งแต่บูต (มี `vehicle`),
`tool vehicle {"action":"open"}` ตอนแหล่งปิด → `sim` + `note`, `close` ผ่าน, heap 40 KB · ✅ ผู้ใช้แตะปุ่มแล้วหน้าปัดเปิด และสั่งด้วยเสียงได้ (2026-10-19)

## T078: หน้าปัดรถทำให้ Gemini ต่อไม่ได้ — "esp-aes: Failed to allocate memory" เพราะโค้ด BLE กิน IRAM 22 KB

**วันที่:** 2026-10-19 · **ผู้ใช้:** "ต่อ wifi ได้แต่ มัน error" (หลังแฟลช branch หน้าปัดรถ T076/T077 ลงบอร์ด)
**อาการ:** ต่อ Wi-Fi ได้ แต่ `[Gemini] Setup sent (24620 bytes)` → `Send failed - reconnecting` วนทุกโมเดล
(`E esp-aes: Failed to allocate memory` · `esp-tls-mbedtls: write error :-0x0001` · `transport_ws: Error write header`)
→ `[Fallback] all live models cooling down -> pausing 101 s` — แม้แหล่งข้อมูลรถเป็น `off`
**วัดบนบอร์ด (`mem`):**

| เฟิร์มแวร์ | heap ภายในตอนบูต | ระหว่าง session: free / ก้อนใหญ่สุด |
|---|---|---|
| main (ec0ba74) | 55 KB | 43 / 30 KB |
| branch หน้าปัด (T077) | **31 KB** | **19 / 8 KB** ← AES ของ TLS ขอ buffer DMA ไม่ได้ |
| หลังแก้ (T078) | 52 KB | 40 / 27 KB |

**สาเหตุ:** โค้ดอ้างถึง `BLEDevice` (แหล่ง `obd_ble`) → linker ดึง BT controller + NimBLE เข้ามา: **`.iram0.text` 98.9 → 121.1 KB**
(`r_lld_*_hack`, `btdm_controller_task`, `coex_pti_v2` … รวม ~20 KB จาก `xtensa-esp32s3-elf-nm`) — บน S3 IRAM กับ heap ภายในใช้ SRAM ก้อนเดียวกัน
คืนตอนรันไม่ได้ (override `bleInUse()` คืนได้แค่หน่วยความจำ controller ~36 KB ส่วนโค้ดใน IRAM ยังอยู่) ·
ตัวเลข "RAM 32.7%" ของ `pio run` นับแค่ `.dram0.data/.bss` จึงไม่มีใครเห็น (+3 KB)
และถ้าเปิด BLE จริงจะกินเพิ่มอีก ~40 KB (NimBLE host + connection) — ไปพร้อมกับ TLS ของ Gemini บนบอร์ดนี้ไม่ได้
**แก้:** OBD ผ่าน BLE เป็นตัวเลือกตอน build `-DVEHICLE_BLE=1` (ค่าเริ่มต้น **ปิด**, บรรทัด comment ใน `platformio.ini`)
- `VehicleData.h`: `VEHICLE_BLE` / `vehicle::kBleBuilt` · `VehicleLink.*`: `BleElmLink` + override `bleInUse()` อยู่ใน `#if VEHICLE_BLE`
- `Vehicle::setSource(obd_ble)` ตอบ error ภาษาไทย (แนะนำ ELM327 แบบสาย) · ค่า `obd_ble` ที่บันทึกไว้ใน NVS → ปิดตอนบูต
- เมนู "แหล่งข้อมูลรถ" ซ่อน BLE · enum `source` ของ tool `vehicle` ไม่มี `obd_ble`
**ทดสอบ:** host tests 2,227 checks ✓ (+ไม่มี `obd_ble` ใน declaration, `!kBleBuilt`) · `pio run` IRAM 99.5 KB, Flash 70.7 → 66.0% ·
build `-DVEHICLE_BLE=1` ผ่าน · บนบอร์ด: Gemini session ready, `dash source sim` → session ใหม่พร้อม tool `vehicle` ผ่าน,
classic / digital ผ่าน, `dash source obd_ble` → error, heap คงที่ 39–40 KB
**บทเรียน:** ฟีเจอร์ที่ลิงก์ไลบรารีใหม่ ให้เทียบ `xtensa-esp32s3-elf-size -A firmware.elf` (`.iram0.text`) และ `mem` บนบอร์ดระหว่าง Live session ด้วย ไม่ใช่ดูแค่ RAM % ของ pio

## T077: หน้าปัดรถ 3 รูปแบบ — วงกลมดิจิทัล สปอร์ต, เข็มวงกลม คลาสสิก, รถแข่ง

**วันที่:** 2026-10-18 · **ผู้ใช้:** "ยังมีพื้นที่เหลือ พอจะเพิ่ม theme มัน อยากได้หน้าตาหลายๆแบบ วงกลมแบบดิจิตอล สปอร์ต, วงกลมแบบเข็ม คลาสสิก, สไตล์รถแข่ง"
- `vehicle::DashStyle` (Race / Digital / Classic, NVS `veh/style`, `parseStyle` รับ race/digital/classic + sport/ring/needle/analog) · `Vehicle::setStyle`
- `DashScreen` แยกเป็น `drawRace` / `drawDigital` / `drawClassic` + ส่วนร่วม `drawHeader` / `drawWaiting` / `drawReadout` / `auxValues` / ป้ายชื่อแบบ
  · แตะข้อความสถานะบนแถบหัว = แบบถัดไป · เมนู "รูปแบบหน้าปัด" · console `dash style`
- คลาสสิก: หน้าปัด (ขอบ, ขีดทุก 500 rpm / 10 km/h, ตัวเลข, โซนแดง) วาดครั้งเดียวลง sprite PSRAM 320×240 — วาดใหม่เมื่อรอบสูงสุด/รอบ shift/ชนิดรถเปลี่ยน
  · เข็ม `drawWedgeLine` (เรียว ลบรอยหยัก) · เข็มความเร็วเลื่อนนุ่ม 0.15 วิ (ความเร็วจาก OBD/K-Line มาไม่กี่ครั้งต่อวิ)
- ตรวจด้วย preview: เลข 0–200 บนหน้าปัดความเร็วแน่นเกิน → รถยนต์เว้นทุก 40 · ฉาก shift ใน preview ต้องรอให้รอบถึง shift จริง (ตัวจำลองรถเก๋งสูงสุด ~6,070)
**ทดสอบ:** host tests 53 ชุด / 2,224 checks ✓ · `pio run` ผ่าน (RAM 32.7%, Flash 70.7% — +6 KB) · ภาพ 3 แบบ × 5 ฉาก

## T076: หน้าปัดรถ — ฟีเจอร์ใหม่ เขียนเองทั้งหมด (MIT) จากแนวคิดของ redline-gauge

**วันที่:** 2026-10-18 · **ผู้ใช้:** "อยากเพิ่ม repo นี้ (moomdate/redline-gauge) เข้ากับโปรเจคเป็นอีกฟีเจอร์" → เลือก **เขียนใหม่เอง (MIT)** + แหล่งข้อมูล SIM/TOUCH,
Honda K-Line, OBD ELM327 BLE และ "วางโปรเจคให้ครอบคลุมหลายแบบ ใช้ได้ทั้งมอเตอร์ไซค์และรถยนต์"
- **ทำไมไม่คัดลอก:** REDLINE เป็น PolyForm Noncommercial (ห้ามใช้เชิงพาณิชย์ + ต้องคงหน้า splash credit) ไม่เข้ากับ MIT → ไม่มีโค้ด/ภาพ/ฟอนต์ของเขาใน repo
- **ทำไมไม่ใช้ ELM327 Bluetooth Classic แบบต้นฉบับ:** ESP32-S3 มีแค่ BLE 5.0 (`SOC_BT_CLASSIC_SUPPORTED` ไม่มีใน soc_caps ของ S3)
  ผู้ใช้แจ้งว่าบอร์ดมี "Bluetooth 4.2 BR/EDR + LE" — เป็นสเปกของ ESP32 รุ่นแรก (CYD) ส่วน ES3C28P คือ ESP32-S3 (wiki 01) → ใช้ adapter รุ่น BLE
- **RAM ของ BLE:** Arduino 3.3 คืน ~36 KB RAM ภายในของ BLE ตอนบูตถ้า `bleInUse()` เป็น false → override `bleInUse()` (strong, `VehicleLink.cpp`) อ่าน NVS `veh/src`
  ตอนบูต: เก็บไว้เฉพาะเมื่อแหล่งข้อมูลเป็น `obd_ble` (เลือกแล้วรีสตาร์ท 1 ครั้ง) · ตรวจใน `firmware.elf`: `T bleInUse` = ของเรา, `W btInUse` = ของ core
  · ราคาที่จ่ายเสมอ: RAM คงที่ +2.9 KB (ไลบรารี BLE + หน้าปัด), flash +300 KB (64.8 → 70.6%)
- **ส่วนที่ทำ:** `src/vehicle/` — `VehicleData.h` (ช่องข้อมูล + เวลาสด 2.5 วิ, แหล่งข้อมูล, parser ข้อความ), `DashModel` (รอบลื่น, peak, shift light กระพริบ,
  ระดับเตือน, ประมาณเกียร์), `SimEngine` (แรงบิด, คลัตช์, เกียร์อัตโนมัติ, ลม/ล้อ, เบรก, อุ่นเครื่อง, ไดชาร์จ — รถเก๋ง/มอเตอร์ไซค์ 110-125cc),
  `Elm327` (init, PID ที่รถรองรับ, รอบทุกคำสั่งเว้นคำสั่ง, ATRV, timeout/เริ่มใหม่), `HondaKline` (ปลุก/hello/ตาราง, checksum, ตัด echo, layout ปรับได้),
  `Vehicle` (task เฉพาะเมื่อเลือกแหล่ง, NVS `veh`, `agent/vehicle.json`, แจ้งเตือน), `VehicleLink` (UART1 GPIO 43/44 + K-Line wake pulse, BLE scan/connect/notify)
  · `src/ui/DashScreen` · เมนู "หน้าปัดรถ" · console `dash ...` · tool `vehicle` (เดิมประกาศเฉพาะเมื่อเปิด +370 bytes — ตั้งแต่ T079 ประกาศทุก session) · AI พูดเตือนเครื่องร้อน/แบต · `Peripherals::usesPin`
  (แหล่ง UART ไม่ชนอุปกรณ์ใน devices.json)
- **`tools/dash_preview/`:** คอมไพล์ `DashScreen.cpp` ตัวจริง + LovyanGFX บน PC (SDL2) แล้วบันทึก PNG — ใช้ตรวจ layout ไม่ต้องมีบอร์ด (ภาพใน wiki 16)
  · ระหว่างตรวจพบ: ฟอนต์ GFX ไม่มีเครื่องหมายองศา → วาดวงกลมเล็กเอง · ขนาดตัวอักษรค้างจากเลขตัวใหญ่ → `setTextSize(1)` ก่อนข้อความไทย
- **ไม่ได้ทำ / เสี่ยง:** ยังไม่ได้ทดสอบบนบอร์ดและกับรถจริงเลย (BLE adapter แต่ละยี่ห้อ, layout ตาราง K-Line ของแต่ละรุ่น, ระดับแรงดันของโมดูล K-Line,
  RAM ตอน BLE + Live + คำปลุก 2 คำ) · ขาช่อง UART ยัง "สันนิษฐาน" ว่าเป็น 43/44 (wiki 15) · ESP32-AI Studio ยังไม่มีแท็บหน้าปัด (ใช้ console `dash`)
**ทดสอบ:** host tests 53 ชุด / 2,213 checks ✓ (ASan + UBSan; `vehicle_*` 5 ชุด + tool `vehicle`) · `pio run` ผ่าน (RAM 32.7%, Flash 70.6%, ไม่มี warning ในไฟล์ใหม่)
· ภาพ 5 ฉากจาก `tools/dash_preview` (เมือง, shift light, เตือน, มอเตอร์ไซค์ K-Line, รอ Bluetooth)

## T075: สร้างกุญแจเซ็นอัปเดตจาก ESP32-AI Studio ด้วยปุ่มเดียว

**วันที่:** 2026-10-18 · **ผู้ใช้:** "ให้ esp32_ai_studio สามารถสร้างกุญแจส่วนตัวให้ได้ โดยการกดปุ่ม"
- แท็บ 🔧 เฟิร์มแวร์ แถวใหม่ **กุญแจเซ็นอัปเดต (OTA):** 🔑 สร้างกุญแจ · 💾 สำรองกุญแจ... · 📂 นำกุญแจสำรองกลับมา... + สถานะ
  (ยังไม่มีกุญแจ / พร้อม / เฟิร์มแวร์มีกุญแจแต่ไม่พบกุญแจส่วนตัว / ไม่ตรงกัน / มีกุญแจส่วนตัวแต่ header ว่าง)
- ใช้โค้ดเดียวกับ command line: `tools/ota_keygen.py` แยกเป็นฟังก์ชัน `status()` / `create_key()` / `write_header_from_key()` /
  `restore_key()` ที่โยน `KeygenError` (เดิม `sys.exit`) — `make_release.py` / `publish_release.py` จับแล้วหยุดเหมือนเดิม
- 🔑 กดได้อย่างปลอดภัยทุกสถานะ: ไม่สร้างทับกุญแจเดิม, header มีกุญแจแต่ไม่มีกุญแจส่วนตัว = บอกให้นำสำรองกลับมา (ไม่สร้างใหม่ — จะล็อกบอร์ดเดิม),
  มีกุญแจส่วนตัวแต่ header ว่าง = เสนอเขียน header · ไม่มี `cryptography` = เสนอ `pip install cryptography` (ผลขึ้นในกล่อง output ของแท็บ)
  · สร้างเสร็จ = ถามสำรองต่อทันที + เตือนให้ commit `src/app/OtaPublicKey.h`
- 💾 ไม่ยอมบันทึกสำรองไว้ในโฟลเดอร์โปรเจกต์ (ห่างจาก GitHub แค่ `git add -A`) · 📂 รับเฉพาะกุญแจ P-256 ที่ตรงกับ header, ไม่ทับกุญแจอื่นที่มีอยู่
**ทดสอบ:** เปิด Studio จริงบน Xvfb (Python 3.12 + Tk 8.6) แล้วกดปุ่มด้วยสคริปต์ 9 กรณี: สร้าง (+สำรอง, ไฟล์สิทธิ์ 600) → กดซ้ำ (ไม่สร้างใหม่) →
ลบกุญแจ → กดสร้าง (เตือน ไม่สร้าง) → นำสำรองกลับ (พร้อม) → สำรองลงโฟลเดอร์โปรเจกต์ (ปฏิเสธ) → นำกุญแจอื่นกลับ (ปฏิเสธ, ไม่เขียนไฟล์) →
ไม่มี cryptography (เสนอ pip) ✓ · command line `ota_keygen.py` / `--header` และ `make_release.sign_app` ยังทำงานเดิม (ลายเซ็นตรวจผ่าน mbedtls)
· ยังไม่ได้ลองบน Windows (ฟอนต์/หน้าต่างเลือกไฟล์ของจริง)

## T074: ปิดงานค้างจากรีวิว — ลายเซ็น OTA (ECDSA P-256), ย้าย I2S ไป i2s_std, redirect แบบ relative ของวิทยุ

**วันที่:** 2026-10-18 · ผู้ใช้สั่ง "ปรับปรุงที่ยังไม่ได้ทำ" (จาก T069 / T073) · **SecretBox คงเดิม** (MAC + UID) — ผู้ใช้เลือกไม่เขียน eFuse
1. **OTA มีลายเซ็นแล้ว** — เดิมความน่าเชื่อถือ = บัญชี GitHub (sha256 อยู่ใน release เดียวกับไฟล์) · ตอนนี้: ECDSA P-256 ของ `-app.bin`
   (`-app.bin.sig`) ตรวจกับกุญแจสาธารณะใน `src/app/OtaPublicKey.h` ก่อนสลับช่องบูต (`util/OtaVerify.h`, `OtaUpdater::runInstall`)
   · Ed25519 ตามแผนเดิมใช้ไม่ได้: mbedtls 3.6 ของ IDF 5.5 ไม่มี EdDSA · `tools/ota_keygen.py` สร้างกุญแจ (ส่วนตัวอยู่นอก repo)
   · `make_release.py` เซ็น + `publish_release.py` แนบ `.sig` + workflow รับ secret `OTA_SIGNING_KEY` · รายละเอียด [[13-OTA-Update#ลายเซ็นของ release (T074)]]
   · **ยังไม่เปิดใช้:** header ว่างจนกว่าผู้ใช้จะรัน `ota_keygen.py` แล้ว commit (Claude ไม่สร้างกุญแจส่วนตัวแทนผู้ใช้)
2. **I2S: `driver/i2s.h` (legacy, หายใน IDF 6) → `driver/i2s_std.h`** — ช่องเดียว full duplex `i2s_new_channel(&tx, &rx)`, master,
   Philips 16-bit stereo, MCLK 256 fs, DMA 8 × 240 frames, `auto_clear` = ค่าเดียวกับเดิมทุกตัว · เปิดทั้งสองช่องใน `initI2S()` ก่อน `ES8311::begin` (ต้องมี MCLK)
   · `i2s_zero_dma_buffer` ไม่มีใน driver ใหม่ → `clearPlayback()` ปล่อยเสียงที่อยู่ใน DMA (≤ 80 ms) เล่นจนจบ (หยุด TX = หยุด clock ของไมค์ + ES8311)
   · ลบ `AudioManager::setDataPins` (ไม่มีใครเรียก) · warning deprecated ของ I2S หาย (16 → 0)
3. **วิทยุ: redirect แบบ relative** (`/stream`, `live.mp3`, `//cdn/x`, `?q`) → `redirect::resolve()` (`util/HttpRedirect.h`)
**ทดสอบ:** host tests 48 ชุด / 2,066 checks ✓ (+ `ota_signature_asset`, `radio_redirect_resolve`; ASan/UBSan) · `pio run` ผ่านทั้งไม่มีกุญแจ
(RAM 31.8%, Flash 64.8%) และมีกุญแจทดสอบ (64.9%) · ลายเซ็นจาก Python ตรวจด้วย mbedtls 3.6 + `OtaVerify.h` บน PC: ของแท้ผ่าน, แก้ 1 บิต = `bad signature`
**ต้องทดสอบบนบอร์ด:** เสียง AI + วิทยุออกลำโพงปกติ · ไมค์ส่งถึง AI (`[Mic]` / `mic` ใน console) · `Echo reference on` และช่องไมค์ยังเป็น L
(ถ้าสลับ L/R ปลุกด้วยเสียงขณะเปิดวิทยุจะพัง) · แตะจอหยุด AI แล้วเสียงค้างไม่เกิน ~0.1 วิ · หลังรัน `ota_keygen.py`: `ota install` ด้วย release ที่เซ็นแล้ว

## T073: รีวิวรอบ 4 — icy-metaint ติดลบจากสถานีวิทยุเขียนเกิน buffer, ลบโน้ตด้วยคำค้นลบหลายอัน, ไฟล์โน้ตเสียถูกเขียนทับ

**วันที่:** 2026-10-18 · รีวิวทั้งโปรเจครอบ 4 (เน้นโค้ด v4.0–4.1 และจุดที่รับข้อมูลจากภายนอก) · ผู้ใช้สั่ง "แก้ไขได้"
1. **`icy-metaint` จาก server วิทยุไม่ถูกตรวจ** (`RadioPlayer::session`) — ค่าติดลบ (`icy-metaint: -5`) ทำให้ `want` ติดลบ → `stream->read(d, n)`
   รับขนาดเป็น `size_t` = "อ่านทั้ง socket" → เขียนเกินท้าย buffer สตรีม 16 KB (PSRAM) หลาย KB · ส่งได้ทั้ง server ของสถานี
   และคนในวง LAN (สถานีส่วนใหญ่เป็น `http://`, `https://` ก็ `setInsecure()`)
   → `util/IcyMeta.h` `icy::metaInterval()`: รับเฉพาะตัวเลขล้วน 1–262,144 อย่างอื่น = 0 (ไม่มีชื่อเพลง แต่เสียงยังเล่น)
   + ตัวอ่าน body คืน 0 ทันทีเมื่อขอ ≤ 0 ไบต์ (host test `icy_meta_interval`)
2. **`notes` `delete` ด้วย `query` ลบทุกโน้ตที่มีคำนั้น** ("ซื้อ" = รายการซื้อของทุกอัน) แบบเดียวกับ `price_alert remove` ใน T069 ข้อ 9
   → ลบทีละโน้ต: `id` หรือ query ที่ตรง**โน้ตเดียว** · ตรงหลายอัน = error + `candidates` (id/title) ให้โมเดลถามผู้ใช้ ·
   ไม่ใส่ทั้ง id และ query = error (เดิมตอบ deleted 0) · ผลเป็น `deleted_id` + `title` (`util/NoteSearch.h`, host test `notes_delete_pick`)
3. **ไฟล์ `notes.json` / `alerts.json` ที่อ่านไม่ได้ถูกมองเป็น "ว่าง"** → `add` ครั้งถัดไปเขียนรายการใหม่ทับ = โน้ต/แจ้งเตือนเก่าหายหมด
   → `jsonfile::loadStatus()` แยก `Missing` (ไม่มีไฟล์ / ไฟล์ว่าง) กับ `Unreadable` (JSON เสีย, ใหญ่เกิน 256 KB, มีไฟล์แต่เปิดไม่ได้)
   · `Unreadable` = tool ตอบ error + `file` และไม่แตะไฟล์ (log `[Tool] ... cannot be read - not touched`) (host test `json_file_status`)
**ยังไม่ได้ทำ:** redirect ของสถานีวิทยุที่เป็น path แบบ relative (`Location: /stream`) ยังขึ้น "bad URL" • ความเสี่ยงเดิมจาก T069
(OTA ไม่มีลายเซ็น, legacy I2S driver, กุญแจ SecretBox จาก MAC + UID) • เนื้อหาเว็บ (ข่าว/Wikipedia) เข้าโมเดลตัวเดียวกับที่สั่ง relay ได้
**ทดสอบ:** host tests 46 ชุด / 2,036 checks ✓ (รันแบบ AddressSanitizer + UBSan ด้วย) · `pio run` ผ่าน (RAM 31.8%, Flash 64.8%, ไม่มี warning ใหม่)

## T072: get_weather ช้า 8–40 วิ — TLS ถูกแย่ง CPU (priority 2), จับมือซ้ำ, geocode ชื่อไทยพลาด

**วันที่:** 2026-10-18 · ต่อจาก T071 (ครั้งหนึ่ง 40.1 วิ) และ T069 (21.6 วิ) — ระหว่างนั้นโมเดลรอเงียบ (BLOCKING บน 3.8 Live)
- **วิธีวัด:** `httpRequest` พิมพ์ `[Web] <host> -> <code> in N ms (dns, tls, reply, body; bytes)` ทุก request (ถาวร) +
  probe ชั่วคราวที่ทำ GET เอง (แยก TCP / TLS / byte แรก) + เทียบ `curl` จาก PC
- **ผลวัด:** `get_weather` = HTTPS 2–4 ครั้งไป 3 โฮสต์ (geocoding / forecast / air-quality ของ Open-Meteo ที่เยอรมนี RTT ~200 ms) จับมือ TLS ใหม่ทุกครั้ง
  | ตอนวัด | TLS ต่อครั้ง | ทั้ง request |
  |---|---|---|
  | ตื่นอยู่ priority 2 (เดิม) | 2.3–2.8 วิ (ถึง 6.2) | ~2.7 วิ |
  | พักจอ (UI แทบไม่วาด) | 1.3–1.5 วิ | — |
  | priority 3 / **4** | 1.5 / **1.15–1.25 วิ** | ~1.7 / **~1.4 วิ** |
  | PC (เน็ตเดียวกัน) | ~0.55 วิ | ~1 วิ |
  probe: byte แรกหลังส่ง request = 1 RTT (181 ms Open-Meteo, 36–44 ms ipwho.is) → เวลาที่เหลือคือ CPU ของ TLS
- **สาเหตุ:**
  1. task `webTools` priority 2 บน core 1 อยู่**ใต้ UI** (priority 3 วาดตาทุก 16 ms) และ `wakefeed`/`wakeword` (3) → คณิต TLS ได้แค่ช่องว่างระหว่างเฟรม
  2. ชื่อจังหวัดภาษาไทยที่ geocoder ไม่รู้จัก ("อุบลราชธานี", "เพชรบุรี", "สระบุรี" ได้ `results` ว่าง 31 B) → geocode ซ้ำด้วยชื่ออังกฤษ = จับมือ TLS เพิ่มอีกครั้ง (ครั้งละ ~6 วิตอนตื่น)
  3. TLS handshake ของ framework มี timeout แยก **120 วิ** (`handshake_timeout`) — `setTimeout`/connect timeout 12 วิ ไม่ครอบ: แพ็กเก็ตหายระหว่างจับมือ (hotspot)
     ค้างได้นานเกินกว่า 12 วิมาก (น่าจะเป็นที่มาของ 21.6/40.1 วิ — ทดสอบ ~30 ครั้งไม่เจอซ้ำ) · `tls.setTimeout(12)` เดิมเป็นหน่วย ms ใน Arduino 3.x (ถูก connect ทับ ไม่มีผล)
  4. (เจอระหว่างวัด) `HTTPClient::begin()` บน client ที่ connect แล้ว เห็นเป็น "host ใหม่" แล้ว**ตัดทิ้งแล้วจับมือใหม่** → ต้อง `begin()` ก่อน connect
- **แก้:** `JobPriority` ใน `WebTools::loop` — งานที่โมเดล/console รอ รัน **priority 4** (เหนือ UI/คำปลุก, ใต้ agent/geminiIO 5; idle task core 1
  ไม่ถูก watchdog เฝ้า) เช็กแจ้งเตือนราคาเบื้องหลังยัง 2 · **`agent/ThaiProvinces.*`**: พิกัดเมืองหลัก 77 จังหวัด (จาก geocoder ของ Open-Meteo เอง, PPLA/PPLC)
  รับ "จังหวัด…", "จ.…", "เมือง…", อังกฤษสะกดต่างๆ ("Chonburi"/"Chon Buri", "Khon Kaen Province"), ชื่อย่อ (กทม, โคราช, อยุธยา, อุบล, อุดร) — อำเภอ/สถานที่อื่นยัง geocode ·
  `setHandshakeTimeout(10)` · `http.begin()` ก่อน `tls.connect()` · host test `thai_provinces`
- **ทดสอบบนบอร์ด (ตื่นอยู่, COM4):** จังหวัด (ลำพูน, อุทัยธานี, "จ.จันทบุรี", "Sing Buri") **2.8–2.9 วิ** (เดิม 8.3–11.8, geocode พลาด 15–23 วิ) ·
  หาดใหญ่ (geocode) 4.3 วิ · ไม่ระบุที่ (ipwho.is) 4.2 วิ · `ask อากาศที่ยโสธร...` (3.1 Flash Live) เรียก tool → ผลใน 2.8 วิ ตอบครั้งเดียว ·
  `wakeword` dropped 0 · RAM ภายในต่ำสุด 23 KB · host tests 43 ชุด / 2,007 checks ✓
- **แลกกับ:** ตอนจับมือ TLS (~0.5 วิ CPU ต่อครั้ง, 2 ครั้งต่อการถามอากาศ) ภาพตาหยุดนิ่งชั่วครู่ระหว่าง "กำลังดึงข้อมูล..." (เสียง/agent ไม่กระทบ — priority 5–6)
- ที่ยังเหลือ: ~1.15 วิต่อ handshake เป็นต้นทุนของ TLS 1.2 ไปเยอรมนี (3 RTT) — ลดอีกได้ด้วย TLS session resumption (ต้องเก็บ session ต่อโฮสต์) หรือรวบ PM2.5 ไว้ในแคช
  · `TextModelClient` / `RadioPlayer` / `OtaUpdater` ยังใช้ handshake timeout 120 วิของ framework (งานเบื้องหลัง ไม่ได้แก้ในรอบนี้)

## T071: ถามอากาศแล้ว AI ตอบข้อความเดิม 2 รอบ (3.8 Live) — scheduling SILENT อยู่ผิดที่ใน functionResponse

**วันที่:** 2026-10-18 · พบระหว่างทดสอบ T069: ถามอากาศ → `turnComplete` 2 ครั้งห่างกัน ~6–8 วิ ข้อความเดียวกัน — ปิดไมค์ (`mute`) ก็ยังซ้ำ
- **วิธีหา:** เพิ่มคำสั่ง serial **`trace on`** (`GeminiLiveClient::traceMessage`) — 1 บรรทัดต่อข้อความจากเซิร์ฟเวอร์ (transcript, toolCall + id,
  `generationComplete`/`turnComplete`; ก้อนเสียงแค่นับ) และทุก JSON ที่ส่ง (`[Trace] tx ...`)
- **log ก่อนแก้ (3.8 Live, ไมค์ปิด, `ask อากาศที่อุดรธานี...`):**
  ```text
  rx toolCall[ set_expression id=call_3381157 get_weather id=fc_1261... ]      <- เรียกคู่กันใน toolCall เดียว
  tx {"toolResponse":...{"name":"set_expression","response":{"result":{...},"scheduling":"SILENT"}}}
  tx {"toolResponse":...get_weather...}                                         (8.3 วิต่อมา, ส่งครั้งเดียว)
  rx ... generationComplete · turnComplete      "ตอนนี้ที่อุดรธานีมีเมฆบางส่วนค่ะ ..."
  rx (1.4 วิต่อมา) out:"มีเมฆบางส่วนค่ะ อุณหภูมิยี่สิบสี่จุดสอง..." · turnComplete    <- รอบ 2 โดยไม่มีอะไรส่งไปเลย
  ```
  toolResponse ของ `get_weather` ส่ง**ครั้งเดียว** และไม่มี clientContent/เสียงไมค์ส่งไปหลังคำตอบแรก → ไม่ใช่ `ask`, ไม่ใช่ช่วงฟังต่อ, ไม่ใช่ web tool ส่งซ้ำ
- **สาเหตุ:** บน 3.8 Live `set_expression` ประกาศเป็น `NON_BLOCKING` (ไม่ให้ขัดการพูด) และเราตอบ `scheduling: "SILENT"` — แต่ใส่ไว้**ใน `response`**
  ตาม API reference / schema v1beta `scheduling` เป็น field ของ **FunctionResponse ข้าง `response`** ("Only applicable to NON_BLOCKING function calls … Defaults to WHEN_IDLE")
  → ใน `response` เซิร์ฟเวอร์อ่านเป็นข้อมูลผลลัพธ์ธรรมดา แล้วใช้ค่าเริ่มต้น **WHEN_IDLE** = "add the result … and prompt to generate output without interrupting"
  → พอคำตอบอากาศจบ (idle) ผลของ `set_expression` ที่ค้างไว้กระตุ้นให้โมเดลพูดอีกรอบ = ตอบคำถามเดิมซ้ำ
  (ตัวอย่าง Python ในหน้า Live API tools วาง `scheduling` ไว้ใน dict ของ response — ทำให้เข้าใจผิด) · เกิดชัดตอนเรียกคู่กับ web tool เพราะ `get_weather` เป็น BLOCKING
  ผลของ `set_expression` จึงค้างคิวรอจนคำตอบหลักจบ · 3.1 Flash Live ไม่มี async (ทดสอบแล้วตอบครั้งเดียว)
- **แก้:** `AgentTools::responseScheduling(name, behavior)` → `fr["scheduling"] = "SILENT"` **ระดับ FunctionResponse** (เฉพาะ 3.8 Live + tool ที่ประกาศ NON_BLOCKING;
  BLOCKING เซิร์ฟเวอร์ไม่สนใจ, Extended Thinking ไม่รองรับ scheduling) — web tool ยังเป็น BLOCKING ไม่ใส่ scheduling
  · host test `tool_response_scheduling`: tool ที่ประกาศ NON_BLOCKING ทุกตัวได้ scheduling และมีแค่ตัวนั้น
- **ทดสอบบนบอร์ด (COM4, 3.8 Live, ไมค์ปิด):** `ask อากาศที่สงขลา...` (ไม่เรียก set_expression) · `ask ทำหน้าดีใจหน่อย แล้วบอกอากาศที่ระยอง...` และ
  `ask ทำหน้าตื่นเต้นแล้วบอกอากาศที่ชลบุรี...` (เรียก `set_expression` + `get_weather` คู่กันแบบเดียวกับที่ซ้ำ, tx เห็น `},"scheduling":"SILENT"}`)
  → **`turnComplete` ครั้งเดียวทุกรอบ** เฝ้าต่อ 37–55 วิไม่มีรอบ 2 · host tests 42 ชุด / 1,744 checks ✓
- หมายเหตุจากการทดสอบ: `get_weather` ครั้งหนึ่งใช้ **40.1 วิ** (ปกติ 8–12 วิ, ไม่มี error ใน log) → ดู T072
  · ค่าบนบอร์ดตอนเริ่มทดสอบเป็น `model 2` (3.1 Flash Live) — สลับเป็น 3.8 Live ชั่วคราวแล้วตั้งกลับ 3.1 + เปิดไมค์คืนแล้ว

## T070: สถานี AAC ทำให้บอร์ดรีบูต (task watchdog) — Helix AAC วนไม่รู้จบใน DecodeSectionData + ตรวจ/เพิ่มสถานีจาก Thailand.json

**วันที่:** 2026-10-18 · ผู้ใช้ให้ตรวจ `Thailand.json` (406 สถานี) แล้วเพิ่มที่ใช้ได้ลงบอร์ด (wiki 12)
- ลองเล่นทุกสถานีที่เพิ่มบนบอร์ด: **"Best Radio 89 ขอนแก่น" (AAC 48 kbps HE-AAC → 44.1 kHz) รีบูตบอร์ดใน ~5 วิ**
  `task_wdt: IDLE0 ... CPU 0: radio` · backtrace (`xtensa-esp32s3-elf-addr2line`): `raac_GetBits` ← `DecodeSectionData` (noiseless.c) ← `AACDecode` ← `RadioPlayer::session`
- **สาเหตุ:** Helix `DecodeSectionData` อ่านความยาว section จาก bitstream — เฟรมที่เสีย/อ่านเกินท้ายเฟรมได้ 0 → `sectLen = 0` → `sfb` ไม่ขยับ = **วนไม่รู้จบ** (radio task priority 4 บน core 0 ไม่คืน CPU)
  และ `sectLen` ที่เกิน `maxSFB` เขียน `sfbCodeBook` **เลยขอบ array** (สตรีมวิทยุคือข้อมูลจากภายนอก = หน่วยความจำเสียได้)
- **แก้:** `lib/helix-aac/src/noiseless.c` — `DecodeSectionData` คืน `ERR_AAC_INVALID_FRAME` เมื่อ section ยาว 0 หรือเกิน `maxSFB` (`DecodeICS` → `DecodeNoiselessData` ส่งต่อ)
  → `RadioPlayer` ข้ามเฟรมนั้นตามปกติ · `RadioPlayer`: ข้ามไบต์ที่ถอดไม่ได้คืน CPU ทุก 256 ไบต์, ไม่มีเฟรมดีใน 64 KB = เลิก ("AAC stream the decoder cannot read"),
  รอบที่ไม่ได้ถอดเฟรมใดเลยหน่วง 1 ms แม้ข้อมูลยังเข้า (เดิมวนเปล่า)
- **ทดสอบ:** สถานีเดิมเล่น 40 วิ ไม่รีบูต ชื่อเพลงขึ้น (`ลูกทุ่ง (ปรับความเร็ว ...)`) · 24/24 สถานีใหม่เล่นได้บนบอร์ด
- ระหว่างตรวจ: SHOUTcast v1 ส่งหน้า HTML ให้ UA "Mozilla" → ต่อท้าย `/;` ได้สตรีม (29 สถานี) · ผลตรวจทั้งหมด + สำรอง 25 สถานีใน wiki 12 · `tools/radio_check.py`

## T069: รีวิวรอบ 3 — โหมดคำปลุกทำให้ไม่ฟังต่อหลัง AI ตอบ + "AI ไม่ตอบ" หลอก, frame สงวนล้น stack, อุปกรณ์ชื่อกำกวม

**วันที่:** 2026-10-17 · รีวิวทั้งโปรเจครอบ 3 (เน้นโค้ดหลัง T053: คำปลุก/AEC, พูดแทรก, แยกความดัง, OTA, อุปกรณ์) · ผู้ใช้สั่ง "แก้ไขทั้งหมด"
1. **โหมดคำปลุก (ค่าเริ่มต้น `wakeMode = 2`) ปิดช่วง "ฟังต่อ 6 วิ" หลัง AI ตอบ** — ตั้งแต่ T067 ระหว่าง AI พูด `bargeIn` เปิดไมค์ให้ WakeNet
   → `_gateAllowed` ไม่เปลี่ยนเลย ส่วนที่ผูกกับการปิด/เปิด gate จึงไม่ทำงาน: (ก) ไม่ตั้ง `_followUp` หลังคำตอบ (ข) ประโยคที่ยังเปิดตอน AI เริ่มตอบ
   (เซิร์ฟเวอร์ตัด 900 ms, เรา 1.5 วิ) ปิดหลังคำตอบจบ → ตั้ง `_awaitingReply` ช้า → 8 วิต่อมาขึ้น **"AI ไม่ตอบ ลองพูดอีกครั้ง" ทั้งที่ตอบแล้ว** และไม่พัก/ไม่กลับวิทยุ
   (ค) ถ้ายังทยอยส่ง backlog ตอนปลุก เสียง 1.5 วิที่เก็บไว้ระหว่าง AI พูดถูกส่งต่อเป็น "เสียงผู้ใช้" (ผ่าน AGC สูงสุด x8)
   → `_replyHeld`: ขอบเริ่ม/จบของคำตอบ (half duplex + หางเสียง) แยกจาก gate — เริ่มตอบ = `endUtterance` + ล้าง preroll, จบ = ล้างบัฟเฟอร์พูดแทรก
   (ถ้าไม่ได้แทรก) + ตั้งช่วงฟังต่อ · `endUtterance` ขณะคำตอบกำลังเล่นไม่ตั้ง "รอ AI ตอบ" อีก (`[Mic] ... -> AI กำลังตอบ`)
2. **WakeWord ตั้ง `_detected` ก่อนเขียน `_lastWord` / `_lastDetectDb`** — agent (priority 5, core เดียวกัน) อาจอ่านคำ/ระดับเสียงของครั้งก่อน
   แล้วตัดสินการพูดแทรกผิด (เสียง "Jarvis" ของ AI เองหยุดคำตอบได้) → เขียนค่าก่อน + memory barrier แล้วค่อยตั้งธง
3. **GeminiLiveClient: opcode สงวน 0x3–0x7** ไม่ใช่ data และไม่ผ่านการเช็ก control ≤125 ไบต์ → payload (ถึง 1 MB) ถูกคัดลอกลง `ctrl[128]` บน stack
   → ตัดการเชื่อมต่อทันทีตาม RFC 6455 (`bad frame header (reserved opcode)`)
4. **`get_device_status` รายงาน `speaker_volume` = เสียง AI (ล็อก 80)** ทั้งที่ `set_volume` ปรับแค่วิทยุ (T068) → `radio_volume` + `voice_volume_locked`
5. **อุปกรณ์ (wiki 15):** `poll()` อ่านรายการโดยไม่ล็อกขณะ `devices reload/save` ทาง console เขียนใหม่ → poll ใช้ `xSemaphoreTake(_lock, 0)` (ข้ามรอบถ้าไม่ว่าง)
   + คัดลอกชื่อก่อนเรียก callback นอกล็อก · ชื่อบางส่วนที่ตรงหลายเครื่อง ("ไฟ" กับ "ไฟโต๊ะ"/"ไฟห้องนอน") เคยเลือกตัวแรก = สั่ง relay ผิดตัว
   → `periph::findByName` ตอบ "several devices match" (host test `device_config`)
6. **consolidation ส่งบทสนทนาเก่าซ้ำทุกบูต** — `_consolidatedUntil` เริ่ม 0 → 24 เทิร์นเก่าถูกส่งให้ Flash-Lite อีกรอบ และความจำที่ลบด้วย `forget_memory`
   อาจกลับมา → เก็บใน NVS `rt/consol`
7. **`AsyncFileSink::idle()` เป็นจริงตั้งแต่คิวว่าง** ขณะบรรทัดสุดท้ายยังเขียนอยู่ → `memclear` (compact: ลบ + rename) ชนกับการเขียนนั้นได้ → ธง `_writing`
   (peek ก่อน receive จึงไม่มีช่วงที่คิวว่างแต่ธงยังไม่ตั้ง)
8. **คิว web tool เต็ม → ตอบโมเดลว่า `unknown function`** (โมเดลเลิกใช้ tool นั้นทั้ง session) → ตอบ `busy ... try again`
9. **`price_alert remove` ไม่ใส่ id = ลบทั้งหมด** (โมเดลส่ง symbol แทน id ก็หายหมด) → ต้องมี id, `id = -1` = ลบทั้งหมด
10. **ประสิทธิภาพ:** ผลของ web tool เดิม + arguments เดิม ตอบจากที่จำไว้ (4 ช่อง, PSRAM, ไม่จำ error): อากาศ 10 นาที, น้ำมัน/ปฏิทิน 30 นาที,
    ข่าว 5 นาที, Wikipedia 1 ชม., ราคา/แลกเงิน/ภาพรวมตลาด 1 นาที — `get_weather` ซ้ำไม่ต้อง TLS handshake 2–3 ครั้ง (`[Tool] ... answered from the last N s`)
11. เล็กน้อย: warning `-Wvolatile` ใน `AsyncFileSink.h` / `AudioManager.h`
**ยังไม่ได้ทำ (ความเสี่ยงที่บันทึกไว้):** OTA ยังไม่มีลายเซ็น (บัญชี GitHub = เฟิร์มแวร์ — แนะนำ Ed25519 ฝัง public key) • legacy I2S driver
(`driver/i2s.h`) จะหายใน IDF 6 • SecretBox: ถ้า eFuse UID เป็นศูนย์ กุญแจมาจาก MAC อย่างเดียว (MAC ส่งออกอากาศ) — การ์ด SD หลุดอย่างเดียวพอถอดรหัส
**ทดสอบ:** `pio run` ผ่าน (RAM 31.8%, Flash 64.6%) · host tests 41 ชุด / 1,695 checks ✓
**ทดสอบบนบอร์ด (2026-10-17, แฟลช COM4, `wakeMode` 2, 3.8 Live):**
- ข้อ 1: หลังทุกคำตอบขึ้น `[Mic] ฟังต่อ 6 วิ หลัง AI ตอบ` (ก่อนแก้ในโหมดคำปลุกไม่เคยขึ้น) · ผู้ใช้พูด "สภาพอากาศวันนี้" ในช่วงนั้น → ส่งไป → `get_weather` + คำตอบ ✅
  · ไม่มี "AI ไม่ตอบ" หลังคำตอบใดๆ ในการทดสอบ 4 รอบ — ส่วนกรณีพูดซ้อนตอน AI เริ่มตอบสร้างด้วยการพิมพ์ไม่ได้ ต้องทดสอบด้วยเสียงอีกครั้ง
- แคช: `tool get_weather {"location":"เชียงใหม่"}` 11.8 วิ → ซ้ำใน 28 วิ = 0.0 วิ (`answered from the last 28 s`) · ภูเก็ตดึงใหม่ ✅
  · AI ส่ง arguments ต่างจาก console (`location_en` + `location`) = คนละ key → ดึงใหม่ (ตามที่ออกแบบ)
- **พบ (มีอยู่ก่อน ไม่เกี่ยวกับการแก้นี้):** ถามอากาศ (web tool + 3.8 Live) → **AI ตอบข้อความเดิม 2 รอบ** — ทดสอบแบบปิดไมค์ (`mute`, ไม่มีช่วงฟังต่อ)
  ก็ยังซ้ำ → **แก้แล้วใน T071** (`scheduling: SILENT` ของ `set_expression` อยู่ใน `response` แทนที่จะอยู่ข้างๆ → WHEN_IDLE)
- **RAM ภายในต่ำสุด 10 KB** (`[Health] ... min 10 KB`) ระหว่างดึงอากาศขณะ session เปิด + คำปลุก 2 คำ · ครั้งหนึ่ง TLS ต่อไม่ได้ (`start_ssl_client: connect failed`, อากาศใช้ 21.6 วิ)
  → ใช้คำปลุกคำเดียว (`set wakeword hiesp`) คืน ~16 KB

## T068: แยกความดังเสียง AI กับวิทยุ — AI ล็อก 80%, ปรับเสียง = วิทยุอย่างเดียว

**วันที่:** 2026-10-17 · **ผู้ใช้:** "เสียง AI ล็อกที่ 80% เมื่อปรับลดเสียงวิทยุ เสียง AI เท่าเดิม ลดเฉพาะวิทยุ"
- เดิม: เสียง AI + วิทยุผสมกันแล้วผ่าน volume ของ codec (ES8311 REG32) ตัวเดียว
- ตอนนี้: codec = `aiVolume` (NVS `aivol`, ค่าเริ่มต้น **80**, `set aivolume`, Studio "ความดังเสียง AI") · วิทยุ = gain ดิจิทัลตอนผสม
  (`AudioManager::setRadioVolume`, สเกลเดียวกับ codec 0.5 dB/ขั้น เทียบกับ codec — วิทยุ 70 ดังเท่าระดับเสียง 70 แบบเดิม, ไม่ดังเกินเสียง AI)
  ramp พร้อมการหรี่วิทยุ (ไม่มีเสียงคลิก) · `volume` (เมนู "เสียงวิทยุ", ปุ่ม +/- หน้าจอวิทยุ, Studio "ความดังวิทยุ", `set_volume`) = วิทยุ
- เสียงอ้างอิงของ AEC วัดหลังการผสม → ตามระดับวิทยุเอง ไม่ต้อง scale · เอาเสียง "ติ๊ง" ยืนยันออก (เล่นที่ระดับเสียง AI ไม่ใช่วิทยุ)
- ทดสอบ: `set_volume` 80/40/10 → codec คงที่ 80, `radio_out_level` 0.0198 / 0.000063 / ~0 · tool declarations 14,931/15,000 bytes

## T067: เฟส 3A — พูดแทรก AI ด้วยคำปลุก (และกัน AI ขัดจังหวะตัวเอง)

**วันที่:** 2026-10-17 · ผู้ใช้เลือกแบบ A (คำปลุก) · half duplex ยังอยู่ (เสียง AI ไม่ไปถึง Gemini) แต่ระหว่าง AI พูด ไมค์เปิดให้ WakeNet หลัง AEC
- จับคำปลุกได้ → `bargeInNow()`: `clearPlayback` + `_dropAudio` (เหมือนแตะจอ) + ฟังต่อ 6 วิทันที (`_followUp`) — เซิร์ฟเวอร์ยกเลิกคำตอบเก่าเองเมื่อได้ยินผู้ใช้
- ทดสอบครั้งแรก: ให้ AI อ่าน "Hi ESP ... Jarvis ..." → **Jarvis จับเสียง AI เอง** (เศษเสียงสะท้อน −63 dBFS; โมเดล TTS + AI พูดชื่อ "จาวิส" บ่อย)
  → ตอน AI พูด: มี 2 คำแทรกได้แค่ "Hi ESP" + ต้อง ≥ −50 dBFS · log `[Mic] ไม่นับพูดแทรก ...` เมื่อถูกกรองทิ้ง
- ทดสอบซ้ำ: อ่านข้อความที่มีคำปลุก 6 ครั้ง + นิทาน 30 วิ → ไม่หยุดตัวเอง (`heard` 0)
- **ผู้ใช้ทดสอบ:** Jarvis −64 dBFS (เสียง AI) ถูกกรองทิ้ง ✅ · "Hi ESP" −42 dBFS หยุดคำตอบ ✅ · แต่**คำสั่งต่อจากนั้นหาย** (`หมดช่วงฟังต่อ (ไม่มีเสียงพูด)`):
  WakeNet ยืนยันคำ ~0.5–1 วิหลังพูดจบ + หางเสียง 0.35 วิ ช่วงนั้นเสียงไปแค่ WakeNet → "Hi ESP ราคาทอง..." ในประโยคเดียวหายทั้งท่อน
  → ระหว่าง AI พูดเก็บเสียง (หลัง AEC) 1.5 วิไว้ใน `_preroll` · แทรกแล้วทำแบบปลุกด้วยเสียง (`_wakeBuffering/_wakeHeard`) = ส่งที่เก็บไว้ + ที่ตามมา
  · ทดสอบ: ยังไม่หยุดตัวเอง · ผู้ใช้รอบแรกเน็ตหลุดพอดี (อัปโหลด 6–26 KB/s, `Send failed`)
- **ผู้ใช้ยืนยัน (12:02):** นิทานลูกหมู → "Hi ESP" (−42 dBFS) → หยุดทันที → Gemini ได้ยิน `Hi, ESP.` → ขานรับ "ไฮค่ะ บอส มีอะไรให้ช่วยไหมคะ?" ✅

## T066: ใช้ 2 คำพร้อมกัน + เรียกคำปลุกแล้ว AI "พักหน้าจอ" ซ้ำทุกครั้ง

**วันที่:** 2026-10-17
- **2 คำพร้อมกัน** (`set wakeword both` = Hi ESP + Jarvis, WakeNet 2 ตัวใน AFE เดียว): RAM ภายในของ AFE 23 → 39 KB, บูต 56 KB, ต่ำสุดขณะวิทยุ 35 KB,
  dropped 0 · ผู้ใช้เรียกทั้งสองคำ: ปลุกติดทุกครั้งและบอกถูกว่าคำไหน (`[Wake] heard "<model>"`)
  · ข้อควรระวัง: ที่ 0.5 สองโมเดลจับข้ามคำกันได้ และ **Jarvis ปลุกเองจากวิทยุ** 1 ครั้งในการทดสอบ (−58 dBFS, ไม่มีคนเรียก) — ค่าเริ่มต้นยังเป็น "Hi ESP" คำเดียว
- **"พักหน้าจอ" ครั้งเดียว → ทุกครั้งที่เรียกแค่คำปลุก AI เรียก `rest_screen` อีก** (6 ครั้งติด): ตื่นแล้ว session ถูก resume พร้อมคำสั่งล่าสุด
  → Gemini ได้ยินแค่ "Jarvis" ก็ทำคำสั่งเดิม แม้ prompt ห้าม · แก้แบบเดียวกับวิทยุ: `requestSleep()` (go_to_sleep, rest_screen) ตั้ง `_freshNextWake`
  → ตื่นครั้งถัดไปเริ่ม session ใหม่ (หลับเพราะเงียบนานยัง resume ตามเดิม) · ทดสอบ: พักหน้าจอ → ตื่น → "จาวิส" ×2 = `new session`, ไม่มี `rest_screen`
- **ยังซ้ำ:** หลับเพราะเงียบ (วิทยุเล่น) แล้วตื่น → resume → `get_weather` ซ้ำ → **ตื่นทุกครั้งเริ่ม session ใหม่** (`enterSleep` ล้าง handle;
  เน็ตหลุดยัง resume) · ทดสอบ: ถามอากาศ → หลับเอง → ตื่น → "จาวิส" ×2 = `new session`, ไม่มี `get_weather`
- `loop held ~1.3 s`: log ใหม่ `[Mic] slow chunk 1332 ms: echo 1332` = agent ถูกแย่ง CPU กลางการตัดเสียงสะท้อน ตรงช่วง `Connecting...`→`Connected`
  = TLS handshake ของ `geminiIO` (priority 5, ไม่ผูก core) → agent priority 4 → **5** (time-slice เท่ากัน) · ทดสอบปลุก 3 ครั้งขณะวิทยุ: ไม่มี slow chunk
- **"ปิดวิทยุ" แค่หยุด** (ผู้ใช้): `stop_radio` หยุดสตรีมแต่หน้าจอวิทยุค้างที่สถานะหยุดชั่วคราว → `RadioScreen::requestClose()` = ปิดหน้าจอกลับหน้าหลักเหมือนปุ่ม ⏻
  · ทดสอบ: `tool stop_radio` และ `ask ปิดวิทยุ` → `[Radio] Screen closed (radio turned off)`

## T065: ปลุกด้วย "Hi ESP" ได้ แต่ลูปค้าง 10 วิ และ 45 วิ หลังตื่น

**วันที่:** 2026-10-17 · log จาก ESP32-AI Studio: ทุกบรรทัดตั้งแต่บูตลงเวลา `00:41:19` เท่ากันหมด
(= Studio ไม่ได้อ่าน serial ช่วงหนึ่ง แล้วรับมาทีเดียว) พร้อม `loop held 10045 ms (audioIn)` และ `45344 ms (sleep 35338 ms)`
- `Serial.setTxTimeoutMs(500)`: เมื่อโปรแกรมฝั่ง PC ถือพอร์ตไว้แต่ไม่อ่าน log ทุกบรรทัดรอ 500 ms → ลูป agent ค้าง
  (ทดลองปิดพอร์ต / เปิดค้างไม่อ่าน 40 วิ บน Windows ไม่เกิด — driver รับไว้ให้; ในเคสของผู้ใช้ Studio ไม่ได้อ่านนานกว่านั้น)
  → **20 ms**: ไม่มีคนอ่าน = บรรทัด log หาย แต่ผู้ช่วยไม่ค้าง · smoke test: คำตอบ tagged (OTA/TOOL/DIAG) ยังมาครบ
- **`loop held ~1.34 s (audioIn)` ทุกครั้งที่ตื่น** (1338/1339/1346 ms — คงที่ ไม่ใช่เน็ต): task AFE + `wakefeed` + `wakeword` อยู่ core 1
  priority 5 สูงกว่า agent (4) → ตอนจับคำได้ (ล้างบัฟเฟอร์ AFE) แย่ง CPU agent → priority **3** · วิทยุตอนหลับ 2 นาที: dropped 0, ลูปสูงสุด 9–12 ms
  · ผู้ใช้ยืนยัน: "Hi ESP" ปลุกจากโหมดนาฬิกาได้ ไม่มี `loop held` หลังตื่น, AI แค่ขานรับ
- ในรอบเดียวกัน: "Hi ESP" ปลุกติดขณะวิทยุเล่น 2 ครั้ง (−39 และ −57 dBFS) · AI ได้ยินคำปลุกเป็น "High ESP" / "Hi, it's P." แล้วขานรับ

## T064: คำปลุกไม่ติดกับเสียงจริง — "Jarvis" เป็นโมเดล TTS, ค่าความไว, AEC ของ AFE ช้าหลังเปิดวิทยุ

**วันที่:** 2026-10-17 · ทดสอบแบบนำทางบนจอ (`say s ...`) พูดช่วงละ 3 ครั้ง, `wakeword` วัดผล
| ทดสอบ | ผล |
|---|---|
| `wn9_jarvis_tts` ความไว 0.63: เงียบ "จาวิส" / "Jarvis" / วิทยุ "จาวิส" / ไว 0.5 "จาวิส" | 0 / 0 / 0 / 0 (เสียงถึง −19 dBFS) · วิทยุ "Jarvis" 1 |
| `wn9_hiesp` 0.63: เงียบ / วิทยุ | 0 / 0 |
| `wn9_hiesp` **0.5** วิทยุ (เล่นมาแล้ว > 1 นาที) | **3/3** |
| `wn9_hiesp` 0.5 วิทยุ**เพิ่งเปิด** (AEC ของ AFE) | 0/3 — `last_dbfs` −21 (ยังตัดวิทยุไม่หมด) |
| `wn9_hiesp` 0.5 + **AEC ของเราเอง** ("M"): เงียบ / วิทยุเพิ่งเปิด / เพิ่งเปลี่ยนสถานี | 1/3 / **2/3** / **3/3** |
- "Jarvis" ฝึกจากเสียงสังเคราะห์ (จับเสียง Gemini ได้ 8/8 แต่ไม่จับคนไทย) → ค่าเริ่มต้น **"Hi ESP"** (ฝึกจากคนจริง, `set wakeword hiesp|jarvis` มีผลหลังรีบูต)
- ความไว 0.63 ของโมเดลสูงไปกับสำเนียงไทย → `wakeThreshold` (NVS `wakethr`) ค่าเริ่มต้น **0.50** · `set wakethreshold 0.4–0.99`
- AEC ของ AFE ใช้เวลา ~1 นาทีหลังสตรีมเริ่ม → **WakeNet รับเสียงหลัง AEC ของเรา** (`WAKE_WORD_OWN_AEC 1`, AFE "M", ไม่มี AEC ซ้ำ) — RAM ภายในของ AFE 36 → **23 KB**
- "Hi ESP" ไปถึง Gemini ด้วย → AI เปลี่ยนสถานีวิทยุเอง → prompt "การเรียกชื่อ" รวมคำปลุกที่ตั้งไว้ (ห้ามเปิด/เปลี่ยนวิทยุ)
- **เรียกครั้งเดียว ขึ้นซ้ำ 4 รอบ** (ผู้ใช้): ที่ 0.5 คำเดียวกันถูกจับหลายเฟรม + เสียงค้างคิว 0.5 วิ → ไม่นับซ้ำ 2 วิหลังจับได้ + ทิ้งคิว (ทดสอบ: ตื่น 1 ครั้งต่อการเรียก)
- **ตื่นแล้วหลับทันที:** `sleep` ที่สั่งตอนหลับอยู่ค้างเป็น `_sleepPending` → `wake()` ล้างทิ้ง · รอบทดสอบนี้จับได้ 2/3 (เงียบ)
- ยังเปิดอยู่: `loop held ~1.3 s (audioIn)` เกิดบางครั้งตอนตื่น (ก่อนมีคำปลุกก็เคยเห็น) · ผู้ใช้เปิดวิทยุทิ้งไว้นาน: ไม่ตื่นเอง ✅

## T063: โหมด "คำปลุก" เรียก "จาวิส" (ใกล้มาก) ไม่มีอะไรเกิดขึ้น

**วันที่:** 2026-10-16 · **ผู้ใช้:** ตั้ง "ปลุกด้วยเสียง" = คำปลุก แล้วเรียกไม่ติด แม้พูดใกล้มาก
- **โหมดนาฬิกา (power save) ปิดไมค์** ตามที่ออกแบบไว้ (T051) — หลับครบ `clock` นาทีแล้วไมค์ไม่รับเสียงเลย คำปลุกจึงไม่มีทางได้ยิน
  → โหมด "คำปลุกอย่างเดียว" เปิดไมค์ในโหมดนาฬิกาด้วย (ปลุกได้แค่คำปลุก ระดับเสียงไม่ปลุก) · โหมดอื่นยังปิดไมค์เหมือนเดิม
- เครื่องมือใหม่: `wakeword` (สถานะ, `peak_dbfs` = เสียงดังสุดที่ WakeNet ได้), `wakeword threshold 0.4–0.9999` (ไม่บันทึกถาวร),
  `wakeword test [วิ]` = ให้คำปลุกฟังลำโพงด้วย (ไม่ปิดไมค์ตอนพูด, ไม่มี reference) แล้ว `ask` ให้ AI พูด "Jarvis" · diag `wake_word_peak_dbfs`
- **self-test บนบอร์ด:** AI พูด "Jarvis" 4 ครั้ง → จับได้ 4/4, "Hey Jarvis" 4/4 (peak −25 dBFS, dropped 0) = AFE + WakeNet + การป้อนเสียงใช้ได้
  → ถ้าผู้ใช้ยังเรียกไม่ติด เหลือเรื่องการออกเสียง ("จาวิส" ไม่มีเสียง r ของ "Jarvis") / ระดับเสียงที่ระยะจริง — ดูจาก `peak_dbfs` แล้วลด threshold

## T062: เรียก "จาวิส" แล้ว AI มั่ว — ค้นหาคำว่า "jawit" / เปิดวิทยุเอง + ลูปค้าง 1.3 วิ

**วันที่:** 2026-10-16 · **ผู้ใช้:** "มันยังมั่วๆหรือเปล่า" (log)
- ไม่มี `[Wake] heard` เลย = ที่ตื่นคือ**ระดับเสียง** (โหมด "ทั้งสองแบบ") — WakeNet ฝึกจาก "Jarvis" แบบอังกฤษ ส่วนผู้ใช้เรียก "จาวิส"
- Gemini ได้ยินแค่ชื่อ → `search_knowledge "jawit"`, "จาวิส คือใคร" · อีกครั้ง (session ที่ resume) เปิดวิทยุ
  → prompt "การเรียกชื่อ": ชื่ออย่างเดียว/คำทักทาย = ขานรับสั้นๆ ห้าม tool ห้ามค้นชื่อ ห้ามทำคำสั่งเก่า (ทดสอบ `ask จาวิส`, `ask jawit`: ไม่เรียก tool)
- `loop held 1346 ms (slowest: audioIn)`: `feed()` ของ AFE **บล็อก**เมื่อ ring เต็ม (AFE บน core 0 แย่ง CPU กับตัวถอด MP3 + เสียง + Wi-Fi) แล้วมันถูกเรียกในลูป agent
  → `WakeWord::feed()` ไม่บล็อก: ใส่ stream buffer ใน PSRAM (เต็ม = ทิ้ง, diag `wake_word_dropped`) + task `wakefeed` ป้อน AFE · AFE ย้ายไป core 1
  → วิทยุ 2.5 นาทีตอนหลับ: ลูปสูงสุด 54–57 ms, dropped 0, RAM ภายในต่ำสุด 33 KB

## T061: แฟลชเฟิร์มแวร์คำปลุกแล้วจอดับ ไฟเขียวติด — RAM ภายในหมด

**วันที่:** 2026-10-16 · **ผู้ใช้:** "หน้าจอบอร์ดดับ แต่ไฟเขียวติด"
1. การแฟลชครั้งแรกหลุดกลางทาง: `pio run -t upload` ใน Git Bash ที่ redirect output → `UnicodeEncodeError: 'gbk'` ตอนพิมพ์แถบความคืบหน้า (█░) → เฟิร์มแวร์ไม่ครบ
   → แฟลชจาก PowerShell + `PYTHONIOENCODING=utf-8`
2. แฟลชครบแล้วยังใช้ไม่ได้: log `[Wake] ... ready` **2 ครั้ง** (เรียก `_wakeWord.begin()` ซ้ำ) → AFE 2 ชุด 36 + 29 KB → RAM ภายในเหลือ 7 → 0 KB → Wi-Fi reason 201/36, วิทยุ `Giving up`
   → `begin()` กันเรียกซ้ำ · ไม่เปิดคำปลุกถ้าเหลือ < 50 KB · หา RAM คืน ~20 KB (ดู [[14-AEC-WakeWord-Plan]] 2a) → ต่ำสุด 34 KB
3. `srmodels.bin` ไม่ถูกแฟลช: builder คัดลอก `FLASH_EXTRA_IMAGES` ไปเป็นคำสั่ง esptool ก่อน post script → `tools/pio_sr_model.py` เติม `UPLOADERFLAGS` เองด้วย

## T060: ย้ายแกนไป Arduino 3.3.12 / IDF 5.5 (เฟส 1) — RAM ภายในหาย 35 KB, หยุดวิทยุแล้วตื่นเอง, สตรีมค้างตอนเริ่ม

**วันที่:** 2026-10-16 · branch `feature/esp-sr` · แผน [[14-AEC-WakeWord-Plan]] เฟส 1
- **compile:** แก้แค่ 2 จุด — mbedtls 3 (`mbedtls_sha256_*_ret` → `mbedtls_sha256_*` ใน SecretBox, OtaUpdater) · `esp_efuse_mac_get_default` ต้อง `#include <esp_mac.h>`
  (ที่เหลือเป็น warning: I2S driver เก่า deprecated, `volatile ++` ของ C++20)
- **RAM ภายในหาย ~35 KB:** ตอนบูต 66 KB (IDF 4: 101–109), ต่ำสุด 22 KB ตอน TLS 2 ช่อง (IDF 4: 65–82) — ระบบของ IDF 5 กินมากขึ้น
  (config เครือข่าย/mbedtls เท่าเดิม: เทียบ sdkconfig แล้ว) → `system/PsramTask.h`: stack ของ `geminiIO` 10 KB, `textModel` 8 KB, `radio` 16 KB อยู่ใน PSRAM
  (`xTaskCreatePinnedToCoreWithCaps`; เฉพาะ task ที่**ไม่เขียน flash เอง** — `webTools` เขียนไฟล์ได้ถ้าไม่มี SD จึงคงไว้) → บูต **102 KB**, ใช้งาน 88–101, ต่ำสุด 59–83
  · คำสั่งใหม่ `mem` = RAM ภายใน + stack ทุก task (อยู่ไหน, เหลือเท่าไร)
- **หยุดวิทยุ / เปลี่ยนสถานีตอนหลับ → "Waking up (voice)" ทุกครั้ง** (เจอบน IDF 5 แต่เป็นบัคของ v3.5.1): ตอนไม่มีเสียงเล่น `echo::Canceller` ส่งไมค์ดิบ
  (มีเสียงฮัม < 250 Hz) แต่ตอนวิทยุเล่นระดับวัดหลัง high-pass 250 Hz → พอหยุด ระดับกระโดดข้าม noise floor ที่เรียนตอนวิทยุ
  → VAD วัดระดับในย่าน high-pass เสมอ (`passthrough()` → `micRms()`) — เสียงที่ส่งให้ Gemini ไม่เปลี่ยน · จบ radio session = settle 1.5 วิ
  · ทดสอบ: เปลี่ยนสถานี 6 ครั้ง + หยุด 2 ครั้งตอนหลับ = ไม่ตื่นเอง · host test เสียงฮัม 50 Hz
- **วิทยุ "stream stalled" 12 วิแรก (3 ใน ~20 ครั้ง, เฉพาะ rstream.mcot.net):** log ใหม่ `[Radio] Stalled:` บอก **ได้ 5760 ไบต์ (= TCP window 1 รอบ)
  แล้วเงียบ socket ยังเปิด** — window update หาย/เซิร์ฟเวอร์ค้าง (TCP window 5760 เท่ากับ IDF 4) → ต่อใหม่เร็วขึ้น: ช่วงเริ่ม (< 16 KB) เงียบ 2 วิ, หลังจากนั้น 5 วิ
  (เดิม 12 วิ > บัฟเฟอร์ 4 วิ = เงียบ 8 วิ) · สตรีมที่เล่นมา > 10 วิแล้วหลุด ต่อใหม่ใน 0.3 วิ
- **ผลทดสอบบนบอร์ด:** host tests 40 ชุด / 1,667 ✓ · Gemini Live (setup/resume/tool) ✓ · tools HTTPS (อากาศ, ทอง, recall) ✓ · OTA check (GitHub TLS) ✓ ·
  วิทยุ MP3 http/https + AEC ERLE 10–31 dB ✓ · ไม่รีบูต

## T059: เรียกจาก 20–30 cm ปลุกติดแต่ AI ไม่ทำตามคำสั่ง

**วันที่:** 2026-10-16 · **ผู้ใช้:** "ไม่ตอบสนองเสียงวิทยุ แยกเสียงคนได้ · พูด 20–30 cm ถูกกระตุ้น หรี่วิทยุ ขึ้นสถานะฟัง แต่ไม่ทำตามคำสั่ง ต้องพูด 10 cm"
- log: ทุกครั้ง `เสียงสั้นเกินไป 0.0 วิ` แล้ว `Sleeping` ~1 วิต่อมา — คำที่พูดตอนกำลังต่อ session (3–4 วิ) อยู่ใน wake buffer แต่**ไม่ถูกนับเป็นเสียงพูด**
  → ประโยค "สั้นเกินไป" ไม่รอคำตอบ → กติกาวิทยุ (ว่าง 6 วิ → พัก) ปิด session ก่อน Gemini ตอบ · ระยะ 10 cm ได้เพราะเสียงดังพอจะพูดต่อหลัง session พร้อม
  → นับ chunk ที่มีเสียงใน wake buffer (`_wakeVoiced`) เข้าประโยค · จบประโยคที่มีเสียงพูด = `_awaitingReply` + `noteActivity` (ปลดเมื่อได้คำตอบ / ครบเวลา `checkNoReply`)
- คำสั่งหาย 0.5–1 วิทุกครั้ง: เน็ตอัปโหลด ~33 KB/s ตัด backlog ที่ 2 วิ (T058) → ไม่ต่อท้ายความเงียบตอนยังส่ง backlog (ต่อเฉพาะเสียงพูด + ช่องว่างสั้น < 300 ms) จึงไม่โตไม่สิ้นสุด
  → ขยายเพดานเป็น 6 วิ และเลิกรอหลัง hangover 8 วิ
**ทดสอบ:** วิทยุ micgain 5 ไม่มี bypass/reset · การเรียกจริงต้องให้ผู้ใช้ทดสอบ

## T058: ปลุกได้แต่ต้องพูดใกล้ + micgain สูงแล้ววิทยุปลุกเอง + วิทยุหรี่ค้าง

**วันที่:** 2026-10-16 · **ผู้ใช้:** "เรียกชื่อได้ยินถูกทุกครั้ง แต่ต้องพูด < 10 cm (micgain 4) · micgain 5–6 ได้ 20–30 cm แต่ชนกับเสียงวิทยุ AI ไม่ตอบวิทยุ แต่หรี่วิทยุแล้วไม่เพิ่มกลับ"
1. **วิทยุหรี่ค้าง (บัคจาก T053):** หลังปลุก ไมค์ส่งไม่หยุด (`ส่ง 115.8 วิ`) — คิวส่งเต็มตลอด (เน็ตอัปโหลดวัดได้ **26–48 KB/s** แต่เสียง 16 kHz ต้อง ~45) backlog ของ preroll ไม่เคยหมด
   → ประโยคจบไม่ได้ → ไม่หลับ → วิทยุหรี่ค้าง · แก้: backlog ≤ 2 วิ (ทิ้งเก่าสุด) + จบประโยคได้เมื่อเลยช่วง hangover 2 วิ · log `[Mic] อัปโหลดได้ X KB/s ... ทิ้งเสียงค้างส่ง`
2. **เปลี่ยน micgain แล้ว AEC พัง:** ADC 6 dB/step (วัด: tone 0.0035 → 0.0070 → 0.014 → 0.028 → 0.056) แต่ filter ไม่รู้ → `scale(2^Δ)` + settle เมื่อเปลี่ยน
3. **ตรวจ "คนพูดแทรก" ด้วยเกณฑ์ตายตัว 0.008:** ที่ micgain 6 เสียงสะท้อน 0.06–0.17 หลังตัด 20 dB ยังเกิน → ถูกนับเป็นคนพูด → filter แช่ → หลุด → `kDtEchoRatio` (ต้อง ≥ 30 % ของเสียงสะท้อนที่คาด)
4. **ต้นเหตุจริงของ AEC ไม่นิ่ง = เสียงเบส:** replay ไฟล์ `recref` ผ่านโค้ด `echo::Canceller` ตัวจริงบน PC (`aec_replay`) → ทุก bypass คือจังหวะกลอง/เบส: อ้างอิงดัง 0.2–0.4 แต่ไมค์ได้ยินแค่ 0.003–0.009
   — ลำโพงเล็กเล่นเบสไม่ออก (`radiotone` ใหม่: 200 Hz ถึงไมค์ 0.5 % ของอ้างอิง, 1 kHz 27 %) filter ทาย echo เกิน → **high-pass 250 Hz ทั้งไมค์และอ้างอิงก่อน AEC**
   → replay: คนพูดแทรก/bypass/reset 16/9/2 → **0/0/0**, ยังตัดได้ 20–33 dB
5. เกณฑ์ปลุกขณะวิทยุ: เทียบกับ**เสียงสะท้อนที่ตัดออกใน chunk เดียวกัน** (`AEC_WAKE_ECHO_RATIO` 0.8) + 6 ใน 8 chunk + ไม่นับ chunk ที่ bypass/clip (ทั้งตอนหลับและตอน AI ตื่น) + ขั้นต่ำ 0.006
   (0.4 = ปลุกเอง 1 ครั้ง/5.5 นาที, 0.8 = ไม่มี)
6. **เข้าใจผิดระหว่างทดสอบ:** ผู้ใช้ตั้ง `clock 3` → หลับ 3 นาทีเข้าโหมดนาฬิกา = **ไมค์ปิด** (ตามที่ตั้งใจใน T051) → ปลุกด้วยเสียงไม่ได้ ต้องแตะ
**ผลบนบอร์ด (วิทยุ ระดับ 70, ห้องเงียบ 0.0007):** micgain 5 4 นาที + micgain **6** 1.5 นาที **ไม่ตื่นเอง** ไม่มี reset · เครื่องมือใหม่: `recref`, `radiotone`, diag `aec_*`
· **micgain 6 ใช้ได้แล้ว** (ระยะ 20–30 cm) · ยังต้องให้ผู้ใช้ทดสอบพูดปลุกจริง

## T057: พูดปลุกขณะเปิดวิทยุไม่ติด — เปลี่ยนจากเทียบความดังเป็นตัดเสียงสะท้อน (NLMS)

**วันที่:** 2026-10-16 · **ผู้ใช้:** "มันยังไม่ทำงาน ยังคงต้องแตะที่หน้าจอถึงจะทำงาน" (log: พัก + วิทยุ 12:30–12:31 ไม่มีการตรวจผ่านเลยสักครั้ง)
- **สาเหตุ:** เสียงพูดที่ไมค์ 0.04–0.09 แต่เสียงวิทยุรั่วเข้าไมค์ (ระดับ 70) ~0.03–0.04 → เกณฑ์ T056 ต้องดังกว่า 3 เท่า (~0.1+) ต่อเนื่อง 8 chunk
  (คำไทยมีช่วงหยุดระหว่างพยางค์) → **การเทียบความดังใช้ไม่ได้กับเสียงพูดปกติ**
- **วัด:** คำสั่งใหม่ `recref <วิ>` อัดไมค์ + เสียงอ้างอิง (RECPAIRS) ตอนวิทยุเล่น 15 วิ → จำลองบน PC: เสียงสะท้อนมาหลังอ้างอิง 1.4 ms,
  NLMS 64 tap ตัดได้ 18 dB, **128 tap (8 ms) ตัดได้ 27 dB** (เหลือ ≤ 0.0036 ต่อ chunk), 256/512 tap ไม่ดีขึ้น → เสียงส่วนใหญ่ผ่านตัวบอร์ดเอง
- **ทำ:** `echo::Canceller` (`util/EchoCanceller.h`, PSRAM, host tested): out = mic − w·ref ทุก chunk (ไม่มีเสียงเล่น = ผ่านตรง) ·
  ปรับ w เฉพาะตอนเล่นและไม่ใช่คนพูดแทรก (pass แรกแช่ w วัด residual; > 3× floor และ > 0.008 = มีคนพูด) · `force` ช่วง 2.5 วิแรก ·
  พูดแทรก 4 วิติด = ทางเสียงเปลี่ยน → เรียนใหม่ · เปลี่ยนระดับเสียง = คูณ w ตาม dB · **ไม่มีวันทำให้แย่กว่าไมค์:** residual > 1.25× ไมค์ = ส่งไมค์ตรง, > 2× ซ้ำ 3 ครั้ง = เริ่มใหม่
  → การปลุก / VAD / เสียงที่ส่งให้ Gemini ใช้เสียงหลังตัดแล้ว (เสียงดีเจตอน AI ตื่นก็ถูกตัดออกด้วย) · ปลุกเมื่อ residual > `AEC_WAKE_MIN_LEVEL` 0.012
- **บนบอร์ด รอบ 1:** ตัดได้ 16–29 dB แต่ใช้ 10.5 ms ต่อ chunk 40 ms (26 % ของ core) → O3 + `dsps_dotprod_f32_ae32` (esp-dsp) = 7.8 ms
- **รอบ 2:** ตื่นเอง 2 ครั้งโดย residual > ไมค์ (0.024 vs 0.004) — ผลรวมพลังงานแบบบวก/ลบสะสมคลาดหลังเพลงดังหลายนาที เกินพลังงานจริงในช่วงเบา
  → step ใหญ่มาก → filter diverge → นับพลังงานใหม่ทุก 128 sample + พื้นขั้นต่ำ (regularization) + กันทำให้แย่กว่าไมค์
- **รอบ 3–4:** ปรับเกณฑ์ reset (เฉพาะ > 2× ไมค์) และ μ 0.5 → 0.3 → **วิทยุ 4.5 นาที ไม่ตื่นเอง ไม่ต้อง bypass ตัดได้ 14–42 dB เหลือ 0.0003–0.0005**
- diag: `aec_erle_db`, `aec_residual`, `aec_double_talk`, `aec_max_us`, `aec_bypass`, `aec_resets` · **การพูดปลุกจริงต้องให้ผู้ใช้ทดสอบ**

## T056: ปลุกด้วยเสียงขณะเปิดวิทยุ — ใช้เสียงอ้างอิงจาก ES8311 (เฟส 0 ของแผน AEC)

**วันที่:** 2026-10-16 · ผู้ใช้สั่งเริ่มเฟส 0 ของ [[14-AEC-WakeWord-Plan]]
- **วัด:** reg 0x44 = 0x50 → L ไมค์, R DAC loopback (wiki 01) · อ้างอิงอยู่**ก่อน** volume ของ codec (tone ทดสอบ 0.129 ทุกระดับ) แต่เสียงที่ไมค์ได้ยินเปลี่ยนมาก
  (volume 30 → 0.002, 60 → 0.008, 70 → 0.035, 90 → 0.31) — ระดับ 60 → 70 ตรงกับ +13 dB ของ codec พอดี
- **ทำ:** capture ring เก็บคู่ (ไมค์, อ้างอิง) 16 kHz (เขียนเฉพาะคู่ครบ — ครึ่งคู่จะเลื่อนทั้งสตรีม) · `readCapture()` คืน RMS อ้างอิงของ sample ชุดเดียวกัน ·
  `echo::WakeGate` (host tested) แทนโค้ด coupling เดิม
- **รอบแรกบนบอร์ด:** ตื่นเอง 2 ครั้ง — สตรีมสะดุด (buffer ว่าง, `R 0.00000`) → `radioSounding()` = false → ตกไปใช้เกณฑ์ปกติ ห้องนี้เสียงพื้น ~0.015 เกินเกณฑ์
  · peak ratio เพลงล้วน 3–9× ในช่วงเพลงเบา (เสียงห้องมากกว่าเสียงสะท้อน)
  → กฎเข้มตาม `gRadio.active()` · คาดหมายรวมเสียงห้องแบบพลังงาน · coupling จากค่าเฉลี่ยพลังงาน → peak 2.4–4.6×, 3.5 นาทีไม่ตื่นเอง
- **ระดับเสียง 85:** ไมค์ 0.50 RMS (ลำโพงทำให้ไมค์อิ่มตัว) ตื่นเอง 1 ครั้ง → ไมค์ > 0.35 ขณะเล่น = ไม่ปลุก/ไม่เรียนรู้
- **เปลี่ยนระดับเสียงแล้วเรียนใหม่จากศูนย์** ช่วงเพลงเบา → ช่วงดังถัดไปดู 19× → คูณ coupling ตาม dB ที่รู้ (`ES8311::volumeDb`, 0.5 dB/step)
**ผล:** 85 → 60 → 80 → 70 ไม่ตื่นเอง · host tests 39 ชุด / 1,649 checks · การพูดปลุกจริงขณะวิทยุดังต้องให้ผู้ใช้ทดสอบ

## T055: OTA — Arduino ยืนยันเฟิร์มแวร์ใหม่เองตอนบูต + CA ของ GitHub ไม่อยู่ในชุดเดิม

**วันที่:** 2026-10-16 · ระหว่างทำ OTA (v3.5.0, [[13-OTA-Update]])
1. **rollback จะไม่เคยเกิด:** bootloader ของ Arduino-ESP32 2.0.17 เปิด `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` แต่ `initArduino()` เรียก
   `esp_ota_mark_app_valid_cancel_rollback()` ทันทีที่บูตถ้า `verifyRollbackLater()` (weak, C linkage) คืน `false`
   → เฟิร์มแวร์ที่พังหลังบูตไปแล้วจะถูกนับว่า "ใช้ได้" → override เป็น `extern "C" bool verifyRollbackLater() { return true; }` แล้วยืนยันเองหลังทำงาน 60 วิ + agent heartbeat
   (ตรวจด้วย objdump: เหลือสัญลักษณ์เดียว คืนค่า 1)
2. **api.github.com ต่อไม่ได้ด้วย CA ชุดเดิม:** github.com ใช้ chain Sectigo E46 → USERTrust ECC (ไม่มีใน `WEB_ROOT_CA_PEM`) · ไฟล์ release redirect ไป
   release-assets.githubusercontent.com (ISRG Root X1) → ชุดใหม่ `OTA_ROOT_CA_PEM` ใน `gen_root_ca.py` (ไม่แตะชุดเดิม)
3. ข้อความ error ภาษาไทยถูกตัดกลางตัวอักษร (บัฟเฟอร์ 96 ไบต์) → 192 + ตัดตามขอบ UTF-8 · ทดสอบครบวงจรเจออีก 2 ข้อความ ("กำลังอัปเดต… ห้ามปิดเครื่อง", "มีเฟิร์มแวร์ใหม่…") → บัฟเฟอร์ 192 ทุกข้อความ + `notice()` ตัดเศษตัวอักษรเสมอ (หลัง v3.5.0)
**ทดสอบบนบอร์ด:** check / fetch (sha256 ตรงกับ release v3.4.4) / selftest สลับช่อง + ยืนยัน / selftest + รีเซ็ตที่ 12 วิ → กลับช่องเดิม · **ครบวงจร:** build 3.4.99 → อัปเดตเองเป็น release v3.5.0 จาก GitHub → ยืนยันแล้ว

## T054: AI ทัก "เรื่องทริปโตเกียว" เกือบทุกครั้ง ทั้งที่ผู้ใช้ไม่เคยคุย

**วันที่:** 2026-10-16 · **ผู้ใช้:** "รีบูตแล้วพูดสวัสดี AI จะตอบเรื่องทริปโตเกียวเกือบ 90%"
**พบบนบอร์ด** (`tool recall_memory` / `recall_conversation` ทาง console):
- ความจำ #9 "บอสกำลังวางแผนไปเที่ยวโตเกียวกับเพื่อนอีกคน" (event, **importance 4** = core memory ใส่ใน prompt ทุก session) + #8 (importance 3) บันทึก 15 ต.ค.
  — คำว่า "บอส" = live model เรียก `save_memory` เอง หลังได้ยินเสียงที่ไม่ใช่ผู้ใช้ (07:49 AI ตอบ "ว้าว ไปกันสองต่อสอง..." · "ใช่ครับที่เขื่อน",
  "ที่อยู่เรื่อยๆ ครับ..." = เสียงดีเจวิทยุก่อนแก้ T050) · และ #11 "ผู้ใช้พูดถึงเรื่องที่เขื่อน", #5 "ผู้ใช้กำลังทดสอบระบบ" จากตัวจัดระเบียบ (importance ≤ 3 ไม่เข้า prompt)
- log ผู้ใช้: `recall_memory {"query":"ทริปโตเกียว"}` ถูกเรียก**ก่อน** transcript "สวัสดี" = มาจาก prompt
- ลบ #8 #9 แล้วยังทักเรื่องโตเกียว: คำทัก 10:08 ("เรื่องทริปโตเกียวเป็นไงบ้าง") อยู่ใน "บทสนทนาล่าสุด" 6 ประโยคที่ใส่ใน prompt → **วงวน** คำทักถูกบันทึกแล้วป้อนกลับ
- ระหว่างทดสอบพบอีกบัค: `new` (เริ่ม session ใหม่ / กดค้างบนจอ) บางครั้งได้ **resumed session** — `sessionResumptionUpdate` ที่ค้างในคิวของ session เก่าใส่ handle กลับหลัง `startFreshSession` ล้างไปแล้ว
**แก้:** ลบ #8 #9 และขยะ #5 #11 (`forget_memory`, เหลือ 3 ข้อ) • facts จากตัวจัดระเบียบ importance ≤ 3 (`consolidate::kMaxImportance`) + prompt ตัวจัดระเบียบข้ามเสียงวิทยุ/ทีวี/คำสั่งทดสอบ •
system prompt: บันทึกเฉพาะสิ่งที่ผู้ใช้พูดถึงตัวเองชัดเจน, core memory / บทสนทนาล่าสุด "อย่ายกขึ้นมาเองตอนทักทาย" • `_freshPending`: ไม่รับ handle จนกว่า session ใหม่ ready
**ทดสอบบนบอร์ด:** `new` + "สวัสดี" 4 รอบ = new session ทุกรอบ ไม่มีการค้นเรื่องโตเกียว (ก่อนแก้รอบแรกยังค้น "ทริปโตเกียว") · host tests 36 ชุด / 1,596 checks
(สังเกต: ข้อความที่พิมพ์ผ่าน `ask` กับ 3.1 Flash Live ไม่มี `[Chat] ai:` — ไม่มี output transcription สำหรับ text turn จึงไม่ถูกบันทึกเป็นประโยคของ AI)

## T053: รีวิวรอบ 2 — เสียงช่วงปลุกหาย, VAD ค้างเมื่อพัดลมเปิด, millis() วนกลับ 49.7 วัน + บัคเล็ก

**วันที่:** 2026-10-16 · จากการรีวิวทั้งโปรเจครอบ 2 (ผู้ใช้สั่ง "แก้บัคทั้งหมด")
1. **เสียงที่พูดตอนกำลังปลุกหายช่วงท้าย** — เก็บเสียงระหว่างต่อ session ได้ 6 วิ (+1.5 วิก่อนปลุกด้วยเสียง) แต่ `sendPreroll()` ส่งรวดเดียว
   เข้าคิวส่งที่จุ 64 ก้อน (≈2.5 วิ) และเสียงไมค์ไม่รอช่องว่าง → ส่วนเกินถูกทิ้งเงียบๆ (นับใน `gemini_dropped`) · ต่อนาน 4 วิ = หาย ~1.4 วิ
   → **ทยอยส่งตามที่คิวว่าง** (`drainPreroll`, เว้น 4 ช่องให้ toolResponse) เสียงสดต่อท้ายใน ring (ลำดับไม่สลับ) และไม่จบประโยคระหว่างยังส่งค้าง
2. **VAD ค้าง "มีเสียงพูด" ตลอด** — ระดับเสียงพื้นหลังปรับเฉพาะก้อนที่ไม่ใช่เสียงพูด: เปิดพัดลม/เครื่องฟอกอากาศจนดังเกิน 2.5 เท่า
   = ทุกก้อนเป็น "เสียงพูด" ตลอดไป → ตื่นอยู่: ส่งเสียงไม่หยุด (โทเคน) และไม่พัก · หลับ: ตื่นเองซ้ำๆ
   → `vad::NoiseFloor` (`src/util/NoiseFloor.h`): ดังต่อเนื่องไม่มีช่วงเงียบ 10 วิ (`AGENT_VAD_NOISE_RISE_MS`) = เสียงพื้นหลัง ค่อยๆ ไต่ขึ้น
   (ไม่ใช้ตอนวิทยุดัง — มีตัวประมาณเสียงรั่วของวิทยุอยู่แล้ว) · host test `vad_noise_floor`
3. **`millis()` วนกลับทุก 49.7 วัน** (บอร์ดเปิดทิ้งไว้) — `now >= deadline` ค้าง: **แจ้งเตือน/ตัวจับเวลาไม่ดัง**, สถานะ/หน้าข้อมูลไม่อัปเดต,
   ตาไม่กระพริบ; `monoSec() = millis()/1000` กลับเป็น 0 → cool-down / หยุดพักค้าง · และแบบ signed ที่ deadline ไม่ถูกตั้งใหม่ก็ "เก่า" ได้:
   `_followUpUntil = 0` อ่านเป็น "ยังฟังต่อ" เมื่อเปิดเกิน 24.8 วัน (ไมค์ส่งไม่หยุด), `_nextAttempt` เก่า = **ต่อ Gemini ไม่ได้หลังพัก**
   ถ้าไม่เคยหลุดเลย 24.8 วัน, ข้อความ error "ถาวร" หายหลัง 24.8 วัน
   → `src/util/Deadline.h`: `deadline::reached()`, `elapsed()`, `Timer` (ปลดตัวเองเมื่อหมดเวลา ไม่มีวัน "เก่า") ใช้กับ follow-up, tool busy,
   radio settle, ข้อความ/คำใบ้/อารมณ์บนจอ, ตา, หน้าข้อมูล, หน้าวิทยุ · `monoSec()` ใช้ `esp_timer_get_time()` (64-bit) ·
   Live client เริ่มต่อทันทีเมื่อ `start()` · host test `deadline_wrap`
4. **เสียงยืนยัน (`playTone`) ตั้งธง "จบคำตอบ"** — ถ้า `set_volume` เล่นเสียงตอนคำตอบกำลังมา เสียงคำตอบเริ่มเล่นโดยไม่รอ jitter buffer (กระตุกบนเน็ตช้า)
   → เสียงของ AI เขียนผ่าน `writeVoice()` ที่ล้างธงนั้น
5. **นาฬิกาซิงก์แล้วบทสนทนาก่อนซิงก์หายจาก prompt** — `_episodes.open()` ซ้ำล้าง working memory → `EpisodicLog::reloadDays()` โหลดเมื่อวาน/วันนี้แล้ววางเทิร์นของ boot นี้ต่อท้าย (ไม่ซ้ำ)
6. **แจ้งเตือนที่พลาดตอนเครื่องปิดดังตอนบูตว่า "ถึงเวลา"** → ช้าเกิน 15 นาที = "การแจ้งเตือนที่ตั้งไว้ DD/MM HH:MM ผ่านไปแล้ว (ตอนนั้นเครื่องปิดอยู่)"
7. **`recall_conversation` อ่านไฟล์ได้ถึง 90 วันใน agent loop** → งบอ่าน 384 KB (`maxScanBytes`) วันนี้อ่านเสมอ วันเก่าข้ามเมื่อเกิน + ตอบ `searched_days`
8. **Studio เรียก `root.after()` จาก worker thread** (ทดสอบ URL วิทยุ, build/upload) — Tk ไม่ thread-safe → ส่งผ่าน event queue (`call_in_ui`)
9. เล็กน้อย: ตัวกรอง transcript ลบคำอังกฤษจริงที่มี "function" (เช่น functional) → ลบเฉพาะชุดคำ scaffolding (`TranscriptClean.h`, test) •
   ผล web tool ที่ parse ไม่ได้ → ตอบ error แทน response ว่าง (แจ้งเตือนราคาว่างไม่ประกาศ) • ความยาว API key ขั้นต่ำใช้กฎเดียวกันทุกที่ (> 20)
**host tests:** 36 ชุด / 1,594 checks ✓ · build ผ่าน (RAM 25.6%, Flash 27.9%)

## T052: รีวิวโปรเจค — UTF-8 ในโน้ต, บัฟเฟอร์ไมค์, reboot ตัดการปิด session, save settings ซ้อนกัน

**วันที่:** 2026-10-16 · จากการรีวิวทั้งโปรเจค (ผู้ใช้สั่งแก้ 3 จุดหลัก + งานเล็กที่เสี่ยงต่ำ)
1. **โน้ตไม่มีหัวข้อ** → หัวข้อ = `content.substring(0, 40)` ตัดเป็น**ไบต์** (ไทย 3 ไบต์/ตัว) → UTF-8 เสียใน `notes.json` และใน toolResponse (WebSocket text ที่ UTF-8 เสีย = เซิร์ฟเวอร์ปิดได้ด้วย 1007)
   → ตัดตามขอบตัวอักษร (`utf8::clampToBoundary`) + ซ่อมหัวข้อเก่าที่เสียตอนโหลด (`utf8::completeLength`, host test `utf8_complete_length`)
2. **บัฟเฟอร์ไมค์ 64 KB (~2 วิ)** แต่ loop ของ agent ค้างได้ ~1.7 วิตอน TLS handshake ของ Live (`geminiIO` priority 5 > agent 4) = ช่วงเพิ่งปลุกที่ผู้ใช้กำลังพูด → **256 KB (~8 วิ, PSRAM)**
3. **`reboot` ทาง console** บังคับรีสตาร์ทที่ 4 วิ แต่ agent ใช้ได้ถึง ~5.7 วิ (close handshake 3.5 + flush ความจำ 2 + 0.2) → session ค้างบนเซิร์ฟเวอร์ (T035/T045) → **fallback 8 วิ**
4. **`Settings::update`** เขียน NVS/SD นอกล็อกด้วยสำเนาของตัวเอง → UI (เสียง/สถานี) กับ agent บันทึกพร้อมกัน สำเนาเก่าอาจลงทีหลัง → **`_saveLock`** หนึ่งคนเขียนต่อครั้ง และอ่านค่าล่าสุดในคิวของตัวเอง
5. **`set agent` / `set user` ทาง console** ตัด `strncpy` กลางตัวอักษร → ตัดตามขอบ UTF-8
6. **`.gitattributes`** (`* text=auto eol=lf`, `*.bat` CRLF, ไฟล์รูป/ไบนารี) — เลิกคำเตือน "LF will be replaced by CRLF" ทุก commit (index เป็น LF อยู่แล้ว ไม่มีไฟล์เปลี่ยน)
**ทดสอบบนบอร์ด:** โน้ตไทยไม่มีหัวข้อ → หัวข้อ "ทดสอบโน้ตภาษา" (ครบตัว) + ให้ AI อ่านรายการโน้ต session ไม่หลุด · `set agent` ชื่อไทยยาว → "ผู้ช่วยส่วนตัวอ" setup ผ่าน (คืนเป็น "จาวิส") · `reboot` → ปิด session แล้วรีสตาร์ทใน 0.7 วิ · PSRAM ว่าง 6.2 MB, RAM ภายใน 109 KB (ต่ำสุด 95)

## T051: แตะปลุกแล้วคำแรกหาย + AI เปิดวิทยุซ้ำตอนถามเรื่องอื่น + ไมค์ตอนหลับ/พักจอ

**วันที่:** 2026-10-16 · **ผู้ใช้:** (1) "แตะหน้าจอ AI ขึ้นว่ากำลังฟัง แต่พูดทันทีคำแรกๆ AI ไม่ได้ยิน" — พูด "ราคาทองคำวันนี้" ได้ `[Chat] user: คำปัจจุบัน`
(2) ถามราคาทอง AI ตอบ "เปิดวิทยุ 93.5 ให้แล้ว... ส่วนราคาทอง..." (3) "ตอนหลับไมค์ถูกปิดเอง ... อยากให้ตอนหลับไมค์เปิด พักหน้าจอไมค์ปิด แตะตอนพักแล้วไมค์เปิด"

1. **คำแรกหาย:** ปลุกด้วยเสียงเก็บเสียงช่วงต่อ session ไว้ส่งตาม (`_wakeBuffering`) แต่**ปลุกด้วยการแตะไม่เก็บ** — ต่อใช้ 2–4 วิ (log: แตะ 00:06:52, ready 00:06:56) คำที่พูดช่วงนั้นหาย
   → แตะปลุกก็เก็บ (ล้างเสียงก่อนแตะ) และ**ส่งเฉพาะถ้ามีเสียงพูด** (`_wakeHeard`) · บัฟเฟอร์ 2 → 6 วิ (PSRAM) · ก่อนปลุกด้วยเสียงเก็บ 1.5 วิเหมือนเดิม
2. **เปิดวิทยุซ้ำ:** session ที่ resume จำว่าเพิ่งเปิดวิทยุ + transcript ไม่ชัด → โมเดลเรียก `play_radio` อีก (สถานีเดิมถูก restart)
   → สถานีเดิมที่เล่นอยู่ = ไม่ restart ตอบ `already_playing` + note "ไม่ต้องพูดถึงวิทยุ" · prompt บอก "ตอนนี้วิทยุกำลังเล่น: X (เรียก play_radio เฉพาะเมื่อประโยคล่าสุดขอ)"
   · transcript เป็นอักษรลาว (`ລາຄາທອງຄຳ...`) = การถอดเสียงของ Gemini เดาภาษาผิด (ไทย/ลาวใกล้กัน) ตัวโมเดลเข้าใจถูก — แสดงผลอย่างเดียว
3. **ไมค์:** ในโค้ดตอนหลับไมค์ไม่ได้ปิด แต่**ไอคอนไมค์ตอนเปิดเป็นสีเทาจางดูเหมือนปิด** และตอนวิทยุดังการปลุกด้วยเสียงต้องดังกว่าเสียงรั่ว
   → ไอคอน "เปิด" = ไมค์สีเขียว (ฟังอยู่ = ฟ้า, ปิด = แดง+ขีด) · **พักหน้าจอ (นาฬิกา) = ปิดไมค์** (gate `!_powerSave`) แตะ = ตื่น + ไมค์เปิด · หลับ = ไมค์เปิด
   · เปลี่ยนสถานีตอนหลับเคยปลุกเอง ("Waking up (voice)" ตอนแตะสถานีถัดไป) → วิทยุเริ่มเล่นใหม่ = เรียนรู้เสียงรั่วใหม่ 2.5 วิก่อนรับการปลุก
**ทดสอบบนบอร์ด:** เปลี่ยนสถานีตอนหลับไม่ตื่น · แตะปลุกเงียบไม่ส่งอะไร · "ราคาทองคำวันนี้" ระหว่างวิทยุ = get_market_quote อย่างเดียว ไม่พูดถึงวิทยุ · พักจอ → แตะ → ตื่น
(การพูดทันทีหลังแตะต้องทดสอบด้วยเสียงจริงโดยผู้ใช้)

**รอบ 2 (ผู้ใช้: "มันยังมั่วๆอยู่"):** แตะปลุกแล้วพูดทันทีได้ยินครบแล้ว ("ราคาทองคำปัจจุบัน") แต่
- AI ยังเปิดวิทยุซ้ำ แม้ได้ note "เล่นอยู่แล้ว" — session ที่ **resume** มีคำสั่ง "เปิดวิทยุ" ค้างใน context พอเสียงไม่ชัด (transcript "a sweet girl" = เนื้อเพลง) ก็ทำซ้ำทั้งเทิร์นก่อน
  → หลัง `play_radio` ตื่นครั้งถัดไป = **session ใหม่** (`_freshAfterRadio` → ล้าง resume handle ตอนพัก; ความจำระยะยาว/prompt ยังอยู่) · เรียกสถานีที่เล่นอยู่ซ้ำ = **error** "ไม่ได้ทำอะไร ห้ามพูดว่าเปิดวิทยุ"
- ตื่นเองตอนกด "+" บนหน้าวิทยุ: ระดับเสียงตั้งที่ codec (หลัง `radioOutLevel`) ลำโพงดังขึ้นแต่ค่าเสียงรั่วที่เรียนไว้ยังเป็นของระดับเดิม → **เปลี่ยนระดับเสียง = เรียนรู้ใหม่ 2.5 วิ** · ตื่นจากเสียงตอนวิทยุเล่นมี log `[Mic] ปลุกด้วยเสียงขณะวิทยุเล่น: ระดับ / คาดว่ารั่ว / เกณฑ์`
- สถิติ "จบประโยค" หลังปลุกด้วยเสียงเป็นของประโยคก่อน → `beginUtterance` ก่อนส่งบัฟเฟอร์
**ทดสอบบนบอร์ด (ลำดับเดียวกับ log ผู้ใช้):** เปิดวิทยุ → "+" 2 ครั้งตอนหลับ ไม่ตื่น 20 วิ → แตะ + "ราคาทองคำวันนี้" = new session, get_market_quote อย่างเดียว ไม่พูดถึงวิทยุ

## T050: เปิดวิทยุแล้ว AI ฟังเสียงวิทยุและตอบ + ตื่นเอง + เปิดสถานี 1 ทุกครั้ง

**วันที่:** 2026-10-15 · **ผู้ใช้:** "ช่วงแรกที่สั่งเปิดวิทยุ เสียงวิทยุดังตามระดับเสียง 80% AI จะฟังเสียงวิทยุอยู่สักพักและตอบเสียงจากวิทยุด้วย · อยากให้จำสถานีล่าสุด ปัจจุบันล็อกที่ Station 1 ทุกครั้ง"
**จาก log ผู้ใช้:**
1. หลัง AI ยืนยันสถานี → `[Mic] ฟังต่อ 6 วิ หลัง AI ตอบ` → ไมค์ส่งเสียงดีเจ 18.7 วิ → `[Chat] user: ที่อยู่เรื่อยๆ ครับ...` (เสียงวิทยุ) → พักได้หลัง 50 วิ เพราะ `_vadOpen` ค้าง
2. ตอนพักกับวิทยุ: `Waking up (voice)` ซ้ำๆ ระดับ 0.005–0.008 ไม่มี transcript (ช่วงเพลงเบา ค่าเสียงรั่วที่คาดไว้ต่ำ เกณฑ์ ×3 จึงผ่าน)
3. โค้ดจำสถานีถูกแล้ว แต่โมเดลใส่ station เอง (เช่นเลข 1 หรือคำกลางๆ) เพราะ declaration ไม่ชัด
**แก้:** `play_radio` → `requestSleep()` (พักหลังพูดจบ, ไม่เปิดช่วงฟังต่อเมื่อ `_sleepPending`, `_awaitingReply` หลังส่ง toolResponse กันพักก่อนเสียงตอบมา) ·
วิทยุเริ่มจากจอขณะตื่นและว่าง → พัก · `RADIO_WAKE_MIN_LEVEL` 0.02 · prompt บอกสถานีล่าสุด + "ไม่ระบุ = ไม่ใส่ station" + กรองคำกลางๆ · log args ของ tool
**ผล (บอร์ด):** ดู [[12-Internet-Radio#เปิดวิทยุแล้วพักทันที + จำสถานีล่าสุด (v3.4, T050)]] · หมายเหตุ: สถานี 94 Bangkok Smile หลุดแล้วต่อใหม่ทุก ~15 วิ (ฝั่งสถานี — ต่อใหม่สำเร็จเอง)

## T049: เลิกใช้ Google Search + ประเมิน DuckDuckGo + ข่าวตามหัวข้อได้ข่าวเก่า

**วันที่:** 2026-10-15 · **ผู้ใช้:** "ไม่ต้องใช้ Search ของ google ตรวจสอบและเปลี่ยนไปใช้ duckduckgo ว่าใช้งานได้ฟรีหรือไม่"
**ประเมิน DuckDuckGo (ทดสอบจาก PC):**
| ช่องทาง | ผล |
|---|---|
| `api.duckduckgo.com` Instant Answer (ทางการ, ฟรี, ไม่ต้อง key) | ได้เฉพาะบทคัดย่อสารานุกรมภาษาอังกฤษ (Bangkok, Python) — คำถามไทย/ข่าว/ราคา = ว่าง · ซ้ำกับ `search_knowledge` (Wikipedia ไทย+อังกฤษ) |
| `html.duckduckgo.com/html` (ไม่ใช่ API) | 2 ครั้งแรกได้ผล 10 รายการ จากนั้น**ทุกคำค้น HTTP 202 หน้าตรวจบอท ("anomaly")** |
| `lite.duckduckgo.com` | หน้าตรวจบอทตั้งแต่ครั้งแรก |
→ **ไม่มี API ค้นเว็บฟรีของ DuckDuckGo** และหน้า HTML กันโปรแกรมอัตโนมัติ — ไม่นำมาใช้ (ไม่หลบระบบตรวจบอท)

**ทำแทน:** ปิด Google Search เป็นค่าเริ่มต้น (`googleSearch = false`, บอร์ดผู้ใช้ `set search 0`) · prompt สอน AI ว่าเรื่องล่าสุด (ผลกีฬา หวย เหตุการณ์ ราคาทองไทย) ให้ใช้ `get_news` ระบุ topic แล้วบอกวันที่ ข้อเท็จจริงใช้ `search_knowledge` หาไม่ได้ให้บอกตรงๆ ห้ามเดา
**ข่าวเก่า:** ทดสอบ "ผลบอลแมนยูนัดล่าสุด" → AI เล่าแมตช์ ก.พ. / พ.ค. เพราะ Google News เรียงตามความเกี่ยวข้องย้อนหลังหลายเดือน
- แก้ 1: topic ค้น `when:7d` ก่อน (ไม่เจอค่อยค้นทุกช่วง, `range` บอก AI)
- แก้ 2 (ลองเรียงใหม่สุดจาก 40 ข่าว → ข่าว UCL ทั่วไปดันข่าวแมนยูตก): เอา **8 ข่าวที่เกี่ยวข้องสุด** แล้วเรียงใหม่สุดก่อน → "แมนยูเสมอสเปอร์ส 1-1 เมื่อ 11 ต.ค." · ทองคำแท่ง: 15 ต.ค. ขึ้นก่อน
**ถ้าอนาคตต้องการค้นเว็บจริง:** API ที่มีโควตาฟรีแต่ต้องสมัคร key เช่น Brave Search API / Tavily — หรือผูก billing ให้ Google Search (ทั้งโปรเจกต์รวมการคุยด้วยเสียงจะคิดเงิน ~$1.4/ชม.)

## T048: Google Search ขึ้น "quota เต็ม" ตลอด ตั้งแต่ 14 ต.ค. — key ไม่มีสิทธิ์ใช้ Search + การตรวจด้วย REST ผิด

**วันที่:** 2026-10-15 · **ผู้ใช้:** "Search เอาไว้ทำอะไร ทำงานตอนไหน เพราะขึ้นว่า quota เต็มตลอดเวลา ตั้งแต่เมื่อวานจนปัจจุบัน"

**Search ทำอะไร / ทำงานตอนไหน:** บอร์ดแค่ประกาศ `{"googleSearch":{}}` ใน setup ของ Live session — ตัวโมเดล**เลือกเอง**ระหว่างคุยว่าจะค้นเว็บเมื่อไหร่
(คำถามที่ tools ในเครื่องไม่ครอบคลุม เช่น ผลบอล, ข่าวเชิงลึก, ราคาทองไทยเป็นบาท, ผลประกาศเศรษฐกิจ) แล้วค้นบนเซิร์ฟเวอร์ Google บอร์ดไม่ได้เรียกเอง
**ทำไมเต็มตลอด:** เซิร์ฟเวอร์ปิด session ด้วย `1011 exceeded your current quota` **ภายใน 0.8 วิหลังส่ง setup** — ยังไม่ทันค้นอะไร = ไม่ใช่ "ใช้จนหมด"
แต่โปรเจกต์ของ key **ไม่มีโควตา Search สำหรับ Live** (limit 0 หรือไม่ได้เปิด) ซึ่งตรงกับที่เห็นมาตั้งแต่ T028 ทุกโมเดล ทุกครั้ง
- เอกสารราคา (2026-10): Live 3.8 / 3.1 Flash Live free tier = Search "Supported"; Flash-Lite / Flash (REST) free tier = **"Not available"**; paid = 5,000 ครั้ง/เดือนฟรีรวมทุก Gemini 3.x แล้ว $14/1,000
- ตรวจลิมิตจริงของโปรเจกต์: https://ai.dev/rate-limit (ลิงก์ในข้อความ error) — ถ้า Search = 0 ต้องเปิด billing หรือปิด Search (`set search 0` / เมนู "ค้นหาเว็บ")

**ข้อผิดพลาดใน T045 (แก้แล้ว):** การตรวจด้วย REST (Flash-Lite + googleSearch) ได้ 429 **เสมอ**สำหรับ key free tier เพราะ REST ไม่มี Search เลย
→ สรุปว่า "โควตาหมด" ทุกครั้ง แม้ต้นเหตุจริงเป็น session ค้างหลังรีบูต · ข้อสรุปเดิมใน T028 ("REST 429 = grounding หมด") ก็ผิดด้วยเหตุผลเดียวกัน
**แก้:** ตัด REST check ออก ให้ Live ตัดสินเอง (`search::Gate`): quota ตอนมี Search → ต่อใหม่ไม่มี Search →
ใช้ได้ = Search ใช้ไม่ได้จริง (ปิด 6 → 12 → 24 → 48 ชม. ถ้าล้มติดกัน, NVS `rt`: `srchoff` + `srchn`) · quota เหมือนกัน = ไม่ใช่ Search · session ที่มี Search ใช้ได้ = ล้างตัวนับ
Studio `apitest` อธิบายว่า 429 ของ Flash-Lite บน free tier ไม่ได้แปลว่า Search ของ Live ใช้ไม่ได้
**ทดสอบบนบอร์ด:** `searchretry` → setup มี Search → 1011 ใน 0.8 วิ → ต่อใหม่ไม่มี Search → ready → `[Search] quota with Google Search, fine without it -> Search off for 6 h (1 in a row, kept across reboots)`

**ผู้ใช้แย้ง (ภาพหน้าโมเดลใน AI Studio):** "ตัว live ทุกโมเดลรองรับ Search grounding" — ถูก: "Supported" = ความสามารถของโมเดล แต่ไม่ได้บอกโควตาของโปรเจกต์
→ พิสูจน์เพิ่มด้วย `setupmode searchonly` (ใหม่: setup มีแค่ model + `googleSearch` = **140 ไบต์** ไม่มี prompt / tools / VAD):
| โมเดล | ผล |
|---|---|
| gemini-3.1-flash-live-preview | 1011 quota ใน 0.5 วิ |
| gemini-3.8-live | 1011 quota |
| gemini-3.8-live-extended-thinking | 1011 quota |
ส่วน setup เต็ม (prompt 6.8 KB + 42 tools) แต่ไม่มี Search → ready ทุกครั้ง ⇒ **โปรเจกต์ของ key นี้ถูกปฏิเสธ Search grounding ใน Live ทุกโมเดล** ไม่เกี่ยวกับ setup ของเรา
สิ่งที่ผู้ใช้ทำได้: เปิด https://ai.dev/rate-limit (ล็อกอินบัญชีเดียวกับ key) ดูแถว Grounding with Google Search / Search · ตรวจว่าโปรเจกต์เป็น free หรือผูก billing ·
ลองสร้าง key ใหม่ในโปรเจกต์อื่น แล้ว `setupmode searchonly` (ปุ่ม "ทดสอบ Search" ใน Studio) — ขึ้น `[Diag] SEARCH ONLY setup accepted` = ใช้ได้

## T047: v3.4.0 ต่อ Gemini ไม่ได้ - TLS handshake -0x004E / -0x004C (keep-alive pointer ค้าง)

**วันที่:** 2026-10-15 · ทดสอบบนบอร์ดหลังแฟลช v3.4.0: Wi-Fi ต่อได้ เวลา sync ได้ tools HTTPS (บางจาก, `apitest` ไป host เดียวกัน) ใช้ได้ แต่ Live WebSocket ล้มทุกครั้ง
`esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x004E` (NET_SEND_FAILED) / `-0x004C` (NET_RECV_FAILED), 1–8 วิหลังเริ่มเชื่อมต่อ
**แยกสาเหตุ (bisect บนบอร์ด):** v3.3.0 ต่อได้บนเน็ตเดียวกัน · v3.4 + `keymode query` ยังล้ม (ไม่ใช่ header) · v3.3 + โค้ด agent/ai ของ v3.4 ต่อได้ → ต่างแค่ layout หน่วยความจำ (`SettingsData` เล็กลงหลังถอด MT5)
**ต้นเหตุ:** `esp_transport_ssl_set_keep_alive(_ssl, &ka)` เก็บแค่ **pointer** (`esp_tls_cfg_t::keep_alive_cfg`) และอ่านใหม่ทุกครั้งที่ connect แต่ `ka` เป็นตัวแปร local ใน `GeminiLiveClient::begin()`
→ ตอน connect อ่านค่าขยะจาก stack ที่ถูกใช้ซ้ำ → ตั้ง TCP keep-alive ผิด (idle/interval/count มั่ว) → socket ถูกตัดกลาง handshake · v3.3 รอดเพราะบังเอิญ stack ตรงนั้นยังเป็นค่าที่ใช้ได้
**แก้:** ย้ายเป็นสมาชิก `GeminiLiveClient::_keepAlive` (อายุเท่า transport) — หลังแก้: setup complete ใน ~2.8 วิ ทุกครั้ง
**บทเรียน:** API ของ ESP-IDF ที่รับ pointer ของ config (`*_set_keep_alive`, `cert_data`, ...) ต้องดูว่า copy หรือเก็บ pointer — `esp_transport_ws_set_config` copy เอง, `ssl_set_cert_data` เก็บ pointer (ใช้ค่าคงที่ใน flash จึงปลอดภัย)

**ยืนยันบนบอร์ดพร้อมกัน:**
- T045: รอบ bisect เจอ quota ตอนมี Search → REST check = 429 → บันทึกลง NVS · boot ถัดไป **ไม่ชน quota เลย** (`search_state: "quota"`, เหลือ ~5.9 ชม.)
- T046: `get_oil_price` (มีผล 9 ต.ค. 05.00 น., 6 วันก่อน, `change_at_last_adjustment`), `get_weather` (`us_aqi_pm25` 81, PM2.5 26), `get_market_quote` (PTT `market_open:false`, `delayed_min:15`; ทอง/BTC/USDTHB เปิด), `get_news` (`published` + `age_hours`), `as_of` ครบทุกตัว
- เวลาบน hotspot มือถือ: weather 18 วิ (3 TLS handshakes), quote 4 ตัว 11 วิ, news 8 วิ, oil 5 วิ

## T046: tool น้ำมันบอกวันที่ไม่ตรงปัจจุบัน + ตรวจความถูกต้องของ tools ทั้งหมด + รอผล Search

**วันที่:** 2026-10-15 · **ผู้ใช้:** "ตรวจสอบความถูกต้องของ tool ต่างๆทั้งหมด เช่น tool น้ำมัน วันที่ไม่ตรงปัจจุบัน" และ "ระวังที่รอผล Search"
**วิธีตรวจ:** เรียก API จริงทุกตัวจาก PC (บอร์ดต่อ Wi-Fi ไม่ได้ตอนนั้น) แล้วเทียบกับโค้ดที่แปลงผล — รายละเอียดใน [[11-Data-Tools-and-Cards#ความถูกต้องของวันที่ / เวลาในผลลัพธ์ (ตรวจ 2026-10-15 กับ API จริง — T046)]]
- **น้ำมัน (ต้นเหตุที่ผู้ใช้เห็น):** API บอก `OilRemark2` = "ราคามีผล ณ วันที่ 9 ต.ค. 69" และ `PriceYesterday` = ราคาก่อนปรับ → tool เดิมเรียกว่า "เมื่อวาน" และหัวการ์ด "วันนี้"
- PM2.5 ใช้ AQI รวม (โอโซน) · ราคาหุ้นนอกเวลาไม่บอกว่าเป็นราคาปิด · ข่าวไม่มีวันที่ · ปฏิทินพังถ้านาฬิกายังไม่ซิงก์
- ผ่าน (ไม่ต้องแก้): พยากรณ์อากาศ (timezone ไทย, วันแรก = วันนี้), geocoding, ipwho.is, ค่าเงิน, สแกนหุ้น, Wikipedia, Fear & Greed, เวลาโลก/เครื่องคิดเลข/แปลงหน่วย (host tests)

**รอผล Google Search:** Search ทำงานบนเซิร์ฟเวอร์และไม่ส่งอะไรมาจนได้คำตอบ (มัก 10–20 วิ) แต่บอร์ดขึ้น "AI ไม่ตอบ ลองพูดอีกครั้ง" ที่ 8 วิ และเลิกแสดงหน้า "กำลังคิด" ที่ 12 วิ
→ ผู้ใช้พูดซ้ำ = ตัดคำตอบที่กำลังมา · **แก้:** session ที่มี Search และได้ยินคำถามแล้ว (มี transcript) รอ 25 วิ (`AGENT_NO_REPLY_SEARCH_MS`) ทั้งข้อความเตือนและหน้ากำลังคิด
· การตรวจโควตา Search (T045) ไม่ยิงซ้ำภายใน 10 นาทีหลังผล "ใช้ได้" (`kRecheckSec`) — กัน session ค้างที่ยังอยู่หลายนาทีทำให้ตรวจวน

## T045: "โควตา Google Search หมด" ขึ้นทุกครั้งหลัง boot

**วันที่:** 2026-10-15 · **อาการ (ผู้ใช้):** "มันมักขึ้น search ติดโควต้า ตอน boot ใหม่"
**สาเหตุ (จากโค้ด — ตอนตรวจบอร์ดต่อ Wi-Fi ไม่ได้ จึงยังไม่ได้เก็บ log จริง):**
1. การปิด Search 6 ชม. (T028) เก็บเป็น `millis()` ใน RAM → รีบูตแล้วหาย → session แรกหลัง boot เสนอ Search อีก → ถ้าโควตา grounding หมดจริงก็ชน + ขึ้นข้อความซ้ำ**ทุก boot**
2. รีบูตแบบไม่มี close handshake (ถอดไฟ, ปุ่ม RESET, แฟลช, watchdog) ทิ้ง session เก่าค้างบนเซิร์ฟเวอร์ (T035) → Live ปิดด้วย `1011 ... exceeded your current quota` ข้อความเดียวกัน
   → โค้ดเดิมเห็น quota ตอน setup มี Search ก็**โทษ Search ทันที** ทั้งที่ Search ไม่ได้หมด

**แก้ (`src/agent/SearchGate.h`, host test `search_gate`):**
- quota ตอน setup มี Search = **สงสัย** เท่านั้น: ต่อใหม่ทันทีโดยไม่มี Search (คุยได้เลยเหมือนเดิม) และไม่ขึ้นข้อความบนจอ
- `TextModelClient::submitSearchCheck()` ยิง REST 2 ครั้งด้วย Flash-Lite: ping ธรรมดา → ping ที่มี `tools:[{googleSearch:{}}]` (แบบเดียวกับ `apitest`)
  - **429** = โควตา grounding หมดจริง → ปิด Search 6 ชม. + ข้อความ "โควตา Google Search หมด" + **บันทึกเวลาสิ้นสุด (epoch) ใน NVS `rt`/`srchoff`** → รีบูตแล้วยังปิดอยู่ (ก่อน SNTP sync ถือว่ายังปิด)
  - **200** = Search ใช้ได้ → error เมื่อกี้มาจากอย่างอื่น (เช่น session ค้าง) → เปิด Search ตั้งแต่ session ถัดไป
  - ping ธรรมดาก็ล้ม / เน็ตล่ม = บอกไม่ได้ → ปิด Search ไว้ไม่เกิน 30 นาที
- ยืนยันก่อน SNTP sync → บันทึกลง NVS เมื่อนาฬิกาพร้อม · หมดเวลา → ลบออกจาก NVS เอง · `searchretry` / `set search 1` / ปุ่มใน Studio = ล้างทั้งหมด
- diag: `search_state` (`ok` / `checking` / `quota`), `search_off_s` · log `[Search] ...`

**ตรวจบนบอร์ด:** หลังแฟลช ดู log ตอน boot: ถ้าขึ้น `[Search] Grounding works (REST 200)` แปลว่าเป็นกรณี session ค้าง (ข้อ 2)
ถ้าขึ้น `Grounding quota used up (REST 429)` = โควตาหมดจริง → รีบูตอีกครั้งแล้วต้อง**ไม่**ขึ้นข้อความซ้ำ และ `diag` แสดง `search_state: "quota"`

## T044: เสียง AI ตอบกลับกระตุก - jitter buffer เล็ก + WebSocket อ่าน header ผิดจังหวะ

**วันที่:** 2026-10-15 · **อาการ (ผู้ใช้):** "เสียงที่ AI ตอบกลับมา บางครั้งมันกระตุก"
**วัด:** เพิ่ม diag `voice_underruns`, `voice_last_gap_ms`, `voice_prebuffer_ms`, `audio_out_gap_max_ms`, `agent_loop_max_ms`, `ws_read_max_ms`, `ws_write_max_ms` + log `[Agent] loop held N ms (slowest: …)` แล้วให้ AI เล่านิทานยาวๆ ผ่าน `ask` (hotspot มือถือ, RSSI −63)

| ผลวัด | ความหมาย |
|---|---|
| `audio_out_gap_max_ms` ≤ 6, `agent_loop_max_ms` ≤ 37 ระหว่างพูด | ลูปลำโพงและ agent task ไม่ได้ช้า |
| `ws_read_max_ms` 300–860 ms | ข้อความเสียงจาก Gemini หนึ่งก้อน (~1 วิของเสียง) ใช้เวลามาถึงครึ่งวินาที — ข้อมูลมาเป็นก้อนๆ |
| 3 gap ในเรื่องแรก (gap 30–108 ms) ที่ jitter buffer 120 ms | **สาเหตุ 1:** บัฟเฟอร์เล็กกว่าช่วงที่ข้อมูลขาด → เสียงหมดกลางประโยค → รอเติมใหม่ = กระตุก |
| `Server closed (17527): ALwAVwBX…` กลางคำตอบ | **สาเหตุ 2:** `esp_transport_ws` ของ IDF 4.4.7 อ่าน header ของ frame (2 + 2/8 ไบต์) ด้วยการอ่านครั้งเดียว ไม่วนจนครบ — ถ้า TLS record ตัดกลาง header จะได้ความยาวผิด → อ่านเหลื่อมไปกลางข้อมูลเสียง base64 แล้วเจอ "opcode close" ปลอม → หลุดการเชื่อมต่อ เสียงขาดกลางคำตอบ |
| `_flush` ค้างเมื่อ `generationComplete` มาหลังเสียงเล่นหมด | **สาเหตุ 3:** คำตอบถัดไปเริ่มเล่นทันทีโดยไม่มีบัฟเฟอร์เลย → ต้นประโยคกระตุก |

**แก้:**
1. **Adaptive jitter buffer** (`AudioManager`): เริ่ม 200 ms; เสียงหมดกลางคำตอบแล้วมีต่อภายใน 3 วิ = gap → บัฟเฟอร์ ×2 (สูงสุด 600 ms); ตอบครบ 5 ครั้งไม่มี gap → ลด 40 ms (ลดช้าๆ ไม่ให้วนกลับมา gap) · ค่าอยู่ใน RAM ตลอดการเปิดเครื่อง
2. **ตัวอ่าน WebSocket frame เอง** (`GeminiLiveClient::readOneFrame` + `readExact`): อ่านตรงจาก TLS transport แบบวนจนครบทุกไบต์ (header, ความยาว 16/64 บิต, mask, payload), ใช้ FIN บอกว่าข้อความจบ, ถ้าข้อความใหญ่เกินก็ยังอ่านทิ้งให้ครบเพื่อไม่ให้เหลื่อม, control frame > 125 ไบต์ = สตรีมเพี้ยน → ต่อใหม่ — ส่วน handshake และการส่งยังใช้ `esp_transport_ws` เหมือนเดิม
3. เคลียร์ `_flush` เมื่อเสียงหมดแล้ว + ไม่นับการขัดจังหวะ (`clearPlayback`) เป็น gap

**ผล:** 5 คำตอบยาว: gap 1 ครั้ง (ข้อความหนึ่งมาช้า 0.86 วิ → บัฟเฟอร์ขยับเป็น 400 ms เอง) จากเดิม 3 ครั้งในเรื่องแรก · ไม่มี close ปลอมอีก · หน่วงก่อนเริ่มพูดเพิ่ม ~0.1 วิ (เน็ตดีจะเริ่มเร็วกว่านี้ได้ถ้าลด `AUDIO_PREBUFFER_MS`)
**ข้อสังเกต:** ก้อนข้อมูลที่อ่านนาน (≤ 5 วิ ต่อ frame) ทำให้ I/O task ส่งเสียงไมค์ไม่ได้ช่วงนั้น — เห็น `gemini_dropped` 59 ครั้งหนึ่งหลังจบคำตอบ (เหมือนเดิมก่อนแก้ ยังไม่แก้)

**รอบ 2 (log จากผู้ใช้ 17:44–18:01):** ถามอากาศ/ราคาทองแล้วกระตุกเหมือนแผ่นเสียงสะดุด
- `[Agent] loop held 202 ms (audioIn)` ซ้ำๆ = คิวส่งเสียงไมค์เต็ม (> 2.5 วิค้าง) และ `Send failed … transport_poll_write` = ขาขึ้นค้าง 5 วิ → **แก้:** พอ AI เริ่มตอบ ทิ้งเสียงไมค์ที่ค้างส่ง (`discardQueuedMic`, เลข generation ต่อก้อน) · `audioStreamEnd` ไม่รอคิวแล้ว (ค้างไว้ส่งทีหลัง) · DMA ลำโพง 60 → 80 ms · แก้ตัวแปร `t0` ซ้อนใน `flushOutbox` ที่ผมทำพลาดรอบแรก
- บรรทัดสรุปต่อคำตอบ `[Audio] จบคำตอบ: …` เผยว่ารอบถัดมา: ลำโพงรอนานสุด 2–3 ms, ทิ้งไมค์ 0 ก้อน แต่ก้อนเสียงมาช้าสุด 1.2 วิ และราคาทองกระตุก 7 ครั้งทั้งที่บัฟเฟอร์ 600 ms
- **ต้นเหตุจริง: TCP receive window ของ lwIP ใน Arduino-ESP32 2.0.17 = 5,760 ไบต์** (`CONFIG_LWIP_TCP_WND_DEFAULT`, คอมไพล์มาในไลบรารีสำเร็จรูป แก้ตอนรันไม่ได้) → ความเร็วรับสูงสุด ≈ window ÷ RTT ≈ 57 KB/s ที่ RTT 100 ms ของ hotspot มือถือ แต่เสียง Gemini (24 kHz PCM16 เป็น base64 ใน JSON) ต้องการ ~64 KB/s → **เสียงมาช้ากว่าที่เล่น** บัฟเฟอร์เล็กเท่าไรก็กระตุก
- **แก้ (ที่ทำได้โดยไม่เปลี่ยน framework):** วัด "เน็ตส่งเสียงทัน X% ของเวลาจริง" ต่อคำตอบ แล้วตั้งบัฟเฟอร์คำตอบถัดไป = เสียงที่ขาด (ยาวเสียง × (1 − X)) + 20% สูงสุด 3 วิ — เน็ตช้าจะรอตอนต้นทีเดียวแทนการสะดุดหลายครั้ง, เน็ตเร็วจะลดกลับ ×0.8 ต่อคำตอบที่ไม่กระตุก
- **แก้ถาวร (ยังไม่ทำ):** build lwIP ใหม่ให้ window ใหญ่ (เช่น 32–64 KB) — ต้องย้ายไป Arduino 3 / pioarduino (`custom_sdkconfig`) หรือ Arduino เป็น component ของ ESP-IDF; หรือใช้ Wi-Fi ที่ RTT ต่ำ (บ้าน ~20–30 ms → ~200 KB/s เหลือเฟือ)

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

**v3.4:** ถอด MT5 ออก → ความลับเหลือ `pass_e` / `key_e`; คีย์เก่า `mt5url`, `mt5tok`, `mt5tok_e`, `mt5trade`, `mt5lot` ถูกลบตอน boot (`Settings::kLegacyKeys`; token แบบข้อความธรรมดา `mt5tok` → ล้าง NVS ทั้ง partition แบบเดียวกับ migrate) และ `secrets.json` ที่ยังมี `mt5tok_e` ถูกเขียนใหม่ 1 ครั้ง

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
