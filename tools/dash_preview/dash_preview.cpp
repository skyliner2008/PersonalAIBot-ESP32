// Renders the vehicle dash (src/ui/DashScreen.cpp, unchanged) for a few scenes into PNG files: a layout check
// off the board. Build + run: bash tools/dash_preview/run.sh (wiki 16). The Vehicle methods the screen calls are
// stubbed below and fed by the real simulator.
#include <stdio.h>
#include <zlib.h>
#include <vector>
#include "ui/DashScreen.h"
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
vehicle::DashConfig Vehicle::dashConfig() { return vehicle::defaultConfig(gKind); }
Vehicle::Settings Vehicle::settings() { Settings s; s.source = gSrc; s.kind = gKind; return s; }
vehicle::Snapshot Vehicle::snapshot() { return gSnap; }
String Vehicle::status() { return String(gStatus); }
bool Vehicle::needsRestart() { return gRestart; }

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
    auto scene = [&](const char* name) {
        gDashUi.tick(gNow, 0.03f);
        gDashUi.draw(c);
        snprintf(path, sizeof(path), "%s/dash_%s.png", out, name);
        savePng(c, path);
    };
    gUi.push(&gDashUi);
    // car, simulator: city driving, then the full-throttle pull with the shift light
    vehicle::SimEngine sim;
    sim.reset(gKind);
    sim.setAutoDrive(true);
    for (int i = 0; i < 9 * 50; i++) { sim.step(0.02f); gNow += 20; sim.write(gSnap, gNow); if (i % 5 == 0) gDashUi.tick(gNow, 0.02f); }
    scene("car_city");
    for (int i = 0; i < 1000 && sim.rpm() < 6100; i++) { sim.step(0.02f); gNow += 20; sim.write(gSnap, gNow); if (i % 5 == 0) gDashUi.tick(gNow, 0.02f); }
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
    // motorcycle on Honda K-Line: no speed from the ECU, no gear
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
    return 0;
}
