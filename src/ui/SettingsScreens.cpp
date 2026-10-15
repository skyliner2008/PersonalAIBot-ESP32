/**
 * @file SettingsScreens.cpp
 * @brief Settings menu and pickers built on ListScreen / KeyboardScreen
 */

#include "SettingsScreens.h"

#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "../agent/Personas.h"
#include "../app/Settings.h"
#include "../util/TextWrap.h"
#include "KeyboardScreen.h"
#include "ListScreen.h"

namespace {

ListScreen gMenu;
ListScreen gPicker;
ListScreen gSubPicker;
ListScreen gWifi;
ListScreen gMemory;
ConfirmScreen gConfirm;
InfoScreen gInfo;
CalibrationScreen gCalibration;

// Target positions (screen coordinates): top-left, top-right, bottom-left
const int16_t kTargets[3][2] = {{40, 44}, {280, 44}, {40, 200}};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

String maskKey(const char* key) {
    const size_t n = strlen(key);
    if (n == 0) return "ยังไม่ตั้ง";
    if (n < 10) return "****";
    return String(key).substring(0, 4) + "..." + String(key).substring(n - 4);
}

const char* sleepLabel(uint8_t m) {
    static char buf[24];
    if (m == 0) return "ไม่พัก";
    snprintf(buf, sizeof(buf), "%u นาที", m);
    return buf;
}

void markCustom(SettingsData& d) { strncpy(d.profile, "custom", sizeof(d.profile)); }

// ------------------------------------------------------------
// Pickers
// ------------------------------------------------------------

void openProfiles() {
    gPicker.setup("โปรไฟล์ผู้ช่วย", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (size_t i = 0; i < persona::kProfileCount; i++) {
            const persona::Profile& p = persona::kProfiles[i];
            ListScreen::Item& it = l.add(p.agentName, p.summaryTh, [id = p.id]() {
                gSettings.update([&](SettingsData& d) { Settings::applyProfile(d, id); });
                gUi.pop();
            });
            it.checked = !strcmp(s.profile, p.id);
            const persona::EyeColor& c = persona::kEyeColors[p.eyeColor];
            it.hasSwatch = true;
            it.swatch = rgb565(c.r, c.g, c.b);
        }
    });
    gUi.push(&gPicker);
}

void openVoices() {
    gPicker.setup("เสียงพูด (30 เสียง)", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        int sel = 0;
        for (size_t i = 0; i < persona::kVoiceCount; i++) {
            const persona::VoiceInfo& v = persona::kVoices[i];
            String value = String(v.gender == persona::kFemale ? "หญิง • " : "ชาย • ") + v.styleTh;
            ListScreen::Item& it = l.add(v.name, value, [name = v.name]() {
                gSettings.update([&](SettingsData& d) {
                    strncpy(d.voice, name, sizeof(d.voice) - 1);
                    markCustom(d);
                });
                gUi.pop();
            });
            if (!strcasecmp(s.voice, v.name)) {
                it.checked = true;
                sel = (int)i;
            }
        }
        l.reveal(sel);
    });
    gUi.push(&gPicker);
}

void openPersonalities() {
    gPicker.setup("นิสัย", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (size_t i = 0; i < persona::kPersonalityCount; i++) {
            ListScreen::Item& it = l.add(persona::kPersonalities[i].nameTh, "", [i]() {
                gSettings.update([&](SettingsData& d) {
                    d.personality = (uint8_t)i;
                    markCustom(d);
                });
                gUi.pop();
            });
            it.checked = s.personality == i;
        }
    });
    gUi.push(&gPicker);
}

void openGender() {
    gPicker.setup("เพศ / คำลงท้าย", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (uint8_t g = 0; g < persona::kGenderCount; g++) {
            ListScreen::Item& it = l.add(persona::genderNameTh((persona::Gender)g), "", [g]() {
                gSettings.update([&](SettingsData& d) {
                    d.gender = g;
                    markCustom(d);
                });
                gUi.pop();
            });
            it.checked = s.gender == g;
        }
    });
    gUi.push(&gPicker);
}

