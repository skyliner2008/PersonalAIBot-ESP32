---
tags: [setup, installation, tools, environment]
created: 2026-10-14
status: reference
---

# 🛠️ Installation & Setup Guide

> **Related:** [[00-Home-Dashboard]] | [[03-Libraries-and-Dependencies]] | [[07-Troubleshooting-Log]] | [[17-Mobile-Flasher]]

---

## ติดตั้งจากไฟล์เฟิร์มแวร์สำเร็จรูป (ไม่ต้อง build)

สำหรับคนที่แค่อยากใช้งาน — ไม่ต้องลง VS Code / PlatformIO

1. ดาวน์โหลด `PersonalAIBot-ESP32-v<ver>-factory.bin` จากหน้า Releases ของ repo
2. เปิด <https://espressif.github.io/esptool-js/> ด้วย Chrome/Edge → **Connect** (หาไม่เจอ: กด BOOT ค้าง → แตะ RESET → ปล่อย BOOT)
3. Flash Address `0x0` → เลือกไฟล์ → **Program** → กด RESET
4. บูตครั้งแรก: แตะ 3 จุดปรับทัช → ⚙ → ใส่ Wi-Fi 2.4 GHz และ Gemini API key **ของตัวเอง** — หรือวาง `ai-setup.txt` (ตัวอย่างใน `tools/sd_card/`) ที่รากการ์ด SD ก่อนเปิดเครื่อง (เก็บเข้ารหัสในการ์ด)

ไฟล์ factory เขียนตั้งแต่ `0x0` จึงล้างค่าตั้งค่าเดิมในบอร์ดด้วย — ถ้าจะอัปเดตโดยเก็บค่าเดิม ให้ build จากซอร์ส (`pio run -t upload`)

**📱 จากมือถือ Android (T080):** ติดตั้ง `ESP32-AI-Flasher.apk` → เสียบสาย USB-C จากมือถือเข้าบอร์ด (OTG) → **แฟลชลงบอร์ด** — แอปโหลด release ล่าสุดเอง
และมีโหมด **อัปเดต** (`-app.bin`, เก็บการตั้งค่า/ความจำ) ด้วย — ดู [[17-Mobile-Flasher]]

**ผู้ดูแลโปรเจกต์ — ออก release (แบบแมนนวล):** แก้ `FIRMWARE_VERSION` ใน `src/app/AppConfig.h` → commit → `git push` → `python tools/publish_release.py`
(build → Release แบบร่าง → แนบ `.bin` + `.sha256` → เผยแพร่ → ดาวน์โหลดกลับมาเทียบ sha256) — ใช้ token ที่ Git Credential Manager เก็บไว้ ไม่แสดงออกมา
- **ทำไมไม่อัตโนมัติ:** บัญชี GitHub ถูกล็อกเรื่อง billing (แก้หลายรอบไม่ผ่าน) GitHub Actions จึงรันไม่ได้ — workflow `.github/workflows/firmware.yml` ตั้งเป็นกดรันเองเท่านั้น (ใส่ `push: {branches: [main]}` กลับเมื่อ Actions ใช้ได้)
- **Immutable releases:** แนบไฟล์ได้เฉพาะตอนเป็นร่าง และชื่อ tag ที่เคยเผยแพร่แล้วใช้ซ้ำไม่ได้ (v3.2.0 ถูกล็อกถาวร → ข้ามไป v3.2.1) — ทุก Release ต้องเปลี่ยนเลขเวอร์ชัน
(สคริปต์ไม่ยอมสร้างถ้า `include/secrets.h` มีค่าจริง, สแกนหา key ในไฟล์ และ `tools/pio_strip_paths.py` ตัด path เครื่องผู้ build ออกจากเฟิร์มแวร์)

## Prerequisites Overview

| Tool | Version | Purpose |
|------|---------|--------|
| VS Code / Antigravity IDE | Latest | Code editor with PlatformIO extension |
| Python | 3.9+ | PlatformIO CLI dependency |
| Git | 2.30+ | Version control |
| PlatformIO CLI | 6.x | Build system for ESP32 |
| USB Driver | CH340 or ESP32-S3 Native USB | Board communication |
| Obsidian | 1.5+ | Documentation viewer/editor |

---

## Step 1: Install VS Code / Antigravity IDE

