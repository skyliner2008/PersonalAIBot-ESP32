// Renders the vehicle dash (src/ui/DashScreen.cpp, unchanged) for a few scenes into PNG files: a layout check
// off the board. Build + run: bash tools/dash_preview/run.sh (wiki 16). The Vehicle methods the screen calls are
// stubbed below and fed by the real simulator.
#include <stdio.h>
#include <zlib.h>
#include <vector>
#include "phone/PhoneLink.h"
#include "ui/DashScreen.h"
#include "ui/NavScreen.h"
#include "vehicle/Vehicle.h"
#include "vehicle/SimEngine.h"

SerialStub Serial;
namespace settingsui { bool openPage(const char*) { return true; } }
static uint32_t gNow = 1000;
uint32_t millis() { return gNow; }

// ---- the Vehicle API the screen uses, fed by the real simulator ----
Vehicle gVehicle;
static vehicle::Snapshot gSnap;
static const char* gStatus = "SIM";
static vehicle::SourceId gSrc = vehicle::SourceId::SimAuto;
static vehicle::Kind gKind = vehicle::Kind::Car;
static bool gRestart = false;
static vehicle::DashStyle gStyle = vehicle::DashStyle::Race;
vehicle::DashConfig Vehicle::dashConfig() { return vehicle::defaultConfig(gKind); }
Vehicle::Settings Vehicle::settings() { Settings s; s.source = gSrc; s.kind = gKind; s.style = gStyle; return s; }
void Vehicle::setStyle(vehicle::DashStyle s) { gStyle = s; }
vehicle::Snapshot Vehicle::snapshot() { return gSnap; }
String Vehicle::status() { return String(gStatus); }
bool Vehicle::needsRestart() { return gRestart; }
// T112: the diagnosis (the simulator's codes) and the health analysis the info page shows
static vehicle::obd::Diag gDiag;
static vehicle::HealthMonitor gHealth;
vehicle::obd::Diag Vehicle::diag() { return gDiag; }
vehicle::Health Vehicle::health() { return gHealth.health(); }
uint32_t Vehicle::requestScan() { return 0; }

// ---- the PhoneLink API the screens use (wiki 18) ----
PhoneLink gPhone;
static phone::Nav gNav;
static phone::Gps gGps;
static std::string gPlace;
phone::Nav PhoneLink::nav() { return gNav; }
phone::Gps PhoneLink::gps() { return gGps; }
String PhoneLink::place() { return String(gPlace); }
bool PhoneLink::connected() { return true; }
PhoneLink::Via PhoneLink::via() { return Via::Wifi; }

static void setNav(const char* icon, const char* text, int dist) {
    gNav = phone::Nav();
    gNav.active = true;
    gNav.icon = phone::parseIcon(icon);
    phone::copyText(gNav.text, sizeof(gNav.text), text);
    gNav.distM = dist;
    snprintf(gNav.eta, sizeof(gNav.eta), "16:40");
    gNav.leftKm = 5.4f;
    gNav.leftMin = 12;
    gNav.at = gNow;
}