void openEyeColors() {
    gPicker.setup("สีดวงตา", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (size_t i = 0; i < persona::kEyeColorCount; i++) {
            const persona::EyeColor& c = persona::kEyeColors[i];
            ListScreen::Item& it = l.add(c.nameTh, "", [i]() {
                gSettings.update([&](SettingsData& d) { d.eyeColor = (uint8_t)i; });
                gUi.pop();
            });
            it.checked = s.eyeColor == i;
            it.hasSwatch = true;
            it.swatch = rgb565(c.r, c.g, c.b);
        }
    });
    gUi.push(&gPicker);
}

void openModels() {
    gPicker.setup("โมเดล AI (Live API)", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const char* const notes[] = {"แนะนำ เร็ว ใช้ tool แบบรอผล", "คิดลึก tool แบบ async",
                                            "รุ่นเก่า ไม่มี proactive audio"};
        for (uint8_t m = 0; m < (uint8_t)LiveModel::Count; m++) {
            ListScreen::Item& it = l.add(liveModelLabel(m), notes[m], [m]() {
                gSettings.update([&](SettingsData& d) { d.model = m; });
                gUi.pop();
            });
            it.checked = s.model == m;
        }
    });
    gUi.push(&gPicker);
}

void openNumberPicker(const char* title, int from, int to, int step, int current, const char* suffix,
                      std::function<void(int)> apply) {
    gPicker.setup(title, [=](ListScreen& l) {
        int sel = 0, idx = 0;
        for (int v = from; v <= to; v += step, idx++) {
            ListScreen::Item& it = l.add(String(v) + suffix, "", [=]() {
                apply(v);
                gUi.pop();
            });
            if (v == current) {
                it.checked = true;
                sel = idx;
            }
        }
        l.reveal(sel);
    });
    gUi.push(&gPicker);
}

void openSleep() {
    gPicker.setup("พักเมื่อเงียบ", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const uint8_t opts[] = {1, 3, 5, 10, 30, 0};
        for (uint8_t m : opts) {
            ListScreen::Item& it = l.add(sleepLabel(m), m ? "ประหยัดโทเคน" : "ฟังตลอด", [m]() {
                gSettings.update([&](SettingsData& d) { d.sleepMinutes = m; });
                gUi.pop();
            });
            it.checked = s.sleepMinutes == m;
        }
    });
    gUi.push(&gPicker);
}

void openClockMode() {
    gPicker.setup("โหมดนาฬิกา (หลังพัก)", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const uint8_t opts[] = {1, 3, 5, 10, 30, 0};
        static char label[6][32];
        for (int i = 0; i < 6; i++) {
            const uint8_t m = opts[i];
            if (m) snprintf(label[i], sizeof(label[i]), "หลังพัก %u นาที", m);
            else snprintf(label[i], sizeof(label[i]), "ไม่ใช้");
            ListScreen::Item& it = l.add(label[i], m ? "นาฬิกา + หรี่จอ" : "แสดงตาหลับตลอด", [m]() {
                gSettings.update([&](SettingsData& d) { d.clockMinutes = m; });
                gUi.pop();
            });
            it.checked = s.clockMinutes == m;
        }
    });
    gUi.push(&gPicker);
}

void openThinking() {
    gPicker.setup("ระดับการคิด", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const char* const labels[] = {"ต่ำ (เร็วสุด)", "กลาง", "สูง (ช้าแต่ลึก)"};
        for (uint8_t t = 0; t < 3; t++) {
            ListScreen::Item& it = l.add(labels[t], thinkingLevelId(t), [t]() {
                gSettings.update([&](SettingsData& d) { d.thinking = t; });
                gUi.pop();
            });
            it.checked = s.thinking == t;
        }
    });
    gUi.push(&gPicker);
}

void editText(const char* title, const char* current, size_t maxBytes, bool password, bool ascii,
              std::function<void(SettingsData&, const String&)> store) {
    KeyboardScreen::Options o;
    o.title = title;
    o.initial = current;
    o.maxBytes = maxBytes;
    o.password = password;
    o.asciiOnly = ascii;
    gKeyboard.open(o, [store](const String& text) { gSettings.update([&](SettingsData& d) { store(d, text); }); });
}

// ------------------------------------------------------------
// Wi-Fi
// ------------------------------------------------------------

