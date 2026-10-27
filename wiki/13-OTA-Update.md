---
tags: [ota, firmware, update, rollback, esp32-ai]
created: 2026-10-16
updated: 2026-10-16
status: done
---

# 13 — อัปเดตเฟิร์มแวร์ผ่าน Wi-Fi (OTA) จาก GitHub Releases

> **Code:** `src/system/OtaUpdater.*` (task `ota`) · `src/util/OtaLogic.h` (ส่วนที่ไม่ขึ้นกับฮาร์ดแวร์ — host tested) ·
> `tools/make_release.py` / `tools/publish_release.py` (ไฟล์ `-app.bin`) · เมนู "อัปเดตเฟิร์มแวร์" (`SettingsScreens.cpp`) · v3.5.0

## ทำอะไร
บอร์ดดาวน์โหลดเฟิร์มแวร์เวอร์ชันล่าสุดจากหน้า Releases ของ repo (`OTA_REPO` ใน `AppConfig.h`) แล้วติดตั้งเอง
**ค่าที่ตั้งไว้ (NVS), ความจำ, รายการวิทยุ และการ์ด SD ไม่ถูกแตะ** — เขียนเฉพาะช่องเฟิร์มแวร์อีกช่อง (`app0` ⇄ `app1`, ช่องละ 6.25 MB ใน `default_16MB.csv`)
ถ้าเวอร์ชันใหม่มีปัญหา (รีเซ็ตเอง ค้าง หรือดับก่อนยืนยัน) **bootloader กลับไปใช้เวอร์ชันเดิมเอง**

## ขั้นตอน
```
check    GET https://api.github.com/repos/<OTA_REPO>/releases/latest  (ไม่นับ draft / pre-release)
         → tag "vX.Y.Z" > FIRMWARE_VERSION ?  → asset "*-app.bin" + "*-app.bin.sha256" (+ "*-app.bin.sig" ถ้าเฟิร์มแวร์มีกุญแจ)
install  (ผู้ใช้กดเท่านั้น) ปิดวิทยุ + พัก agent (ปิด Live session ระหว่างโหลด)
         → โหลด .sha256 (+ .sig) → โหลด -app.bin (github.com → 302 → release-assets.githubusercontent.com)
         → เขียนลงอีกช่องทีละ 4 KB (esp_ota_begin(OTA_WITH_SEQUENTIAL_WRITES))
         → ตรวจ: header ESP32-S3 (magic 0xE9, chip id 9, app descriptor) • marker "PAB-ESP32-FW:<version>" = เวอร์ชันของ release
                 • sha256 = ไฟล์ .sha256 ที่ประกาศ • ลายเซ็น ECDSA P-256 ของ sha256 นั้นด้วยกุญแจ release (ถ้ามีกุญแจในเฟิร์มแวร์)
                 • esp_ota_end (ตรวจ segment + hash ในตัว image)
         → esp_ota_set_boot_partition → บันทึก NVS "ota" {try: เวอร์ชัน, part: ช่อง} → รีสตาร์ทผ่าน agent (ปิด session + flush ความจำ)
confirm  boot ใหม่ = PENDING_VERIFY → ทำงานปกติ 60 วิ และ agent loop ยังเต้นอยู่ (heartbeat) → esp_ota_mark_app_valid_cancel_rollback
         (T113: ทำใน `gOta.tick()` จาก loop ของ agent — task "ota" (stack ภายใน 10 KB) มีเฉพาะตอนเช็ก/ดาวน์โหลด แล้วลบตัวเอง)
         รีเซ็ตก่อนนั้น (crash / watchdog / ไฟดับ / ผู้ใช้) → bootloader ทำเครื่องหมาย ABORTED แล้วบูตช่องเดิม
report   boot ถัดไปเทียบช่องที่รันกับ NVS "part": ตรง = อัปเดตแล้ว ("อัปเดตเป็น vX เรียบร้อย") • ไม่ตรง = "อัปเดต vX ไม่สำเร็จ กลับมาใช้ vY แล้ว"
```
- **ไม่ติดตั้งเอง:** เช็กอัตโนมัติวันละครั้ง (เริ่ม 2 นาทีหลังบูต) แค่ขึ้นข้อความ "มีเฟิร์มแวร์ใหม่ vX • เมนูตั้งค่า > อัปเดตเฟิร์มแวร์"
- เวอร์ชันเทียบเป็นตัวเลข (`3.4.10 > 3.4.9`), tag ที่ไม่ใช่ตัวเลขล้วน (`3.5.0-rc1`) = ไม่อัปเดต
- **Arduino ทำเครื่องหมาย "ใช้ได้" ทันทีตอนบูต** ถ้าไม่ override `verifyRollbackLater()` → เรา override ให้ return `true` แล้วยืนยันเองหลัง 60 วิ
  (bootloader ของ Arduino-ESP32 2.0.17 เปิด `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` อยู่แล้ว)

