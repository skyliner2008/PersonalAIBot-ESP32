/**
 * @file DashScreen.cpp
 * @brief Vehicle dash screen, three styles (see DashScreen.h)
 */

#include "DashScreen.h"

#include <math.h>

#include <new>

#include "../phone/PhoneLink.h"
#include "../util/PsramAlloc.h"
#include "../vehicle/Vehicle.h"
#include "NavScreen.h"
#include "SettingsScreens.h"

DashScreen gDashUi;

using namespace vehicle;

namespace {

constexpr int kHeaderH = 26;
constexpr int kSetupX = 246;
constexpr float kPi = 3.14159265f;

// race
constexpr int kBarTop = 30, kBarBottom = 64, kSegments = 30;
constexpr int kMainTop = 80, kMainBottom = 178;
constexpr int kTileY = 184, kTileH = 52, kTileW = 100, kTileGap = 6;

// digital: one LED ring for the rpm (270 degrees, open at the bottom)
constexpr int kRingX = 116, kRingY = 134, kRingOuter = 102, kRingInner = 82, kRingSegs = 34;
constexpr float kArcStart = 135.0f, kArcSweep = 270.0f;  // degrees, 0 = 3 o'clock, clockwise

// classic: two dials with needles
constexpr int kDialR = 74, kTachX = 80, kSpeedX = 240, kDialY = 124;
constexpr int kStripY = 204, kStripH = 32;

// info: 3 x 3 values, the codes / health strip below
constexpr int kInfoTop = 30, kInfoTileH = 46, kInfoGap = 4, kInfoStripY = 182;
constexpr int kMilX = kSetupX - 32;  // the check-engine icon in the header

// palette (RGB565)
constexpr uint16_t kGreen = 0x47E8, kYellow = 0xFEA0, kRed = 0xF8E4, kOff = 0x18E3, kCold = 0x3D9F;
constexpr uint16_t kRpmText = 0x5F1F, kShiftWhite = 0xFFFF;
constexpr uint16_t kDialFace = 0x0861, kDialRim = 0x52AA, kNeedle = 0xFB40, kRedZone = 0xB000;

enum Hit { kNone = 0, kBack, kSetup, kStyle, kMain, kStrip };

Hit hitTest(int x, int y, bool info) {
    if (y < kHeaderH) return x < 48 ? kBack : (x >= kSetupX ? kSetup : kStyle);
    if (info && y >= kInfoStripY) return kStrip;
    return kMain;
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

/** Colour of a stretch of the rpm scale: green, yellow from 3/4 of the shift point, red from it. */
uint16_t rpmZoneColor(float rpm, const DashConfig& cfg) {
    return rpm >= cfg.shiftRpm ? kRed : (rpm >= cfg.shiftRpm * 0.75f ? kYellow : kGreen);
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

/** Point at `r` from (cx, cy) in direction `deg` (0 = 3 o'clock, clockwise: the screen's y grows down). */
void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    const float a = deg * kPi / 180.0f;
    x = cx + (int)lroundf(r * cosf(a));
    y = cy + (int)lroundf(r * sinf(a));
}

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

/** Speedometer full scale. */
int speedMax(bool moto) { return moto ? 140 : 200; }

/** Thousands per rpm label: one per 1000, or per 2000 on a scale above 10000. */
int rpmLabelStep(int rpmMax) { return rpmMax > 10000 ? 2000 : 1000; }

}  // namespace

// ============================================================ state, touch

void DashScreen::onEnter() {
    _nextConfig = 0;
    _fingerDown = false;
    _pressed = kNone;
    gVehicle.setTouchThrottle(0);
    tick(millis(), 0);
}

void DashScreen::cycleStyle() {
    _style = (DashStyle)(((uint8_t)_style + 1) % (uint8_t)DashStyle::Count);
    gVehicle.setStyle(_style);  // UI task: an internal stack, NVS may be written
    toast(nullptr);
}

void DashScreen::toast(const char* text) {
    _toastText = text;
    _toastUntil = _now + 1500;
}

void DashScreen::onTouch(const TouchEvent& e) {
    if (gUi.host()) gUi.host()->uiActivity();
    const bool info = _style == DashStyle::Info;
    switch (e.type) {
        case TouchEvent::Down:
            _pressed = hitTest(e.x, e.y, info);
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
            switch (hitTest(e.x, e.y, info)) {
                case kBack: gUi.pop(); break;
                case kSetup: settingsui::openPage("vehicle"); break;
                case kStyle:  // navigating: the strip opens the turn-by-turn screen
                    if (_nav.fresh(_now)) gUi.push(&gNavUi);
                    else cycleStyle();
                    break;
                case kStrip:  // the codes strip: read them now
                    toast(gVehicle.requestScan() ? "กำลังอ่านรหัสปัญหา..." : "อ่านรหัสปัญหาไม่ได้ตอนนี้");
                    break;
                default: break;
            }
            break;
        case TouchEvent::LongPress:
            if (info) {
                gVehicle.resetTrip();
                toast("เริ่มทริปใหม่");
            } else if (!_touchSource && hitTest(e.x, e.y, info) == kMain) {
                _model.resetPeak();
            }
            break;
    }
}

void DashScreen::tick(uint32_t now, float dt) {
    _now = now;
    if ((int32_t)(now - _nextConfig) >= 0) {  // settings may change from the menu, voice or console
        _nextConfig = now + 1000;
        _model.setConfig(gVehicle.dashConfig());
        const Vehicle::Settings s = gVehicle.settings();
        const uint8_t src = (uint8_t)s.source;
        if (src != _source) _model.resetPeak();  // a peak from the simulator is not this car's
        _source = src;
        _style = s.style;
        _touchSource = _source == (uint8_t)SourceId::SimTouch;
        _needsRestart = gVehicle.needsRestart();
        const obd::Diag d = gVehicle.diag();
        _mil = d.available && ((d.ready.valid && d.ready.mil) || d.stored.n);
        _codes = d.stored.n;
    }
    gVehicle.noteDashVisible(_style == DashStyle::Info);  // what the source polls fast
    _status = gVehicle.status();
    _nav = gPhone.nav();
    const Snapshot s = gVehicle.snapshot();
    const DashView& v = _model.update(s, now);
    if (_style == DashStyle::Info) {
        if (!_info) {
            if (void* m = psramMalloc(sizeof(InfoData))) _info = new (m) InfoData();
        }
        if (_info) {
            _info->snap = s;
            _info->diag = gVehicle.diag();
            _info->health = gVehicle.health();
        }
    }
    // the speedometer needle glides between readings (OBD / K-Line speed comes a few times a second)
    const float target = v.speedValid ? v.speed : 0;
    const float a = dt <= 0 ? 1.0f : clamp01(dt / 0.15f);
    _speedNeedle += (target - _speedNeedle) * a;
    // a dash in use keeps the screen awake (the power-save clock would hide it while driving)
    if (v.live && (int32_t)(now - _nextActivity) >= 0) {
        _nextActivity = now + 10000;
        if (gUi.host()) gUi.host()->uiActivity();
    }
}

// ============================================================ shared parts

void DashScreen::auxValues(const DashView& v, bool moto, Aux out[3]) const {
    out[0] = {moto ? "อุณหภูมิเครื่อง" : "เครื่องยนต์", "เครื่อง", "--", "C", v.coolantLevel};
    if (v.coolantLevel != Level::None) snprintf(out[0].value, sizeof(out[0].value), "%.0f", v.coolant);
    out[1] = {"แบตเตอรี่", "แบต", "--", "V", v.batteryLevel};
    if (v.batteryLevel != Level::None) snprintf(out[1].value, sizeof(out[1].value), "%.1f", v.battery);
    if (v.intakeLevel != Level::None) {
        out[2] = {"อากาศเข้า", "อากาศ", "", "C", v.intakeLevel};
        snprintf(out[2].value, sizeof(out[2].value), "%.0f", v.intake);
    } else {
        out[2] = {"คันเร่ง", "คันเร่ง", "--", "%", v.throttleLevel};
        if (v.throttleLevel != Level::None) snprintf(out[2].value, sizeof(out[2].value), "%.0f", v.throttle);
    }
}

void DashScreen::drawHeader(LGFX_Sprite& c, const DashView& v) {
    c.fillRect(0, 0, ui::W, kHeaderH, ui::BG);
    c.setTextSize(1);
    const uint16_t chevron = _pressed == kBack ? ui::ACCENT : ui::TEXT;
    for (int t = 0; t < 3; t++) {  // "<" like the title bars
        c.drawLine(20 + t, 5, 12 + t, 13, chevron);
        c.drawLine(12 + t, 13, 20 + t, 21, chevron);
    }
    if (_nav.fresh(_now)) {  // navigating: the next turn takes the status line (the dash stays the dash)
        NavScreen::drawIcon(c, _nav.icon, 46, kHeaderH / 2, 22, ui::ACCENT, ui::BG);
        char dist[24], line[240];
        phone::formatDistance(_nav.distM, dist, sizeof(dist), true);
        snprintf(line, sizeof(line), "%s  %s", dist, _nav.text[0] ? _nav.text : phone::TurnAnnouncer::iconTh(_nav.icon));
        gUi.drawFitted(c, line, 62, kHeaderH / 2, kSetupX - 66, gUi.thai20(),
                       _nav.distM >= 0 && _nav.distM < 200 ? ui::WARN : ui::TEXT);
        gUi.drawButton(c, kSetupX, 2, ui::W - kSetupX - 4, kHeaderH - 4, "ตั้งค่า", _pressed == kSetup ? ui::PANEL_HI : ui::PANEL);
        return;
    }
    const bool sim = _source == (uint8_t)SourceId::SimAuto || _source == (uint8_t)SourceId::SimTouch;
    const uint16_t dot = v.live ? (sim ? ui::ACCENT : ui::OK) : (((_now / 400) & 1) ? ui::WARN : ui::PANEL);
    c.fillCircle(44, kHeaderH / 2, 5, dot);
    char head[96];
    snprintf(head, sizeof(head), "%s  %s", _status.c_str(), sourceNameTh((SourceId)_source));
    gUi.drawFitted(c, head, 56, kHeaderH / 2, (_mil ? kMilX : kSetupX) - 62, gUi.thai20(),
                   _pressed == kStyle ? ui::ACCENT : (v.live ? ui::TEXT : ui::DIM));
    if (_mil) {  // check-engine light / stored codes: an amber engine (+ how many codes)
        drawEngineIcon(c, kMilX, 6, ui::WARN);
        if (_codes) {
            char n[4];
            snprintf(n, sizeof(n), "%u", _codes);
            c.setFont(&fonts::Font0);
            c.setTextSize(1);
            c.setTextColor(ui::WARN);
            c.setTextDatum(textdatum_t::top_left);
            c.drawString(n, kMilX + 23, 4);
        }
    }
    gUi.drawButton(c, kSetupX, 2, ui::W - kSetupX - 4, kHeaderH - 4, "ตั้งค่า", _pressed == kSetup ? ui::PANEL_HI : ui::PANEL);
}

// the usual check-engine symbol, drawn small: engine block, valve cover cap, intake on the left, fan on the right
void DashScreen::drawEngineIcon(LGFX_Sprite& c, int x, int y, uint16_t col) {
    c.fillRect(x + 5, y + 3, 12, 9, col);
    c.fillRect(x + 8, y, 6, 3, col);
    c.fillRect(x + 1, y + 5, 4, 5, col);
    c.fillRect(x, y + 4, 2, 7, col);
    c.fillRect(x + 17, y + 5, 2, 3, col);
    c.fillRect(x + 19, y + 2, 2, 9, col);
    c.fillRect(x + 9, y + 6, 4, 3, ui::BG);  // a window in the block: reads as an engine, not a box
}

void DashScreen::drawWaiting(LGFX_Sprite& c, int y) {
    const char* why = _source == (uint8_t)SourceId::Off ? "ยังไม่ได้เลือกแหล่งข้อมูล — แตะ ตั้งค่า"
                      : _needsRestart                   ? "รีสตาร์ทบอร์ด 1 ครั้งเพื่อเปิด Bluetooth"
                                                        : "กำลังรอข้อมูลจากรถ...";
    c.fillRoundRect(20, y - 22, ui::W - 40, 44, 8, ui::PANEL);
    c.setTextDatum(textdatum_t::middle_center);
    c.setTextSize(1);
    c.setFont(gUi.thai20());
    c.setTextColor(ui::WARN);
    c.drawString(why, ui::W / 2, y);
}

void DashScreen::drawToast(LGFX_Sprite& c) {
    if ((int32_t)(_now - _toastUntil) >= 0) return;
    char t[80];
    if (_toastText) snprintf(t, sizeof(t), "%s", _toastText);
    else snprintf(t, sizeof(t), "หน้าปัด: %s", styleNameTh(_style));
    c.setFont(gUi.thai20());
    c.setTextSize(1);
    const int w = c.textWidth(t) + 28;
    c.fillRoundRect((ui::W - w) / 2, kHeaderH + 4, w, 30, 15, ui::PANEL_HI);
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(t, ui::W / 2, kHeaderH + 19);
}

void DashScreen::drawReadout(LGFX_Sprite& c, int x, int y, int w, int h, const Aux& a) {
    c.fillRoundRect(x, y, w, h, 8, a.level == Level::Crit ? 0x4000 : ui::PANEL);
    c.setTextSize(1);  // the big numbers drawn before leave a scale behind
    const bool tall = h >= 44;
    gUi.drawFitted(c, tall ? a.label : a.shortLabel, x + 8, tall ? y + 13 : y + h / 2, tall ? w - 12 : w / 2 - 4,
                   gUi.thai20(), ui::DIM);
    const uint16_t col = levelColor(a.level, _now);
    const bool degrees = a.unit[0] == 'C';  // the GFX fonts have no degree sign: a small ring before "C"
    c.setFont(&fonts::Font2);
    const int unitW = a.level == Level::None ? 0 : c.textWidth(a.unit) + (degrees ? 6 : 2);
    const int right = x + w - 8;
    const int base = tall ? y + h - 6 : y + h / 2 + 9;
    if (unitW) {
        c.setTextColor(col);
        c.setTextDatum(textdatum_t::bottom_right);
        c.drawString(a.unit, right, base - 2);
        if (degrees) c.drawCircle(right - c.textWidth(a.unit) - 4, base - 15, 2, col);
    }
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextColor(col);
    c.setTextDatum(textdatum_t::bottom_right);
    c.drawString(a.value, right - unitW, base);
}

void DashScreen::draw(LGFX_Sprite& c) {
    const DashView& v = _model.view();
    const bool moto = _model.config().kind == Kind::Motorcycle;
    switch (_style) {
        case DashStyle::Digital: drawDigital(c, v, moto); break;
        case DashStyle::Classic: drawClassic(c, v, moto); break;
        case DashStyle::Info: drawInfo(c, v, moto); break;
        default: drawRace(c, v, moto); break;
    }
    drawHeader(c, v);
    if (!v.live) drawWaiting(c, _style == DashStyle::Race ? kMainTop + 44 : 128);
    drawToast(c);
}

// ============================================================ race: bar across the top, big digits

void DashScreen::drawRace(LGFX_Sprite& c, const DashView& v, bool moto) {
    const DashConfig& cfg = _model.config();
    c.fillScreen(ui::BG);

    // ---- rpm bar, rising toward the redline ----
    const float pitch = (ui::W - 12) / (float)kSegments;
    const float lit = v.rpmValid ? v.rpm / cfg.rpmMax * kSegments : 0;
    for (int i = 0; i < kSegments; i++) {
        const int x = 6 + (int)(i * pitch);
        const int w = (int)((i + 1) * pitch) - (int)(i * pitch) - 2;
        const int h = 14 + (kBarBottom - kBarTop - 14) * i / (kSegments - 1);
        const float segRpm = (i + 0.5f) * cfg.rpmMax / kSegments;
        uint16_t col = rpmZoneColor(segRpm, cfg);
        if (v.shift) col = v.shiftOn ? kShiftWhite : kRed;  // shift light: the whole bar flashes
        if (i + 0.5f > lit && !(v.shift && v.shiftOn)) col = segRpm >= cfg.shiftRpm ? 0x4800 : kOff;
        c.fillRect(x, kBarBottom - h, w, h, col);
    }
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::top_center);
    for (int k = 0; k * 1000 <= cfg.rpmMax; k += rpmLabelStep(cfg.rpmMax) / 1000) {
        const int x = 6 + (int)((ui::W - 12) * (k * 1000.0f / cfg.rpmMax));
        char t[12];
        snprintf(t, sizeof(t), "%d", k);
        c.drawString(t, x < 8 ? 8 : (x > ui::W - 8 ? ui::W - 8 : x), kBarBottom + 3);
    }
    c.setTextDatum(textdatum_t::top_right);
    c.drawString("x1000 rpm", ui::W - 6, kBarBottom + 3 + 9);

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
        c.setFont(gUi.thai20());
        c.setTextColor(ui::DIM);
        c.drawString("เกียร์", gx + gw / 2, gy + gh + 10);
    }

    Aux aux[3];
    auxValues(v, moto, aux);
    for (int i = 0; i < 3; i++) drawReadout(c, 6 + i * (kTileW + kTileGap), kTileY, kTileW, kTileH, aux[i]);
}