struct Net {
    String ssid;
    int rssi;
    bool open;
};
std::vector<Net> gNets;
bool gScanning = false;
bool gScanFailed = false;

void startScan() {
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    gScanFailed = WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED;
    gScanning = !gScanFailed;
}

void askWifiPassword(const String& ssid) {
    KeyboardScreen::Options o;
    static String title;
    title = "รหัส " + ssid;
    o.title = title.c_str();
    o.maxBytes = 64;
    o.password = true;
    gKeyboard.open(o, [ssid](const String& pass) {
        gSettings.update([&](SettingsData& d) {
            strncpy(d.wifiSsid, ssid.c_str(), sizeof(d.wifiSsid) - 1);
            d.wifiSsid[sizeof(d.wifiSsid) - 1] = '\0';
            strncpy(d.wifiPass, pass.c_str(), sizeof(d.wifiPass) - 1);
            d.wifiPass[sizeof(d.wifiPass) - 1] = '\0';
        });
        gUi.pop();  // back from the network list to the menu
    });
}

void openWifi() {
    gWifi.setup("Wi-Fi", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        if (gScanning) {
            l.add("กำลังค้นหาเครือข่าย...", "", nullptr);
        } else if (gScanFailed) {
            l.add("ค้นหาไม่สำเร็จ", "แตะเพื่อลองใหม่", []() {
                startScan();
                gWifi.rebuild();
            });
        }
        for (const Net& n : gNets) {
            const char* q = n.rssi > -60 ? "แรง" : (n.rssi > -72 ? "ปานกลาง" : "อ่อน");
            String value = String(q) + (n.open ? " • เปิด" : "");
            ListScreen::Item& it = l.add(n.ssid, value, [n]() {
                if (n.open) {
                    gSettings.update([&](SettingsData& d) {
                        strncpy(d.wifiSsid, n.ssid.c_str(), sizeof(d.wifiSsid) - 1);
                        d.wifiPass[0] = '\0';
                    });
                    gUi.pop();
                } else {
                    askWifiPassword(n.ssid);
                }
            });
            it.checked = n.ssid == s.wifiSsid;
        }
        l.add("ค้นหาใหม่", "", []() {
            startScan();
            gWifi.rebuild();
        });
        l.add("ป้อนชื่อเครือข่ายเอง", "", []() {
            KeyboardScreen::Options o;
            o.title = "ชื่อ Wi-Fi (SSID)";
            o.maxBytes = 32;
            gKeyboard.open(o, [](const String& ssid) {
                if (ssid.length()) askWifiPassword(ssid);
            });
        });
    });
    gWifi.onTick = [](ListScreen& l, uint32_t) {
        if (!gScanning) return;
        const int16_t n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        gScanning = false;
        gNets.clear();
        if (n == WIFI_SCAN_FAILED) {
            gScanFailed = true;
        } else {
            for (int i = 0; i < n; i++) {
                const String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;
                bool dup = false;
                for (const Net& e : gNets) dup |= e.ssid == ssid;
                if (!dup) gNets.push_back(Net{ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
            }
            std::sort(gNets.begin(), gNets.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
            WiFi.scanDelete();
        }
        l.rebuild();
    };
    startScan();
    gUi.push(&gWifi);
}

// ------------------------------------------------------------
// Memory
// ------------------------------------------------------------

void openMemory() {
    gMemory.setup("ความจำ", [](ListScreen& l) {
        l.add("ล้างความจำระยะยาว", "facts + graph", []() {
            gConfirm.ask("ล้างความจำ", "ลบทุกอย่างที่ผู้ช่วยจำเกี่ยวกับคุณ? ย้อนกลับไม่ได้", "ลบทั้งหมด", []() {
                if (gUi.host()) gUi.host()->uiClearMemory();
            });
        }).valueColor = ui::ERR;
        l.add("ล้างประวัติบทสนทนา", "episodes", []() {
            gConfirm.ask("ล้างประวัติ", "ลบบันทึกบทสนทนาทั้งหมด? ย้อนกลับไม่ได้", "ลบทั้งหมด", []() {
                if (gUi.host()) gUi.host()->uiClearHistory();
            });
        }).valueColor = ui::ERR;
    });
    gUi.push(&gMemory);
}

// ------------------------------------------------------------
// Main menu
// ------------------------------------------------------------

void buildMenu(ListScreen& l) {
    const SettingsData s = gSettings.get();
    const persona::Profile* prof = persona::findProfile(s.profile);
    const persona::VoiceInfo* voice = persona::findVoice(s.voice);
    const persona::EyeColor& eye = persona::kEyeColors[s.eyeColor < persona::kEyeColorCount ? s.eyeColor : 0];

    l.addSubmenu("โปรไฟล์ผู้ช่วย", prof ? prof->agentName : "กำหนดเอง", openProfiles);
    l.addSubmenu("ชื่อผู้ช่วย", s.agentName, []() {
        const SettingsData d = gSettings.get();
        editText("ชื่อผู้ช่วย", d.agentName, 45, false, false, [](SettingsData& x, const String& t) {
            if (t.length()) strncpy(x.agentName, t.c_str(), sizeof(x.agentName) - 1);
        });
    });
    l.addSubmenu("ชื่อของคุณ", s.userName[0] ? s.userName : "ยังไม่ตั้ง", []() {
        const SettingsData d = gSettings.get();
        editText("ชื่อของคุณ", d.userName, 45, false, false, [](SettingsData& x, const String& t) {
            strncpy(x.userName, t.c_str(), sizeof(x.userName) - 1);
            x.userName[sizeof(x.userName) - 1] = '\0';
        });
    });
    l.addSubmenu("เสียงพูด", voice ? String(voice->name) + " • " + voice->styleTh : String(s.voice), openVoices);
    l.addSubmenu("นิสัย", persona::kPersonalities[s.personality % persona::kPersonalityCount].nameTh,
                 openPersonalities);
    l.addSubmenu("เพศ / คำลงท้าย", persona::genderNameTh((persona::Gender)s.gender), openGender);
    ListScreen::Item& eyeItem = l.addSubmenu("สีดวงตา", eye.nameTh, openEyeColors);
    eyeItem.hasSwatch = true;
    eyeItem.swatch = rgb565(eye.r, eye.g, eye.b);

    l.addSubmenu("Wi-Fi", s.wifiSsid[0] ? s.wifiSsid : "ยังไม่ตั้ง", openWifi).valueColor =
        s.wifiSsid[0] ? ui::DIM : ui::WARN;
    l.addSubmenu("API Key", maskKey(s.apiKey), []() {
        const SettingsData d = gSettings.get();
        editText("Gemini API Key", d.apiKey, 120, true, true, [](SettingsData& x, const String& t) {
            strncpy(x.apiKey, t.c_str(), sizeof(x.apiKey) - 1);
            x.apiKey[sizeof(x.apiKey) - 1] = '\0';
        });
    }).valueColor = strlen(s.apiKey) > 20 ? ui::DIM : ui::WARN;
    l.addSubmenu("โมเดล AI", liveModelLabel(s.model), openModels);
    if (s.model == (uint8_t)LiveModel::Live38Thinking) {
        l.addSubmenu("ระดับการคิด", thinkingLevelId(s.thinking), openThinking);
    }
    l.addSubmenu("ระดับเสียง", String(s.volume) + "%", []() {
        openNumberPicker("ระดับเสียง", 0, 100, 10, gSettings.get().volume / 10 * 10, "%",
                         [](int v) { gSettings.update([&](SettingsData& d) { d.volume = (uint8_t)v; }); });
    });
    l.addSubmenu("ความสว่างจอ", String(s.brightness) + "%", []() {
        openNumberPicker("ความสว่างจอ", 10, 100, 10, gSettings.get().brightness / 10 * 10, "%", [](int v) {
            gSettings.update([&](SettingsData& d) { d.brightness = (uint8_t)v; });
            gUi.setBrightness((uint8_t)v);
        });
    });
    {  // tap cycles 2..7 (12-42 dB): raise it if you must speak close to the board
        static char micLabel[24];
        snprintf(micLabel, sizeof(micLabel), "ระดับ %u/7", (unsigned)s.micGain);
        l.add("ความไวไมค์ (แตะเพื่อเพิ่ม)", micLabel, []() {
            gSettings.update([](SettingsData& d) { d.micGain = d.micGain >= 7 ? 2 : d.micGain + 1; });
            gMenu.rebuild();
        });
    }
    l.addSubmenu("พักเมื่อเงียบ", sleepLabel(s.sleepMinutes), openSleep);
    {
        static char clockLabel[24];
        if (s.clockMinutes) snprintf(clockLabel, sizeof(clockLabel), "หลังพัก %u นาที", (unsigned)s.clockMinutes);
        else snprintf(clockLabel, sizeof(clockLabel), "ไม่ใช้");
        l.addSubmenu("โหมดนาฬิกา ประหยัดไฟ", clockLabel, openClockMode);
        static char dimLabel[16];
        snprintf(dimLabel, sizeof(dimLabel), "%u%%", (unsigned)s.dimBrightness);
        l.add("แสงจอตอนเป็นนาฬิกา (แตะ)", dimLabel, []() {  // cycles 4 / 8 / 15 / 25 %
            gSettings.update([](SettingsData& d) {
                d.dimBrightness = d.dimBrightness < 8 ? 8 : d.dimBrightness < 15 ? 15 : d.dimBrightness < 25 ? 25 : 4;
            });
            gMenu.rebuild();
        });
    }
    l.add("จัดความจำอัตโนมัติ", s.consolidate ? "เปิด" : "ปิด", []() {
        gSettings.update([](SettingsData& d) { d.consolidate = !d.consolidate; });
        gMenu.rebuild();
    }).valueColor = s.consolidate ? ui::OK : ui::DIM;
    l.add("ค้นหาเว็บ (Google Search)", s.googleSearch ? "เปิด" : "ปิด", []() {
        gSettings.update([](SettingsData& d) { d.googleSearch = !d.googleSearch; });
        gMenu.rebuild();
    }).valueColor = s.googleSearch ? ui::OK : ui::DIM;
    l.addSubmenu("ความจำ", "", openMemory);
    l.addSubmenu("ปรับการแตะหน้าจอ", s.touchCal ? "ปรับแล้ว" : "ยังไม่ปรับ", []() { gUi.push(&gCalibration); });
    l.add("หมุนจอ 180°", s.rotation == 3 ? "เปิด" : "ปิด", []() {
        uint8_t rot = 1;
        gSettings.update([&](SettingsData& d) {
            d.rotation = d.rotation == 3 ? 1 : 3;
            rot = d.rotation;
        });
        gUi.setRotation(rot);
        gMenu.rebuild();
    });
    // พื้นจอควรเป็นสีดำ: ถ้าเห็นพื้นขาว/สีเพี้ยน ให้สลับค่านี้ (แผง IPS บางล็อตต้องเปิด INVON บางล็อตไม่ต้อง)
    l.add("พื้นจอเป็นสีขาว? แตะเพื่อสลับสี", s.lcdInvert ? "แบบ 1" : "แบบ 2", []() {
        bool inv = true;
        gSettings.update([&](SettingsData& d) {
            d.lcdInvert = !d.lcdInvert;
            inv = d.lcdInvert;
        });
        gUi.setInvert(inv);
        gMenu.rebuild();
    });
    l.add("ทดสอบ API key", "", []() {
        if (gUi.host()) gUi.host()->uiApiTest();
        gUi.popToRoot();  // the result appears on the face
    });
    l.add("เริ่มบทสนทนาใหม่", "", []() {
        if (gUi.host()) gUi.host()->uiNewSession();
        gUi.popToRoot();
    });
    l.addSubmenu("ข้อมูลระบบ", "", []() { gUi.push(&gInfo); });
    l.add("รีสตาร์ทเครื่อง", "", []() {
        gConfirm.ask("รีสตาร์ท", "รีสตาร์ทเครื่องตอนนี้?", "รีสตาร์ท", []() {
            if (gUi.host()) gUi.host()->uiRestart();  // closes the Live session first
            else ESP.restart();
        });
    });
}

}  // namespace

// ============================================================
// ConfirmScreen
// ============================================================

void ConfirmScreen::ask(const char* title, const char* message, const char* okLabel, std::function<void()> onYes) {
    _title = title;
    _message = message;
    _ok = okLabel;
    _onYes = onYes;
    gUi.push(this);
}

void ConfirmScreen::onTouch(const TouchEvent& e) {
    if (e.type != TouchEvent::Tap) return;
    if (UiManager::hitBack(e.x, e.y) || (e.y > 180 && e.x < 160)) {
        gUi.pop();
    } else if (e.y > 180 && e.x >= 160) {
        auto yes = _onYes;
        gUi.pop();
        if (yes) yes();
    }
}

void ConfirmScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    gUi.drawTitle(c, _title.c_str(), true);
    c.setFont(gUi.thai26());
    const auto lines = textwrap::wrap(_message.c_str(), _message.length(), ui::W - 40, [&](uint32_t cp) {
        char b[5] = {};
        utf8::encode(cp, b);
        return (int)c.textWidth(b);
    });
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::top_center);
    int y = 60;
    for (const auto& ln : lines) {
        String part = _message.substring(ln.start, ln.start + ln.len);
        c.drawString(part.c_str(), ui::W / 2, y);
        y += 32;
    }
    gUi.drawButton(c, 12, 186, 142, 44, "ยกเลิก", ui::PANEL_HI);
    gUi.drawButton(c, 166, 186, 142, 44, _ok.c_str(), 0x9800);
}