## TLS
`OTA_ROOT_CA_PEM` (`tools/gen_root_ca.py`): api.github.com / github.com → **USERTrust ECC** (Sectigo E46 cross-sign) + USERTrust RSA / Sectigo R46 ·
release-assets.githubusercontent.com → **ISRG Root X1** (+ X2) — ตรวจครบทุกขั้น ไม่มี `setInsecure`
· **ช่วง connect + handshake รันที่ priority 4** (`ConnectPriority`, T121): ที่ priority 1 ของ task `ota` ตอนวิทยุเล่น handshake ใช้ 13–23 วิ
แล้ว GitHub ตัดทิ้ง (EOF / reset, "HTTP -1") · ทุก request พิมพ์ `[OTA] <host> (<ip>) -> <code> in <ms>` (+ ข้อความ mbedTLS ถ้าไม่มี HTTP status)

## วิธีใช้
| ที่ | ทำอะไร |
|---|---|
| เมนูตั้งค่า → **อัปเดตเฟิร์มแวร์** | เวอร์ชันปัจจุบัน • ตรวจหาเวอร์ชันใหม่ • ติดตั้ง vX (ยืนยันก่อน) • % ที่ดาวน์โหลด |
| Serial | `ota` (สถานะ `OTA {json}`) · `ota check` · `ota install` · `ota rollback` (บูตอีกช่อง) · `ota fetch <url>` (โหลดผ่าน TLS เดียวกัน แค่คำนวณ sha256) · `ota selftest` (คัดลอกเฟิร์มแวร์ที่รันไปอีกช่องแล้วบูต — ทดสอบสลับ/ยืนยัน/rollback โดยไม่ใช้เน็ต) |
| PC Studio แท็บ 🔧 เฟิร์มแวร์ | 🔍 ตรวจเวอร์ชันใหม่ · ⬇ ติดตั้งบนบอร์ด · ↩ กลับเวอร์ชันก่อน + สถานะ |

## ออก release ให้บอร์ดอัปเดตได้
`python tools/publish_release.py` แนบ 4 ไฟล์: `-factory.bin` (ติดตั้งใหม่ที่ 0x0, ล้างค่า) + `.sha256` และ **`-app.bin` (สำหรับ OTA) + `.sha256`**
(+ **`-app.bin.sig`** เมื่อมีกุญแจ release — หัวข้อถัดไป)
(+ **`PersonalAIBot-Mobile.apk`** + `.sha256` เมื่อ build แอปมือถือไว้แล้ว — [[17-Mobile-Flasher]]; แอปตรวจ `.sha256` และ `.sig` ชุดเดียวกันนี้ก่อนแฟลชผ่าน USB)

## ลายเซ็นของ release (T074)
sha256 มาจาก release เดียวกับไฟล์ จึงจับได้แค่ไฟล์เสียระหว่างโหลด — คนที่ยึดบัญชี GitHub ได้ก็ปล่อยเฟิร์มแวร์ปลอมพร้อม sha256 ที่ตรงได้
ลายเซ็นปิดช่องนี้: มีแต่เจ้าของกุญแจส่วนตัว (อยู่ใน PC ไม่ใช่ใน GitHub) ที่ทำได้
- **อัลกอริทึม:** ECDSA P-256 + SHA-256 (DER, base64 หนึ่งบรรทัด) — mbedtls ของ IDF 5.5 ไม่มี Ed25519 แต่ ECDSA เปิดอยู่แล้วสำหรับ TLS
  · ตรวจด้วย `ota::verifySignature` (`util/OtaVerify.h`, `mbedtls_pk_verify`) **ก่อน** `esp_ota_end` / สลับช่องบูต
- **ตั้งครั้งเดียว** (บน PC ที่ออก release): **ESP32-AI Studio แท็บ 🔧 เฟิร์มแวร์ → 🔑 สร้างกุญแจ** (T075 — ติดตั้ง `cryptography` ให้ถ้ายังไม่มี, ถามสำรองกุญแจต่อทันที)
  หรือ command line: `pip install cryptography` → `python tools/ota_keygen.py`
  → กุญแจส่วนตัว `~/.pab-esp32/ota_signing_key.pem` (สิทธิ์ 600; เปลี่ยนที่ด้วย `PAB_OTA_SIGNING_KEY`) + กุญแจสาธารณะลง `src/app/OtaPublicKey.h` → **commit header นั้น**
  · **สำรองกุญแจส่วนตัวไว้** (password manager / USB) — หายแล้วบอร์ดที่มีกุญแจสาธารณะนั้นอัปเดตผ่าน Wi-Fi ไม่ได้อีก (ต้องแฟลช USB)
  · clone ใหม่ / header ถูกล้าง: `python tools/ota_keygen.py --header` (หรือปุ่ม 🔑 อีกครั้ง) เขียน header จากกุญแจเดิม · ไม่ยอมสร้างกุญแจทับของเดิม
  · PC ใหม่: Studio **📂 นำกุญแจสำรองกลับมา...** (ต้องตรงกับ header) · **💾 สำรองกุญแจ...** ไม่ยอมบันทึกไว้ในโฟลเดอร์โปรเจกต์
