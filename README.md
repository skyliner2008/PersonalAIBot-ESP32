<div align="center">

# 🤖 PersonalAIBot-ESP32

### A Thai-speaking, real-time voice AI assistant on an ESP32-S3 touch screen

**Gemini Live voice** · **on-device long-term memory** · **39 tools with on-screen cards** · **PC companion app**

[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-N16R8-E7352C?logo=espressif&logoColor=white)](wiki/01-Hardware-ES3C28P-Pinout.md)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino%202.0.17-F5822A?logo=platformio&logoColor=white)](platformio.ini)
[![Gemini Live](https://img.shields.io/badge/Gemini-Live%20API-4285F4?logo=googlegemini&logoColor=white)](wiki/05-Gemini-Live-API-Protocol.md)
[![Firmware](https://img.shields.io/badge/firmware-v3.2.0-3ddc84)](src/app/AppConfig.h)
[![Tests](https://img.shields.io/badge/host%20tests-1%2C421%20checks%20passing-3ddc84)](host_tests/)
[![Language](https://img.shields.io/badge/voice-ไทย%20%7C%20English-ffb020)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

**English** · [ภาษาไทย](README.th.md)

<img src="wiki/images/readme-eyes.png" width="760" alt="Animated eyes: happy, love, thinking, surprised, wink, curious">

</div>

---

## ✨ What it does

Talk to it like a person — in Thai or English. It answers with natural speech, shows its mood with animated eyes,
remembers you across days, and pulls live data (weather, fuel prices, markets, news…) onto the screen while it explains.
Everything runs on the board itself: **no server, no Raspberry Pi, no vector database** — just Wi-Fi and a Gemini API key.

| | |
|---|---|
| 🎙️ **Real-time voice** | Full-duplex audio over a TLS-verified WebSocket (mic 16 kHz up, voice 24 kHz down). Local VAD + AGC, barge-in by tapping the screen. |
| 🧠 **Long-term memory** | Facts, a small knowledge graph (GraphRAG-lite, BM25 search) and a daily conversation log stored on MicroSD — or internal flash when there is no card. Background consolidation with Flash-Lite. |
| 🧑‍💼 **Personalities** | 8 ready-made profiles, 30 voices, 7 personalities, male/female Thai politeness (ค่ะ / ครับ). Change them by touch or just by asking. |
| 👀 **Expressive face** | Cozmo-style eyes with 14 emotions, Thai subtitles, on-screen mic button, dark theme. |
| 📊 **Live data cards** | Weather + PM2.5, Thai fuel prices, stocks / gold / crypto / FX, technical analysis, market scanner, economic calendar, news, Wikipedia — drawn as graphics while the AI talks. |
| ⏰ **Personal assistant** | Timers, reminders (daily / weekdays / weekly), to-do list, notes, price alerts that speak up, calculator, unit & world-time conversion. |
| 📈 **MetaTrader 5** *(optional)* | Read your MT5 account through [mt5-core-server](#-metatrader-5-optional); voice trading is **off by default** and needs a spoken two-step confirmation. |
| 🔁 **Self-healing** | Automatic model fallback, quota-aware back-off, Google Search auto-disable on quota, session resumption, Wi-Fi reconnect. |
| 🖥️ **ESP32-AI Studio** | Dark-mode PC app: all settings, live debug log, chat, remote screen with touch, mic recorder, tool runner, build & flash. |

## 🖼️ Screenshots

<div align="center">

**Live data cards** — the AI fetches, the board draws, the AI summarises out loud

<img src="wiki/images/readme-cards.png" width="900" alt="Weather, fuel price, gold, market overview, technical analysis and economic calendar cards">

**ESP32-AI Studio** — the PC companion app

<img src="wiki/images/pc-studio-screen.png" width="440" alt="Remote screen tab"> <img src="wiki/images/pc-studio-log.png" width="440" alt="Debug log tab">

</div>

## 🔧 Hardware

| Part | Details |
|---|---|
| **Board** | **ES3C28P** — ESP32-S3R8 (dual-core 240 MHz, 16 MB flash, 8 MB PSRAM) with 2.8" 320×240 IPS (ILI9341V) and capacitive touch (FT6336G) |
| **Audio** | ES8311 codec, on-board MEMS mic, FM8002E amplifier → **a small 4–8 Ω speaker on the 2-pin JST connector** (required) |
| **Extras** | WS2812 RGB LED · MicroSD slot (FAT32, optional) · LiPo connector with battery gauge · BOOT button used as a push-button |
| **Network** | 2.4 GHz Wi-Fi (5 GHz is not supported by the ESP32) |

Pinout, board photos and connector guide → [wiki/01-Hardware-ES3C28P-Pinout.md](wiki/01-Hardware-ES3C28P-Pinout.md)

## 🚀 Quick start

> **No passwords or keys ship with this project.** You only need your own **2.4 GHz Wi-Fi** and a free
> **Gemini API key** from **[aistudio.google.com/api-keys](https://aistudio.google.com/api-keys)** — both are entered on the
> device after flashing and stored encrypted on *your* board.

### Option A — ready-made firmware (no programming tools)

1. Download `PersonalAIBot-ESP32-v…-factory.bin` from **[Releases](https://github.com/skyliner2008/PersonalAIBot-ESP32/releases)**.
2. Open **[espressif.github.io/esptool-js](https://espressif.github.io/esptool-js/)** in Chrome or Edge, plug the board in with USB-C and press **Connect**
   (if it is not found: hold **BOOT**, tap **RESET**, release BOOT, then connect again).
3. Flash address **`0x0`** → choose the `.bin` file → **Program**. When it finishes, press **RESET**.

Command-line alternative: `esptool.py --chip esp32s3 write_flash 0x0 PersonalAIBot-ESP32-v3.2.0-factory.bin`

> The factory image starts at `0x0`, so it also **clears saved settings** on that board (a clean install).

### Option B — build from source

```bash
git clone https://github.com/skyliner2008/PersonalAIBot-ESP32.git
cd PersonalAIBot-ESP32
pio run -t upload
pio device monitor
```

Needs [PlatformIO](https://platformio.org/install) (VS Code extension or `pip install platformio`); libraries download automatically.
Updating this way keeps your saved Wi-Fi / API key.

> **Optional:** pre-fill Wi-Fi and the API key at build time by copying `include/secrets.example.h` to `include/secrets.h`
> (it is git-ignored). Never publish a firmware image built that way — `tools/make_release.py` refuses to.

### First boot — enter your own Wi-Fi and API key

1. **Touch calibration** — tap the three targets.
2. Tap ⚙ (or hold **BOOT** for 1.5 s) → **Wi-Fi** → scan and pick your network → type the password with the on-screen Thai/English keyboard.
3. **API key** → type the key → **Test API key** shows which models your key can use.
4. When the screen says **"พร้อมคุย" (ready)** — just start talking.

**Or set it up from a PC with the SD card:** copy [`tools/sd_card/ai-setup.txt`](tools/sd_card/ai-setup.txt) to the root of the
micro SD card (FAT32), fill in `WIFI_SSID`, `WIFI_PASSWORD` and `GEMINI_API_KEY`, put the card in the board and power it on.
The board imports the file, stores the values encrypted in `agent/secrets.json` on the card and deletes the plain file.
(The Studio's **💾 SD card file…** button writes this file for you.)

Prefer a keyboard? Use the PC app instead (step below) — every setting is there.

### ESP32-AI Studio (PC app, optional)

Needs Python 3.9+ (the python.org installer includes Tkinter). Get the repo (`git clone`, or **Code → Download ZIP**), then:

```bash
pip install -r tools/pc_app/requirements.txt
python tools/pc_app/esp32_ai_studio.py
```

On Windows you can double-click `tools\pc_app\run_studio.bat`. Close the Studio (or press **Disconnect**) before flashing from another tool — or flash from the Studio's **🔧 Firmware** tab, which releases the port for you.

## 💬 Using it

| Do this | What happens |
|---|---|
| Speak when the screen shows **พร้อมคุย** | Voice answer + subtitles (and a data card when relevant) |
| Tap 🎤 (top-left) | Mute / unmute the mic |
| Tap the screen or press BOOT while it speaks | Stop talking (barge-in) |
| Tap while asleep / paused | Wake up / reconnect immediately |
| Long-press the screen 1.5 s | Fresh conversation (long-term memory stays) |
| Tap a data card | Close it (cards also close after ~30 s) |

**Things to try** *(Thai shown, English works too)*

```text
"จำไว้นะว่าฉันแพ้กุ้ง"                    → saves a memory          ("Remember I'm allergic to shrimp")
"อากาศที่เชียงใหม่วันนี้เป็นยังไง ฝุ่นเยอะไหม" → weather + PM2.5 card
"ราคาน้ำมันดีเซลวันนี้เท่าไหร่"               → Thai fuel price table
"วิเคราะห์ทองคำทุกกรอบเวลา"                 → multi-timeframe gauge card
"หุ้นไทยตัวไหนขึ้นแรงวันนี้"                  → SET top gainers
"สัปดาห์นี้มีข่าวเศรษฐกิจอะไรสำคัญ"            → economic calendar in Thai time
"เตือนฉันถ้าบิทคอยน์ต่ำกว่า 80,000"           → spoken price alert (checked every 2 min)
"เตือนกินยาทุกวันสองทุ่ม"                    → daily reminder that rings
"เปลี่ยนเป็นเสียงผู้ชาย" / "ราตรีสวัสดิ์"         → change voice / go to sleep
```

## 🛠️ Tools

The model calls these by itself. Data tools run in a background task and draw a card while the AI keeps talking.

<details>
<summary><b>All 39 tools</b> (37 by default; the 2 MT5 tools appear when an MT5 server is configured)</summary>

| Group | Tools |
|---|---|
| **Memory** | `save_memory` · `save_relation` · `recall_memory` · `forget_memory` · `recall_conversation` |
| **Time & tasks** | `set_timer` · `set_reminder` · `list_reminders` · `cancel_reminder` · `add_task` · `list_tasks` · `update_task` |
| **Assistant & device** | `get_device_status` · `set_expression` · `configure_assistant` · `list_assistant_options` · `set_volume` · `set_brightness` · `control_led` · `go_to_sleep` |
| **Live data** 🖼️ | `get_weather` (Open-Meteo + PM2.5, IP location by default) · `get_oil_price` (Bangchak) · `get_market_quote` (TradingView) · `convert_currency` · `get_news` (Google News) |
| **Utilities** 🖼️ | `calculate` · `convert_units` (incl. Thai ไร่/งาน/ตารางวา, บาททอง) · `get_world_time` (DST-aware) · `get_location` · `search_knowledge` (Wikipedia th/en) · `notes` |
| **Finance pack** 🖼️ | `market_overview` (+ crypto Fear & Greed) · `market_analysis` (RSI, MACD, EMA, ADX, multi-timeframe, P/E, dividend) · `market_scan` (gainers / losers / volume / RSI, TH-US-crypto) · `economic_calendar` (ForexFactory, Thai time) · `position_size` · `price_alert` |
| **MetaTrader 5** | `mt5_account` · `mt5_trade` (off by default, max-lot limit, two-step confirmation) |
| **Built-in** | Google Search grounding (auto-disabled for 6 h when its quota runs out) |

All data sources are free and key-less; every HTTPS connection is certificate-verified.
Market data comes from TradingView's public scanner endpoint (unofficial) — **for information only, not investment advice.**

Details and timings → [wiki/11-Data-Tools-and-Cards.md](wiki/11-Data-Tools-and-Cards.md) · [wiki/09-Assistant-Profiles-and-Tools.md](wiki/09-Assistant-Profiles-and-Tools.md)

</details>

## 🏗️ Architecture

```mermaid
flowchart LR
    MIC[🎙️ MEMS mic<br/>ES8311] -->|I2S 24 kHz → 16 kHz<br/>VAD + AGC| AG
    subgraph ESP32-S3
        AG[AgentController<br/>state · fallback · tools]
        IO[geminiIO task<br/>WebSocket + TLS]
        WT[webTools task<br/>HTTPS data tools]
        MEM[(Memory<br/>BM25 + graph<br/>SD / LittleFS)]
        UI[UI task<br/>eyes · cards · menus]
        AG <--> IO
        AG <--> WT
        AG <--> MEM
        AG --> UI
        WT --> UI
    end
    IO <-->|wss://| GEM[☁️ Gemini Live API]
    WT -->|https://| DATA[☁️ Open-Meteo · Bangchak<br/>TradingView · Wikipedia ...]
    AG -->|24 kHz PCM| SPK[🔊 Speaker]
    UI --> LCD[📱 2.8″ touch LCD]
    PC[🖥️ ESP32-AI Studio] <-->|USB serial<br/>TAG + JSON| AG
```

- **Models:** preferred Live model → `gemini-3.8-live` → `gemini-3.8-live-extended-thinking` → `gemini-3.1-flash-live-preview`; text tasks use `gemini-3.5-flash-lite` → `gemini-3.1-flash-lite`. Quota / overload / auth errors are classified and each model cools down separately.
- **Memory budget:** audio rings, the LCD sprite, WebSocket frames and all TLS buffers live in PSRAM, keeping ~85 KB of internal RAM free under full load.
- **Tokens:** local VAD, context compression and auto-sleep keep usage well under the 65k tokens/min Live limit.

Full design → [PROJECT_ARCHITECTURE_SPEC.md](PROJECT_ARCHITECTURE_SPEC.md)

## 📈 MetaTrader 5 (optional)

Works with the same **mt5-core-server** bridge as the PersonalAIBot Android app.

1. Studio → ⚙️ Settings → **Finance / MetaTrader 5** → enter the server URL (e.g. `http://192.168.1.10:8000`) and token.
2. `mt5_account` is now available: balance, equity, floating P/L, open positions, orders, history.
3. Voice trading (`mt5_trade`) stays **off** until you tick *Allow voice trading* (with a warning). Even then every order
   is limited to *max lot*, read back to you, and sent only after you clearly confirm within 2 minutes.

## 🖥️ ESP32-AI Studio

| Tab | Highlights |
|---|---|
| 📜 Debug log | Colour-coded live log, regex filter, copy/save, console with history |
| 💬 Chat | Type to the assistant; it answers through the board's speaker |
| ⚙️ Settings | Wi-Fi scan, API key + self-test, profile / voice / personality, models, Search, mic sensitivity, Finance & MT5, import/export (no secrets) |
| 🧰 Tools | Record what the AI hears (waveform + WAV), speaker test, run any of the AI's tools, eye emotions, memory tools |
| 🖥️ Screen | Live mirror of the LCD — click to tap, drag to swipe, right-click to long-press |
| 📊 Status | Session, models & cool-downs, tokens/min graph, RAM, storage, battery |
| 🔧 Firmware | Build, build + upload, host unit tests |

Copy/paste works with both Thai and English keyboard layouts. Docs → [wiki/10-PC-Studio-App.md](wiki/10-PC-Studio-App.md)

## 🧪 Tests

Portable modules (memory, tools, scheduler, tasks, personas, model fallback, calculator, units …) are tested on the PC:

```bat
host_tests\run_tests.bat
```

28 suites · 1,421 checks · needs Visual Studio Build Tools (`cl.exe`) and one `pio run` first (for ArduinoJson).

## 📁 Project layout

```text
src/agent      AgentController (the brain), AgentTools, WebTools + WebToolsExtra (data tools), Personas, Scheduler, TaskList
src/ai         GeminiLiveClient (geminiIO task), TextModelClient, ModelFallback, root CAs
src/ui         Eyes, FaceScreen, CardScreen (data cards), menus, Thai keyboard
src/audio      AudioManager (I2S), ES8311 driver, resampler
src/memory     MemoryStore (BM25 + graph), EpisodicLog
src/display    LovyanGFX config, touch driver, Thai fonts 26/20 px
src/system     I2C bus, storage (SD / LittleFS) · src/util  UTF-8, calculator, units, time, JSON helpers
tools/pc_app   ESP32-AI Studio (Python + Tkinter + pyserial)
tools/         gen_thai_font.py, gen_root_ca.py
host_tests/    PC unit tests
wiki/          Obsidian knowledge base (Thai): hardware, setup, protocol, UI, memory, troubleshooting log
```

## 📚 Documentation

| | |
|---|---|
| [00 Home dashboard](wiki/00-Home-Dashboard.md) | Index and change log |
| [01 Hardware](wiki/01-Hardware-ES3C28P-Pinout.md) · [02 Installation](wiki/02-Installation-and-Setup.md) · [03 Libraries](wiki/03-Libraries-and-Dependencies.md) | Board, setup, dependencies |
| [04 Architecture](wiki/04-System-Architecture.md) · [05 Gemini Live protocol](wiki/05-Gemini-Live-API-Protocol.md) | How it works |
| [06 Eyes & Thai UI](wiki/06-Avatar-and-Thai-UI.md) · [08 Long-term memory](wiki/08-Long-Term-Memory-GraphRAG.md) | Face, menus, memory design |
| [09 Profiles & tools](wiki/09-Assistant-Profiles-and-Tools.md) · [11 Data tools & cards](wiki/11-Data-Tools-and-Cards.md) | What the assistant can do |
| [10 PC Studio](wiki/10-PC-Studio-App.md) · [07 Troubleshooting log](wiki/07-Troubleshooting-Log.md) | Tools and 40+ solved problems |

## 🔒 Security & privacy

**Where your Wi-Fi password, API key and MT5 token live**

| Place | Contains secrets? |
|---|---|
| Source code in this repo | **No.** Secrets are typed on the device (touch menu or Studio), never into the code. |
| `include/secrets.h` (optional build-time defaults) | Only if you fill it in — it is **git-ignored**, so it is never uploaded. `secrets.example.h` holds placeholders only. |
| Firmware binary (`.pio/`) | Only if you used `secrets.h`; the build folder is git-ignored. |
| The SD card — `agent/secrets.json` (or the board's NVS flash when no card is inserted) | **Yes, encrypted** — AES-256-GCM with a key derived from this chip's factory MAC + unique-ID eFuses (`src/app/SecretBox.*`). The file opens only on the board that wrote it (moving the card to another board = set up again with `ai-setup.txt`); reading the card on a PC shows no secrets. A plain `ai-setup.txt` is overwritten and deleted right after import. Older plain-text copies are wiped automatically once (including the Wi-Fi driver's own copy, now disabled with `WiFi.persistent(false)`). |

- This is not full flash encryption: someone who has your board **and** this firmware could still derive the key. Use a dedicated API key and rotate it if the board is lost.
- Neither the firmware nor the Studio ever prints the API key, Wi-Fi password or MT5 token; `config` shows only a masked hint and `secretsSealed`. Exported settings files contain no secrets; voice recordings (`recordings/`) are git-ignored.
- Audio is streamed to Google's Gemini API while the mic is open. Memory and notes stay on the board's SD card / flash.
- Voice trading is disabled by default and always confirmed; you are responsible for any trades.

## 🧑‍💻 For developers

- **Fork & build:** `pio run` builds from a clean clone — no `secrets.h` needed. Settings live in NVS (`src/app/Settings.*`), so your own keys never touch the source tree.
- **Add a tool:** declare it in `AgentTools::addDeclarations` (`src/agent/AgentTools.cpp`); network tools go in `src/agent/WebToolsExtra.cpp` and draw a `CardData` (see [wiki/11](wiki/11-Data-Tools-and-Cards.md)).
- **Test without a board:** `host_tests\run_tests.bat`; on a board, every tool can be called from the Studio's 🧰 tab or the serial console (`tool <name> {json}`).
- **Releases are automatic:** bump `FIRMWARE_VERSION` in `src/app/AppConfig.h`, commit, `git push` — GitHub Actions ([`.github/workflows/firmware.yml`](.github/workflows/firmware.yml)) builds the firmware, runs the host tests and publishes Release `v<version>` with `PersonalAIBot-ESP32-v<version>-factory.bin` + `.sha256`. Every other push / pull request just builds (the image is kept as a workflow artifact). *Actions → Firmware → Run workflow* re-uploads the current version.
- **Local release image:** `python tools/make_release.py` (same script the workflow uses) — refuses to run while `include/secrets.h` holds real values and scans the image for API-key-like strings.
- **Docs:** the `wiki/` folder is an Obsidian vault; please add problems you solve to [wiki/07-Troubleshooting-Log.md](wiki/07-Troubleshooting-Log.md).

Pull requests and issues are welcome.

## 📄 License

[MIT](LICENSE) © 2026 skyliner2008 — free to use, modify and share; keep the copyright notice. Third-party fonts, libraries and data services keep their own licenses/terms (see [LICENSE](LICENSE)).

## 🙏 Credits

- [Google Gemini Live API](https://ai.google.dev/gemini-api/docs/live) · [LovyanGFX](https://github.com/lovyan03/LovyanGFX) · [ArduinoJson](https://arduinojson.org/) · [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)
- Thai font: **TH Sarabun New** (SIPA, Thai national font)
- Data: [Open-Meteo](https://open-meteo.com/) · [Bangchak](https://www.bangchak.co.th/) · [TradingView](https://www.tradingview.com/) · [Wikipedia](https://www.wikipedia.org/) · [ForexFactory](https://www.forexfactory.com/) · [alternative.me](https://alternative.me/crypto/fear-and-greed-index/) · [ipwho.is](https://ipwho.is/) · Google News
- Tools and finance features ported from the **PersonalAIBot** Android app

<div align="center">

Made with ❤️ in Thailand · ⭐ Star the repo if it helped you

</div>