static void savePng(LGFX_Sprite& c, const char* path) {
    const int w = c.width(), h = c.height();
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            const uint16_t p = c.readPixel(x, y);
            raw.push_back((p >> 11) * 255 / 31);
            raw.push_back(((p >> 5) & 63) * 255 / 63);
            raw.push_back((p & 31) * 255 / 31);
        }
    }
    uLongf zl = compressBound(raw.size());
    std::vector<uint8_t> z(zl);
    compress(z.data(), &zl, raw.data(), raw.size());
    FILE* f = fopen(path, "wb");
    auto be = [&](uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); };
    auto chunk = [&](const char* type, const uint8_t* d, uint32_t n) {
        be(n);
        uLong crc = crc32(0, (const Bytef*)type, 4);
        fwrite(type, 1, 4, f);
        if (n) { fwrite(d, 1, n, f); crc = crc32(crc, d, n); }
        be((uint32_t)crc);
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), (uint32_t)zl);
    chunk("IEND", nullptr, 0);
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    gUi.begin(nullptr, 1, nullptr);
    LGFX_Sprite& c = gUi.canvas();
    char path[256];
    auto scene = [&](const char* name) {  // the same moment in every style
        for (uint8_t st = 0; st < (uint8_t)vehicle::DashStyle::Count; st++) {
            gStyle = (vehicle::DashStyle)st;
            gDashUi.onEnter();  // picks the style up now (it is read once a second)
            gDashUi.tick(gNow, 0.03f);
            gDashUi.draw(c);
            snprintf(path, sizeof(path), "%s/dash_%s_%s.png", out, vehicle::styleKey(gStyle), name);
            savePng(c, path);
        }
    };
    gUi.push(&gDashUi);
    // car, simulator: city driving, then the full-throttle pull with the shift light
    vehicle::SimEngine sim;
    sim.reset(gKind);
    sim.setAutoDrive(true);
    sim.diag(gDiag);
    gHealth.reset(vehicle::HealthConfig());
    for (int i = 0; i < 9 * 50; i++) {
        sim.step(0.02f); gNow += 20; sim.write(gSnap, gNow); gHealth.update(gSnap, &gDiag, gNow);
        if (i % 5 == 0) gDashUi.tick(gNow, 0.02f);
    }
    scene("car_city");
    for (int i = 0; i < 1000 && sim.rpm() < vehicle::defaultConfig(gKind).shiftRpm + 20; i++) { sim.step(0.02f); gNow += 20; sim.write(gSnap, gNow); if (i % 5 == 0) gDashUi.tick(gNow, 0.02f); }
    gNow = (gNow / 140) * 140;  // the "on" phase of the shift flash
    sim.write(gSnap, gNow);
    scene("car_shift");
    // overheating + not charging
    gSnap.set(vehicle::kCoolant, 114, gNow);
    gSnap.set(vehicle::kBattery, 11.3f, gNow);
    gNow += 300;
    gSnap.set(vehicle::kRpm, 2400, gNow); gSnap.set(vehicle::kSpeed, 62, gNow);
    gSnap.set(vehicle::kCoolant, 114, gNow); gSnap.set(vehicle::kBattery, 11.3f, gNow);
    scene("car_warnings");
    // motorcycle on Honda K-Line: no speed from the ECU, no gear (and no trouble codes)
    gDiag = vehicle::obd::Diag();
    gKind = vehicle::Kind::Motorcycle;
    gSrc = vehicle::SourceId::HondaK;
    gStatus = "LIVE";
    gSnap.clear();
    gNow += 3000;
    for (int i = 0; i < 30; i++) {
        gNow += 33;
        gSnap.set(vehicle::kRpm, 4850, gNow); gSnap.set(vehicle::kCoolant, 96, gNow);
        gSnap.set(vehicle::kIntake, 38, gNow); gSnap.set(vehicle::kBattery, 13.9f, gNow);
        gDashUi.tick(gNow, 0.03f);
    }
    scene("moto_kline");
    // OBD over BLE, waiting for the adapter (needs the one restart)
    gKind = vehicle::Kind::Car;
    gSrc = vehicle::SourceId::ObdBle;
    gStatus = "BT RESTART";
    gRestart = true;
    gSnap.clear();
    gNow += 5000;
    scene("obd_wait");

    // ---- navigation from the phone (wiki 18): the strip on the dash, the turn-by-turn screen, every arrow ----
    gRestart = false;
    gSrc = vehicle::SourceId::PhoneGps;
    gStatus = "GPS";
    gSnap.clear();
    gNow += 1000;
    gSnap.set(vehicle::kSpeed, 47, gNow);
    gGps.valid = true;
    gGps.speedKmh = 47;
    gGps.accuracy = 4;
    gGps.at = gNow;
    setNav("right", "เลี้ยวขวาเข้า ถ.พระราม 4", 350);
    scene("phone_nav");
    gUi.push(&gNavUi);
    auto nav = [&](const char* name) {
        gNavUi.onEnter();
        gNavUi.tick(gNow, 0.03f);
        gNavUi.draw(c);
        snprintf(path, sizeof(path), "%s/nav_%s.png", out, name);
        savePng(c, path);
    };
    nav("right");
    setNav("roundabout", "ที่วงเวียน ใช้ทางออกที่ 2 ไปทาง ถ.ราชดำริ มุ่งหน้า สยาม", 120);
    nav("roundabout");
    setNav("uturn", "กลับรถ", 600);
    nav("uturn");
    setNav("arrive", "ถึงที่หมาย เซ็นทรัลเวิลด์ อยู่ทางซ้าย", 40);
    nav("arrive");
    gNav = phone::Nav();
    gPlace = "ถ.พระราม 4 แขวงลุมพินี เขตปทุมวัน กรุงเทพฯ";
    nav("idle");
    // every arrow on one sheet: the shapes at a glance
    c.fillScreen(ui::BG);
    for (uint8_t i = 1; i < (uint8_t)phone::Icon::Count; i++) {
        const int col = (i - 1) % 5, row = (i - 1) / 5;
        NavScreen::drawIcon(c, (phone::Icon)i, 32 + col * 64, 40 + row * 80, 50, ui::TEXT, ui::BG);
        c.setFont(&fonts::Font0);
        c.setTextColor(ui::DIM);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(phone::iconKey((phone::Icon)i), 32 + col * 64, 78 + row * 80);
    }
    snprintf(path, sizeof(path), "%s/nav_icons.png", out);
    savePng(c, path);
    return 0;
}
