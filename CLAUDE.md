# PersonalAIBot-ESP32 (repo นี้)

เฟิร์มแวร์ผู้ช่วย AI พูดไทยแบบ real-time บนบอร์ด ES3C28P (ESP32-S3) + แอป PC "ESP32-AI Studio"
ทำงานในตัวบอร์ดทั้งหมด: Gemini Live (เสียง) + ความจำระยะยาว + tools ข้อมูลสด — **ไม่มี server ของตัวเอง**
ข้อมูลการเงินอ่านจาก TradingView อย่างเดียว (**ไม่มีการเชื่อมต่อ MT5 / โบรกเกอร์** — ถอดออกใน v3.4)

## คำสั่งที่ใช้บ่อย
- Build เฟิร์มแวร์: `pio run` · แฟลช: `pio run -t upload` · serial: `pio device monitor`
- Host unit tests (ต้อง `pio run` ก่อน 1 ครั้ง): `host_tests\run_tests.bat` (MSVC) หรือ `bash host_tests/run_tests.sh` (g++/clang++)
- PC Studio: `python tools/pc_app/esp32_ai_studio.py`
- ออก Release (GitHub Actions ใช้ไม่ได้ — บัญชีติด billing): แก้ `FIRMWARE_VERSION` ใน `src/app/AppConfig.h` → commit → push →
  `python tools/publish_release.py` (build + host tests + draft → upload → publish; release เป็น immutable ต้องขึ้นเลขใหม่ทุกครั้ง)
  · ลายเซ็น OTA: ครั้งเดียว `python tools/ota_keygen.py` (กุญแจส่วนตัวอยู่นอก repo — ห้าม commit / ห้ามสร้างแทนผู้ใช้) แล้ว make_release เซ็น `-app.bin` ให้

## โครงสร้างหลัก
- `src/agent/` — `AgentController.cpp` (setup/loop) + `AgentSession.cpp` / `AgentMic.cpp` / `AgentRoutines.cpp` / `AgentDevice.cpp`,
  `AgentTools.*` (function declarations), `WebTools*.*` (tools ข้อมูล, HTTPS เท่านั้น)
- `src/ai/` Gemini Live client · `src/audio/` I2S/ES8311/วิทยุ · `src/ui/` ดวงตา/การ์ด/เมนู · `src/memory/` ความจำ · `src/app/` Settings + SecretBox
- `tools/pc_app/` ESP32-AI Studio · `host_tests/` unit tests · `wiki/` Obsidian vault

## กติกาการทำงาน
- ตอบผู้ใช้เป็นภาษาไทย
- แก้ไฟล์ C++ ด้วย Edit tool (สคริปต์ heredoc/Python อาจแปลง `\n` ในสตริง C++ เป็นขึ้นบรรทัดจริง)
- ห้ามใส่รหัส Wi-Fi / API key ในโค้ด (`include/secrets.h` ถูก git-ignore และ `make_release.py` ไม่ยอมปล่อย image ที่มีความลับ)
- สร้าง skill / tool / script เพิ่มได้ถ้าจำเป็น (ใส่ใน `tools/`)

## "สมองส่วนนอก" — wiki/ (Obsidian)
`wiki/` คือสารบัญที่ Claude กับผู้ใช้ดูแลร่วมกัน ใช้อ้างอิงก่อนแก้ไขโปรเจค — เริ่มที่ `wiki/00-Home-Dashboard.md`
**ทุกครั้งที่แก้โปรเจค:** อัปเดตหน้า wiki ที่เกี่ยวข้อง + แถว change log ใน `00-Home-Dashboard.md`,
บันทึกปัญหาที่แก้ใน `wiki/07-Troubleshooting-Log.md`, และอัปเดต `README.md` + `README.th.md`
(`PROJECT_ARCHITECTURE_SPEC.md` ต้องเหมือนกับ `wiki/PROJECT_ARCHITECTURE_SPEC.md`)
