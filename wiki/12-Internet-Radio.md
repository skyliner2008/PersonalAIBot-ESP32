---
tags: [radio, audio, streaming, helix, mp3, aac, ducking, vad, station-list, ui]
created: 2026-10-15
updated: 2026-10-15
status: implemented — station list + touch player, tested on hardware 2026-10-15
---

# 📻 Internet Radio

> **Related:** [[06-Avatar-and-Thai-UI]] | [[09-Assistant-Profiles-and-Tools]] | [[11-Data-Tools-and-Cards]] | [[04-System-Architecture]]
> **Code:** `src/audio/RadioStations.*` (รายการสถานี) · `src/ui/RadioScreen.*` (หน้าจอวิทยุ) · `src/audio/RadioPlayer.*` (stream + decode) · `src/util/RadioResampler.h` (→ mono 24 kHz) · `src/audio/AudioManager.*` (mixer + ducking) · `src/agent/AgentDevice.cpp` (`playRadio`, `handleRadio`) · `lib/helix-mp3`, `lib/helix-aac`

## ปัญหาที่ต้องแก้ก่อนเพิ่มวิทยุ

ไมค์กับลำโพงอยู่บนบอร์ดเดียวกัน → ไมค์ได้ยินวิทยุเต็มๆ:
- VAD ในเครื่องและ Gemini แยกเสียงผู้ประกาศ/นักร้องกับเสียงผู้ใช้ไม่ออก → AI จะตอบวิทยุ
- ถ้าส่งไมค์ขึ้น Gemini ตลอดเวลาที่เปิดวิทยุ → โทเคนหมด (ลิมิต Live 65k/นาที)
- การแยกเสียงพูดสองแหล่งแบบอัจฉริยะ (source separation) หนักเกิน ESP32

## การออกแบบ (เฟส 1 — ผู้ใช้เลือก: "แตะ/พูดดังเพื่อสั่ง" + "ลดเสียงเบาๆ ตอน AI พูด")

```
"เปิดวิทยุ Cool 93" → play_radio(station) → RadioStations.find() → RadioPlayer.play(url)
   AI พูดชื่อสถานีสั้นๆ (วิทยุดัง 12% ข้างใต้)
   เงียบ RADIO_RESUME_MS (6 วิ) → enterSleep(): ปิดเซสชัน Gemini (ไม่เสียโทเคน) → วิทยุดัง 100%
ขณะพัก + วิทยุเล่น:
   แตะจอ / กด BOOT                     → ตื่นทันที (ชัวร์ 100%)
   พูดดังกว่าเสียงวิทยุ (level > threshold × 2, นานเป็น 2 เท่า) → ตื่นด้วยเสียง
   ตื่น → วิทยุลดเหลือ 12% (RADIO_DUCK_GAIN, ไล่ระดับ ~80 ms) → คุย/สั่งงานปกติ → เงียบ 6 วิ → พัก → วิทยุดังเท่าเดิม
```