// ============================================================
// InfoScreen
// ============================================================

void InfoScreen::onEnter() { _next = 0; }

void InfoScreen::onTouch(const TouchEvent& e) {
    if (e.type == TouchEvent::Tap && UiManager::hitBack(e.x, e.y)) gUi.pop();
}

void InfoScreen::tick(uint32_t now, float dt) {
    (void)dt;
    if (now < _next) return;
    _next = now + 1000;
    if (gUi.host()) gUi.host()->uiSystemInfo(_text);
}

void InfoScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    gUi.drawTitle(c, "ข้อมูลระบบ", true);
    c.setFont(gUi.thai20());
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::top_left);
    int y = ui::kTitleH + 6, start = 0;
    while (start < (int)_text.length() && y < ui::H - 18) {
        int end = _text.indexOf('\n', start);
        if (end < 0) end = _text.length();
        String line = _text.substring(start, end);
        gUi.drawFitted(c, line.c_str(), 10, y + 10, ui::W - 20, gUi.thai20(), ui::TEXT);
        y += 23;
        start = end + 1;
    }
}

// ============================================================
// CalibrationScreen
// ============================================================

void CalibrationScreen::onEnter() {
    _index = 0;
    _haveCandidate = false;
    _doneAt = 0;
    _failed = false;
}