// ============================================================ digital: LED ring + digital speed (sport)

void DashScreen::drawDigital(LGFX_Sprite& c, const DashView& v, bool moto) {
    const DashConfig& cfg = _model.config();
    c.fillScreen(ui::BG);

    // ---- rpm ring ----
    const float segSweep = kArcSweep / kRingSegs;
    const float lit = v.rpmValid ? v.rpm / cfg.rpmMax * kRingSegs : 0;
    for (int i = 0; i < kRingSegs; i++) {
        const float a0 = kArcStart + i * segSweep + 0.8f, a1 = kArcStart + (i + 1) * segSweep - 0.8f;
        const float segRpm = (i + 0.5f) * cfg.rpmMax / kRingSegs;
        uint16_t col = rpmZoneColor(segRpm, cfg);
        if (v.shift) col = v.shiftOn ? kShiftWhite : kRed;
        if (i + 0.5f > lit && !(v.shift && v.shiftOn)) col = segRpm >= cfg.shiftRpm ? 0x4800 : kOff;
        // the lit segments reach further out: a glowing ring over a dim track
        c.fillArc(kRingX, kRingY, kRingInner, i + 0.5f <= lit ? kRingOuter : kRingOuter - 6, a0, a1, col);
    }
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::middle_center);
    const int step = rpmLabelStep(cfg.rpmMax);
    for (int r = 0; r <= cfg.rpmMax; r += step) {
        int x, y;
        polar(kRingX, kRingY, kRingInner - 12, kArcStart + kArcSweep * r / cfg.rpmMax, x, y);
        char t[12];
        snprintf(t, sizeof(t), "%d", r / 1000);
        c.drawString(t, x, y);
    }

    // ---- rpm, speed, gear inside the ring ----
    char buf[16];
    if (v.rpmValid) withCommas(buf, sizeof(buf), lroundf(v.rpm / 10.0f) * 10);
    else snprintf(buf, sizeof(buf), "--");
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextColor(v.shift ? kRed : (v.rpmValid ? kRpmText : ui::LINE));
    c.drawString(buf, kRingX, kRingY - 44);
    c.setFont(&fonts::Font0);
    c.setTextColor(ui::DIM);
    c.drawString("RPM", kRingX, kRingY - 28);

    if (v.speedValid) snprintf(buf, sizeof(buf), "%ld", lroundf(v.speed));
    else snprintf(buf, sizeof(buf), "--");
    c.setFont(&fonts::FreeSansBold24pt7b);
    c.setTextSize(fitSize(c, &fonts::FreeSansBold24pt7b, "188", 104, 1.5f));
    c.setTextColor(v.speedValid ? ui::TEXT : ui::LINE);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(buf, kRingX, kRingY + 8);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(ui::DIM);
    c.drawString("km/h", kRingX, kRingY + 42);

    if (v.gear >= 0 || !moto) {  // in the open bottom of the ring
        if (v.gear > 0) snprintf(buf, sizeof(buf), "%d", v.gear);
        else snprintf(buf, sizeof(buf), v.gear == 0 ? "N" : "-");
        c.fillCircle(kRingX, kRingY + 80, 20, ui::PANEL);
        c.drawCircle(kRingX, kRingY + 80, 20, v.shift ? kRed : ui::LINE);
        c.setFont(&fonts::FreeSansBold18pt7b);
        c.setTextColor(v.gear == 0 ? kGreen : ui::TEXT);
        c.drawString(buf, kRingX, kRingY + 81);
    }

    // ---- readouts on the right ----
    Aux aux[3];
    auxValues(v, moto, aux);
    for (int i = 0; i < 3; i++) drawReadout(c, 228, 32 + i * 69, ui::W - 228 - 4, 64, aux[i]);
}