- พักครบ `clockMinutes` → โหมดนาฬิกาหรี่แสง วิทยุยังเล่นต่อ (ชื่อสถานี/เพลงแสดงใต้นาฬิกา)
- **ปลุกด้วยเสียงขณะเปิดวิทยุ = ประมาณเสียงสะท้อนอย่างง่าย**: บอร์ดรู้ระดับเสียงวิทยุที่ส่งออกลำโพง (`AudioManager::radioOutLevel`, หลัง duck gain) จึงเรียนรู้ `coupling` = ระดับไมค์ ÷ ระดับวิทยุ จากช่วงที่มีแต่เพลง (EMA; 2.5 วิแรกหลังพักเรียนเร็ว เพราะวิทยุกำลังดังขึ้น) แล้วนับเป็นเสียงพูดเมื่อ ระดับไมค์ > coupling × ระดับวิทยุ × `RADIO_WAKE_FACTOR` (3) ต่อเนื่อง 8 chunk — ปรับตามจังหวะเพลงทันที ต่างจาก noise floor ที่ตามช้า
  - วัดจริง: coupling ≈ 0.04–0.10 (ไมค์ได้ยินลำโพง 4–10%)
  - **v3.5.1 (เฟส 0 ของ [[14-AEC-WakeWord-Plan]]):** ใช้ **เสียงอ้างอิงจากฮาร์ดแวร์** แทนค่าที่คำนวณในซอฟต์แวร์ — ES8311 reg 0x44 = 0x50 (L ไมค์, R DAC loopback)
    เก็บคู่ (ไมค์, อ้างอิง) ใน capture ring ตรงกันทุก sample → ต่อ chunk 40 ms รู้ว่าลำโพงเล่นอะไรในช่วงเดียวกับที่ไมค์ได้ยิน (`util/EchoWakeGate.h`):
    envelope ลดลง ×0.6/chunk (เสียงก้องห้อง ~0.3 วิหลังเพลงเบาลง) • คาดหมาย = √((coupling×envelope)² + เสียงห้อง²) • coupling จากค่าเฉลี่ยพลังงาน •
    → **v3.5.1 รอบ 3 (T058): high-pass 250 Hz ก่อน AEC (ลำโพงเล่นเบสไม่ออก), เกณฑ์ปลุกเทียบเสียงสะท้อนที่ตัดออก, micgain 6 ใช้ได้**
    → **v3.5.1 รอบ 2 (T057): ตัดเสียงสะท้อนจริงด้วย NLMS 128 tap** (`util/EchoCanceller.h`) แล้วตัดสินจากเสียงที่เหลือ — การเทียบความดังข้างล่างเหลือเป็นทางสำรองเมื่อไม่มี loopback
    กฎเข้มตาม**ช่วงเปิดวิทยุ** (`gRadio.active()`) ไม่ใช่ "ตอนนี้มีเสียง" (สตรีมสะดุดชั่วครู่เคยตกไปใช้เกณฑ์ปกติแล้วเสียงห้องปลุก) •
    เปลี่ยนระดับเสียง = คูณ coupling ตาม dB ที่รู้ (0.5 dB/step) แทนเรียนใหม่ • ไมค์ > 0.35 ขณะเล่น = ลำโพงดังจนไมค์อิ่มตัว ไม่นับ
  - เดิมใช้ noise floor × 2: ตื่นเองทุก ~1.5 วิหลังเข้าพัก (noise floor ยังเป็นค่าตอนวิทยุเบา 12%) → แบบใหม่ 55 วิ ไม่ตื่นเองเลย
- ระหว่างวิทยุเล่น หยุดการเลือกช่องไมค์ (L/R) ใหม่ — ไม่งั้นอาจไปเลือกช่องที่เป็น DAC loopback

## รายการสถานี (ล็อกไว้ — ไม่ค้นออนไลน์)

ผู้ใช้เลือก (2026-10-15): **ไม่ค้นสถานีเอง** ใช้รายการที่ล็อกไว้แทน — เดิม `play_radio` ค้น Radio Browser ออนไลน์ ช้า (~20 วิ) และบางครั้งได้ URL เสีย จึงตัดทิ้ง

- เก็บที่ `<การ์ด SD>/agent/radio_stations.json` (`[{name,url,genre}]`, สูงสุด 40) — ไม่มีไฟล์ = เขียนรายการเริ่มต้น 16 สถานีไทยที่ทดสอบแล้ว (ยิงจาก PC: HTTP 200, MP3/AAC, มีข้อมูลเสียงไหล) · ไม่มีการ์ด = ใช้รายการเริ่มต้นในหน่วยความจำ
- สถานีล่าสุดจำเป็น **ชื่อ** (`cfg/radioname`) ไม่ใช่ URL — แก้ URL ใน Studio แล้วยังจำได้
- ชื่อ ≤ 47 ไบต์ (ภาษาไทย ≈ 15 ตัว), แนว ≤ 39 ไบต์, URL ≤ 199 ตัว ASCII เท่านั้น
- **AI เห็นรายการทั้งหมดใน system prompt** ("สถานีวิทยุในรายการ: 1 COOL Fahrenheit 93 (เพลงฟังสบาย), 2 …") จึงเลือกได้เองจากคำพูด
- `RadioStations::find(query)` ลำดับการจับคู่: เลขลำดับล้วน ("4") → ชื่อมีคำนั้น (ไม่สนช่องว่าง/ตัวพิมพ์) → แนวเพลง ("ลูกทุ่ง", "ข่าว") → คำใดคำหนึ่งในชื่อ ("เปิด 93.5") → เลขในประโยค ("สถานีที่ 4")