void CalibrationScreen::onTouch(const TouchEvent& e) {
    if (_doneAt) return;
    if (e.type == TouchEvent::Down) {
        _candidate[0] = e.rawX;
        _candidate[1] = e.rawY;
        _haveCandidate = true;
    } else if (e.type == TouchEvent::Up && _haveCandidate) {
        _haveCandidate = false;
        _raw[_index][0] = _candidate[0];
        _raw[_index][1] = _candidate[1];
        Serial.printf("[Calibrate] point %d raw %d,%d\n", _index, _candidate[0], _candidate[1]);
        if (++_index == kPoints) finish();
    }
}

void CalibrationScreen::finish() {
    // top-left -> top-right: screen X grows; top-left -> bottom-left: screen Y grows
    const int dxx = _raw[1][0] - _raw[0][0], dxy = _raw[1][1] - _raw[0][1];
    const int dyx = _raw[2][0] - _raw[0][0], dyy = _raw[2][1] - _raw[0][1];
    TouchCalibration c;
    c.xAxis = abs(dxy) > abs(dxx) ? 1 : 0;
    c.yAxis = abs(dyy) > abs(dyx) ? 1 : 0;
    const int dRawX = c.xAxis ? dxy : dxx;
    const int dRawY = c.yAxis ? dyy : dyx;
    if (c.xAxis == c.yAxis || abs(dRawX) < 60 || abs(dRawY) < 40) {
        Serial.println("[Calibrate] inconsistent points, retrying");
        _failed = true;
        _index = 0;
        return;
    }
    c.xScale = (float)(kTargets[1][0] - kTargets[0][0]) / dRawX;
    c.xOffset = kTargets[0][0] - _raw[0][c.xAxis] * c.xScale;
    c.yScale = (float)(kTargets[2][1] - kTargets[0][1]) / dRawY;
    c.yOffset = kTargets[0][1] - _raw[0][c.yAxis] * c.yScale;
    c.rotation = gSettings.get().rotation;
    c.valid = true;
    gUi.setCalibration(c);
    gSettings.update([&](SettingsData& d) {
        d.touchCal = true;
        d.calXAxis = c.xAxis;
        d.calYAxis = c.yAxis;
        d.calXs = c.xScale;
        d.calXo = c.xOffset;
        d.calYs = c.yScale;
        d.calYo = c.yOffset;
        d.calRot = c.rotation;
    });
    Serial.printf("[Calibrate] x = raw%c*%.3f%+.1f, y = raw%c*%.3f%+.1f\n", c.xAxis ? 'y' : 'x', c.xScale, c.xOffset,
                  c.yAxis ? 'y' : 'x', c.yScale, c.yOffset);
    _doneAt = millis();
}

