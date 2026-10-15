/**
 * @file main.cpp
 * @brief ESP32-AI Personal Assistant v3 — boot sequence, UI/LED tasks, serial console
 * @see wiki/00-Home-Dashboard.md
 * @see wiki/04-System-Architecture.md
 *
 * Tasks:
 *   Core 0: audioOut (P6), audioIn (P6), textModel (P1), led (P1), [Wi-Fi/lwIP]
 *   Core 1: agent (P4), ui (P3), memWriter (P2), loopTask (console)
 *   any:    websocket (P5)
 */

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include "agent/AgentController.h"
#include "agent/Personas.h"
#include "app/AppConfig.h"
#include "app/Settings.h"
#include "audio/AudioManager.h"
#include "audio/RadioPlayer.h"
#include "audio/RadioStations.h"
#include "display/LGFX_ES3C28P.hpp"
#include "display/TouchFT6336.h"
#include "pins_config.h"
#include "system/I2cBus.h"
#include "system/Storage.h"
#include "ui/CardScreen.h"
#include "ui/FaceScreen.h"
#include "ui/KeyboardScreen.h"
#include "ui/RadioScreen.h"
#include "ui/SettingsScreens.h"
#include "ui/UiManager.h"
#include "util/Utf8.h"

static LGFX_ES3C28P lcd;
static I2cBus i2c;
static TouchFT6336 touch;
static AudioManager audio;
static Storage storage;
static AgentController agent;
static Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// ============================================================
// UI commands from the serial console (executed in the UI task)
// ============================================================

struct UiCommand {
    enum Type : uint8_t { Tap, Long, Drag, Open, Type, Back, Home, Shot, Rotate, Invert } type;
    int16_t x0, y0, x1, y1;
    char text[200];
};
static QueueHandle_t uiQueue;
static volatile bool shotRequested = false;

// Dump the sprite right after a complete frame (called from the UI task)
static void dumpScreenshot() {
    size_t len = 0;
    const uint8_t* fb = gUi.frameBuffer(len);
    Serial.printf("SCREENSHOT %d %d %u\n", ui::W, ui::H, (unsigned)len);
    // One write: HWCDC keeps its TX lock for the whole frame, so other tasks' log lines wait
    // (≤ tx timeout) instead of landing inside the image. The PC app checks the END trailer.
    if (fb) Serial.write(fb, len);
    Serial.print("\nEND\n");
}

static void postUi(const UiCommand& c) {
    UiCommand* p = new UiCommand(c);
    if (xQueueSend(uiQueue, &p, 0) != pdTRUE) delete p;
}

static void runUiCommands() {
    UiCommand* c;
    while (xQueueReceive(uiQueue, &c, 0) == pdTRUE) {
        switch (c->type) {
            case UiCommand::Tap: gUi.injectTap(c->x0, c->y0); break;
            case UiCommand::Long: gUi.injectLongPress(c->x0, c->y0); break;
            case UiCommand::Drag: gUi.injectDrag(c->x0, c->y0, c->x1, c->y1); break;
            case UiCommand::Open: settingsui::openPage(c->text); break;
            case UiCommand::Type:
                if (gUi.top() == &gKeyboard) gKeyboard.typeText(c->text);
                break;
            case UiCommand::Back: gUi.pop(); break;
            case UiCommand::Home: gUi.popToRoot(); break;
            case UiCommand::Shot: shotRequested = true; break;
            case UiCommand::Rotate: gUi.setRotation((uint8_t)c->x0); break;
            case UiCommand::Invert: gUi.setInvert(c->x0 != 0); break;
        }
        delete c;
    }
}

// ============================================================
// Battery (LiPo via 1:2 divider on GPIO 9)
// ============================================================

static int lipoPercent(uint32_t mv) {
    static const uint16_t kCurve[][2] = {{4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
                                         {3700, 30},  {3600, 15}, {3500, 5},  {3300, 0}};
    if (mv >= kCurve[0][0]) return 100;
    for (size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); i++) {
        if (mv >= kCurve[i][0]) {
            const int v0 = kCurve[i][0], v1 = kCurve[i - 1][0];
            const int p0 = kCurve[i][1], p1 = kCurve[i - 1][1];
            return p0 + (int)((mv - v0) * (p1 - p0) / (v1 - v0));
        }
    }
    return 0;
}

static int readBatteryPercent() {
    static float filtered = -1.0f;
    const float mv = analogReadMilliVolts(VBAT_ADC_PIN) * VBAT_DIVIDER;
    if (mv < 2500.0f) {
        filtered = -1.0f;
        return -1;
    }
    filtered = filtered < 0 ? mv : filtered * 0.9f + mv * 0.1f;
    return lipoPercent((uint32_t)filtered);
}

// ============================================================
// Tasks
// ============================================================

