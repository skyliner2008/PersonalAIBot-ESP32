# 20 — รายการ Tool + ตัวอย่างคำสั่ง

รายการ tool ทั้งหมดที่ AI บนบอร์ดเรียกใช้ได้ (ตาม `AgentTools::addDeclarations()` ใน `src/agent/AgentTools.cpp`, เฟิร์มแวร์ T144)
แต่ละ tool มีตัวอย่างประโยคที่พูดกับบอร์ด และคำสั่ง console สำหรับทดสอบตรงๆ

> **วิธีทดสอบ tool โดยไม่ต้องพูด:** เปิด serial (`pio device monitor`, แท็บ log ของ ESP32-AI Studio หรือแอปมือถือ) แล้วพิมพ์
> `tool <ชื่อ> <json>` เช่น `tool get_weather {"location":"เชียงใหม่"}` → บอร์ดตอบ `TOOL <ชื่อ> <json>`
> หรือ `ask <ข้อความ>` เพื่อพิมพ์คุยกับ AI เหมือนพูด

**สัญลักษณ์**
- 🖼️ = แสดงการ์ดบนจอ · ⏳ = ทำงานเบื้องหลัง (AI พูดต่อได้ระหว่างรอ แล้วตอบเมื่อข้อมูลมา)
- ⚙️ = มีเฉพาะเมื่อเปิดใช้ / มีอุปกรณ์ (ไม่เปิด = ไม่ถูกประกาศ ไม่กินโทเคน)
- พารามิเตอร์ **ตัวหนา** = ต้องใส่

ทั้งหมด 49 tool (+ Google Search ถ้าเปิด `set search 1` และ API key มีสิทธิ์ใช้ Search)

---

## 0. ตัวตน

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `about_me` | บอกว่าตัวเองชื่ออะไร ทำอะไรได้บ้างตอนนี้ (อ่านจาก tool จริงที่ประกาศอยู่) | – |

- 🗣️ "แนะนำตัวหน่อย" · "ทำอะไรได้บ้าง" · "ดูกราฟหุ้นได้ไหม"
- 💻 `tool about_me {}`

---

## 1. ความจำระยะยาว

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `save_memory` | จำข้อเท็จจริงเกี่ยวกับผู้ใช้ | **content**, category (profile / preference / person / event / task / knowledge / other), importance 1-5 |
| `save_relation` | จำความสัมพันธ์ (knowledge graph) | **subject**, **relation**, **object** |
| `recall_memory` | ค้นความจำ | **query**, limit 1-10 |
| `forget_memory` | ลบความจำที่ผิด | **memory_id** |
| `recall_conversation` | ค้นบทสนทนาเก่า | query, days_back 1-90 |

- 🗣️ "จำไว้นะว่าฉันแพ้กุ้ง" · "แมวฉันชื่อส้ม" · "ฉันแพ้อะไรบ้าง" · "เมื่อวานเราคุยเรื่องอะไรกัน" · "ลืมเรื่องที่ฉันชอบกาแฟไปเลย"
- 💻
  ```
  tool save_memory {"content":"ผู้ใช้แพ้กุ้ง","category":"profile","importance":5}
  tool save_relation {"subject":"ผู้ใช้","relation":"มีแมวชื่อ","object":"ส้ม"}
  tool recall_memory {"query":"แพ้ อาหาร"}
  tool forget_memory {"memory_id":12}
  tool recall_conversation {"query":"หุ้น","days_back":3}
  ```

---

## 2. ตัวจับเวลา + เตือนความจำ

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `set_timer` | นับถอยหลัง (บอร์ดส่งเสียงเมื่อครบ) | minutes, seconds, label |
| `set_reminder` | เตือนตามเวลา / ปลุก | **when** (`YYYY-MM-DD HH:MM` / `HH:MM` / `tomorrow HH:MM`), **label**, repeat (none / daily / weekdays / weekly), ai_task **(P4, T160)** = งานให้ AI ทำเองเมื่อถึงเวลา (แทนเสียงกริ่ง) |
| `list_reminders` | ดูรายการที่ตั้งไว้ | – |
| `cancel_reminder` | ยกเลิก | id หรือ all |
| `background_task` **(P4, T160)** | งานหลายขั้นตอนทำเบื้องหลัง (ค้นหลายแหล่ง/หลายสินทรัพย์) ขณะคุยต่อได้ เสร็จแล้ว AI พูดสรุปให้ (1-3 นาที) | **action** (start / status / list / cancel), goal (start), id (status / cancel) |