| สถานีเริ่มต้น | แนว |
|---|---|
| COOL Fahrenheit 93 · Smooth 105.5 (อสมท) · Radio Samui | ฟังสบาย / สากล / ชิลล์ |
| ลูกทุ่ง รักไทย 90 · FM 103 คนลูกทุ่ง · Isan FM 98 | ลูกทุ่ง / อีสาน |
| มิติข่าว 90.5 · JS100 · สวท. FM 92.5 · Radio Thailand FM 88 | ข่าว / จราจร / ข่าวภาษาอังกฤษ |
| 94 Bangkok Smile · 106 Yes Radio · 93.5 Top Radio | เพลงไทย / เพลงเก่า |
| Surf 102.5 · BKK.FM | ป๊อป ร็อก / อัลเทอร์เนทีฟ |
| บ้านธัมมะ | ธรรมะ |

### เพิ่มจาก `Thailand.json` (2026-10-18, T070) — บอร์ดของผู้ใช้มี 40 สถานี (เต็ม)

ไฟล์รายชื่อสถานีไทย 406 สถานี (แบบ radio-browser: `Title`, `Genre`, `Source1..6`) ตรวจด้วย **`tools/radio_check.py`** แบบเดียวกับที่บอร์ดเล่น:
แตก `.pls`/`.m3u` เป็น URL สตรีม (บอร์ดไม่อ่าน playlist) · ต่อแบบ HTTP/1.1 + `Icy-MetaData` + redirect + รับ `ICY 200` + TLS ไม่ตรวจ ·
ต้องเจอเฟรม MP3 (layer III) / AAC-ADTS ต่อกัน 3 เฟรม, ≤ 192 kbps, เน็ตส่งทันเวลาจริง, content-type ที่ `RadioPlayer` ยอมรับ
- **ใช้ได้ 56 / 406** — ส่วนใหญ่ตาย: โดเมนหาย 115, ปฏิเสธการเชื่อมต่อ 79, timeout 55, HTTP error 19, HLS `.m3u8` 13 (บอร์ดเล่นไม่ได้), > 192 kbps 3
- **29 สถานีได้คืนด้วยการต่อท้าย `;`** — SHOUTcast v1 ส่งหน้า HTML ให้ user agent ที่มี "Mozilla" (ของบอร์ด) → `http://host:port/;` ได้ตัวสตรีม (เก็บ URL แบบนี้ในรายการเลย)
- ตัดซ้ำ: 94 Smile / 94 E FM, รักไทย 90, JS100, Yes 106 (97 Mazz = icy "YES Radio 106"), Met 107 ×2, Mello ×2, FM 102 ×2, อสมท พิจิตร = วัดนาคปรก
- **เพิ่ม 24 สถานีผ่าน `radioadd`** (ลำดับ 17–40) และ**ลองเล่นบนบอร์ดทุกสถานี: เล่นได้ 24/24** (หลังแก้ decoder — ดูด้านล่าง)

| เพิ่มแล้ว (17–40) | แนว |
|---|---|
| MCOT News 100.5 · FM 102 ขสทบ. | ข่าว / จราจร |
| Met 107 · Mello 97.5 · City Life 93.75 ศรีราชา | สากล / ฟังสบาย |
| Modern 98.75 | ร็อก |
| Kiss 91.75 · Love Radio 93.75 · Radio Trip 89.75 ภูเก็ต · Hitz FM 102.25 ลำปาง · 107 Love FM ลำปาง · Fat 93 เชียงราย · 107 Pink Radio เชียงราย · Best Radio 89 ขอนแก่น (AAC) · ส.ทร.15 นราธิวาส 94.75 | ป๊อป / วาไรตี้ |
| Hot Monitor 102.25 กระบี่ · TP4 107.5 ชุมพร · TP4 103 ตรัง | ลูกทุ่ง |
| อสมท ภูเก็ต 101.5 · โคราช 95.75 · สงขลา 96.5 · พิษณุโลก 106.25 · จันทบุรี 95.25 · ลำปาง 99 | อสมท ภูมิภาค |