// ============================================================ classic: two dials with needles

void DashScreen::buildClassicFace(LGFX_Sprite& f, bool moto) {
    const DashConfig& cfg = _model.config();
    f.fillScreen(ui::BG);
    auto dial = [&](int cx) {
        f.fillCircle(cx, kDialY, kDialR, kDialRim);           // bezel
        f.fillCircle(cx, kDialY, kDialR - 3, 0x2104);
        f.fillCircle(cx, kDialY, kDialR - 5, kDialFace);
    };
    auto tick = [&](int cx, float frac, int len, float width, uint16_t col) {
        const float a = kArcStart + kArcSweep * frac;
        int x0, y0, x1, y1;
        polar(cx, kDialY, kDialR - 8, a, x0, y0);
        polar(cx, kDialY, kDialR - 8 - len, a, x1, y1);
        f.drawWideLine(x0, y0, x1, y1, width, col);
    };
    f.setFont(&fonts::Font2);
    f.setTextSize(1);
    f.setTextDatum(textdatum_t::middle_center);

    // tachometer: red zone from the shift point, a mark every 500 rpm, numbers in thousands
    dial(kTachX);
    const float redFrom = clamp01((float)cfg.shiftRpm / cfg.rpmMax);
    f.fillArc(kTachX, kDialY, kDialR - 14, kDialR - 8, kArcStart + kArcSweep * redFrom, kArcStart + kArcSweep, kRedZone);
    for (int r = 0; r <= cfg.rpmMax; r += 500) {
        const bool major = r % 1000 == 0;
        tick(kTachX, (float)r / cfg.rpmMax, major ? 9 : 5, major ? 1.3f : 0.7f, r >= cfg.shiftRpm ? kRed : ui::TEXT);
    }
    const int step = rpmLabelStep(cfg.rpmMax);
    for (int r = 0; r <= cfg.rpmMax; r += step) {
        int x, y;
        polar(kTachX, kDialY, kDialR - 27, kArcStart + kArcSweep * r / cfg.rpmMax, x, y);
        char t[12];
        snprintf(t, sizeof(t), "%d", r / 1000);
        f.setTextColor(r >= cfg.shiftRpm ? kRed : ui::TEXT);
        f.drawString(t, x, y);
    }
    f.setFont(&fonts::Font0);
    f.setTextColor(ui::DIM);
    f.drawString("x1000 r/min", kTachX, kDialY + 22);

    // speedometer: a mark every 10 km/h, numbers every 20
    dial(kSpeedX);
    const int vmax = speedMax(moto);
    for (int s = 0; s <= vmax; s += 10) {
        const bool major = s % 20 == 0;
        tick(kSpeedX, (float)s / vmax, major ? 9 : 5, major ? 1.3f : 0.7f, ui::TEXT);
    }
    f.setFont(&fonts::Font2);
    f.setTextColor(ui::TEXT);
    for (int s = 0; s <= vmax; s += vmax > 160 ? 40 : 20) {  // 0-200: every 40, else the top numbers crowd
        int x, y;
        polar(kSpeedX, kDialY, kDialR - 28, kArcStart + kArcSweep * s / vmax, x, y);
        char t[12];
        snprintf(t, sizeof(t), "%d", s);
        f.drawString(t, x, y);
    }
    f.setFont(&fonts::Font0);
    f.setTextColor(ui::DIM);
    f.drawString("km/h", kSpeedX, kDialY + 22);
}