/** UI task — core 1: touch, BOOT button, console UI commands, rendering. */
static void uiTask(void*) {
    bool bootDown = false;
    uint32_t bootAt = 0, lastBattery = 0;
    bool bootLong = false;
    for (;;) {
        const uint32_t now = millis();
        runUiCommands();

        int16_t rx = 0, ry = 0;
        const bool down = touch.read(rx, ry);
        gUi.handleRawTouch(down, rx, ry, now);

        // BOOT button: short = tap (or back in menus), long (1.5 s) = settings menu
        const bool b = digitalRead(BOOT_BUTTON) == LOW;
        if (b && !bootDown) {
            bootDown = true;
            bootAt = now;
            bootLong = false;
        } else if (b && !bootLong && now - bootAt > 1500) {
            bootLong = true;
            if (gUi.atRoot()) settingsui::openMenu();  // always reachable, even with bad touch
        } else if (!b && bootDown) {
            bootDown = false;
            if (!bootLong && now - bootAt > 30) {
                if (gUi.atRoot()) agent.uiTap();
                else gUi.pop();
            }
        }

        // info cards from the web tools: over the face only (never over a menu), closed after a while
        if (gCard.hasPending()) {
            if (gUi.top() == &gCard) {
                gCard.activate();
            } else if (gUi.atRoot() || gUi.top() == &gRadioUi) {
                gCard.activate();
                gUi.push(&gCard);
            }
        } else if (gUi.top() == &gCard && (gCard.expired(now) || agent.powerSave())) {
            gUi.pop();  // power save: the clock face must not stay hidden under a card
        }

        // radio player screen: opens when a station starts (voice or console) over the face; the power-save
        // clock is the face itself, so the player steps aside while dimmed and comes back on wake
        static bool radioWas = false, radioHidden = false;
        const bool radioOn = gRadio.active();
        if (agent.powerSave()) {
            if (gUi.top() == &gRadioUi) {
                gUi.pop();
                radioHidden = true;
            }
        } else if (radioOn && gUi.atRoot() && (!radioWas || radioHidden)) {
            gUi.push(&gRadioUi);
            radioHidden = false;
        }
        if (!radioOn) radioHidden = false;
        radioWas = radioOn;

        if (now - lastBattery > 1000) {
            lastBattery = now;
            agent.setBattery(readBatteryPercent());
        }
        // power save: the clock face changes once a second - redraw (SPI push) only then, poll slower
        static uint32_t lastFrame = 0;
        const bool clockFace = agent.powerSave() && gUi.atRoot();
        if (!clockFace || now - lastFrame >= 500 || shotRequested) {
            lastFrame = now;
            gUi.frame(now);
        }
        if (shotRequested) {
            shotRequested = false;
            dumpScreenshot();
        }
        vTaskDelay(pdMS_TO_TICKS(clockFace ? 40 : 16));
    }
}