**สำรอง (ใช้ได้ ณ 2026-10-18 แต่ไม่มีที่ว่าง — สลับใน Studio แท็บวิทยุ / `radioedit <n> ชื่อ|URL|แนว`):**

| สถานี (ชื่อในไฟล์) | รูปแบบ | URL |
|---|---|---|
| ส.ทร.  3 ภูเก็ต am 1485 khz | MP3 128 kbps | `http://rcp.plathong.net:7354/index.html/;` |
| mcot radio network fm 107.75 mhz พิจิตร | MP3 160 kbps | `http://rstream.mcot.net:9004/;` |
| mcot radio network fm 102.5 mhz ยะลา | MP3 192 kbps | `http://rstream.mcot.net:9102/;` |
| mcot radio network fm  91 mhz ปัตตานี | MP3 192 kbps | `http://rstream.mcot.net:9098/;` |
| mcot radio network fm  95.75 mhz พัทลุง | MP3 192 kbps | `http://rstream.mcot.net:9100/;` |
| mcot radio network fm  92 mhz บุรีรัมย์ | MP3 160 kbps | `http://rstream.mcot.net:9050/;` |
| mcot radio network fm 104.5 mhz นครศรีธร | MP3 128 kbps | `http://rstream.mcot.net:9094/;` |
| mcot radio network fm  90.75 mhz ชุมพร | MP3 112 kbps | `http://rstream.mcot.net:9014/;` |
| mcot radio network fm  92 mhz กาฬสินธุ์ | MP3 192 kbps | `http://rstream.mcot.net:9028/;` |
| mcot radio network fm 105 mhz กระบี่ | MP3 128 kbps | `http://rstream.mcot.net:9084/;` |
| mcot radio network fm 106.75 mhz ประจวบค | MP3 112 kbps | `http://rstream.mcot.net:9012/;` |
| mcot radio network fm 100.5 mhz ระนอง | MP3 128 kbps | `http://rstream.mcot.net:9016/;` |
| mcot radio network fm  96.75 mhz ระยอง | MP3 112 kbps | `http://rstream.mcot.net:9026/;` |
| mcot radio network fm 107 mhz สกลนคร | MP3 160 kbps | `http://rstream.mcot.net:9040/;` |
| mcot radio network fm  93.25 mhz สตูล | MP3 192 kbps | `http://rstream.mcot.net:9106/;` |
| mcot radio network fm 105.30 mhz สิงห์บุ | MP3 128 kbps | `http://rstream.mcot.net:9006/;` |
| mcot radio network fm 102 mhz สุราษฎร์ธา | MP3 112 kbps | `http://rstream.mcot.net:9090/;` |
| mcot radio network fm  99.25 mhz สุโขทัย | MP3 64 kbps | `http://rstream.mcot.net:9080/;` |
| mcot radio network fm 102.5 mhz หนองคาย | MP3 192 kbps | `http://rstream.mcot.net:9042/;` |
| mcot radio network fm  92 mhz น่าน | MP3 112 kbps | `http://rstream.mcot.net:9072/;` |
| mcot radio network fm 107.25 mhz กาญจนบุ | MP3 192 kbps | `http://rstream.mcot.net:9010/;` |
| Pattaya People Radio Radio 96 FM | MP3 128 kbps | `http://s1.thailandstreaming.net:8020/;` |
| 97 Mazz Radio Phuket | AAC 128 kbps | `http://radio1.comgig.com:4096/;stream.m4a` |
| js 100 กรุงเทพฯ | MP3 128 kbps | `http://sv4.hostpleng.com:8184/;` |
| สถานี: TP4, FM FM 89.25 MHz. Phatthalung | MP3 96 kbps | `http://radio11.plathong.net/7018/;` |

**พบระหว่างทดสอบ:** ชื่อเพลงบางสถานี (Hot Monitor) เป็น TIS-620 ที่ server แปลงเป็น UTF-8 ผิดซ้ำอีกชั้น ("¢èÒÇ…") — `validUtf8` ผ่านจึงไม่ถูกแปลง (ยังไม่แก้)

## หน้าจอวิทยุ (`RadioScreen`)

![[images/radio-player.png]] ![[images/radio-player-stopped.png]]

