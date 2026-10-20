---
tags: [flash, android, usb, release, tools]
created: 2026-10-20
status: active
---

# 📱 แอปมือถือแฟลชเฟิร์มแวร์ — ESP32-AI Flasher (Android)

> **Related:** [[00-Home-Dashboard]] | [[02-Installation-and-Setup]] | [[13-OTA-Update]] | [[10-PC-Studio-App]] | [[07-Troubleshooting-Log#T078 แอปมือถือ Android แฟลชเฟิร์มแวร์ผ่านสาย USB (OTG)]]

แฟลช PersonalAIBot-ESP32 ลงบอร์ด ES3C28P จากมือถือ Android โดยเสียบสาย USB-C จากมือถือเข้าพอร์ต Type-C ของบอร์ด —
ไม่ต้องมีคอมพิวเตอร์ ไม่ต้องลงไดรเวอร์ ไม่ต้อง root

| | |
|---|---|
| โค้ด | `tools/android_flasher/` (Kotlin, ไม่มี AndroidX — APK ~0.7 MB) · package `io.github.skyliner2008.pabflasher` |
| มือถือ | Android 8.0+ (API 26) ที่รองรับ **USB OTG** · iPhone/iPad ใช้ไม่ได้ (iOS ไม่ให้แอปคุยกับพอร์ต USB serial) |
| สาย | USB-C ↔ USB-C (หรือ OTG adapter + สาย A↔C) ที่**ส่งข้อมูลได้** |
| Build | `python tools/build_flasher_apk.py` → `dist/ESP32-AI-Flasher.apk` · `publish_release.py` แนบไฟล์นี้ให้ release อัตโนมัติถ้ามี |

![[images/flasher_main.png]] ![[images/flasher_factory_confirm.png]]

*ภาพจาก Android emulator (API 30) — ในเครื่องที่ถ่าย GitHub ถูกดักด้วย proxy จึงขึ้นข้อความ "เชื่อมต่อแบบปลอดภัยไม่ได้"; บนมือถือจริงจะเป็น "เวอร์ชันล่าสุดบน GitHub: v…"*

## ใช้งาน
1. ติดตั้ง `ESP32-AI-Flasher.apk` (จาก GitHub Release หรือ build เอง) — Android จะถามให้อนุญาต "ติดตั้งแอปที่ไม่รู้จัก" ครั้งแรก
2. เปิดแอป → เสียบสายเข้าบอร์ด → ขึ้น "✔ พบบอร์ด ESP32 (USB 303A:1001)" (เสียบตอนแอปปิดอยู่ Android จะเสนอให้เปิดแอปนี้ —
   ติ๊ก "ใช้เป็นค่าเริ่มต้น" แล้วจะไม่ถามสิทธิ์ USB อีก)
3. เลือกเฟิร์มแวร์:
   - **อัปเดต** — ดาวน์โหลด `-app.bin` ของ release ล่าสุด เขียนที่ app0 (0x10000) + otadata = `boot_app0` (0xe000) → **การตั้งค่า Wi-Fi/API key และความจำอยู่ครบ**
   - **ติดตั้งใหม่ทั้งหมด** — `-factory.bin` ที่ 0x0 (bootloader, partition table, app, โมเดลคำปลุก) → **ล้างการตั้งค่า** (NVS 0x9000 ในไฟล์ factory เป็น 0xFF)
     · ความจำใน LittleFS (0xD50000) / การ์ด SD ไม่ถูกแตะ · แอปถามยืนยันก่อน · ใช้ครั้งแรก หรือเมื่อ partition เปลี่ยน (เช่น v3.x → v4)
   - **ไฟล์ .bin ในเครื่อง** — แยกชนิดจากเนื้อไฟล์ (มี partition table `AA 50` ที่ 0x8000 = factory, ขึ้นต้น `E9` = app)
4. กด **แฟลชลงบอร์ด** → แอปรีเซ็ตบอร์ดเข้าโหมดแฟลชเอง → เขียน → ตรวจ MD5 → รีสตาร์ต (ระหว่างนี้จอมือถือไม่ดับ)

ถ้าบอร์ดไม่เข้าโหมดแฟลช (เช่นเฟิร์มแวร์เก่าปิด USB): กด **BOOT** ค้าง แตะ **RESET** ปล่อย BOOT แล้วกดแฟลชอีกครั้ง ·
สายหลุดกลางทาง: เสียบใหม่แล้วแฟลชซ้ำ (ROM bootloader อยู่ในชิป ไม่มีทางเสีย)

## ความปลอดภัยของไฟล์
- ดาวน์โหลดผ่าน HTTPS จาก GitHub API (`/releases/latest`) → ตรวจ **sha256** กับไฟล์ `.sha256` ของ release ก่อนเขียนทุกครั้ง (ไฟล์ที่ผ่านแล้วเก็บใน cache ของแอป)
- ถ้า `src/app/OtaPublicKey.h` มีกุญแจ (wiki 13) ตอน build แอป → แอปฝังกุญแจสาธารณะเดียวกัน (`BuildConfig.OTA_PUBKEY_PEM`) และ**ตรวจลายเซ็น `-app.bin.sig`**
  แบบเดียวกับบอร์ด (ECDSA P-256 / SHA-256, DER base64) — ไม่มีลายเซ็นหรือลายเซ็นผิด = ไม่แฟลช · ไฟล์ factory ไม่มีลายเซ็น (ตรวจ sha256 อย่างเดียว)
- ก่อนแตะบอร์ด: ตรวจ magic `E9`, chip id ใน header ของ image = 9 (ESP32-S3), ขนาด app ≤ 5 MB, factory ต้องไม่ยาวเกิน 0xD50000 (ไม่ทับความจำ)
- หลังเข้าโหมดแฟลช: ถามชนิดชิปจาก ROM (`GET_SECURITY_INFO`, chip id 9) — ชิปอื่นหยุดก่อนลบอะไร

## ทำงานอย่างไร (สำหรับแก้โค้ด)
```
MainActivity ── ดาวน์โหลด + ตรวจ (Net, Releases) ── FlashJob ── EspLoader ── SerialPort ── UsbCdcPort (Android USB host)
                                                  └ FlashPlan (ตำแหน่ง/ตรวจไฟล์)        └ Slip/Esp (โปรโตคอล)
```
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
python tools/build_flasher_apk.py --create-keystore   # ครั้งเดียว: ~/.pab-esp32/flasher-release.jks + flasher-keystore.properties (สำรองไว้!)
python tools/build_flasher_apk.py                     # unit tests + APK release → dist/ESP32-AI-Flasher.apk (+ .sha256)
python tools/build_flasher_apk.py --debug             # APK debug ไม่ต้องมี keystore (ลงมือถือตัวเอง)
cd tools/android_flasher && ./gradlew testDebugUnitTest lintDebug
```
- ต้องมี JDK 17+ และ Android SDK (ลง Android Studio ได้ครบ) — หา SDK จาก `ANDROID_HOME` / `local.properties` / `%LOCALAPPDATA%\Android\Sdk`
- keystore อยู่**นอก repo** เหมือนกุญแจ OTA (สร้างเมื่อผู้ใช้สั่งเท่านั้น) · APK รุ่นใหม่ลงทับได้เฉพาะเมื่อเซ็นด้วยกุญแจเดิม — หายแล้วต้องถอนแอปก่อนลงใหม่
- **unit tests 24 ข้อ (JVM):** `FakeRom` จำลองบอร์ดทั้งตัว — แอปเดิมที่ไม่สนใจโปรโตคอลจนกว่าจะได้ลำดับ DTR/RTS, ROM loader ที่ตรวจ checksum/ลำดับบล็อก/ขนาด
  พารามิเตอร์, แตก zlib ลง flash เสมือน 16 MB, ตอบ MD5 จากเนื้อจริง → ทดสอบว่าไฟล์ลงครบทุกไบต์, อัปเดตไม่แตะ NVS/LittleFS, บล็อกถูกปฏิเสธแล้วส่งซ้ำ,
  บอร์ดหลุดจาก USB ตอนรีเซ็ต, เข้าโหมดแฟลชด้วยมือ, บอร์ดไม่ยอมเข้าโหมด (ไม่แกล้งบอกว่าสำเร็จ), ชิปอื่นถูกปฏิเสธก่อนลบ, ลายเซ็น OTA
  · `REAL_RELEASE_DIR=<โฟลเดอร์ที่มี app.bin + factory.bin>` ตรวจกับไฟล์ release จริงด้วย (v4.2.0 ผ่าน: otadata ของแอปตรงกับ boot_app0 ในไฟล์ factory ทุกไบต์)

## ข้อจำกัด / ยังไม่ได้ทดสอบ
- **ยังไม่ได้ลองกับบอร์ดจริง + มือถือจริง** (หน้าจอ + ปุ่ม + กล่องยืนยันลองบน emulator แล้ว) — โปรโตคอลทดสอบกับตัวจำลองเท่านั้น (ค่าคงที่เทียบกับ esptool) · มือถือบางรุ่นจ่ายไฟ OTG ไม่พอ/ปิด OTG ไว้ในการตั้งค่า
- รองรับเฉพาะพอร์ต USB ของ ESP32-S3 เอง (VID 303A) — บอร์ดที่ใช้ชิป CH340/CP210x ต่อ UART ไม่รองรับ (ต้องใช้ไดรเวอร์ของชิปนั้น)
- ความเร็ว: ROM loader รับทีละ 1 KB และตอบทุกบล็อก — ไฟล์ factory 11 MB (บีบอัดแล้วเล็กลงมาก) คาดว่า 1–3 นาที
