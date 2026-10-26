---
tags: [assistant, persona, voice, tools, settings, fallback, tokens]
created: 2026-10-14
updated: 2026-10-14
status: implemented
---

# 🧑‍💼 Assistant: Profiles, Tools, Settings & Model Fallback (v3.1)

> **Related:** [[00-Home-Dashboard]] | [[05-Gemini-Live-API-Protocol]] | [[06-Avatar-and-Thai-UI]] | [[08-Long-Term-Memory-GraphRAG]] | [[10-PC-Studio-App]]
> **Code:** `src/agent/Personas.*` · `src/agent/AgentTools.*` · `src/agent/Scheduler.*` · `src/agent/TaskList.*` · `src/agent/Consolidation.*` · `src/ai/ModelFallback.*` · `src/app/Settings.*` · `src/ui/SettingsScreens.*`

---

## 1. โปรไฟล์ผู้ช่วย (เสียง • นิสัย • เพศ • ชื่อ • สีตา)

ทุกอย่างปรับได้ 3 ทาง: **เมนูทัช** (⚙ มุมขวาบน หรือกด BOOT ค้าง 1.5 วิ) · **สั่งด้วยเสียง** (`configure_assistant`) · **Serial** (`set ...`)

| โปรไฟล์ | ชื่อ | เสียง (Gemini) | เพศ | นิสัย | สีตา |
|---|---|---|---|---|---|
| `khaimuk` | ไข่มุก | Aoede (สบายๆ) | หญิง | เพื่อนสนิทร่าเริง | ฟ้า |
| `phum` | ภูมิ | Puck (ร่าเริง) | ชาย | เพื่อนสนิทร่าเริง | เขียวมิ้นต์ |
| `lina` | ลีน่า | Kore (หนักแน่น) | หญิง | เลขาฯ มืออาชีพ | ขาว |
| `thanwa` | ธันวา | Charon (ชัดเจน) | ชาย | ปราชญ์สุขุม | อำพัน |
| `maprang` | มะปราง | Leda (อ่อนวัย) | หญิง | น้องน่ารัก | ชมพู |
| `om` | โอม | Fenrir (มีพลัง) | ชาย | สายฮา | ส้ม |
| `nam` | น้ำ | Sulafat (อบอุ่น) | หญิง | พี่เลี้ยงใจดี | ม่วง |
| `kru` | ครูเก่ง | Sadaltager (รอบรู้) | ชาย | ติวเตอร์ | น้ำเงิน |

- **เสียง:** ทั้ง 30 prebuilt voices ของ Gemini (Zephyr … Sulafat) ใช้ได้กับ Live native-audio ทุกตัว; "เพศ" ในตารางคือโทนเสียงที่รับรู้ (ไม่ใช่ field ของ API)
- **เพศ/คำลงท้าย:** หญิง → "ค่ะ/คะ", ชาย → "ครับ", กลาง → สุภาพเป็นกลาง (ใส่ใน system prompt)
- **นิสัย 7 แบบ:** friendly, secretary, caring, funny, tutor, calm, cute
- เปลี่ยนเสียง/นิสัย/ชื่อ → มีผล **หลังคำตอบปัจจุบันจบ** (เริ่ม session ใหม่ เพราะ voice อยู่ใน setup) ความจำระยะยาวยังอยู่ครบ
- ตัวอย่างสั่งด้วยเสียง: "เปลี่ยนเป็นมะปรางหน่อย", "ขอเสียงผู้ชายแบบสุขุม", "เรียกฉันว่าพี่โจนะ", "เปลี่ยนสีตาเป็นชมพู"

---

## 2. เมนูตั้งค่า (ทัช) + คีย์บอร์ดไทย/อังกฤษ