| ส่วน | การแตะ |
|---|---|
| หัว: `<` / ชื่อ / นาฬิกา / ⏸ หรือ ▶ / **⏻ ปิด** (แดง) | `<` กลับ (วิทยุยังเล่น) · ⏸ หยุดชั่วคราว (อยู่หน้าวิทยุ) · ▶ เล่นสถานีล่าสุด · **⏻ ปิด = หยุดวิทยุ + กลับหน้า avatar** (v3.4, ผู้ใช้ขอ: "มีแต่ปุ่มหยุดชั่วคราว ไม่มีปุ่มปิด") |
| **แผงกลาง** (ชื่อสถานี, เพลง, แท่งระดับเสียง, สถานะ AI) | **แตะ = สั่งงาน AI เหมือนแตะหน้า avatar** (`uiTap`), กดค้าง = เริ่มเซสชันใหม่ · บรรทัดล่างบอก "AI กำลังฟัง / คิด / พูด" |
| แถบสถานี (เลื่อนซ้าย-ขวา) | แตะ = เปลี่ยนสถานี (สถานีที่เล่นเป็นสีฟ้า, เลื่อนมากลางเองเมื่อ AI เปลี่ยนให้) |
| ⏮ − 70% + ⏭ | สถานีก่อน/ถัดไป · เสียง ±10% (บันทึกในการตั้งค่า) |

- **เปิดหน้าจออัตโนมัติ** เมื่อวิทยุเริ่มเล่น (สั่งด้วยเสียง / console / Studio) ขณะอยู่หน้า avatar
- เปิดเอง: แตะแถบชื่อสถานีใต้ดวงตา หรือ ⚙ ตั้งค่า → "วิทยุออนไลน์"
- เข้าโหมดประหยัดพลังงาน → หน้าวิทยุหลบให้นาฬิกา (หรี่จอ) → ตื่น → กลับมาเอง; แตะหน้าวิทยุ = ยืดเวลาก่อนเข้าโหมดนาฬิกา (`uiActivity`)
- การ์ดข้อมูล (อากาศ ฯลฯ) แสดงทับหน้าวิทยุได้ ปิดแล้วกลับหน้าวิทยุ
- แท่งระดับเสียง = `sqrt(radioOutLevel)` (หลัง duck gain) — ยังขยับตอน AI พูด

## Signal path

```
HTTP(S) stream ──► ICY metadata strip (StreamTitle, TIS-620 → UTF-8) ──► 16 KB input buffer
   ──► Helix MP3 / AAC (HE-AAC) decoder ──► stereo→mono + biquad low-pass + linear resample → 24 kHz
   ──► AudioManager radio ring (192 KB PSRAM, เริ่มเล่นเมื่อมี 1 วิ) ──► mixer: voice + radio × gain ──► I2S → ES8311 → ลำโพง
```

- task `radio` core 0, prio 4, stack 16 KB; ป้อนเข้า ring ตามจังหวะจริง (รอที่ว่าง) จึงไม่กิน CPU เกินจำเป็น
- redirect ทำเอง (http ↔ https เปลี่ยน client), HTTPS สตรีมเปิดแบบไม่ตรวจใบรับรอง (เสียงสาธารณะ ไม่มีความลับ, สถานีใช้ CA หลากหลาย)
- หลุด → ต่อใหม่สูงสุด 4 ครั้ง (2, 4, 6, 8 วิ) — ไม่ได้ → แจ้งบนจอ "เปิดวิทยุไม่ได้ (...)" และ AI บอกผู้ใช้
- รองรับ: MP3, AAC/AAC+ (ADTS) ≤ 192 kbps — ไม่รองรับ Ogg/Opus, HLS (.m3u8)

## ทดสอบบนบอร์ด (2026-10-15)

![[images/radio-face.png]] ![[images/radio-clock.png]]