- 🗣️ "จับเวลา 5 นาที ต้มมาม่า" · "ปลุกตีหกครึ่งทุกวันธรรมดา" · "พรุ่งนี้บ่ายสองเตือนโทรหาหมอ" · "มีเตือนอะไรบ้าง" · "ยกเลิกเตือนทั้งหมด"
  - (P4) "ทุกวันธรรมดาแปดโมงครึ่ง สรุปตลาดหุ้นไทยให้ฟัง" · "ไปหาข้อมูลเทียบ BTC กับทองวันนี้ให้หน่อย เสร็จแล้วบอก" · "งานเบื้องหลังเสร็จหรือยัง"
- 💻
  ```
  tool set_timer {"minutes":5,"label":"ต้มมาม่า"}
  tool set_reminder {"when":"06:30","label":"ตื่นนอน","repeat":"weekdays"}
  tool set_reminder {"when":"tomorrow 14:00","label":"โทรหาหมอ"}
  tool set_reminder {"when":"08:30","label":"สรุปตลาด","repeat":"weekdays","ai_task":"สรุปตลาดหุ้นไทยเช้านี้"}
  tool list_reminders {}
  tool cancel_reminder {"all":true}
  tool background_task {"action":"start","goal":"เทียบราคาและแนวโน้มวันนี้ของ BTC กับทองคำ แล้วสรุปว่าตัวไหนแข็งแรงกว่า"}
  tool background_task {"action":"status","id":1}
  tool background_task {"action":"list"}
  tool background_task {"action":"cancel","id":1}
  ```
- แก้ / ลบเองได้ที่หน้ากระดิ่ง 🔔 ข้างไอคอน Wi-Fi (T141)
- งานเบื้องหลัง (P4): โมเดลข้อความ (Flash-Lite) เรียก tool ค้นข้อมูลได้ ≤ 6 รอบ / 5 นาที, พร้อมกันได้ 2 งาน, เก็บ 6 งานล่าสุด;
  ใช้ได้เฉพาะ tool ค้นข้อมูล (ไม่จด note / ไม่ตั้ง alert); ผลไม่ขึ้นการ์ด — ส่งให้ AI พูดสรุป (ปลุกบอร์ดถ้าหลับ)

---

## 3. รายการสิ่งที่ต้องทำ

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `add_task` | เพิ่มงาน | **title**, priority (1 สูง / 2 ปกติ / 3 ต่ำ), due |
| `list_tasks` | ดูงาน | include_done |
| `update_task` | ทำเสร็จ / เปิดใหม่ / ลบ / เปลี่ยนชื่อ | **id**, **action** (done / undone / delete / rename), title |

- 🗣️ "เพิ่มงานซื้อนมด่วน" · "วันนี้มีงานอะไรบ้าง" · "งานซื้อนมเสร็จแล้ว"
- 💻
  ```
  tool add_task {"title":"ซื้อนม","priority":1}
  tool list_tasks {"include_done":false}
  tool update_task {"id":3,"action":"done"}
  ```

---

## 4. ตัว AI, หน้าจอ, เสียง, วิทยุ

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `get_device_status` | วันเวลา แบต Wi-Fi ความดัง การตั้งค่า จำนวนความจำ/เตือน/งาน | – |
| `configure_assistant` | เปลี่ยนโปรไฟล์ เสียง นิสัย เพศ ชื่อ AI ชื่อผู้ใช้ สีตา | profile, voice, personality, gender (female / male / neutral), agent_name, user_name, eye_color |
| `list_assistant_options` | ดูตัวเลือก | **kind** (profiles / voices / personalities / eye_colors) |
| `set_volume` | ความดังวิทยุ (เสียง AI ล็อกไว้ `set aivolume`) | level 0-100 หรือ change ±10 |
| `set_brightness` | ความสว่างจอ | level 5-100 หรือ change ±15 |
| `configure_device` | ความไวไมค์ เวลาหลับเอง หน้านาฬิกา ความสว่างตอนหรี่ | mic_sensitivity 0-7, mic_change ±1, sleep_minutes, clock_minutes, dim_brightness 1-60 |
| `rest_screen` | พักหน้าจอ (หน้านาฬิกาหรี่) หลังตอบจบ | – |
| `control_led` | ไฟ RGB | **color** (red / green / blue / white / yellow / purple / orange / off / auto) |
| `go_to_sleep` | หยุดฟังแล้วหลับหลังตอบจบ | – |
| `play_radio` | เปิดวิทยุ / เพลง MP3 จาก SD card | station (เลข / ชื่อ / แนว, ว่าง = สถานีล่าสุด), music (คำค้น / next / prev / random / `*`) |
| `stop_radio` | ปิดวิทยุ | – |
| `mute_microphone` | ปิดไมค์ (เปิดคืนได้ด้วยปุ่มไมค์บนจอเท่านั้น) | – |

