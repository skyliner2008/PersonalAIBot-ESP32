---
tags: [tools, weather, oil, tradingview, finance, news, graphics, cards, personalaibot]
created: 2026-10-15
updated: 2026-10-15
status: implemented
---

> **ESP32-P4 (T149):** การ์ดบนจอแนวตั้ง 480×800 วาดใหม่ (ฟอนต์ใหญ่ anti-alias, แอนิเมชันตอนเข้า) — ดู [[19-ESP32-P4-JC4880P443C]] หัวข้อ T149 · การ์ดการเงินยังเป็นแบบเดิม

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
| **finance pack** | 6 (+ `market_chart`, `market_sentiment`, **`market_anticipation` T187**; P4 มี `market_signals`, `day_trade` เพิ่ม) | `set finance 1` (ค่าเริ่มต้น เปิด) |
| `about_me` (T096) | 1 | เสมอ |

ค่าเริ่มต้น = **44 tools** (~15.6 KB JSON ต่อ setup — งบใน host test 15.7 KB: `about_me` +200 B, `water` +470 B; `play_radio` `music` +100 B (T122) → งบ 15.8 KB; tool ใหม่ต้องย่อคำอธิบายเก่าหรือทำเป็นชุดเปิดปิดได้; core 25 รวม `configure_device`, `rest_screen`, `mute_microphone`, `play_radio`, `stop_radio`) + Google Search (ถ้าใช้ได้) — ทดสอบใน host test `tool_declarations_per_model`

## 1) Live data (ชุดแรก)