| ปัญหาที่เจอ | สาเหตุ | แก้ |
|---|---|---|
| อัตราเสียงกระโดด 44.1 → 22.05 → 48 kHz, เสียงสะดุด | สถานี (CDN byteark) ส่ง `Transfer-Encoding: chunked` + ICY — ตัวเลขขนาด chunk ปนในข้อมูลเสียง → false sync | ตัวอ่าน body ถอด chunk ก่อนตัด ICY + ล็อก rate/ช่องเสียงหลังเฟรมแรก (เปลี่ยนเมื่อเจอซ้ำ 8 เฟรม) |
| บอร์ดรีบูต: `task_wdt: IDLE0 (CPU 0)` ขณะ `webTools` ทำงาน | ช่วงเติมบัฟเฟอร์ ตัวถอดรหัสไม่ได้รอเลย + TLS handshake/JSON ของ webTools บน core 0 เดียวกัน → idle task ของ core 0 ไม่ได้รัน > 5 วิ (Arduino เฝ้าเฉพาะ IDLE0) | `vTaskDelay(1)` ทุกเฟรม + ย้าย `webTools` และ `textModel` ไป core 1 (core 0 = เสียง, วิทยุ, Wi-Fi) |
| ตื่นเองหลังเข้าพัก | ดูหัวข้อการปลุกด้านบน | ประมาณเสียงสะท้อน |
| การ์ดสถานีค้างทับนาฬิกา, ชื่อยาวถูกตัด | — | ปิดการ์ดเมื่อเข้าโหมดนาฬิกา, ชื่อยาวใช้ฟอนต์เล็ก + "..." (การ์ดวิทยุเลิกใช้ — แทนด้วยหน้าจอวิทยุ) |
| ค้นออนไลน์ช้า 21 วิ แล้วล้ม, "Starting ?", URL สถานีล่าสุดเป็นขยะ | Radio Browser ช้า/ล้มบ่อยบนเน็ตมือถือ; ต้นเหตุ URL ขยะหาไม่พบ | เลิกค้นออนไลน์ → รายการล็อก + จำชื่อสถานีแทน URL |

ผล (เฟส 1): เล่นต่อเนื่อง ~2 นาที พร้อมเรียก Wikipedia/ภาพรวมตลาด/อากาศซ้อน — ไม่รีบูต, 44.1 kHz นิ่ง, บัฟเฟอร์เต็ม ~4 วิ, แตะ → ตื่น (วิทยุ 12%) → เงียบ 6 วิ → พัก (วิทยุ 100%)
ผล (รายการ + หน้าจอ): `play_radio` "ลูกทุ่ง" → ลูกทุ่ง รักไทย 90, "สถานีที่ 4" → Smooth 105.5, "เปิด 93.5" → 93.5 Top Radio, "ข่าว" → มิติข่าว 90.5, ชื่อที่ไม่มี → error · แตะแถบสถานี/ปุ่มเสียง/โหมดนาฬิกา-ตื่น ทำงานถูกต้อง · เพิ่ม/ย้าย/ลบผ่าน console แล้วรายการคงอยู่หลังรีบูต
ยังไม่ได้ทดสอบ: ผู้ใช้พูดแทรกเพลงเพื่อปลุก (ต้องคนจริง) — ถ้าปลุกยากให้ลด `RADIO_WAKE_FACTOR`, ตื่นเองบ่อยให้เพิ่ม

## เปิดวิทยุแล้วพักทันที + จำสถานีล่าสุด (v3.4, T050)

- **เปิดด้วยเสียง** (`play_radio`) → AI บอกชื่อสถานีแล้ว**พักทันทีหลังพูดจบ** (`requestSleep`) ไม่มีช่วง "ฟังต่อ 6 วิ" — เดิมช่วงนั้นส่งเสียงดีเจเข้า AI เป็น "คำพูดผู้ใช้" แล้ว AI ตอบเสียงวิทยุ
- **เปิดจากจอ / console ขณะ AI ตื่นและไม่ได้คุยอยู่** → พักทันทีเช่นกัน (`handleRadio` ตอนวิทยุเริ่มดัง)
- ตอนพัก ไมค์ฟังแค่เสียงปลุก (echo-aware) + **เกณฑ์ขั้นต่ำ `RADIO_WAKE_MIN_LEVEL` 0.02** — เดิมช่วงรอยต่อเพลงเสียงรั่ว 0.005–0.008 ปลุก AI เองโดยไม่มีคนพูด (เสียงพูดจริง ~0.05–0.12)
- **สถานีล่าสุด:** บันทึกทุกครั้งที่เล่น (เสียง / จอ / console → `Settings.radioName`) และ prompt บอก AI ว่า "สถานีล่าสุด = N ชื่อ — สั่งเปิดวิทยุเฉยๆ ให้เรียก play_radio โดยไม่ใส่ station"; คำกลางๆ ที่โมเดลชอบใส่ ("วิทยุ", "ล่าสุด", "last" ...) ถือว่าไม่ระบุ
- log `[Tool] play_radio {"station":"..."} -> ok` แสดง argument ที่โมเดลส่ง (ยกเว้น tools ความจำ/โน้ต)
- ทดสอบบนบอร์ด: "เปิดวิทยุหน่อย" → สถานี 5 (ล่าสุด) → พัก 0.4 วิหลังพูดจบ · เปลี่ยนเป็น 4 แล้ว "เปิดวิทยุ" → 4 · สถานีข่าว (พูดตลอด) 45 วิ ไม่ตื่นเอง