### Option A: VS Code
1. Download from [https://code.visualstudio.com/](https://code.visualstudio.com/)
2. Install and open VS Code
3. Install extensions:
   - **PlatformIO IDE** (`platformio.platformio-ide`)
   - **C/C++** (`ms-vscode.cpptools`)
   - **Serial Monitor** (optional, PlatformIO includes one)

### Option B: Antigravity IDE
1. Download Antigravity IDE from Google
2. Built-in support for PlatformIO and AI-assisted coding
3. Recommended for this project due to AI agent integration

---

## Step 2: Install Python 3.9+

### Windows
1. Download from [https://www.python.org/downloads/](https://www.python.org/downloads/)
2. **IMPORTANT:** Check ✅ "Add Python to PATH" during installation
3. Verify:
   ```powershell
   python --version
   pip --version
   ```

### macOS
```bash
brew install python3
```

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install python3 python3-pip python3-venv
```

---

## Step 3: Install Git

### Windows
1. Download from [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Use default settings during installation
3. Verify:
   ```powershell
   git --version
   ```

### macOS
```bash
brew install git
```

### Linux
```bash
sudo apt install git
```

---

## Step 4: Install PlatformIO CLI

PlatformIO can be installed as a VS Code extension (recommended) or standalone CLI:

### Via pip (Standalone CLI)
```powershell
pip install platformio
pio --version
```

### Via VS Code Extension
1. Open VS Code
2. Go to Extensions (`Ctrl+Shift+X`)
3. Search for "PlatformIO IDE"
4. Click Install
5. Restart VS Code
6. PlatformIO CLI will be available in the integrated terminal

### Verify Installation
```powershell
pio --version
# Expected: PlatformIO Core, version 6.x.x

pio platform list
# Should show installed platforms
```

---

## Step 5: USB Driver Setup

The ES3C28P board has **two USB interfaces**:

### Option A: CH340 UART (External USB-to-Serial)
1. Download CH340 driver from [https://www.wch.cn/download/CH341SER_EXE.html](https://www.wch.cn/download/CH341SER_EXE.html)
2. Install and restart computer
3. Connect board via USB → Device Manager should show "COM x (CH340)"

> [!NOTE] Verified 2026-10-14
> พอร์ต Type-C ของบอร์ดตัวที่ใช้อยู่ต่อกับ **Native USB ของ ESP32-S3** — Windows เห็นเป็น
> `USB Serial Device (COM4)` VID:PID `303A:1001` ไม่ต้องลงไดรเวอร์ CH340

### Option B: ESP32-S3 Native USB (USB-CDC)
- No driver needed on Windows 10/11, macOS, or Linux
- Uses the USB-OTG port on the ESP32-S3
- Board appears as "USB Serial Device" or "ESP32-S3"
- In `platformio.ini`, set:
  ```ini
  build_flags = -DARDUINO_USB_CDC_ON_BOOT=1
  ```

### Checking the Port
```powershell
# Windows - List COM ports
Get-WmiObject Win32_SerialPort | Select-Object DeviceID, Description

# Or use PlatformIO
pio device list
```

> [!TIP] If the board is not detected  
> 1. Try a different USB cable (use data cable, not charge-only)  
> 2. Hold BOOT button → Press RST → Release BOOT to enter download mode  
> 3. Check Device Manager for unknown devices  
> See [[07-Troubleshooting-Log]] for more solutions.

---

## Step 6: Clone & Initialize the Project

```powershell
# Clone the repository
git clone <your-repo-url> ESP32-AI
cd ESP32-AI

# Install PlatformIO dependencies
pio pkg install

# Verify the build environment
pio run --environment esp32s3
```

### Configure Secrets
```powershell
# Copy the example secrets file
copy include\secrets.example.h include\secrets.h

# Edit with your credentials
notepad include\secrets.h
```

Fill in your credentials:
```cpp
#define WIFI_SSID     "YourWiFiName"
#define WIFI_PASSWORD "YourWiFiPassword"
#define GEMINI_API_KEY "your-gemini-api-key-here"
```

> [!CAUTION] API Key Security  
> Never commit `secrets.h` to version control! The `.gitignore` file already excludes it. Only `secrets.example.h` should be in the repository.

> [!IMPORTANT] ถ้ายังเป็นค่าตัวอย่าง
> เฟิร์มแวร์ v2 ตรวจว่า `WIFI_SSID` / `GEMINI_API_KEY` ยังเป็นค่าใน template หรือไม่ ถ้าใช่จะขึ้นจอ ERROR
> พร้อมข้อความ "ยังไม่ได้ตั้งค่า Wi-Fi ในไฟล์ include/secrets.h" — ส่วนอื่น (จอ, เสียง, ความจำ, console) ยังทำงานได้
> ใช้ Wi-Fi **2.4 GHz** เท่านั้น

### MicroSD (แนะนำ)
ใส่การ์ด FAT32 ก่อนเปิดเครื่อง → ความจำเก็บที่ `/agent/` บนการ์ด (`ltm.jsonl`, `episodes/`)
ถ้าไม่มีการ์ด ระบบจะใช้ LittleFS ในแฟลช 3.4 MB แทนโดยอัตโนมัติ — ดู [[08-Long-Term-Memory-GraphRAG]]

### Get a Gemini API Key
1. Go to [Google AI Studio](https://aistudio.google.com/apikey)
2. Click "Create API Key"
3. Copy the key to `secrets.h`
4. The Live API requires a project with billing enabled for production use

---

## Step 7: Install & Configure Obsidian

1. Download Obsidian from [https://obsidian.md/](https://obsidian.md/)
2. Install and open Obsidian
3. Click "Open folder as vault"
4. Navigate to `ESP32-AI/wiki/` folder
5. Select it as your vault

### Recommended Obsidian Settings
- **Editor → Default editing mode:** Live Preview
- **Files & Links → Default location for new notes:** Same folder as current file
- **Core Plugins:** Enable Graph View, Backlinks, Tags, Outline

### Recommended Community Plugins
| Plugin | Purpose |
|--------|---------|
| **Dataview** | Query and display note metadata |
| **Mermaid Tools** | Enhanced Mermaid diagram support |
| **Kanban** | Project task management board |
| **Checklist** | Aggregate checklists from all notes |
| **Calendar** | Daily notes and timeline view |

### Obsidian Graph View
After opening the vault, press `Ctrl+G` to see the knowledge graph. All `[[WikiLinks]]` will be visualized as connected nodes.

---

## Step 8: First Build & Upload

```powershell
# Build the firmware
pio run

# Upload to the board (connect via USB first)
pio run --target upload

# Open serial monitor to see debug output
pio device monitor --baud 115200
```

### Boot Output (จริงจากบอร์ด, 2026-10-14)
```
========================================
  ESP32-AI Personal Agent v2.0.0
  Board: ES3C28P (ESP32-S3-N16R8)
========================================
[PSRAM] 8189 KB
[AvatarUI] Ready (Thai font OK, line 28 px)
[Touch] FT6336G found
[Storage] SD card mounted (4-bit)
[Storage] Agent data in /sdcard/agent
[Audio] Initializing...
[ES8311] chip id 0x83, configuring for 24000 Hz
[Audio] Ready: I2S 24000 Hz, playback buffer 1024 KB, mic -> 16000 Hz
[Memory] 1 facts, 2 relations, 0 recent turns loaded in 6 ms (sd_card)
[Gemini] Client ready (model models/gemini-3.8-live, TLS verified against Google roots)
[Agent] task started on core 1
[Setup] Complete - type 'help' for console commands
```
หลังใส่ secrets แล้วจะเห็น `[WiFi] Connected` → `[Time] Synced` → `[Gemini] Setup sent` → `[Gemini] Setup complete - session ready`

### Serial console (พิมพ์ใน `pio device monitor`)
| คำสั่ง | ใช้ทำอะไร |
|---|---|
| `status` | heap/PSRAM, storage, codec/i2s/touch, volume, mic level |
| `audio` | ระดับ RMS ดิบของไมค์ช่อง L/R, ช่องที่เลือก |
| `tone 1000 500` | เล่นเสียงทดสอบ · `mic 3` ดูระดับไมค์ 3 วินาที · `vol 60` |
| `i2c` · `regs` · `reg 44 60` | สแกน I2C · ดู/เขียน register ES8311 (`44 60` = loopback) |
| `tool recall_memory {"query":"กาแฟ"}` | เรียก tool ตัวเดียวกับที่ Gemini ใช้ (ทดสอบความจำโดยไม่ต้องมีเน็ต) |
| `say a ข้อความ` · `screenshot` | ทดสอบซับไตเติล · ดึงภาพหน้าจอ RGB565 |

### Host unit tests (PC)
```powershell
host_tests\run_tests.bat     # ต้องมี Visual Studio Build Tools และรัน pio run มาก่อน 1 ครั้ง
```

---

## Build Shortcuts

| Action | Command | VS Code Shortcut |
|--------|---------|------------------|
| Build | `pio run` | `Ctrl+Alt+B` |
| Upload | `pio run -t upload` | `Ctrl+Alt+U` |
| Monitor | `pio device monitor` | `Ctrl+Alt+S` |
| Clean | `pio run -t clean` | — |
| Upload + Monitor | `pio run -t upload && pio device monitor` | — |

---

*Back to [[00-Home-Dashboard]]*
