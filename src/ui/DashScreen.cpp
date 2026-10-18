/**
 * @file DashScreen.cpp
 * @brief Vehicle dash screen (see DashScreen.h)
 */

#include "DashScreen.h"

#include <math.h>

#include "../vehicle/Vehicle.h"
#include "SettingsScreens.h"

DashScreen gDashUi;

using namespace vehicle;

namespace {

constexpr int kHeaderH = 26;
constexpr int kBarTop = 30, kBarBottom = 64, kSegments = 30;
constexpr int kMainTop = 80, kMainBottom = 178;
constexpr int kTileY = 184, kTileH = 52, kTileW = 100, kTileGap = 6;
constexpr int kSetupX = 246;

// palette (RGB565)
constexpr uint16_t kGreen = 0x47E8, kYellow = 0xFEA0, kRed = 0xF8E4, kOff = 0x18E3, kCold = 0x3D9F;
constexpr uint16_t kRpmText = 0x5F1F, kShiftWhite = 0xFFFF;

enum Hit { kNone = 0, kBack, kSetup, kMain };

Hit hitTest(int x, int y) {
    if (y < kHeaderH) return x < 48 ? kBack : (x >= kSetupX ? kSetup : kNone);
    if (y >= kBarTop && y < kMainBottom) return kMain;
    return kNone;
}

uint16_t levelColor(Level l, uint32_t now) {
    switch (l) {
        case Level::Cold: return kCold;
        case Level::Warn: return kYellow;
        case Level::Crit: return ((now / 300) & 1) ? kRed : ui::TEXT;  // blinks
        case Level::None: return ui::DIM;
        default: return ui::TEXT;
    }
}

/** "3,250" */
void withCommas(char* out, size_t cap, long v) {
    if (v < 1000) {
        snprintf(out, cap, "%ld", v);
        return;
    }
    snprintf(out, cap, "%ld,%03ld", v / 1000, v % 1000);
}

/** Largest text size (<= maxSize) that keeps `text` within `w` px in `font`. */
float fitSize(LGFX_Sprite& c, const lgfx::IFont* font, const char* text, int w, float maxSize) {
    c.setFont(font);
    c.setTextSize(1);
    const int tw = c.textWidth(text);
    if (tw <= 0) return 1;
    const float s = (float)w / (float)tw;
    return s < maxSize ? s : maxSize;
}

}  // namespace

void DashScreen::onEnter() {
    _nextConfig = 0;
    _fingerDown = false;
    _pressed = kNone;
    gVehicle.setTouchThrottle(0);
    tick(millis(), 0);
}

void DashScreen::onTouch(const TouchEvent& e) {
    if (gUi.host()) gUi.host()->uiActivity();
    switch (e.type) {
        case TouchEvent::Down:
            _pressed = hitTest(e.x, e.y);
            _fingerDown = true;
            [[fallthrough]];
        case TouchEvent::Move:
            if (_touchSource && _pressed == kMain) {  // the finger is the throttle: further right = more
                const float t = (e.x - 20) / 270.0f;
                gVehicle.setTouchThrottle(t < 0.05f ? 0.05f : (t > 1 ? 1 : t));
            }
            break;
        case TouchEvent::Up:
            _fingerDown = false;
            gVehicle.setTouchThrottle(0);
            _pressed = kNone;
            break;
        case TouchEvent::Tap:
            if (hitTest(e.x, e.y) == kBack) {
                gUi.pop();
            } else if (hitTest(e.x, e.y) == kSetup) {
                settingsui::openPage("vehicle");
            }
            break;
        case TouchEvent::LongPress:
            if (!_touchSource && hitTest(e.x, e.y) == kMain) _model.resetPeak();
            break;
    }
}

void DashScreen::tick(uint32_t now, float dt) {
    (void)dt;
    _now = now;
    if ((int32_t)(now - _nextConfig) >= 0) {  // settings may change from the menu, voice or console
        _nextConfig = now + 1000;
        _model.setConfig(gVehicle.dashConfig());
        const uint8_t src = (uint8_t)gVehicle.source();
        if (src != _source) _model.resetPeak();  // a peak from the simulator is not this car's
        _source = src;
        _touchSource = _source == (uint8_t)SourceId::SimTouch;
        _needsRestart = gVehicle.needsRestart();
    }
    _status = gVehicle.status();
    const Snapshot s = gVehicle.snapshot();
    const DashView& v = _model.update(s, now);
    // a dash in use keeps the screen awake (the power-save clock would hide it while driving)
    if (v.live && (int32_t)(now - _nextActivity) >= 0) {
        _nextActivity = now + 10000;
        if (gUi.host()) gUi.host()->uiActivity();
    }
}

