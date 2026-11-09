---
tags: [research, esp32-p4, graphics, animation, 3d, ppa]
created: 2026-11-01
status: reference
---

# 🎞️ แอนิเมชัน 2D / 3D บน ESP32-P4 — ผลค้นคว้า

> **Related:** [[19-ESP32-P4-JC4880P443C]] (ไปป์ไลน์จอที่ใช้อยู่ — หัวข้อ T149) | [[06-Avatar-and-Thai-UI]] | [[00-Home-Dashboard]]

ผู้ใช้ (2026-11-01): "หาข้อมูล เชิงลึก เกี่ยวกับการทำ 2D, 3D motion animation บน esp32-p4" พร้อมข้อมูลเบื้องต้นที่หามาเอง (PPA, Lottie, Intellar Engine,
LVGL 60 FPS, a3d, tgx, ลูกโลก 3D / ray tracing) — หน้านี้คือผลตรวจข้อมูลนั้นกับแหล่งต้นทาง + ความหมายต่อบอร์ด JC4880P443C (480×800)

**สรุปสั้น:** P4 ไม่มี GPU — งานที่ลื่นจริงคือ *ภาพที่เตรียมไว้แล้ว* (JPEG ฮาร์ดแวร์ + PPA คัดลอก/แปลง) ไม่ใช่การวาดเวกเตอร์หรือ 3D สด
ไปป์ไลน์ของเรา (Godot render ล่วงหน้า → `robot.pak` → ถอด JPEG ฮาร์ดแวร์ → PPA → double buffer + vsync, ดวงตาวาดสด) จึงเป็นทางที่คุ้มที่สุดอยู่แล้ว (30 fps)

## 1. ตรวจข้อมูลที่ผู้ใช้หามา

| ข้อความเดิม | ผลตรวจ |
|---|---|
| PPA + 2D-DMA ทำ blend / หมุน / ย่อขยาย / เลเยอร์ | ✅ มีจริง — แต่ข้อจำกัดเยอะ (หัวข้อ 2) |
| Lottie "ไม่กินทรัพยากร CPU หลัก" | ❌ **ผิด** — ThorVG (ตัวเล่น Lottie ของ LVGL / `esp_lv_lottie_player`) วาดด้วย CPU ทั้งหมด · วิศวกร Espressif (ฟอรัม 2026-03): Lottie เกิน 25 fps ได้ยาก, การ parse ยังหนักเกินสำหรับ embedded · เคสจริง 800×480 บน P4 ได้ไม่ถึง 6 fps · ตัวอย่าง ThorVG ของ Espressif เอง 19–20 fps ที่ CPU 86 % |
| Intellar Engine: Blender 2D skeletal บน P4 | ❌ **ไม่พบหลักฐาน** — repo เป็นเฟิร์มแวร์ดวงตาจอกลมบน **ESP32-S3** ใช้ภาพแบบแถบ (strip) จาก LittleFS ไม่มี P4 / Blender / กระดูก |
| LVGL 1024×600 / 1280×800 ที่ 60 FPS "สบายๆ" | ⚠️ ไม่มีผลวัดที่เผยแพร่ · PPA ใน LVGL ยัง *experimental* · ผู้ใช้ในฟอรัม LVGL เปิด PPA แล้วภาพฉีก/เมนูเพี้ยน จนต้องปิด |
| a3d ของ "chegewara" | ⚠️ **ผู้เขียนผิด** — a3d เป็นของ **0015 (Eric Nam)**, Apache-2.0 · ตัวไลบรารีมีจริงและดี (หัวข้อ 3) |
| tgx multi-thread 30+ fps ที่ 1024×600@16bit | ⚠️ มาจากโพสต์เดียวใน esp32.com (โปรเจคที่ต่อยอด tgx ของ vindar: multi-thread, scene graph, Collada) — รายละเอียดอ่านไม่ได้ (หน้าเว็บมีด่าน bot-check, ไม่ข้าม) |
| ลูกโลก 3D / Ray Tracing | ⚠️ อ้างจาก YouTube ตรวจไม่ได้ · ray tracing แบบ real-time บน CPU 400 MHz ไม่สมจริง — น่าจะเป็นภาพนิ่งที่คำนวณนาน |

## 2. ข้อจำกัดฮาร์ดแวร์ที่กำหนดทุกอย่าง