- `tools/make_release.py` เซ็น `-app.bin` → `dist/...-app.bin.sig` (ตรวจซ้ำด้วย public key ก่อนเขียน) · header มีกุญแจแต่หากุญแจส่วนตัวไม่เจอ/ไม่ตรง = หยุด
  · header ว่าง = ไม่เซ็น และไม่เตือนแล้ว (v4.3.1, T084 — ลายเซ็นเป็นทางเลือก ค่าปกติคือไม่เซ็น) · `publish_release.py` แนบ `.sig` · GitHub Actions: ใส่ PEM ใน secret `OTA_SIGNING_KEY`
- **บนบอร์ด:** เฟิร์มแวร์ที่ build พร้อมกุญแจ → release ที่ไม่มี `.sig` = "ไม่มีลายเซ็น (.sig) - ไม่ติดตั้ง" · ลายเซ็นไม่ผ่าน =
  "ลายเซ็นไม่ถูกต้อง ... ไม่ติดตั้ง" (`[OTA] Signature check failed: bad signature`) · ผ่าน = `[OTA] Signature OK (release key)`
  · boot log บอกโหมด: `updates must be signed by the release key` / `no release key built in` · `ota` (JSON) มี `signed_updates`
- **ช่วงเปลี่ยน:** บอร์ดที่ยังไม่มีกุญแจ (≤ v4.1.0) รับเวอร์ชันแรกที่มีกุญแจได้ตามปกติ (ตรวจแค่ sha256) — นับจากนั้นต้องมีลายเซ็นทุกครั้ง
  · ย้อนเวอร์ชันด้วย release ปลอมไม่ได้: marker ในไฟล์ต้องตรงกับ tag และ tag ต้องใหม่กว่าเวอร์ชันที่รันอยู่
- **ทดสอบ (2026-10-18, บน PC):** ลายเซ็นจาก `make_release.sign_app` ตรวจด้วย mbedtls 3.6 + `OtaVerify.h` ตัวเดียวกับเฟิร์มแวร์: ของแท้ ผ่าน ·
  แก้ไฟล์ 1 บิต / แก้ลายเซ็น 1 บิต = `bad signature` · `pio run` ผ่านทั้งแบบมีกุญแจทดสอบและไม่มีกุญแจ — **ยังไม่ได้ทดสอบบนบอร์ด**
release ก่อน v3.5.0 ไม่มี `-app.bin` → บอร์ดบอก "ไม่มีไฟล์สำหรับอัปเดตผ่าน Wi-Fi" — บอร์ดที่ยังเป็น v3.4.x ต้องแฟลช USB ครั้งเดียวเพื่อได้ v3.5.0

## ทดสอบบนบอร์ด (2026-10-16)
- `ota check` → ต่อ api.github.com ผ่าน CA ชุดใหม่: "Up to date (latest release v3.4.4)"
- `ota fetch` ไฟล์ release v3.4.4 ผ่าน redirect: 1,893,136 bytes, sha256 `5fb956d3…` **ตรงกับที่ประกาศ** (33 KB/s ผ่าน hotspot มือถือ ≈ 1 นาทีต่อไฟล์)
- `ota selftest`: app0 → app1 บูตเป็น pending → ยืนยันที่ 60 วิ ("อัปเดตเป็น v3.5.0 เรียบร้อย") · selftest อีกครั้ง (app1 → app0) แล้ว `reboot` ที่ 12 วิ
  → **bootloader กลับไป app1** และขึ้น "อัปเดต … ไม่สำเร็จ กลับมาใช้ … แล้ว" · เขียน 1.8 MB ใช้ ~50 วิ (ลบ + เขียนแฟลชทีละ sector)
- หน้าเมนู "อัปเดตเฟิร์มแวร์" (ภาพจาก `screenshot`): เวอร์ชันปัจจุบัน 3.5.0 + "ตรวจหาเวอร์ชันใหม่ • จาก GitHub" แสดงครบ ไม่ล้น