void DashScreen::drawBar(LGFX_Sprite& c, const DashView& v) {
    const DashConfig& cfg = _model.config();
    const float pitch = (ui::W - 12) / (float)kSegments;
    const float lit = v.rpmValid ? v.rpm / cfg.rpmMax * kSegments : 0;
    for (int i = 0; i < kSegments; i++) {
        const int x = 6 + (int)(i * pitch);
        const int w = (int)((i + 1) * pitch) - (int)(i * pitch) - 2;
        const int h = 14 + (kBarBottom - kBarTop - 14) * i / (kSegments - 1);  // rising toward the redline
        const float segRpm = (i + 0.5f) * cfg.rpmMax / kSegments;
        uint16_t col = segRpm >= cfg.shiftRpm ? kRed : (segRpm >= cfg.shiftRpm * 0.75f ? kYellow : kGreen);
        if (v.shift) col = v.shiftOn ? kShiftWhite : kRed;  // shift light: the whole bar flashes
        if (i + 0.5f > lit && !(v.shift && v.shiftOn)) col = segRpm >= cfg.shiftRpm ? 0x4800 : kOff;
        c.fillRect(x, kBarBottom - h, w, h, col);
    }
    // scale: one mark per 1000 rpm
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::top_center);
    for (int k = 0; k * 1000 <= cfg.rpmMax; k++) {
        const int x = 6 + (int)((ui::W - 12) * (k * 1000.0f / cfg.rpmMax));
        char t[4];
        snprintf(t, sizeof(t), "%d", k);
        c.drawString(t, x < 8 ? 8 : (x > ui::W - 8 ? ui::W - 8 : x), kBarBottom + 3);
    }
    c.setTextDatum(textdatum_t::top_right);
    c.drawString("x1000 rpm", ui::W - 6, kBarBottom + 3 + 9);
}

void DashScreen::drawTile(LGFX_Sprite& c, int x, const char* label, const char* value, const char* unit, Level level) {
    c.fillRoundRect(x, kTileY, kTileW, kTileH, 8, level == Level::Crit ? 0x4000 : ui::PANEL);
    c.setTextSize(1);  // the big numbers above leave a scale behind
    gUi.drawFitted(c, label, x + 8, kTileY + 13, kTileW - 12, gUi.thai20(), ui::DIM);
    const uint16_t col = levelColor(level, _now);
    const bool degrees = unit[0] == 'C';  // the GFX fonts have no degree sign: a small ring before "C"
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    const int unitW = level == Level::None ? 0 : c.textWidth(unit) + (degrees ? 6 : 2);
    const int right = x + kTileW - 8;
    if (unitW) {
        c.setTextColor(col);
        c.setTextDatum(textdatum_t::bottom_right);
        c.drawString(unit, right, kTileY + kTileH - 8);
        if (degrees) c.drawCircle(right - c.textWidth(unit) - 4, kTileY + kTileH - 21, 2, col);
    }
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextColor(col);
    c.setTextDatum(textdatum_t::bottom_right);
    c.drawString(value, right - unitW, kTileY + kTileH - 6);
}