### PPA (Pixel-Processing Accelerator) — "blitter" ไม่ใช่ GPU
- 3 งาน: **SRM** (scale-rotate-mirror), **Blend** (alpha FG ทับ BG + color-key + CLUT L4/L8, A4/A8), **Fill** (สีเดียว)
- หมุนได้แค่ **0 / 90 / 180 / 270°** — มุมอื่น = CPU
- ย่อ/ขยาย bilinear ละเอียดขั้นละ **1/16** (ขอบนุ่ม/สีเหลื่อมเล็กน้อย)
- SRM: buffer เข้ากับออกต้องคนละชุด · buffer ออก (cacheable) ต้อง align ตาม cache line (PSRAM: L1 + L2)
- โหมด non-blocking: `on_trans_done` ใน ISR, `max_pending_trans_num` ค่าเริ่ม 1 · client หนึ่งตัวต่อ task เดียว
- เวลาขึ้นกับขนาด block ที่ประมวลผล · ถ้าภาพอยู่ใน PSRAM = ติดแบนด์วิดท์ PSRAM (อุปกรณ์อื่นแย่งก็ช้าลง)
- ผลใน LVGL: **fill เร็วขึ้นเฉลี่ย ~30 %** (fill เต็มจอบางกรณี 9×), **หมุน ~40 %**, **blend แทบไม่เร็วขึ้น** (DMA2D bandwidth) ·
  partial buffer ไม่ได้อะไร — ต้อง full-screen double buffer · UI ที่มีตัวหนังสือ anti-alias/เงาไล่สีจะส่งงานเล็กๆ (1×1) ไป PPA จนเสียมากกว่าได้

### อย่างอื่นที่ต้องรู้
- **ต้นทุนคือจำนวนพิกเซล ไม่ใช่ polygon:** a3d โมเดลเดียวกัน ~55–65 fps ที่ 240×320 แต่ 15 fps ที่ 800×1280 · ที่ 800×1280 แค่ล้างจอ (12.5 ms) แพงกว่าส่งภาพ (7.2 ms)
- **JPEG ฮาร์ดแวร์ = ของดีที่สุดของ P4:** ESP-IDF ระบุ 480×800 = 253 fps, 720×1280 = 109 fps (YUV422 → RGB565/888) · บอร์ดเราวัดได้ 3.3 ms/เฟรม
  (ต้องบีบช่วงสี 16–235 ก่อน — T149)
- **PSRAM:** ผู้ใช้หนึ่งรายวัด PSRAM→SRAM ~185 MB/s (memcpy แบบ PIE, 200 MHz) — เฟรม 480×800 RGB565 = 768 KB ต่อการคัดลอกหนึ่งรอบ
- CPU: RISC-V 2 คอร์ 400 MHz (rev 3.x) มี FPU single + PIE SIMD — 3D แบบซอฟต์แวร์ใช้ได้ทั้งสองคอร์ แต่บอร์ดเรามีเสียง Gemini Live + ESP-SR ใช้ CPU อยู่ด้วย

## 3. ไลบรารี / เครื่องมือที่ตรวจแล้ว

| ชื่อ | สิ่งที่ได้ | หมายเหตุ |
|---|---|---|
| **a3d** (0015, Apache-2.0) | 3D header-only: glTF/GLB → `.a3d` (`tools/a3d_export.py --check` ประเมิน fps ต่อชิปให้), STL · skinning 255 กระดูก (2 weight/vertex) · texture RGB565 · Gouraud · z-buffer เลือกได้ · วาดทีละแถบ (`sendTile`) ไม่ต้องมี framebuffer เต็ม · อ่าน asset จาก flash ตรงๆ ได้ | **ไม่มี** alpha / เงา / PBR (BLEND/MASK = ทึบ) · P4 240×320: CesiumMan 4,672 tris 64.6 fps (2 workers), Fox 96 fps · 800×1280: 15–20 fps · Arduino ช้ากว่า IDF (ใส่ `-O2`), PlatformIO ยังไม่ทดสอบ · tile buffer ควรอยู่ RAM ภายใน (`setAllocator`) · DSI: `sendTile` ต้องรอ DMA เสร็จ |
| `andresragot/esp32_3d_engine` (MIT) | scene graph, กล้อง, แสง, mesh แบบหมุน/อัด (revolution/extrusion), painter's algorithm, thread pool 2 คอร์ | ไม่มีตัวเลขความเร็ว · ไดรเวอร์จอของตัวเอง (EK79007 DSI) |
| **tgx** (vindar) | 2D/3D ลง framebuffer ในหน่วยความจำ, ไม่ผูกกับจอ | มาจาก Teensy · ตัวเลข 30 fps 1024×600 เป็นของโปรเจคที่ต่อยอด |
| **LGFX_PPA** (tobozo, MIT) | `PPA_Sprite` (สืบจาก `LGFX_Sprite`) + `ppa_srm` / `ppa_blend` สำหรับ LovyanGFX/M5GFX | มีบั๊ก: mirror + หมุนมุมคี่ crash, ผล srm ที่ส่งต่อ blend เยื้อง, จอกะพริบบน arduino-esp32 3.3.0 · เราเรียก `driver/ppa.h` ตรงอยู่แล้ว ไม่จำเป็น |
| LVGL 9 + `esp_lvgl_adapter` | PPA draw unit (`CONFIG_LV_USE_PPA`), โหมดกันภาพฉีก (double/triple buffer) | PPA experimental · ต้องย้าย UI ทั้งหมด — ไม่คุ้มสำหรับเรา |
| ThorVG / `esp_lv_lottie_player` | Lottie / SVG ผ่าน LVGL | CPU ล้วน, ช้า (หัวข้อ 1) |
| `esp_emote_expression` / `brookesia_expression_emote` | ระบบดวงตา/อารมณ์ของ Espressif สำหรับ AI chatbot | เราทำเองแล้ว (Eyes + RobotAvatar) |
| Spine / Rive | 2D skeletal | ไม่พบ runtime สำหรับ ESP32 |