/** LED task — core 0: status colors (or the color chosen by control_led). */
static void ledTask(void*) {
    for (;;) {
        const uint32_t now = millis();
        uint32_t rgb;
        if (agent.ledOverride(rgb)) {
            rgbLed.setPixelColor(0, rgbLed.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        } else {
            const int pulse = (int)(sinf(now / 300.0f) * 50 + 50);
            switch (agent.state()) {
                case AVATAR_BOOT: rgbLed.setPixelColor(0, rgbLed.Color(50, 50, 50)); break;
                case AVATAR_SETUP: rgbLed.setPixelColor(0, (now / 800 % 2) ? rgbLed.Color(60, 60, 60) : 0); break;
                case AVATAR_CONNECTING: rgbLed.setPixelColor(0, (now / 500 % 2) ? rgbLed.Color(0, 0, 100) : 0); break;
                case AVATAR_IDLE:
                    rgbLed.setPixelColor(0, agent.micMuted() ? rgbLed.Color(40, 0, 20) : rgbLed.Color(0, 30, 0));
                    break;
                case AVATAR_LISTENING: rgbLed.setPixelColor(0, rgbLed.Color(0, 0, 150)); break;
                case AVATAR_THINKING: rgbLed.setPixelColor(0, rgbLed.Color(pulse, pulse, 0)); break;
                case AVATAR_SPEAKING: rgbLed.setPixelColor(0, rgbLed.Color(0, pulse, 0)); break;
                case AVATAR_SLEEP: rgbLed.setPixelColor(0, rgbLed.Color(pulse / 8, 0, pulse / 5)); break;
                case AVATAR_ERROR: rgbLed.setPixelColor(0, (now / 250 % 2) ? rgbLed.Color(150, 0, 0) : 0); break;
            }
        }
        rgbLed.show();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
// Serial console: type "help"
// ============================================================

static void printStatus() {
    const SettingsData s = gSettings.get();
    Serial.printf("fw %s | state %d | wifi %s | storage %s | agent \"%s\" voice %s | model %s\n", FIRMWARE_VERSION,
                  (int)agent.state(), agent.wifiConnected() ? "up" : "down", storage.kindName(), s.agentName,
                  s.voice, liveModelId(s.model));
    Serial.printf("heap %u KB (min %u KB) | psram %u KB free | codec %s | touch %s | volume %d | mic %.4f\n",
                  ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024, ESP.getFreePsram() / 1024,
                  audio.codecReady() ? "ok" : "FAIL", touch.ok() ? "ok" : "FAIL", audio.volume(), audio.micLevel());
}

// ---- Machine-readable replies for the PC app (tools/pc_app): one line "<TAG> <json>" ----

// One Serial.write per line: HWCDC holds its TX lock for a whole write, so log lines printed by
// other tasks cannot land in the middle of the JSON (chunked writes let them, breaking the PC app).
static void printTagged(const char* tag, const String& json) {
    String line;
    line.reserve(strlen(tag) + json.length() + 3);
    line += tag;
    line += ' ';
    line += json;
    line += '\n';
    Serial.write((const uint8_t*)line.c_str(), line.length());
}

static void printTagged(const char* tag, const JsonDocument& doc) {
    String s;
    serializeJson(doc, s);
    printTagged(tag, s);
}

static void printConfig() {
    const SettingsData s = gSettings.get();
    JsonDocument d;
    d["fw"] = FIRMWARE_VERSION;
    d["ssid"] = s.wifiSsid;
    d["passSet"] = s.wifiPass[0] != '\0';  // secrets are never printed
    d["keySet"] = gSettings.hasApiKey();
    // enough to recognise which key is stored without printing it
    const size_t keyLen = strlen(s.apiKey);
    d["keyLen"] = (uint32_t)keyLen;
    d["keyHint"] = keyLen >= 12 ? String(s.apiKey).substring(0, 3) + "..." + String(s.apiKey + keyLen - 4) : String();
    d["agent"] = s.agentName;
    d["user"] = s.userName;
    d["voice"] = s.voice;
    d["profile"] = s.profile;
    d["gender"] = persona::genderId((persona::Gender)s.gender);
    d["personality"] = persona::kPersonalities[s.personality % persona::kPersonalityCount].id;
    d["eye"] = persona::kEyeColors[s.eyeColor % persona::kEyeColorCount].id;
    d["model"] = s.model;
    d["thinking"] = s.thinking;
    d["textmodel"] = s.textModel;
    d["volume"] = s.volume;
    d["brightness"] = s.brightness;
    d["sleep"] = s.sleepMinutes;
    d["clock"] = s.clockMinutes;
    d["dimlevel"] = s.dimBrightness;
    d["rotation"] = s.rotation;
    d["invert"] = s.lcdInvert;
    d["consolidate"] = s.consolidate;
    d["search"] = s.googleSearch;
    d["micgain"] = s.micGain;
    d["finance"] = s.financeTools;
    d["secretsSealed"] = gSettings.secretsSealed();      // AES-GCM on flash (SecretBox)
    d["secretsMigratedAtBoot"] = gSettings.migratedAtBoot();
    d["secretsStore"] = gSettings.secretsStore();         // "sd" (agent/secrets.json) or "nvs"
    d["setupFileImported"] = gSettings.importedSetupFile();
    d["secretsMovedFromNvs"] = gSettings.movedFromNvs();
    d["touchCal"] = s.touchCal;
    printTagged("CONFIG", d);
}

static void printOptions() {
    JsonDocument d;
    JsonArray profiles = d["profiles"].to<JsonArray>();
    for (size_t i = 0; i < persona::kProfileCount; i++) {
        const persona::Profile& p = persona::kProfiles[i];
        JsonObject o = profiles.add<JsonObject>();
        o["id"] = p.id;
        o["name"] = p.agentName;
        o["voice"] = p.voice;
        o["gender"] = persona::genderId(p.gender);
        o["personality"] = p.personality;
        o["eye"] = persona::kEyeColors[p.eyeColor % persona::kEyeColorCount].id;
        o["summary"] = p.summaryTh;
    }
    JsonArray voices = d["voices"].to<JsonArray>();
    for (size_t i = 0; i < persona::kVoiceCount; i++) {
        const persona::VoiceInfo& v = persona::kVoices[i];
        JsonObject o = voices.add<JsonObject>();
        o["name"] = v.name;
        o["gender"] = persona::genderId(v.gender);
        o["style"] = v.styleTh;
        o["styleEn"] = v.styleEn;
    }
    JsonArray pers = d["personalities"].to<JsonArray>();
    for (size_t i = 0; i < persona::kPersonalityCount; i++) {
        JsonObject o = pers.add<JsonObject>();
        o["id"] = persona::kPersonalities[i].id;
        o["name"] = persona::kPersonalities[i].nameTh;
    }
    JsonArray eyes = d["eyes"].to<JsonArray>();
    for (size_t i = 0; i < persona::kEyeColorCount; i++) {
        const persona::EyeColor& e = persona::kEyeColors[i];
        char rgb[8];
        snprintf(rgb, sizeof(rgb), "#%02X%02X%02X", e.r, e.g, e.b);
        JsonObject o = eyes.add<JsonObject>();
        o["id"] = e.id;
        o["name"] = e.nameTh;
        o["rgb"] = rgb;
    }
    JsonArray genders = d["genders"].to<JsonArray>();
    for (uint8_t g = 0; g < persona::kGenderCount; g++) {
        JsonObject o = genders.add<JsonObject>();
        o["id"] = persona::genderId((persona::Gender)g);
        o["name"] = persona::genderNameTh((persona::Gender)g);
    }
    JsonArray live = d["liveModels"].to<JsonArray>();
    for (uint8_t m = 0; m < (uint8_t)LiveModel::Count; m++) {
        JsonObject o = live.add<JsonObject>();
        o["id"] = liveModelId(m);
        o["label"] = liveModelLabel(m);
    }
    JsonArray text = d["textModels"].to<JsonArray>();
    for (uint8_t m = 0; m < 2; m++) text.add(textModelId(m));
    JsonArray thinking = d["thinking"].to<JsonArray>();
    for (uint8_t t = 0; t < 3; t++) thinking.add(thinkingLevelId(t));
    printTagged("OPTIONS", d);
}

static void printWifiScan() {
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks(false, false);
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject o = a.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    printTagged("WIFISCAN", d);
}

static int indexOrFind(const String& v, int (*find)(const char*)) {
    if (v.length() && isDigit(v[0])) return v.toInt();
    return find(v.c_str());
}

// set <field> <value> — returns false for an unknown field / invalid value
static bool handleSet(const String& field, const String& value) {
    static const char* const kFields[] = {"ssid", "pass", "key", "agent", "user", "voice", "profile",
                                          "personality", "gender", "eye", "model", "thinking", "textmodel",
                                          "volume", "brightness", "sleep", "clock", "dimlevel", "rotation", "invert", "consolidate",
                                          "search", "micgain", "touch", "finance"};
    bool known = false;
    for (const char* f : kFields) known |= field == f;
    if (!known) return false;
    if (field == "voice" && !persona::findVoice(value.c_str())) return false;
    if (field == "profile" && !persona::findProfile(value.c_str())) return false;
    const int pers = field == "personality" ? indexOrFind(value, persona::findPersonality) : 0;
    const int eye = field == "eye" ? indexOrFind(value, persona::findEyeColor) : 0;
    if (pers < 0 || pers >= (int)persona::kPersonalityCount || eye < 0 || eye >= (int)persona::kEyeColorCount) return false;

    uint8_t rotation = 0;
    int invert = -1;
    const bool on = value == "1" || value == "on" || value == "true";
    gSettings.update([&](SettingsData& d) {
        auto copy = [&](char* dst, size_t n) {  // never half a Thai character (invalid UTF-8 in the prompt)
            const size_t len = utf8::clampToBoundary(value.c_str(), value.length(), n - 1);
            memcpy(dst, value.c_str(), len);
            dst[len] = '\0';
        };
        auto markCustom = [&]() { strncpy(d.profile, "custom", sizeof(d.profile)); };
        if (field == "ssid") copy(d.wifiSsid, sizeof(d.wifiSsid));
        else if (field == "pass") copy(d.wifiPass, sizeof(d.wifiPass));
        else if (field == "key") copy(d.apiKey, sizeof(d.apiKey));
        else if (field == "agent") copy(d.agentName, sizeof(d.agentName));
        else if (field == "user") copy(d.userName, sizeof(d.userName));
        else if (field == "voice") {
            strncpy(d.voice, persona::findVoice(value.c_str())->name, sizeof(d.voice) - 1);
            markCustom();
        } else if (field == "profile") Settings::applyProfile(d, value.c_str());
        else if (field == "personality") {
            d.personality = (uint8_t)pers;
            markCustom();
        } else if (field == "gender") {
            d.gender = isDigit(value[0]) ? (uint8_t)constrain(value.toInt(), 0, (int)persona::kGenderCount - 1)
                                         : (uint8_t)persona::parseGender(value.c_str());
            markCustom();
        } else if (field == "eye") d.eyeColor = (uint8_t)eye;
        else if (field == "model") d.model = (uint8_t)constrain(value.toInt(), 0, (int)LiveModel::Count - 1);
        else if (field == "thinking") d.thinking = (uint8_t)constrain(value.toInt(), 0, 2);
        else if (field == "textmodel") d.textModel = (uint8_t)constrain(value.toInt(), 0, 1);
        else if (field == "volume") d.volume = (uint8_t)constrain(value.toInt(), 0, 100);
        else if (field == "brightness") d.brightness = (uint8_t)constrain(value.toInt(), 5, 100);
        else if (field == "sleep") d.sleepMinutes = (uint8_t)constrain(value.toInt(), 0, 120);
        else if (field == "clock") d.clockMinutes = (uint8_t)constrain(value.toInt(), 0, 120);
        else if (field == "dimlevel") d.dimBrightness = (uint8_t)constrain(value.toInt(), 1, 60);
        else if (field == "rotation") rotation = d.rotation = value.toInt() == 3 ? 3 : 1;
        else if (field == "invert") invert = d.lcdInvert = on;
        else if (field == "consolidate") d.consolidate = on;
        else if (field == "search") d.googleSearch = on;
        else if (field == "micgain") d.micGain = (uint8_t)constrain(value.toInt(), 0, 7);
        else if (field == "finance") d.financeTools = on;
        else if (field == "touch") d.touchMap = (uint8_t)constrain(value.toInt(), 0, 7);
    });
    if (field == "search" && on) agent.retrySearch();  // try Google Search again now, not in 6 h
    if (rotation) {  // the LCD belongs to the UI task
        UiCommand c{};
        c.type = UiCommand::Rotate;
        c.x0 = rotation;
        postUi(c);
    }
    if (invert >= 0) {
        UiCommand c{};
        c.type = UiCommand::Invert;
        c.x0 = invert;
        postUi(c);
    }
    return true;
}

static void handleConsoleLine(String line) {
    line.trim();
    if (line.isEmpty()) return;
    const int sp = line.indexOf(' ');
    const String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? String() : line.substring(sp + 1);
    rest.trim();

    if (cmd == "help") {
        Serial.println("status | audio | tone [hz] [ms] | mic [sec] | vol <0-100> | i2c | regs | reg <r> [v]");
        Serial.println("tool <name> [json] | say <u|a|s> <text> | expr <emotion> | sleep | powersave | wake | screenshot | radio [<n>|<url> [name]|stop]");
        Serial.println("ask <text> (type to the assistant) | mute (toggle mic) | retry | new (fresh session) | memclear <facts|history>");
        Serial.println("rec [sec] (record what the mic sends to Gemini: RECINFO <json> + RECORD <rate> <bytes> + PCM + END)");
        Serial.println("ui tap x y | ui long x y | ui drag x0 y0 x1 y1 | ui open <menu|wifi|apikey|agent|user|profile|voice|info>");
        Serial.println("ui type <text> | ui back | ui home");
        Serial.println("set <ssid|pass|key|agent|user|voice|profile|personality|gender|eye|model|thinking|textmodel|");
        Serial.println("     volume|brightness|sleep|clock|dimlevel|rotation|invert|consolidate|search|micgain(0-7)|touch|finance> <value>");
        Serial.println("PC app: config | options | tools | diag | wifiscan | apitest  (reply: '<TAG> <json>')");
        Serial.println("diagnostics: keymode <query|header> | setupmode <full|nosearch|min|searchonly> | searchretry");
        Serial.println("time <unix-epoch> (set clock offline) | reboot");
        Serial.println("radio list: radiolist | radioadd name|url|genre | radioedit <n> name|url|genre | radiodel <n> | radiomove <from> <to> | radioreset | radio <n>");
    } else if (cmd == "config") {
        printConfig();
    } else if (cmd == "options") {
        printOptions();
    } else if (cmd == "tools") {
        String s;
        agent.describeTools(s);
        printTagged("TOOLS", s);
    } else if (cmd == "diag") {
        String r;
        printTagged("DIAG", agent.runTool("_diag", "{}", r) ? r : String("{\"error\":\"busy\"}"));
    } else if (cmd == "wifiscan") {
        printWifiScan();
    } else if (cmd == "searchretry") {
        agent.retrySearch();
    } else if (cmd == "rec") {
        agent.requestRecording((uint8_t)(rest.isEmpty() ? 5 : rest.toInt()));  // reply: RECINFO + RECORD
    } else if (cmd == "ask") {
        Serial.println(agent.ask(rest.c_str()) ? "OK ask" : "ERR ask: empty or queue full");
    } else if (cmd == "keymode") {
        if (rest == "header" || rest == "query") agent.setKeyInHeader(rest == "header");
        else Serial.println("keymode <query|header>  (how the Live API key is sent; not saved)");
    } else if (cmd == "setupmode") {
        if (rest == "full") agent.setSetupMode(0);
        else if (rest == "nosearch") agent.setSetupMode(1);
        else if (rest == "min") agent.setSetupMode(2);
        else if (rest == "searchonly") agent.setSetupMode(3);
        else Serial.println("setupmode <full|nosearch|min|searchonly>  (diagnostic Live API setup; not saved)");
    } else if (cmd == "apitest") {
        agent.uiApiTest();  // reply: "APITEST <json>" a few seconds later
    } else if (cmd == "mute") {
        agent.uiToggleMic();
    } else if (cmd == "retry") {
        agent.requestRetry();
    } else if (cmd == "new") {
        agent.uiNewSession();
    } else if (cmd == "memclear") {
        if (rest == "facts") agent.uiClearMemory();
        else if (rest == "history") agent.uiClearHistory();
        else Serial.println("memclear <facts|history>");
    } else if (cmd == "status") {
        printStatus();
    } else if (cmd == "audio") {
        Serial.printf("i2s rx blocks %lu | raw L %.5f R %.5f | slot %s | mic16k %.5f | playing %d\n",
                      (unsigned long)audio.inBlocks(), audio.rawLevelLeft(), audio.rawLevelRight(),
                      audio.micOnRight() ? "R" : "L", audio.micLevel(), audio.isPlaying());
    } else if (cmd == "tone") {
        int hz = 1000, ms = 500;
        sscanf(rest.c_str(), "%d %d", &hz, &ms);
        audio.playTone((uint16_t)hz, (uint16_t)ms);
    } else if (cmd == "mic") {
        const int sec = rest.isEmpty() ? 3 : rest.toInt();
        float peak = 0;
        for (int i = 0; i < sec * 10; i++) {
            const float l = audio.micLevel();
            peak = l > peak ? l : peak;
            Serial.printf("%.4f %.*s\n", l, (int)min(60.0f, l * 400), "############################################################");
            delay(100);
        }
        Serial.printf("peak %.4f\n", peak);
    } else if (cmd == "vol") {
        String r;
        agent.runTool("set_volume", (String("{\"level\":") + rest.toInt() + "}").c_str(), r);
        Serial.println(r);
    } else if (cmd == "i2c") {
        for (uint8_t a = 1; a < 127; a++) {
            if (i2c.probe(a)) Serial.printf("i2c device at 0x%02X\n", a);
        }
    } else if (cmd == "reg") {
        unsigned r = 0, v = 0;
        const int n = sscanf(rest.c_str(), "%x %x", &r, &v);
        if (n == 2) audio.codec().write((uint8_t)r, (uint8_t)v);
        if (n >= 1) Serial.printf("ES8311[0x%02X] = 0x%02X\n", r, audio.codec().read((uint8_t)r));
    } else if (cmd == "regs") {
        for (unsigned r = 0; r <= 0x45; r++) {
            Serial.printf("%02X:%02X%s", r, audio.codec().read((uint8_t)r), (r % 8 == 7) ? "\n" : " ");
        }
        Serial.println();
    } else if (cmd == "tool") {
        const int s2 = rest.indexOf(' ');
        const String name = s2 < 0 ? rest : rest.substring(0, s2);
        const String args = s2 < 0 ? String("{}") : rest.substring(s2 + 1);
        String result;
        if (!agent.runTool(name.c_str(), args.c_str(), result)) result = "{\"error\":\"busy\"}";
        printTagged((String("TOOL ") + name).c_str(), result);
    } else if (cmd == "say") {
        const char role = rest.length() ? rest[0] : 's';
        gFace.pushTranscript(role, rest.length() > 2 ? rest.c_str() + 2 : "");
    } else if (cmd == "expr") {
        String r;
        agent.runTool("set_expression", (String("{\"emotion\":\"") + rest + "\"}").c_str(), r);
        Serial.println(r);
    } else if (cmd == "sleep") {
        String r;
        agent.runTool("go_to_sleep", "{}", r);
        Serial.println(r);
    } else if (cmd == "radio") {  // radio <url> [name] | radio stop | radio (status)
        if (rest == "stop") {
            gRadio.stop();
        } else if (rest.length() && rest.toInt() > 0) {  // number in the station list
            Serial.println(gStations.play((size_t)rest.toInt() - 1) ? "OK radio" : "ERR radio");
        } else if (rest.startsWith("http")) {
            const int s2 = rest.indexOf(" ");
            const String url = s2 < 0 ? rest : rest.substring(0, s2);
            const String name = s2 < 0 ? String("console") : rest.substring(s2 + 1);
            Serial.println(gRadio.play(url.c_str(), name.c_str()) ? "OK radio" : "ERR radio");
        } else {
            char rn[64], rt[128];
            gRadio.info(rn, sizeof(rn), rt, sizeof(rt));
            Serial.printf("radio %s | %s | %s | %s %lu Hz | buffered %u ms | %s\n", gRadio.stateName(), rn, rt, gRadio.codec(),
                          (unsigned long)gRadio.sampleRate(), (unsigned)(audio.radioBuffered() / 24), gRadio.lastError());
        }
    } else if (cmd.startsWith("radio")) {  // station list (ESP32-AI Studio radio tab); numbers are 1-based
        const char* err = nullptr;
        RadioStation st;
        const int sp2 = rest.indexOf(' ');
        if (cmd == "radioadd") {
            err = RadioStations::parse(rest, st);
            if (!err) err = gStations.add(st);
        } else if (cmd == "radioedit") {
            err = sp2 < 0 ? "radioedit <n> name|url|genre" : RadioStations::parse(rest.substring(sp2 + 1), st);
            if (!err) err = gStations.update((size_t)(rest.toInt() - 1), st);
        } else if (cmd == "radiodel") {
            if (!gStations.remove((size_t)(rest.toInt() - 1))) err = "ไม่มีสถานีลำดับนี้";
        } else if (cmd == "radiomove") {
            if (sp2 < 0 || !gStations.move((size_t)(rest.toInt() - 1), (size_t)(rest.substring(sp2 + 1).toInt() - 1))) {
                err = "radiomove <from> <to>";
            }
        } else if (cmd == "radioreset") {
            gStations.resetDefaults();
        } else if (cmd != "radiolist") {
            err = "unknown radio command";
        }
        if (cmd != "radiolist") Serial.println(err ? String("ERR ") + err : String("OK"));
        JsonDocument d;
        gStations.toJson(d["stations"].to<JsonArray>());
        char rn[64];
        gRadio.info(rn, sizeof(rn), nullptr, 0);
        d["playing"] = gRadio.active() ? gStations.indexOf(rn) + 1 : 0;
        d["state"] = gRadio.stateName();
        d["max"] = (int)RadioStations::kMax;
        if (err) d["error"] = err;
        printTagged("RADIOLIST", d);
    } else if (cmd == "powersave") {
        agent.requestPowerSave();  // sleep now + dim clock face (voice or a tap wakes it)
    } else if (cmd == "wake") {
        agent.requestWake();
    } else if (cmd == "ui") {
        UiCommand c{};
        const int s2 = rest.indexOf(' ');
        const String sub = s2 < 0 ? rest : rest.substring(0, s2);
        const String arg = s2 < 0 ? String() : rest.substring(s2 + 1);
        int a = 0, b = 0, x = 0, y = 0;
        sscanf(arg.c_str(), "%d %d %d %d", &a, &b, &x, &y);
        c.x0 = a, c.y0 = b, c.x1 = x, c.y1 = y;
        if (sub == "tap") c.type = UiCommand::Tap;
        else if (sub == "long") c.type = UiCommand::Long;
        else if (sub == "drag") c.type = UiCommand::Drag;
        else if (sub == "back") c.type = UiCommand::Back;
        else if (sub == "home") c.type = UiCommand::Home;
        else if (sub == "open" || sub == "type") {
            c.type = sub == "open" ? UiCommand::Open : UiCommand::Type;
            strncpy(c.text, arg.c_str(), sizeof(c.text) - 1);
        } else {
            Serial.println("ui: tap|long|drag|open|type|back|home");
            return;
        }
        postUi(c);
    } else if (cmd == "set") {
        const int s2 = rest.indexOf(' ');
        if (s2 < 0) {
            Serial.println("set <field> <value>");
            return;
        }
        const String field = rest.substring(0, s2);
        if (handleSet(field, rest.substring(s2 + 1))) Serial.printf("OK set %s (saved to NVS)\n", field.c_str());
        else Serial.printf("ERR set %s: unknown field or invalid value\n", field.c_str());
    } else if (cmd == "screenshot") {
        UiCommand c{};
        c.type = UiCommand::Shot;
        postUi(c);  // taken by the UI task between frames (no tearing)
    } else if (cmd == "time") {
        // time <unix epoch> : set the clock without NTP (offline testing)
        struct timeval tv = {(time_t)strtoul(rest.c_str(), nullptr, 10), 0};
        settimeofday(&tv, nullptr);
        setenv("TZ", TIME_TZ, 1);
        tzset();
        Serial.printf("clock set to %lu\n", (unsigned long)tv.tv_sec);
    } else if (cmd == "reboot") {
        agent.uiRestart();  // closes the Live session cleanly, then restarts
        // fallback if the agent task is stuck - after its own worst case (close handshake 3.5 s +
        // memory flush 2 s + 0.2 s): 4 s cut it short, leaving a session open on the server
        delay(8000);
        ESP.restart();
    } else {
        Serial.println("unknown command - type help");
    }
}

static void pollConsole() {
    static String buf;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (buf.length()) handleConsoleLine(buf);
            buf = "";
        } else if (buf.length() < 1500) {
            buf += c;
        }
    }
}

// ============================================================
// Setup & loop
// ============================================================

// The prebuilt Arduino SDK has CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC: every TLS link (Gemini Live, web
// tools, REST probe, consolidation) took ~40 KB of internal RAM, and two at once failed with
// mbedtls_ssl_setup -0x7F00 (alloc failed). Buffers go to PSRAM; small objects stay internal (fast).
static void* tlsCalloc(size_t n, size_t size) {
    const size_t bytes = n * size;
    void* p = bytes >= 512 ? heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
    return p;
}

void setup() {
    mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);  // before any TLS connection
    Serial.setRxBufferSize(2048);  // console lines with Thai JSON exceed the 256-byte default
    Serial.setTxBufferSize(4096);  // whole "<TAG> <json>" replies fit without blocking
    Serial.setTxTimeoutMs(500);    // a reading host keeps up; a stalled one cannot block a task long
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);

    Serial.println("========================================");
    Serial.printf("  ESP32-AI Personal Assistant v%s\n", FIRMWARE_VERSION);
    Serial.println("  Board: ES3C28P (ESP32-S3-N16R8)");
    Serial.println("========================================");
    Serial.printf("[PSRAM] %u KB\n", ESP.getPsramSize() / 1024);

    uiQueue = xQueueCreate(8, sizeof(UiCommand*));
    WiFi.persistent(false);  // the driver must not keep its own plain-text copy of SSID + password in NVS
    gSettings.begin();       // secrets are sealed with SecretBox (and old plain copies wiped once)
    const SettingsData cfg = gSettings.get();

    i2c.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_I2C_FREQ);

    lcd.init();
    lcd.fillScreen(0x0000);
    gUi.begin(&lcd, cfg.rotation, &agent);
    gUi.setInvert(cfg.lcdInvert);
    gUi.setBrightness(cfg.brightness);
    gUi.setTouchTransform(cfg.touchMap);
    if (cfg.touchCal) {
        TouchCalibration cal;
        cal.valid = true;
        cal.xAxis = cfg.calXAxis;
        cal.yAxis = cfg.calYAxis;
        cal.xScale = cfg.calXs;
        cal.xOffset = cfg.calXo;
        cal.yScale = cfg.calYs;
        cal.yOffset = cfg.calYo;
        cal.rotation = cfg.calRot;
        gUi.setCalibration(cal);
    }
    gFace.begin([]() { settingsui::openMenu(); }, []() { gUi.push(&gRadioUi); });
    gRadioUi.begin(&audio);
    gCard.begin();
    gUi.push(&gFace);
    if (!cfg.touchCal) settingsui::openCalibration();  // first boot: fix the touch axes
    gUi.frame(millis());

    touch.begin(&i2c, TOUCH_RST, TOUCH_INT);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    rgbLed.begin();
    rgbLed.setBrightness(50);
    rgbLed.setPixelColor(0, rgbLed.Color(50, 50, 50));
    rgbLed.show();
    analogReadResolution(12);
    analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);

    storage.begin();
    // Wi-Fi password / API key: sealed on the SD card when there is one (else NVS);
    // also imports ai-setup.txt from the card root. Before agent.begin(), which reads the settings.
    gSettings.attachSecretStore(storage.kind() == StorageKind::SdCard ? "/sdcard" : nullptr);
    if (!audio.begin(&i2c)) gFace.setSystemText("ระบบเสียงผิดพลาด (ES8311 / I2S) ดู Serial log", 0);
    gStations.begin(storage.kind() != StorageKind::None ? storage.agentDir() : "");  // radio_stations.json
    gRadio.begin(&audio);  // Internet radio task (idle until play_radio)
    agent.begin(&audio, &gFace, &storage);

    if (audio.i2sReady()) audio.startTasks();
    agent.startTask();
    xTaskCreatePinnedToCore(uiTask, "ui", 10240, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(ledTask, "led", 3072, nullptr, 1, nullptr, 0);

    audio.playTone(660, 90, 3000);
    audio.playTone(990, 120, 3000);
    printStatus();
    Serial.println("[Setup] Complete - type 'help' for console commands");
}

void loop() {
    pollConsole();
    static uint32_t lastReport = 0;
    if (millis() - lastReport > 60000) {
        lastReport = millis();
        Serial.printf("[Health] heap %u KB (min %u KB), psram %u KB, state %d\n", ESP.getFreeHeap() / 1024,
                      ESP.getMinFreeHeap() / 1024, ESP.getFreePsram() / 1024, (int)agent.state());
    }
    vTaskDelay(pdMS_TO_TICKS(20));
}