- 🗣️
  - "ตอนนี้กี่โมง" · "แบตเหลือเท่าไหร่"
  - "เปลี่ยนเป็นเสียงผู้ชาย" · "เรียกฉันว่าพี่บอส" · "เปลี่ยนชื่อเป็นฟ้า" · "ตาสีม่วง" · "มีเสียงอะไรให้เลือกบ้าง"
  - "เบาเสียงหน่อย" · "เปิดเสียง 70" · "จอสว่างขึ้น" · "ไมค์ไวขึ้นอีก" · "ไม่ต้องหลับเอง"
  - "พักหน้าจอ" · "ไปนอนได้แล้ว / ฝันดี" · "ไฟสีเขียว" · "ปิดไมค์"
  - "เปิดวิทยุลูกทุ่ง" · "สถานีที่ 4" · "เปิดเพลงคาราบาว" · "เปลี่ยนเพลง" · "สุ่มเพลง" · "ปิดวิทยุ"
- 💻
  ```
  tool get_device_status {}
  tool configure_assistant {"voice":"Puck","user_name":"พี่บอส"}
  tool list_assistant_options {"kind":"voices"}
  tool set_volume {"change":-10}
  tool set_brightness {"level":60}
  tool configure_device {"sleep_minutes":0,"clock_minutes":5}
  tool rest_screen {}
  tool control_led {"color":"green"}
  tool play_radio {"station":"ลูกทุ่ง"}
  tool play_radio {"music":"random"}
  tool stop_radio {}
  ```
  ทางลัด console: `sleep` (= go_to_sleep), `radio <n>` / `radio stop`, `music next`

---

## 5. ฮาร์ดแวร์ที่ต่อเพิ่ม, รถ, มือถือ ⚙️

| Tool | มีเมื่อ | ทำอะไร | พารามิเตอร์ |
|---|---|---|---|
| `device` | มี `agent/devices.json` (wiki 15) | อ่าน / สั่งรีเลย์ dimmer เซนเซอร์ | **action** (list / read / set), name, value (on / off / 0-100) |
| `vehicle` | เฟิร์มแวร์ที่มีหน้าปัด (ทุก release) | หน้าปัดรถ OBD-II / K-Line | **action** (status / scan / explain / clear_codes / open / close / set_source), source (off / sim / touch / kline / obd_ble / obd_uart / serial / phone / replay), code |
| `navigate` | จับคู่แอปมือถือแล้ว (wiki 18) — **เฉพาะ ESP32-S3** (P4 ถอดออก T151) | Google Maps บนมือถือ | **action** (go / stop / status / where), to |

- 🗣️
  - "เปิดไฟห้องนั่งเล่น" · "อุณหภูมิห้องเท่าไหร่" · "หรี่ไฟเหลือ 30"
  - "เปิดหน้าปัด" · "รถเป็นยังไงบ้าง" · "เครื่องร้อนไหม" · "สแกนรถให้หน่อย" · "P0420 คืออะไร" · "ล้างรหัสปัญหา" · "ใช้ OBD บลูทูธ"
  - "นำทางไปเซ็นทรัลเวิลด์" · "อีกไกลไหม / ถึงกี่โมง" · "ตอนนี้อยู่ไหน" · "หยุดนำทาง"
- 💻
  ```
  tool device {"action":"set","name":"ไฟห้องนั่งเล่น","value":"on"}
  tool vehicle {"action":"open","source":"sim"}
  tool vehicle {"action":"explain","code":"P0420"}
  tool navigate {"action":"go","to":"เซ็นทรัลเวิลด์"}
  ```

---