## 4. แนวทาง 3D สามแบบ

1. **Render ล่วงหน้า → เล่นเป็น JPEG** (ของเรา: Godot → `robot.pak`) — คุณภาพระดับ PC, CPU แทบว่าง · มุมกล้องตายตัว, ไฟล์ใหญ่ (9.3 MB), ต้องทำท่าเชื่อม
2. **Real-time (a3d)** — หมุน/ซูมด้วยนิ้วได้ · คุณภาพต่ำกว่า, ต้นทุนตามพื้นที่ที่วัตถุกินบนจอ
3. **ผสม** (ของเรา: ตัวหุ่นจากเฟรม + ดวงตา/ปากวาดสด) — คุ้มที่สุด

## 5. ความหมายต่อบอร์ดเรา (ลำดับตามความคุ้ม)

- **ไม่ย้ายไป LVGL** — LovyanGFX + ไปป์ไลน์ของเราได้ 30 fps แล้ว (หน้าหลักวาด 12 ms, ฉากโฮโลแกรม 21 ms จากงบ 33 ms)
- **Lottie / After Effects:** อย่าเล่นสด — **render เป็นเฟรม JPEG บน PC** แล้วใช้ระบบ `.pak` เดิม (`tools/p4_avatar/build_pack.py`)
- **ปลดล็อก PPA ย่อ/ขยายหุ่น (น่าลอง):** ตอนนี้ย่อไม่ได้เพราะ canvas เก็บ RGB565 สลับไบต์ (PPA เฉลี่ยไบต์ผิดคู่ — wiki 19) →
  ถอด JPEG แบบไบต์ปกติลง buffer แยก แล้ว PPA SRM ย่อ + สลับไบต์ในรอบเดียว · **ต้องทดสอบก่อน** ว่า PPA สลับไบต์ก่อนหรือหลังเฉลี่ยพิกเซล
- **3D สดชิ้นเล็กในหน้าต่างโฮโลแกรม:** ลูกโลก/ไอคอน 3D ~200×200 px ด้วย a3d — พื้นดำของเราเข้ากับ a3d ที่ไม่มี alpha พอดี ·
  ต้องวัดด้วย `uistat` (งบเหลือ ~12 ms ในฉากการ์ด) และดูว่าเสียง AI ไม่สะดุด
- **2D skeletal เอง:** ตัดชิ้นส่วนเป็น sprite + หมุนแต่ละชิ้นด้วย `pushRotateZoom` (CPU — PPA หมุนได้แค่ 90°) · สำหรับหุ่น Godot ให้ผลดีกว่า
- **60 fps:** จอ 60 Hz แต่คลิปหุ่นใหญ่ขึ้นเท่าตัว + งบวาดเหลือ 16 ms — ไม่คุ้ม

## แหล่งข้อมูล

- [LVGL — PPA Support](https://docs.lvgl.io/latest/en/html/integration/chip_vendors/espressif/hardware_accelerator_ppa.html) · [LVGL — DMA2D Support](https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_dma2d) · [LVGL forum: ESP PPA mixed results](https://forum.lvgl.io/t/esp-ppa-support-mixed-results/22744)
- [ESP-IDF PPA driver (ESP32-P4)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/ppa.html) · [ESP-IDF JPEG codec](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/jpeg.html) · [ESP-IDF External RAM](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32p4/api-guides/external-ram.html)
- [a3d — GitHub](https://github.com/0015/a3d) · [a3d component readme](https://components.espressif.com/components/0015/a3d/versions/0.9.4/readme) · [andresragot/esp32_3d_engine](https://components.espressif.com/components/andresragot/esp32_3d_engine/versions/1.0.1/readme)
- [tgx — Arduino reference](https://reference.arduino.cc/reference/en/libraries/tgx) · [esp32.com: tgx บน P4](https://esp32.com/viewtopic.php?p=149995)
- [esp32.com: Optimising P4 for Lottie](https://esp32.com/viewtopic.php?p=153948) · [ThorVG example (Espressif)](https://components.espressif.com/components/espressif/thorvg/versions/0.13.8~1/examples/thorvg-example) · [Espressif GUI optimization](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/lcd/gui_solution.html)
- [Intellar Engine — GitHub](https://github.com/intellar/Intellar-Engine) · [LGFX_PPA (tobozo)](https://github.com/tobozo/LGFX_PPA) · [esp_emote_expression](https://components.espressif.com/components/espressif2022/esp_emote_expression)
- [PIE / PSRAM discussion #353](https://github.com/espressif/developer-portal/discussions/353) · [esp_lvgl_adapter](https://components.espressif.com/components/espressif/esp_lvgl_adapter) · [DFRobot: MJPEG บน P4](https://community.dfrobot.com/makelog-318899.html)