void CalibrationScreen::tick(uint32_t now, float dt) {
    (void)dt;
    if (_doneAt && now - _doneAt > 1500) gUi.pop();
}

void CalibrationScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    c.setFont(gUi.thai26());
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::middle_center);
    if (_doneAt) {
        c.drawString("ปรับการแตะเรียบร้อย", ui::W / 2, ui::H / 2);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "แตะกลางวงกลม (%d/3)", _index + 1);
    c.drawString(buf, ui::W / 2, ui::H / 2 - 8);
    c.setFont(gUi.thai20());
    c.setTextColor(_failed ? ui::WARN : ui::DIM);
    c.drawString(_failed ? "จุดไม่สอดคล้องกัน ลองใหม่อีกครั้ง" : "ใช้ปลายนิ้วแตะให้ตรงที่สุด", ui::W / 2, ui::H / 2 + 26);
    const int tx = kTargets[_index][0], ty = kTargets[_index][1];
    const bool pulse = (millis() / 400) % 2;
    c.drawCircle(tx, ty, 16, ui::ACCENT);
    c.drawCircle(tx, ty, pulse ? 10 : 8, ui::ACCENT);
    c.drawFastHLine(tx - 22, ty, 44, ui::TEXT);
    c.drawFastVLine(tx, ty - 22, 44, ui::TEXT);
}

