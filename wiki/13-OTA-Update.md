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
         → tag "vX.Y.Z" > FIRMWARE_VERSION ?  → asset "*-app.bin" + "*-app.bin.sha256"
install  (ผู้ใช้กดเท่านั้น) ปิดวิทยุ + พัก agent (ปิด Live session ระหว่างโหลด)
         → โหลด .sha256 → โหลด -app.bin (github.com → 302 → release-assets.githubusercontent.com)
         → เขียนลงอีกช่องทีละ 4 KB (esp_ota_begin(OTA_WITH_SEQUENTIAL_WRITES))
         → ตรวจ: header ESP32-S3 (magic 0xE9, chip id 9, app descriptor) • marker "PAB-ESP32-FW:<version>" = เวอร์ชันของ release
                 • sha256 = ไฟล์ .sha256 ที่ประกาศ • esp_ota_end (ตรวจ segment + hash ในตัว image)
         → esp_ota_set_boot_partition → บันทึก NVS "ota" {try: เวอร์ชัน, part: ช่อง} → รีสตาร์ทผ่าน agent (ปิด session + flush ความจำ)
confirm  boot ใหม่ = PENDING_VERIFY → ทำงานปกติ 60 วิ และ agent loop ยังเต้นอยู่ (heartbeat) → esp_ota_mark_app_valid_cancel_rollback
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

## วิธีใช้
| ที่ | ทำอะไร |
|---|---|
| เมนูตั้งค่า → **อัปเดตเฟิร์มแวร์** | เวอร์ชันปัจจุบัน • ตรวจหาเวอร์ชันใหม่ • ติดตั้ง vX (ยืนยันก่อน) • % ที่ดาวน์โหลด |
| Serial | `ota` (สถานะ `OTA {json}`) · `ota check` · `ota install` · `ota rollback` (บูตอีกช่อง) · `ota fetch <url>` (โหลดผ่าน TLS เดียวกัน แค่คำนวณ sha256) · `ota selftest` (คัดลอกเฟิร์มแวร์ที่รันไปอีกช่องแล้วบูต — ทดสอบสลับ/ยืนยัน/rollback โดยไม่ใช้เน็ต) |
| PC Studio แท็บ 🔧 เฟิร์มแวร์ | 🔍 ตรวจเวอร์ชันใหม่ · ⬇ ติดตั้งบนบอร์ด · ↩ กลับเวอร์ชันก่อน + สถานะ |

## ออก release ให้บอร์ดอัปเดตได้
`python tools/publish_release.py` แนบ 4 ไฟล์: `-factory.bin` (ติดตั้งใหม่ที่ 0x0, ล้างค่า) + `.sha256` และ **`-app.bin` (สำหรับ OTA) + `.sha256`**
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

## ข้อจำกัด / ต่อยอด
- ความน่าเชื่อถือของไฟล์ = บัญชี GitHub + TLS + sha256 ใน release เดียวกัน (ยังไม่มีลายเซ็นแยก) → ต่อยอด: เซ็น Ed25519 แล้วฝัง public key ในเฟิร์มแวร์
- `pio run -t upload` เขียน `boot_app0.bin` = กลับไปบูต app0 เสมอ (ช่อง app1 อาจยังมีเวอร์ชัน OTA เก่า — `ota rollback` จะบูตตัวนั้น)
- ยังไม่มีการอัปเดตด้วยเสียง (ตั้งใจ: ต้องกดยืนยันบนจอ / Studio / console)
