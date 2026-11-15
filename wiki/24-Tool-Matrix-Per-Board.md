---
tags: [tools, boards, reference]
created: 2026-11-15
status: active
---

# 24 — ตาราง Tool แยกตามบอร์ด

> **Related:** [[20-Tool-Command-Reference]] (พารามิเตอร์ + ตัวอย่างคำสั่ง) · [[07-Troubleshooting-Log#T203]] · [[07-Troubleshooting-Log#T204]] · [[22-Mini-OLED-Boards-XHS3E-XminiC3]] · [[19-ESP32-P4-JC4880P443C]]

รายการจากซอร์ส v4.9.8 (`AgentTools::addDeclarations()` ของแต่ละบอร์ด) + ผลยิง `tool <ชื่อ> <json>` ใส่บอร์ดจริงวันที่ 2026-11-14/15

**สัญลักษณ์**

| | ความหมาย |
|---|---|
| ✅ | มี และทดสอบผ่านบนบอร์ดจริง |
| ☑️ | มี แต่ยังไม่ได้ทดสอบบนบอร์ดจริง (เปลี่ยนการตั้งค่า / ส่งเสียง / ลบคืนไม่ได้) |
| ⚠️ | มี แต่ใช้ได้บางส่วน (ดูหมายเหตุ) |
| ⚙️ | ประกาศให้ AI เฉพาะเมื่อตั้งค่า / เชื่อมต่อแล้ว |
| ❌ | ไม่มีบนบอร์ดนี้ |

> **ES3C35P (จอ 3.5", T205):** ซอร์สเดียวกับ ES3C28P → tool ชุดเดียวกับคอลัมน์ ES3C28P ทุกตัว (49) — ยังไม่ได้ทดสอบบนบอร์ดจริง · [[25-ES3C35P-3.5inch]]

**บอร์ดที่ทดสอบ**

| บอร์ด | โฟลเดอร์ | เฟิร์มแวร์ตอนทดสอบ | tool สูงสุดที่ประกาศได้ | ผล |
|---|---|---|---|---|
| ES3C28P (ESP32-S3) | `src/` | 4.9.6 | 49 | 60 คำสั่งผ่านครบ |
| JC4880P443C (ESP32-P4) | `firmware-p4/` | 4.9.3 | 52 | 69 คำสั่งผ่านครบ (ยิงทีละบอร์ด) |
| XH-S3E-AI (ESP32-S3 mini) | `firmware-s3-mini/` | 4.9.5 | 47 | 57 คำสั่งผ่านครบ (ยิงทีละบอร์ด) · ค้างทั้งบอร์ด 1 ครั้ง → ตัวกันค้าง T204 |
| Xmini-C3 (ESP32-C3) | `firmware-c3-mini/` | 4.9.7 + แก้ T203 | 44 | 38/40 ผ่าน (2 = error ที่ตั้งใจ) |

---

## ตารางรวม

| # | Tool | ทำอะไร | ES3C28P | P4 | XH-S3E-AI | Xmini-C3 |
|---|---|---|:-:|:-:|:-:|:-:|
| | **ตัวตน** | | | | | |
| 1 | `about_me` | บอกชื่อและความสามารถจาก tool ที่ประกาศจริง | ✅ | ✅ | ✅ | ✅ |
| | **ความจำระยะยาว** | | | | | |
| 2 | `save_memory` | จำข้อเท็จจริงเกี่ยวกับผู้ใช้ | ✅ | ✅ | ✅ | ✅ |
| 3 | `save_relation` | จำความสัมพันธ์ (knowledge graph) | ☑️ | ☑️ | ☑️ | ☑️ |
| 4 | `recall_memory` | ค้นความจำ | ✅ | ✅ | ✅ | ✅ |
| 5 | `forget_memory` | ลบความจำ | ✅ | ✅ | ✅ | ✅ |
| 6 | `recall_conversation` | ค้นบทสนทนาเก่า | ✅ | ✅ | ✅ | ✅ |
| | **ตัวจับเวลา / เตือน** | | | | | |
| 7 | `set_timer` | นับถอยหลัง | ✅ | ✅ | ✅ | ✅ |
| 8 | `set_reminder` | เตือนตามเวลา / ปลุก (P4: `ai_task` ให้ AI ทำงานเอง) | ✅ | ✅ | ✅ | ✅ |
| 9 | `list_reminders` | ดูรายการเตือน | ✅ | ✅ | ✅ | ✅ |
| 10 | `cancel_reminder` | ยกเลิกเตือน | ✅ | ✅ | ✅ | ✅ |
| 11 | `background_task` | งานหลายขั้นตอนทำเบื้องหลัง | ❌ | ✅ ¹ | ❌ | ❌ |
| | **รายการสิ่งที่ต้องทำ** | | | | | |
| 12 | `add_task` | เพิ่มงาน | ✅ | ✅ | ✅ | ✅ |
| 13 | `list_tasks` | ดูงาน | ✅ | ✅ | ✅ | ✅ |
| 14 | `update_task` | ทำเสร็จ / เปิดใหม่ / ลบ / เปลี่ยนชื่อ | ✅ | ✅ | ✅ | ✅ |
| | **ตัว AI / จอ / เสียง** | | | | | |
| 15 | `get_device_status` | วันเวลา แบต Wi-Fi การตั้งค่า | ✅ | ✅ | ✅ | ✅ |
| 16 | `set_expression` | แสดงอารมณ์ทางดวงตา | ❌ ² | ✅ | ❌ ² | ❌ ² |
| 17 | `configure_assistant` | เปลี่ยนโปรไฟล์ เสียง นิสัย ชื่อ สีตา | ☑️ | ☑️ | ☑️ | ☑️ |
| 18 | `list_assistant_options` | ดูตัวเลือกโปรไฟล์ / เสียง / นิสัย / สีตา | ✅ | ✅ | ✅ | ✅ |
| 19 | `set_volume` | ความดังวิทยุ | ☑️ | ☑️ | ☑️ | ☑️ |
| 20 | `set_brightness` | ความสว่างจอ | ☑️ | ☑️ | ☑️ | ☑️ |
| 21 | `configure_device` | ไวไมค์ เวลาหลับเอง หน้านาฬิกา | ☑️ | ☑️ | ☑️ | ☑️ |
| 22 | `rest_screen` | พักหน้าจอ (นาฬิกาหรี่) | ☑️ | ☑️ | ☑️ | ☑️ |
| 23 | `control_led` | ไฟ RGB | ✅ | ❌ ³ | ✅ | ❌ ³ |
| 24 | `go_to_sleep` | หลับหลังตอบจบ | ☑️ | ✅ | ☑️ | ✅ |
| 25 | `play_radio` | วิทยุ / เพลง MP3 จาก SD | ☑️ | ☑️ | ☑️ | ❌ ⁴ |
| 26 | `stop_radio` | ปิดวิทยุ | ✅ | ✅ | ✅ | ❌ ⁴ |
| 27 | `mute_microphone` | ปิดไมค์ | ☑️ | ☑️ | ☑️ | ☑️ |
| | **ฮาร์ดแวร์ / รถ / มือถือ** | | | | | |
| 28 | `device` | รีเลย์ dimmer เซนเซอร์ที่ต่อเพิ่ม | ⚙️ ⁵ | ⚙️ ⁵ | ⚙️ ⁵ | ⚙️ ⁵ |
| 29 | `vehicle` | หน้าปัดรถ OBD-II / K-Line | ✅ | ❌ | ❌ | ❌ |
| 30 | `navigate` | นำทาง Google Maps ผ่านแอปมือถือ | ⚙️ ✅ | ❌ | ⚙️ ⁶ | ⚙️ ✅ |
| 31 | `camera` | ถ่ายภาพจากกล้องหน้าให้ AI ดู | ❌ | ⚙️ ✅ ⁷ | ❌ | ❌ |
| 32 | `modbus` | อ่าน / เขียน Modbus RTU ผ่าน RS485 | ❌ | ⚙️ ⁸ | ❌ | ❌ |
| | **ข้อมูลสด + การ์ดบนจอ** | | | | | |
| 33 | `get_weather` | อากาศ PM2.5 พยากรณ์ | ✅ | ✅ | ✅ | ✅ |
| 34 | `get_oil_price` | ราคาน้ำมันวันนี้ / พรุ่งนี้ | ✅ | ✅ | ✅ | ✅ ⁹ |
| 35 | `get_market_quote` | ราคาหุ้น ทอง คริปโต FX | ✅ | ✅ | ✅ | ✅ |
| 36 | `convert_currency` | แปลงค่าเงิน | ✅ | ✅ | ✅ | ✅ |
| 37 | `get_news` | พาดหัวข่าว | ✅ | ✅ | ✅ | ✅ |
| | **เครื่องมือทั่วไป** | | | | | |
| 38 | `calculate` | เครื่องคิดเลข | ✅ | ✅ | ✅ | ✅ |
| 39 | `convert_units` | แปลงหน่วย (ไร่ บาททอง ...) | ✅ | ✅ | ✅ | ✅ |
| 40 | `get_world_time` | เวลาเมืองอื่น | ✅ | ✅ | ✅ | ✅ |
| 41 | `get_location` | ตำแหน่งโดยประมาณจาก IP | ✅ | ✅ | ✅ | ✅ |
| 42 | `search_knowledge` | สรุปจาก Wikipedia | ✅ | ✅ | ✅ | ✅ |
| 43 | `notes` | สมุดโน้ต | ✅ | ✅ | ✅ | ✅ |
| | **น้ำ (`set water 1`)** | | | | | |
| 44 | `water` | ระดับน้ำ ฝน พายุ (ThaiWater) | ✅ | ⚠️ ¹⁰ | ⚠️ ¹⁰ | ⚠️ ¹¹ |
| | **การเงิน (`set finance 1`, TradingView อ่านอย่างเดียว)** | | | | | |
| 45 | `market_overview` | ภาพรวมตลาด | ✅ | ✅ | ✅ | ✅ |
| 46 | `market_analysis` | วิเคราะห์เทคนิค (P4: `combined`) | ✅ | ✅ | ✅ | ✅ |
| 47 | `market_scan` | สแกนหุ้น / คริปโต (P4: squeeze / breakout) | ✅ | ✅ | ✅ | ✅ |
| 48 | `market_chart` | กราฟแท่งเทียน SMC สัญญาณ (P4: patterns) | ✅ | ✅ | ✅ | ✅ |
| 49 | `market_sentiment` | กลัว/โลภ หุ้น คริปโต เศรษฐกิจ | ✅ | ✅ | ✅ | ⚠️ ¹² |
| 50 | `market_signals` | สมุดสัญญาณกลยุทธ์ + เฝ้ากราฟ | ❌ | ✅ | ❌ | ❌ |
| 51 | `market_anticipation` | คาดการณ์ล่วงหน้าแบบเรียลไทม์ | ✅ ¹³ | ✅ ¹³ | ❌ | ❌ |
| 52 | `economic_calendar` | ปฏิทินข่าวเศรษฐกิจ | ✅ | ✅ | ✅ | ✅ |
| 53 | `position_size` | คำนวณขนาด lot / หุ้น | ✅ | ✅ | ✅ | ✅ |
| 54 | `day_trade` | เทรดรายวัน (ORB / backtest / setups) | ❌ | ✅ | ❌ | ❌ |
| 55 | `price_alert` | แจ้งเตือนราคา / indicator | ✅ | ✅ | ✅ | ✅ |
| | **รวมสูงสุดที่ประกาศได้** | | **49** | **52** | **47** | **44** |

**หมายเหตุ**

1. `background_task` ทดสอบแค่ `list` — `start` (ใช้โมเดลข้อความ 1–3 นาที) ยังไม่ได้ลอง
2. ถอดออกใน T140: โมเดล 3.8 Live หลุดพูดชื่อ tool ออกเสียง บอร์ดเลือกอารมณ์ดวงตาเองจากคำที่ AI พูด — **P4 ยังประกาศอยู่** ควรฟังว่ามีอาการเดียวกันไหม
3. ไม่มี LED บนบอร์ด (P4: `RGB_LED_COUNT 0`, C3: T181)
4. Xmini-C3 ไม่มีวิทยุ / เพลง SD (RAM ไม่พอ — T181)
5. ประกาศเมื่อมี `agent/devices.json` (wiki 15) — ไม่มีบอร์ดไหนตั้งค่าไว้ ทดสอบได้แค่ `list` (ได้รายการว่าง)
6. XH-S3E-AI ยังไม่ได้จับคู่แอปมือถือ (`paired: false`) จึงไม่ถูกประกาศตอนทดสอบ
7. ต้องมี session ของ Gemini เปิดอยู่ — ตอนบอร์ดหลับตอบ "connection busy"
8. ประกาศเมื่อตั้ง RS485 แล้ว (console `rs485 set ...`) — ไม่ได้ตั้งค่า ยังไม่ได้ทดสอบ
9. Xmini-C3 เคยรีบูตตอนเรียก 1 ครั้ง (Guru Meditation) ก่อนแก้ T203 — ลองซ้ำหลังแก้ 3 ครั้งไม่เกิดอีก ยังไม่รู้สาเหตุ
10. ฝน 3/5/7 วัน (ทั้งประเทศและรายจังหวัด) ได้ "no data" เพราะ ThaiWater ตอบ `data: []` (ตรวจด้วย curl 2026-11-15) — ปัญหาต้นทาง กระทบทุกบอร์ด; ระดับน้ำ ฝน 24 ชม. พายุ ใช้ได้
11. Xmini-C3 ต้องระบุจังหวัด (ข้อมูลทั้งประเทศ 4.8 MB เกิน RAM — AI จะถามจังหวัด) + หมายเหตุ 10
12. Xmini-C3 ไม่มีหัวข้อ `stocks` (ต้องใช้ ~120 KB) — แนะนำ `get_market_quote TVC:VIX` แทน; `crypto` / `economy` ใช้ได้
13. ทดสอบ `list` / `factors` / `summary` / `scan` — `create` / `remove` / `config` ไม่ได้ลอง (มีรายการเฝ้าทองของผู้ใช้อยู่บน P4)

---

## รายการแยกบอร์ด

### ES3C28P (ESP32-S3) — 49

- **มีทุกครั้ง (47):** about_me · save_memory · save_relation · recall_memory · forget_memory · recall_conversation · set_timer · set_reminder · list_reminders · cancel_reminder ·
  add_task · list_tasks · update_task · get_device_status · configure_assistant · list_assistant_options · set_volume · set_brightness · configure_device · rest_screen ·
  control_led · go_to_sleep · play_radio · stop_radio · mute_microphone · vehicle · get_weather · get_oil_price · get_market_quote · convert_currency · get_news ·
  calculate · convert_units · get_world_time · get_location · search_knowledge · notes · water ᵂ · market_overview ᶠ · market_analysis ᶠ · market_scan ᶠ ·
  market_chart ᶠ · market_sentiment ᶠ · economic_calendar ᶠ · position_size ᶠ · price_alert ᶠ · market_anticipation ᶠ
- **เมื่อตั้งค่า / เชื่อมต่อ (2):** device (devices.json) · navigate (จับคู่มือถือ)
- ไม่มี: set_expression · background_task · camera · modbus · market_signals · day_trade

### ESP32-P4 (JC4880P443C) — 52

- **มีทุกครั้ง (49):** ชุดเดียวกับ ES3C28P ยกเว้น `control_led` / `vehicle` + เพิ่ม `set_expression` · `background_task` · `market_signals` ᶠ · `day_trade` ᶠ
- **เมื่อตั้งค่า / เชื่อมต่อ (3):** device (devices.json) · camera (เจอกล้องตอนบูต) · modbus (ตั้ง RS485)
- ไม่มี: control_led · vehicle · navigate (ถอดออก T151)

### XH-S3E-AI (ESP32-S3 mini, OLED) — 47

- **มีทุกครั้ง (45):** ชุดเดียวกับ ES3C28P ยกเว้น `vehicle` และ `market_anticipation`
- **เมื่อตั้งค่า / เชื่อมต่อ (2):** device · navigate
- ไม่มี: vehicle · market_anticipation · set_expression · background_task · camera · modbus · market_signals · day_trade

### Xmini-C3 (ESP32-C3, OLED, ไม่มี PSRAM) — 44

- **มีทุกครั้ง (42):** ชุดเดียวกับ XH-S3E-AI ยกเว้น `control_led` / `play_radio` / `stop_radio`
- **เมื่อตั้งค่า / เชื่อมต่อ (2):** device · navigate
- ไม่มี: control_led · play_radio · stop_radio · vehicle · market_anticipation · set_expression · background_task · camera · modbus · market_signals · day_trade
- ทำงานต่างจากบอร์ดอื่น: `water` ทีละจังหวัด · `market_sentiment` ไม่มี `stocks` · ทุก tool ข้อมูลสดปิด session ของ Gemini ระหว่างดึงข้อมูลแล้วต่อใหม่ (ช้ากว่า ~3–5 วิ)

ᵂ = เปิด/ปิดด้วย `set water 1|0` · ᶠ = เปิด/ปิดด้วย `set finance 1|0` (ปิด = ไม่ถูกประกาศ ไม่กินโทเคน)

+ Google Search (ไม่ใช่ tool ของบอร์ด) ถ้าเปิด `set search 1` และ API key มีสิทธิ์ใช้

---

## วิธีตรวจซ้ำ

```
tools                                  # รายการที่ประกาศจริงตอนนี้ (JSON)
tool <ชื่อ> <json>                     # เรียก tool ตรงๆ เช่น tool get_weather {"location":"เชียงใหม่"}
```

- Xmini-C3: สั่ง `sleep` ก่อนทดสอบ tool ข้อมูลสดจาก console (RAM ไม่พอเปิด TLS สายที่สองข้าง session ของ Gemini)
- ยิงหลายบอร์ดพร้อมกันบน Wi-Fi เดียวกัน: TradingView อาจปฏิเสธการเชื่อมต่อ (จำกัดต่อ IP) — ทดสอบทีละบอร์ด

---

*Back to [[00-Home-Dashboard]]*
