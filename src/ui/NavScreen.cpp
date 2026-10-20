/**
 * @file NavScreen.cpp
 * @brief Turn-by-turn screen + the arrows (wiki 18)
 */

#include "NavScreen.h"

#include <math.h>

#include "../phone/PhoneLink.h"
#include "../util/TextWrap.h"

using phone::Icon;

NavScreen gNavUi;

namespace {
constexpr int kHeaderH = 30;
constexpr int kBottomY = 204;

struct Pt {
    float x, y;
};

// a stroke through `pts` (unit box -0.5..0.5) with an arrow head at the last point
void strokeArrow(LGFX_Sprite& c, const Pt* pts, int n, int cx, int cy, int size, uint16_t color) {
    const float s = (float)size, r = s * 0.075f;
    auto X = [&](float u) { return cx + u * s; };
    auto Y = [&](float v) { return cy + v * s; };
    const Pt& a = pts[n - 2];
    const Pt& b = pts[n - 1];
    float dx = b.x - a.x, dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    dx /= len;
    dy /= len;
    const float head = 0.26f, half = 0.21f;
    for (int i = 0; i + 1 < n; i++) {
        Pt p = pts[i], q = pts[i + 1];
        if (i + 2 == n) q = {b.x - dx * head * 0.8f, b.y - dy * head * 0.8f};  // the shaft stops inside the head
        c.drawWideLine(X(p.x), Y(p.y), X(q.x), Y(q.y), r, color);
        c.fillCircle((int)X(q.x), (int)Y(q.y), (int)r, color);  // round joints
    }
    const float bx = b.x - dx * head, by = b.y - dy * head;
    c.fillTriangle((int)X(b.x), (int)Y(b.y), (int)X(bx - dy * half), (int)Y(by + dx * half), (int)X(bx + dy * half),
                   (int)Y(by - dx * half), color);
}

void mirrored(const Pt* in, int n, Pt* out) {
    for (int i = 0; i < n; i++) out[i] = {-in[i].x, in[i].y};
}
}  // namespace

void NavScreen::drawIcon(LGFX_Sprite& c, Icon icon, int cx, int cy, int size, uint16_t color, uint16_t bg) {
    static const Pt kStraight[] = {{0, 0.5f}, {0, -0.5f}};
    static const Pt kRight[] = {{-0.22f, 0.5f}, {-0.22f, -0.05f}, {0.48f, -0.05f}};
    static const Pt kSlight[] = {{-0.12f, 0.5f}, {-0.12f, 0.1f}, {0.32f, -0.45f}};
    static const Pt kSharp[] = {{-0.25f, 0.5f}, {-0.25f, -0.35f}, {0.38f, 0.3f}};
    static const Pt kKeep[] = {{-0.18f, 0.5f}, {-0.18f, 0.15f}, {0.16f, -0.2f}, {0.16f, -0.5f}};
    // Thailand drives on the left: a U-turn goes round to the right
    static const Pt kUTurn[] = {{-0.25f, 0.5f}, {-0.25f, -0.15f}, {-0.18f, -0.33f}, {0, -0.42f},
                                {0.18f, -0.33f}, {0.25f, -0.15f}, {0.25f, 0.4f}};
    static const Pt kMerge[] = {{-0.38f, 0.5f}, {0, 0.05f}, {0, -0.5f}};
    Pt m[8];
    switch (icon) {
        case Icon::Right: strokeArrow(c, kRight, 3, cx, cy, size, color); break;
        case Icon::Left: mirrored(kRight, 3, m), strokeArrow(c, m, 3, cx, cy, size, color); break;
        case Icon::SlightRight: strokeArrow(c, kSlight, 3, cx, cy, size, color); break;
        case Icon::SlightLeft: mirrored(kSlight, 3, m), strokeArrow(c, m, 3, cx, cy, size, color); break;
        case Icon::SharpRight: strokeArrow(c, kSharp, 3, cx, cy, size, color); break;
        case Icon::SharpLeft: mirrored(kSharp, 3, m), strokeArrow(c, m, 3, cx, cy, size, color); break;
        case Icon::KeepRight: strokeArrow(c, kKeep, 4, cx, cy, size, color); break;
        case Icon::KeepLeft: mirrored(kKeep, 4, m), strokeArrow(c, m, 4, cx, cy, size, color); break;
        case Icon::UTurn: strokeArrow(c, kUTurn, 7, cx, cy, size, color); break;
        case Icon::Merge: strokeArrow(c, kMerge, 3, cx, cy, size, color); break;
        case Icon::Roundabout: {
            const int r = size * 22 / 100, w = size * 15 / 100;
            const int ry = cy - size * 5 / 100;
            c.fillCircle(cx, ry, r + w / 2, color);
            c.fillCircle(cx, ry, r - w / 2, bg);
            c.drawWideLine(cx, cy + size / 2, cx, ry + r, size * 0.075f, color);
            static const Pt kExit[] = {{0.12f, -0.22f}, {0.42f, -0.48f}};
            strokeArrow(c, kExit, 2, cx, cy, size, color);
            break;
        }
        case Icon::Arrive: {  // a map pin
            const int r = size * 26 / 100, py = cy - size * 14 / 100;
            c.fillTriangle(cx - r * 87 / 100, py + r / 2, cx + r * 87 / 100, py + r / 2, cx, cy + size * 45 / 100, color);
            c.fillCircle(cx, py, r, color);
            c.fillCircle(cx, py, r * 4 / 10, bg);
            break;
        }
        default: strokeArrow(c, kStraight, 2, cx, cy, size, color); break;
    }
}