| Tool | ข้อมูล | แหล่ง (ไม่ต้องใช้ key) | การ์ด |
|---|---|---|---|
| `get_weather(location?, location_en?)` | อากาศตอนนี้, **PM2.5** + `us_aqi_pm25` (AQI ของ PM2.5 เท่านั้น — v3.4), พยากรณ์ 5 วัน | Open-Meteo forecast / air-quality / geocoding | ไอคอนวาดเอง + 5 วัน · **ไม่ระบุที่ = ตำแหน่งจาก IP** |
| `get_oil_price()` | น้ำมันขายปลีก 8 ชนิด: `price_now`, **`price_effective_since` + `days_since_last_change`**, `change_at_last_adjustment`, `tomorrow` (ถ้าประกาศแล้ว) | `oil-price.bangchak.co.th/ApiOilPrice2/th` | ตาราง + ▲▼ |
| `get_market_quote(symbols)` | ราคา, ช่วงวันนี้, 1W/1M/3M/1Y, RSI, สัญญาณ + **`market_open` / `price_time` / `delayed_min`** · ทองคำ: + `thai_gold_estimate.bar_baht_per_baht_weight` (ทองคำแท่ง 96.5% บาทละ โดยประมาณ = ทองโลก × USDTHB × 15.244 × 0.965 / 31.1035, T110) | `scanner.tradingview.com/symbol` | 1 ตัว = กราฟ · 2–6 ตัว = ตาราง |
| `convert_currency(amount, from, to)` | แปลงเงิน | TradingView `FX_IDC:` | ยอดเงิน + อัตรา |
| `get_news(topic?)` | หัวข้อข่าว 8 ข่าว + **`published` / `age_hours`** · มี topic = ค้น **7 วันล่าสุด** (`when:7d`, ไม่เจอค่อยค้นทุกช่วง) เอา 8 ข่าวที่เกี่ยวข้องสุดแล้วเรียงใหม่สุดก่อน · ใช้แทนการค้นเว็บเมื่อปิด Google Search · ได้แค่หัวข่าว: ขอรายละเอียดข่าวไหน AI เรียก `get_news` อีกครั้งด้วย topic ของข่าวนั้น (หัวข่าวหลายสำนัก) ห้ามแต่งเพิ่ม (T111) | Google News RSS (ไทย) | หัวข้อละ 2 บรรทัด |

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
| `market_chart(symbol, timeframe?, compare?)` **(T142)** | **กราฟแท่งเทียน 60 แท่งบนจอ** + EMA20 (ฟ้า) / EMA50 (ส้ม) + เส้นประแนวรับ (เขียว) / แนวต้าน (แดง) · คำนวณจาก**แท่งเทียนย้อนหลังจริง 300 แท่ง** บนบอร์ด (`util/MarketMath.h`): แนวโน้ม (ราคาเทียบ EMA20/50/200 + ความชันของ EMA50), RSI14, MACD (+ ตัดกันกี่แท่งก่อน), ATR14 (%), Bollinger (ความกว้าง, ตำแหน่ง, **บีบแคบสุดในรอบ 120 แท่ง**), แนวรับ/แนวต้าน 2 ระดับจาก swing high/low ที่รวมกันในระยะ ½ ATR (+ จำนวนครั้งที่แตะ), แท่งล่าสุด (กลืนกิน/โดจิ/ค้อน/ดาวตก), วอลุ่มเทียบเฉลี่ย 20 · `timeframe` 5m/15m/1h/4h/1d/1w · **`compare`** ("eth, gold, usdthb" สูงสุด 4) = ตาราง correlation ของผลตอบแทน 100 แท่ง (รายวันจับคู่ตามวันเวลาไทย เพราะแต่ละตลาดเปิดแท่งคนละเวลา) | **TradingView chart websocket** `wss://data.tradingview.com/socket.io/websocket` (ไม่ต้องล็อกอิน เหมือน `TvHistoryBridge` ของแอปมือถือ; หุ้นไทยช้า 15 นาที) — **ทุกสัญลักษณ์ใน connection เดียว** (1 chart session ต่อสัญลักษณ์ รวมตัวเลือก SET/NASDAQ/NYSE ของชื่อย่อ) · สำรอง: Yahoo Finance `v8/finance/chart` (4 ชม. = รวม 1 ชม. × 4) |
| `market_scan(market, type, limit?)` | หุ้นไทย / หุ้นสหรัฐ / คริปโต (Binance USDT): ขึ้นแรง, ลงแรง, วอลุ่มพุ่ง, RSI ขายมากเกิน/ซื้อมากเกิน | TradingView `POST /{thailand|america|crypto}/scan` + filter (ตัด `.R`/`.F` ซ้ำ) |
| `economic_calendar(range?, impact?)` | ข่าวเศรษฐกิจสำคัญ today/tomorrow/week **เวลาไทย** (`date` + `when_th`) + forecast/previous — การ์ดแสดงเฉพาะที่ยังไม่ถึง; ไม่มีค่า actual; นาฬิกายังไม่ซิงก์ = ไม่ตอบ | `nfs.faireconomy.media/ff_calendar_thisweek.json` (แคช 30 นาที เพราะจำกัดจำนวนครั้ง) |
| `position_size(balance, entry, stop_loss, risk_percent?, symbol?, contract_size?)` | ขนาด lot ตามความเสี่ยง (XAU 100 oz, XAG 5000, FX 100k, หุ้น/คริปโต 1) | คำนวณบนบอร์ด |
| `market_chart(..., view: "smc")` **(T143)** | **SMC — indicator ของผู้ใช้เอง "SMC & Multi-TF Order Blocks Sweeps V8.3 / V11.29"** (พอร์ตจาก `automation/smc` ของแอปมือถือ → `util/SmcMath.h`, ผลตรงกับ Kotlin ทุกตัวเลขบนแท่งจริง 5 ชุด): โครงสร้าง BOS (SMS/BMS) / CHoCH + inducement, premium/discount, order block (ต้องมี FVG ยืนยัน, กรองตามทิศโครงสร้าง) 3 อันใกล้ราคาต่อฝั่ง, FVG, liquidity (equal highs/lows + swing, 0-5 ดาว), sweep / attack force ของแท่งล่าสุด, confluence ต่อฝั่ง /10 · **ค่าเริ่มต้น 15m** (main TF ของ indicator) · การ์ด = แท่งเทียน + กล่อง OB (ทึบ) / FVG (ขอบจุด) เขียว = demand แดง = supply + เส้นประ structure high/low | แท่งเทียนชุดเดียวกับ `market_chart` |
| `market_chart(..., view: "signals")` **(T143)** | **5 กลยุทธ์โหวต** (แบบ `StrategySignalProvider` ของแอป): โมเมนตัม 20 แท่ง, ตามเทรนด์ EMA50/200, กลับตัว RSI 30/70 ที่ Bollinger, เบรก Donchian 20, ใกล้จุดสูงสุดของช่วง (≥ 98 % / ≤ 90 %) · นับเฉพาะเมื่อ**แท่งล่าสุดยืนยัน** (ตัวแท่ง/ATR, ตำแหน่งปิด, ไส้) · น้ำหนักตาม**สภาพตลาด** (ADX14 + ความกว้าง Bollinger: เทรนด์ / ไซด์เวย์ / ผันผวน) → ซื้อแรง…ขายแรง · ไม่ใส่ timeframe = **15m + 1h + 4h + 1d** ในตารางเดียว (+ สรุปว่าหลายกรอบเวลาตรงกันไหม) | เหมือนกัน (หลายกรอบเวลาใน connection เดียว) |
| `market_sentiment(topic, symbol?)` **(T143)** | `stocks` = **ดัชนีกลัว/โลภหุ้นสหรัฐที่บอร์ดคำนวณเอง** จาก 5 องค์ประกอบแบบ CNN (S&P 500 เทียบเฉลี่ย 125 วัน, VIX เทียบ 50 วัน, put/call 5 วัน, หุ้นเทียบพันธบัตร 20 วัน, HYG/IEF 20 วัน — แต่ละตัว = เปอร์เซ็นไทล์ใน 1 ปี) การ์ดแถบ 0-100 · `crypto` = Fear & Greed (+ เมื่อวาน / 7 วันก่อน), มูลค่าตลาดรวม + dominance, **Binance futures long/short** (รายย่อย, รายใหญ่, taker) + การอ่านแบบแอป (squeeze / ไปทางเดียวกัน) · `economy` = เงินเฟ้อ (+ พื้นฐาน) ดอกเบี้ย Fed ว่างงาน NFP GDP ยีลด์ 10 ปี **สหรัฐ + เงินเฟ้อ ดอกเบี้ย GDP ไทย** · `symbol` = ย้อนหลัง 12 ค่าของชุดเดียว (`us_inflation`, `fed_rate`, `th_rate`, `FRED:UNRATE`, `ECONOMICS:USCCI` …) เป็นกราฟเส้น | TradingView websocket (`SP:SPX`, `TVC:VIX`, `USI:PCC`, `NASDAQ:TLT`, `AMEX:HYG`, `NASDAQ:IEF`, `ECONOMICS:*`, `FRED:*`) · `api.alternative.me/fng` · `api.coingecko.com/api/v3/global` · `fapi.binance.com/futures/data/*` (3 คำขอ TLS เดียว keep-alive) |
| `price_alert(action, symbol?, above?, below?, id?)` | ตั้งเตือนราคา (สูงสุด 10) — task webTools ตรวจ **ทุก 2 นาที** (batch เดียว) แล้ว AI **พูดแจ้งเตือน** + เสียงกริ่ง + ข้อความบนหน้าตา (ปลุกจากโหมดพักด้วย) | `<agentDir>/alerts.json` + `/global/scan` |
| `price_alert(..., condition, timeframe?)` **(T143)** | **แจ้งเตือนตาม indicator** บนแท่งที่ปิดแล้วของ timeframe (ค่าเริ่มต้น 1h): `rsi` (above/below = ระดับ RSI), `macd_up/down` (MACD ตัดเส้นสัญญาณ), `ema_up/down` (EMA20 ตัด EMA50), `choch_up/down` (SMC change of character) · เตือน**เมื่อเปลี่ยนเข้าเงื่อนไข** (เก็บ `state` ใน alerts.json) — ตั้งตอนเงื่อนไขเป็นจริงอยู่แล้ว = รอครั้งถัดไป (ผลบอก `already_true`) · ตรวจทุก 2 นาทีพร้อมแจ้งเตือนราคา: แท่งเทียนของทุกรายการใน connection เดียว · จอกระดิ่งแสดงเงื่อนไข แก้ระดับ RSI ได้ (เส้นตัดกันแก้ไม่ได้ ลบได้) | `util/IndicatorAlert.h` + TradingView websocket |