// ============================================================
// Public entry points
// ============================================================

namespace settingsui {

void openCalibration() { gUi.push(&gCalibration); }

void openMenu() {
    gMenu.setup("ตั้งค่า", buildMenu);
    gUi.push(&gMenu);
}

bool openPage(const char* page) {
    if (!strcmp(page, "menu")) openMenu();
    else if (!strcmp(page, "wifi")) openWifi();
    else if (!strcmp(page, "profile")) openProfiles();
    else if (!strcmp(page, "voice")) openVoices();
    else if (!strcmp(page, "info")) gUi.push(&gInfo);
    else if (!strcmp(page, "calibrate")) openCalibration();
    else if (!strcmp(page, "agent") || !strcmp(page, "user") || !strcmp(page, "apikey")) {
        openMenu();
        const SettingsData d = gSettings.get();
        if (!strcmp(page, "agent")) {
            editText("ชื่อผู้ช่วย", d.agentName, 45, false, false, [](SettingsData& x, const String& t) {
                if (t.length()) strncpy(x.agentName, t.c_str(), sizeof(x.agentName) - 1);
            });
        } else if (!strcmp(page, "user")) {
            editText("ชื่อของคุณ", d.userName, 45, false, false, [](SettingsData& x, const String& t) {
                strncpy(x.userName, t.c_str(), sizeof(x.userName) - 1);
            });
        } else {
            editText("Gemini API Key", d.apiKey, 120, true, true, [](SettingsData& x, const String& t) {
                strncpy(x.apiKey, t.c_str(), sizeof(x.apiKey) - 1);
            });
        }
    } else {
        return false;
    }
    return true;
}

}  // namespace settingsui