void NavScreen::onEnter() {
    _pressedBack = false;
    _nextRead = 0;
}

void NavScreen::onTouch(const TouchEvent& e) {
    if (gUi.host()) gUi.host()->uiActivity();
    const bool back = e.x < 60 && e.y < kHeaderH + 6;
    if (e.type == TouchEvent::Down) _pressedBack = back;
    if (e.type == TouchEvent::Up) _pressedBack = false;
    if (e.type == TouchEvent::Tap && back) gUi.pop();
}

void NavScreen::tick(uint32_t now, float) {
    _now = now;
    if ((int32_t)(now - _nextRead) < 0) return;
    _nextRead = now + 250;
    _nav = gPhone.nav();
    _gps = gPhone.gps();
    _place = gPhone.place();
    _connected = gPhone.connected();
    _via = PhoneLink::viaName(gPhone.via());
    // navigating keeps the screen awake (the power-save clock would hide the next turn)
    static uint32_t nextActivity = 0;
    if (_nav.fresh(now) && (int32_t)(now - nextActivity) >= 0 && gUi.host()) {
        nextActivity = now + 10000;
        gUi.host()->uiActivity();
    }
}

void NavScreen::drawWrapped(LGFX_Sprite& c, const char* text, int x, int y, int w, int maxLines, int lineH) {
    auto widthOf = [&](uint32_t cp) -> int {
        char b[5] = {0};
        if (cp < 0x80) b[0] = (char)cp;
        else if (cp < 0x800) b[0] = (char)(0xC0 | (cp >> 6)), b[1] = (char)(0x80 | (cp & 0x3F));
        else b[0] = (char)(0xE0 | (cp >> 12)), b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F));
        return c.textWidth(b);
    };
    const std::vector<textwrap::Line> lines = textwrap::wrap(text, strlen(text), w, widthOf);
    const int n = (int)lines.size() < maxLines ? (int)lines.size() : maxLines;
    int ly = y - (n - 1) * lineH / 2;  // centred on y
    char line[200];
    for (int k = 0; k < n; k++, ly += lineH) {
        const size_t len = lines[k].len < sizeof(line) - 4 ? lines[k].len : sizeof(line) - 4;
        memcpy(line, text + lines[k].start, len);
        line[len] = '\0';
        if (k == n - 1 && (int)lines.size() > maxLines) strcat(line, "...");
        c.drawString(line, x, ly);
    }
}

void NavScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    // ---- header ----
    const uint16_t chevron = _pressedBack ? ui::ACCENT : ui::TEXT;
    for (int t = 0; t < 3; t++) {
        c.drawLine(20 + t, 7, 12 + t, 15, chevron);
        c.drawLine(12 + t, 15, 20 + t, 23, chevron);
    }
    c.setTextSize(1);
    c.setFont(gUi.thai20());
    c.setTextDatum(textdatum_t::middle_left);
    c.setTextColor(ui::TEXT);
    c.drawString("นำทาง", 36, kHeaderH / 2);
    const char* link = !_connected ? "มือถือไม่ได้เชื่อม" : !strcmp(_via, "wifi") ? "มือถือ • Wi-Fi" : "มือถือ • USB";
    c.setTextDatum(textdatum_t::middle_right);
    c.setTextColor(_connected ? ui::OK : ui::WARN);
    c.drawString(link, ui::W - 8, kHeaderH / 2);
    c.drawFastHLine(0, kHeaderH, ui::W, ui::LINE);

    const bool nav = _nav.fresh(_now);
    if (!nav) {
        c.setTextDatum(textdatum_t::middle_center);
        c.setFont(gUi.thai26());
        c.setTextColor(ui::DIM);
        c.drawString("ยังไม่ได้นำทาง", ui::W / 2, 82);
        c.setFont(gUi.thai20());
        c.drawString("พูดว่า \"นำทางไป ...\"", ui::W / 2, 120);
        c.drawString("หรือเริ่มใน Google Maps บนมือถือ", ui::W / 2, 146);
        if (_place.length()) {
            c.setTextColor(ui::TEXT);
            gUi.drawFitted(c, _place.c_str(), ui::W / 2, 180, ui::W - 24, gUi.thai20(), ui::TEXT, textdatum_t::middle_center);
        }
    } else {
        // ---- the arrow + distance (left), what to do (right) ----
        c.fillRoundRect(6, kHeaderH + 6, 124, kBottomY - kHeaderH - 12, 12, ui::PANEL);
        drawIcon(c, _nav.icon, 68, 98, 92, ui::TEXT, ui::PANEL);
        char dist[24];
        phone::formatDistance(_nav.distM, dist, sizeof(dist), true);
        c.setFont(gUi.thai26());
        c.setTextDatum(textdatum_t::middle_center);
        c.setTextColor(_nav.distM >= 0 && _nav.distM < 200 ? ui::WARN : ui::TEXT);
        c.drawString(dist, 68, 172);
        const char* what = _nav.text[0] ? _nav.text : phone::TurnAnnouncer::iconTh(_nav.icon);
        c.setFont(gUi.thai26());
        c.setTextDatum(textdatum_t::middle_left);
        c.setTextColor(ui::TEXT);
        drawWrapped(c, what, 142, 116, ui::W - 150, 4, 34);
    }

    // ---- arrival + speed ----
    c.drawFastHLine(0, kBottomY, ui::W, ui::LINE);
    char bottom[96] = "";
    if (nav) {
        size_t n = 0;
        if (_nav.eta[0]) n += snprintf(bottom + n, sizeof(bottom) - n, "ถึง %s", _nav.eta);
        if (_nav.leftKm >= 0) n += snprintf(bottom + n, sizeof(bottom) - n, "%sอีก %.1f กม.", n ? " • " : "", _nav.leftKm);
        if (_nav.leftMin >= 0 && n < sizeof(bottom)) snprintf(bottom + n, sizeof(bottom) - n, "%s%d นาที", n ? " • " : "", _nav.leftMin);
    } else if (_gps.fresh(_now)) {
        snprintf(bottom, sizeof(bottom), "GPS %s (%.0f ม.)", _gps.accuracy <= 15 ? "แม่นยำ" : "สัญญาณอ่อน", _gps.accuracy);
    }
    gUi.drawFitted(c, bottom, 10, (kBottomY + ui::H) / 2, 210, gUi.thai20(), ui::DIM, textdatum_t::middle_left);
    if (_gps.fresh(_now)) {
        char spd[16];
        snprintf(spd, sizeof(spd), "%.0f", _gps.speedKmh);
        c.setFont(&fonts::FreeSansBold18pt7b);
        c.setTextDatum(textdatum_t::middle_right);
        c.setTextColor(ui::TEXT);
        c.drawString(spd, ui::W - 48, (kBottomY + ui::H) / 2);
        c.setFont(&fonts::Font2);
        c.setTextColor(ui::DIM);
        c.drawString("km/h", ui::W - 6, (kBottomY + ui::H) / 2 + 4);
    }
}
