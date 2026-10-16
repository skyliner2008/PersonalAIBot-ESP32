---
tags: [moc, dashboard, esp32-ai]
created: 2026-10-14
updated: 2026-10-15
status: in-progress
---

# 🏠 ESP32-AI Personal Agent — Home Dashboard

> **Project:** Personal AI Agent on ES3C28P (ESP32-S3 2.8" IPS LCD)
> **Goal:** คุยด้วยเสียงภาษาไทยแบบ real-time กับ Gemini 3.8 Live + **ความจำระยะยาวแบบ standalone ในตัวเครื่อง**
> **Board:** ES3C28P — ESP32-S3-N16R8 + ILI9341V + ES8311 + FT6336G + MicroSD
> **Firmware:** v3.4.4 (2026-10-16) — กันความจำผิด (ทริปโตเกียวจากเสียงวิทยุ), new session ไม่ resume ของเก่า • v3.4.3: แก้จากรีวิวรอบ 2 (เสียงช่วงปลุกหาย, VAD ค้างเมื่อพัดลมเปิด, millis() วนกลับ 49.7 วัน, แจ้งเตือนที่พลาด) • v3.4.2: UTF-8 โน้ต, บัฟเฟอร์ไมค์ 8 วิ, reboot, ล็อก settings • วิทยุ: พักทันทีหลังเปิด, จำสถานีล่าสุด, ปุ่ม ⏻ ปิด, แตะปลุกพูดได้ทันที • ผู้ช่วยส่วนตัว + ดวงตา + เมนูทัช + auto fallback (รวม Google Search) + ปุ่มไมค์ + วิทยุออนไลน์ + PC Studio app • host tests 36 ชุด / 1,596 checks

---

## 📑 Map of Content (MOC)

| # | Page | Description | Status |
|---|------|-------------|--------|
| 00 | [[PROJECT_ARCHITECTURE_SPEC]] | เอกสารแม่บทสถาปัตยกรรมทั้งระบบ | ✅ v3.1 |
| 01 | [[01-Hardware-ES3C28P-Pinout]] | Pin mapping (แก้ขา I2S data ตามที่วัดจริง) | ✅ Verified on board |
| 02 | [[02-Installation-and-Setup]] | ติดตั้งเครื่องมือ, secrets, flash, serial console | ✅ |
| 03 | [[03-Libraries-and-Dependencies]] | ไลบรารี (pinned), partition, tools | ✅ v2 |
| 04 | [[04-System-Architecture]] | Tasks, audio pipeline, derived state, memory map | ✅ v2 |
| 05 | [[05-Gemini-Live-API-Protocol]] | WSS/TLS, setup, tools, resumption, goAway | ✅ v2 |
| 06 | [[06-Avatar-and-Thai-UI]] | ดวงตาเด้งดึง 14 อารมณ์ (แนวนอน) + ฟอนต์ไทย + touch calibration | ✅ Verified on board |
| 07 | [[07-Troubleshooting-Log]] | ปัญหาจริงที่พบและวิธีแก้ (T013–T054) | ✅ |
| 08 | [[08-Long-Term-Memory-GraphRAG]] | ความจำระยะยาว standalone: BM25 + Knowledge Graph | ✅ Implemented |
| 09 | [[09-Assistant-Profiles-and-Tools]] | โปรไฟล์เสียง/นิสัย, เมนูทัช + คีย์บอร์ดไทย, 37+ tools, งบโทเคน, auto fallback, ทดสอบ API key | ✅ v3.1 |
| 12 | [[12-Internet-Radio]] | วิทยุออนไลน์: รายการสถานีไทยที่ล็อกไว้ (แก้ใน Studio), หน้าจอแบบแอปวิทยุ (แตะกลางจอ = สั่ง AI), AI เลือกสถานีให้, Helix MP3/AAC, ลดเสียงตอนคุย | ✅ ทดสอบบนบอร์ดแล้ว |
| 11 | [[11-Data-Tools-and-Cards]] | tools ข้อมูลสด + การ์ดกราฟิก: อากาศ/PM2.5, ราคาน้ำมัน, TradingView, ค่าเงิน, ข่าว + พอร์ตจาก PersonalAIBot: เครื่องคิดเลข, แปลงหน่วย, เวลาโลก, Wikipedia, โน้ต, วิเคราะห์เทคนิค, สแกนหุ้น, ปฏิทินเศรษฐกิจ, แจ้งเตือนราคา (TradingView อ่านอย่างเดียว — ไม่มี MT5 ตั้งแต่ v3.4) | ✅ v3.4 |
| 10 | [[10-PC-Studio-App]] | แอป PC: ตั้งค่า, เครื่องมือ, debug log สด, แชทพิมพ์, จอเสมือน + แตะจากระยะไกล, build/upload | ✅ v3.1 |

---

## 🔧 Checklist

- [x] Hardware pinout documented & verified (I2C scan `0x18`, `0x38`; I2S loopback)
- [x] Dev tools: PlatformIO 6.2.0, MSVC 2019 (host tests), Python + Pillow (font tool)
- [x] Firmware v2 builds (RAM 17.8%, Flash 18.9% of 6.25 MB) and boots on the real board
- [x] Display + Thai subtitles + avatar states (screenshot verified)
- [x] Touch FT6336G, BOOT button, RGB LED, battery ADC (calibrated)
- [x] Audio: ES8311 init, I2S 24 kHz full-duplex, mic → 16 kHz resampler, codec loopback test
- [x] MicroSD (4-bit) + LittleFS fallback
- [x] Long-term memory: facts + knowledge graph + episodic log, persists across reboot (tested on SD)
- [x] Agent tools (8) declared BLOCKING + toolResponse
- [x] Gemini client: esp_websocket_client, verified TLS, large frames, session resumption, context compression
- [x] Host unit tests: 14 suites / 1,171 checks passing
- [x] v3: ดวงตาแนวนอน 14 อารมณ์ (screenshot), เมนูตั้งค่า, คีย์บอร์ดไทย/อังกฤษ, สแกน Wi-Fi — ทดสอบบนบอร์ดแล้ว
- [x] v3: 20 tools + Google Search, timers/reminders/to-do, โปรไฟล์ 8 แบบ, auto model fallback, local VAD, auto-sleep
- [x] Host unit tests: 23 suites / 1,340 checks passing
- [x] Calibrate ทัช 3 จุดบนบอร์ด (ผู้ใช้ทำแล้ว · ค่าเริ่มต้นแก้เป็น rotation ^ 2)
- [x] API key ของผู้ใช้: `apitest` ผ่าน • Gemini 3.8 Live **setup complete** บนบอร์ดจริง (หลังปิด Google Search ที่โควตาหมด — T028)
- [x] v3.1: ปุ่มไมค์, หยุดวนสลับโมเดล, auto ปิด Search, setup timeout, Wi-Fi back-off, แก้หลับเอง (T027–T033), PC Studio app
- [x] Host unit tests: 24 suites / 1,362 checks passing
- [x] คุยกับ AI end-to-end บนบอร์ดจริง (ข้อความ → 3.8 Live → เสียง + transcript) — 2026-10-15
- [ ] ทดสอบพูดด้วยเสียงจริงผ่านไมค์ (local VAD) และฟังคุณภาพเสียงลำโพง
- [x] สีจอ: แผงนี้ต้อง `invert = 1` (ผู้ใช้เทียบกับ PC app: invert 0 = พื้นขาว/ตาน้ำเงิน) — T029
- [ ] ฟังเสียงลำโพงจริง / ปรับ `AUDIO_MIC_GAIN`, volume ให้เหมาะกับห้อง

---

## 🏗️ Project Structure

```
ESP32-AI/
├── README.md                      # ภาพรวม + quick start
├── PROJECT_ARCHITECTURE_SPEC.md   # เอกสารแม่บท (สำเนาเดียวกับ wiki/)
├── platformio.ini                 # pinned platform/libs, default_16MB.csv
├── include/
│   ├── pins_config.h              # GPIO (I2S data: DOUT 8, DIN 6)
│   ├── secrets.example.h          # template
│   └── secrets.h                  # Wi-Fi + API key (ไม่ commit)
├── src/
│   ├── main.cpp                   # boot, ui/led tasks, serial console
│   ├── app/AppConfig.h            # persona, เสียง, ความจำ, echo gate
│   ├── agent/                     # AgentController (+Session/Mic/Routines/Device), AgentTools (42 tools), WebTools
│   ├── ai/                        # GeminiLiveClient, GoogleRootCA.h
│   ├── audio/                     # AudioManager, ES8311, Resampler
│   ├── display/                   # AvatarUI, TouchFT6336, LGFX config, fonts/
│   ├── memory/                    # MemoryStore, EpisodicLog, TextFeatures, sinks
│   ├── system/                    # I2cBus, Storage
│   └── util/                      # UTF-8, Base64, TextWrap, TimeFmt, JsonFrame, PSRAM alloc
├── host_tests/                    # unit tests บน PC (run_tests.bat)
├── tools/                         # gen_thai_font.py, gen_root_ca.py, pc_app/ (ESP32-AI Studio)
├── _backup/                       # สำเนาโค้ด/เอกสารก่อนอัปเกรด v2
└── wiki/                          # Obsidian vault (หน้านี้)
```

---

## ⚡ Quick Reference

| Signal | GPIO | ⚠️ Note |
|--------|------|---------|
| LCD Backlight | 45 | Active HIGH, ปิดตอนบูต |
| Speaker Amp Enable | 1 | **Active LOW** |
| I2S data MCU→codec | **8** | (ไม่ใช่ 6 — ดู [[07-Troubleshooting-Log#T013 I2S DIN/DOUT สลับกัน - ไมค์อ่านได้ 0 ตลอด]]) |
| I2S data codec→MCU | **6** | |
| USB | Type-C = native USB (COM4, VID 303A) | `ARDUINO_USB_CDC_ON_BOOT=1` |

| Control | Action |
|---|---|
| ปุ่มไมค์ (ซ้ายบน) | เปิด/ปิดไมค์ |
| แตะเฟือง (ขวาบน) / BOOT ค้าง 1.5 s | เมนูตั้งค่า |
| แตะจอ / BOOT | หยุด AI พูด / ปลุก / ปิดเสียงแจ้งเตือน / ลองเชื่อมต่อใหม่ |
| กดค้างบนจอ 1.5 s | เริ่ม session ใหม่ (ความจำยังอยู่) |
| Serial `help` | status, tool, ask, expr, ui ..., set ..., apitest, diag, screenshot ... |
| PC | `tools\pc_app\run_studio.bat` → [[10-PC-Studio-App]] |

---

## 📝 Development Log

| Date | Activity | Related Docs |
|------|----------|--------------|
| 2026-10-14 | Project initialized, wiki created, v1 compiled | All pages |
| 2026-10-14 | Code review: พบปัญหา sample rate, ES8311 init, WebSocket 15 KB, TLS insecure, echo, tools ไม่ถูกประกาศ | [[07-Troubleshooting-Log]] |
| 2026-10-14 | **v2:** AI agent + standalone long-term memory (facts, knowledge graph, episodic log) | [[08-Long-Term-Memory-GraphRAG]] |
| 2026-10-14 | v2: esp_websocket_client + TLS, I2S 24 kHz + resampler, Thai VLW font, touch via I2cBus | [[04-System-Architecture]], [[05-Gemini-Live-API-Protocol]] |
| 2026-10-14 | Host unit tests 1,171 checks ✓ | `host_tests/` |
| 2026-10-14 | ทดสอบบนบอร์ดจริง: SD, codec, touch, จอไทย, ความจำข้าม reboot ✓ · พบและแก้ขา I2S สลับ | [[07-Troubleshooting-Log]] |
| 2026-10-14 | **v3:** ดวงตาเด้งดึงแนวนอน, โปรไฟล์เสียง/นิสัย, เมนูทัช + คีย์บอร์ดไทย, 20 tools, งบโทเคน 65k TPM, auto fallback, Flash-Lite consolidation | [[09-Assistant-Profiles-and-Tools]] |
| 2026-10-14 | ผู้ใช้รายงานแตะเฟืองไม่ได้ → เพิ่ม touch calibration + BOOT ค้าง = เมนู | [[07-Troubleshooting-Log#T022 แตะเฟืองไม่ได้ - แกนทัชไม่ตรงกับจอแนวนอน]] |
| 2026-10-14 | ผู้ใช้รายงาน "สลับโมเดลวนไปมา" → พบโควตา Google Search หมด (ไม่ใช่โมเดล) • fallback หยุดพัก + ปิด Search อัตโนมัติ • 3.8 Live เชื่อมต่อสำเร็จ | [[07-Troubleshooting-Log#T027 สลับโมเดลวนไปมาไม่หยุด - ทุกโมเดลติด quota]], [[07-Troubleshooting-Log#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]] |
| 2026-10-14 | **v3.1:** ปุ่มไมค์บนจอ, จอ IPS กลับสี (พื้นดำ), `apitest`, `ask`, `diag`, Wi-Fi reason + back-off, แก้หลับเองหลังแตะ, **ESP32-AI Studio** (PC app) | [[10-PC-Studio-App]] |
| 2026-10-15 | **คุยกับ AI end-to-end สำเร็จบนบอร์ดจริง** (พิมพ์ผ่าน `ask` → 3.8 Live ตอบเป็นเสียง + transcript) • แก้ 1008 session expired ถูกนับเป็นโมเดลหาย (T034) • Wi-Fi กลับมาเชื่อมได้ | [[07-Troubleshooting-Log#T034 "session not found or expired" ถูกตีความเป็นโมเดลหาย - 2 โมเดลโดนพัก 24 ชม.]] |
| 2026-10-15 | ปิด session ด้วย close handshake (พัก/สลับ/รีบูต) — แก้ quota จาก session ค้าง • พัก↔ตื่น 5 รอบ quota 0 • log/JSON ไม่แทรกกัน (write ครั้งเดียว) | [[07-Troubleshooting-Log#T035 quota ทุกโมเดลแม้ปิด Search - session ค้างบนเซิร์ฟเวอร์]] |
| 2026-10-15 | ยืนยันสีจอ (invert = 1 → โหมดมืด) • PC app: คัดลอก/วาง (Ctrl+C/V/X/A ใช้ได้กับแป้นไทย + เมนูคลิกขวา + คัดลอก log) • ปุ่ม 🌓 สลับสีจอบอร์ด | [[10-PC-Studio-App]] |
| 2026-10-15 | ผู้ใช้: AI ไม่ค่อยตอบ (3.8 แย่กว่า 3.1) → hybrid VAD + ฟังต่อ 6 วิ + AGC + server VAD tuning + log `[Mic]` + `rec` อัดเสียงที่ AI ได้ยิน (WAV ใน PC app) • ภาพ app เขินอาย = จังหวะท่าทางหลังแตะ | [[07-Troubleshooting-Log#T036 ไอคอนไมค์ขึ้นแต่ AI เงียบ - 3.8 Live ไม่ค่อยตอบ]] |
| 2026-10-15 | **แก้บอร์ดรีบูตเอง (CORRUPT HEAP)**: race ใน esp_websocket_client → เขียนใหม่เป็น task `geminiIO` ตัวเดียวถือการเชื่อมต่อ • 3 โมเดลรับ VAD tuning • พัก↔ตื่น 4 รอบผ่าน | [[07-Troubleshooting-Log#T039 บอร์ดรีบูตเอง CORRUPT HEAP หลัง error เครือข่าย - race ใน esp_websocket_client]] |
| 2026-10-15 | ไมค์ต้องพูดใกล้ → ADC 18→36 dB + ปรับ "ความไวไมค์" ได้ • `apitest` ทดสอบ Search grounding: 429 = โควตา Search หมด (โมเดลรองรับ) • `searchretry` | [[07-Troubleshooting-Log#T036 ไอคอนไมค์ขึ้นแต่ AI เงียบ - 3.8 Live ไม่ค่อยตอบ]] |
| 2026-10-15 | **แก้เสียง AI กระตุก**: jitter buffer ปรับตัวเอง (200→400→600 ms ตามเน็ต) + ตัวอ่าน WebSocket frame เอง (IDF 4.4 อ่าน header ขาดเมื่อ TLS ตัดกลาง → เสียงถูกอ่านเป็นคำสั่งปิด → หลุดกลางคำตอบ) + diag วัด gap/ลูปค้าง — 5 คำตอบยาว gap 1 ครั้ง (เดิม 3 ในเรื่องแรก) | [[07-Troubleshooting-Log#T044 เสียง AI ตอบกลับกระตุก - jitter buffer เล็ก + WebSocket อ่าน header ผิดจังหวะ]] |
| 2026-10-15 | **v3.4.0: ถอด MT5 ออกทั้งหมด** (ไม่มีการเชื่อมต่อโบรกเกอร์ — ข้อมูลการเงินอ่านจาก TradingView อย่างเดียว: ลบ `mt5_account`/`mt5_trade`, ค่า `mt5*`, `MT5_*` ใน `ai-setup.txt`, ส่วน MT5 ใน Studio; boot แรกลบคีย์เก่าใน NVS/SD) • tools ข้อมูลใช้ **HTTPS เท่านั้น** • Gemini key ส่งทาง header `x-goog-api-key` เป็นค่าเริ่มต้น • แยก `AgentController.cpp` (2,000+ บรรทัด) เป็น Session/Mic/Routines/Device • `publish_release.py` รัน host tests ก่อนเผยแพร่ + CI release รอ host-tests • host tests 1,493 checks ✓ — **ยังไม่ได้ทดสอบบนบอร์ด** | [[11-Data-Tools-and-Cards#ไม่มีการเชื่อมต่อ MT5 / โบรกเกอร์ (ถอดออกใน v3.4)]] · [[04-System-Architecture]] |
| 2026-10-15 | **แก้ "โควตา Google Search หมด" ขึ้นทุกครั้งที่ boot** — สถานะเก็บแค่ใน RAM (รีบูตแล้วชนซ้ำ) และ session ค้างจากการรีบูตแบบไม่ handshake ให้ error "quota" เหมือนกันจนโทษ Search ผิด → `search::Gate`: quota ตอนมี Search = แค่สงสัย, ตรวจด้วย REST (ping ธรรมดา + ping ที่มี googleSearch) → 429 = ปิด 6 ชม. **จำข้ามรีบูต** (NVS `rt`/`srchoff`), 200 = เปิดต่อไม่ขึ้นข้อความ • diag `search_state` • host test `search_gate` — ทดสอบบนบอร์ดแล้ว (T047) | [[07-Troubleshooting-Log#T045 "โควตา Google Search หมด" ขึ้นทุกครั้งหลัง boot]] |
| 2026-10-15 | **ตรวจความถูกต้องของ tools ทั้งหมดกับ API จริง** — น้ำมัน: วันที่มีผล + "ส่วนต่างตอนปรับ" แทน "เมื่อวาน" (ราคาปรับ 9 ต.ค. ไม่ใช่วันนี้) • PM2.5 ใช้ AQI ของ PM2.5 • ราคาบอกตลาดเปิด/ปิด + เวลาราคา + หน่วง • ข่าวมีวันที่ • ปฏิทินมีวันที่ + กันนาฬิกายังไม่ซิงก์ • `as_of` ทุก tool ข้อมูลสด • **รอผล Google Search 25 วิ** ก่อนขึ้น "AI ไม่ตอบ" • host tests 31 ชุด / 1,541 checks — ทดสอบบนบอร์ดแล้ว (T047) | [[07-Troubleshooting-Log#T046 tool น้ำมันบอกวันที่ไม่ตรงปัจจุบัน + ตรวจความถูกต้องของ tools ทั้งหมด + รอผล Search]] |
| 2026-10-15 | **ทดสอบ v3.4.0 บนบอร์ด + แก้ TLS ล้ม**: Live ต่อไม่ได้ (handshake -0x004E) เพราะ keep-alive config เป็นตัวแปร local ที่ IDF เก็บแค่ pointer → ย้ายเป็นสมาชิก (bisect บนบอร์ดกับ v3.3.0) • ยืนยันบนบอร์ด: จำโควตา Search ข้ามรีบูต (boot ไม่ชน quota) + tools น้ำมัน/อากาศ/ราคา/ข่าว ให้วันที่ถูกต้อง | [[07-Troubleshooting-Log#T047 v3.4.0 ต่อ Gemini ไม่ได้ - TLS handshake -0x004E / -0x004C (keep-alive pointer ค้าง)]] |
| 2026-10-15 | **Search "quota เต็มตลอด" = key ไม่มีสิทธิ์ใช้ Search กับ Live** (ปิดใน 0.8 วิหลัง setup ก่อนค้นอะไร) • การตรวจ REST ใน T045 ผิด (free tier REST ไม่มี Search เลย = 429 เสมอ) → ให้ Live ตัดสิน: ไม่มี Search ใช้ได้ = ปิด 6→48 ชม. จำข้ามรีบูต • ทดสอบบนบอร์ดแล้ว | [[07-Troubleshooting-Log#T048 Google Search ขึ้น "quota เต็ม" ตลอด ตั้งแต่ 14 ต.ค. — key ไม่มีสิทธิ์ใช้ Search + การตรวจด้วย REST ผิด]] |
| 2026-10-15 | **เลิกใช้ Google Search** (ค่าเริ่มต้นปิด) • ประเมิน DuckDuckGo: ไม่มี API ค้นเว็บฟรี + หน้า HTML บล็อกบอท → ไม่ใช้ • เรื่องล่าสุดใช้ `get_news` (7 วันล่าสุด, เกี่ยวข้องสุด 8 ข่าวเรียงใหม่สุด) • `setupmode searchonly` + ปุ่ม "ทดสอบ Search" ใน Studio: ทั้ง 3 โมเดล Live ถูกปฏิเสธแม้ setup มีแค่ Search • ทดสอบบนบอร์ดแล้ว | [[07-Troubleshooting-Log#T049 เลิกใช้ Google Search + ประเมิน DuckDuckGo + ข่าวตามหัวข้อได้ข่าวเก่า]] |
| 2026-10-15 | **วิทยุ: เปิดแล้วพักทันที + จำสถานีล่าสุด** — AI ไม่ฟัง/ตอบเสียงดีเจอีก (พักหลังพูดยืนยัน, ไม่มีช่วงฟังต่อ) • ไม่ตื่นเองจากเสียงวิทยุ (เกณฑ์ปลุกขั้นต่ำ 0.02) • "เปิดวิทยุ" = สถานีล่าสุด (prompt บอก + กรองคำกลางๆ) • log args ของ tools • ทดสอบบนบอร์ดแล้ว | [[07-Troubleshooting-Log#T050 เปิดวิทยุแล้ว AI ฟังเสียงวิทยุและตอบ + ตื่นเอง + เปิดสถานี 1 ทุกครั้ง]] · [[12-Internet-Radio]] |
| 2026-10-16 | **ปุ่ม ⏻ ปิดวิทยุ** บนหน้าจอวิทยุ (หยุด + กลับหน้า avatar) แยกจาก ⏸/▶ หยุดชั่วคราว • ทดสอบบนบอร์ด: ภาพหน้าจอ + `radio: idle` หลังกด | [[12-Internet-Radio#หน้าจอวิทยุ (`RadioScreen`)]] |
| 2026-10-16 | **แตะปลุกแล้วพูดทันทีได้ยินครบ** (เก็บเสียงช่วงต่อ session 6 วิ) • AI ไม่เปิดวิทยุซ้ำ/ไม่พูดถึงวิทยุตอนถามเรื่องอื่น • **พักหน้าจอ = ปิดไมค์, หลับ = ไมค์เปิด** (ไอคอนไมค์เปิดเป็นสีเขียว) • เปลี่ยนสถานีตอนหลับไม่ปลุก AI | [[07-Troubleshooting-Log#T051 แตะปลุกแล้วคำแรกหาย + AI เปิดวิทยุซ้ำตอนถามเรื่องอื่น + ไมค์ตอนหลับ/พักจอ]] |
| 2026-10-16 | **วิทยุรอบ 2:** หลังเปิดวิทยุ ตื่นครั้งถัดไปเริ่ม session ใหม่ (resume เคยทำคำสั่งเปิดวิทยุซ้ำ) • เรียกสถานีที่เล่นอยู่ซ้ำ = ปฏิเสธ • ปรับเสียงตอนวิทยุเล่น = เรียนรู้เสียงรั่วใหม่ (กด + เคยปลุก AI) • log ค่าตอนปลุกด้วยเสียง • ทดสอบบนบอร์ดแล้ว | [[07-Troubleshooting-Log#T051 แตะปลุกแล้วคำแรกหาย + AI เปิดวิทยุซ้ำตอนถามเรื่องอื่น + ไมค์ตอนหลับ/พักจอ]] |
| 2026-10-16 | **Release v3.4.1** — งานวิทยุทั้งหมด (T050, T051 + ปุ่ม ⏻ ปิด) ผู้ใช้ทดสอบด้วยเสียงจริงแล้วใช้ได้ | [[12-Internet-Radio]] |
| 2026-10-16 | **รีวิวโปรเจค + แก้:** หัวข้อโน้ตไทยตัดกลางตัวอักษร (UTF-8 เสีย → session หลุดได้) • บัฟเฟอร์ไมค์ 2 → 8 วิ • `reboot` fallback 4 → 8 วิ (ไม่ตัดการปิด session) • ล็อกการบันทึก Settings • ตัด UTF-8 ใน console • `.gitattributes` • ทดสอบบนบอร์ดแล้ว | [[07-Troubleshooting-Log#T052 รีวิวโปรเจค — UTF-8 ในโน้ต, บัฟเฟอร์ไมค์, reboot ตัดการปิด session, save settings ซ้อนกัน]] |
| 2026-10-16 | **Release v3.4.2** — งานแก้จากรีวิว (T052) | [[07-Troubleshooting-Log#T052 รีวิวโปรเจค — UTF-8 ในโน้ต, บัฟเฟอร์ไมค์, reboot ตัดการปิด session, save settings ซ้อนกัน]] |
| 2026-10-16 | **รีวิวรอบ 2 + แก้บัคทั้งหมด:** เสียงช่วงปลุกหายท้าย (ทยอยส่ง preroll) • VAD ค้างเมื่อเสียงพื้นหลังดังขึ้น (`NoiseFloor`) • `millis()` วนกลับ 49.7 วัน — แจ้งเตือนไม่ดัง/ต่อไม่ได้/ไมค์ส่งไม่หยุด (`Deadline.h`, `monoSec` 64-bit) • tone ทำคำตอบกระตุก • บทสนทนาก่อนซิงก์นาฬิกาหาย • แจ้งเตือนที่พลาดบอกว่าพลาด • `recall_conversation` มีงบอ่าน • Studio thread-safe • host tests 36 ชุด / 1,594 checks | [[07-Troubleshooting-Log#T053 รีวิวรอบ 2 — เสียงช่วงปลุกหาย, VAD ค้างเมื่อพัดลมเปิด, millis() วนกลับ 49.7 วัน + บัคเล็ก]] |
| 2026-10-16 | **Release v3.4.3** — งานแก้จากรีวิวรอบ 2 (T053) ทดสอบบนบอร์ดผ่าน serial แล้ว | [[07-Troubleshooting-Log#T053 รีวิวรอบ 2 — เสียงช่วงปลุกหาย, VAD ค้างเมื่อพัดลมเปิด, millis() วนกลับ 49.7 วัน + บัคเล็ก]] |
| 2026-10-16 | **AI ทัก "ทริปโตเกียว" ทั้งที่ไม่เคยคุย:** ความจำผิดจากเสียงวิทยุ (importance 4 = อยู่ใน prompt ทุก session) + วงวนผ่านบทสนทนาล่าสุด → ลบ, ความจำจากตัวจัดระเบียบ ≤ 3, prompt ห้ามยกเรื่องเก่าตอนทัก • แก้ `new` แล้วได้ resumed session | [[07-Troubleshooting-Log#T054 AI ทัก "เรื่องทริปโตเกียว" เกือบทุกครั้ง ทั้งที่ผู้ใช้ไม่เคยคุย]] |
| 2026-10-16 | **Release v3.4.4** — กันความจำผิด + new session (T054) · ลบความจำขยะบนบอร์ด (#5 #8 #9 #11) เหลือ 3 ข้อ | [[07-Troubleshooting-Log#T054 AI ทัก "เรื่องทริปโตเกียว" เกือบทุกครั้ง ทั้งที่ผู้ใช้ไม่เคยคุย]] |
| 2026-10-15 | **วิทยุ v3.3.0: รายการสถานี + หน้าจอแบบแอปวิทยุ** — เลิกค้น Radio Browser ออนไลน์ (ช้า/ล้ม/URL ขยะ) → 16 สถานีไทยที่ทดสอบแล้ว เก็บใน SD (`agent/radio_stations.json`) • AI เห็นรายการใน prompt เลือกตามชื่อ/เลข/แนว • หน้าจอวิทยุ: แผงกลางแตะ = สั่ง AI, แถบสถานีเลื่อนได้, ⏮ − + ⏭, เปิดเองเมื่อวิทยุเล่น, หลบให้โหมดนาฬิกา • Studio แท็บ 📻 วิทยุ (เพิ่ม/แก้/ลบ/เรียง/ทดสอบ URL) • ทดสอบบนบอร์ดแล้ว | [[12-Internet-Radio#รายการสถานี (ล็อกไว้ — ไม่ค้นออนไลน์)]] · [[10-PC-Studio-App]] |
| 2026-10-15 | **ทดสอบวิทยุบนบอร์ด**: แก้ chunked transfer (เสียงสะดุด), watchdog รีบูต (ย้าย webTools/textModel ไป core 1), ตื่นเองเพราะเสียงวิทยุ (ประมาณเสียงสะท้อนจากระดับที่ส่งออก), การ์ดค้างทับนาฬิกา | [[12-Internet-Radio#ทดสอบบนบอร์ด (2026-10-15)]] · [[07-Troubleshooting-Log#T043 บอร์ดรีบูต task_wdt IDLE0 ขณะเปิดวิทยุ + เรียก tool ออนไลน์]] |
| 2026-10-15 | **Internet Radio เฟส 1**: `play_radio`/`stop_radio`, ตัวเล่นสตรีม + Helix (RPSL, ไม่ใช้ส่วน GPL), mixer + ducking 12%, ปลุกด้วยเสียงต้องดังกว่าวิทยุ 2 เท่า, ชื่อสถานี/เพลงบนจอและนาฬิกา, host test resampler — build/บูตผ่าน รอ Wi-Fi ทดสอบสตรีม | [[12-Internet-Radio]] |
| 2026-10-15 | **คำสั่งเสียง "พักหน้าจอ"** (`rest_screen` → นาฬิกาหรี่แสงทันที) + **สถานะปิดไมค์จำข้ามรีบูต** (เดิมรีบูต/แฟลชแล้วไมค์เปิดเอง) | [[06-Avatar-and-Thai-UI#โหมดนาฬิกา ประหยัดพลังงาน (v3.2.1)]] |
| 2026-10-15 | **โหมดนาฬิกาประหยัดไฟ** (พักครบ 5 นาที → นาฬิกา + หรี่จอ 8%, ได้ยินเสียง/แตะ → สว่าง + ตื่น) + **สั่งเครื่องด้วยเสียง**: เสียง/แสงปรับขึ้นลง (`change`), `configure_device`, `mute_microphone` (เปิดไมค์ได้เฉพาะบนจอ) — ทดสอบบนบอร์ด: ตั้งเวลา 1 นาทีเข้าโหมดที่ 59 วิ | [[06-Avatar-and-Thai-UI#โหมดนาฬิกา ประหยัดพลังงาน (v3.2.1)]] |
| 2026-10-15 | **ออก Release แบบแมนนวล** (ผู้ใช้ตัดสินใจ: แก้ billing ไม่ผ่าน) — `tools/publish_release.py` ทำ ร่าง → แนบไฟล์ → เผยแพร่ → ตรวจ sha256; workflow เหลือแบบกดรันเอง | [[02-Installation-and-Setup#ติดตั้งจากไฟล์เฟิร์มแวร์สำเร็จรูป (ไม่ต้อง build)]] |
| 2026-10-15 | **Release v3.2.1 แรกบน GitHub** (factory.bin + sha256) — repo ใช้ immutable releases: v3.2.0 ใช้ไม่ได้ถาวร (tag ถูกล็อกจาก Release ที่สร้างผิดลำดับ) จึงออกเป็น v3.2.1; workflow แก้เป็น ร่าง → แนบไฟล์ → เผยแพร่ • Actions ยังรันไม่ได้เพราะบัญชี GitHub ติดปัญหา billing • commit ใช้อีเมล noreply | [[02-Installation-and-Setup#ติดตั้งจากไฟล์เฟิร์มแวร์สำเร็จรูป (ไม่ต้อง build)]] |
| 2026-10-15 | **อัปขึ้น GitHub** (`skyliner2008/PersonalAIBot-ESP32`) + **Release อัตโนมัติ**: GitHub Actions build ทุก push, ออก Release + แนบ factory.bin เมื่อเปลี่ยน `FIRMWARE_VERSION`, รัน host test ด้วย g++ (`host_tests/run_tests.sh`) | [[02-Installation-and-Setup#ติดตั้งจากไฟล์เฟิร์มแวร์สำเร็จรูป (ไม่ต้อง build)]] |
| 2026-10-15 | **รหัส Wi-Fi / API key ย้ายไปอยู่ในการ์ด SD** (`agent/secrets.json` เข้ารหัส, ไม่มีการ์ด → NVS) + ตั้งค่าครั้งแรกด้วย `ai-setup.txt` จากคอมพิวเตอร์ (บอร์ดนำเข้า เข้ารหัส แล้วลบ) + ปุ่มสร้างไฟล์ใน Studio | [[07-Troubleshooting-Log#T042 รหัส Wi-Fi และ API key เก็บเป็นข้อความธรรมดาในแฟลช]] |
| 2026-10-15 | **พร้อมเผยแพร่บน GitHub**: LICENSE (MIT), `tools/make_release.py` สร้างเฟิร์มแวร์ไฟล์เดียว (ไม่ยอมสร้างถ้า `secrets.h` มีค่าจริง + สแกนหา key), `tools/pio_strip_paths.py` ตัด path เครื่องผู้ build ออกจากเฟิร์มแวร์, README มีทางเลือกแฟลชผ่านเว็บ, ลบชื่อ Wi-Fi/path ส่วนตัวใน wiki — build จาก clone สะอาดผ่าน, สแกน 136 ไฟล์ไม่พบข้อมูลส่วนตัว | [[02-Installation-and-Setup#ติดตั้งจากไฟล์เฟิร์มแวร์สำเร็จรูป (ไม่ต้อง build)]] |
| 2026-10-15 | **เข้ารหัสรหัส Wi-Fi / API key / MT5 token ในแฟลช** (AES-256-GCM ผูกกับชิป) + ล้างสำเนาข้อความธรรมดาเดิม + ปิดสำเนาของไดรเวอร์ Wi-Fi — ตรวจด้วย dump NVS: ข้อความธรรมดา 5 → 0 | [[07-Troubleshooting-Log#T042 รหัส Wi-Fi และ API key เก็บเป็นข้อความธรรมดาในแฟลช]] |
| 2026-10-15 | **README 2 ภาษาสำหรับ GitHub** (`README.md` อังกฤษ + `README.th.md` ไทย, ภาพหัวดวงตา + การ์ด, แผนภาพ mermaid) • `.gitignore` เพิ่ม `recordings/`, `*.wav`, `__pycache__/` • ปิดชื่อ Wi-Fi ในภาพ pc-studio-settings | [README.md](../README.md) |
| 2026-10-15 | **ตรวจ ESP32-AI Studio ทุกปุ่ม**: ยุบเมนูซ้ำ 6 จุด, แก้การอัปเดตอัตโนมัติหยุดเอง, template tool ตัวเลข • **TLS ใช้ PSRAM** — RAM ภายในต่ำสุดจาก 15 → 85 KB, เลิก error -0x7F00 | [[10-PC-Studio-App#ตรวจปุ่มทั้งแอป (2026-10-15)]] · [[07-Troubleshooting-Log#T041 TLS ล้มเหลว -0x7F00 เมื่อเปิดหลายการเชื่อมต่อ - RAM ภายในไม่พอ]] |
| 2026-10-15 | **+14 tools พอร์ตจาก PersonalAIBot** (คิดเลข, แปลงหน่วยไทย, เวลาโลก+DST, ตำแหน่ง IP, Wikipedia, โน้ต, ภาพรวมตลาด, วิเคราะห์เทคนิคหลายกรอบเวลา+ปัจจัยพื้นฐาน, สแกนหุ้น, ปฏิทินเศรษฐกิจ, ขนาด lot, แจ้งเตือนราคาด้วยเสียง, MT5 อ่านพอร์ต/ส่งคำสั่งแบบยืนยัน 2 ขั้น) — ทดสอบบนบอร์ดครบ | [[11-Data-Tools-and-Cards#2) Tools ที่พอร์ตจาก PersonalAIBot (`composeApp`)]] |
| 2026-10-15 | **+5 tools ข้อมูลสด + การ์ดกราฟิก** (อากาศ+PM2.5, น้ำมันบางจาก, TradingView, แปลงเงิน, ข่าว) — พอร์ตแนวทางจาก PersonalAIBot, ทดสอบกับ AI จริง | [[11-Data-Tools-and-Cards]] |

---

> [!tip] Obsidian Tip
> `Ctrl+Click` ที่ `[[WikiLink]]` เพื่อเปิดหน้า · `Ctrl+G` เพื่อดู Graph View