```
⚙ ตั้งค่า
├── โปรไฟล์ผู้ช่วย        (8 preset)
├── ชื่อผู้ช่วย / ชื่อของคุณ  (คีย์บอร์ดไทย-อังกฤษ)
├── เสียงพูด (30) • นิสัย (7) • เพศ/คำลงท้าย • สีดวงตา (8)
├── Wi-Fi               (สแกนเครือข่าย → รหัสผ่าน, หรือป้อนชื่อเอง · เครือข่ายที่ "จำไว้" แตะแล้วต่อเลย · แตะตัวที่ตั้งอยู่ = ใส่รหัสใหม่)
│   └── Wi-Fi ที่จำไว้   (สูงสุด 6 · แตะ = ลืม, มียืนยัน — T099)
├── API Key             (คีย์บอร์ดอังกฤษ, ซ่อน/แสดง)
├── โมเดล AI            (3.8 Live / 3.8 Extended Thinking / 3.1 Flash Live) + ระดับการคิด
├── ระดับเสียง • ความสว่างจอ • พักเมื่อเงียบ (1/3/5/10/30 นาที/ไม่พัก)
├── จัดความจำอัตโนมัติ (Flash-Lite) เปิด/ปิด
├── ค้นหาเว็บ (Google Search) เปิด/ปิด   (ค่าเริ่มต้นปิด — key free tier ใช้ไม่ได้ ดู T048/T049)
├── ความจำ → ล้างความจำระยะยาว / ล้างประวัติบทสนทนา (มียืนยัน)
├── ปรับการแตะหน้าจอ (calibrate) • หมุนจอ 180° • "พื้นจอเป็นสีขาว? แตะเพื่อสลับสี"
├── ทดสอบ API key       (ผลขึ้นบนหน้าตา)
├── เริ่มบทสนทนาใหม่ • ข้อมูลระบบ • รีสตาร์ทเครื่อง
```

ตั้งค่าทั้งหมดจาก PC ได้ด้วย [[10-PC-Studio-App]]

**คีย์บอร์ด** (320×240, ปุ่ม 32×40 px): `abc/ABC` · `123/#+=` · `ก-ฝ` · `พ-ฮ` · `สระ` (สระบน/ล่าง/วรรณยุกต์แสดงบน "อ" สีจาง) · เว้นวรรค · ตกลง
⌫ ลบทีละตัวอักษร (code point), กดค้าง ⌫ = ลบทั้งหมด · ตัวนับไบต์มุมขวาบน (ไทย 1 ตัว = 3 ไบต์)

การตั้งค่าเก็บใน **NVS** (namespace `cfg`) — `include/secrets.h` ใช้เป็นค่าเริ่มต้นตอนบูตครั้งแรกเท่านั้น

> [!WARNING] API key ใน NVS ไม่ได้เข้ารหัส (flash encryption ปิดอยู่) ใครเข้าถึงบอร์ดอ่านได้ — ใช้ key ที่จำกัดโควตา

### Touch calibration
ทัช FT6336G ของบอร์ดแต่ละรุ่นอาจสลับ/กลับแกน — ตอนบูตครั้งแรกจะขึ้นหน้า **"แตะกลางวงกลม (1/3)"** ให้แตะ 3 จุด
(ซ้ายบน → ขวาบน → ซ้ายล่าง) ระบบคำนวณแกน การกลับด้าน และสเกลเอง แล้วเก็บใน NVS
ถ้าแตะเฟืองไม่ติด: **กดปุ่ม BOOT ค้าง 1.5 วินาที** = เปิดเมนูตั้งค่าเสมอ

---

## 3. Tools ของผู้ช่วยส่วนตัว (44 ฟังก์ชัน + Google Search ถ้าเปิด)

