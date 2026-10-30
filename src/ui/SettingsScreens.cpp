/**
 * @file SettingsScreens.cpp
 * @brief Settings menu and pickers built on ListScreen / KeyboardScreen
 */

#include "SettingsScreens.h"

#include <WiFi.h>
#include <math.h>

#include <algorithm>
#include <vector>

#include "../agent/Personas.h"
#include "../agent/WebTools.h"
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
#include "../util/PsramAlloc.h"
#include "../vehicle/Vehicle.h"
#include "../vehicle/VehicleLog.h"

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
        static const char* const notes[] = {"แนะนำ เร็ว ใช้ tool แบบรอผล", "คิดลึก ตอนนี้ใช้ tool ไม่ได้ (Google)",
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
void openReplayFiles();

void openVehicleSources() {
    gPicker.setup("แหล่งข้อมูลรถ", [](ListScreen& l) {
        const vehicle::SourceId cur = gVehicle.source();
        // one per source (T115: the GPS one was missing - the list read past the array)
        static const char* const kHint[] = {"ไม่ใช้หน้าปัด", "ทดลองดูหน้าปัด", "นิ้วคือคันเร่ง",
                                            "มอเตอร์ไซค์ Honda หัวฉีด (GPIO 43/44)", "ELM327 รุ่น BLE (รีสตาร์ท 1 ครั้ง)",
                                            "ELM327 ต่อสาย (GPIO 43/44)", "บอร์ดอื่นส่ง rpm=... (GPIO 43/44)",
                                            "ความเร็วจากแอปมือถือ", "log ล่าสุดใน SD card"};
        static_assert(sizeof(kHint) / sizeof(kHint[0]) == (size_t)vehicle::SourceId::Count, "a hint per source");
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
                                            "เข็มวัดรอบ + เข็มความเร็ว", "9 ค่า + รหัสปัญหา + วิเคราะห์สุขภาพรถ"};
        static_assert(sizeof(kHint) / sizeof(kHint[0]) == (size_t)vehicle::DashStyle::Count, "a hint per style");
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
    const vehicle::obd::Diag diag = gVehicle.diag();
    if (diag.available) {  // OBD-II / the simulator: trouble codes (T112)
        static char codes[64];
        if (!diag.codesValid) {
            snprintf(codes, sizeof(codes), "ยังไม่ได้อ่าน");
        } else if (!diag.stored.n && !diag.pending.n) {
            snprintf(codes, sizeof(codes), "ไม่พบ");
        } else {
            int len = 0;
            for (uint8_t i = 0; i < diag.stored.n && len < 50; i++) {
                char c[6];
                vehicle::obd::formatDtc(diag.stored.code[i], c);
                len += snprintf(codes + len, sizeof(codes) - len, "%s%s", len ? " " : "", c);
            }
            if (diag.pending.n && len < 50) snprintf(codes + len, sizeof(codes) - len, "%s+%u รอยืนยัน", len ? " " : "", diag.pending.n);
        }
        ListScreen::Item& it = l.add("อ่านรหัสปัญหา (DTC)", codes, []() {
            if (!gVehicle.requestScan()) gConfirm.ask("รหัสปัญหา", "ยังไม่ได้เชื่อมต่อกับกล่อง ECU ของรถ", "ตกลง", nullptr);
            gVehicleMenu.rebuild();
        });
        it.valueColor = diag.stored.n ? ui::WARN : ui::DIM;
        if (diag.stored.n || diag.pending.n) {
            l.add("ล้างรหัสปัญหา", "ดับเครื่อง เปิดสวิตช์", []() {
                gConfirm.ask("ล้างรหัสปัญหา",
                             "ไฟเช็คเครื่องจะดับ และระบบตรวจไอเสียต้องเรียนรู้ใหม่ ถ้ายังไม่ได้ซ่อม รหัสจะกลับมา — ล้างเลยไหม?", "ล้าง",
                             []() {
                                 uint32_t seq;
                                 if (const char* why = gVehicle.requestClear(seq)) {
                                     gConfirm.ask("ล้างรหัสปัญหา", why, "ตกลง", nullptr);
                                 }
                             });
            });
        }
    }
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
    // T115: everything the OBD sources do, on the SD card - and playing a recorded drive back
    if (!gVehicleLog.available()) {
        l.add("บันทึก log OBD ลง SD", "ไม่มี SD card", nullptr).valueColor = ui::DIM;
        return;
    }
    l.add("บันทึก log OBD ลง SD", vs.log ? (gVehicleLog.active() ? "กำลังบันทึก" : "เปิด") : "ปิด", []() {
        gVehicle.setLogEnabled(!gVehicle.settings().log);
        gVehicleMenu.rebuild();
    }).valueColor = vs.log ? ui::OK : ui::DIM;
    l.addSubmenu("เล่นย้อนหลังจาก log", vs.source == vehicle::SourceId::Replay ? "กำลังเล่น" : "", openReplayFiles);
}

void openReplayFiles() {
    gPicker.setup("เล่นย้อนหลังจาก log", [](ListScreen& l) {
        constexpr size_t kShown = 15;
        auto* files = (VehicleLog::FileInfo*)psramMalloc(kShown * sizeof(VehicleLog::FileInfo));
        const size_t n = files ? gVehicleLog.list(files, kShown) : 0;
        if (!n) l.add("ยังไม่มีไฟล์ log", "ใช้หน้าปัดกับรถจริงก่อน", nullptr);
        for (size_t i = 0; i < n; i++) {
            const String name = files[i].name;
            l.add(name, String(files[i].bytes / 1024) + " KB", [name]() {
                const char* err = gVehicle.startReplay(name.c_str());
                gUi.pop();
                if (err) gConfirm.ask("เล่นย้อนหลัง", err, "ตกลง", nullptr);
                else gVehicle.requestOpen();
                gVehicleMenu.rebuild();
            });
        }
        psramFree(files);
    });
    gUi.push(&gPicker);
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

// T116: the language the assistant speaks (Thai locked by default - a quiet sentence was taken for Spanish)
void openLanguage() {
    gPicker.setup("ภาษาที่ AI พูด", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (uint8_t i = 0; i < (uint8_t)AiLanguage::Count; i++) {
            ListScreen::Item& it = l.add(aiLanguageLabel(i), aiLanguageId(i), [i]() {
                gSettings.update([&](SettingsData& d) { d.language = i; });
                gUi.pop();
            });
            it.checked = s.language == i;
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
// Screensaver Clock Themes, Weather City & Battery Mode Pickers
// ------------------------------------------------------------

void openClockThemes() {
    gPicker.setup("ธีมพักหน้าจอ", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const char* const kHints[] = {"เวลาใหญ่ + การ์ดสภาพอากาศ & PM2.5", "หน้าปัดวงแหวน 60 วิ + วัน & อากาศ",
                                             "การ์ดตัวเลขพับคู่ + แถบสภาพอากาศ", "ตาหลับคลาสสิก + เวลาใหญ่ + สรุปอากาศ",
                                             "หน้าปัดเข็ม + วันที่ & อากาศด้านข้าง", "เวลาตัวใหญ่สีฟ้าเต็มจอ + วันที่ วัน & อุณหภูมิ",
                                             "โค้งท้องฟ้า + อีกกี่ชั่วโมงตะวันขึ้น/ตก"};
        static_assert(sizeof(kHints) / sizeof(kHints[0]) == kClockThemeCount, "one hint per clock theme");
        for (uint8_t i = 0; i < kClockThemeCount; i++) {
            ListScreen::Item& it = l.add(clockThemeLabel(i), kHints[i], [i]() {
                gSettings.update([&](SettingsData& d) { d.clockTheme = i; });
                gUi.pop();
            });
            it.checked = (s.clockTheme % kClockThemeCount) == i;
        }
    });
    gUi.push(&gPicker);
}

void openClockColors() {  // T136
    gPicker.setup("สีนาฬิกา (นีออน)", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (uint8_t i = 0; i < kClockColorCount; i++) {
            ListScreen::Item& it = l.add(clockColorLabel(i), clockColorRgb565(i) ? "" : "สีเดียวกับดวงตา", [i]() {
                gSettings.update([&](SettingsData& d) { d.clockColor = i; });
                gUi.pop();
            });
            it.checked = s.clockColor == i;
            it.hasSwatch = clockColorRgb565(i) != 0;
            it.swatch = clockColorRgb565(i);
        }
    });
    gUi.push(&gPicker);
}

void openBatteryModes() {
    gPicker.setup("การแสดงแบตเตอรี่", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        static const char* const kHints[] = {"แสดง USB เมื่อไม่ต่อแบต / % เมื่อมีแบต", "แสดง % แบตจากวงจรชาร์จตลอด",
                                             "แสดงสถานะไฟ USB ตลอด"};
        for (uint8_t i = 0; i < kBatteryModeCount; i++) {
            ListScreen::Item& it = l.add(batteryModeLabel(i), kHints[i], [i]() {
                gSettings.update([&](SettingsData& d) { d.batteryMode = i; });
                gUi.pop();
            });
            it.checked = (s.batteryMode % kBatteryModeCount) == i;
        }
    });
    gUi.push(&gPicker);
}

void openRegionProvinces(const char* regionTitle, const char* const* list, size_t count) {
    gSubPicker.setup(regionTitle, [=](ListScreen& l) {
        const SettingsData s = gSettings.get();
        for (size_t i = 0; i < count; i++) {
            const char* city = list[i];
            ListScreen::Item& it = l.add(city, "", [city]() {
                gSettings.update([&](SettingsData& d) {
                    strncpy(d.weatherCity, city, sizeof(d.weatherCity) - 1);
                    d.weatherCity[sizeof(d.weatherCity) - 1] = '\0';
                });
                webtools::requestClockWeatherRefresh();
                gUi.pop();  // close region list
                gUi.pop();  // close city picker
            });
            it.checked = !strcmp(s.weatherCity, city);
        }
    });
    gUi.push(&gSubPicker);
}

void openWeatherCity() {
    gPicker.setup("เมืองสภาพอากาศ", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        ListScreen::Item& autoIt = l.add("อัตโนมัติ (ตาม IP เน็ต)", "ค้นหาตำแหน่งจากอินเทอร์เน็ต", []() {
            gSettings.update([](SettingsData& d) { d.weatherCity[0] = '\0'; });
            webtools::requestClockWeatherRefresh();
            gUi.pop();
        });
        autoIt.checked = s.weatherCity[0] == '\0';

        static const char* const kSouth[] = {"ปัตตานี", "สงขลา", "ภูเก็ต", "สุราษฎร์ธานี", "นครศรีธรรมราช", "ยะลา",
                                             "นราธิวาส", "กระบี่", "พังงา", "ตรัง", "พัทลุง", "สตูล", "ชุมพร", "ระนอง"};
        static const char* const kCentral[] = {"กรุงเทพมหานคร", "นนทบุรี", "ปทุมธานี", "สมุทรปราการ", "พระนครศรีอยุธยา",
                                               "นครปฐม", "สมุทรสาคร", "สมุทรสงคราม", "สระบุรี", "ลพบุรี", "สิงห์บุรี",
                                               "อ่างทอง", "ชัยนาท", "สุพรรณบุรี", "อุทัยธานี", "นครสวรรค์", "นครนายก"};
        static const char* const kNorth[] = {"เชียงใหม่", "เชียงราย", "ลำปาง", "ลำพูน", "แม่ฮ่องสอน", "น่าน", "พะเยา",
                                             "แพร่", "อุตรดิตถ์", "สุโขทัย", "พิษณุโลก", "พิจิตร", "กำแพงเพชร", "เพชรบูรณ์"};
        static const char* const kIsan[] = {"ขอนแก่น", "นครราชสีมา", "อุดรธานี", "อุบลราชธานี", "บุรีรัมย์", "สุรินทร์",
                                            "ศรีสะเกษ", "ร้อยเอ็ด", "มหาสารคาม", "กาฬสินธุ์", "สกลนคร", "นครพนม",
                                            "มุกดาหาร", "เลย", "หนองคาย", "หนองบัวลำภู", "บึงกาฬ", "ชัยภูมิ", "อำนาจเจริญ",
                                            "ยโสธร"};
        static const char* const kEastWest[] = {"ชลบุรี", "ระยอง", "จันทบุรี", "ตราด", "ฉะเชิงเทรา", "ปราจีนบุรี",
                                                "สระแก้ว", "กาญจนบุรี", "ราชบุรี", "เพชรบุรี", "ประจวบคีรีขันธ์", "ตาก"};

        l.addSubmenu("ภาคใต้", "ปัตตานี สงขลา ภูเก็ต...", []() {
            openRegionProvinces("ภาคใต้", kSouth, sizeof(kSouth) / sizeof(kSouth[0]));
        });
        l.addSubmenu("กรุงเทพฯ & ภาคกลาง", "กทม. นนทบุรี ปทุมธานี...", []() {
            openRegionProvinces("กรุงเทพฯ & ภาคกลาง", kCentral, sizeof(kCentral) / sizeof(kCentral[0]));
        });
        l.addSubmenu("ภาคเหนือ", "เชียงใหม่ เชียงราย พิษณุโลก...", []() {
            openRegionProvinces("ภาคเหนือ", kNorth, sizeof(kNorth) / sizeof(kNorth[0]));
        });
        l.addSubmenu("ภาคอีสาน", "ขอนแก่น โคราช อุดร อุบล...", []() {
            openRegionProvinces("ภาคอีสาน", kIsan, sizeof(kIsan) / sizeof(kIsan[0]));
        });
        l.addSubmenu("ภาคตะวันออก & ตะวันตก", "ชลบุรี ระยอง หัวหิน...", []() {
            openRegionProvinces("ตะวันออก & ตะวันตก", kEastWest, sizeof(kEastWest) / sizeof(kEastWest[0]));
        });
        l.add("พิมพ์ชื่อเมืองเอง...", s.weatherCity[0] ? s.weatherCity : "", []() {
            const SettingsData d = gSettings.get();
            KeyboardScreen::Options o;
            o.title = "ชื่อเมือง / จังหวัด";
            o.initial = d.weatherCity;
            o.maxBytes = 45;
            gKeyboard.open(o, [](const String& t) {
                gSettings.update([&](SettingsData& x) {
                    strncpy(x.weatherCity, t.c_str(), sizeof(x.weatherCity) - 1);
                    x.weatherCity[sizeof(x.weatherCity) - 1] = '\0';
                });
                webtools::requestClockWeatherRefresh();
                gUi.pop();
            });
        });
    });
    gUi.push(&gPicker);
}

// ------------------------------------------------------------
// Smartwatch Category Submenus
// ------------------------------------------------------------

void openCatPersona() {
    gMenu.setup("ผู้ช่วย & เสียงพูด", [](ListScreen& l) {
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
        l.addSubmenu("เสียงพูด (30 เสียง)", voice ? String(voice->name) + " • " + voice->styleTh : String(s.voice),
                     openVoices);
        l.addSubmenu("นิสัย", persona::kPersonalities[s.personality % persona::kPersonalityCount].nameTh,
                     openPersonalities);
        l.addSubmenu("เพศ / คำลงท้าย", persona::genderNameTh((persona::Gender)s.gender), openGender);
        ListScreen::Item& eyeItem = l.addSubmenu("สีดวงตา", eye.nameTh, openEyeColors);
        eyeItem.hasSwatch = true;
        eyeItem.swatch = rgb565(eye.r, eye.g, eye.b);
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatAi() {
    gMenu.setup("สมอง AI & ภาษา", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        l.addSubmenu("โมเดล AI", liveModelLabel(s.model), openModels);
        if (s.model == (uint8_t)LiveModel::Live38Thinking) {
            l.addSubmenu("ระดับการคิด", thinkingLevelId(s.thinking), openThinking);
        }
        l.addSubmenu("ภาษาที่ AI พูด", aiLanguageLabel(s.language), openLanguage);
        l.add("ค้นหาเว็บ (Google Search)", s.googleSearch ? "เปิด" : "ปิด", []() {
            gSettings.update([](SettingsData& d) { d.googleSearch = !d.googleSearch; });
            gMenu.rebuild();
        }).valueColor = s.googleSearch ? ui::OK : ui::DIM;
        l.add("เครื่องมือการเงิน / หุ้น", s.financeTools ? "เปิด" : "ปิด", []() {
            gSettings.update([](SettingsData& d) { d.financeTools = !d.financeTools; });
            gMenu.rebuild();
        }).valueColor = s.financeTools ? ui::OK : ui::DIM;
        l.add("เครื่องมือระดับน้ำ / ฝน", s.waterTools ? "เปิด" : "ปิด", []() {
            gSettings.update([](SettingsData& d) { d.waterTools = !d.waterTools; });
            gMenu.rebuild();
        }).valueColor = s.waterTools ? ui::OK : ui::DIM;
        l.addSubmenu("API Key", maskKey(s.apiKey), []() {
            const SettingsData d = gSettings.get();
            editText("Gemini API Key", d.apiKey, 120, true, true, [](SettingsData& x, const String& t) {
                strncpy(x.apiKey, t.c_str(), sizeof(x.apiKey) - 1);
                x.apiKey[sizeof(x.apiKey) - 1] = '\0';
            });
        }).valueColor = strlen(s.apiKey) > 20 ? ui::DIM : ui::WARN;
        l.add("ทดสอบ API Key", "", []() {
            if (gUi.host()) gUi.host()->uiApiTest();
            gUi.popToRoot();
        });
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatAudio() {
    gMenu.setup("เสียง & ไมโครโฟน", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        l.addSubmenu("ความดังเสียง AI", String(s.aiVolume) + "%", []() {
            openNumberPicker("ความดังเสียง AI", 10, 100, 10, gSettings.get().aiVolume / 10 * 10, "%",
                             [](int v) { gSettings.update([&](SettingsData& d) { d.aiVolume = (uint8_t)v; }); });
        });
        l.addSubmenu("ความดังวิทยุ / เพลง", String(s.volume) + "%", []() {
            openNumberPicker("ความดังวิทยุ / เพลง", 0, 100, 10, gSettings.get().volume / 10 * 10, "%",
                             [](int v) { gSettings.update([&](SettingsData& d) { d.volume = (uint8_t)v; }); });
        });
        static char micLabel[24];
        snprintf(micLabel, sizeof(micLabel), "ระดับ %u/7", (unsigned)s.micGain);
        l.add("ความไวไมค์ (แตะเพื่อเพิ่ม)", micLabel, []() {
            gSettings.update([](SettingsData& d) { d.micGain = d.micGain >= 7 ? 2 : d.micGain + 1; });
            gMenu.rebuild();
        });
        static const char* const kWake[] = {"ระดับเสียง", "คำปลุก", "คำปลุก + ระดับเสียง"};
        l.add("ปลุกด้วยเสียง (แตะ)", kWake[s.wakeMode > 2 ? 2 : s.wakeMode], []() {
            gSettings.update([](SettingsData& d) { d.wakeMode = (uint8_t)((d.wakeMode + 1) % 3); });
            gMenu.rebuild();
        });
        static const char* const kWords[] = {"Jarvis", "Hi ESP", "Jarvis + Hi ESP"};
        l.add("คำปลุก (แตะ • รีสตาร์ทมีผล)", kWords[s.wakeWord > 2 ? 1 : s.wakeWord], []() {
            gSettings.update([](SettingsData& d) { d.wakeWord = (uint8_t)((d.wakeWord + 1) % 3); });
            gMenu.rebuild();
        });
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatDisplay() {
    gMenu.setup("หน้าจอ & พักหน้าจอ", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        l.addSubmenu("ธีมพักหน้าจอ", clockThemeLabel(s.clockTheme), openClockThemes);
        ListScreen::Item& col = l.addSubmenu("สีนาฬิกา (นีออน)", clockColorLabel(s.clockColor), openClockColors);
        if (clockColorRgb565(s.clockColor)) col.valueColor = clockColorRgb565(s.clockColor);  // the name in its colour
        l.addSubmenu("เมืองสภาพอากาศ", s.weatherCity[0] ? s.weatherCity : "อัตโนมัติ (ตาม IP)", openWeatherCity);
        l.add("ดูตัวอย่างพักหน้าจอ", "แตะเพื่อแสดงตอนนี้", []() {
            gUi.popToRoot();
            if (gUi.host()) gUi.host()->uiPowerSave();
        }).valueColor = ui::ACCENT;
        static char clockLabel[24];
        if (s.clockMinutes) snprintf(clockLabel, sizeof(clockLabel), "หลังพัก %u นาที", (unsigned)s.clockMinutes);
        else snprintf(clockLabel, sizeof(clockLabel), "ไม่ใช้");
        l.addSubmenu("โหมดนาฬิกา ประหยัดไฟ", clockLabel, openClockMode);
        l.addSubmenu("พักเมื่อเงียบ", sleepLabel(s.sleepMinutes), openSleep);
        l.addSubmenu("ความสว่างจอ", String(s.brightness) + "%", []() {
            openNumberPicker("ความสว่างจอ", 10, 100, 10, gSettings.get().brightness / 10 * 10, "%", [](int v) {
                gSettings.update([&](SettingsData& d) { d.brightness = (uint8_t)v; });
                gUi.setBrightness((uint8_t)v);
            });
        });
        static char dimLabel[16];
        snprintf(dimLabel, sizeof(dimLabel), "%u%%", (unsigned)s.dimBrightness);
        l.add("แสงจอตอนพักหน้าจอ (แตะ)", dimLabel, []() {
            gSettings.update([](SettingsData& d) {
                d.dimBrightness = d.dimBrightness < 8 ? 8 : d.dimBrightness < 15 ? 15 : d.dimBrightness < 25 ? 25 : 4;
            });
            gMenu.rebuild();
        });
        l.addSubmenu("การแสดงแบตเตอรี่", batteryModeLabel(s.batteryMode), openBatteryModes);
        l.add("หมุนจอ 180°", s.rotation == 3 ? "เปิด" : "ปิด", []() {
            uint8_t rot = 1;
            gSettings.update([&](SettingsData& d) {
                d.rotation = d.rotation == 3 ? 1 : 3;
                rot = d.rotation;
            });
            gUi.setRotation(rot);
            gMenu.rebuild();
        });
        l.add("สลับสีจอ (พื้นดำ/ขาว)", s.lcdInvert ? "แบบ 1" : "แบบ 2", []() {
            bool inv = true;
            gSettings.update([&](SettingsData& d) {
                d.lcdInvert = !d.lcdInvert;
                inv = d.lcdInvert;
            });
            gUi.setInvert(inv);
            gMenu.rebuild();
        });
        l.addSubmenu("ปรับการแตะหน้าจอ", s.touchCal ? "ปรับแล้ว" : "ยังไม่ปรับ", []() { gUi.push(&gCalibration); });
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatMediaDash() {
    gMenu.setup("วิทยุ / MP3 & หน้าปัด", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        l.addSubmenu("เครื่องเล่น วิทยุ / MP3", gRadio.active() ? "กำลังเล่น" : "เปิดเครื่องเล่น",
                     []() { gUi.push(&gRadioUi); });
        l.add("เปิดหน้าปัดรถยนต์", "", []() {
            gUi.popToRoot();
            gVehicle.requestOpen();
        });
        l.addSubmenu("ตั้งค่าหน้าปัดรถ (OBD)", gVehicle.enabled() ? vehicle::sourceNameTh(gVehicle.source()) : "ปิด",
                     openVehicle);
        l.addSubmenu("ความดังวิทยุ / เพลง", String(s.volume) + "%", []() {
            openNumberPicker("ความดังวิทยุ / เพลง", 0, 100, 10, gSettings.get().volume / 10 * 10, "%",
                             [](int v) { gSettings.update([&](SettingsData& d) { d.volume = (uint8_t)v; }); });
        });
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatMemory() {
    gMenu.setup("ความจำ & ประวัติ", [](ListScreen& l) {
        const SettingsData s = gSettings.get();
        l.addSubmenu("การแจ้งเตือน", "เตือนความจำ • จับเวลา • ราคา", []() { settingsui::openAlerts(); });
        l.add("จัดความจำอัตโนมัติ", s.consolidate ? "เปิด" : "ปิด", []() {
            gSettings.update([](SettingsData& d) { d.consolidate = !d.consolidate; });
            gMenu.rebuild();
        }).valueColor = s.consolidate ? ui::OK : ui::DIM;
        l.add("เริ่มบทสนทนาใหม่", "เคลียร์บริบทรอบนี้", []() {
            if (gUi.host()) gUi.host()->uiNewSession();
            gUi.popToRoot();
        });
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
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

void openCatSystem() {
    gMenu.setup("ระบบ & อัปเดต", [](ListScreen& l) {
        l.addSubmenu("ข้อมูลระบบ", "สถานะฮาร์ดแวร์ & เครือข่าย", []() { gUi.push(&gInfo); });
        {
            const OtaUpdater::State os = gOta.state();
            String v = "v" FIRMWARE_VERSION;
            if (os == OtaUpdater::State::Available) v = "มี v" + gOta.latest();
            ListScreen::Item& up = l.addSubmenu("อัปเดตเฟิร์มแวร์ (OTA)", v, openUpdate);
            if (os == OtaUpdater::State::Available) up.valueColor = ui::OK;
        }
        l.add("ทดสอบ API Key", "", []() {
            if (gUi.host()) gUi.host()->uiApiTest();
            gUi.popToRoot();
        });
        l.add("รีสตาร์ทเครื่อง", "", []() {
            gConfirm.ask("รีสตาร์ท", "รีสตาร์ทเครื่องตอนนี้?", "รีสตาร์ท", []() {
                if (gUi.host()) gUi.host()->uiRestart();
                else ESP.restart();
            });
        }).valueColor = ui::WARN;
    });
    gMenu.onTick = nullptr;
    gUi.push(&gMenu);
}

// ============================================================
// Smartwatch-Style Grouped Settings Launcher
// ============================================================

class SmartwatchMenuScreen : public Screen {
public:
    static constexpr int kCatCount = 8;
    static constexpr int kPerPage = 4;
    static constexpr int kPages = 2;
    static constexpr int kCardW = 146, kCardH = 84;

    void setPage(int p) {
        _page = constrain(p, 0, kPages - 1);
        _scrollX = (float)(_page * ui::W);
    }

    void onEnter() override {
        _pressed = -1;
        _dragging = false;
        _fingerDown = false;
    }

    void onTouch(const TouchEvent& e) override {
        switch (e.type) {
            case TouchEvent::Down:
                _startX = _lastX = e.x;
                _startY = _lastY = e.y;
                _dragging = false;
                _fingerDown = true;
                _pressed = cardAt(e.x, e.y);
                break;
            case TouchEvent::Move: {
                const int dx = e.x - _startX;
                const int dy = e.y - _startY;
                if (!_dragging && (abs(dx) > 10 || abs(dy) > 10)) {
                    _dragging = true;
                    _pressed = -1;
                }
                if (_dragging) {
                    const int step = abs(dx) >= abs(dy) ? (e.x - _lastX) : (e.y - _lastY);
                    _scrollX -= step;
                    const float maxS = (kPages - 1) * (float)ui::W;
                    if (_scrollX < -30) _scrollX = -30;
                    if (_scrollX > maxS + 30) _scrollX = maxS + 30;
                }
                _lastX = e.x;
                _lastY = e.y;
                break;
            }
            case TouchEvent::Up:
                _fingerDown = false;
                _pressed = -1;
                if (_dragging) {
                    const int dx = e.x - _startX;
                    const int dy = e.y - _startY;
                    const int delta = abs(dx) >= abs(dy) ? dx : dy;
                    if (delta < -35 && _page < kPages - 1) _page++;
                    else if (delta > 35 && _page > 0) _page--;
                    else _page = (_scrollX + ui::W / 2) >= ui::W ? 1 : 0;
                }
                break;
            case TouchEvent::Tap: {
                if (UiManager::hitBack(e.x, e.y)) {
                    gUi.pop();
                    return;
                }
                // Top-right page pill or bottom navigation bar toggles / steps page
                if ((e.y < ui::kTitleH && e.x >= 220) || e.y >= 216) {
                    if (e.x < 120 && _page > 0) _page--;
                    else if (e.x > 200 && _page < kPages - 1) _page++;
                    else _page = (_page + 1) % kPages;
                    return;
                }
                const int idx = cardAt(e.x, e.y);
                if (idx >= 0 && idx < kCatCount) openCategory(idx);
                break;
            }
            default:
                break;
        }
    }

    void tick(uint32_t now, float dt) override {
        (void)now;
        (void)dt;
        if (!_fingerDown) {
            const float target = (float)(_page * ui::W);
            const float diff = target - _scrollX;
            if (fabsf(diff) < 1.5f) _scrollX = target;
            else _scrollX += diff * 0.35f;
        }
    }

    void draw(LGFX_Sprite& c) override {
        c.fillScreen(ui::BG);
        const SettingsData s = gSettings.get();
        const String joined = joinedSsid();

        c.setClipRect(0, ui::kTitleH, ui::W, 218 - ui::kTitleH);
        for (int p = 0; p < kPages; p++) {
            const int baseX = p * ui::W - (int)_scrollX;
            if (baseX <= -ui::W || baseX >= ui::W) continue;
            for (int slot = 0; slot < kPerPage; slot++) {
                const int idx = p * kPerPage + slot;
                const int col = slot % 2;
                const int row = slot / 2;
                const int x = baseX + 10 + col * (kCardW + 8);
                const int y = 40 + row * (kCardH + 6);
                drawCard(c, idx, x, y, s, joined);
            }
        }
        c.clearClipRect();

        // Bottom swipe / page indicator bar
        c.setFont(&fonts::Font2);
        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(_page > 0 ? ui::ACCENT : 0x39E7);
        c.drawString("<", 112, 229);
        for (int p = 0; p < kPages; p++) {
            const int dx = 150 + p * 20;
            if (p == _page) c.fillSmoothRoundRect(dx - 8, 225, 16, 8, 4, ui::ACCENT);
            else c.fillSmoothCircle(dx, 229, 4, 0x4208);
        }
        c.setTextColor(_page < kPages - 1 ? ui::ACCENT : 0x39E7);
        c.drawString(">", 208, 229);

        // Title bar + page badge
        gUi.drawTitle(c, "ตั้งค่า", true);
        char pageBadge[32];
        snprintf(pageBadge, sizeof(pageBadge), "หน้า %d/%d", _page + 1, kPages);
        gUi.drawFitted(c, pageBadge, ui::W - 12, ui::kTitleH / 2, 110, gUi.thai20(), ui::ACCENT,
                       textdatum_t::middle_right);
    }

private:
    int _page = 0;
    float _scrollX = 0.0f;
    int _pressed = -1;
    int16_t _startX = 0, _startY = 0, _lastX = 0, _lastY = 0;
    bool _dragging = false;
    bool _fingerDown = false;

    int cardAt(int16_t x, int16_t y) const {
        if (y < 40 || y >= 40 + 2 * kCardH + 6) return -1;
        const int col = (x >= 160) ? 1 : 0;
        const int row = (y >= 40 + kCardH + 3) ? 1 : 0;
        return _page * kPerPage + row * 2 + col;
    }

    void openCategory(int idx) {
        switch (idx) {
            case 0: openWifi(); break;
            case 1: openCatPersona(); break;
            case 2: openCatAi(); break;
            case 3: openCatAudio(); break;
            case 4: openCatDisplay(); break;
            case 5: openCatMediaDash(); break;
            case 6: openCatMemory(); break;
            case 7: openCatSystem(); break;
            default: break;
        }
    }

    void drawIcon(LGFX_Sprite& c, int idx, int cx, int cy, uint16_t badgeCol) {
        c.fillSmoothCircle(cx, cy, 18, badgeCol);
        switch (idx) {
            case 0:  // Wi-Fi arcs
                c.fillCircle(cx, cy + 7, 2, ui::TEXT);
                c.drawArc(cx, cy + 7, 7, 5, 225, 315, ui::TEXT);
                c.drawArc(cx, cy + 7, 12, 10, 225, 315, ui::TEXT);
                c.drawArc(cx, cy + 7, 17, 15, 225, 315, ui::TEXT);
                break;
            case 1:  // Persona / Avatar face
                c.fillSmoothRoundRect(cx - 11, cy - 9, 22, 18, 6, ui::TEXT);
                c.fillSmoothRoundRect(cx - 7, cy - 4, 5, 6, 2, badgeCol);
                c.fillSmoothRoundRect(cx + 2, cy - 4, 5, 6, 2, badgeCol);
                c.drawArc(cx, cy + 2, 5, 4, 30, 150, badgeCol);
                break;
            case 2:  // AI Brain / Sparkle
                c.fillTriangle(cx, cy - 12, cx - 4, cy, cx + 4, cy, ui::TEXT);
                c.fillTriangle(cx, cy + 12, cx - 4, cy, cx + 4, cy, ui::TEXT);
                c.fillTriangle(cx - 12, cy, cx, cy - 4, cx, cy + 4, ui::TEXT);
                c.fillTriangle(cx + 12, cy, cx, cy - 4, cx, cy + 4, ui::TEXT);
                c.fillCircle(cx + 8, cy - 8, 2, 0xFFE0);
                c.fillCircle(cx - 8, cy + 8, 2, 0xFFE0);
                break;
            case 3:  // Audio & Mic
                c.fillSmoothRoundRect(cx - 4, cy - 10, 9, 13, 4, ui::TEXT);
                c.fillArc(cx, cy - 2, 6, 8, 0, 180, ui::TEXT);
                c.fillRect(cx - 1, cy + 6, 3, 3, ui::TEXT);
                c.fillRect(cx - 4, cy + 9, 9, 2, ui::TEXT);
                break;
            case 4:  // Display & Clock / Sun
                c.fillCircle(cx, cy, 7, 0xFFE0);
                for (int i = 0; i < 8; i++) {
                    const float a = i * 0.7854f;
                    c.drawWideLine(cx + cosf(a) * 9, cy + sinf(a) * 9, cx + cosf(a) * 13, cy + sinf(a) * 13, 2.0f,
                                   ui::TEXT);
                }
                break;
            case 5:  // Radio / MP3 & Vehicle Dash
                c.fillCircle(cx - 5, cy + 5, 3, ui::TEXT);
                c.fillCircle(cx + 4, cy + 3, 3, ui::TEXT);
                c.fillRect(cx - 3, cy - 6, 2, 11, ui::TEXT);
                c.fillRect(cx + 6, cy - 8, 2, 11, ui::TEXT);
                c.drawWideLine(cx - 3, cy - 6, cx + 7, cy - 8, 2.5f, ui::TEXT);
                break;
            case 6:  // Memory chip
                c.drawRoundRect(cx - 8, cy - 8, 16, 16, 3, ui::TEXT);
                c.fillRect(cx - 4, cy - 4, 8, 8, ui::TEXT);
                for (int d = -5; d <= 5; d += 5) {
                    c.drawFastHLine(cx - 12, cy + d, 4, ui::TEXT);
                    c.drawFastHLine(cx + 8, cy + d, 4, ui::TEXT);
                    c.drawFastVLine(cx + d, cy - 12, 4, ui::TEXT);
                    c.drawFastVLine(cx + d, cy + 8, 4, ui::TEXT);
                }
                break;
            case 7:  // System & OTA Gear
                for (int i = 0; i < 8; i++) {
                    const float a = i * 0.7854f;
                    c.fillCircle(cx + (int)(cosf(a) * 9.5f), cy + (int)(sinf(a) * 9.5f), 2, ui::TEXT);
                }
                c.fillCircle(cx, cy, 8, ui::TEXT);
                c.fillCircle(cx, cy, 3, badgeCol);
                break;
        }
    }

    void drawCard(LGFX_Sprite& c, int idx, int x, int y, const SettingsData& s, const String& joined) {
        static const uint16_t kBadgeColors[kCatCount] = {
            0x0548,  // 0: Emerald Green (Wi-Fi)
            0x14BF,  // 1: Ocean Blue (Persona)
            0x699F,  // 2: Royal Purple (AI Brain)
            0xE2E4,  // 3: Coral Orange (Audio/Mic)
            0xD460,  // 4: Amber Gold (Display/Clock)
            0x0576,  // 5: Teal Cyan (Radio/Dash)
            0xB133,  // 6: Rose Magenta (Memory)
            0x337D   // 7: Slate Blue (System/OTA)
        };
        static const char* const kTitles[kCatCount] = {
            "Wi-Fi & เน็ต", "ผู้ช่วย & เสียง", "สมอง AI", "เสียง & ไมค์",
            "จอ & พักจอ",  "วิทยุ & รถ",   "ความจำ",  "ระบบ & อัปเดต"
        };

        const bool pressed = (_pressed == idx);
        const uint16_t badgeCol = kBadgeColors[idx];
        c.fillSmoothRoundRect(x, y, kCardW, kCardH, 14, pressed ? 0x1926 : 0x10A3);
        c.drawRoundRect(x, y, kCardW, kCardH, 14, pressed ? badgeCol : 0x2945);

        drawIcon(c, idx, x + 26, y + 28, badgeCol);
        gUi.drawFitted(c, kTitles[idx], x + 50, y + 28, kCardW - 56, gUi.thai20(), ui::TEXT, textdatum_t::middle_left);

        String sub;
        uint16_t subCol = 0xAD55;
        switch (idx) {
            case 0:
                if (joined.length()) {
                    sub = joined;
                    subCol = ui::OK;
                } else {
                    sub = s.wifiSsid[0] ? "ไม่ได้เชื่อมต่อ" : "ยังไม่ตั้งค่า";
                    subCol = ui::WARN;
                }
                break;
            case 1:
                sub = String(s.agentName) + " • " + s.voice;
                subCol = ui::ACCENT;
                break;
            case 2:
                sub = s.model == 0 ? "3.8 Live" : (s.model == 1 ? "3.8 Thinking" : "3.1 Flash");
                sub += String(" • ") + aiLanguageId(s.language);
                break;
            case 3:
                sub = "AI " + String(s.aiVolume) + "% • ไมค์ " + String(s.micGain) + "/7";
                break;
            case 4:
                sub = String(clockThemeLabel(s.clockTheme));
                subCol = ui::ACCENT;
                break;
            case 5:
                sub = gRadio.active() ? "กำลังเล่นวิทยุ/เพลง"
                                      : (gVehicle.enabled() ? vehicle::sourceNameTh(gVehicle.source()) : "MP3 • หน้าปัด OBD");
                if (gRadio.active()) subCol = ui::OK;
                break;
            case 6:
                sub = s.consolidate ? "สรุปอัตโนมัติ: เปิด" : "สรุปอัตโนมัติ: ปิด";
                break;
            case 7:
                if (gOta.state() == OtaUpdater::State::Available) {
                    sub = "มีอัปเดต v" + gOta.latest();
                    subCol = ui::OK;
                } else {
                    sub = "v" FIRMWARE_VERSION " • ปกติ";
                }
                break;
        }
        gUi.drawFitted(c, sub.c_str(), x + 10, y + 65, kCardW - 20, gUi.thai20(), subCol, textdatum_t::middle_left);
    }
};

SmartwatchMenuScreen gLauncher;

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

// ------------------------------------------------------------
// T141: alerts - reminders, timers, price alerts (the bell next to Wi-Fi on the face)
// ------------------------------------------------------------

ListScreen gAlerts;
ListScreen gAlertDetail;

namespace {
std::vector<UiAlert> gAlertList;  // the agent's copy, as last fetched (UI task)
uint32_t gAlertVer = 0;

void fetchAlerts() {
    if (gUi.host()) gAlertVer = gUi.host()->uiAlerts(gAlertList);
}

const UiAlert* findAlert(UiAlert::Kind kind, uint32_t id) {
    for (const UiAlert& a : gAlertList) {
        if (a.kind == kind && a.id == id) return &a;
    }
    return nullptr;
}

void sendAlertEdit(const UiAlertEdit& e) {
    if (gUi.host()) gUi.host()->uiAlertEdit(e);
}

const char* repeatTh(uint8_t r) {
    static const char* const kRep[] = {"ครั้งเดียว", "ทุกวัน", "วันทำงาน (จ.–ศ.)", "ทุกสัปดาห์"};
    return kRep[r < 4 ? r : 0];
}

// "วันนี้" / "พรุ่งนี้" / "ศ. 31 ต.ค." for a local day
String dayTh(time_t t) {
    static const char* const kDay[] = {"อา.", "จ.", "อ.", "พ.", "พฤ.", "ศ.", "ส."};
    static const char* const kMon[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.",
                                       "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};
    const time_t now = time(nullptr);
    struct tm a{}, b{};
    localtime_r(&t, &a);
    localtime_r(&now, &b);
    const long days = (long)((t - a.tm_hour * 3600 - a.tm_min * 60 - a.tm_sec) -
                             (now - b.tm_hour * 3600 - b.tm_min * 60 - b.tm_sec) + 43200) / 86400;
    if (days == 0) return "วันนี้";
    if (days == 1) return "พรุ่งนี้";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d %s", kDay[a.tm_wday % 7], a.tm_mday, kMon[a.tm_mon % 12]);
    return buf;
}

String clockTh(time_t t) {
    struct tm a{};
    localtime_r(&t, &a);
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d น.", a.tm_hour, a.tm_min);
    return buf;
}

String leftTh(uint32_t due) {
    const long s = (long)due - (long)time(nullptr);
    if (s <= 0) return "ครบเวลาแล้ว";
    char buf[40];
    if (s < 60) snprintf(buf, sizeof(buf), "อีก %ld วินาที", s);
    else if (s < 3600) snprintf(buf, sizeof(buf), "อีก %ld นาที", (s + 59) / 60);
    else snprintf(buf, sizeof(buf), "อีก %ld ชม. %ld นาที", s / 3600, (s % 3600) / 60);
    return buf;
}

// 80,847.12 / 2,400 / 1.0845 - grouped like a price board
String priceTh(double v) {
    char raw[32];
    if (v >= 100) snprintf(raw, sizeof(raw), "%.2f", v);
    else snprintf(raw, sizeof(raw), "%.5g", v);
    String s = raw;
    if (s.indexOf('.') > 0 && v >= 100) {
        while (s.endsWith("0")) s.remove(s.length() - 1);
        if (s.endsWith(".")) s.remove(s.length() - 1);
    }
    int dot = s.indexOf('.');
    if (dot < 0) dot = s.length();
    for (int i = dot - 3; i > 0; i -= 3) s = s.substring(0, i) + "," + s.substring(i);
    return s;
}

// the same local time on another day, or another time on the same day (the next day if that is past)
uint32_t withDayAndTime(uint32_t due, int addDaysFromToday, int hour, int minute) {
    const time_t base = addDaysFromToday >= 0 ? time(nullptr) : (time_t)due;
    struct tm a{}, d{};
    localtime_r(&base, &a);
    const time_t dt = (time_t)due;
    localtime_r(&dt, &d);
    a.tm_hour = hour >= 0 ? hour : d.tm_hour;
    a.tm_min = minute >= 0 ? minute : d.tm_min;
    a.tm_sec = 0;
    if (addDaysFromToday > 0) a.tm_mday += addDaysFromToday;
    time_t t = mktime(&a);
    if (addDaysFromToday < 0 && t <= time(nullptr)) t += 86400;  // a time already gone today: tomorrow
    return (uint32_t)t;
}

void openAlertDetail(UiAlert::Kind kind, uint32_t id);

// the version moved on (the agent applied an edit, the AI set or removed one): show what is there now
void rebuildOnChange(ListScreen& l, uint32_t now) {
    static uint32_t last = 0;
    bool timer = false;  // a timer's "อีก 4 นาที" counts down
    for (const UiAlert& a : gAlertList) timer |= a.kind == UiAlert::Timer;
    if ((gUi.host() && gUi.host()->uiAlertsVersion() != gAlertVer) || (timer && now - last >= 1000)) {
        last = now;
        l.rebuild();
    }
}

void openAlertMinute(uint32_t id, int hour) {
    gSubPicker.setup("นาที", [id, hour](ListScreen& l) {
        const UiAlert* a = findAlert(UiAlert::Reminder, id);
        struct tm d{};
        const time_t due = a ? (time_t)a->due : 0;
        localtime_r(&due, &d);
        for (int m = 0; m < 60; m += 5) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", hour, m);
            ListScreen::Item& it = l.add(buf, "", [id, hour, m]() {
                const UiAlert* a = findAlert(UiAlert::Reminder, id);
                if (a) {
                    UiAlertEdit e;
                    e.op = UiAlertEdit::Update;
                    e.id = id;
                    e.due = withDayAndTime(a->due, -1, hour, m);
                    e.repeat = a->repeat;
                    sendAlertEdit(e);
                }
                gUi.pop();  // minutes
                gUi.pop();  // hours
            });
            if (m == d.tm_min / 5 * 5) it.checked = true;
        }
        l.reveal(d.tm_min / 5);
    });
    gUi.push(&gSubPicker);
}
}  // namespace

void openAlerts() {
    gAlerts.setup("การแจ้งเตือน", [](ListScreen& l) {
        fetchAlerts();
        if (gAlertList.empty()) {
            l.add("ยังไม่มีการแจ้งเตือน", "", nullptr);
            l.add("สั่ง AI ได้เลย", "\"เตือนพรุ่งนี้ 7 โมง\"", nullptr);
            return;
        }
        for (const UiAlert& a : gAlertList) {
            String value;
            String title = a.label;
            if (a.kind == UiAlert::Price && a.what[0]) {  // T143: RSI / MACD / EMA / CHoCH
                value = a.what;
            } else if (a.kind == UiAlert::Price) {
                value = String(a.above ? "ขึ้นถึง " : "ลงถึง ") + priceTh(a.target);  // the Thai font has no ≥ / ≤
            } else if (a.kind == UiAlert::Timer) {
                if (!title.length()) title = "ตัวจับเวลา";
                value = leftTh(a.due);
            } else {
                value = dayTh(a.due) + " " + clockTh(a.due);
            }
            const UiAlert::Kind k = a.kind;
            const uint32_t id = a.id;
            ListScreen::Item& it = l.addSubmenu(title, value, [k, id]() { openAlertDetail(k, id); });
            it.valueColor = a.kind == UiAlert::Price ? ui::WARN : (a.kind == UiAlert::Timer ? ui::OK : ui::ACCENT);
        }
    });
    gAlerts.onTick = rebuildOnChange;
    gUi.push(&gAlerts);
}

namespace {
void openAlertDetail(UiAlert::Kind kind, uint32_t id) {
    gAlertDetail.setup(kind == UiAlert::Price ? "แจ้งเตือนราคา" : (kind == UiAlert::Timer ? "ตัวจับเวลา" : "เตือนความจำ"),
                       [kind, id](ListScreen& l) {
        fetchAlerts();
        const UiAlert* a = findAlert(kind, id);
        if (!a) {
            l.add("รายการนี้ไม่มีแล้ว", "ลบไปแล้ว / ครบเวลาแล้ว", nullptr);
            return;
        }
        if (kind == UiAlert::Price && a->what[0]) {  // T143: an indicator condition
            l.add(a->label, "", nullptr);
            l.add("เงื่อนไข", a->what, nullptr).valueColor = ui::WARN;
            if (a->editable) {  // RSI: the level
                l.addSubmenu("ระดับ RSI", String((int)lround(a->target)), [id]() {
                    const UiAlert* a = findAlert(UiAlert::Price, id);
                    if (!a) return;
                    static char initial[8];
                    snprintf(initial, sizeof(initial), "%d", (int)lround(a->target));
                    KeyboardScreen::Options o;
                    o.title = "ระดับ RSI (1-99)";
                    o.initial = initial;
                    o.maxBytes = 2;
                    o.asciiOnly = true;
                    gKeyboard.open(o, [id](const String& t) {
                        const double level = atof(t.c_str());
                        if (!(level >= 1 && level <= 99)) return;
                        UiAlertEdit e;
                        e.op = UiAlertEdit::Update;
                        e.kind = UiAlert::Price;
                        e.id = id;
                        e.target = level;
                        sendAlertEdit(e);
                    });
                }).valueColor = ui::WARN;
            }
            l.add("ตรวจ", "ทุก 2 นาที เมื่อแท่งปิด", nullptr);
        } else if (kind == UiAlert::Price) {
            l.add(a->label, "", nullptr);
            l.addSubmenu(a->above ? "แจ้งเมื่อขึ้นถึง" : "แจ้งเมื่อลงถึง", priceTh(a->target), [id]() {
                const UiAlert* a = findAlert(UiAlert::Price, id);
                if (!a) return;
                static char initial[24];
                snprintf(initial, sizeof(initial), "%.6g", a->target);
                KeyboardScreen::Options o;
                o.title = "ราคาเป้าหมาย";
                o.initial = initial;
                o.maxBytes = 16;
                o.asciiOnly = true;
                gKeyboard.open(o, [id](const String& t) {
                    String v = t;
                    v.replace(",", "");
                    const double target = atof(v.c_str());
                    if (!(target > 0)) return;
                    UiAlertEdit e;
                    e.op = UiAlertEdit::Update;
                    e.kind = UiAlert::Price;
                    e.id = id;
                    e.target = target;
                    sendAlertEdit(e);
                });
            }).valueColor = ui::WARN;
            l.add("ตรวจราคา", "ทุก 2 นาที", nullptr);
        } else if (kind == UiAlert::Timer) {
            l.add(a->label[0] ? a->label : "ตัวจับเวลา", "", nullptr);
            l.add("ดังเมื่อ", clockTh(a->due), nullptr);
            l.add("เหลือเวลา", leftTh(a->due), nullptr).valueColor = ui::OK;
        } else {
            l.addSubmenu("ข้อความ", a->label, [id]() {
                const UiAlert* a = findAlert(UiAlert::Reminder, id);
                if (!a) return;
                static char initial[96];
                snprintf(initial, sizeof(initial), "%s", a->label);
                KeyboardScreen::Options o;
                o.title = "ข้อความแจ้งเตือน";
                o.initial = initial;
                o.maxBytes = 90;
                gKeyboard.open(o, [id](const String& t) {
                    if (!t.length()) return;
                    UiAlertEdit e;
                    e.op = UiAlertEdit::Update;
                    e.id = id;
                    const UiAlert* a = findAlert(UiAlert::Reminder, id);
                    e.repeat = a ? a->repeat : 0;
                    snprintf(e.label, sizeof(e.label), "%s", t.c_str());
                    sendAlertEdit(e);
                });
            });
            l.addSubmenu("วัน", dayTh(a->due), [id]() {
                gPicker.setup("เลือกวัน", [id](ListScreen& l) {
                    const UiAlert* a = findAlert(UiAlert::Reminder, id);
                    if (!a) return;
                    for (int d = 0; d < 14; d++) {
                        const uint32_t t = withDayAndTime(a->due, d, -1, -1);
                        if (t <= (uint32_t)time(nullptr)) continue;  // today, but that time is gone
                        ListScreen::Item& it = l.add(dayTh(t), clockTh(t), [id, t]() {
                            const UiAlert* a = findAlert(UiAlert::Reminder, id);
                            UiAlertEdit e;
                            e.op = UiAlertEdit::Update;
                            e.id = id;
                            e.due = t;
                            e.repeat = a ? a->repeat : 0;
                            sendAlertEdit(e);
                            gUi.pop();
                        });
                        it.checked = dayTh(t) == dayTh(a->due);
                    }
                });
                gUi.push(&gPicker);
            });
            l.addSubmenu("เวลา", clockTh(a->due), [id]() {
                gPicker.setup("ชั่วโมง", [id](ListScreen& l) {
                    const UiAlert* a = findAlert(UiAlert::Reminder, id);
                    struct tm d{};
                    const time_t due = a ? (time_t)a->due : 0;
                    localtime_r(&due, &d);
                    for (int h = 0; h < 24; h++) {
                        char buf[12];
                        snprintf(buf, sizeof(buf), "%02d น.", h);
                        l.addSubmenu(buf, "", [id, h]() { openAlertMinute(id, h); }).checked = h == d.tm_hour;
                    }
                    l.reveal(d.tm_hour);
                });
                gUi.push(&gPicker);
            });
            l.addSubmenu("ทำซ้ำ", repeatTh(a->repeat), [id]() {
                gPicker.setup("ทำซ้ำ", [id](ListScreen& l) {
                    const UiAlert* a = findAlert(UiAlert::Reminder, id);
                    for (uint8_t r = 0; r < 4; r++) {
                        ListScreen::Item& it = l.add(repeatTh(r), "", [id, r]() {
                            UiAlertEdit e;
                            e.op = UiAlertEdit::Update;
                            e.id = id;
                            e.repeat = r;
                            sendAlertEdit(e);
                            gUi.pop();
                        });
                        it.checked = a && a->repeat == r;
                    }
                });
                gUi.push(&gPicker);
            });
        }
        l.add("ลบการแจ้งเตือนนี้", "ลบ", [kind, id]() {
            gConfirm.ask("ลบการแจ้งเตือน", "ลบรายการนี้? ย้อนกลับไม่ได้", "ลบ", [kind, id]() {
                UiAlertEdit e;
                e.op = UiAlertEdit::Remove;
                e.kind = kind;
                e.id = id;
                sendAlertEdit(e);
                gUi.pop();  // back to the list
            });
        }).valueColor = ui::ERR;
    });
    gAlertDetail.onTick = rebuildOnChange;
    gUi.push(&gAlertDetail);
}
}  // namespace

namespace settingsui {

void openAlerts() { ::openAlerts(); }

void openCalibration() { gUi.push(&gCalibration); }

void openMenu() {
    gLauncher.setPage(0);
    gUi.push(&gLauncher);
}

bool openPage(const char* page) {
    if (!strcmp(page, "menu")) openMenu();
    else if (!strcmp(page, "menu2")) {
        gLauncher.setPage(1);
        if (gUi.top() != &gLauncher) gUi.push(&gLauncher);
    } else if (!strcmp(page, "wifi")) openWifi();
    else if (!strcmp(page, "persona")) openCatPersona();
    else if (!strcmp(page, "ai")) openCatAi();
    else if (!strcmp(page, "audio")) openCatAudio();
    else if (!strcmp(page, "display")) openCatDisplay();
    else if (!strcmp(page, "clocktheme")) openClockThemes();
    else if (!strcmp(page, "clockcolor")) openClockColors();
    else if (!strcmp(page, "alerts")) openAlerts();
    else if (!strcmp(page, "city")) openWeatherCity();
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