## 6. ข้อมูลสด + การ์ดบนจอ 🖼️⏳

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `get_weather` | อากาศ PM2.5 พยากรณ์ 5 วัน · **P4 (T163):** + ทิศลม/ลมกระโชก ความกดอากาศ เมฆ UV ทัศนวิสัย พระอาทิตย์ขึ้น/ตก ข้างขึ้นข้างแรม ฝนรายชั่วโมง 12 ชม. ปริมาณฝนรายวัน (เวลาท้องถิ่นของสถานที่) | location (ไทย, ว่าง = กรุงเทพ), location_en |
| `get_oil_price` | ราคาน้ำมันวันนี้ + พรุ่งนี้ (บางจาก) | – |
| `get_market_quote` | ราคา การเปลี่ยนแปลง ผลตอบแทน RSI rating | **symbols** (1-6 ตัว คั่นด้วย `,`) |
| `convert_currency` | แปลงค่าเงินตามเรตสด | **amount**, **from**, **to** |
| `get_news` | พาดหัวข่าว Google News | topic (ว่าง = ข่าวเด่นไทย) |

- 🗣️ "เชียงใหม่ฝนตกไหม" · "ฝุ่นวันนี้เป็นไง" · "น้ำมันพรุ่งนี้ขึ้นไหม" · "ทองวันนี้เท่าไหร่" · "บิทคอยน์กับหุ้น PTT" · "100 ดอลลาร์กี่บาท" · "ข่าวเศรษฐกิจล่าสุด"
  - (P4, T163) "ฝนจะตกกี่โมง" · "แดดแรงไหม UV เท่าไร" · "พระอาทิตย์ตกกี่โมง" · "คืนนี้พระจันทร์เต็มดวงไหม" · "ลมพัดมาทางไหน" · "โตเกียวตอนนี้อากาศเป็นไง"
- P4: ดึงข้อมูลใหม่ไม่สำเร็จ → ตอบด้วยข้อมูลเดิมของคำถามเดียวกันที่อายุไม่เกิน 3 ชม. พร้อมบอกว่าเก่ากี่นาที (การ์ดขึ้น "ข้อมูลเก่า")
- 💻
  ```
  tool get_weather {"location":"เชียงใหม่","location_en":"Chiang Mai"}
  tool get_oil_price {}
  tool get_market_quote {"symbols":"OANDA:XAUUSD,BINANCE:BTCUSDT,SET:PTT"}
  tool get_market_quote {"symbols":"ทองคำ"}
  tool convert_currency {"amount":100,"from":"USD","to":"THB"}
  tool get_news {"topic":"เศรษฐกิจ"}
  ```
- สัญลักษณ์ TradingView ที่ใช้บ่อย: `SET:SET` (ดัชนีไทย), `SET:PTT`, `NASDAQ:AAPL`, `OANDA:XAUUSD` (ทอง), `BINANCE:BTCUSDT`, `FX_IDC:USDTHB`, `NYMEX:CL1!` (WTI), `ICEEUR:BRN1!` (Brent), `TVC:SILVER` — คำไทยใช้ได้: ทองคำ บิทคอยน์ ดัชนีหุ้นไทย ดอลลาร์ น้ำมันดิบ

---

## 7. เครื่องมือทั่วไป

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `calculate` | เครื่องคิดเลขแม่นยำ (%, VAT, ดอกเบี้ย, หารบิล) | **expression** |
| `convert_units` | แปลงหน่วย (ไร่/งาน/ตารางวา, บาททอง, °C/°F ...) | **value**, **from**, **to** |
| `get_world_time` | เวลาเมืองอื่น + ต่างจากไทยกี่ชม. | **city** |
| `get_location` | ตำแหน่งโดยประมาณจาก IP | – |
| `search_knowledge` | สรุปจาก Wikipedia 🖼️⏳ | **query** |
| `notes` | สมุดโน้ต (สูตรอาหาร รายการ ไอเดีย) | **action** (add / list / search / read / delete), title, content, query, id |

- 🗣️ "1500 คูณ 12 บวก VAT 7%" · "3 ไร่กี่ตารางเมตร" · "2 บาททองกี่กรัม" · "โตเกียวกี่โมงแล้ว" · "ตอนนี้ฉันอยู่จังหวัดไหน" · "ไอน์สไตน์คือใคร" · "จดสูตรต้มยำไว้" · "อ่านโน้ตสูตรต้มยำ"
- 💻
  ```
  tool calculate {"expression":"(1500*12)*1.07"}
  tool convert_units {"value":3,"from":"ไร่","to":"m2"}
  tool get_world_time {"city":"โตเกียว"}
  tool get_location {}
  tool search_knowledge {"query":"อัลเบิร์ต ไอน์สไตน์"}
  tool notes {"action":"add","title":"ต้มยำ","content":"ข่า ตะไคร้ ใบมะกรูด ..."}
  tool notes {"action":"read","query":"ต้มยำ"}
  ```

---

## 8. ระดับน้ำ ฝน พายุ (ThaiWater) ⚙️🖼️⏳

