# AGENTS.md — PersonalAIBot-ESP32

Thai-speaking real-time voice assistant firmware for the ES3C28P board (ESP32-S3, PlatformIO / Arduino 2.0.17)
plus a Python/Tkinter PC companion app. Gemini Live for voice, on-device memory, read-only data tools
(weather, fuel, TradingView quotes/analysis, news, Wikipedia). There is no trading / MT5 / broker connection.

## Build & test
- Firmware: `pio run` (upload: `pio run -t upload`)
- Host unit tests (after one `pio run`, which fetches ArduinoJson): `host_tests\run_tests.bat` (MSVC) or `bash host_tests/run_tests.sh`
- PC app: `pip install -r tools/pc_app/requirements.txt` then `python tools/pc_app/esp32_ai_studio.py`
- Android phone flasher (wiki 17): `python tools/build_flasher_apk.py` (needs JDK 17+ and the Android SDK) → `dist/ESP32-AI-Flasher.apk`;
  tests `cd tools/android_flasher && ./gradlew testDebugUnitTest`. Its release keystore lives outside the repo; never create or commit it for the user.

## Layout
- `src/agent/` AgentController split by concern (`AgentController.cpp` setup/loop, `AgentSession.cpp`, `AgentMic.cpp`,
  `AgentRoutines.cpp`, `AgentDevice.cpp`, shared helpers in `AgentInternal.h`); tools in `AgentTools.*`, `WebTools*.*`
- `src/ai/` Gemini Live client (own WebSocket frame reader on ESP-IDF transport), text model, model fallback
- `src/app/` settings (NVS / SD) and `SecretBox` (AES-256-GCM sealing of Wi-Fi password + API key)
- `src/vehicle/` + `src/ui/DashScreen.*` vehicle dash (simulator, ELM327 over BLE / UART, Honda K-Line; wiki 16).
  Written from scratch under MIT: do not copy code or art from moomdate/redline-gauge (PolyForm Noncommercial)
- `wiki/` Obsidian knowledge base in Thai — start at `wiki/00-Home-Dashboard.md`

## Rules
- Never commit secrets (`include/secrets.h` is git-ignored). Data tools must use verified HTTPS (`httpRequest` refuses `http://`).
- Tool declarations are billed every turn: `host_tests` keeps the base set under 15 KB of JSON (opt-in tools such as
  `device` / `vehicle` are declared only when configured).
- When you change behaviour, update the matching wiki page, the change log in `wiki/00-Home-Dashboard.md`,
  and both READMEs (`README.md`, `README.th.md`). Keep both copies of `PROJECT_ARCHITECTURE_SPEC.md` identical.
- Releases are manual: bump `FIRMWARE_VERSION`, commit, push, `python tools/publish_release.py` (immutable releases).
  OTA images are signed (ECDSA P-256) with a private key that lives outside the repo (`tools/ota_keygen.py`, once); never commit it.
