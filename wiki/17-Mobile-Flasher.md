---
tags: [flash, android, usb, release, tools]
created: 2026-10-20
status: active
---

# 📱 แอปมือถือ — Personal AI Bot (Android): ผู้ช่วย Jarvis + หน้าบอร์ด ESP32 (แฟลช + คอนโซลแบบ Studio)

> **Related:** [[00-Home-Dashboard]] | [[02-Installation-and-Setup]] | [[13-OTA-Update]] | [[10-PC-Studio-App]] | [[07-Troubleshooting-Log#T080 แอปมือถือ Android แฟลชเฟิร์มแวร์ผ่านสาย USB (OTG)]] | [[07-Troubleshooting-Log#T081 แอปมือถือ — สถานะ, log, ตั้งค่า, แชท แบบ ESP32-AI Studio]]

> **ตั้งแต่ 2026-10-23 (T088):** แอปมือถือของ repo นี้คือ **แอป Jarvis จากโปรเจค PersonalAIBot (`composeApp`)** ที่ย้ายมาไว้ใน
> `tools/mobile_app/` แล้วเติมหน้า **"บอร์ด ESP32"** (ไอคอนชิป 🔲 ที่แถบบนของ Jarvis) — หน้านี้คือแอป ESP32-AI Flasher เดิมทั้งชุด
> (แท็บบอร์ด / Log / ตั้งค่า / นำทาง / แฟลช ตามตารางด้านล่าง) ทำงานเหมือนเดิมทุกอย่าง · เสียบบอร์ดตอนแอปปิดอยู่ Android จะเสนอเปิดหน้านี้ตรงๆ
>
> | โมดูล | คืออะไร |
> |---|---|
> | `tools/mobile_app/composeApp` | แอป Jarvis (Kotlin Multiplatform + Compose): แชท/Live เสียง Gemini, ความจำ, tools, avatar Godot, automation ฯลฯ — package `com.skyliner2008.jarvis` (applicationId เดิมของ Jarvis → ติดตั้งทับแอป Jarvis เดิมได้ ข้อมูลอยู่ครบ ถ้าเซ็นด้วยกุญแจเดียวกัน) |
> | `tools/mobile_app/esp32` | Android library = ESP32-AI Flasher เดิม (โค้ด + test ไม่เปลี่ยน, package `io.github.skyliner2008.pabflasher`) เปิดด้วย `pabflasher.MainActivity` |
>
> **T182 — แอปรุ่น lite (2.2.0): ติดตั้งจากไฟล์ได้โดย Play Protect ไม่บล็อก + คำปลุกปล่อยไมค์ให้แอปกล้อง**
> - **อาการ:** ติดตั้ง APK v4.9.1 ที่ดาวน์โหลดจาก GitHub ผ่าน Chrome แล้วขึ้น "Google Play Protect — แอปถูกบล็อกเพื่อปกป้องอุปกรณ์ของคุณ
>   … ขอสิทธิ์เข้าถึงข้อมูลที่ละเอียดอ่อน" → "ไม่ได้ติดตั้งแอป"
> - **สาเหตุ:** Play Protect "enhanced fraud protection" (เปิดในไทยแล้ว) บล็อก APK จากเบราว์เซอร์ / แอปแชท / ตัวจัดการไฟล์ที่ประกาศ
>   RECEIVE_SMS, READ_SMS, **notification listener** หรือ **Accessibility service** — แอปมี 3 ตัว: `JarvisAccessibilityService`
>   (ควบคุมแอปอื่น), `JarvisNotificationListener` (อ่านแจ้งเตือน LINE/SMS + เพลงที่เล่น), `MapsListener` ของหน้าบอร์ด (จุดเลี้ยว Google Maps)
> - **ตัดออก (ผู้ใช้สั่ง "ให้เหลือเฉพาะโหมดหุ่น 3D avatar"):** ทั้ง 3 service + **โหมดขับขี่ / โหมดควบคุม** (หน้า `DriveModeScreen`,
>   `DriveBridge`, โปรไฟล์ `DRIVE`, จุดจอดรถ, โหมดลดแสง) + tool ที่ต้องใช้มัน 11 ตัว (`device_read_screen`, `_screenshot`, `_tap`,
>   `_gesture`, `_type_text`, `_scroll`, `_press_button`, `_get_app_info`, `_open_app`, `_notification_read`, `_notification_reply`)
>   + `media_control(now_playing)` · Always Live = **โหมดอวตารอย่างเดียว** (คำสั่ง "โหมดขับขี่ / ควบคุม" เปิดอวตารแทนและบอกว่าตัดออกแล้ว)
>   · เล่น/หยุด/ข้ามเพลงยังได้ (ปุ่มมีเดียของระบบ) · หน้าบอร์ด: ส่ง GPS + "นำทางไป …" (เปิด Google Maps) ยังได้ แต่**ไม่ส่งจุดเลี้ยวให้บอร์ดแล้ว** ([[18-Phone-Link-Navigation-Plan]])
> - **ตรวจได้:** `aapt dump xmltree dist/PersonalAIBot-Mobile.apk AndroidManifest.xml | grep BIND_` ต้องว่าง
> - **คำปลุกแย่งไมค์ (`MicSharing`):** เดิมตัวฟัง "Hey Jarvis" ถือไมค์ตลอดเวลาที่ไม่มี Live → แอปกล้องอัดวิดีโอไม่ได้ · ตอนนี้
>   (1) แอปอื่นเปิดกล้องขณะแอปเราไม่อยู่หน้าจอ (`CameraManager.AvailabilityCallback`, ไฟฉายไม่นับ) → หยุดฟังทันทีก่อนกดอัด และ Live ที่ค้างในพื้นหลังปิดเอง
>   (2) แอปอื่นอัดเสียง (`AudioRecordingCallback`; session ของแอปเราเอง `OwnMic` และ hotword ของระบบไม่นับ) → หยุดฟัง ·
>   ทั้งสองหมดแล้วฟังต่อเอง (1 วิ) · logcat `WakeWord`: `mic sharing: another app opened camera …`, `… the wake word gives the mic to it`,
>   `another app opened the camera - closing the background Live session to free the mic`
>
> **T091 — คำปลุกในมือถือ "Hey Jarvis"** (openWakeWord บน ONNX Runtime ในเครื่อง, แอป 2.1.0) — คำปลุกกับ Live ผลัดกันใช้ไมค์:
> - **ไม่มี Live เปิดอยู่** (จอเปิดหรือดับ, ไม่ต้องเปิด Always Live): ฟังคำปลุกด้วยไมค์ของตัวเอง (`WakeWordService`)
> - **"Hey Jarvis"** → เสียงชิป → เปิด Live (จอดับก็เปิดในพื้นหลัง) · Always Live ที่พักจออยู่ยังขึ้นจอเต็มเหมือนเดิม
> - **Live เปิดอยู่:** คำปลุกหยุดฟัง เสียงไปหา Gemini ตามปกติ (กดเปิด Live เองก็เหมือนกัน; โหมดประชุม/แปลภาษาก็ให้ไมค์เช่นกัน)
> - **ไม่มีการคุย** (AI ไม่ได้พูด, ผู้ใช้ไม่ได้พูดจบประโยค) ครบเวลาที่ตั้ง (1/2/5/10 นาที, ค่าเริ่ม 5) → **Live ปิดเอง** → กลับมาฟังคำปลุก
>   · Live ปิดเพราะเหตุอื่น (กดปิด, error) ก็กลับมาฟังเช่นกัน (`WakeWordController` เช็คทุก 5 วิ)
> - **คำปลุก 3 แบบ** (Settings → คำปลุก): **Hey Jarvis** (โมเดลสำเร็จรูป) · **+ เสียงฉัน** (ฝึก "Hey Jarvis" ด้วยเสียงผู้ใช้ 5 ครั้ง:
>   ปลุกเมื่อโมเดลเดิมผ่าน **หรือ** เสียงตรงกับที่ฝึก ขณะมีคนพูด — ไม่รอคะแนนโมเดลเดิม: "Hey Jarvis" ของผู้ใช้ตอนพูดปกติได้แค่ 0.00–0.08
>   แม้กับ openWakeWord ทางการ แต่ตรงกับเสียงที่ฝึกที่ระยะ 0.016–0.052 (เกณฑ์ ~0.08, ประโยคอื่น 0.21–0.25))
> - เสียงที่อัดตอนฝึก 5 ครั้งล่าสุด: `Android/data/com.skyliner2008.jarvis/files/wake_enrol/take_N.pcm` (16 kHz PCM16, `adb pull` ไปตรวจบน PC; ลบไปพร้อมแอป) · **คำของฉัน** (ตั้งคำเอง ภาษาไทยได้ เช่น "จาวิส" —
>   เทียบกับเสียงที่ฝึกอย่างเดียว ขณะมีคนพูด) · ปุ่ม **ฝึกเสียง** อัด 5 ครั้ง ครั้งละ 2.5 วิ (คำปลุกปล่อยไมค์ระหว่างอัด) · **ล้าง** ลบเสียงที่ฝึก
> - **เทียบเสียงที่ฝึก (`PersonalWake`):** เก็บ speech embedding ของ Google (96 ค่า/80 ms ที่ openWakeWord คำนวณอยู่แล้ว) ของแต่ละครั้ง →
>   เทียบเสียงสดด้วย DTW (พูดช้า/เร็วก็ตรงกัน) · เกณฑ์เรียนจากเสียงที่ฝึกเอง (ระยะของคู่ที่ห่างเป็นอันดับ 2 × 1.15; ความไวคูณเพิ่ม/ลด)
>   · เก็บใน Settings DB `wake_profile_jarvis` / `wake_profile_custom` · ไม่ต้องเทรนโมเดล ไม่ใช้เน็ต · จำเสียงคนที่ฝึกเป็นหลัก
> - **คะแนนเฉลี่ย (`WakeScorer`):** ตัวตรวจตัดเสียงเป็นช่วง 80 ms — "Hey Jarvis" เดียวกันเลื่อนไป 0–70 ms คะแนนแกว่ง 0.19…0.98 (ต้นเหตุ "เสียงเดิม บางครั้งติด")
>   → รัน 2 ตัวห่างกัน 40 ms แล้วเฉลี่ยคะแนนล่าสุดของทั้งคู่ (เกณฑ์ = ความไว × 0.8) → 0.47…0.99 บนเสียงเดียวกัน · เฉลี่ย 3 ค่าต่ำไป (0.32)
> - มือถือเชื่อมบอร์ด ESP32 อยู่ (USB / Wi-Fi) = มือถือไม่ตอบคำปลุก (บอร์ดตอบ "Jarvis" เอง)
> - ตั้งค่า: Settings → **คำปลุก (Hey Jarvis)** — เปิด/ปิด, แบบคำปลุก + ฝึกเสียง, ปิด Live เมื่อไม่มีการคุย, ความไว (0.35 / 0.5 / 0.65) · ปิดคำปลุก = Always Live ใช้แบบเดิม (เสียงดัง + ถอดเสียง)
> - **พูดคำไทย (เช่น "จาวิส") → ใช้ "คำของฉัน"** ไม่ใช่ "+ เสียงฉัน": โหมด 1 ยังฟังโมเดล "hey jarvis" อยู่ (คนอื่นพูด "Hey Jarvis" ก็ปลุก)
>   · หน้าฝึกโหมด 1 เตือนเมื่ออัด ≥ 3 ครั้งแล้วโมเดลเดิมได้ < 0.05 ทุกครั้ง (T093)
> - **ดู log:** Logcat กรอง tag `WakeWord` — `no Live session - listening`, `heard (mode …) in the last 5 s: average score … (need …), voice distance … (match <= …)`
>   (เฉพาะช่วงที่ได้ยินเสียงพูด — ห้องเงียบไม่ log, โหมด 2 ไม่แสดงคะแนนโมเดลเดิม; T093), `near miss`,
>   `wake word heard by the stock model / own recordings (mode …, average score …, voice distance …)`, `… takes the mic - wake word stops listening`, `no conversation for … min - closing the Live session`
> - **Logcat ทั้งแอปแบบไม่รก:** `package:com.skyliner2008.jarvis -tag~:^VRI\[|BLASTBufferQueue|SurfaceComposerClient|HWUI|ViewRootImpl|Insets|Ime|InputMethodManager|InputTransport|DecorView|SurfaceView|OpenGLRenderer|Choreographer|WindowOnBackDispatcher|NativeCustomFrequencyManager|ScrollIdentify|ProfileInstaller|nativeloader|CompatChangeReporter|^libc$|BufferQueueProducer|BBA2|Kumiho|^Dialog$|RemoteInpu|InteractionJankMonitor|ActivityThread|PhoneWindow|iner2008\.jarvis`
>   (หน้าบอร์ด ESP32 ไม่เขียน Logcat — ดูแท็บ Log ในแอป)
> - ยังไม่ได้ทำ: คำสั่งที่พูดต่อท้าย "Hey Jarvis" ทันที (ก่อน Live พร้อม ~1–2 วิ) ยังไม่ถูกส่ง — รอเสียงชิป/AI ทักก่อนแล้วค่อยพูด
> - โมเดล `assets/models/openwakeword/` (~3.7 MB, **CC BY-NC-SA 4.0** — ใช้แบบไม่ใช่เชิงพาณิชย์) · ตัว port Kotlin `OpenWakeWord.kt` ตรวจกับ
>   package Python ทางการบนเสียงเดียวกัน (`OpenWakeWordTest`: ทุก chunk ต่างไม่เกิน 0.05, "Hey Jarvis" ≈ 0.998, ประโยคอื่น < 0.002)
>
> **T090:** ปิดหน้าบอร์ด (✕ / back) ตอนต่อ USB อยู่ → แอปสลับไปส่ง GPS/การนำทางทาง Wi-Fi ให้เอง (เดิมส่งเข้าพอร์ตที่ปิดแล้ว) ·
> อยู่หน้า Jarvis ก็นับว่าแอปเปิดอยู่บนจอ (บอร์ดสั่งเปิด Google Maps ได้โดยไม่ต้องมีสิทธิ์ "แสดงทับแอปอื่น") · handshake Wi-Fi v2 (wiki 18)
> **T089:** หน้าบอร์ดใช้ธีมเดียวกับ Jarvis (พื้นมืด `#0A0A14`, การ์ด `#1C1C2E`, ฟ้า Cyan `#00E5FF`, หัวกลาง + ปุ่ม ✕ กลับ Jarvis) ·
> **ถอด MT5 ออกทั้งหมด** (เทอร์มินัล MT5, Auto Trading, tools `trading_mt5_*`, ตารางฐานข้อมูล MT5 — ลบทิ้งตอนอัปเกรด) —
> ข้อมูลการเงินในแอปมาจาก TradingView อย่างเดียว ตรงกับเฟิร์มแวร์ (ไม่มี MT5 ตั้งแต่ v3.4) ·
> ไฟล์ที่ git-ignore และต้องคัดลอกเองถ้าต้องการ: `composeApp/google-services.json` (Firebase/Vertex AI), `composeApp/libs/godot-lib.*.aar`
> (ไม่มี = Gradle ดึงจาก Maven), `composeApp/src/androidMain/godot-assets/` (หุ่น 3D ของ Avatar mode export จาก `pet_godot/` ของ repo PersonalAIBot;
> ไม่มี = ใช้หน้าหุ่น 2D) · APK release ~224 MB (Godot + โมเดล ML) · Android 8.0+

แฟลช PersonalAIBot-ESP32 ลงบอร์ด ES3C28P จากมือถือ Android โดยเสียบสาย USB-C จากมือถือเข้าพอร์ต Type-C ของบอร์ด —
ไม่ต้องมีคอมพิวเตอร์ ไม่ต้องลงไดรเวอร์ ไม่ต้อง root — และ (T081) ใช้เป็น **ESP32-AI Studio ฉบับพกพา**: ดูสถานะ, log สด, ตั้งค่า, แชท
ผ่านสายเส้นเดียวกัน

| แท็บ | ทำอะไรได้ (คำสั่ง console เดียวกับ Studio — [[10-PC-Studio-App#Protocol (serial console)]]) |
|---|---|
| 🤖 **บอร์ด** | การ์ดสถานะจาก `diag` ทุก 3 วิ (สถานะ, เซสชัน AI, Wi-Fi/IP, โมเดล, Search, ไมค์/พัก, วิทยุ, โทเคน/นาที, RAM, เวลาเปิด) — แตะการ์ด = อัปเดตทันที • ปุ่มด่วน 🎤 ไมค์ (`mute`) / 💤 พัก / ☀ ปลุก / ↻ ต่อ AI ใหม่ (`retry`) / 🆕 บทสนทนาใหม่ (`new`) / ⟲ รีบูต (ถามก่อน) • **แชท**: พิมพ์ถึงผู้ช่วย (`ask`) — คำตอบมาจาก log `[Chat] ai:` และพูดออกลำโพงบอร์ด |
| 📜 **Log** | log สดสีตามชนิด (กติกาเดียวกับ Studio: error แดง, fallback ส้ม, Gemini ฟ้าเขียว, แชท, Wi-Fi ม่วง ...) • กรองด้วยคำ • ซ่อน Touch/Health • แสดง/ซ่อนคำตอบ JSON • เลื่อนตาม • ล้าง • **แชร์ log** (ส่งเข้า LINE/อีเมล) — กดค้างบรรทัด = แชร์บรรทัดเดียว • ช่องพิมพ์คำสั่ง console + ↑ ประวัติคำสั่ง + help • เก็บล่าสุด 3,000 บรรทัด |
| ⚙️ **ตั้งค่า** | Wi-Fi (📶 สแกนจากบอร์ด → เลือก + รหัสผ่าน) • Gemini API key (บันทึก / 🔍 ทดสอบบนบอร์ด → ผลเป็นภาษาไทย) • ผู้ช่วย: ชื่อ, ชื่อผู้ใช้, โปรไฟล์สำเร็จรูป, เสียง, นิสัย, เพศ, สีดวงตา • โมเดล Live / ระดับการคิด / Text model, Google Search (+ ลองเปิดใหม่), จัดความจำ • ความดังวิทยุ / เสียง AI / ความสว่าง / ความไวไมค์ (ส่งทันทีเมื่อปล่อยนิ้ว), ปลุกด้วยเสียง, **คำปลุกของบอร์ด (1 จาก 9 คำ) + ความไวคำปลุก** (T145, ต้อง build APK ใหม่), พักเมื่อเงียบ, นาฬิกาหลังพัก, แสงจอตอนนาฬิกา, หมุนจอ • การเงิน (TradingView) • **บันทึกทั้งหมด** = ส่งเฉพาะค่าที่เปลี่ยน (โปรไฟล์ก่อน แบบ Studio) แล้วอ่านค่ากลับ |
| 🧭 **นำทาง** (T083) | ส่ง GPS + จุดเลี้ยวจาก Google Maps ให้บอร์ด (Wi-Fi / USB), บอร์ดสั่งเปิด/หยุด Maps ได้ · สิทธิ์ที่ต้องใช้ + ปุ่มไปหน้าตั้งค่า (Android 13+: dialog แนะนำปลดล็อค ⋮ การตั้งค่าที่ถูกจำกัด, T085) · ทดสอบ Maps · ข้อความดิบจาก Maps → [[18-Phone-Link-Navigation-Plan]] |
| ⬇️ **แฟลช** | ของเดิม (T080) — ระหว่างแฟลชแท็บอื่นหยุดเชื่อมต่อชั่วคราว แล้วต่อใหม่เองเมื่อบอร์ดรีสตาร์ต |

![[images/mobile_board.png]] ![[images/mobile_log.png]] ![[images/mobile_settings.png]] ![[images/mobile_settings_device.png]] ![[images/mobile_nav.png]]

*ภาพจาก Android emulator ต่อกับ "บอร์ดจำลอง" ที่ตอบคำสั่ง console ด้วยข้อมูลตัวอย่าง (ใช้ถ่ายภาพเท่านั้น ไม่อยู่ในแอป) — ภาพ Log: เลื่อนความดังวิทยุ
→ ส่ง `set volume 68` ทันที แล้วปิด Search + กดบันทึกทั้งหมด → ส่งแค่ `set search 0`*

- **จอทุกขนาด / Android 15 (T082):** เว้นขอบตามแถบสถานะ แถบนำทาง และคีย์บอร์ดของเครื่องเอง · ตอนพิมพ์ แถบแท็บหลบ และแท็บบอร์ดเหลือแชท + ช่องพิมพ์
  ![[images/mobile_board_typing.png]]
- **เชื่อมต่ออัตโนมัติ** เมื่อเสียบบอร์ดและเคยอนุญาต USB แล้ว (แถบบนสุด: สถานะ • fw • สถานะผู้ช่วย • Wi-Fi) · ปุ่ม "เชื่อมต่อ" ขอสิทธิ์ครั้งแรก
- **ไม่รีเซ็ตบอร์ด** ตอนเชื่อมต่อ: console ไม่แตะ DTR/RTS เลย (แบบเดียวกับ Studio ที่ตั้ง DTR/RTS = off) — มี test ยืนยัน
- **ความลับ:** รหัส Wi-Fi / API key เขียนอย่างเดียว — บอร์ดไม่เคยส่งกลับ (มีแค่ `keyHint`, `passSet`), log ในแอปแสดงคำสั่งเป็น `set key ***` / `set pass ***`,
  `key=` ใน URL ถูกซ่อน, แอปไม่บันทึกค่าเหล่านี้ลงมือถือ
- ยังไม่มี (มีใน Studio): จอเสมือน, รัน tool, อัดเสียงไมค์, จัดการวิทยุ, อัปเดต OTA ผ่าน Wi-Fi, ส่งออก/นำเข้าไฟล์ตั้งค่า

| | |
|---|---|
| โค้ด | `tools/mobile_app/esp32/` (Kotlin, ไม่มี AndroidX) · package `io.github.skyliner2008.pabflasher` — อยู่ในแอป `tools/mobile_app/composeApp` |
| มือถือ | Android 8.0+ (API 26) ที่รองรับ **USB OTG** · iPhone/iPad ใช้ไม่ได้ (iOS ไม่ให้แอปคุยกับพอร์ต USB serial) |
| สาย | USB-C ↔ USB-C (หรือ OTG adapter + สาย A↔C) ที่**ส่งข้อมูลได้** |
| Build | `python tools/build_mobile_apk.py` → `dist/PersonalAIBot-Mobile.apk` · `publish_release.py` แนบไฟล์นี้ให้ release อัตโนมัติถ้ามี — **ยกเว้น `--no-mobile-app`** (T196: ตั้งแต่ v4.9.6 release ไม่มี APK ใหญ่ / `.json` → ปุ่ม "อัปเดตแอป" ของแอปใหญ่จะไม่เจออัปเดต; ESP32-AI-Flasher.apk ยังแนบอยู่) |

![[images/flasher_main.png]] ![[images/flasher_factory_confirm.png]]

*ภาพจาก Android emulator (API 30) — ในเครื่องที่ถ่าย GitHub ถูกดักด้วย proxy จึงขึ้นข้อความ "เชื่อมต่อแบบปลอดภัยไม่ได้"; บนมือถือจริงจะเป็น "เวอร์ชันล่าสุดบน GitHub: v…"*

## ใช้งาน
1. ติดตั้ง `PersonalAIBot-Mobile.apk` (จาก GitHub Release หรือ build เอง) — Android จะถามให้อนุญาต "ติดตั้งแอปที่ไม่รู้จัก" ครั้งแรก
2. เปิดแอป → แตะไอคอนชิปที่แถบบน (หน้า "บอร์ด ESP32") → เสียบสายเข้าบอร์ด → ขึ้น "✔ พบบอร์ด ESP32 (USB 303A:1001)" (เสียบตอนแอปปิดอยู่ Android จะเสนอให้เปิดแอปนี้ —
   ติ๊ก "ใช้เป็นค่าเริ่มต้น" แล้วจะไม่ถามสิทธิ์ USB อีก)
3. เลือกเฟิร์มแวร์:
   - **อัปเดต** — ดาวน์โหลด `-app.bin` ของ release ล่าสุด เขียนที่ app0 (0x10000) + otadata = `boot_app0` (0xe000) → **การตั้งค่า Wi-Fi/API key และความจำอยู่ครบ**
   - **ติดตั้งใหม่ทั้งหมด** — `-factory.bin` ที่ 0x0 (bootloader, partition table, app, โมเดลคำปลุก) → **ล้างการตั้งค่า** (NVS 0x9000 ในไฟล์ factory เป็น 0xFF)
     · ความจำใน LittleFS (0xD50000) / การ์ด SD ไม่ถูกแตะ · แอปถามยืนยันก่อน · ใช้ครั้งแรก หรือเมื่อ partition เปลี่ยน (เช่น v3.x → v4)
   - **ไฟล์ .bin ในเครื่อง** — แยกชนิดจากเนื้อไฟล์ (มี partition table `AA 50` ที่ 0x8000 = factory, ขึ้นต้น `E9` = app)
4. กด **แฟลชลงบอร์ด** → แอปรีเซ็ตบอร์ดเข้าโหมดแฟลชเอง → เขียน → ตรวจ MD5 → รีสตาร์ต (ระหว่างนี้จอมือถือไม่ดับ)

ถ้าบอร์ดไม่เข้าโหมดแฟลช (เช่นเฟิร์มแวร์เก่าปิด USB): กด **BOOT** ค้าง แตะ **RESET** ปล่อย BOOT แล้วกดแฟลชอีกครั้ง ·
สายหลุดกลางทาง: เสียบใหม่แล้วแฟลชซ้ำ (ROM bootloader อยู่ในชิป ไม่มีทางเสีย)

### แอปอัปเดตตัวเอง (T097)

Jarvis → ตั้งค่า → **อัปเดตแอป** (การ์ดเปิดเองเมื่อพบเวอร์ชันใหม่) · ตรวจเองวันละครั้งตอนเปิดแอป → แจ้งเตือน "มีแอปเวอร์ชันใหม่"
1. `releases/latest` ของ GitHub → ไฟล์ `PersonalAIBot-Mobile.json` ที่ `build_mobile_apk.py` สร้าง + `publish_release.py` แนบ
   (`versionName`, `versionCode`, `sha256`, `size`, `signerSha256` = SHA-256 ของใบรับรองที่เซ็น จาก `apksigner`) — tag ของ release เป็นเลขเฟิร์มแวร์ จึงอ่านเวอร์ชันแอปจากไฟล์นี้
2. เทียบ `versionCode` กับที่ติดตั้ง และ**กุญแจเซ็นก่อนดาวน์โหลด**: เซ็นจาก PC คนละเครื่อง = บอกให้ถอนแอปก่อน (ไม่โหลด ~215 MB เปล่าๆ)
3. ขอสิทธิ์ "ติดตั้งแอปที่ไม่รู้จัก" ก่อนดาวน์โหลด → สตรีมลง `cache/app-update/` พร้อมคำนวณ sha256 + เทียบขนาด (ยกเลิกได้)
4. `PackageInstaller` session → Android ถามยืนยัน (`AppUpdateReceiver` เปิดหน้ายืนยัน / รายงานผล) · ไฟล์ในแคชลบหลังติดตั้งและตอนเปิดแอป
- release ก่อน v4.4.2 ไม่มีไฟล์ json → การ์ดบอก "ยังไม่มีข้อมูลเวอร์ชันแอป" · โค้ด: `ui/AppUpdate.kt` (ตัดสินใจ, test `AppUpdateLogicTest`),
  `update/AppSelfUpdate.kt` (ดาวน์โหลด/ติดตั้ง), `AppUpdateSection` ใน `SettingsDialog.kt` · log tag `AppUpdate`
- ตรวจบนมือถือแล้ว (SM-S908E): ก่อน v4.4.2 → `latest v4.4.1: no PersonalAIBot-Mobile.json` · หลังออก v4.4.2 →
  `latest v4.4.2: app 2.1.2 (6), installed 2.1.1 (5) -> Available(…, sameSigner=true)` + แจ้งเตือนช่อง `app_update` ขึ้น
  (กุญแจที่แอปคำนวณ = `signerSha256` จาก apksigner) · **ขั้นดาวน์โหลด + ติดตั้งยังไม่ได้ลอง** (ต้องกดยืนยันบนมือถือ)

### แอปเล็ก ESP32-AI Flasher + แฟลชได้ครบ 4 รุ่น (T195)

**ESP32-AI Flasher** (`tools/mobile_app/flasher`, ~0.8 MB) = หน้าบอร์ดของโมดูล `:esp32` เป็นแอปแยก สำหรับคนที่ไม่อยากลงแอปใหญ่ ~200 MB:
แท็บ บอร์ด (สถานะ + แชท) / Log / ตั้งค่า / แฟลช — **ไม่มี** Jarvis, แท็บนำทาง, GPS, NavService, phone link (`R.bool.esp32_lite` = true
→ `MainActivity.lite`) · สิทธิ์แค่ INTERNET + ACCESS_NETWORK_STATE (manifest ลบสิทธิ์ตำแหน่ง / overlay / foreground service ของ library ด้วย
`tools:node="remove"`) · applicationId `io.github.skyliner2008.pabflasher` เดิม (เวอร์ชัน 2.0.0 / code 10, `flasherVersion*` ใน gradle.properties)
แต่ Flasher 1.0.1 เดิมเซ็นด้วยกุญแจอื่น → ถอนแอปเดิมครั้งเดียว · build: `python tools/build_flasher_apk.py` → `dist/ESP32-AI-Flasher.apk`
(`publish_release.py` แนบทุก release) · ไม่เปิด R8 (โค้ดเหมือน library ที่ test แล้วทุกบรรทัด)

**แก้หน้าแฟลชให้รองรับบอร์ด mini (ทั้งแอปใหญ่และแอปเล็ก):** เดิม `Chip` มีแค่ S3 / P4 และข้อความหน้าแฟลชเขียนแต่ "ESP32-S3 / ESP32-P4"
(ผู้ใช้เสียบบอร์ด mini แล้วเห็นแต่ P4) — ที่อันตรายกว่า: XH-S3E-AI เป็น ESP32-S3 เหมือน ES3C28P แอปจะดาวน์โหลด `-factory.bin` ของ ES3C28P
ไปแฟลชลง XH-S3E-AI · Xmini-C3 (chip 5) ถูกปฏิเสธ
- `Chip` = **รุ่นบอร์ด** 4 ตัว (ชื่อเดิมเพื่อไม่ต้องแก้ทั้งโปรเจกต์): `S3` ES3C28P, `P4`, `XH_S3E_AI`, `XMINI_C3` + `configName` (`config` "chip") + `assetSuffix`
- เลือกรุ่น = ชิปจาก ROM + รุ่นที่เฟิร์มแวร์บอก (`config`) หรือที่ผู้ใช้เลือกในช่อง **"รุ่นบอร์ด"** (ใหม่ในหน้าแฟลช: อัตโนมัติ / 4 รุ่น)
  · ROM บอก S3 แต่ไม่รู้รุ่น → **ไม่เดา** ("ชิป ESP32-S3 มี 2 รุ่น … เลือกรุ่นบอร์ด") · เลือกรุ่นไม่ตรงกับที่เฟิร์มแวร์บอก / ชิปไม่ตรง → หยุดก่อนลบ
- ESP32-C3 ROM รุ่นแรกไม่ส่ง chip id ใน GET_SECURITY_INFO → อ่าน `CHIP_DETECT_MAGIC_REG` (0x40001000) แบบ esptool
- **อัปเดตต้องพอดีตารางพาร์ติชันในบอร์ด:** อ่าน MD5 ของ entry app0 (0x8040, 32 ไบต์) เทียบกับ 5 / 6 / 6.25 MB — P4 ที่ยังเป็น v4.9.3 มี app0 6 MB
  แต่ `-app-p4.bin` v4.9.4 ใหญ่ 6.30 MB (T193 ขยายพาร์ติชัน) → เดิมแอปเขียนทับเกินพื้นที่ (ไม่บูต) · ตอนนี้บอก "ใช้ ติดตั้งใหม่ทั้งหมด ครั้งเดียว"
  · บอร์ดเปล่า (ไม่มีตาราง) อัปเดตไม่ได้ ต้อง factory · `Chip.P4.appSize` = 0x640000
- กุญแจ OTA แยกต่อบอร์ด: `OTA_PUBKEY_PEM_XH` / `_C3` (จาก `firmware-s3-mini/`, `firmware-c3-mini/`)
- unit tests 70 (เพิ่ม: S3 สองรุ่น, รุ่นผิดชิป, C3 ROM เก่า, ตารางพาร์ติชันเก่า / บอร์ดเปล่า, ไฟล์ release ครบ 4 รุ่น) + ไฟล์ release v4.9.4 จริงทั้ง 4 บอร์ด
  (`REAL_RELEASE_DIR`, `_P4`, `_XH`, `_C3`: entry app0 ในไฟล์ factory ตรงกับที่แอปเทียบ)
- หน้าตั้งค่า: คีย์ `set` ทุกตัวที่แอปส่ง มีในเฟิร์มแวร์ทั้ง 4 บอร์ด (ตั้งค่าได้ทุกรุ่น)

| | ES3C28P | JC4880P443C | XH-S3E-AI | Xmini-C3 |
|---|---|---|---|---|
| ชิป (id) | ESP32-S3 (9) | ESP32-P4 (18) | ESP32-S3 (9) | ESP32-C3 (5) |
| `config` "chip" | `esp32s3` | `esp32p4` | `xh-s3e-ai` | `xmini-c3` |
| bootloader / app0 | 0x0 / 5 MB | 0x2000 / 6.25 MB (v4.9.4+) | 0x0 / 5 MB | 0x0 / 5 MB |
| LittleFS | 0xD50000 | 0xF50000 | 0xD50000 | 0xA10000 (ไม่มี model) |
| ไฟล์ release | `-app.bin` | `-app-p4.bin` | `-app-xh-s3e-ai.bin` | `-app-xmini-c3.bin` |

**หน้าตาสะอาดขึ้น (ผู้ใช้ขอ, ทั้งสองแอป):**
- แถบบน (เชื่อมต่อแล้ว) แสดง **แค่ชื่อรุ่นบอร์ด** เช่น "● XH-S3E-AI (ESP32-S3 OLED)" — สถานะ / Wi-Fi / เฟิร์มแวร์ อยู่ในแท็บบอร์ดแล้ว
- แท็บตั้งค่า: ปุ่ม "↻ อ่านค่า" / "💾 บันทึก" ด้านบน + **แท็บหมวด** (Wi-Fi, API key, ผู้ช่วย, โมเดล, เครื่อง, การเงิน — `drawable/chip.xml`) ทีละหมวด
  จำหมวดล่าสุดไว้ · ตัดคำอธิบายยาว (วิธีหา API key, ระดับไมค์, ความไวคำปลุก)
- แท็บแฟลช: บอร์ด (สถานะ + ช่องรุ่นในการ์ดเดียว) → เฟิร์มแวร์ "ล่าสุด v4.9.4" → 3 ตัวเลือกสั้นๆ → ปุ่มแฟลช · คำอธิบาย / รุ่นที่รองรับ /
  แก้ปัญหา ย้ายไปปุ่ม **❔ วิธีใช้** (popup)
- การ์ดเฟิร์มแวร์ **เทียบเวอร์ชันบอร์ด (`config` "fw") กับ GitHub**: "✔ ล่าสุดแล้ว" (การ์ด**เขียว**) / "⬆ มีเวอร์ชันใหม่ … (บอร์ด v…)" (การ์ด**แดง** `card_update.xml` + ตัวอักษรแดง) /
  "บอร์ดใหม่กว่า GitHub" / ไม่ได้เชื่อมต่อ = "เชื่อมต่อบอร์ดเพื่อเทียบเวอร์ชัน" · บอก "release นี้ไม่มีไฟล์ของ <รุ่น>" · "ตรวจเมื่อ HH:mm"
  (แตะแล้วเวลาเปลี่ยน = ตรวจใหม่จริง — เดิมแตะแล้วข้อความเหมือนเดิมจนดูเหมือนไม่ทำงาน) · `Releases.compareVersions` (เทียบเป็นตัวเลข, unit test)

**ทดสอบบนมือถือจริง (Galaxy S22 Ultra, Android 16):** ติดตั้ง + เปิดได้ · ตรวจเจอทั้ง 4 บอร์ด · **แฟลช XH-S3E-AI แบบอัปเดตผ่านมือถือสำเร็จ**
(แอปเลือก `-app-xh-s3e-ai.bin` v4.9.4 เอง, บอร์ดกลับมาเป็น fw 4.9.4) · หน้าใหม่ + popup ดูบนเครื่องจริงแล้ว · ยังไม่ได้แฟลช Xmini-C3 / P4 ด้วยมือถือ

### สองบอร์ด: ESP32-S3 + ESP32-P4 (T155)

| | ESP32-S3 (ES3C28P) | ESP32-P4 (JC4880P443C) |
|---|---|---|
| chip id (ROM + header ของ image) | 9 | 18 |
| bootloader | 0x0 | 0x2000 (ไฟล์ factory เริ่มที่ 0x0 ด้วย 0xFF 8 KB) |
| app0 / ขนาด | 0x10000 / 5 MB | 0x10000 / 6 MB |
| LittleFS (factory ห้ามยาวเกิน) | 0xD50000 | 0xF50000 |
| ไฟล์ใน release | `-app.bin` / `-factory.bin` | `-app-p4.bin` / `-factory-p4.bin` |
| กุญแจ OTA | `src/app/OtaPublicKey.h` | `firmware-p4/src/app/OtaPublicKey.h` (`BuildConfig.OTA_PUBKEY_PEM_P4`) |

- โหมด **อัปเดต / ติดตั้งใหม่ทั้งหมด**: เข้าโหมดแฟลช → ถามชิปจาก ROM → **ดาวน์โหลดไฟล์ของบอร์ดนั้น** (ROM รอระหว่างดาวน์โหลด) · ROM ไม่บอก = ใช้ `chip` จาก `config` ของเฟิร์มแวร์
- ไฟล์ในเครื่อง / ไฟล์ release: header ของ image ต้องเป็นชิปเดียวกับบอร์ด — "บอร์ดนี้เป็น ESP32-P4 แต่ไฟล์เป็นของ ESP32-S3" หยุดก่อนลบอะไร (unit test ทั้งสองทาง)
- แท็บนำทาง: บอร์ด P4 ไม่มีระบบนำทาง (T151) → ขึ้นคำเตือน และแอปไม่ส่ง GPS/การนำทางทาง USB ให้ P4
- release ก่อน v4.9.1 มีแต่ไฟล์ S3 → P4 ขึ้น "Release … ไม่มีไฟล์ -app-p4.bin สำหรับ ESP32-P4" (ใช้ไฟล์ .bin ในเครื่องได้)

## ความปลอดภัยของไฟล์
- ดาวน์โหลดผ่าน HTTPS จาก GitHub API (`/releases/latest`) → ตรวจ **sha256** กับไฟล์ `.sha256` ของ release ก่อนเขียนทุกครั้ง (ไฟล์ที่ผ่านแล้วเก็บใน cache ของแอป)
- ถ้า `src/app/OtaPublicKey.h` มีกุญแจ (wiki 13) ตอน build แอป → แอปฝังกุญแจสาธารณะเดียวกัน (`BuildConfig.OTA_PUBKEY_PEM`) และ**ตรวจลายเซ็น `-app.bin.sig`**
  แบบเดียวกับบอร์ด (ECDSA P-256 / SHA-256, DER base64) — ไม่มีลายเซ็นหรือลายเซ็นผิด = ไม่แฟลช · ไฟล์ factory ไม่มีลายเซ็น (ตรวจ sha256 อย่างเดียว)
- ก่อนแตะบอร์ด: ตรวจ magic `E9`, chip id ใน header ของ image = 9 (ESP32-S3) หรือ 18 (ESP32-P4), ขนาด app ≤ partition ของชิปนั้น (5 / 6 MB),
  factory ต้องไม่ยาวเกิน LittleFS ของชิปนั้น (0xD50000 / 0xF50000 — ไม่ทับความจำ)
- หลังเข้าโหมดแฟลช: ถามชนิดชิปจาก ROM (`GET_SECURITY_INFO`) — ชิปอื่น หรือไฟล์ของอีกบอร์ด หยุดก่อนลบอะไร

## ทำงานอย่างไร (สำหรับแก้โค้ด)
```
MainActivity (แท็บ + สิทธิ์ USB + เจ้าของพอร์ต)
 ├ FlashPage ── ดาวน์โหลด + ตรวจ (Net, Releases) ── FlashJob ── EspLoader ── SerialPort ── UsbCdcPort (Android USB host)
 │                                              └ FlashPlan (ตำแหน่ง/ตรวจไฟล์)       └ Slip/Esp (โปรโตคอล ROM)
 └ BoardLink (thread อ่าน) ── LineStream (แยกบรรทัด, ข้ามไบนารี SCREENSHOT/RECORD) ── Console (TAG, สี, ซ่อนความลับ)
      ├ BoardPage (diag + ปุ่มด่วน + แชท)   ├ LogPage   └ SettingsPage ── BoardOptions / SettingsForm (ค่าที่ต้อง `set`)
```
- พอร์ต USB มีเจ้าของได้ทีละคน: ก่อนแฟลช `suspendLink()` ปิด console (รอ thread อ่านจบ) แล้วค่อยเปิดพอร์ตให้ FlashJob ·
  แฟลชเสร็จ `resumeLink()` ลองต่อใหม่ทุกวินาที 2–10 วิ (บอร์ดรีสตาร์ตแล้ว enumerate ใหม่) · สายหลุด = ลองต่อใหม่ 8 ครั้ง ทุก 1.5 วิ
- **ไม่ใช้ไดรเวอร์/ไลบรารีภายนอก:** พอร์ต USB ของ S3 (USB-Serial-JTAG) เป็น CDC-ACM → claim interface comm + data แล้วใช้ bulk endpoint ตรงๆ
  · DTR/RTS = `SET_CONTROL_LINE_STATE` (0x22)
- **โปรโตคอล ROM ของ ESP32-S3** (เขียนจากเอกสาร serial protocol ของ Espressif, เทียบค่ากับ esptool 5.6): SLIP, `SYNC` → `GET_SECURITY_INFO` →
  `SPI_ATTACH` (8 ไบต์) → `SPI_SET_PARAMS` (16 MB) → ต่อ region: `FLASH_DEFL_BEGIN` (5 words รวม flag encrypted, erase ปัดขึ้นทีละ 1 KB) →
  `FLASH_DEFL_DATA` บล็อกละ 1 KB (zlib level 9, checksum XOR 0xEF, ส่งซ้ำได้ 3 ครั้ง) → `SPI_FLASH_MD5` เทียบกับ MD5 ในเครื่อง
  · ไม่อัปโหลด stub (ROM ของ S3 รับข้อมูลบีบอัดได้เอง) · ไม่ส่ง `FLASH_DEFL_END` (ROM จะออกไปรันโค้ดทันที — แบบเดียวกับ esptool)
- **รีเซ็ต:** ลำดับ `usb_jtag_bootloader_reset` ของ esptool — (DTR,RTS) = (0,0) → (1,0) GPIO0 ต่ำ → (1,1) → (0,1) รีเซ็ต → (0,0) ปล่อย
  · จบงาน: RTS 200 ms (`hard_reset`) → บูตเฟิร์มแวร์ใหม่ · ถ้าบอร์ดหลุดจาก USB ตอนรีเซ็ตแล้วกลับมา แอปรอ/ขอสิทธิ์ใหม่แล้ว sync ต่อโดยไม่รีเซ็ตซ้ำ
- **timeout ตาม esptool:** ลบ 40 วิ/MB, MD5 8 วิ/MB, ขั้นต่ำ 3 วิ · timeout ของแต่ละบล็อกคิดจากขนาด**หลังแตก**ของบล็อกนั้น
  (บล็อก 1 KB ของช่องว่าง 0xFF ในไฟล์ factory แตกได้เกือบ 1 MB — ถ้าใช้อัตราบีบอัดเฉลี่ยจะ timeout ปลอม)
- **ทำไมไม่ทำเป็นเว็บ (Web Serial / WebUSB):** ไฟล์ release บน `release-assets.githubusercontent.com` ไม่ส่ง `Access-Control-Allow-Origin`
  (ตรวจแล้ว) → เว็บเพจโหลด .bin จาก GitHub เองไม่ได้ ต้องฝากไฟล์ไว้ที่ hosting แยกทุก release (GitHub Pages ก็อาจติด billing เหมือน Actions)
  + บนมือถือต้องพึ่ง polyfill WebUSB และใช้ได้เฉพาะ Chrome · แอป native ไม่ติด CORS และขอสิทธิ์ USB เองได้

## Build / ทดสอบ
```bash
python tools/build_mobile_apk.py                      # unit tests (esp32) + APK release → dist/PersonalAIBot-Mobile.apk (+ .sha256)
python tools/build_mobile_apk.py --debug              # APK debug (debuggable สำหรับทดสอบ)
python tools/build_mobile_apk.py --create-keystore    # ทางเลือก: keystore ของตัวเอง ~/.pab-esp32/flasher-release.jks (สำรองไว้!)
cd tools/mobile_app && ./gradlew :esp32:testDebugUnitTest   # test ของหน้าบอร์ด ESP32 (64 ข้อ, T155)
cd tools/mobile_app && ./gradlew :composeApp:testDebugUnitTest   # test ของ Jarvis (บางชุดต่อเน็ตจริง)
```
- ต้องมี JDK 17–24 (Gradle 8.14 ดาวน์โหลด JDK 21 ที่ใช้ build เอง) และ Android SDK (ลง Android Studio ได้ครบ) — หา SDK จาก `ANDROID_HOME` / `local.properties` / `%LOCALAPPDATA%\Android\Sdk`
- **ไม่ต้องมี keystore (v4.3.1, T084):** ไม่มี keystore ของตัวเอง → APK release เซ็นด้วย debug key ของ Android SDK (`~/.android/debug.keystore`
  SDK สร้างให้เอง) · `publish_release.py` build APK ให้ทุก release เมื่อเครื่องมี Android SDK · APK รุ่นใหม่ลงทับได้เฉพาะเมื่อเซ็นด้วยกุญแจเดิม
  → build บน PC เครื่องเดิมก็อัปเดตทับได้ตลอด; ย้ายไป PC เครื่องอื่น**หรือลง Windows ใหม่** = ถอนแอปบนมือถือครั้งเดียวแล้วลงใหม่
  (หรือคัดลอก `~/.android/debug.keystore` เดิมกลับมา — T092)
- "OpenJDK Platform binary" ใน Task Manager = Gradle daemon (~2.8 GB) + Kotlin daemon — `gradle.properties` ตั้ง
  `org.gradle.daemon.idletimeout=900000` ให้ปิดเองหลังว่าง 15 นาที (ค่าเดิม 3 ชม.) · ปิดทันที: `cd tools/mobile_app && gradlew --stop` (T092)
- build ไม่มี warning Kotlin (T092): API ที่ deprecated ใช้ตัว compat ตามรุ่น Android (`recycleCompat`, `LocationManagerCompat`, `GeocodeListener`)
- keystore ของตัวเอง (ถ้าสร้าง) อยู่**นอก repo** (สร้างเมื่อผู้ใช้สั่งเท่านั้น)
- **unit tests 58 ข้อ (JVM)** — console (T081): แยก TAG/สี/แชท, ซ่อน `set key`/`set pass`/`key=`, บรรทัดถูกตัดกลางตัวอักษรไทย, ข้ามภาพ/เสียงไบนารี
  และ resync เมื่อมีข้อความแทรก, ค่าตั้งที่ต้องส่ง (เปลี่ยนเฉพาะที่แก้, โปรไฟล์ก่อน, ค่าเป็นบรรทัดเดียว), การ์ดสถานะ/ผลทดสอบ key ภาษาไทย,
  `BoardLink` อ่าน/เขียนจริงผ่าน thread และ**ไม่แตะ DTR/RTS** · แฟลช (T080): `FakeRom` จำลองบอร์ดทั้งตัว — แอปเดิมที่ไม่สนใจโปรโตคอลจนกว่าจะได้ลำดับ DTR/RTS, ROM loader ที่ตรวจ checksum/ลำดับบล็อก/ขนาด
  พารามิเตอร์, แตก zlib ลง flash เสมือน 16 MB, ตอบ MD5 จากเนื้อจริง → ทดสอบว่าไฟล์ลงครบทุกไบต์, อัปเดตไม่แตะ NVS/LittleFS, บล็อกถูกปฏิเสธแล้วส่งซ้ำ,
  บอร์ดหลุดจาก USB ตอนรีเซ็ต, เข้าโหมดแฟลชด้วยมือ, บอร์ดไม่ยอมเข้าโหมด (ไม่แกล้งบอกว่าสำเร็จ), ชิปอื่นถูกปฏิเสธก่อนลบ, ลายเซ็น OTA
  · `REAL_RELEASE_DIR=<โฟลเดอร์ที่มี app.bin + factory.bin>` ตรวจกับไฟล์ release จริงด้วย (v4.2.0 ผ่าน: otadata ของแอปตรงกับ boot_app0 ในไฟล์ factory ทุกไบต์)
  · `REAL_RELEASE_DIR_P4=…` แบบเดียวกันสำหรับ P4 (T155: ไฟล์ v4.9.0 จาก `make_release.py --board p4` ผ่าน)

## ข้อจำกัด / ยังไม่ได้ทดสอบ
- ✅ ทดสอบบนบอร์ดจริง 2026-10-20 (มือถือ Android จริง, debug APK): โหมด "อัปเดต" → พบบอร์ด 303A:1001, ดึง v4.2.0 จาก GitHub, sha256 ✓, รีเซ็ตเข้าโหมดแฟลชเองได้ (ไม่ต้องกด BOOT), เขียน otadata + app 3,525,840 ไบต์, MD5 ตรง, รีสตาร์ต — ทั้งหมดไม่ถึง 4 นาทีรวมดาวน์โหลด
- ยังไม่ได้ลองกับบอร์ดจริง: โหมด "ติดตั้งใหม่ทั้งหมด" (factory) และไฟล์ .bin ในเครื่อง
- ยังไม่ได้แฟลชบอร์ด ESP32-P4 จากมือถือจริง (T155: ทดสอบด้วย `FakeRom` + ไฟล์ P4 จริง) — ROM ของ P4 ใช้คำสั่งชุดเดียวกับ S3 (esptool ไม่ใช้ stub ก็แฟลชได้)
- รองรับเฉพาะพอร์ต USB ของชิปเอง (VID 303A — S3 และ P4 เป็น 303A:1001 ทั้งคู่) — บอร์ดที่ใช้ชิป CH340/CP210x ต่อ UART ไม่รองรับ (ต้องใช้ไดรเวอร์ของชิปนั้น)
- ความเร็ว: ROM loader รับทีละ 1 KB และตอบทุกบล็อก — ไฟล์ factory 11 MB (บีบอัดแล้วเล็กลงมาก) คาดว่า 1–3 นาที