เปิด/ปิดด้วย `set water 1|0` (หรือเมนูตั้งค่า / Studio)

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `water` | ระดับน้ำแม่น้ำ (กราฟ), ฝน, พายุ | **action** (level / rain / storm), province (ว่าง = 8 จังหวัดทั่วประเทศ), place, days (rain: 1 / 3 / 5 / 7) |

- 🗣️ "แม่น้ำปิงที่เชียงใหม่ระดับเท่าไหร่" · "ฝนตกหนักที่ไหนบ้าง 3 วันนี้" · "มีพายุเข้าไหม"
- 💻
  ```
  tool water {"action":"level","province":"เชียงใหม่","place":"ปิง"}
  tool water {"action":"rain","days":3}
  tool water {"action":"storm"}
  ```

---

## 9. การเงิน (TradingView อ่านอย่างเดียว) ⚙️🖼️⏳

เปิด/ปิดด้วย `set finance 1|0` — ไม่มีการเชื่อมต่อโบรกเกอร์ / MT5

| Tool | ทำอะไร | พารามิเตอร์ |
|---|---|---|
| `market_overview` | ภาพรวม SET, S&P 500, Nasdaq, ทอง, WTI, BTC, USD/THB + Fear&Greed คริปโต | – |
| `market_analysis` | วิเคราะห์เทคนิค RSI MACD Stoch EMA ADX + rating (gauge) | **symbol**, timeframe (15m / 1h / 4h / 1d / 1w / multi), fundamentals (P/E, P/BV, ปันผล), combined **(P4, T160)** = คำตัดสินรวม |
| `market_scan` | สแกนหุ้น/คริปโต | **market** (thailand / america / crypto), **type** (gainers / losers / volume / oversold / overbought / squeeze / breakout **(P4, T160)**), limit 1-8 |
| `market_chart` | กราฟแท่งเทียน แนวโน้ม แนวรับ/ต้าน EMA RSI MACD ATR Bollinger | **symbol**, timeframe (5m / 15m / 1h / 4h / 1d / 1w), compare (correlation ≤ 4 ตัว), view (chart / smc / signals / patterns **(P4)**) |
| `market_sentiment` | อารมณ์ตลาด / เศรษฐกิจ | **topic** (stocks / crypto / economy), symbol |
| `market_signals` **(P4, T160)** | สมุดบันทึกสัญญาณกลยุทธ์ + เฝ้ากราฟ (พูดเตือนเมื่อเกิดสัญญาณซื้อ/ขายใหม่) | **action** (stats / watch / unwatch / watches / export / import), symbol, timeframe (ค่าปกติ 1h), config (import) |
| `market_anticipation` **(T187, T188, T192)** | คาดการณ์ล่วงหน้าแบบเรียลไทม์ (T192: ราคาสดจากสตรีม TradingView, M1, เหตุการณ์ระหว่างแท่ง, ตัวตัดสินโมเดล Live — `config set judge=live\|text`; console `stream`): เฝ้ากราฟ M1–H4 ด้วย 62 ปัจจัย (ตรวจทุก 1 นาที) → ปลุก AI ตัดสินเองว่าควรพูดเตือนไหม (พร้อม SL + TP) → ติดตามผล + แดชบอร์ดสรุปสถิติ & แยกรายการแจ้ง/ข้ามผ่านปุ่ม 🔔 บนจอ | **action** (create / remove / list / scan / analyze / factors / config / learning / summary), symbol (สูงสุด 3 สินทรัพย์; remove: `all`), timeframe (5m / 15m / 1h / 4h, ค่าปกติ 15m), period (summary: today / 24h / 7d / 30d / all), delivery (ai / direct), factors (config: `A,B` = เปิดเฉพาะ; `+A,-B`), set (config: `hourly_budget=12,daily_budget=200,cooldown_bars=3,check_minutes=1`) |
| `economic_calendar` | ปฏิทินข่าวเศรษฐกิจ (เวลาไทย) | range (today / tomorrow / week), impact (high / medium) |
| `position_size` | คำนวณขนาด lot/หุ้นจากความเสี่ยง | **balance**, risk_percent (ค่าปกติ 1), **entry**, **stop_loss**, symbol, contract_size |
| `price_alert` | แจ้งเตือนราคา / indicator (เช็คทุก 2 นาที แล้วพูดเตือน) | **action** (add / list / remove), symbol, above, below, id (-1 = ทั้งหมด), condition, timeframe |
| `day_trade` **(P4, T151)** | เทรดรายวันจากแท่งเทียน TradingView: เบรกกรอบเปิดตลาด + แผนเข้า/ตัดขาดทุน/เป้า/จำนวน, ทดสอบย้อนหลัง + จำลอง, รูปแบบกราฟรายวัน — ไม่ส่งคำสั่งซื้อขาย | action (**orb** / backtest / setups), symbol, symbols (setups ≤ 8), or_minutes (15), stop (or_mid / or_range / pct:X / atr:K), target_r (2), side (long / short / both), account, risk_percent (1) |

