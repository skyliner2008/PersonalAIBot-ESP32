/**
 * @file SettingsScreens.cpp
 * @brief Settings menu and pickers built on ListScreen / KeyboardScreen
 */

#include "SettingsScreens.h"

#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "../agent/Personas.h"
#include "../agent/WifiLink.h"
#include "../app/AppConfig.h"
#include "../app/Settings.h"
#include "../system/OtaUpdater.h"
#include "../util/TextWrap.h"
#include "KeyboardScreen.h"
#include "ListScreen.h"
#include "RadioScreen.h"
#include "DashScreen.h"
#include "../audio/RadioPlayer.h"
#include "../vehicle/Vehicle.h"

namespace {

ListScreen gMenu;
ListScreen gPicker;
ListScreen gSubPicker;
ListScreen gWifi;
ListScreen gWifiKnown;
ListScreen gMemory;
ListScreen gUpdate;
ListScreen gVehicleMenu;
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

// ---- vehicle dash (wiki 16) ----
void openVehicleSources() {
    gPicker.setup("แหล่งข้อมูลรถ", [](ListScreen& l) {
        const vehicle::SourceId cur = gVehicle.source();
        static const char* const kHint[] = {"ไม่ใช้หน้าปัด", "ทดลองดูหน้าปัด", "นิ้วคือคันเร่ง",
                                            "มอเตอร์ไซค์ Honda หัวฉีด (GPIO 43/44)", "ELM327 รุ่น BLE (รีสตาร์ท 1 ครั้ง)",
                                            "ELM327 ต่อสาย (GPIO 43/44)", "บอร์ดอื่นส่ง rpm=... (GPIO 43/44)"};
        for (uint8_t i = 0; i < (uint8_t)vehicle::SourceId::Count; i++) {
            const vehicle::SourceId id = (vehicle::SourceId)i;
            if (id == vehicle::SourceId::ObdBle && !vehicle::kBleBuilt) continue;  // not in this firmware
            ListScreen::Item& it = l.add(vehicle::sourceNameTh(id), kHint[i], [id]() {
                const char* err = gVehicle.setSource(id);
                gUi.pop();
                if (err) {
                    gConfirm.ask("แหล่งข้อมูลรถ", err, "ตกลง", nullptr);
                } else if (gVehicle.needsRestart()) {
                    gConfirm.ask("Bluetooth", "ต้องรีสตาร์ทบอร์ด 1 ครั้งเพื่อเปิด Bluetooth (จองหน่วยความจำ) รีสตาร์ทเลยไหม?",
                                 "รีสตาร์ท", []() {
                                     if (gUi.host()) gUi.host()->uiRestart();
                                 });
                }
                gVehicleMenu.rebuild();
            });
            it.checked = id == cur;
        }
    });
    gUi.push(&gPicker);
}

void openDashStyles() {
    gPicker.setup("รูปแบบหน้าปัด", [](ListScreen& l) {
        const vehicle::DashStyle cur = gVehicle.settings().style;
        static const char* const kHint[] = {"บาร์รอบด้านบน + ตัวเลขใหญ่", "วงแหวนไฟรอบเครื่อง + ความเร็วดิจิทัล",
                                            "เข็มวัดรอบ + เข็มความเร็ว"};
        for (uint8_t i = 0; i < (uint8_t)vehicle::DashStyle::Count; i++) {
            const vehicle::DashStyle st = (vehicle::DashStyle)i;
            ListScreen::Item& it = l.add(vehicle::styleNameTh(st), kHint[i], [st]() {
                gVehicle.setStyle(st);
                gUi.pop();
                gVehicleMenu.rebuild();
            });
            it.checked = st == cur;
        }
    });
    gUi.push(&gPicker);
}

void buildVehicleMenu(ListScreen& l) {
    const Vehicle::Settings vs = gVehicle.settings();
    const vehicle::DashConfig cfg = gVehicle.dashConfig();
    l.add("เปิดหน้าปัด", "", []() {
        gUi.pop();  // opened from the dash itself: back to it; from the main menu: the UI task shows it
        gVehicle.requestOpen();
    });
    l.addSubmenu("แหล่งข้อมูล", vehicle::sourceNameTh(vs.source), openVehicleSources);
    l.addSubmenu("รูปแบบหน้าปัด", vehicle::styleNameTh(vs.style), openDashStyles);
    l.add("สถานะ", gVehicle.status(), nullptr).valueColor = gVehicle.snapshot().anyFresh(millis()) ? ui::OK : ui::DIM;
    l.add("ประเภทรถ (แตะ)", vs.kind == vehicle::Kind::Car ? "รถยนต์" : "มอเตอร์ไซค์", []() {
        const Vehicle::Settings now = gVehicle.settings();
        gVehicle.setKind(now.kind == vehicle::Kind::Car ? vehicle::Kind::Motorcycle : vehicle::Kind::Car);
        gVehicleMenu.rebuild();
    });
    l.addSubmenu("รอบเตือนเปลี่ยนเกียร์", String(cfg.shiftRpm), []() {
        const vehicle::DashConfig c = gVehicle.dashConfig();
        openNumberPicker("รอบเตือนเปลี่ยนเกียร์", 2000, c.rpmMax, 250, c.shiftRpm / 250 * 250, " rpm",
                         [](int v) { gVehicle.setShiftRpm(v); gVehicleMenu.rebuild(); });
    });
    l.addSubmenu("รอบเต็มสเกล", String(cfg.rpmMax), []() {
        openNumberPicker("รอบเต็มสเกล", 4000, 16000, 1000, gVehicle.dashConfig().rpmMax / 1000 * 1000, " rpm",
                         [](int v) { gVehicle.setRpmMax(v); gVehicleMenu.rebuild(); });
    });
    if (vs.source == vehicle::SourceId::ObdBle) {
        l.add("ค้นหา adapter Bluetooth ใหม่", vs.bleAddr[0] ? vs.bleAddr : "ยังไม่พบ", []() {
            gVehicle.forgetBleAdapter();
            gVehicleMenu.rebuild();
        });
    }
}

void openVehicle() {
    gVehicleMenu.setup("หน้าปัดรถ", buildVehicleMenu);
    gUi.push(&gVehicleMenu);
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

/** The network really joined ("" = none): a remembered one while the configured one is away (T099/T102). */
String joinedSsid() {
    return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
}

/** A list showing the connection rebuilds itself when the board joins / leaves / switches network (T102). */
bool joinedChanged(uint32_t now) {
    static uint32_t lastCheck = 0;
    static String last;
    if (now - lastCheck < 1000) return false;
    lastCheck = now;
    const String j = joinedSsid();
    if (j == last) return false;
    last = j;
    return true;
}

/**
 * Networks the board joined before (T099): it switches to the strongest of them in range by itself when the
 * configured one cannot be reached. Tap = forget (the password goes with it).
 */
void openKnownWifi() {
    gWifiKnown.setup("Wi-Fi ที่จำไว้", [](ListScreen& l) {
        const wifiknown::List known = gSettings.knownWifi();
        const SettingsData s = gSettings.get();
        const String joined = joinedSsid();
        l.add("ต่อตัวที่สัญญาณแรงที่สุดเอง", "หลักใช้ไม่ได้ / มีตัวที่แรงกว่าชัดเจน", nullptr);  // T099 / T103
        for (size_t i = 0; i < known.size(); i++) {
            const String ssid = known.at(i).ssid;
            const bool primary = ssid == s.wifiSsid;
            const bool on = ssid == joined;
            String value = on ? "เชื่อมต่ออยู่ • " : "";
            value += primary ? "Wi-Fi หลัก • แตะเพื่อลืม" : "แตะเพื่อลืม";
            ListScreen::Item& it = l.add(ssid, value, [ssid]() {
                static String text;
                text = "ลืม \"" + ssid + "\" และรหัสผ่าน? ต่ออีกครั้งต้องใส่รหัสใหม่";
                gConfirm.ask("ลืม Wi-Fi", text.c_str(), "ลืม", [ssid]() {
                    gSettings.forgetWifi(ssid.c_str());
                    gWifiKnown.rebuild();
                });
            });
            it.checked = on;
        }
    });
    gUi.push(&gWifiKnown);
}

void openWifi() {
    gWifi.setup("Wi-Fi", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        // what is really joined - it is not always the configured network (T102: the menu used to tick that one)
        const String joined = joinedSsid();
        const uint8_t primaryReason = gWifiPrimaryReason.load();
        if (joined.length()) {
            l.add("เชื่อมต่ออยู่: " + joined,
                  joined == s.wifiSsid ? String("Wi-Fi หลัก") : "Wi-Fi ที่จำไว้ • หลัก \"" + String(s.wifiSsid) + "\"",
                  nullptr);
        } else if (s.wifiSsid[0]) {
            l.add("ยังไม่ได้เชื่อมต่อ", "กำลังต่อ \"" + String(s.wifiSsid) + "\"" +
                                           (primaryReason ? String(" • ") + wifiReasonTh(primaryReason) : String("")),
                  nullptr);
        }
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
            const bool remembered = gSettings.isKnownWifi(n.ssid.c_str());
            const bool primary = n.ssid == s.wifiSsid;
            const bool on = n.ssid == joined;
            const bool wrongPass = primary && !on && !n.open && wifiReasonIsAuth(primaryReason);
            String value = String(q) + (n.open ? " • เปิด" : "");
            if (on) value += " • เชื่อมต่ออยู่";
            else if (wrongPass) value += " • รหัสผ่านผิด แตะใส่ใหม่";
            else if (primary) value += " • หลัก แตะเพื่อต่อ";
            else if (remembered) value += " • จำไว้";
            ListScreen::Item& it = l.add(n.ssid, value, [n, primary, on, wrongPass]() {
                if (primary && !wrongPass && (!on || n.open)) {
                    // the configured network: join it now with its password (T102) - no settings change, so the
                    // agent is asked directly instead of waiting for its next retry / 5-minute check
                    if (!on) gWifiJoinPrimaryNow = true;
                    gUi.pop();
                    return;
                }
                if (primary) {  // wrong password, or joined and tapped again: a new password
                    askWifiPassword(n.ssid);
                    return;
                }
                const wifiknown::List saved = gSettings.knownWifi();
                const int i = saved.find(n.ssid.c_str());
                if (i >= 0) {  // joined before (T099): its saved password, no keyboard
                    gSettings.update([&](SettingsData& d) {
                        strncpy(d.wifiSsid, saved.at((size_t)i).ssid, sizeof(d.wifiSsid) - 1);
                        d.wifiSsid[sizeof(d.wifiSsid) - 1] = '\0';
                        strncpy(d.wifiPass, saved.at((size_t)i).pass, sizeof(d.wifiPass) - 1);
                        d.wifiPass[sizeof(d.wifiPass) - 1] = '\0';
                    });
                    gUi.pop();
                } else if (n.open) {
                    gSettings.update([&](SettingsData& d) {
                        strncpy(d.wifiSsid, n.ssid.c_str(), sizeof(d.wifiSsid) - 1);
                        d.wifiPass[0] = '\0';
                    });
                    gUi.pop();
                } else {
                    askWifiPassword(n.ssid);
                }
            });
            it.checked = on;
        }
        l.add("ค้นหาใหม่", "", []() {
            startScan();
            gWifi.rebuild();
        });
        const size_t known = gSettings.knownWifiCount();
        if (known) l.addSubmenu("Wi-Fi ที่จำไว้", String((unsigned)known) + " เครือข่าย", openKnownWifi);
        l.add("ป้อนชื่อเครือข่ายเอง", "", []() {
            KeyboardScreen::Options o;
            o.title = "ชื่อ Wi-Fi (SSID)";
            o.maxBytes = 32;
            gKeyboard.open(o, [](const String& ssid) {
                if (ssid.length()) askWifiPassword(ssid);
            });
        });
    });
    gWifi.onTick = [](ListScreen& l, uint32_t now) {
        if (joinedChanged(now)) l.rebuild();
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

// Firmware update from GitHub Releases (system/OtaUpdater): settings, memory and the SD card stay
void openUpdate() {
    gUpdate.setup("อัปเดตเฟิร์มแวร์", [](ListScreen& l) {
        l.add("เวอร์ชันปัจจุบัน", FIRMWARE_VERSION, nullptr);
        const OtaUpdater::State st = gOta.state();
        const String latest = gOta.latest();
        switch (st) {
            case OtaUpdater::State::Checking:
                l.add("กำลังตรวจสอบ...", "", nullptr);
                break;
            case OtaUpdater::State::UpToDate:
                l.add("เป็นเวอร์ชันล่าสุดแล้ว", latest.length() ? "ล่าสุด v" + latest : "", nullptr);
                break;
            case OtaUpdater::State::Available:
                l.add("ติดตั้ง v" + latest, "ค่าที่ตั้งไว้และความจำอยู่ครบ", []() {
                    gConfirm.ask("อัปเดตเฟิร์มแวร์",
                                 "ดาวน์โหลดและติดตั้งตอนนี้? ใช้เวลา 1-3 นาที ห้ามปิดเครื่อง ถ้าเวอร์ชันใหม่มีปัญหาจะกลับเป็นเวอร์ชันเดิมเอง",
                                 "ติดตั้ง", []() { gOta.requestInstall(); });
                }).valueColor = ui::OK;
                break;
            case OtaUpdater::State::Downloading:
                l.add("กำลังดาวน์โหลด " + String(gOta.progress()) + "%", "ห้ามปิดเครื่อง", nullptr);
                break;
            case OtaUpdater::State::Ready:
                l.add("ติดตั้งแล้ว กำลังรีสตาร์ท...", "", nullptr);
                break;
            case OtaUpdater::State::Error:
                l.add("ไม่สำเร็จ", gOta.error(), nullptr).valueColor = ui::WARN;
                break;
            default:
                break;
        }
        if (!gOta.busy() && st != OtaUpdater::State::Available) {
            l.add("ตรวจหาเวอร์ชันใหม่", "จาก GitHub", []() {
                gOta.requestCheck();
                gUpdate.rebuild();
            });
        }
        if (gOta.pendingVerify()) l.add("กำลังยืนยันเวอร์ชันใหม่", "ใช้งานปกติ 1 นาที", nullptr);
    });
    // follow the updater (state / download percentage) while the page is open
    gUpdate.onTick = [](ListScreen& l, uint32_t now) {
        static uint32_t last = 0;
        static uint8_t seenState = 255, seenPct = 255;
        if (now - last < 300) return;
        last = now;
        const uint8_t s = (uint8_t)gOta.state(), p = gOta.progress();
        if (s != seenState || p != seenPct) {
            seenState = s;
            seenPct = p;
            l.rebuild();
        }
    };
    gUi.push(&gUpdate);
}

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

    // the network really joined, not the configured one (T102: a remembered network stands in when it is away)
    const String joined = joinedSsid();
    String wifiValue = joined.length() ? joined + (joined == s.wifiSsid ? "" : " (จำไว้)")
                                       : String(s.wifiSsid[0] ? "ไม่ได้เชื่อมต่อ" : "ยังไม่ตั้ง");
    l.addSubmenu("Wi-Fi", wifiValue, openWifi).valueColor = joined.length() ? ui::DIM : ui::WARN;
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
    l.addSubmenu("วิทยุออนไลน์", gRadio.active() ? "กำลังเล่น" : "เลือกสถานี", []() { gUi.push(&gRadioUi); });
    l.addSubmenu("หน้าปัดรถ", gVehicle.enabled() ? vehicle::sourceNameTh(gVehicle.source()) : "ปิด", openVehicle);
    l.addSubmenu("เสียงวิทยุ", String(s.volume) + "%", []() {  // the voice stays at aiVolume
        openNumberPicker("เสียงวิทยุ", 0, 100, 10, gSettings.get().volume / 10 * 10, "%",
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
    {  // what wakes it by voice while asleep (wiki 14): the wake word needs the "model" partition
        static const char* const kWake[] = {"ระดับเสียง", "คำปลุก", "คำปลุก + ระดับเสียง"};
        l.add("ปลุกด้วยเสียง (แตะ)", kWake[s.wakeMode > 2 ? 2 : s.wakeMode], []() {
            gSettings.update([](SettingsData& d) { d.wakeMode = (uint8_t)((d.wakeMode + 1) % 3); });
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
    {
        const OtaUpdater::State os = gOta.state();
        String v = "v" FIRMWARE_VERSION;
        if (os == OtaUpdater::State::Available) v = "มี v" + gOta.latest();
        ListScreen::Item& up = l.addSubmenu("อัปเดตเฟิร์มแวร์", v, openUpdate);
        if (os == OtaUpdater::State::Available) up.valueColor = ui::OK;
    }
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

void InfoScreen::onEnter() { _next = millis(); }  // due now (0 read as weeks ahead after 24.8 days up)

void InfoScreen::onTouch(const TouchEvent& e) {
    if (e.type == TouchEvent::Tap && UiManager::hitBack(e.x, e.y)) gUi.pop();
}

void InfoScreen::tick(uint32_t now, float dt) {
    (void)dt;
    if ((int32_t)(now - _next) < 0) return;
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
    gMenu.onTick = [](ListScreen& l, uint32_t now) {
        if (joinedChanged(now)) l.rebuild();  // the Wi-Fi row follows the real connection
    };
    gUi.push(&gMenu);
}

bool openPage(const char* page) {
    if (!strcmp(page, "menu")) openMenu();
    else if (!strcmp(page, "wifi")) openWifi();
    else if (!strcmp(page, "profile")) openProfiles();
    else if (!strcmp(page, "voice")) openVoices();
    else if (!strcmp(page, "info")) gUi.push(&gInfo);
    else if (!strcmp(page, "update")) openUpdate();
    else if (!strcmp(page, "calibrate")) openCalibration();
    else if (!strcmp(page, "vehicle")) openVehicle();
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