void DashScreen::draw(LGFX_Sprite& c) {
    const DashView& v = _model.view();
    const bool moto = _model.config().kind == Kind::Motorcycle;
    c.fillScreen(ui::BG);

    // ---- header: back, status dot + source, settings ----
    c.setFont(gUi.thai20());
    c.setTextSize(1);
    const uint16_t chevron = _pressed == kBack ? ui::ACCENT : ui::TEXT;
    for (int t = 0; t < 3; t++) {  // "<" like the title bars
        c.drawLine(20 + t, 5, 12 + t, 13, chevron);
        c.drawLine(12 + t, 13, 20 + t, 21, chevron);
    }
    const bool sim = _source == (uint8_t)SourceId::SimAuto || _source == (uint8_t)SourceId::SimTouch;
    const uint16_t dot = v.live ? (sim ? ui::ACCENT : ui::OK) : (((_now / 400) & 1) ? ui::WARN : ui::PANEL);
    c.fillCircle(44, kHeaderH / 2, 5, dot);
    char head[96];
    snprintf(head, sizeof(head), "%s  %s", _status.c_str(), sourceNameTh((SourceId)_source));
    gUi.drawFitted(c, head, 56, kHeaderH / 2, kSetupX - 62, gUi.thai20(), v.live ? ui::TEXT : ui::DIM);
    gUi.drawButton(c, kSetupX, 2, ui::W - kSetupX - 4, kHeaderH - 4, "ตั้งค่า", _pressed == kSetup ? ui::PANEL_HI : ui::PANEL);

    drawBar(c, v);

    // ---- rpm (left), speed (middle), gear (right) ----
    char buf[16];
    if (v.rpmValid) withCommas(buf, sizeof(buf), lroundf(v.rpm / 10.0f) * 10);
    else snprintf(buf, sizeof(buf), "--");
    c.setTextDatum(textdatum_t::top_left);
    c.setFont(&fonts::FreeSansBold18pt7b);
    c.setTextSize(fitSize(c, &fonts::FreeSansBold18pt7b, buf, 108, 1.0f));
    c.setTextColor(v.shift ? kRed : (v.rpmValid ? kRpmText : ui::LINE));
    c.drawString(buf, 6, kMainTop + 4);
    c.setTextSize(1);
    gUi.drawFitted(c, "รอบ/นาที", 6, kMainTop + 50, 110, gUi.thai20(), ui::DIM);
    if (v.peakRpm > 0) {
        char peak[32], num[12];
        withCommas(num, sizeof(num), lroundf(v.peakRpm / 10.0f) * 10);
        snprintf(peak, sizeof(peak), "สูงสุด %s", num);
        gUi.drawFitted(c, peak, 6, kMainTop + 76, 110, gUi.thai20(), ui::DIM);
    }

    if (v.speedValid) snprintf(buf, sizeof(buf), "%ld", lroundf(v.speed));
    else snprintf(buf, sizeof(buf), "--");
    c.setFont(&fonts::FreeSansBold24pt7b);
    c.setTextSize(fitSize(c, &fonts::FreeSansBold24pt7b, "188", 132, 1.9f));  // the same size for 1-3 digits
    c.setTextColor(v.speedValid ? ui::TEXT : ui::LINE);  // no speed (most motorcycle ECUs): a quiet "--"
    c.setTextDatum(textdatum_t::top_right);
    c.drawString(buf, 250, kMainTop);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::bottom_right);
    c.drawString("km/h", 250, kMainBottom - 2);

    const int gx = 258, gw = ui::W - gx - 6, gy = kMainTop + 6, gh = 74;
    if (v.gear >= 0 || !moto) {
        c.drawRoundRect(gx, gy, gw, gh, 8, v.shift ? kRed : ui::LINE);
        if (v.gear > 0) snprintf(buf, sizeof(buf), "%d", v.gear);
        else snprintf(buf, sizeof(buf), v.gear == 0 ? "N" : "-");
        c.setFont(&fonts::FreeSansBold24pt7b);
        c.setTextSize(1.3f);
        c.setTextColor(v.gear == 0 ? kGreen : ui::TEXT);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(buf, gx + gw / 2, gy + gh / 2 + 2);
        c.setTextSize(1);
        c.setTextDatum(textdatum_t::middle_center);
        c.setFont(gUi.thai20());
        c.setTextColor(ui::DIM);
        c.drawString("เกียร์", gx + gw / 2, gy + gh + 10);
    }

    // ---- not receiving: say why over the main area ----
    if (!v.live) {
        const char* why = _source == (uint8_t)SourceId::Off ? "ยังไม่ได้เลือกแหล่งข้อมูล — แตะ ตั้งค่า"
                          : _needsRestart                   ? "รีสตาร์ทบอร์ด 1 ครั้งเพื่อเปิด Bluetooth"
                                                            : "กำลังรอข้อมูลจากรถ...";
        c.fillRoundRect(20, kMainTop + 22, ui::W - 40, 44, 8, ui::PANEL);
        c.setTextDatum(textdatum_t::middle_center);
        c.setTextSize(1);
        c.setFont(gUi.thai20());
        c.setTextColor(ui::WARN);
        c.drawString(why, ui::W / 2, kMainTop + 44);
    }

    // ---- tiles ----
    char val[16];
    if (v.coolantLevel != Level::None) snprintf(val, sizeof(val), "%.0f", v.coolant);
    else snprintf(val, sizeof(val), "--");
    drawTile(c, 6, moto ? "อุณหภูมิเครื่อง" : "เครื่องยนต์", val, "C", v.coolantLevel);
    if (v.batteryLevel != Level::None) snprintf(val, sizeof(val), "%.1f", v.battery);
    else snprintf(val, sizeof(val), "--");
    drawTile(c, 6 + kTileW + kTileGap, "แบตเตอรี่", val, "V", v.batteryLevel);
    if (v.intakeLevel != Level::None) {
        snprintf(val, sizeof(val), "%.0f", v.intake);
        drawTile(c, 6 + 2 * (kTileW + kTileGap), "อากาศเข้า", val, "C", v.intakeLevel);
    } else {
        if (v.throttleLevel != Level::None) snprintf(val, sizeof(val), "%.0f", v.throttle);
        else snprintf(val, sizeof(val), "--");
        drawTile(c, 6 + 2 * (kTileW + kTileGap), "คันเร่ง", val, "%", v.throttleLevel);
    }
}