void DashScreen::drawClassic(LGFX_Sprite& c, const DashView& v, bool moto) {
    const DashConfig& cfg = _model.config();
    // the faces change only with the settings: drawn once into a sprite, copied every frame
    const uint32_t key = 0x80000000u | (moto ? 0x40000000u : 0) | (uint32_t)cfg.rpmMax << 15 | (uint32_t)cfg.shiftRpm;
    if (!_face) {
        _face = new LGFX_Sprite(&c);
        _face->setColorDepth(16);
        _face->setPsram(true);
        if (!_face->createSprite(ui::W, ui::H)) {
            delete _face;
            _face = nullptr;
        }
        _faceKey = 0;
    }
    if (_face && _faceKey != key) {
        buildClassicFace(*_face, moto);
        _faceKey = key;
    }
    if (_face) _face->pushSprite(&c, 0, 0);
    else buildClassicFace(c, moto);  // no memory for the cache: draw it every frame

    char buf[16];
    // ---- windows inside the dials: gear (tachometer), digital speed (speedometer) ----
    const int wy = kDialY + 34;
    if (v.gear >= 0 || !moto) {
        if (v.gear > 0) snprintf(buf, sizeof(buf), "%d", v.gear);
        else snprintf(buf, sizeof(buf), v.gear == 0 ? "N" : "-");
        c.fillRoundRect(kTachX - 16, wy, 32, 28, 4, ui::BG);
        c.drawRoundRect(kTachX - 16, wy, 32, 28, 4, ui::LINE);
        c.setFont(&fonts::FreeSansBold12pt7b);
        c.setTextSize(1);
        c.setTextColor(v.gear == 0 ? kGreen : kYellow);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(buf, kTachX, wy + 15);
    }
    if (v.speedValid) snprintf(buf, sizeof(buf), "%ld", lroundf(v.speed));
    else snprintf(buf, sizeof(buf), "--");
    c.fillRoundRect(kSpeedX - 28, wy, 56, 28, 4, ui::BG);
    c.drawRoundRect(kSpeedX - 28, wy, 56, 28, 4, ui::LINE);
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextSize(1);
    c.setTextColor(v.speedValid ? kYellow : ui::LINE);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(buf, kSpeedX, wy + 15);

    // ---- needles: tapered, with a short tail and a cap ----
    auto needle = [&](int cx, float frac) {
        const float a = kArcStart + kArcSweep * clamp01(frac);
        int tx, ty, bx, by;
        polar(cx, kDialY, kDialR - 12, a, tx, ty);
        polar(cx, kDialY, 14, a + 180.0f, bx, by);
        c.drawWedgeLine(bx, by, tx, ty, 3.2f, 1.0f, kNeedle);
        c.drawWideLine(cx, kDialY, cx, kDialY, 7.0f, 0x6B6D);
        c.drawWideLine(cx, kDialY, cx, kDialY, 3.0f, 0x2104);
    };
    needle(kTachX, v.rpmValid ? v.rpm / cfg.rpmMax : 0);
    needle(kSpeedX, _speedNeedle / speedMax(moto));

    // ---- shift lamp between the dials ----
    const uint16_t lamp = v.shift ? (v.shiftOn ? kRed : 0x7800) : 0x2000;
    if (v.shift && v.shiftOn) c.drawWideLine(ui::W / 2, 46, ui::W / 2, 46, 12.0f, 0x5000);  // glow
    c.drawWideLine(ui::W / 2, 46, ui::W / 2, 46, 8.0f, lamp);
    c.setFont(&fonts::Font0);
    c.setTextColor(v.shift ? kRed : ui::LINE);
    c.setTextDatum(textdatum_t::top_center);
    c.drawString("SHIFT", ui::W / 2, 58);

    // ---- readouts along the bottom ----
    Aux aux[3];
    auxValues(v, moto, aux);
    for (int i = 0; i < 3; i++) drawReadout(c, 6 + i * (kTileW + kTileGap), kStripY, kTileW, kStripH, aux[i]);
}