| `day_trade(action?, symbol?, symbols?, or_minutes?, stop?, target_r?, side?, account?, risk_percent?)` **(ESP32-P4, T151)** | แนวคิดจาก [day-trader-skill](https://github.com/Jakeschincariol/day-trader-skill) (MIT) เขียนใหม่: **`orb`** เบรกกรอบเปิดตลาด (กรอบ 15 นาทีแรก, แท่ง 5 นาทีแรกที่ปิดเหนือ/ใต้กรอบ) + แผน เข้า / ตัดขาดทุน (กึ่งกลางกรอบ, ขอบอีกด้าน, %, ATR) / เป้า (R) / จำนวน (ทุน × % เสี่ยง ปัดตาม lot) / ออกก่อนปิด · **`backtest`** กฎเดียวกันทุกวันย้อนหลัง + จำลอง 2,000 ครั้งจากแท่งของวันก่อนๆ ตามช่วงเวลาของวัน → คำตัดสินเทียบจุดคุ้มทุน · **`setups`** รูปแบบรายวัน 1-8 ตัว (ทะลุจุดสูง 20 วัน, ธงขาขึ้น, กลับเหนือเส้น 20 วัน, RSI < 30, เด้งจาก RSI < 30) · ไม่ส่งคำสั่งซื้อขาย · คณิตใน `firmware-p4/src/util/DayTradeMath.h` (host tested) | **TradingView chart websocket** (แท่ง 5 นาที 2,500 แท่ง / รายวัน 90 แท่ง, `agent/MarketHistory.cpp` ของ P4, User-Agent `ESP32-AI/<version>`, ไม่มี Yahoo สำรอง) |

| `market_anticipation(action, symbol?, timeframe?, period?, delivery?, factors?, set?)` **(S3 + P4, T187–T189)** | **คาดการณ์ล่วงหน้า / ระบบปลุก AI** (พอร์ตจาก `trading_signal_anticipation` ของแอปมือถือ): เครื่องเฝ้ากราฟ M5 / M15 / H1 / H4 ด้วย **60 ปัจจัย 14 หมวด** ทุก 1 นาที (ซิงค์วินาที `:03` ตรงกันทั้ง S3, P4 และมือถือ) · เหตุการณ์ใหม่ **ปลุกโมเดลข้อความ (temperature 0.0)** ให้ดูภาพ 4 ไทม์เฟรมแล้วตอบแบบฟอร์ม 7 บรรทัด (NOTIFY/SKIP · BUY/SELL · SL · TP · ความมั่นใจ · เหตุผล · สรุป) → ระบบตรวจซ้ำ (SL ≥ 1 ATR, RR ≥ 1:1, ห้ามสวน H4/H1/M15) → **พูดเตือนเฉพาะ NOTIFY** · ติดตามว่าชน TP หรือ SL พร้อมแดชบอร์ดสถิติสรุป (วันนี้/7 วัน/30 วัน/ทั้งหมด), แยกรายการแจ้ง/ข้าม และปุ่ม 🔔 ดูย้อนหลังทั้งหมดบนจอ · ดูหัวข้อ "คาดการณ์ล่วงหน้า" ด้านล่าง | แท่งเทียน TradingView (websocket เดียวต่อรอบ) + `generativelanguage.googleapis.com` (โมเดลข้อความ) · `<agentDir>/antic*.json` |

ทุก tool การเงินใส่ note ให้ AI ปิดท้ายด้วยข้อเสนอที่ช่วยต่อได้ 1 อย่าง (ตั้งแจ้งเตือนราคา วิเคราะห์เพิ่ม เทียบตัวอื่น ข่าวที่กระทบ) —
**ไม่ลงท้ายด้วย "ไม่ใช่คำแนะนำการลงทุน"** แล้ว (T139: ผู้ใช้ไม่ต้องการ; เดิมทุกคำตอบลงท้ายแบบนี้)

![[images/card-overview.png]] ![[images/card-analysis.png]]
![[images/card-analysis-multi.png]] ![[images/card-scan.png]]
![[images/card-calendar.png]] ![[images/card-alert.png]]
![[images/card-calc.png]] ![[images/card-worldtime.png]]
![[images/card-position.png]] ![[images/card-wiki.png]]

### คาดการณ์ล่วงหน้า — `market_anticipation` (T187, S3 + P4)

พอร์ตจาก tool `trading_signal_anticipation` ของแอปมือถือ (`tools/mobile_app/.../automation/wake/`) ให้ทำงานในตัวบอร์ด
หลักการเดียวกับแอป: **ปัจจัยเป็นแค่ "นาฬิกาปลุก" ไม่ใช่ข้อสรุป** — เมื่อเกิดเหตุการณ์ AI ถูกปลุกมาดูภาพรวมแล้วตัดสินเองว่าควรแจ้งไหม
ปัจจัยไม่คำนวณ Entry / SL / TP ให้ (กัน anchor AI)

#### เรียลไทม์ (T192) — เฝ้าแบบคนนั่งดูกราฟ

| ส่วน | ทำอะไร | ที่ |
|---|---|---|
| **สตรีมราคาสด** | websocket ของ TradingView **เปิดค้างเส้นเดียว** (task `tvstream`): ซีรีส์ M1 ต่อสินทรัพย์ที่เฝ้า (300 แท่ง + แท่งที่กำลังก่อตัว) — เซิร์ฟเวอร์ส่ง `du` ราว 1 ครั้ง/วินาที, `timescale_update` ทันทีที่แท่งปิด, heartbeat `~h~` ทุก 10 วิ (ต้อง echo) · หลุดแล้วต่อใหม่ 5 วิ → 10 → … → 5 นาที · ตลาดปิด / ไม่มีงานเฝ้า = ปิดการเชื่อมต่อ | `agent/TvStream.*` |
| **รอบตรวจ** | รัน**วินาทีที่แท่ง M1 ปิด** (ไม่ใช่ tick นาที) หรือทันทีที่สตรีมเห็นเหตุการณ์สด · TF อื่นอ่านเฉพาะส่วนต่าง**ผ่านการเชื่อมต่อเดิม** (~0.3–1 วิ ไม่ต้อง TLS ใหม่) · สตรีมล่ม = กลับไป tick นาที + แคช T191 (ไม่มี M1) | `MarketAnticipation.cpp` (`loadCached`, `streamRead`) |
| **M1** | ไทม์เฟรมที่ 5 (index 0; ไฟล์เก่าถูกเลื่อนเลข TF ให้ตอนโหลด — `"v": 2`) · ปลุกได้ตั้งแต่ต้นเฉพาะปัจจัย "จังหวะ" (แตะ Order Block, กวาดสภาพคล่อง, กวาด High/Low ช่วงก่อน, แท่งผิดปกติ) ตาม `WakeTfProfile` ของแอป · ปัจจัยอื่นบน M1 เก็บสถิติอย่างเดียวจนกว่าจะพิสูจน์ว่าดี · สภาวะ (State) บน M1 ไม่เก็บ (กะพริบรายนาที) | `util/WakeFactors.h` |
| **เหตุการณ์สด** (ระหว่างแท่ง) | สตรีมเฝ้า**ระดับราคา** (สูงสุด 10: แนวรับ/ต้าน + Order Block ของ M15/H1/H4 + ขอบกรอบ 20 แท่ง ที่ห่างราคา ≥ ¼ ATR) → `LIVE_LEVEL` · ราคาวิ่ง ≥ 1 ATR ของ M5 ใน 60 วิ → `LIVE_FAST_MOVE` (เงียบ 3 นาที) · เป็นสิ่งที่ "เกิดแล้ว" ไม่หายตอนแท่งปิด (ไม่ repaint) · ป้าย `[สด]` ใน prompt | `util/LiveWatch.h` |
| **ภาพแท่งที่ยังไม่ปิด** | ส่งให้ AI เป็นบริบท (รวมจากแท่ง M1): "H1 แท่งปัจจุบัน (ผ่านไป 40 จาก 60 นาที): เปิด/สูง/ต่ำ/ตอนนี้" — **ไม่ใช้เป็นตัวปลุก** อินดิเคเตอร์ยังคิดจากแท่งปิดเท่านั้น | `liveLines` |
| **TP / SL ของสัญญาณที่แจ้ง** | สตรีมเฝ้าไว้ ถึงเมื่อไร**บันทึกผล + พูดบอกทันที** ("ผลคาดการณ์ … ราคาถึง TP แล้ว (+2.2R)") | `resolveHit` |
| **ตัวตัดสิน = โมเดล Live** | `gemini-3.8-live` ทาง websocket ต่อการปลุก 1 ครั้ง (ไม่เก็บบริบท) · โมเดล Live **ไม่รับ output แบบ TEXT** ("The requested combination of response modalities (TEXT) is not supported") → รับคำตอบเป็น **function call `submit_view`** (decision / bias / level_sl / level_tp / confidence / reason_th / summary_th) แล้วแปลงเป็น 7 บรรทัดให้ parser เดิม · **ไม่มีงบจำนวนครั้ง** เว้น 20 วิ/การปลุก + กันโทเคน 50,000/นาที · ล้ม 3 ครั้งติด = โมเดลข้อความ + งบเดิม 10 นาที · `config set judge=live\|text` | `askLive` |
| **ไม่แจ้งซ้ำ** | NOTIFY ทิศเดียวกับคาดการณ์ที่ยังเปิดอยู่ และ SL/TP ห่างกัน ≤ ¼ ATR = SKIP ("ระบบไม่แจ้งซ้ำ") | `wake::enforceNoRepeat` |
| **ที่เก็บ** | คำตอบ 240 ครั้ง (เดิม 80 = แค่ 3 ชม. ที่อัตราใหม่) — เต็มแล้วทิ้ง SKIP ที่จบแล้วก่อน **ไม่ทิ้งคาดการณ์ที่แจ้งแล้วยังเปิดอยู่** · เหตุการณ์รอวัดผล 200 (เต็มแล้วทิ้งของ TF ที่มากสุด — M1 ไม่ดัน H1 ออก) | `evictIndex`, `wake::addPending` |

console: `stream` → `STREAM {...}` (connected / connects / drops / ราคา / อายุราคา / จำนวนแนวที่เฝ้า)

**ขั้นตอนต่อรอบ (ทุก `check_minutes`, ค่าปกติ 1 นาที เฉพาะ tool นี้ — task `webTools` ตอนว่าง ส่วน `price_alert` ยังทุก 2 นาที):**

1. ตลาดปิด (FX / ทอง: ศุกร์ 17:00 ถึงอาทิตย์ 17:00 เวลานิวยอร์ก คิด DST ให้) → **ไม่ดึงแท่งเทียนเลย** · หุ้น: ไม่มีแท่ง M5 ใหม่เกิน 45 นาที = ปิด · คริปโตเปิดตลอด
2. แท่งเทียน M5 / M15 / H1 / H4 อย่างละ 300 แท่ง ของทุกสินทรัพย์ที่เฝ้า (สูงสุด 3) + ตลาดเกี่ยวข้อง H1 60 แท่ง (ทอง / FX → `TVC:DXY`, เหรียญอื่น → BTC)
   — **T191: เก็บแคชไว้ใน PSRAM (`util/CandleCache.h`) ดึงเฉพาะแท่งที่ปิดใหม่ แบบแอป** · แต่ละซีรีส์ขอเมื่อแท่งล่าสุดของมันปิดแล้วเท่านั้น
   (M5 ทุก 5 นาที, M15 ทุก 15, H1 / DXY ทุกชั่วโมง, H4 ทุก 4 ชม.) และขอแค่ "แท่งที่ยังก่อตัวตอนอ่าน + ที่ปิดตั้งแต่นั้น + แท่งใหม่ + 2" ·
   ทุกซีรีส์ที่ถึงรอบรวมใน **websocket เดียว** · นาทีที่ไม่มีแท่งไหนปิด = **ไม่ต่อ TradingView เลย** (เดิมอ่านชุดเต็ม ~145 KB ทุกนาที) ·
   ครั้งแรก / เน็ตหลุดนาน / ตลาดเปิดหลังเสาร์-อาทิตย์ = อ่านชุดเต็ม · อ่านพลาด = ใช้ของเดิม แล้วลองนาทีถัดไป (ส่วนต่างครอบคลุมแท่งที่พลาด) ·
   ได้คำตอบแต่ไม่มีแท่งใหม่ (ตลาดเงียบ / วันหยุด) = เว้นหนึ่งแท่ง (≤ 5 นาที) · แท่งที่ยังก่อตัวตอนอ่านแล้วปิดไปโดยยังไม่ได้อ่านใหม่ **ไม่ถูกนับเป็นแท่งปิด**
   (ไม่ตัดสินจากแท่งครึ่งเดียว) · เหตุการณ์ที่ติดงบจะปลุกพร้อมการอ่าน M5 ครั้งถัดไป (ราคาที่ AI ใช้เป็นราคาปัจจุบัน) ·
   ไม่มีผลต่อการวิเคราะห์: ทุกปัจจัยใช้แท่งที่ปิดแล้ว ข้อมูลของไทม์เฟรมหนึ่งเปลี่ยนเฉพาะตอนแท่งของมันปิด (เปิด session ใช้หน้าต่าง 10 นาที)
3. สแกนปัจจัยบน**แท่งที่ปิดแล้ว**ของทุกไทม์เฟรม (`util/WakeFactors.h`) — **เหตุการณ์ (E)** ปลุกได้ · **สภาวะ (S)** เป็นบริบท
4. Governor (`util/WakeLogic.h`): ปัจจัยเดิม × TF เดิม × ทิศเดิม เว้น `cooldown_bars` (3) แท่งของ TF นั้น · แต่ละปัจจัยปลุกได้เฉพาะ TF ที่เหมาะ
   (ตารางเดียวกับ `WakeTfProfile` ของแอป ตัด M1) + TF หลักของงานปลุกได้เสมอ · เว้น 1 นาที/การปลุก · งบ `hourly_budget` (12 / ชม. / สินทรัพย์)
   และ `daily_budget` (200 / วัน) · เหตุการณ์ที่ติดงบ**พกไปแสดงในการปลุกครั้งถัดไป** (≤ 10 นาที) · 60 นาทีก่อนตลาดปิดสุดสัปดาห์ไม่ปลุก
5. ปลุก: สร้าง prompt (เหตุการณ์ + สถิติของปัจจัยนั้นบน TF นั้น, สภาวะ, มุมมองเดิม 24 ชม. พร้อมผล, ภาพโครงสร้าง 4 TF, บรรทัดสรุปทิศ, กติกา)
   → `generateContent` ของโมเดลข้อความที่ตั้งไว้ (อีกตัวเป็นสำรอง) → แยกคำตอบ 7 บรรทัด
6. ระบบตรวจซ้ำ (ลำดับเดียวกับแอป): ตัดระดับที่ผิดฝั่ง / ห่างเกิน 20 % → SL ชิดกว่า 1 ATR ของ TF หลัก **ขยายเป็น 1 ATR** แล้วคิด RR ใหม่ (ไม่ถึง 1:1 → SKIP)
   → RR < 1:1 หรือไม่มีระดับ → SKIP → NOTIFY สวนเทรนด์ที่ H4/H1/M15 เรียงกัน → SKIP (คงทิศไว้วัดผล)
7. บันทึกทุกคำตอบ (`antic_views.json`, 80 ครั้งล่าสุด) · **NOTIFY → พูดเตือน** + **การ์ดสัญญาณเด้งขึ้นจอ (T190)** + กริ่ง (ปลุกจากโหมดพัก) · SKIP = เงียบ ·
   **การ์ดสัญญาณ (T190, `ui/SignalCard.*` ทั้ง S3 และ P4):** แสดงแค่สิ่งที่ต้องใช้ตัดสินใจ — ออเดอร์ BUY / SELL, ความมั่นใจ (ตัวเลข + แถบ),
   ราคาเข้า + RR, TP / SL (ราคา + % จากราคาเข้า) และสรุปสั้นๆ ของ AI (ตัดหมายเหตุของระบบออก ไม่แสดงปัจจัย / เงื่อนไขละเอียด) ·
   เด้งขึ้นทับหน้าจอใดก็ได้ยกเว้นคีย์บอร์ด · **ไม่แตะ = ปิดเองใน 1 นาที** (แถบนับถอยหลังล่างจอ) · **แตะการ์ด = ค้างไว้** (ขึ้น "ค้างไว้")
   จนกว่าจะกด **✕** มุมขวาบน (หรือปุ่ม BOOT) · หลายสินทรัพย์ NOTIFY ในรอบเดียว = การ์ดแสดงตัวที่มั่นใจที่สุด (เสียงพูดครบทุกตัว) ·
   การ์ดข้อมูลอื่นที่มาระหว่างนั้นรอใต้การ์ดสัญญาณ · มีการ์ดแล้วไม่ขึ้นข้อความยาวบนหน้าตาซ้ำ · **เปิดดูบนจอได้ตลอดเวลา (T193):** ปุ่มกระดิ่ง 🔔 บนหน้าหลักแสดงตลอดทั้ง S3 และ P4 (สีอำพันเมื่อมีรายการแจ้งเตือน/งานเฝ้า, สีเทาเมื่อไม่มี) แตะเข้าไปจะพบหน้า **Alerts Hub (ไอคอนใหญ่)** 4 หมวดสัมผัสสะดวก:
   1. 📊 **ประวัติคาดการณ์ล่วงหน้า** (เปิด AnticCardViewerScreen)
   2. 📡 **รายการเฝ้ากราฟล่วงหน้า** (งาน market_anticipation ที่กำลังเฝ้าอยู่)
   3. 🏷️ **แจ้งเตือนราคา & อินดิเคเตอร์** (price_alert)
   4. ⏰ **นาฬิกาปลุก / เตือนความจำ**
   และหน้า **"ประวัติคาดการณ์ล่วงหน้า" (T193)** ออกแบบใหม่เป็น **การ์ดเต็มจอปัดซ้าย-ขวา (Swipeable Carousel)** ตัวหนังสือใหญ่ สบายตา ไม่ต้องเพ่ง:
   - **Card 0 (Dashboard):** สรุปผลสถิติแบบ Hero Cards (แจ้งเตือนกี่ครั้ง, ข้ามกี่ครั้ง, ชนะ/TP กี่ครั้ง, แพ้/SL กี่ครั้ง, กำไร/ขาดทุนสะสม R, และไม้ที่กำลังติดตามผล) พร้อมปุ่มเลือกช่วงเวลาสัมผัสง่าย (วันนี้ / 7 วัน / 30 วัน / ทั้งหมด)
   - **Card 1..N (Event Cards):** การ์ดแสดงผลทีละรายการ มี Badge ชัดเจน `[ NOTIFY ]` หรือ `[ SKIP ]`, ทิศทาง BUY/SELL, ระดับราคา Entry, SL, TP, และ RR พร้อมผลลัพธ์ R, วันเวลาไทย และเหตุผลที่ AI วิเคราะห์แบบตัดคำไทยสวยงาม (Wrapped Text)
   - **Filter Tabs:** แตะเลือกดูเฉพาะ `[ ทั้งหมด ]`, `[ แจ้ง ]`, หรือ `[ ข้าม ]` ได้ทันที
   - **ปุ่มถังขยะ 🗑️:** ล้างประวัติได้สะดวกรวดเร็ว
8. เรียนรู้ (ไม่ใช้โทเคน): ทุกเหตุการณ์ที่มีทิศถูกวัด **12 แท่งข้างหน้าของ TF ที่เกิด** หน่วย ATR (`antic_learn.json`) — n ≥ 40 และเฉลี่ย ≤ 0 = **ลดชั้น**
   (เก็บสถิติต่อ ไม่ปลุก) · TF นอกชุดเริ่มต้นที่เฉลี่ย ≥ 0.2 และไปตามทิศ ≥ 55 % = **เลื่อนชั้น** · มุมมองที่มี SL/TP ถูกตามทีละแท่ง 48 แท่ง
   (ช่วงแท่งแรกใช้ M5 กันชนปลอม; แท่งเดียวโดนทั้งคู่ = SL) · SKIP วัดว่า 12 แท่งต่อมาราคาไปไกลเกิน 3 ATR ไหม

**action:** `create` (เฝ้า — เหตุการณ์ที่อยู่บนกราฟตอนตั้งไม่นับเป็นข่าว) · `remove` (`symbol: "all"` = ทั้งหมด) · `list` (งบที่ใช้ + ค่าตั้ง) ·
`scan` (ดูอย่างเดียว: ไม่กิน cooldown / งบ ไม่ถาม AI) · `analyze` (scan + มุมมอง AI ตอนนี้ — ไม่บันทึก ไม่ใช้งบ) · `factors` (รายการปัจจัยตามหมวด) ·
`config` (`factors: "A,B"` = เปิดเฉพาะ, `"+A,-B"` = เปิด/ปิด — ชื่อผิดไม่เดาให้; `set: "hourly_budget=12,daily_budget=200,cooldown_bars=3,check_minutes=1"`) ·
`learning` (ปัจจัยที่ผลดี/แย่, ลดชั้น/เลื่อนชั้น) · `summary` (ปลุกกี่ครั้ง, NOTIFY กี่ครั้ง แยก BUY/SELL + ชน TP/SL รวมกี่ R, SKIP กี่ครั้งเพราะอะไร —
`period`: today / 24h / 7d / 30d / all) · `delivery: "direct"` ตอน create = แจ้งเหตุการณ์ตรงๆ ไม่ถาม AI

**ปัจจัย 60 ตัว 14 หมวด** (id เดียวกับแอปเมื่อเป็นแนวคิดเดียวกัน):
- โครงสร้าง (5): `BOS` `CHOCH` `RANGE_BREAKOUT` `HL_LH_CONFIRMED` `RANGE_FORMED`(S)
- จุดสำคัญ (7): `OB_TOUCH` `FVG_ENTER` `FVG_FILLED` `BREAKER_RETEST` `FIB_RETRACEMENT` `KEY_LEVEL_TOUCH` `PREMIUM_DISCOUNT_EXTREME`(S)
- สภาพคล่อง (4): `LIQUIDITY_SWEEP_REJECTION` `EQ_POOL_FORMED`(S) `LIQUIDITY_VOID` `SESSION_LEVEL_SWEEP`
- เทรนด์ (8): `EMA_CROSS` `GOLDEN_DEATH_CROSS` `PRICE_CROSS_EMA200` `EMA_RIBBON_ALIGN`(S) `ADX_TREND_BIRTH` `ADX_EXHAUSTION` `ICHIMOKU_TK_CROSS` `ICHIMOKU_CLOUD_BREAK`
- โมเมนตัม (10): `RSI_EXIT_EXTREME` `RSI_50_CROSS` `RSI_DIVERGENCE` `MACD_SIGNAL_CROSS` `MACD_ZERO_CROSS` `MACD_HISTOGRAM_TURN` `MACD_DIVERGENCE` `STOCH_EXTREME_CROSS` `CCI_100_CROSS` `MOMENTUM_EXHAUSTION`
- ผันผวน (6): `VOLATILITY_SQUEEZE`(S) `SQUEEZE_FIRE` `BAND_REJECTION` `BAND_WALK`(S) `ATR_CONTRACTION`(S) `NR7_INSIDE_BAR`(S)
- volume (3): `VOLUME_SPIKE` `VOLUME_ABSORPTION` `VWAP_RECLAIM`
- แท่งเทียน (6): `ENGULFING_AT_LEVEL` `PIN_BAR` `INSIDE_BAR_BREAK` `IMPULSE_CANDLE` `STAR_PATTERN` `THREE_SOLDIERS_CROWS`
- รูปแบบ (2): `DOUBLE_TOP_BOTTOM` `HEAD_SHOULDERS`
- หลาย TF (4): `ALL_TF_ALIGNED`(S) `HTF_CONFLICT`(S) `LTF_PULLBACK_END` `MTF_RSI_CONFLUENCE`
- session (1): `SESSION_OPEN`
- engine (1): `STRATEGY_VOTE` (5 กลยุทธ์โหวต ±2)
- ตลาดเกี่ยวข้อง (2): `DXY_MOVE` `CRYPTO_BROAD_MOVE`
- ผิดปกติ (1): `PRICE_ANOMALY`

**ต่างจากแอปมือถือ (และเหตุผล):**

| จุด | แอป | บอร์ด | เหตุผล |
|---|---|---|---|
| ไทม์เฟรม | M1 / M5 / M15 / H1 / H4 × ~800 แท่ง, เก็บในฐานข้อมูลเครื่อง | M5 / M15 / H1 / H4 × 300 แท่ง ดึงใหม่ทุกรอบ | RAM + เวลาดึง (S3 ~5 วิ / P4 ~1.3 วิ ต่อรอบ) |
| ปัจจัย | 115 ตัว 15 หมวด | 60 ตัว 14 หมวด | คำนวณจากแท่งเทียนจริงทั้งหมด (รวม Ichimoku, Stoch, CCI, VWAP, Fib, EQH/EQL, Breaker, H&S, 3 Soldiers/Crows); ไม่มีเฉพาะส่วนที่ต้องใช้ฐานข้อมูลข่าว/Fear & Greed ย้อนหลังหรือ M1 |
| รอบตรวจ | ทุก 1 นาที | ทุก 1 นาที (ตั้ง 1–30) | ปรับให้ตรวจทุก 1 นาทีเฉพาะ `market_anticipation` (ส่วน `price_alert` ยังตรวจทุก 2 นาที) |
| งบ | 40 / ชม. · 3000 / วัน (สลับ API key 7 ตัว) | 12 / ชม. · 200 / วัน (ตั้งได้ถึง 60 / 1000) | บอร์ดมี key เดียว และ AI ตอบ 14–22 วิ / ครั้ง (ช่วงนั้น tool อื่นรอ) |
| จำนวนงาน | ไม่จำกัด, `timeframe: all` = 4 งาน | 3 สินทรัพย์ งานละ 1 TF หลัก (ทุกงานอ่าน 4 TF อยู่แล้ว) | 1 websocket ต่อรอบ |
| การเรียนรู้ | แยกสภาพแวดล้อม (MTF / ADX / ความผันผวน / session), ปัจจัยไม่มีทิศเทียบเส้นฐาน | สถิติรวมต่อปัจจัย × TF เฉพาะเหตุการณ์ที่มีทิศ | ที่เก็บ + ขนาดโค้ด |
| ประวัติ | NOTIFY 365 วัน / SKIP 30 วัน, action `inspect` | 80 คำตอบล่าสุด + หน้า **"ประวัติคาดการณ์"** (T188, T193): แดชบอร์ดสรุปสถิติ (เลือกช่วงเวลา วันนี้ / 7 วัน / 30 วัน / ทั้งหมด), แยกดู "รายการแจ้งเตือน" (NOTIFY) และ "รายการที่ข้าม" (SKIP) ชัดเจน ในรูปแบบการ์ดปัดซ้าย-ขวา (Carousel) ตัวหนังสือใหญ่ สบายตา | ไฟล์ JSON เล็กบน SD / LittleFS + ดูย้อนหลังบนจอทัชได้โดยตรง |
| ช่องทางแจ้ง | notification + การ์ดแชท + เสียง (`voice` ปิดได้) | เสียง AI + ข้อความบนหน้า + กริ่ง + ปุ่ม 🔔 บนหน้าจอ (Alerts Hub ไอคอนใหญ่ 4 หมวด, ใช้คำว่า **SL** และ **TP** ตรงไปตรงมา) | กดปุ่ม 🔔 ดูรายการเฝ้า, สถิติแดชบอร์ด และประวัติย้อนหลังบนจอได้ทั้ง S3 และ P4 |
| เมื่อ AI ไม่ตอบ | `WakeAiFallback` | ไม่แจ้ง (log `[Antic] no verdict`) การปลุกนั้นนับงบแล้ว | เรียบง่าย ไม่แจ้งมั่ว |

**ไฟล์:** `util/WakeFactors.h`, `util/WakeLogic.h` (pure, เหมือนกันทั้ง `src/util` และ `firmware-p4/src/util` — host test `test_anticipation.cpp` 13 ชุด),
`agent/MarketAnticipation.cpp` (ต่างกันบรรทัดเดียว: `Wanted.tf` ↔ `Wanted.resolution`) · declaration +1,130 B (งบ host test 18.1 KB)

**ทดสอบบนบอร์ดจริง 2026-11-11 (console `tool market_anticipation {...}`):** S3 `scan` BTC 6.0 วิ (ดึง 5.6 วิ), `analyze` 28 วิ (AI 22 วิ, prompt 10.3 KB) —
AI เสนอ SELL แต่ **ระบบเปลี่ยนเป็น SKIP เพราะ RR 1:0.68** · P4 `analyze` ทอง 16 วิ (ดึง 1.3 + DXY 0.8, AI 14 วิ) → SKIP "H4 ขาลงแต่ H1/M15 ขาขึ้น"

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
- **ดาวน์โหลดค้าง & กันค้างถาวรในตัวคลาย gzip** (T104, **T168**): ไม่มีไบต์ 6 วิ / ไม่ได้ header ใน 8 วิ / สตรีมขาดกลางคัน = ต่อใหม่ สูงสุด 2 ครั้ง — บนบอร์ดเจอค้างกลางทางแบบสุ่ม ~1 ใน 3 (hotspot มือถือ) · **T168 แก้บั๊กวิกฤตบอร์ดค้างถาวร:** เดิมเมื่อสตรีมขาดกลางบล็อก Huffman `GzipHttpStream` ล้างแฟล็ก `TINFL_FLAG_HAS_MORE_INPUT` ทำให้ `tinfl` ใน ROM ของ ESP32 สังเคราะห์ไบต์ `0x00` วนไม่รู้จบที่ priority 4 จนจอ/ทัช/คำปลุก/console ค้างทั้งหมด → แยก `util/GzipInflate.h` ส่ง `TINFL_FLAG_HAS_MORE_INPUT` เสมอเพื่อให้คืน `NEEDS_MORE_INPUT` ทันทีเมื่อข้อมูลหมด + เช็ค EOF ใน header gzip + เช็ค deadline/abort + ปล่อย CPU (`delay(1)`) ทุก 15 ms และใช้ `thaiwater::parseJsonArrayItems` บังคับเจอ `,` หรือ `]` จริง (ไม่เผลอแคชอันดับจังหวัดที่โหลดมาครึ่งเดียวเมื่อสตรีมขาดหลัง `}`)
- โค้ด: `util/ThaiWater.h` + `util/GzipInflate.h` (parse + inflate step, host tests `thaiwater_parsing` และ `thaiwater_stream_stall_and_truncation`) · `agent/WaterTools.cpp` (ดึง + การ์ด) · การ์ดใหม่ `CardData::Graph` / `Bars`
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
| backtest / Harmonic / Elliott | ผู้ใช้ไม่ต้องการ (2026-10-30) — แท่งเทียนดึงได้แล้ว (`market_chart`, T142) |
| SMC / strategy signals / sentiment / FRED / แจ้งเตือนตาม indicator | **ทำแล้ว (T143)**: `market_chart` view smc / signals, `market_sentiment`, `price_alert condition` · CNN Fear & Greed ของจริงใช้ไม่ได้ (API ตอบอุปกรณ์ว่า "You're a bot" 418 — บอร์ดไม่ปลอมตัวเป็นเบราว์เซอร์) → คำนวณเองจากองค์ประกอบเดียวกัน · FRED CSV ค้างทุก client ที่ไม่ใช่ curl → ใช้ชุด `FRED:` / `ECONOMICS:` ของ TradingView แทน (ไม่ต้องมี API key) |
| SMC แบบหลายกรอบเวลา (MTF sweeps / liquidity ของแอป) | ยังไม่ทำ — `view smc` เป็นกรอบเวลาเดียว (ถามทีละกรอบได้) |
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
| `market_chart` 1 สัญลักษณ์ (T142) | ~1.1–1.7 วิ (ชื่อย่อหุ้น = 3 ตัวเลือกใน connection เดียว ~1.8–3.5 วิ) |
| `market_chart` + `compare` 4 ตัว | ~3.1–3.7 วิ (เดิมทีละ connection ~9 วิ) |
| `market_chart` view smc (T143) | ~1.5–1.6 วิ (300 แท่ง) |
| `market_chart` view signals 4 กรอบเวลา | ~3.9 วิ (1 กรอบ ~1–4 วิ ตามจำนวนตัวเลือกของชื่อย่อ) |
| `market_sentiment` stocks / crypto / economy | ~5.4 วิ (6 × 420 แท่งรายวัน — TradingView ส่งช้าเอง, JSON บนบอร์ด 0.2 วิ) / ~4.3 วิ (2 handshake + Binance 3 คำขอใน TLS เดียว 0.9 วิ) / ~1.7 วิ (10 ชุด) |
| `price_alert` add แบบ indicator | ~0.9–2.3 วิ · รอบตรวจ 2 นาทีที่มี indicator 3 รายการ ~2.7 วิ |
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
- (T142, 2026-10-30) "ดูกราฟทองคำ 4 ชั่วโมงให้หน่อย แนวรับแนวต้านอยู่ตรงไหน" → `market_chart {"symbol":"OANDA:XAUUSD","timeframe":"4h"}` →
  "ขาลงอย่างชัดเจน… แนวรับที่ใกล้ที่สุด… แนวต้าน 4,032… ตั้งแจ้งเตือนราคาเผื่อถึงแนวรับไหม" · "แล้วบิทคอยน์รายวันล่ะ มีความสัมพันธ์กับทองแค่ไหน" →
  `{"symbol":"BINANCE:BTCUSDT","compare":"OANDA:XAUUSD","timeframe":"1d"}` → "0.55 ไปทางเดียวกันเป็นส่วนใหญ่"
- (T143, 2026-10-30) "วิเคราะห์ SMC บิทคอยน์ มี order block ตรงไหน" → `market_chart {"view":"smc","symbol":"BINANCE:BTCUSDT"}` → "โครงสร้างยังไม่ชัด…
  Supply OB ที่ยังไม่ถูกทดสอบ 78,817–79,023… FVG ขาลง… ฝั่งขาลงได้เปรียบ… ตั้งแจ้งเตือนที่โซนนี้ไหม" · "ตลาดหุ้นอเมริกากลัวหรือโลภ" → `market_sentiment stocks`
  → "กลัวสุดขีด 18 คะแนน…" · "เงินเฟ้อกับดอกเบี้ยสหรัฐล่าสุด แล้วของไทยล่ะ" → 3.7 % / 4 % / 2.82 % / 1 % (เรียกทีละชุด 4 ครั้ง → prompt บอกให้เรียก
  ภาพรวมครั้งเดียว) · "ตั้งเตือนเมื่อ MACD ทองคำ 4 ชั่วโมงตัดขึ้น" → `price_alert {"condition":"macd_up","timeframe":"4h",...}` · แก้ระดับ RSI จากจอกระดิ่ง →
  รอบตรวจถัดไปพูด "RSI 5 นาทีของบิทคอยน์ขึ้นไปแตะ…"
- แจ้งเตือนราคา: ตั้ง BTC below 999999 → 20 วิต่อมา `[Alert] ราคาบิทคอยน์ ลงถึง …` แล้วรายการถูกลบเอง
