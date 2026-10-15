---
tags: [tools, weather, oil, tradingview, finance, news, graphics, cards, mt5, personalaibot]
created: 2026-10-15
updated: 2026-10-15
status: implemented
---

# 📊 Live-Data Tools + การ์ดกราฟิกบนจอ

> **Related:** [[09-Assistant-Profiles-and-Tools]] | [[06-Avatar-and-Thai-UI]] | [[10-PC-Studio-App]] | [[07-Troubleshooting-Log]]
> **Code:** `src/agent/WebTools.*` (task + weather/oil/quote/FX/news) · `src/agent/WebToolsExtra.cpp` (tools ที่พอร์ตจาก PersonalAIBot) · `src/agent/WebToolsCommon.h` (HTTPS, TradingView batch) · `src/util/Calc.h` · `src/util/Units.h` · `src/ui/CardScreen.*` (วาดการ์ด) · `src/agent/AgentTools.cpp` (declarations)

AI เรียก tool → บอร์ดดึงข้อมูลจริง (HTTPS) หรือคำนวณเอง → **แสดงการ์ดกราฟิกบนจอ** ~30 วิ (แตะเพื่อปิด) → AI **พูดสรุป** ให้ฟัง
ใช้แทน Google Search ได้ในเรื่องเหล่านี้ (โควตา Search ของ key ปัจจุบันหมด — [[07-Troubleshooting-Log#T028 "You exceeded your current quota" ทั้งที่ยังไม่ถึงลิมิต - โควตา Google Search]])

## จำนวน tool ทั้งหมด

| ชุด | จำนวน | เปิดเมื่อ |
|---|---|---|
| core (ความจำ, เวลา, งาน, ผู้ช่วย, เครื่อง) | 22 | เสมอ |
| live data (อากาศ, น้ำมัน, ราคา, แลกเงิน, ข่าว) | 5 | เสมอ |
| utilities ที่พอร์ตจาก PersonalAIBot | 6 | เสมอ |
| **finance pack** | 6 | `set finance 1` (ค่าเริ่มต้น เปิด) |
| **MT5** `mt5_account` | 1 | ตั้ง `mt5url` แล้ว |
| **MT5** `mt5_trade` | 1 | ตั้ง `mt5url` **และ** `set mt5trade 1` (ค่าเริ่มต้น ปิด) |

ค่าเริ่มต้น = **39 tools** (~14.6 KB JSON ต่อ setup; core 22 รวม `configure_device`, `mute_microphone`) + Google Search (ถ้าใช้ได้) — ทดสอบใน host test `tool_declarations_per_model`

## 1) Live data (ชุดแรก)

| Tool | ข้อมูล | แหล่ง (ไม่ต้องใช้ key) | การ์ด |
|---|---|---|---|
| `get_weather(location?, location_en?)` | อากาศตอนนี้, **PM2.5**, พยากรณ์ 5 วัน | Open-Meteo forecast / air-quality / geocoding | ไอคอนวาดเอง + 5 วัน · **ไม่ระบุที่ = ตำแหน่งจาก IP** |
| `get_oil_price()` | น้ำมันขายปลีก 8 ชนิด วันนี้ / เมื่อวาน / พรุ่งนี้ | `oil-price.bangchak.co.th/ApiOilPrice2/th` | ตาราง + ▲▼ |
| `get_market_quote(symbols)` | ราคา, ช่วงวันนี้, 1W/1M/3M/1Y, RSI, สัญญาณ | `scanner.tradingview.com/symbol` | 1 ตัว = กราฟ · 2–6 ตัว = ตาราง |
| `convert_currency(amount, from, to)` | แปลงเงิน | TradingView `FX_IDC:` | ยอดเงิน + อัตรา |
| `get_news(topic?)` | หัวข้อข่าว 8 ข่าว | Google News RSS (ไทย) | หัวข้อละ 2 บรรทัด |

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
| `notes(action, …)` | สมุดโน้ตบนบอร์ด: add / list / search / read / delete (สูงสุด 100) | `<agentDir>/notes.json` (SD หรือ LittleFS) | 0 วิ |

### Finance pack

| Tool | ทำอะไร | แหล่ง |
|---|---|---|
| `market_overview()` | SET, S&P 500, Nasdaq 100, ทองคำ, WTI, BTC, USD/THB + ดัชนีกลัว/โลภคริปโต | TradingView `POST /global/scan` (1 request ทุกตลาด) + `api.alternative.me/fng` |
| `market_analysis(symbol, timeframe?, fundamentals?)` | RSI, MACD, Stoch, EMA20/50/200, ADX, BB, สัญญาณรวม (MA/oscillators) — `timeframe`: 15m/1h/4h/1d/1w หรือ **`multi`** (สัญญาณทุกกรอบเวลา) · `fundamentals`: P/E, P/BV, ปันผล, มูลค่าตลาด, EPS, sector | TradingView `/symbol` field suffix `|15`, `|60`, `|240`, `|1W` |
| `market_scan(market, type, limit?)` | หุ้นไทย / หุ้นสหรัฐ / คริปโต (Binance USDT): ขึ้นแรง, ลงแรง, วอลุ่มพุ่ง, RSI ขายมากเกิน/ซื้อมากเกิน | TradingView `POST /{thailand|america|crypto}/scan` + filter (ตัด `.R`/`.F` ซ้ำ) |
| `economic_calendar(range?, impact?)` | ข่าวเศรษฐกิจสำคัญ today/tomorrow/week **เวลาไทย** + forecast/previous — การ์ดแสดงเฉพาะที่ยังไม่ถึง | `nfs.faireconomy.media/ff_calendar_thisweek.json` (แคช 30 นาที เพราะจำกัดจำนวนครั้ง) |
| `position_size(balance, entry, stop_loss, risk_percent?, symbol?, contract_size?)` | ขนาด lot ตามความเสี่ยง (XAU 100 oz, XAG 5000, FX 100k, หุ้น/คริปโต 1) | คำนวณบนบอร์ด |
| `price_alert(action, symbol?, above?, below?, id?)` | ตั้งเตือนราคา (สูงสุด 10) — task webTools ตรวจ **ทุก 2 นาที** (batch เดียว) แล้ว AI **พูดแจ้งเตือน** + เสียงกริ่ง + ข้อความบนหน้าตา (ปลุกจากโหมดพักด้วย) | `<agentDir>/alerts.json` + `/global/scan` |

ทุก tool การเงินใส่ note ให้ AI บอกว่า "ไม่ใช่คำแนะนำการลงทุน"

![[images/card-overview.png]] ![[images/card-analysis.png]]
![[images/card-analysis-multi.png]] ![[images/card-scan.png]]
![[images/card-calendar.png]] ![[images/card-alert.png]]
![[images/card-calc.png]] ![[images/card-worldtime.png]]
![[images/card-position.png]] ![[images/card-wiki.png]]

### MT5 (mt5-core-server bridge — ระบบเดียวกับแอปมือถือ)

- ตั้งค่าใน PC Studio → ⚙️ ตั้งค่า → **การเงิน / MetaTrader 5** หรือ console `set mt5url http://<server>:<port>`, `set mt5token <token>` (`-` = ลบ)
- API: `{url}/api/mt5/account|positions|orders|history` (GET), `/order|close|close_all|modify` (POST), `Authorization: Bearer <token>`; รองรับผลลัพธ์ห่อใน `data/result/items/positions/...` และ type 0/1 หรือ side/action แบบเดียวกับ `Mt5TerminalService.kt`
- `mt5_account(view?)` — summary (balance, equity, กำไร/ขาดทุนลอย, free margin + สถานะเปิด) / positions / orders / history
- `mt5_trade(action, symbol, volume, sl?, tp?, ticket?, confirm?)` — **ปิดเป็นค่าเริ่มต้น** และมีกันพลาด 3 ชั้น:
  1. ต้องเปิด `mt5trade` เอง (PC Studio มีกล่องเตือนก่อนเปิด; นำเข้าไฟล์ตั้งค่าจะไม่เปิดให้)
  2. `volume` ≤ `mt5maxlot` (ค่าเริ่มต้น 0.10)
  3. เรียกครั้งแรก (ไม่มี `confirm`) = แสดงการ์ด "ยืนยันคำสั่ง MT5?" ให้ AI อ่านทวน → ต้องเรียกซ้ำด้วยคำสั่ง**เดียวกัน** + `confirm=true` ภายใน 2 นาที
- token เก็บใน NVS แบบเข้ารหัส (เหมือน API key — [[07-Troubleshooting-Log#T042 รหัส Wi-Fi และ API key เก็บเป็นข้อความธรรมดาในแฟลช]]) และไม่ถูกพิมพ์ออก log (`config` แสดงแค่ `mt5tokenSet`)

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
task webTools (core 0, prio 2, 12 KB): HTTPS (WiFiClientSecure + WEB_ROOT_CA_PEM) / คำนวณ → CardData
   ├─ gCard.show(card)  → task UI push การ์ดทับหน้าตา (เฉพาะหน้าหลัก), ปิดเองหลัง 30 วิ / แตะ
   ├─ Result → AgentController::handleWebResults → toolResponse ด้วย id เดิม (ทิ้งถ้า session เปลี่ยนแล้ว)
   └─ ว่างอยู่ทุก 2 นาที: checkPriceAlerts() → Result(tag = kAlertTag) → announcement "[แจ้งเตือนราคาจากระบบ] …"
```

- การ์ดชนิด: `Weather`, `Oil`, `Quotes`, `News`, `Text` (ตัวเลขใหญ่ + ย่อหน้า), `Table` (ชื่อ + ค่าสี), `Analysis` (เกจ -1..+1 + ตาราง 8 แถว)
- TLS ตรวจใบรับรองทุกโฮสต์: `WEB_ROOT_CA_PEM` = Google GTS R1/R3/R4 + GlobalSign ECC R4 + ISRG Root X1 + DigiCert Global Root G2 + Amazon Root CA 1 — ครอบคลุม Open-Meteo, บางจาก, TradingView, Wikipedia, ipwho.is, alternative.me, faireconomy (สร้างด้วย `python tools/gen_root_ca.py`); `http://` ใช้ได้เฉพาะ MT5 bridge ใน LAN
- console / PC app: `tool <name> {json}` → `{"status":"fetching..."}` แล้วตามด้วยบรรทัด `TOOL <name> {...}` + การ์ดบนจอ

## เวลาที่ใช้ (วัดบนบอร์ด 2026-10-15)

| tool | เวลา |
|---|---|
| calculate / convert_units / get_world_time / position_size / notes | ~0 วิ |
| `market_analysis` / `market_scan` / `get_market_quote` 1 ตัว | ~1.1–1.3 วิ |
| `price_alert add` | ~1.3 วิ |
| `get_oil_price` / `get_location` / `economic_calendar` | ~2–3 วิ |
| `market_overview` (+ Fear&Greed) | ~3.5 วิ |
| `search_knowledge` | ~8 วิ |
| `get_weather` | ~8.5 วิ (geocode + forecast + PM2.5) |

## ทดสอบจริงกับ AI (2026-10-15, `ask` ผ่าน console)

- "ตลาดหุ้นวันนี้เป็นยังไงบ้าง" → `market_overview` → "ดัชนี SET บวกเล็กน้อย… S&P 500 กับ Nasdaq ปิดลบ… ทองคำขึ้นนิดหน่อย"
- "วิเคราะห์ทองคำทุกกรอบเวลา" → `market_analysis {"symbol":"gold","timeframe":"multi"}` → สรุประยะสั้น/ยาว
- "สัปดาห์นี้มีข่าวเศรษฐกิจอะไรสำคัญ" → `economic_calendar {"range":"week"}`
- "เจ็ดเปอร์เซ็นต์ของ 2350" → `calculate {"expression":"2350 * 7%"}`
- แจ้งเตือนราคา: ตั้ง BTC below 999999 → 20 วิต่อมา `[Alert] ราคาบิทคอยน์ ลงถึง …` แล้วรายการถูกลบเอง