**`market_chart` view**
- `chart` (ค่าปกติ, 1d) — กราฟแท่งเทียน + indicator
- `smc` (ค่าปกติ 15m) — order blocks, FVG, liquidity, BOS/CHoCH, sweeps (indicator SMC ของผู้ใช้)
- `signals` — โหวต 5 กลยุทธ์ (MOM, TR, REV, DC, 52H) กรอบเดียว หรือ 15m/1h/4h/1d (P4: บันทึกลงสมุดสัญญาณด้วย)
- `patterns` **(P4, T160, ค่าปกติ 4h)** — harmonic (Gartley / Bat / Butterfly / Crab ครบรูป หรือกำลังก่อตัว + โซน PRZ),
  Elliott (ช่วงคลื่น 1-5 / ABC), คะแนนรวม deep analysis (squeeze, โมเมนตัม, แรงซื้อขาย, ตำแหน่ง Fibonacci, ADX) −100…100
- P4 แสดงการ์ดแท่งเทียนแบบโฮโลแกรม (60 แท่ง + EMA + แนวรับ/ต้าน; view smc วาดโซน OB/FVG ด้วย)

**`market_scan` type ใหม่ (P4, T160)**
- `squeeze` — Bollinger แคบสุด (60 ตัวที่วอลุ่มสูงสุด เรียงตามความกว้าง BB %)
- `breakout` — วอลุ่มเทียบค่าเฉลี่ย 10 วัน > 2 เท่า + ขึ้น > 3% + ปิดเหนือ SMA20

**`market_analysis` combined (P4, T160)** — รวมคะแนน: rating เทคนิค 40% + หลายกรอบเวลา 25% + อารมณ์ตลาด 15%
(คริปโต = Fear&Greed, อื่นๆ = VIX) + โทนข่าว 7 วัน (Google News) 20% → ซื้อ / ขาย / รอ

**`market_signals`** — ไฟล์ใน SD `agent/`: `signals.jsonl` (สัญญาณที่บันทึก), `signal_watch.json` (เฝ้า ≤ 6 กราฟ),
`signal_config.json` (sl_atr, tp_atr, min_consensus, check_minutes); `stats` = ผลจริงของสัญญาณ (ชน TP/SL ก่อน, แท่งที่แตะทั้งสองถือว่าโดน SL),
`export` = `signals.csv`

**`market_sentiment` topic**
- `stocks` — ดัชนีกลัว/โลภหุ้น (S&P 500, VIX, put/call, พันธบัตร) คำนวณบนบอร์ด
- `crypto` — Fear&Greed, market cap, Binance long/short (symbol = เหรียญ, ค่าปกติ BTC)
- `economy` — เงินเฟ้อ ดอกเบี้ย การจ้างงาน GDP สหรัฐ/ไทย; symbol = ดูประวัติชุดเดียว เช่น `us_inflation`, `fed_rate`, `th_rate`, `FRED:UNRATE`

**`price_alert` condition** — `price` (ค่าปกติ), `rsi` (above/below = ระดับ RSI), `macd_up` / `macd_down`, `ema_up` / `ema_down` (EMA20 ตัด EMA50), `choch_up` / `choch_down` (SMC); อ่านจากแท่งที่ปิดแล้วของ timeframe (ค่าปกติ 1h)