// ============================================================ info: values + trouble codes + health (T112)

void DashScreen::drawInfo(LGFX_Sprite& c, const DashView& v, bool moto) {
    (void)moto;
    c.fillScreen(ui::BG);
    if (!_info) return;
    const Snapshot& s = _info->snap;
    const obd::Diag& d = _info->diag;
    const Health& h = _info->health;
    const uint32_t now = _now;

    // ---- nine values: the four basics always, then what this car has (the rest as "--") ----
    struct Cand {
        const char* label;
        bool ok;
        float value;
        int decimals;
        const char* unit;
        Level level;
    };
    Cand cand[16];
    int nc = 0;
    auto add = [&](const char* label, bool ok, float value, int decimals, const char* unit, Level level) {
        if (nc < 16) cand[nc++] = {label, ok, value, decimals, unit, level};
    };
    add("ความเร็ว", v.speedValid, v.speed, 0, "km/h", Level::Normal);
    add("รอบเครื่อง", v.rpmValid, s.value(kRpm), 0, "rpm", v.shift ? Level::Warn : Level::Normal);
    add("เครื่องยนต์", v.coolantLevel != Level::None, v.coolant, 0, "C", v.coolantLevel);
    add("แบตเตอรี่", v.batteryLevel != Level::None, v.battery, 1, "V", v.batteryLevel);
    const bool stOk = s.fresh(kStftB1, now), ltOk = s.fresh(kLtftB1, now);
    const float trim = (stOk ? s.value(kStftB1) : 0) + (ltOk ? s.value(kLtftB1) : 0);
    const float at = fabsf(trim);
    add("ชดเชยน้ำมัน", stOk || ltOk, trim, 0, "%", at > 25 ? Level::Crit : at > 12 ? Level::Warn : Level::Normal);
    add("ภาระเครื่อง", s.fresh(kLoad, now), s.value(kLoad), 0, "%", Level::Normal);
    add("น้ำมันในถัง", s.fresh(kFuelLevel, now), s.value(kFuelLevel), 0, "%",
        s.value(kFuelLevel) < 12 ? Level::Warn : Level::Normal);
    const Trip& t = h.trip;
    if (t.instKmL > 0) add("กม./ลิตร", true, t.instKmL, 1, "km/l", Level::Normal);
    else if (t.fuelFrom && h.running) add("ใช้น้ำมัน", true, t.lph, 1, "l/h", Level::Normal);
    else add("เฉลี่ย กม./ลิตร", t.avgKmL() > 0, t.avgKmL(), 1, "km/l", Level::Normal);
    add("อากาศเข้า", v.intakeLevel != Level::None, v.intake, 0, "C", v.intakeLevel);
    add("ท่อไอดี", s.fresh(kMap, now), s.value(kMap), 0, "kPa", Level::Normal);
    add("ลมเข้า (MAF)", s.fresh(kMaf, now), s.value(kMaf), 1, "g/s", Level::Normal);
    add("คันเร่ง", s.fresh(kThrottle, now), s.value(kThrottle), 0, "%", Level::Normal);
    add("องศาไฟจุดระเบิด", s.fresh(kTiming, now), s.value(kTiming), 0, "deg", Level::Normal);
    add("น้ำมันเครื่อง", s.fresh(kOilTemp, now), s.value(kOilTemp), 0, "C", Level::Normal);
    add("อากาศภายนอก", s.fresh(kAmbient, now), s.value(kAmbient), 0, "C", Level::Normal);
    add("ระยะทริป", t.km > 0.05f, t.km, 1, "km", Level::Normal);
    int pick[9], np = 0;
    for (int i = 0; i < 4; i++) pick[np++] = i;
    for (int i = 4; i < nc && np < 9; i++) {
        if (cand[i].ok) pick[np++] = i;
    }
    for (int i = 4; i < nc && np < 9; i++) {
        if (!cand[i].ok) pick[np++] = i;
    }
    const int tileW = (ui::W - 12 - 2 * kInfoGap) / 3;
    for (int k = 0; k < np; k++) {
        const Cand& cd = cand[pick[k]];
        Aux a = {cd.label, cd.label, "--", cd.unit, cd.ok ? cd.level : Level::None};
        if (cd.ok) snprintf(a.value, sizeof(a.value), cd.decimals ? "%.1f" : "%.0f", cd.value);
        drawReadout(c, 6 + (k % 3) * (tileW + kInfoGap), kInfoTop + (k / 3) * (kInfoTileH + kInfoGap), tileW, kInfoTileH, a);
    }

    // ---- the strip: trouble codes (or the trip), the most serious health finding ----
    c.fillRoundRect(6, kInfoStripY, ui::W - 12, ui::H - kInfoStripY - 4, 8,
                    _pressed == kStrip ? ui::PANEL_HI : ui::PANEL);
    char line[200];
    uint16_t col = ui::TEXT;
    if (d.available) {
        if (!d.codesValid) {
            snprintf(line, sizeof(line), "กำลังอ่านรหัสปัญหา...");
            col = ui::DIM;
        } else if (d.stored.n || d.pending.n) {
            int len = snprintf(line, sizeof(line), "%s", d.simulated ? "(จำลอง) รหัส:" : "รหัสปัญหา:");
            uint8_t worst = 1;
            for (uint8_t i = 0; i < d.stored.n && len < (int)sizeof(line) - 8; i++) {
                char code[6];
                obd::formatDtc(d.stored.code[i], code);
                len += snprintf(line + len, sizeof(line) - len, " %s", code);
                const uint8_t sv = obd::describeDtc(d.stored.code[i]).severity;
                if (sv > worst) worst = sv;
            }
            if (d.pending.n && len < (int)sizeof(line) - 30) len += snprintf(line + len, sizeof(line) - len, "  รอยืนยัน:");
            for (uint8_t i = 0; i < d.pending.n && len < (int)sizeof(line) - 8; i++) {
                char code[6];
                obd::formatDtc(d.pending.code[i], code);
                len += snprintf(line + len, sizeof(line) - len, " %s", code);
            }
            col = d.stored.n ? (worst >= 3 ? ui::ERR : ui::WARN) : ui::DIM;
        } else {
            snprintf(line, sizeof(line), "ไม่พบรหัสปัญหา%s", d.ready.valid && d.ready.total ? "" : " (แตะเพื่ออ่านใหม่)");
            if (d.ready.valid && d.ready.total) {
                const size_t len = strlen(line);
                snprintf(line + len, sizeof(line) - len, " • ระบบพร้อม %u/%u", d.ready.done, d.ready.total);
            }
            col = ui::OK;
        }
    } else {
        snprintf(line, sizeof(line), "ทริป %.1f กม. • %.0f นาที", t.km, t.runMin);  // (the Thai font has • not ·)
        if (t.avgKmL() > 0) {
            const size_t len = strlen(line);
            snprintf(line + len, sizeof(line) - len, " • %.1f กม./ลิตร", t.avgKmL());
        }
    }
    gUi.drawFitted(c, line, 14, kInfoStripY + 14, ui::W - 28, gUi.thai20(), col);
    if (h.n) {
        const Issue& is = h.issue[0];
        char num[24] = "";
        switch (is.id) {
            case Finding::LeanB1: case Finding::RichB1: case Finding::LeanB2: case Finding::RichB2: case Finding::LeanAtIdle:
                snprintf(num, sizeof(num), " %+.0f%%", is.value);
                break;
            case Finding::Overheat: case Finding::ColdEngine: case Finding::HotIntake:
                snprintf(num, sizeof(num), " %.0f°C", is.value);
                break;
            case Finding::OverheatRising: snprintf(num, sizeof(num), " +%.1f°C/นาที", is.value); break;
            case Finding::NotCharging: case Finding::Overcharge: case Finding::LowBatteryParked: case Finding::WeakStart:
                snprintf(num, sizeof(num), " %.1f V", is.value);
                break;
            case Finding::LowFuel: snprintf(num, sizeof(num), " %.0f%%", is.value); break;
            case Finding::RoughIdle: snprintf(num, sizeof(num), " แกว่ง %.0f rpm", is.value); break;
            default: break;
        }
        snprintf(line, sizeof(line), "%s%s%s", findingTh(is.id), num, h.n > 1 ? "  (+อีก)" : "");
        col = is.sev >= kSevCrit ? ui::ERR : is.sev == kSevWarn ? ui::WARN : ui::TEXT;
    } else {
        snprintf(line, sizeof(line), "%s", v.live ? "ไม่พบสิ่งผิดปกติจากค่าที่อ่านได้" : "");
        col = ui::OK;
    }
    gUi.drawFitted(c, line, 14, kInfoStripY + 39, ui::W - 28, gUi.thai20(), col);
}