## Tools

| Tool | ทำอะไร |
|---|---|
| `play_radio(station?)` | เล่นสถานีจาก **รายการ** — เลขลำดับ / ชื่อ / แนวเพลง; ว่าง = สถานีล่าสุด; ไม่มีในรายการ → error ให้ AI เลือกจากรายชื่อใน prompt (เป็น tool ในเครื่อง ไม่ผ่าน webTools) |
| `stop_radio()` | ปิดวิทยุ + ปิดหน้าจอวิทยุกลับหน้าหลัก (เหมือนปุ่ม ⏻, T066) |
| `get_device_status` | มี `radio.station / now_playing` — ตอบ "เพลงอะไรอยู่" ได้ |

ปรับความดังวิทยุ = `set_volume` (ความดังรวมของลำโพง)

## Console / Studio

- `radio <n>` เล่นสถานีลำดับ n · `radio <url> [ชื่อ]` เล่นสตรีมตรงๆ (ไม่เพิ่มในรายการ) · `radio stop` · `radio` = สถานะ (state, สถานี, เพลง, codec, Hz, buffered ms, error)
- รายการ (เลขเริ่ม 1): `radiolist` · `radioadd ชื่อ|url|แนว` · `radioedit <n> ชื่อ|url|แนว` · `radiodel <n>` · `radiomove <จาก> <ไป>` · `radioreset` — ทุกคำสั่งตอบ `OK`/`ERR …` แล้วตามด้วย `RADIOLIST {"stations":[…],"playing":n,"state":"…","max":40,"error"?}`
- `diag`: `radio`, `radio_station`, `radio_title`, `radio_codec`, `radio_rate`, `radio_error`
- **Studio → 📻 วิทยุ**: ตารางสถานี (▶ = กำลังเล่น), เล่น/หยุด, ⬆⬇ เรียงลำดับ, ลบ, ฟอร์มเพิ่ม/แก้ไข, **🔎 ทดสอบ URL จาก PC** (ดึง 32 KB ดู content-type ว่าเป็น MP3/AAC), คืนค่ารายการเริ่มต้น — ดู [[10-PC-Studio-App]]

## License

ตัวถอดรหัส Helix (RealNetworks, **RPSL 1.0**) คัดลอกแบบไม่แก้ไขจาก `earlephilhower/ESP8266Audio` 1.9.7 ไปไว้ที่ `lib/helix-*` พร้อมไฟล์ license
**ไม่ใช้** คลาสห่อของ ESP8266Audio (GPL-3.0) — จึงไม่ทำให้เฟิร์มแวร์ทั้งก้อนต้องเป็น GPL; โปรเจกต์ยังเป็น MIT

## เฟส 2 (ยังไม่ทำ)

- **Echo cancellation (AEC)**: ES8311 **มี** DAC loopback ในอีก slot (reg 0x44 = 0x50, ใช้แล้วใน v3.5.1 สำหรับการปลุก) → หักออกจากไมค์ด้วย ESP-SR AFE → แผนเต็มใน [[14-AEC-WakeWord-Plan]] (ลำโพงดังเกิน ~80 ทำให้ไมค์อิ่มตัว = non-linear)
- Wake word ในเครื่อง (ESP-SR WakeNet) — ไม่มีคำไทยสำเร็จรูป