- 🗣️
  - "ตลาดวันนี้เป็นยังไง"
  - "วิเคราะห์ทองกราฟ 4 ชั่วโมง" · "PTT ทุกกรอบเวลา" · "P/E ของ ADVANC เท่าไหร่"
  - "หุ้นไทยขึ้นแรงสุดวันนี้" · "คริปโตตัวไหน oversold"
  - "ขอกราฟบิทคอยน์รายวัน" · "ทองกับดอลลาร์ไปทางเดียวกันไหม" · "ดู SMC ทอง 15 นาที" · "สัญญาณกลยุทธ์ของบิทคอยน์"
  - "ตลาดหุ้นตอนนี้กลัวหรือโลภ" · "long short บิทคอยน์" · "ภาพรวมเศรษฐกิจ" · "เงินเฟ้อสหรัฐย้อนหลัง"
  - "สัปดาห์นี้มีข่าวแรงอะไรบ้าง" · "คืนนี้มีข่าวอะไร"
  - "พอร์ต 1,000 ดอลลาร์ เสี่ยง 2% ซื้อทอง 2650 SL 2640 ได้กี่ lot"
  - "เตือนเมื่อทองเกิน 2700" · "เตือนเมื่อ RSI บิทคอยน์ต่ำกว่า 30 กราฟ 4 ชั่วโมง" · "เตือนเมื่อ EMA ทองตัดขึ้น" · "มีแจ้งเตือนอะไรบ้าง" · "ลบแจ้งเตือนทั้งหมด"
  - (P4) "AAPL วันนี้ทะลุกรอบเปิดตลาดไหม ทุนหนึ่งหมื่นดอลลาร์ซื้อได้กี่หุ้น" · "ทดสอบย้อนหลังกลยุทธ์เบรกกรอบของ NVDA" · "PTT AOT DELTA มีรูปแบบกราฟน่าสนใจไหม"
  - (P4, T160) "บิทคอยน์มี harmonic หรือคลื่น Elliott ไหม" · "คริปโตตัวไหน Bollinger บีบตัว" · "หุ้นอเมริกาตัวไหนวอลุ่มทะลุ"
    · "สรุปรวมทุกมุม PTT ควรซื้อไหม" · "เฝ้ากราฟบิทคอยน์ 1 ชั่วโมงให้หน่อย" · "สัญญาณที่ผ่านมาแม่นแค่ไหน"
- 💻
  ```
  tool market_overview {}
  tool market_analysis {"symbol":"OANDA:XAUUSD","timeframe":"4h"}
  tool market_analysis {"symbol":"SET:PTT","timeframe":"multi","fundamentals":true}
  tool market_scan {"market":"thailand","type":"gainers","limit":6}
  tool market_chart {"symbol":"BINANCE:BTCUSDT","timeframe":"1d"}
  tool market_chart {"symbol":"OANDA:XAUUSD","compare":"FX_IDC:USDTHB,TVC:DXY"}
  tool market_chart {"symbol":"OANDA:XAUUSD","view":"smc","timeframe":"15m"}
  tool market_chart {"symbol":"BINANCE:BTCUSDT","view":"signals"}
  tool market_sentiment {"topic":"stocks"}
  tool market_sentiment {"topic":"crypto","symbol":"ETH"}
  tool market_sentiment {"topic":"economy"}
  tool market_sentiment {"topic":"economy","symbol":"us_inflation"}
  tool economic_calendar {"range":"week","impact":"high"}
  tool position_size {"balance":1000,"risk_percent":2,"entry":2650,"stop_loss":2640,"symbol":"XAUUSD"}
  tool price_alert {"action":"add","symbol":"OANDA:XAUUSD","above":2700}
  tool price_alert {"action":"add","symbol":"BINANCE:BTCUSDT","condition":"rsi","below":30,"timeframe":"4h"}
  tool price_alert {"action":"add","symbol":"OANDA:XAUUSD","condition":"ema_up","timeframe":"1h"}
  tool price_alert {"action":"list"}
  tool price_alert {"action":"remove","id":-1}
  tool day_trade {"symbol":"AAPL","account":10000}
  tool day_trade {"symbol":"SET:PTT","stop":"atr:1.5","target_r":1.5,"account":100000}
  tool day_trade {"action":"backtest","symbol":"BTC"}
  tool day_trade {"action":"setups","symbols":"PTT,AOT,CPALL,DELTA,KBANK,NVDA,TSLA,BTC"}
  tool market_chart {"symbol":"BTCUSDT","view":"patterns","timeframe":"4h"}
  tool market_scan {"market":"crypto","type":"squeeze"}
  tool market_scan {"market":"america","type":"breakout"}
  tool market_analysis {"symbol":"SET:PTT","combined":true}
  tool market_signals {"action":"watch","symbol":"BTCUSDT","timeframe":"1h"}
  tool market_signals {"action":"watches"}
  tool market_signals {"action":"stats"}
  tool market_signals {"action":"export"}
  tool market_signals {"action":"import","config":"{\"sl_atr\":1.2,\"tp_atr\":2.4,\"check_minutes\":10}"}
  tool market_signals {"action":"unwatch","symbol":"BTCUSDT","timeframe":"1h"}
  tool market_anticipation {"action":"create","symbol":"XAUUSD","timeframe":"15m"}
  tool market_anticipation {"action":"create","symbol":"BTCUSDT","timeframe":"5m","delivery":"direct"}
  tool market_anticipation {"action":"scan","symbol":"BTC"}
  tool market_anticipation {"action":"analyze","symbol":"ทอง"}
  tool market_anticipation {"action":"list"}
  tool market_anticipation {"action":"factors"}
  tool market_anticipation {"action":"config","factors":"-RSI_50_CROSS,-SESSION_OPEN","set":"hourly_budget=6,check_minutes=5"}
  tool market_anticipation {"action":"summary","period":"7d"}
  tool market_anticipation {"action":"learning"}
  tool market_anticipation {"action":"remove","symbol":"all"}
  ```