> v3.1: +5 tools ข้อมูลสดพร้อมการ์ดกราฟิก — `get_weather`, `get_oil_price`, `get_market_quote`, `convert_currency`, `get_news` → [[11-Data-Tools-and-Cards]]
> v3.2: +14 tools พอร์ตจาก PersonalAIBot — `calculate`, `convert_units`, `get_world_time`, `get_location`, `search_knowledge`, `notes` + finance pack (`market_overview`, `market_analysis`, `market_scan`, `economic_calendar`, `position_size`, `price_alert`) (MT5 `mt5_account`/`mt5_trade` เคยมีใน v3.2–v3.3 — ถอดออกใน v3.4) → [[11-Data-Tools-and-Cards#2) Tools ที่พอร์ตจาก PersonalAIBot (`composeApp`)]]

| กลุ่ม | Tools |
|---|---|
| ความจำ | `save_memory`, `save_relation`, `recall_memory`, `forget_memory`, `recall_conversation` |
| เวลา | `set_timer` (นาที/วินาที), `set_reminder` (`YYYY-MM-DD HH:MM` / `HH:MM` / `tomorrow HH:MM`, repeat none/daily/weekdays/weekly), `list_reminders`, `cancel_reminder` |
| งาน (to-do) | `add_task` (priority 1-3, due), `list_tasks`, `update_task` (done/undone/delete/rename) |
| ผู้ช่วย | `about_me` (ตัวตน + ความสามารถทั้งหมด สร้างตอนเรียกจาก tools ที่ประกาศจริง — T096), `get_device_status`, `set_expression` (14 อารมณ์ของดวงตา), `configure_assistant`, `list_assistant_options` |
| อุปกรณ์ | `set_volume` / `set_brightness` (`level` หรือ `change` +/- เช่น "ลดเสียงลงหน่อย" = -10), `configure_device` (ความไวไมค์ / mic_change, sleep_minutes, clock_minutes, dim_brightness), `rest_screen` ("พักหน้าจอ" = นาฬิกาหรี่แสงทันทีหลังตอบ), `play_radio` / `stop_radio` (วิทยุออนไลน์ → [[12-Internet-Radio]]), `mute_microphone` (ปิดได้อย่างเดียว — เปิดต้องกดปุ่ม 🎤 บนจอ; สถานะปิดไมค์จำข้ามการรีบูต `cfg/muted`), `control_led`, `go_to_sleep` · `vehicle` (หน้าปัดรถ — ประกาศเฉพาะเมื่อเลือกแหล่งข้อมูล: status / open / close / set_source → [[16-Vehicle-Dash]]) · `navigate` (Google Maps บนมือถือ — ประกาศเฉพาะเมื่อจับคู่แอปมือถือแล้ว: go / stop / status / where; ระหว่างนำทาง AI พูดเตือนก่อนเลี้ยวเอง → [[18-Phone-Link-Navigation-Plan]]) |
| ในตัว | **Google Search** — ปิดเป็นค่าเริ่มต้น (ต้องมี key ที่มีสิทธิ์ Search); ถ้าปิด เรื่องล่าสุดใช้ `get_news(topic)` |

- **`about_me` — รู้จักตัวเองแบบไม่ต้องแก้มือ (T096):** ถูกเรียกเมื่อผู้ใช้ถาม "เป็นใคร / ทำอะไรได้บ้าง / แนะนำ-รีวิวตัวเอง / ทำ X ได้ไหม"
  (กติกาใน system prompt) · ผลสร้าง**ตอนเรียก**: รัน `addDeclarations()` อีกรอบพร้อมตัวเก็บ (`group()` เปิดหมวด, `declare()` ใส่ tool
  + ประโยคแรกของคำอธิบาย) → tool ใหม่ที่ประกาศในหมวดไหนก็โผล่ในหมวดนั้นเอง, หมวดที่ไม่มี tool (ไม่มีอุปกรณ์/มือถือ) ตัดทิ้ง ·
  `assistant` (ชื่อ/เสียง/นิสัย), `device` (เวอร์ชันเฟิร์มแวร์ — แทน "อัปเดตล่าสุด" ที่ต้องแก้มือแบบ `self_review.md` ของแอปมือถือ, ฮาร์ดแวร์, คำปลุก),
  `connected` (Google Search ที่ setup เสนอจริง, ชุดการเงิน, มือถือ, หน้าปัด, อุปกรณ์ที่ต่อ), `how_to_present` (โหมดเล่า 2–3 นาที ครบทุกหมวด
  พร้อมตัวอย่าง — ข้อยกเว้นของการตอบสั้น, ไม่อ่านชื่อ tool; T098) · รายการชั่วคราวสร้างใน PSRAM (T098)
  · ผล ~5 KB (ส่งเฉพาะตอนเรียก) · คำประกาศ +~200 ไบต์/เทิร์น · **เพิ่ม tool ใหม่:** ประกาศใต้ `group(...)` ที่เหมาะสมใน `AgentTools::addDeclarations` แค่นั้น
- คำอธิบาย tools เป็นภาษาอังกฤษแบบสั้น (≈6 KB JSON ≈ 1.6k tokens) เพราะถูกนับทุก turn
- **behavior ตามโมเดล:** 3.8 Live = `BLOCKING` (ยกเว้น `set_expression` = `NON_BLOCKING` + `scheduling: SILENT` — field ของ FunctionResponse ข้าง `response`, ใส่ใน `response` แล้วโมเดลพูดซ้ำ 2 รอบ T071), 3.8 Extended Thinking = `NON_BLOCKING` ทั้งหมด (BLOCKING = hard error), 3.1 Flash Live = ไม่ใส่ field
- **การแจ้งเตือน:** ถึงเวลา → เสียงกริ๊ง 2 โน้ตทุก 2.5 วิ (สูงสุด 30 วิ หรือจนแตะจอ), ตาตกใจ, ข้อความบนจอ และส่ง `clientContent` "[แจ้งเตือนจากระบบ] ..." ให้ผู้ช่วยพูดบอก; ถ้ากำลังพักจะตื่นเอง
- ไฟล์: `agent/reminders.json`, `agent/tasks.json` (เขียนแบบ atomic ผ่าน `.tmp`)

---

## 4. โมเดลและงบโทเคน (65k TPM)

| โมเดล | ใช้ทำอะไร | ลิมิตของบัญชี |
|---|---|---|
| `gemini-3.8-live` (ค่าเริ่มต้น) | สนทนาเสียง | ∞ RPM • 65k TPM • ∞ RPD |
| `gemini-3.8-live-extended-thinking` | สำรอง / คิดลึก | เหมือนกัน |
| `gemini-3.1-flash-live-preview` | สำรองสุดท้าย | เหมือนกัน |
| `gemini-3.5-flash-lite` → `gemini-3.1-flash-lite` | จัดระเบียบความจำเบื้องหลัง | 15 RPM • 250k TPM • 500 RPD |

Live API **คิดโทเคนของบริบททั้งหมดซ้ำทุก turn** และ 3.8 มี proactive audio (นับตลอดเวลาที่ฟัง) จึงทำ:
1. **contextWindowCompression** `triggerTokens 12000` → `slidingWindow.targetTokens 5000`
2. **Local VAD (hybrid)**: ส่งเสียงเฉพาะตอนพูดจริง (เกณฑ์ noise floor ×2.5, ขั้นต่ำ RMS 0.004, pre-roll 600 ms, hangover 1.5 s) + `audioStreamEnd` เมื่อหยุดพูด
   · ฟังต่อเนื่อง 6 วิหลัง AI พูดจบ · AGC ยกเสียงเบา (≤ ×6) · server VAD start HIGH / end LOW · log `[Mic]` ทุกประโยค ([[07-Troubleshooting-Log#T036 ไอคอนไมค์ขึ้นแต่ AI เงียบ - 3.8 Live ไม่ค่อยตอบ]])
3. **Auto-sleep** หลังเงียบ N นาที: ปิด session (ไม่เสียโทเคน) — ปลุกด้วยการ**พูด** (4 chunk ติดกัน) หรือแตะจอ; เสียงที่พูดตอนปลุก (≤2 s) ถูกส่งต่อหลังเชื่อมต่อใหม่
4. ส่วนบุคคลใน system prompt จำกัด (core memory ≤1.5 KB, บทสนทนาเก่า ≤6 turns/1.2 KB)
5. แสดง **โทเคน/นาที** (จาก `usageMetadata`) ในหน้าข้อมูลระบบ

---

## 5. Auto fallback ของโมเดล

`src/ai/ModelFallback.*` — ถ้าโมเดลที่ใช้อยู่มีปัญหา จะพักโมเดลนั้น (cool-down) แล้วสลับไปตัวถัดไปอัตโนมัติ และกลับมาใช้ตัวที่เลือกไว้เมื่อพ้น cool-down

| สาเหตุ (จาก close code/reason หรือ HTTP status) | Cool-down |
|---|---|
| Quota / RESOURCE_EXHAUSTED / 429 | 10 → 20 → 40 นาที ... สูงสุด 2 ชม. |
| Unavailable / overloaded / internal / 5xx / 1011 / 1007 | 5 นาที |
| Not found / not supported / 404 / 1008 | 24 ชม. |
| หลุดก่อน `setupComplete` ติดกัน 3 ครั้ง / ไม่ตอบ setup ใน 15 วิ | 2 นาที |
| **API key ผิด / permission** | **ไม่สลับ** — แจ้ง "API key ใช้ไม่ได้" |

ลำดับการตัดสินใจเมื่อเซสชัน Live ถูกปิด (v3.1):

```
close frame ──► classifyClose()
   Auth ─────────────► หยุด + "API key ใช้ไม่ได้" (แตะจอ = ลองใหม่)
   SessionExpired (1008 session not found/expired) ─► ทิ้ง resume handle แล้วเริ่ม session ใหม่ (โมเดลเดิม)   [T034]
   Quota และ setup มี googleSearch ─► ลองโมเดลเดิมใหม่โดยไม่มี Search ทันที   [T028, T045, T048]
        session ไม่มี Search ใช้ได้ ─► Search เป็นต้นเหตุ ─► ปิด 6 → 12 → 24 → 48 ชม. (ล้มติดกัน)
                                     จำข้ามรีบูต (NVS "rt": srchoff, srchn) + "Google Search ใช้ไม่ได้ (โควตา)"
        ไม่มี Search ก็ quota ─► ไม่ใช่ Search (session ค้างจากรีบูต / โควตาโมเดล) ─► Search ยังเปิด
        session ที่มี Search ใช้ได้ ─► ล้างตัวนับ
   อื่นๆ ─► cool-down โมเดลนั้น ─► ยังมีโมเดลว่าง? ─ ใช่ ─► สลับ + "สลับไปใช้ …"
                                              └ ไม่ ─► หยุดพัก (hold) จนโมเดลแรกพ้น cool-down     [T027]
                                                       จอ: "โควตา API key หมดทุกโมเดล • ลองใหม่ HH:MM หรือแตะจอ"
```

- Live: เลือกโมเดลใหม่ตอนสร้าง setup ทุกครั้ง (handle ของ session resumption ผูกกับโมเดล จึงเริ่ม session ใหม่เมื่อสลับ) — จอแสดง "สลับไปใช้ …"
- cool-down **ไม่ถูกย่อ** ด้วยความผิดพลาดที่เบากว่า และ disconnect ที่ตามหลัง close frame ไม่ถูกนับซ้ำ
- Text: ถ้า 3.5 Flash-Lite ล้ม จะลอง 3.1 Flash-Lite ในรอบถัดไป (เว้น 30 วิ)
- เปลี่ยน API key = ล้าง cool-down ทั้งหมด
- ทดสอบใน `host_tests` (`model_fallback`, `model_fallback_key_wide_quota`)

### ทดสอบ API key บนเครื่อง (`apitest` / เมนู "ทดสอบ API key" / PC app)

ใช้ key ที่เก็บในบอร์ด เรียก REST: `GET models/{id}` ทั้ง 5 โมเดล + `generateContent` 1 โทเคน → รายงาน HTTP status,
ข้อความ error และ **QuotaFailure** (quotaId, limit, model) ซึ่ง close reason ของ WebSocket ถูกตัดไป (123 ไบต์)
ผลสรุปขึ้นจอ เช่น "API key ใช้ได้ • เห็นโมเดล 5/5 • เรียก Flash-Lite ได้"

---

## 6. จัดระเบียบความจำเบื้องหลัง (Flash-Lite)

หลังบทสนทนาเงียบ ≥45 วิ และมี ≥4 turn ใหม่ → ส่ง turn ล่าสุด + core memory ไปที่ Flash-Lite (structured output JSON `facts[]`, `relations[]`) → `MemoryStore` รวมข้อความซ้ำเอง
จำกัด 1 ครั้ง/5 นาที, ≤150 ครั้ง/วัน (ต่ำกว่า 500 RPD), ทำงานใน task `textModel` (HTTPS + TLS verified) ไม่บล็อกเสียง
ปิดได้ในเมนู "จัดความจำอัตโนมัติ"

---

## 7. Serial console (สำหรับทดสอบ)

```
set ssid <ชื่อ> | set pass <รหัส> | set key <API key> | set agent <ชื่อ> | set user <ชื่อ>
set profile maprang | set voice Puck | set personality cute | set gender male | set eye pink
set model 0-2 | set thinking 0-2 | set textmodel 0-1 | set volume 0-100 | set brightness 5-100
set sleep <นาที> | set rotation 1|3 | set invert 0|1 | set search 0|1 | set consolidate 0|1
tool set_timer {"seconds":30,"label":"ทดสอบ"} | tool list_tasks {}        → TOOL <name> <json>
ask <ข้อความ>   (พิมพ์คุยกับ AI)  | mute | retry | new | memclear facts|history
expr love | sleep | wake | time <unix-epoch> | reboot
ui open menu|wifi|apikey|agent|user|profile|voice|info|calibrate | ui tap x y | ui type ข้อความ | screenshot
PC app: config | options | tools | diag | wifiscan | apitest              → <TAG> <json> หนึ่งบรรทัด
วินิจฉัย: keymode query|header | setupmode full|nosearch|min|searchonly   (ไม่บันทึกถาวร; searchonly = โมเดล + Search เท่านั้น → log [Diag])
          trace [on|off]   (ทุกข้อความรับ/ส่งกับ Live API เป็นบรรทัด [Trace] — ก้อนเสียงแค่นับ; ไม่บันทึกถาวร)
```

`config` ไม่พิมพ์ API key / รหัส Wi-Fi — มีแค่ `keySet`, `keyLen`, `keyHint` (3 ตัวแรก…4 ตัวท้าย) ไว้ตรวจว่าใส่ key ถูกตัว
ดู PC app ที่ [[10-PC-Studio-App]]
