---
tags: [tools, weather, oil, tradingview, finance, news, graphics, cards, personalaibot]
created: 2026-10-15
updated: 2026-10-15
status: implemented
---

# 📊 Live-Data Tools + การ์ดกราฟิกบนจอ

> **Related:** [[09-Assistant-Profiles-and-Tools]] | [[06-Avatar-and-Thai-UI]] | [[10-PC-Studio-App]] | [[07-Troubleshooting-Log]]
> **Code:** `src/agent/WebTools.*` (task + weather/oil/quote/FX/news) · `src/agent/WebToolsExtra.cpp` (tools ที่พอร์ตจาก PersonalAIBot) · `src/agent/WebToolsCommon.h` (HTTPS, TradingView batch) · `src/util/Calc.h` · `src/util/Units.h` · `src/ui/CardScreen.*` (วาดการ์ด) · `src/agent/AgentTools.cpp` (declarations)

AI เรียก tool → บอร์ดดึงข้อมูลจริง (HTTPS) หรือคำนวณเอง → **แสดงการ์ดกราฟิกบนจอ** ~30 วิ (แตะเพื่อปิด) → AI **พูดสรุป** ให้ฟัง
ใช้แทน Google Search ได้ในเรื่องเหล่านี้ (โควตา Search ของ key ปัจจุบันหมด — [[07-Troubleshooting-Log#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]])

## ความถูกต้องของวันที่ / เวลาในผลลัพธ์ (ตรวจ 2026-10-15 กับ API จริง — T046)

| ปัญหาที่พบ | แก้ |
|---|---|
| **น้ำมัน:** `PriceYesterday` ของบางจากคือ **ราคาก่อนปรับครั้งล่าสุด** (ปรับ 9 ต.ค. — วันที่ 15 ต.ค. ยังเป็นค่าเดิม) แต่ tool ส่งเป็น `vs_yesterday` + หัวการ์ด "วันนี้" → AI พูดว่า "ขึ้นจากเมื่อวาน 75 สตางค์" | อ่านวันที่มีผลจาก `OilRemark2` ("ราคามีผล ณ วันที่ 9 ต.ค. 69 เวลา 05.00 น.", สำรอง `OilPriceDate` + 1 วัน) → `price_effective_since`, `days_since_last_change`, `change_at_last_adjustment` + note บอก AI ว่าไม่ใช่เมื่อวาน · การ์ด "มีผล 9 ต.ค. 05.00 น. • ส่วนต่าง=ตอนปรับ" |
| **PM2.5:** `us_aqi` = มลพิษที่แย่ที่สุด (เช่นโอโซนจากแบบจำลอง: 201 ขณะ PM2.5 = 25 µg/m³) | ใช้ `us_aqi_pm2_5` (= 81) ส่งเป็น `us_aqi_pm25` |
| **ราคา TradingView:** หุ้นไทยนอกเวลา/วันหยุดเป็นราคาปิด แต่ AI ไม่รู้ | `current_session` → `market_open`, `last_bar_update_time` → `price_time`, `update_mode` → `delayed_min` (SET หน่วง 15 นาที) · `market_overview` ใส่ `market_open` ต่อรายการ |
| **ข่าว:** ไม่มีวันที่ — ข่าวเก่าจากการค้นตามหัวข้อถูกเล่าเหมือนข่าววันนี้ | อ่าน `<pubDate>` → `published` (เวลาไทย) + `age_hours` |
| **ปฏิทินเศรษฐกิจ:** นาฬิกายังไม่ซิงก์ = "วันนี้" เป็นปี 1970; มีแค่ชื่อวัน; ไม่มีค่า actual | ไม่ตอบจนกว่าจะซิงก์, เพิ่ม `date`, note ห้ามเดาผลของข่าวที่ประกาศแล้ว |
| prompt มีวันที่ตอนเริ่ม session เท่านั้น (หรือไม่มีเลยถ้าเริ่มก่อน SNTP) | ทุก tool ข้อมูลสดใส่ **`as_of`** = วันเวลาไทยตอนดึงข้อมูล (ยกเว้น calculate / convert_units / position_size / notes) |

ตัวแปลงวันที่อยู่ใน `src/util/DataDates.h` (host test `data_dates`)

## จำนวน tool ทั้งหมด

| ชุด | จำนวน | เปิดเมื่อ |
|---|---|---|
| core (ความจำ, เวลา, งาน, ผู้ช่วย, เครื่อง, วิทยุ) | 25 | เสมอ |
| live data (อากาศ, น้ำมัน, ราคา, แลกเงิน, ข่าว) | 5 | เสมอ |
| utilities ที่พอร์ตจาก PersonalAIBot | 6 | เสมอ |
| **water** (ThaiWater, T100) | 1 | `set water 1` (ค่าเริ่มต้น เปิด) |
| **finance pack** | 6 | `set finance 1` (ค่าเริ่มต้น เปิด) |
| `about_me` (T096) | 1 | เสมอ |

ค่าเริ่มต้น = **44 tools** (~15.6 KB JSON ต่อ setup — งบใน host test 15.7 KB: `about_me` +200 B, `water` +470 B; tool ใหม่ต้องย่อคำอธิบายเก่าหรือทำเป็นชุดเปิดปิดได้; core 25 รวม `configure_device`, `rest_screen`, `mute_microphone`, `play_radio`, `stop_radio`) + Google Search (ถ้าใช้ได้) — ทดสอบใน host test `tool_declarations_per_model`

## 1) Live data (ชุดแรก)

| Tool | ข้อมูล | แหล่ง (ไม่ต้องใช้ key) | การ์ด |
|---|---|---|---|
| `get_weather(location?, location_en?)` | อากาศตอนนี้, **PM2.5** + `us_aqi_pm25` (AQI ของ PM2.5 เท่านั้น — v3.4), พยากรณ์ 5 วัน | Open-Meteo forecast / air-quality / geocoding | ไอคอนวาดเอง + 5 วัน · **ไม่ระบุที่ = ตำแหน่งจาก IP** |
| `get_oil_price()` | น้ำมันขายปลีก 8 ชนิด: `price_now`, **`price_effective_since` + `days_since_last_change`**, `change_at_last_adjustment`, `tomorrow` (ถ้าประกาศแล้ว) | `oil-price.bangchak.co.th/ApiOilPrice2/th` | ตาราง + ▲▼ |
| `get_market_quote(symbols)` | ราคา, ช่วงวันนี้, 1W/1M/3M/1Y, RSI, สัญญาณ + **`market_open` / `price_time` / `delayed_min`** | `scanner.tradingview.com/symbol` | 1 ตัว = กราฟ · 2–6 ตัว = ตาราง |
| `convert_currency(amount, from, to)` | แปลงเงิน | TradingView `FX_IDC:` | ยอดเงิน + อัตรา |
| `get_news(topic?)` | หัวข้อข่าว 8 ข่าว + **`published` / `age_hours`** · มี topic = ค้น **7 วันล่าสุด** (`when:7d`, ไม่เจอค่อยค้นทุกช่วง) เอา 8 ข่าวที่เกี่ยวข้องสุดแล้วเรียงใหม่สุดก่อน · ใช้แทนการค้นเว็บเมื่อปิด Google Search | Google News RSS (ไทย) | หัวข้อละ 2 บรรทัด |

![[images/card-weather.png]] ![[images/card-oil.png]]
![[images/card-gold.png]] ![[images/card-multi.png]]
![[images/card-fx.png]] ![[images/card-news.png]]

## 2) Tools ที่พอร์ตจาก PersonalAIBot (`composeApp`)

| Tool | ทำอะไร | แหล่ง / วิธี | เวลา |
|---|---|---|---|
| `calculate(expression)` | คิดเลขแม่น (บวกลบคูณหาร, `^`, `%`, sqrt/sin/cos(องศา)/log, `x` = คูณ) — สั่ง AI ห้ามคิดเลขเอง | `util/Calc.h` บนบอร์ด | 0 วิ |
| `convert_units(value, from, to)` | ความยาว, พื้นที่ (**ไร่/งาน/ตารางวา**), น้ำหนัก (**บาททอง** 15.244 g), ปริมาตร (ถัง), อุณหภูมิ, ความเร็ว | `util/Units.h` | 0 วิ |
| `get_world_time(city)` | เวลาท้องถิ่น ~40 เมือง/ประเทศ (ชื่อไทย/อังกฤษ) **รองรับ DST** EU/US/AU/NZ + ต่างจากไทยกี่ ชม. | ตารางในตัว + นาฬิกา NTP | 0 วิ |
| `get_location()` | ตำแหน่งโดยประมาณจาก IP (ไม่มี GPS) — ใช้เป็นค่าเริ่มต้นของ `get_weather` | `ipwho.is` (แคช 1 ชม.) | ~2.6 วิ |
| `search_knowledge(query)` | สรุปสารานุกรม (ไทยก่อน แล้วอังกฤษ) | Wikipedia `action=query&generator=search&prop=extracts` | ~8 วิ |
| `notes(action, …)` | สมุดโน้ตบนบอร์ด: add / list / search / read / delete (สูงสุด 100) · **delete ทีละโน้ต**: `id` หรือ query ที่ตรงโน้ตเดียว (ตรงหลายอัน = error + `candidates`) · ไฟล์อ่านไม่ได้ = error ไม่เขียนทับ (T073) | `<agentDir>/notes.json` (SD หรือ LittleFS) | 0 วิ |

### Finance pack

| Tool | ทำอะไร | แหล่ง |
|---|---|---|
| `market_overview()` | SET, S&P 500, Nasdaq 100, ทองคำ, WTI, BTC, USD/THB + ดัชนีกลัว/โลภคริปโต | TradingView `POST /global/scan` (1 request ทุกตลาด) + `api.alternative.me/fng` |
| `market_analysis(symbol, timeframe?, fundamentals?)` | RSI, MACD, Stoch, EMA20/50/200, ADX, BB, สัญญาณรวม (MA/oscillators) — `timeframe`: 15m/1h/4h/1d/1w หรือ **`multi`** (สัญญาณทุกกรอบเวลา) · `fundamentals`: P/E, P/BV, ปันผล, มูลค่าตลาด, EPS, sector | TradingView `/symbol` field suffix `|15`, `|60`, `|240`, `|1W` |
| `market_scan(market, type, limit?)` | หุ้นไทย / หุ้นสหรัฐ / คริปโต (Binance USDT): ขึ้นแรง, ลงแรง, วอลุ่มพุ่ง, RSI ขายมากเกิน/ซื้อมากเกิน | TradingView `POST /{thailand|america|crypto}/scan` + filter (ตัด `.R`/`.F` ซ้ำ) |
| `economic_calendar(range?, impact?)` | ข่าวเศรษฐกิจสำคัญ today/tomorrow/week **เวลาไทย** (`date` + `when_th`) + forecast/previous — การ์ดแสดงเฉพาะที่ยังไม่ถึง; ไม่มีค่า actual; นาฬิกายังไม่ซิงก์ = ไม่ตอบ | `nfs.faireconomy.media/ff_calendar_thisweek.json` (แคช 30 นาที เพราะจำกัดจำนวนครั้ง) |
| `position_size(balance, entry, stop_loss, risk_percent?, symbol?, contract_size?)` | ขนาด lot ตามความเสี่ยง (XAU 100 oz, XAG 5000, FX 100k, หุ้น/คริปโต 1) | คำนวณบนบอร์ด |
| `price_alert(action, symbol?, above?, below?, id?)` | ตั้งเตือนราคา (สูงสุด 10) — task webTools ตรวจ **ทุก 2 นาที** (batch เดียว) แล้ว AI **พูดแจ้งเตือน** + เสียงกริ่ง + ข้อความบนหน้าตา (ปลุกจากโหมดพักด้วย) | `<agentDir>/alerts.json` + `/global/scan` |

ทุก tool การเงินใส่ note ให้ AI บอกว่า "ไม่ใช่คำแนะนำการลงทุน"

![[images/card-overview.png]] ![[images/card-analysis.png]]
![[images/card-analysis-multi.png]] ![[images/card-scan.png]]
![[images/card-calendar.png]] ![[images/card-alert.png]]
![[images/card-calc.png]] ![[images/card-worldtime.png]]
![[images/card-position.png]] ![[images/card-wiki.png]]

### น้ำ: ระดับน้ำ / ฝน / พายุ จาก ThaiWater (T100)

`water(action, province, place?, days?)` — ข้อมูล**วัดจริง**จากคลังข้อมูลน้ำแห่งชาติ (พยากรณ์ยังใช้ `get_weather`) · ปิดได้ `set water 0`

| action | ทำอะไร | API (`api-v3.thaiwater.net/api/v1/thaiwater30/`) | ขนาด | การ์ด |
|---|---|---|---|---|
| `level` | ระดับน้ำสถานีโทรมาตรในจังหวัด เรียง % ความจุลำน้ำมาก→น้อย (8 แห่ง) · ระดับ: >100 ล้นตลิ่ง, >70 มาก, >30 ปกติ, >10 น้อย, ≤10 น้อยวิกฤติ (เกณฑ์ของ ThaiWater) | `public/waterlevel?province_code=` | ~80 KB | แท่ง % (เส้นประ = ตลิ่ง) สีตามระดับ |
| `level` + `place` ตรงสถานีเดียว (ทั้งชื่อหรือรหัส) | + กราฟ 2 วันของสถานีนั้น (ตลิ่ง / เตือนภัย / วิกฤต เป็นเส้นประ) + `change_last_hours_m` (ขึ้น/ลง) | `public/waterlevel_graph?station_type=tele_waterlevel&station_id=&start_date=&end_date=` | ~15 KB/วัน | กราฟเส้น 60 จุด |
| `rain` | ฝน 24 ชม. (`days` 3 / 5 / 7 = ฝนสะสม 3 / 5 / 7 วัน, T106) ทุกสถานีในจังหวัด: เฉลี่ย, สูงสุด, ตกกี่สถานี + 8 สถานีที่ตกมากสุด + `period_from`/`period_to` | `public/rain_24h` / `provinces/rain3d` / `rain5d` / `rain7d` `?province_code=` | ~350 KB | แท่ง มม. สีตามระดับฝน (กรมอุตุฯ) |
| `storm` | พายุที่เข้าใกล้ประเทศไทย | `public/storm_data` | ~1 KB | ข้อความ |
| `rain` **ไม่ใส่ province** | ฝน 24 ชม. (หรือ 3 / 5 / 7 วัน) **สูงสุดรายจังหวัด 8 จังหวัด**: สถานีที่ตกมากสุดของแต่ละจังหวัด + จำนวนสถานีที่ฝนตก | `public/rain_24h` ทั้งประเทศ | 614 KB gzip / 4.8 MB JSON | แท่ง มม. ต่อจังหวัด |
| `level` **ไม่ใส่ province** | **สถานการณ์น้ำสูงสุดรายจังหวัด 8 จังหวัด**: สถานีที่ % ความจุลำน้ำสูงสุดของแต่ละจังหวัด + จำนวนสถานีล้นตลิ่ง / น้ำมาก | `public/waterlevel` ทั้งประเทศ | 224 KB gzip / 1.3 MB JSON | แท่ง % ต่อจังหวัด (เส้นประ = ตลิ่ง) |

- **ไม่มี key** — เป็น API ที่หน้าเว็บ thaiwater.net เรียกเอง (ไม่ใช่ API ที่ประกาศเป็นทางการ; [standard.thaiwater.net/docs](https://standard.thaiwater.net/docs/) คือมาตรฐานแลกเปลี่ยนข้อมูลระหว่างหน่วยงาน) · TLS: DigiCert Global Root G2 (มีใน `WEB_ROOT_CA_PEM` แล้ว)
- **gzip + อ่านแบบสตรีม** (`agent/GzipHttpStream.h`): ขอ `Accept-Encoding: gzip` → แกะ chunked + inflate ด้วย `tinfl` ใน ROM (หน้าต่าง 32 KB ใน PSRAM)
  → ArduinoJson อ่านจาก reader ตรงๆ (ไม่ผ่าน `Stream::readBytes` ที่เสีย virtual call + อ่านนาฬิกาทุกไบต์) — ข้อมูลรายจังหวัดเล็กลง ~8 เท่า (ระดับน้ำ 13.5 KB → 2.4 KB)
- **ทั่วประเทศ parse ทีละสถานี** (`thaiwater::ProvinceRanker`) ลงเอกสารเล็กที่ใช้ซ้ำ: ทั้งก้อน 1.3 MB ทำให้การกันสตริงซ้ำของ ArduinoJson 7 ช้าแบบกำลังสอง
  (21 วิ → ระดับน้ำทั้งประเทศ 4–6 วิ) · ฝนทั้งประเทศ ~8–21 วิ (parse ~5 วิ ที่เหลือคือเน็ต: Wi-Fi มือถือ 38–70 KB/s) · แคช 10 นาที
- **ใส่ `province` เมื่อถามถึงจังหวัด** (หรือชื่อจังหวัดใน `place` — ต้องเป็นทั้งคำ: "ตากใบ" ไม่ใช่ จ.ตาก, T101) · ไม่ใส่ = อันดับ 8 จังหวัด (`place` ยังกรองได้ เช่น `ปิง` = เฉพาะลุ่มน้ำปิง) · `ThaiProvinces` มีรหัสจังหวัด (`code`, TIS 1099) แล้ว
  · `place`: แม่น้ำ (`ปิง` → ตรงกับ "ลุ่มน้ำปิง"), อำเภอ, ชื่อหรือรหัสสถานี (`P.73`) — ตัดคำนำ "แม่น้ำ/อ./สถานี/จ." ให้เอง
- อ่านแบบกรองระหว่าง parse (ArduinoJson `Filter`, PSRAM): ฝน 350 KB เหลือ ~10 KB · แคช 10 นาที (`cacheTtlMs`) · timeout 20 วิ (ทั้งประเทศ 40)
- **ดาวน์โหลดค้าง** (T104): ไม่มีไบต์ 6 วิ / ไม่ได้ header ใน 8 วิ = ต่อใหม่ สูงสุด 2 ครั้ง — บนบอร์ดเจอค้างกลางทางแบบสุ่ม ~1 ใน 3 (hotspot มือถือ)
- โค้ด: `util/ThaiWater.h` (parse, host test `thaiwater_parsing` ด้วยข้อมูลจริงที่ตัดมา) · `agent/WaterTools.cpp` (ดึง + การ์ด) · การ์ดใหม่ `CardData::Graph` / `Bars`
![[images/card-water-level.png]] ![[images/card-water-graph.png]] ![[images/card-water-rain.png]]
![[images/card-water-rank-rain.png]] ![[images/card-water-rank-level.png]] ![[images/card-water-rank-rain5.png]]

- **ฝนหลายวัน** (T106): ช่วงวันที่มาจาก ThaiWater เอง (`rainfall_start_date`–`rainfall_end_date`, วันที่ 26 ต.ค.: 3 วัน = 23–25, 5 วัน = 21–25, 7 วัน = 20–25)
  แสดงบนการ์ด ("21/10–25/10") และส่งให้ AI · ตรวจกับ PC (คำนวณเองจาก API เดียวกัน) แล้วตรงทุกอันดับ: 7 วัน ปัตตานี 324.5, 5 วัน สตูล 273.2, 3 วัน ตรัง 268.0
- ชื่อสถานี / อำเภอเก็บ 128 / 96 ไบต์ (`kStationCap` / `kPlaceCap`) — 64 ไบต์ตัดชื่อกลางคำ (T106)

- **ยังไม่ทำ (เฟส 2–3):** เขื่อนใหญ่ (`analyst/dam` ~1 MB กรองไม่ได้ → ต้องแคช), น้ำขึ้นน้ำลง (public มีแค่ระดับวัดจริง 2 สถานีปากแม่น้ำ;
  ระดับคาดการณ์อยู่ที่ `api.hii.or.th` ซึ่งต้องใช้ key — ต้องขอสิทธิ์จาก สสน.), ความเค็ม/คุณภาพน้ำ (3.6 MB กรองไม่ได้), ประตูระบายน้ำ

### ไม่มีการเชื่อมต่อ MT5 / โบรกเกอร์ (ถอดออกใน v3.4)

- ข้อมูลการเงินทั้งหมด**อ่านอย่างเดียว**จาก TradingView (`scanner.tradingview.com`) — ผู้ช่วยดูพอร์ตหรือส่งคำสั่งซื้อขายไม่ได้
- `mt5_account` / `mt5_trade`, ค่า `mt5url` / `mt5token` / `mt5trade` / `mt5maxlot` และ `MT5_URL` / `MT5_TOKEN` ใน `ai-setup.txt` ถูกลบทั้งหมด
- บอร์ดที่อัปเดตจากเวอร์ชันเก่า: boot แรกลบคีย์ `mt5url`, `mt5tok`, `mt5tok_e`, `mt5trade`, `mt5lot` ออกจาก NVS และเขียน `agent/secrets.json` ใหม่โดยไม่มี `mt5tok_e` (`Settings::kLegacyKeys`)

### สิ่งที่ **พอร์ตไม่ได้ / ไม่จำเป็น** จาก PersonalAIBot

| ในแอปมือถือ | เหตุผล |
|---|---|
| ควบคุมมือถือ (`device_*`: โทร, SMS, ไฟฉาย, แอป, ตั้งค่าเครื่อง) | เป็นความสามารถของ Android |
| กล้อง / วิเคราะห์ภาพ / ไฟล์ในเครื่อง | บอร์ดไม่มีกล้อง; ไฟล์ใช้ `notes` แทน |
| agent ซ้อน (`agent_task`, `system_*_agent_tool`) | Live API มี session เดียว + RAM จำกัด |
| backtest / strategy library / SMC / Harmonic / Elliott / แดชบอร์ดกราฟ | ต้องใช้แท่งเทียนย้อนหลังจำนวนมาก (websocket TradingView) และ CPU/RAM เกินบอร์ด — ใช้ `market_analysis` (สัญญาณสำเร็จรูปของ TradingView) แทน |
| summarize / translate | โมเดล Gemini ทำได้เองโดยไม่ต้องมี tool |

## สถาปัตยกรรม

```
Gemini toolCall ──► AgentController::onToolCall
                      ├─ tool ทั่วไป  → ตอบทันที (toolResponse)
                      └─ web tool     → WebTools::submit(id, name, args, sessionGen)   (agent ไม่ถูกบล็อก)
task webTools (core 1, prio 2 → 4 ระหว่างทำงานให้โมเดล/console, 12 KB): HTTPS (WiFiClientSecure + WEB_ROOT_CA_PEM) / คำนวณ → CardData
   ├─ gCard.show(card)  → task UI push การ์ดทับหน้าตา (เฉพาะหน้าหลัก), ปิดเองหลัง 30 วิ / แตะ
   ├─ Result → AgentController::handleWebResults → toolResponse ด้วย id เดิม (ทิ้งถ้า session เปลี่ยนแล้ว)
   └─ ว่างอยู่ทุก 2 นาที: checkPriceAlerts() → Result(tag = kAlertTag) → announcement "[แจ้งเตือนราคาจากระบบ] …"
```

- การ์ดชนิด: `Weather`, `Oil`, `Quotes`, `News`, `Text` (ตัวเลขใหญ่ + ย่อหน้า), `Table` (ชื่อ + ค่าสี), `Analysis` (เกจ -1..+1 + ตาราง 8 แถว)
- TLS ตรวจใบรับรองทุกโฮสต์: `WEB_ROOT_CA_PEM` = Google GTS R1/R3/R4 + GlobalSign ECC R4 + ISRG Root X1 + DigiCert Global Root G2 + Amazon Root CA 1 — ครอบคลุม Open-Meteo, บางจาก, TradingView, Wikipedia, ipwho.is, alternative.me, faireconomy (สร้างด้วย `python tools/gen_root_ca.py`); `http://` ถูกปฏิเสธเสมอ (`httpRequest` คืน -3) — ทุกแหล่งข้อมูลเป็น HTTPS
- console / PC app: `tool <name> {json}` → `{"status":"fetching..."}` แล้วตามด้วยบรรทัด `TOOL <name> {...}` + การ์ดบนจอ
- **แคชผลลัพธ์ (T069):** tool + arguments เดิมภายในเวลาที่ข้อมูลยังสด ตอบจากที่จำไว้พร้อมการ์ดเดิม (4 ช่อง, PSRAM, ไม่จำ error, `as_of` = เวลาที่ดึงจริง):
  `get_weather` 10 นาที · `get_oil_price` / `economic_calendar` 30 นาที · `get_news` 5 นาที · `search_knowledge` 1 ชม. · `get_market_quote` / `convert_currency` / `market_overview` 1 นาที — log `[Tool] … answered from the last N s`
- **ความเร็ว (T072):** ทุก request พิมพ์ `[Web] <host> -> <code> in N ms (dns, tls, reply, body; bytes)` —
  งานที่โมเดล/console รอ รันที่ **priority 4** (สูงกว่า UI/คำปลุกที่ 3, ต่ำกว่า agent/geminiIO ที่ 5; เช็กราคาเบื้องหลังยัง 2) ·
  ชื่อ**จังหวัด 77 จังหวัด** (ไทย/อังกฤษ/"จ.…"/"Chonburi"/"กทม"/"โคราช") ใช้พิกัดในตัว `agent/ThaiProvinces.*` ไม่ต้อง geocode ·
  TLS handshake จำกัด 10 วิ (`kTlsHandshakeSec`, เดิม framework 120 วิ) · `http.begin()` ก่อน connect (หลัง connect มันตัดทิ้งแล้วจับมือใหม่)
- คิว webTools เต็ม → ตอบโมเดลว่า `busy … try again` (เดิม `unknown function` ทำให้โมเดลเลิกใช้ tool นั้น) · `price_alert remove` ต้องมี `id` (`-1` = ลบทั้งหมด; เดิมไม่ใส่ = ลบหมด)

## เวลาที่ใช้ (วัดบนบอร์ด 2026-10-15)

| tool | เวลา |
|---|---|
| calculate / convert_units / get_world_time / position_size / notes | ~0 วิ |
| `market_analysis` / `market_scan` / `get_market_quote` 1 ตัว | ~1.1–1.3 วิ |
| `price_alert add` | ~1.3 วิ |
| `get_oil_price` / `get_location` / `economic_calendar` | ~2–3 วิ |
| `market_overview` (+ Fear&Greed) | ~3.5 วิ |
| `search_knowledge` | ~8 วิ |
| `get_weather` | **~2.8 วิ** ชื่อจังหวัด (forecast + PM2.5) · ~4.3 วิ อำเภอ/สถานที่อื่น (+ geocode) · ~4.2 วิ ไม่ระบุที่ (+ ipwho.is) — วัด 2026-10-18 หลัง T072 (เดิม 8–12 วิ, ถึง 23 วิ) |

ทำไมยังเกือบ 3 วิ: HTTPS แต่ละครั้งต้องจับมือ TLS ใหม่ (คนละโฮสต์: `api.` / `air-quality-api.` / `geocoding-api.open-meteo.com`)
เซิร์ฟเวอร์อยู่เยอรมนี RTT ~200 ms → TCP + TLS 1.2 = 3 RTT + คณิต RSA/ECDHE ≈ 1.15 วิ, คำตอบ 1 RTT ≈ 0.2 วิ (จาก PC ยังใช้ ~1 วิต่อ request)

## ทดสอบจริงกับ AI (2026-10-15, `ask` ผ่าน console)

- "ตลาดหุ้นวันนี้เป็นยังไงบ้าง" → `market_overview` → "ดัชนี SET บวกเล็กน้อย… S&P 500 กับ Nasdaq ปิดลบ… ทองคำขึ้นนิดหน่อย"
- "วิเคราะห์ทองคำทุกกรอบเวลา" → `market_analysis {"symbol":"gold","timeframe":"multi"}` → สรุประยะสั้น/ยาว
- "สัปดาห์นี้มีข่าวเศรษฐกิจอะไรสำคัญ" → `economic_calendar {"range":"week"}`
- "เจ็ดเปอร์เซ็นต์ของ 2350" → `calculate {"expression":"2350 * 7%"}`
- แจ้งเตือนราคา: ตั้ง BTC below 999999 → 20 วิต่อมา `[Alert] ราคาบิทคอยน์ ลงถึง …` แล้วรายการถูกลบเอง
