---
tags: [radio, audio, streaming, helix, mp3, aac, ducking, vad]
created: 2026-10-15
updated: 2026-10-15
status: implemented (phase 1) — tested on hardware 2026-10-15 (COOL Fahrenheit 93)
---

# 📻 Internet Radio

> **Related:** [[06-Avatar-and-Thai-UI]] | [[09-Assistant-Profiles-and-Tools]] | [[11-Data-Tools-and-Cards]] | [[04-System-Architecture]]
> **Code:** `src/audio/RadioPlayer.*` (stream + decode) · `src/util/RadioResampler.h` (→ mono 24 kHz) · `src/audio/AudioManager.*` (mixer + ducking) · `src/agent/WebToolsExtra.cpp` (`runRadio`) · `src/agent/AgentController.cpp` (`startRadio`, `handleRadio`) · `lib/helix-mp3`, `lib/helix-aac`

## ปัญหาที่ต้องแก้ก่อนเพิ่มวิทยุ

ไมค์กับลำโพงอยู่บนบอร์ดเดียวกัน → ไมค์ได้ยินวิทยุเต็มๆ:
- VAD ในเครื่องและ Gemini แยกเสียงผู้ประกาศ/นักร้องกับเสียงผู้ใช้ไม่ออก → AI จะตอบวิทยุ
- ถ้าส่งไมค์ขึ้น Gemini ตลอดเวลาที่เปิดวิทยุ → โทเคนหมด (ลิมิต Live 65k/นาที)
- การแยกเสียงพูดสองแหล่งแบบอัจฉริยะ (source separation) หนักเกิน ESP32

## การออกแบบ (เฟส 1 — ผู้ใช้เลือก: "แตะ/พูดดังเพื่อสั่ง" + "ลดเสียงเบาๆ ตอน AI พูด")

```
"เปิดวิทยุ Cool 93" → play_radio (Radio Browser) → RadioPlayer.play(url)
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
  - เดิมใช้ noise floor × 2: ตื่นเองทุก ~1.5 วิหลังเข้าพัก (noise floor ยังเป็นค่าตอนวิทยุเบา 12%) → แบบใหม่ 55 วิ ไม่ตื่นเองเลย
- ระหว่างวิทยุเล่น หยุดการเลือกช่องไมค์ (L/R) ใหม่ — ไม่งั้นอาจไปเลือกช่องที่เป็น DAC loopback

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
| การ์ดสถานีค้างทับนาฬิกา, ชื่อยาวถูกตัด | — | ปิดการ์ดเมื่อเข้าโหมดนาฬิกา, การ์ดวิทยุโชว์ 8 วิ, ชื่อยาวใช้ฟอนต์เล็ก + "..." |

ผล: เล่นต่อเนื่อง ~2 นาที พร้อมเรียก Wikipedia/ภาพรวมตลาด/อากาศซ้อน — ไม่รีบูต, 44.1 kHz นิ่ง, บัฟเฟอร์เต็ม ~4 วิ, แตะ → ตื่น (วิทยุ 12%) → เงียบ 6 วิ → พัก (วิทยุ 100%)
ยังไม่ได้ทดสอบ: ผู้ใช้พูดแทรกเพลงเพื่อปลุก (ต้องคนจริง) — ถ้าปลุกยากให้ลด `RADIO_WAKE_FACTOR`, ตื่นเองบ่อยให้เพิ่ม

## Tools

| Tool | ทำอะไร |
|---|---|
| `play_radio(query?, country?)` | ค้น [Radio Browser](https://www.radio-browser.info) (`de1.api.radio-browser.info`, ฟรี ไม่ใช้ key): ชื่อ → แนวเพลง (tag) → ทั่วโลก; กรองเฉพาะ MP3/AAC ที่ใช้ได้, เรียงตามความนิยม; ไม่ระบุ query = สถานีล่าสุด (`cfg/radiourl`) • การ์ดชื่อสถานีบนจอ |
| `stop_radio()` | ปิดวิทยุ |
| `get_device_status` | มี `radio.station / now_playing` — ตอบ "เพลงอะไรอยู่" ได้ |

ปรับความดังวิทยุ = `set_volume` (ความดังรวมของลำโพง)

## Console / Studio

- `radio <url> [ชื่อ]` เล่นสตรีมตรงๆ · `radio stop` · `radio` = สถานะ (state, สถานี, เพลง, codec, Hz, buffered ms, error)
- `diag`: `radio`, `radio_station`, `radio_title`, `radio_codec`, `radio_rate`, `radio_error`
- Studio → 🧰 → รัน tool `play_radio` / `stop_radio`

## License

ตัวถอดรหัส Helix (RealNetworks, **RPSL 1.0**) คัดลอกแบบไม่แก้ไขจาก `earlephilhower/ESP8266Audio` 1.9.7 ไปไว้ที่ `lib/helix-*` พร้อมไฟล์ license
**ไม่ใช้** คลาสห่อของ ESP8266Audio (GPL-3.0) — จึงไม่ทำให้เฟิร์มแวร์ทั้งก้อนต้องเป็น GPL; โปรเจกต์ยังเป็น MIT

## เฟส 2 (ยังไม่ทำ)

- **Echo cancellation (AEC)**: บอร์ดรู้สัญญาณที่ส่งออกลำโพงอยู่แล้ว (และ ES8311 อาจมี DAC loopback ในอีก slot) → หักออกจากไมค์ด้วย ESP-SR AFE / speex → ปลุกด้วยเสียงได้แม่นขึ้นโดยไม่ต้องพูดดัง (ต้องทดลองบนบอร์ดจริง ลำโพงติดไมค์ทำให้เสียงเพี้ยนแบบ non-linear)
- Wake word ในเครื่อง (ESP-SR WakeNet) — ไม่มีคำไทยสำเร็จรูป