**ทดสอบครบวงจรจาก release จริง (2026-10-16):** แฟลช USB ด้วย build ที่ตั้งเลข 3.4.99 → `ota check` = "Update available: v3.5.0 (1819 KB)"
→ `ota install`: agent พัก → ดาวน์โหลด 1,862,768 bytes จาก GitHub (18 KB/s ผ่าน hotspot, รวมตรวจ 104 วิ) → sha256 `5fc51307…` = ไฟล์ที่ประกาศ
→ รีสตาร์ทเข้า **app1** → Live พร้อมใน 3 วิ → ยืนยันที่ 60 วิ "อัปเดตเป็น v3.5.0 เรียบร้อย" · Wi-Fi / API key / ความจำอยู่ครบ (ต่อ session ได้ทันที)
· เจอข้อความไทยถูกตัดกลางตัวอักษร ("ห้ามปิดเครื่�") — บัฟเฟอร์ข้อความ 96/120 ไบต์ → 192 + `notice()` ตัดตามขอบ UTF-8 (แก้หลังออก v3.5.0 → มากับ release ถัดไป)

## v4.0.0: ตาราง partition ใหม่
v4.0.0 ใช้ `partitions_sr.csv` (app 5 MB × 2 + `model` 3.25 MB สำหรับคำปลุก ESP-SR) — **OTA เปลี่ยนตาราง partition ไม่ได้**:
บอร์ด v3.5.x ที่อัปเดตผ่าน Wi-Fi ได้ v4.0.0 (app 3.4 MB พอดี slot 6.25 MB เดิม) ทำงานได้ทุกอย่าง แต่ไม่มี partition `model`
→ `[Wake] No model partition` → ปลุกด้วยระดับเสียงแบบเดิม · ได้คำปลุกเมื่อแฟลชไฟล์ factory ผ่าน USB ครั้งเดียว (ค่าตั้งค่าใน NVS ถูกล้าง, ความจำบน SD อยู่ครบ)

**ทดสอบ 2026-10-17:** บอร์ดตาราง `partitions_sr.csv` (build 3.5.1) → `ota check` เจอ v4.0.0 → `ota install`: ดาวน์โหลด 3,441,120 ไบต์ 118 วิ, sha256 ตรง
→ รีสตาร์ทเข้า `app1` → คำปลุกพร้อม → ยืนยันตัวเองหลัง 60 วิ · ค่าที่ตั้งไว้ครบ (Wi-Fi, key, ชื่อ, คำปลุก 2 คำ) · ไฟล์ factory ตรวจทีละส่วนแล้ว (partitions/app/model ตรงตำแหน่ง)

**ทดสอบ v4.1.0 (2026-10-18):** บอร์ด build 4.0.0 (app0) → `ota check` = "Update available: v4.1.0 (3380 KB)" → `ota install`: 3,461,680 ไบต์ใน 109 วิ,
sha256 `a5f3c3d7…1529` ตรงกับ `-app.bin.sha256` ใน release → `rst:0xc (RTC_SW_CPU_RST)` → `Firmware 4.1.0 in app1 (pending verification)` →
Live พร้อมใน 6 วิ → 60 วิหลังบูต "อัปเดตเป็น v4.1.0 เรียบร้อย" · `ota` = `current 4.1.0, pending_verify false, app1` · ค่าตั้งค่าครบ (Wi-Fi, ชื่อ, โมเดล, ความลับบน SD) ·
RAM ภายในต่ำสุด 36 KB
- เฝ้า serial ข้ามการรีบูต: เปิด COM4 ด้วย DTR/RTS = low แล้ว**ไม่ปิด/เปิดพอร์ตใหม่** — USB ของ S3 อยู่รอดการรีเซ็ตแบบ software (`esp_restart`) log ต่อเนื่อง
  · ถ้าพอร์ตหลุด อย่าเปิดใหม่ก่อน ~75 วิ: การเปิดพอร์ตอาจรีเซ็ตบอร์ด และรีเซ็ตก่อนยืนยัน 60 วิ = bootloader rollback

## ข้อจำกัด / ต่อยอด
- ความน่าเชื่อถือของไฟล์ = บัญชี GitHub + TLS + sha256 ใน release เดียวกัน **จนกว่าจะรัน `tools/ota_keygen.py`** — จากนั้น = ลายเซ็นของกุญแจ release (T074)
- เปลี่ยนกุญแจ (rotation) ยังไม่รองรับ: บอร์ดรู้จักกุญแจเดียว — ต้องการเปลี่ยนต้องแฟลช USB หรือออก release ที่เซ็นด้วยกุญแจเก่าแต่ฝังกุญแจใหม่ (ทำมือ)
- `pio run -t upload` เขียน `boot_app0.bin` = กลับไปบูต app0 เสมอ (ช่อง app1 อาจยังมีเวอร์ชัน OTA เก่า — `ota rollback` จะบูตตัวนั้น)
- ยังไม่มีการอัปเดตด้วยเสียง (ตั้งใจ: ต้องกดยืนยันบนจอ / Studio / console)