- `market_anticipation` (T187): คำพูดที่ใช้ — "ตั้งคาดการณ์ล่วงหน้าทองให้หน่อย", "เตือนก่อนเกิดสัญญาณ BTC", "สแกนทองตอนนี้", "คาดการณ์ล่วงหน้าแจ้งไปกี่ครั้งแล้ว ชน TP หรือ SL",
  "ทำไมไม่แจ้ง", "ปัจจัยปลุกมีอะไรบ้าง", "ปิดปัจจัย RSI_50_CROSS", "เลิกเฝ้าทอง" · log เบื้องหลังขึ้นต้น `[Antic]` (เหตุการณ์ที่ปลุก, เวลาที่ AI ตอบ, NOTIFY/SKIP + เหตุผล, ผล TP/SL)
- P4: ชื่อคริปโตเปล่าๆ ที่ลงท้าย USDT / USDC / FDUSD (เช่น `BTCUSDT`) จะลอง `BINANCE:` แล้ว `BYBIT:` ให้เอง (T160)

---

## 10. คำสั่ง console อื่นๆ ที่ใช้บ่อย

| คำสั่ง | ทำอะไร |
|---|---|
| `help` | รายการคำสั่งทั้งหมด |
| `ask <ข้อความ>` | พิมพ์คุยกับ AI |
| `tool <ชื่อ> [json]` | เรียก tool ตรงๆ (ไม่ผ่าน AI) |
| `status` / `diag` / `config` / `tools` | สถานะ, วินิจฉัย, การตั้งค่า, รายการ tool ที่ประกาศ (JSON) |
| `set <field> <ค่า>` | ตั้งค่า เช่น `set model 0`, `set finance 1`, `set water 1`, `set wake word`, `set wakeword alexa` (คำปลุก 1 จาก 9 คำ สลับทันที, T145), `set wakethreshold 0.6`, `set language th`, `set aivolume 80`, `set search 1` |
| `sleep` / `powersave` / `wake` | ให้หลับ / พักหน้าจอ / ปลุก |
| `mute` / `new` / `retry` | สลับไมค์ / เริ่ม session ใหม่ / ต่อใหม่ |
| `radio [<n>\|<url>\|stop]` · `music [list\|next\|prev\|pause\|resume\|<คำ>]` | วิทยุ / เพลง SD |
| `ui open <menu\|wifi\|apikey\|...>` · `ui tap x y` · `ui type <text>` · `ui back` · `ui home` | สั่งหน้าจอจากระยะไกล |
| `screenshot` | จับภาพหน้าจอ (Studio แสดงให้) |
| `memclear <facts\|history>` | ล้างความจำ / ประวัติสนทนา |
| `wifi` / `wifi forget <ssid>` | ดู / ลืม Wi-Fi ที่จำไว้ |
| `wifi off` / `wifi on` **(P4, T166)** | ตัด Wi-Fi ไว้ทดสอบตอนออฟไลน์ (ไม่ต่อเครือข่ายที่จำไว้อื่นด้วย) / ต่อใหม่ — ไม่บันทึก รีสตาร์ต = ต่อปกติ |
| `sdls [path]` · `sdget <path>` · `sddel <path>` | ไฟล์ใน SD card |

ดูรายละเอียดแต่ละกลุ่ม: [[09-Assistant-Profiles-and-Tools]] (ตัว AI + โมเดล), [[11-Data-Tools-and-Cards]] (ข้อมูลสด + การเงิน), [[12-Internet-Radio]], [[15-Expansion-Ports]], [[16-Vehicle-Dash]], [[18-Phone-Link-Navigation-Plan]]
