/**
 * @file CardScreen.cpp
 * @brief Info cards: weather / oil / quotes / news (all drawn with primitives, Thai VLW fonts)
 */

#include "CardScreen.h"

#include <math.h>
#include <string.h>

#include <vector>

#include "../util/TextWrap.h"

CardScreen gCard;

namespace {

constexpr uint16_t UP = 0x2FE8;      // green
constexpr uint16_t DOWN = 0xF9E7;    // red
constexpr uint16_t SUN = 0xFE60;     // amber
constexpr uint16_t CLOUD = 0xC618;   // light grey
constexpr uint16_t RAIN = 0x3D7F;    // blue
constexpr uint16_t BAR = 0x2124;     // dark bar background

struct Guard {
    explicit Guard(SemaphoreHandle_t m) : m(m) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m); }
    SemaphoreHandle_t m;
};

uint16_t changeColor(float v) { return v > 0.0001f ? UP : (v < -0.0001f ? DOWN : ui::DIM); }

// small ▲ / ▼ (the Thai fonts have no arrows)
void drawArrow(LGFX_Sprite& c, int x, int cy, float v, uint16_t col) {
    if (v > 0.0001f) c.fillTriangle(x, cy + 4, x + 10, cy + 4, x + 5, cy - 5, col);
    else if (v < -0.0001f) c.fillTriangle(x, cy - 4, x + 10, cy - 4, x + 5, cy + 5, col);
    else c.fillRect(x, cy - 1, 10, 3, col);
}

void fmtPrice(char* out, size_t n, float v) {
    const float a = fabsf(v);
    if (a >= 10000) snprintf(out, n, "%.0f", v);
    else if (a >= 1) snprintf(out, n, "%.2f", v);
    else snprintf(out, n, "%.5f", v);
}

uint16_t aqiColor(int pm25) {
    if (pm25 < 0) return ui::DIM;
    if (pm25 <= 15) return 0x2EDF;   // very good (Thai PCD scale)
    if (pm25 <= 25) return UP;       // good
    if (pm25 <= 37) return 0xFFE0;   // moderate
    if (pm25 <= 75) return 0xFD20;   // starts to affect health
    return DOWN;                     // unhealthy
}

}  // namespace

// ============================================================
// Weather helpers
// ============================================================

namespace wx {

const char* describe(int code) {
    switch (code) {
        case 0: return "ท้องฟ้าแจ่มใส";
        case 1: return "ส่วนใหญ่แจ่มใส";
        case 2: return "มีเมฆบางส่วน";
        case 3: return "เมฆมาก";
        case 45:
        case 48: return "มีหมอก";
        case 51:
        case 53:
        case 55: return "ฝนปรอยๆ";
        case 56:
        case 57: return "ฝนปรอยเยือกแข็ง";
        case 61: return "ฝนตกเล็กน้อย";
        case 63: return "ฝนตกปานกลาง";
        case 65: return "ฝนตกหนัก";
        case 66:
        case 67: return "ฝนเยือกแข็ง";
        case 71:
        case 73:
        case 75:
        case 77: return "หิมะตก";
        case 80: return "ฝนตกเป็นช่วงๆ";
        case 81: return "ฝนตกเป็นช่วงปานกลาง";
        case 82: return "ฝนตกหนักเป็นช่วง";
        case 85:
        case 86: return "หิมะเป็นช่วงๆ";
        case 95: return "พายุฝนฟ้าคะนอง";
        case 96:
        case 99: return "พายุฝนฟ้าคะนอง ลูกเห็บ";
        default: return "ไม่ทราบสภาพอากาศ";
    }
}

static void cloud(LGFX_Sprite& c, int cx, int cy, int s, uint16_t col) {
    c.fillCircle(cx - s / 4, cy, s / 4, col);
    c.fillCircle(cx + s / 8, cy - s / 8, s / 3, col);
    c.fillCircle(cx + s / 3, cy + s / 16, s / 5, col);
    c.fillRoundRect(cx - s / 2, cy, s, s / 4, s / 8, col);
}

static void sun(LGFX_Sprite& c, int cx, int cy, int r) {
    for (int i = 0; i < 8; i++) {
        const float a = i * 0.7854f;
        c.drawWideLine(cx + cosf(a) * (r + 3), cy + sinf(a) * (r + 3), cx + cosf(a) * (r + r / 2 + 3),
                       cy + sinf(a) * (r + r / 2 + 3), 1.6f, SUN);
    }
    c.fillCircle(cx, cy, r, SUN);
}

static void moon(LGFX_Sprite& c, int cx, int cy, int r) {
    c.fillCircle(cx, cy, r, 0xEF5D);
    c.fillCircle(cx + r / 2, cy - r / 3, r, ui::BG);
}

void drawIcon(LGFX_Sprite& c, int cx, int cy, int s, int code, bool isDay) {
    const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    const bool storm = code >= 95;
    if (code <= 1) {  // clear
        if (isDay) sun(c, cx, cy, s / 4);
        else moon(c, cx, cy, s / 4);
        return;
    }
    if (code == 2) {  // partly cloudy
        if (isDay) sun(c, cx - s / 6, cy - s / 6, s / 5);
        else moon(c, cx - s / 6, cy - s / 6, s / 5);
        cloud(c, cx + s / 10, cy + s / 10, s * 3 / 4, CLOUD);
        return;
    }
    if (code == 45 || code == 48) {  // fog
        for (int i = 0; i < 4; i++) c.fillRoundRect(cx - s / 2 + (i & 1) * 6, cy - s / 4 + i * s / 7, s - 6, 4, 2, CLOUD);
        return;
    }
    const uint16_t cc = (rain || storm) ? 0x8C71 : CLOUD;
    cloud(c, cx, cy - s / 8, s, cc);
    if (rain) {
        for (int i = -1; i <= 1; i++) c.drawWideLine(cx + i * s / 4, cy + s / 5, cx + i * s / 4 - 5, cy + s / 5 + s / 5, 1.5f, RAIN);
    } else if (snow) {
        for (int i = -1; i <= 1; i++) c.fillCircle(cx + i * s / 4, cy + s / 4, 3, ui::TEXT);
    }
    if (storm) {
        const int x = cx, y = cy + s / 6;
        c.fillTriangle(x, y, x + 10, y, x - 2, y + s / 4, SUN);
        c.fillTriangle(x - 2, y + s / 6, x + 8, y + s / 6, x - 6, y + s / 2 - 4, SUN);
    }
}

}  // namespace wx

// ============================================================
// Screen
// ============================================================

void CardScreen::begin() { _lock = xSemaphoreCreateMutex(); }

void CardScreen::show(const CardData& d, uint32_t holdMs) {
    if (!_lock) return;
    Guard g(_lock);
    memcpy(&_next, &d, sizeof(CardData));
    _hold = holdMs;
    _pending = true;
}

void CardScreen::activate() {
    Guard g(_lock);
    memcpy(&_cur, &_next, sizeof(CardData));
    _pending = false;
    _closeAt = millis() + _hold;
}

void CardScreen::onTouch(const TouchEvent& e) {
    if (e.type == TouchEvent::Tap) gUi.pop();  // any tap closes the card
}

void CardScreen::drawHeader(LGFX_Sprite& c) {
    gUi.drawTitle(c, _cur.title, true);
    if (_cur.subtitle[0]) gUi.drawFitted(c, _cur.subtitle, 8, ui::kTitleH + 11, ui::W - 16, gUi.thai20(), ui::DIM);
}

void CardScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    switch (_cur.kind) {
        case CardData::Weather: drawWeather(c); break;
        case CardData::Oil: drawOil(c); break;
        case CardData::Quotes: _cur.rows == 1 ? drawQuote(c) : drawQuotes(c); break;
        case CardData::News: drawNews(c); break;
        case CardData::Text: drawText(c); break;
        case CardData::Table: drawTable(c); break;
        case CardData::Analysis: drawAnalysis(c); break;
        default: drawHeader(c); break;
    }
    // remaining time: thin bar at the very bottom
    const int32_t left = (int32_t)(_closeAt - millis());
    if (left > 0 && _hold) c.fillRect(0, ui::H - 2, ui::W * left / (int32_t)_hold, 2, ui::LINE);
}

// ---- weather: icon + big temperature + details + 5-day strip ----
void CardScreen::drawWeather(LGFX_Sprite& c) {
    drawHeader(c);
    const CardData::WeatherInfo& w = _cur.w;
    wx::drawIcon(c, 52, 98, 70, w.code, w.isDay);

    char buf[48];
    c.setFont(&fonts::FreeSansBold24pt7b);
    c.setTextDatum(textdatum_t::top_left);
    c.setTextColor(ui::TEXT);
    snprintf(buf, sizeof(buf), "%.0f", w.temp);
    c.drawString(buf, 98, 62);
    const int tw = c.textWidth(buf);
    c.drawCircle(98 + tw + 6, 68, 4, ui::TEXT);
    c.drawCircle(98 + tw + 6, 68, 3, ui::TEXT);
    gUi.drawFitted(c, "C", 98 + tw + 12, 74, 20, gUi.thai20(), ui::TEXT);
    gUi.drawFitted(c, wx::describe(w.code), 98, 112, 120, gUi.thai20(), ui::TEXT);

    const int x = 214;
    int y = 60;
    snprintf(buf, sizeof(buf), "รู้สึก %.0f°", w.feels);
    gUi.drawFitted(c, buf, x, y, 104, gUi.thai20(), ui::DIM);
    snprintf(buf, sizeof(buf), "ความชื้น %d%%", w.humidity);
    gUi.drawFitted(c, buf, x, y += 20, 104, gUi.thai20(), ui::DIM);
    snprintf(buf, sizeof(buf), "ลม %.0f กม./ชม.", w.wind);
    gUi.drawFitted(c, buf, x, y += 20, 104, gUi.thai20(), ui::DIM);
    if (w.pm25 >= 0) {
        snprintf(buf, sizeof(buf), "PM2.5 %d", w.pm25);
        c.fillCircle(x + 4, y + 20, 4, aqiColor(w.pm25));
        gUi.drawFitted(c, buf, x + 12, y += 20, 92, gUi.thai20(), aqiColor(w.pm25));
    }

    // 5-day forecast
    const int top = 138, colW = w.days ? ui::W / w.days : ui::W;
    c.drawFastHLine(0, top - 4, ui::W, ui::LINE);
    for (int i = 0; i < w.days; i++) {
        const CardData::Day& d = w.day[i];
        const int cx = i * colW + colW / 2;
        gUi.drawFitted(c, d.label, cx, top + 8, colW - 4, gUi.thai20(), i == 0 ? ui::ACCENT : ui::DIM,
                       textdatum_t::middle_center);
        wx::drawIcon(c, cx, top + 34, 30, d.code, true);
        snprintf(buf, sizeof(buf), "%.0f°/%.0f°", d.tmax, d.tmin);
        gUi.drawFitted(c, buf, cx, top + 58, colW - 2, gUi.thai20(), ui::TEXT, textdatum_t::middle_center);
        // rain probability bar
        const int bw = colW - 18, bx = cx - bw / 2, by = top + 72;
        c.fillRoundRect(bx, by, bw, 5, 2, BAR);
        c.fillRoundRect(bx, by, bw * (d.pop < 0 ? 0 : d.pop) / 100, 5, 2, RAIN);
        snprintf(buf, sizeof(buf), "ฝน %d%%", d.pop < 0 ? 0 : d.pop);
        gUi.drawFitted(c, buf, cx, top + 87, colW - 2, gUi.thai20(), RAIN, textdatum_t::middle_center);
    }
}

// ---- oil: one row per product, bar = price relative to the most expensive ----
void CardScreen::drawOil(LGFX_Sprite& c) {
    drawHeader(c);
    float maxV = 1;
    for (int i = 0; i < _cur.rows; i++) maxV = _cur.row[i].value > maxV ? _cur.row[i].value : maxV;
    const int top = 60, rowH = _cur.rows > 7 ? 22 : 24;
    char buf[32];
    for (int i = 0; i < _cur.rows; i++) {
        const CardData::Row& r = _cur.row[i];
        const int y = top + i * rowH;
        c.fillRoundRect(0, y + 1, (int)(ui::W * 0.62f * r.value / maxV), rowH - 3, 3, BAR);
        gUi.drawFitted(c, r.name, 6, y + rowH / 2, 168, gUi.thai20(), ui::TEXT);
        fmtPrice(buf, sizeof(buf), r.value);
        c.setFont(&fonts::FreeSansBold9pt7b);
        c.setTextDatum(textdatum_t::middle_right);
        c.setTextColor(ui::TEXT);
        c.drawString(buf, 236, y + rowH / 2);
        // tomorrow's change if announced, otherwise today's vs yesterday
        const bool tomorrow = r.next > 0 && fabsf(r.next - r.value) > 0.001f;
        const float d = tomorrow ? r.next - r.value : r.change;
        const uint16_t col = tomorrow ? ui::WARN : changeColor(d);
        drawArrow(c, 244, y + rowH / 2, d, col);
        snprintf(buf, sizeof(buf), tomorrow ? "%+.2f*" : "%+.2f", d);
        if (fabsf(d) < 0.001f) snprintf(buf, sizeof(buf), "0.00");
        c.setFont(&fonts::Font2);
        c.setTextDatum(textdatum_t::middle_left);
        c.setTextColor(col);
        c.drawString(buf, 258, y + rowH / 2);
    }
}

// ---- one instrument: price, change, day range, performance bars, RSI, rating ----
void CardScreen::drawQuote(LGFX_Sprite& c) {
    drawHeader(c);
    const CardData::Row& r = _cur.row[0];
    char buf[48];
    fmtPrice(buf, sizeof(buf), r.value);
    c.setFont(&fonts::FreeSansBold24pt7b);
    c.setTextDatum(textdatum_t::top_left);
    c.setTextColor(ui::TEXT);
    c.drawString(buf, 8, 60);
    const uint16_t col = changeColor(r.change);
    drawArrow(c, 10, 116, r.change, col);
    snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)", r.change, r.changePct);
    c.setFont(&fonts::FreeSansBold9pt7b);
    c.setTextDatum(textdatum_t::middle_left);
    c.setTextColor(col);
    c.drawString(buf, 26, 116);
    if (r.extra[0]) gUi.drawFitted(c, r.extra, 312, 116, 150, gUi.thai20(), ui::DIM, textdatum_t::middle_right);

    // day range: low |----o----| high, tick = open
    if (r.high > r.low) {
        const int x0 = 70, x1 = 250, y = 140;
        c.fillRoundRect(x0, y - 2, x1 - x0, 5, 2, BAR);
        auto px = [&](float v) { return x0 + (int)((v - r.low) / (r.high - r.low) * (x1 - x0)); };
        c.fillRect(px(r.open) - 1, y - 6, 2, 13, ui::DIM);
        c.fillCircle(px(r.value), y, 5, col);
        fmtPrice(buf, sizeof(buf), r.low);
        gUi.drawFitted(c, buf, x0 - 4, y, 62, gUi.thai20(), ui::DIM, textdatum_t::middle_right);
        fmtPrice(buf, sizeof(buf), r.high);
        gUi.drawFitted(c, buf, x1 + 4, y, 64, gUi.thai20(), ui::DIM);
    }

    // performance bars, centred at 0
    if (r.hasPerf) {
        static const char* const kLabel[4] = {"1 สัปดาห์", "1 เดือน", "3 เดือน", "1 ปี"};
        float m = 1;
        for (float p : r.perf) m = fabsf(p) > m ? fabsf(p) : m;
        const int cx = 170, half = 70;
        for (int i = 0; i < 4; i++) {
            const int y = 160 + i * 19;
            gUi.drawFitted(c, kLabel[i], 8, y + 8, 80, gUi.thai20(), ui::DIM);
            const int len = (int)(fabsf(r.perf[i]) / m * half);
            const uint16_t pc = changeColor(r.perf[i]);
            if (r.perf[i] >= 0) c.fillRect(cx, y + 3, len, 11, pc);
            else c.fillRect(cx - len, y + 3, len, 11, pc);
            snprintf(buf, sizeof(buf), "%+.1f%%", r.perf[i]);
            c.setFont(&fonts::Font2);
            c.setTextColor(pc);
            c.setTextDatum(r.perf[i] >= 0 ? textdatum_t::middle_right : textdatum_t::middle_left);
            c.drawString(buf, r.perf[i] >= 0 ? cx - 4 : cx + 4, y + 8);
        }
        c.drawFastVLine(cx, 160, 76, ui::LINE);
    }

    // RSI + TradingView rating, right column of the bottom area
    int y = 168;
    if (r.rsi >= 0) {
        snprintf(buf, sizeof(buf), "RSI %.0f", r.rsi);
        const uint16_t rc = r.rsi >= 70 ? DOWN : (r.rsi <= 30 ? UP : ui::TEXT);
        gUi.drawFitted(c, buf, 250, y, 68, gUi.thai20(), rc);
        y += 24;
    }
    if (r.rating > -2) {
        const char* s = r.rating >= 0.5f ? "ซื้อแรง" : r.rating >= 0.1f ? "ซื้อ" : r.rating > -0.1f ? "กลางๆ"
                        : r.rating > -0.5f ? "ขาย" : "ขายแรง";
        gUi.drawFitted(c, "สัญญาณ", 250, y, 68, gUi.thai20(), ui::DIM);
        gUi.drawFitted(c, s, 250, y + 22, 68, gUi.thai20(), changeColor(r.rating));
    }
}

// ---- several instruments: one row each ----
void CardScreen::drawQuotes(LGFX_Sprite& c) {
    drawHeader(c);
    const int top = 60, rowH = (ui::H - top - 4) / (_cur.rows > 0 ? _cur.rows : 1);
    char buf[40];
    for (int i = 0; i < _cur.rows; i++) {
        const CardData::Row& r = _cur.row[i];
        const int y = top + i * rowH, cy = y + rowH / 2;
        if (i) c.drawFastHLine(6, y, ui::W - 12, ui::LINE);
        gUi.drawFitted(c, r.name, 6, cy, 130, gUi.thai20(), ui::TEXT);
        fmtPrice(buf, sizeof(buf), r.value);
        c.setFont(&fonts::FreeSansBold9pt7b);
        c.setTextDatum(textdatum_t::middle_right);
        c.setTextColor(ui::TEXT);
        c.drawString(buf, 236, cy);
        const uint16_t col = changeColor(r.changePct);
        drawArrow(c, 244, cy, r.changePct, col);
        snprintf(buf, sizeof(buf), "%+.2f%%", r.changePct);
        c.setFont(&fonts::Font2);
        c.setTextDatum(textdatum_t::middle_left);
        c.setTextColor(col);
        c.drawString(buf, 258, cy);
    }
}

// ---- news headlines: up to 2 Thai-wrapped lines each (syllable-aware, like the subtitles) ----
void CardScreen::drawNews(LGFX_Sprite& c) {
    drawHeader(c);
    c.setFont(gUi.thai20());
    c.setTextDatum(textdatum_t::middle_left);
    auto widthOf = [&](uint32_t cp) -> int {  // measured with the sprite itself: exact for this font
        char b[5] = {0};
        if (cp < 0x80) b[0] = (char)cp;
        else if (cp < 0x800) b[0] = (char)(0xC0 | (cp >> 6)), b[1] = (char)(0x80 | (cp & 0x3F));
        else b[0] = (char)(0xE0 | (cp >> 12)), b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F));
        return c.textWidth(b);
    };
    const int lineH = 21, maxW = ui::W - 22;
    int y = 58;
    char line[300];
    for (int i = 0; i < _cur.rows; i++) {
        const char* t = _cur.row[i].name;
        const std::vector<textwrap::Line> lines = textwrap::wrap(t, strlen(t), maxW, widthOf);
        const int n = lines.size() > 2 ? 2 : (int)lines.size();
        if (y + n * lineH > ui::H - 2) break;
        c.fillCircle(8, y + lineH / 2, 2, ui::ACCENT);
        for (int k = 0; k < n; k++) {
            size_t len = lines[k].len < sizeof(line) - 4 ? lines[k].len : sizeof(line) - 4;
            memcpy(line, t + lines[k].start, len);
            line[len] = '\0';
            if (k == 1 && lines.size() > 2) strcat(line, "...");
            c.setTextColor(k == 0 ? ui::TEXT : ui::DIM);
            c.drawString(line, 16, y + lineH / 2);
            y += lineH;
        }
        y += 4;
    }
}

// ============================================================
// Generic cards: text / table / analysis
// ============================================================

int CardScreen::drawWrapped(LGFX_Sprite& c, const char* text, int x, int y, int w, int maxLines, int lineH,
                            uint16_t color) {
    c.setFont(gUi.thai20());
    c.setTextDatum(textdatum_t::middle_left);
    c.setTextColor(color);
    auto widthOf = [&](uint32_t cp) -> int {
        char b[5] = {0};
        if (cp < 0x80) b[0] = (char)cp;
        else if (cp < 0x800) b[0] = (char)(0xC0 | (cp >> 6)), b[1] = (char)(0x80 | (cp & 0x3F));
        else b[0] = (char)(0xE0 | (cp >> 12)), b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), b[2] = (char)(0x80 | (cp & 0x3F));
        return c.textWidth(b);
    };
    const std::vector<textwrap::Line> lines = textwrap::wrap(text, strlen(text), w, widthOf);
    char line[300];
    for (size_t k = 0; k < lines.size() && (int)k < maxLines; k++) {
        const size_t len = lines[k].len < sizeof(line) - 4 ? lines[k].len : sizeof(line) - 4;
        memcpy(line, text + lines[k].start, len);
        line[len] = '\0';
        if ((int)k == maxLines - 1 && lines.size() > k + 1) strcat(line, "...");
        c.drawString(line, x, y + lineH / 2);
        y += lineH;
    }
    return y;
}

// big result (ASCII -> big GFX font, Thai -> 26 px) + wrapped paragraph
void CardScreen::drawText(LGFX_Sprite& c) {
    drawHeader(c);
    int y = 58;
    if (_cur.big[0]) {
        bool ascii = true;
        for (const char* p = _cur.big; *p; p++) ascii &= (unsigned char)*p < 0x80;
        c.setFont(&fonts::FreeSansBold18pt7b);
        if (ascii && c.textWidth(_cur.big) <= ui::W - 16) {
            c.setTextDatum(textdatum_t::top_left);
            c.setTextColor(ui::TEXT);
            c.drawString(_cur.big, 8, y + 4);
            y += 50;
        } else {  // Thai, or too long for the big font (station names): smaller, with "..."
            gUi.drawFitted(c, _cur.big, 8, y + 16, ui::W - 16, gUi.thai26(), ui::TEXT);
            y += 36;
        }
    }
    drawWrapped(c, _cur.body, 8, y, ui::W - 16, (ui::H - y - 4) / 21, 21, _cur.big[0] ? ui::DIM : ui::TEXT);
}

// name on the left, value (extra) on the right in the row colour
void CardScreen::drawTable(LGFX_Sprite& c) {
    drawHeader(c);
    const int top = 58;
    const int rowH = _cur.rows ? ((ui::H - top - 2) / _cur.rows < 26 ? (ui::H - top - 2) / _cur.rows : 26) : 26;
    for (int i = 0; i < _cur.rows; i++) {
        const CardData::Row& r = _cur.row[i];
        const int cy = top + i * rowH + rowH / 2;
        if (i) c.drawFastHLine(6, top + i * rowH, ui::W - 12, ui::LINE);
        c.setFont(gUi.thai20());
        const int ew = r.extra[0] ? c.textWidth(r.extra) : 0;
        const int extraW = ew > 150 ? 150 : ew;
        gUi.drawFitted(c, r.name, 6, cy, ui::W - 18 - extraW, gUi.thai20(), ui::TEXT);
        if (r.extra[0]) gUi.drawFitted(c, r.extra, ui::W - 6, cy, 150, gUi.thai20(), r.color ? r.color : ui::ACCENT,
                                       textdatum_t::middle_right);
    }
}

// rating gauge (strong sell .. strong buy) + indicator rows
void CardScreen::drawAnalysis(LGFX_Sprite& c) {
    drawHeader(c);
    const int cx = 78, cy = 150, r = 62;
    static const uint16_t kSeg[5] = {DOWN, 0xFC60, 0x8410, 0x8FE8, UP};
    for (int s = 0; s < 5; s++) {  // 180° split into 5 zones
        const float a0 = 180 + s * 36, a1 = a0 + 34;
        c.fillArc(cx, cy, r - 12, r, a0, a1, kSeg[s]);
    }
    const float g = _cur.gauge < -1.5f ? 0 : (_cur.gauge > 1 ? 1 : (_cur.gauge < -1 ? -1 : _cur.gauge));
    const float ang = (180.0f + (g + 1.0f) * 90.0f) * 0.0174533f;
    c.drawWideLine(cx, cy, cx + cosf(ang) * (r - 16), cy + sinf(ang) * (r - 16), 2.5f, ui::TEXT);
    c.fillCircle(cx, cy, 5, ui::TEXT);
    const char* label = _cur.gauge < -1.5f ? "ไม่มีข้อมูล" : g >= 0.5f ? "ซื้อแรง" : g >= 0.1f ? "ซื้อ"
                        : g > -0.1f ? "กลางๆ" : g > -0.5f ? "ขาย" : "ขายแรง";
    gUi.drawFitted(c, label, cx, cy + 22, 140, gUi.thai26(), (g > -0.1f && g < 0.1f) ? (uint16_t)0x9CD3 : changeColor(g),
                   textdatum_t::middle_center);
    if (_cur.big[0]) gUi.drawFitted(c, _cur.big, cx, 74, 150, gUi.thai20(), ui::TEXT, textdatum_t::middle_center);

    const int x = 160, top = 60, rowH = 22;
    for (int i = 0; i < _cur.rows && top + (i + 1) * rowH <= ui::H; i++) {
        const CardData::Row& row = _cur.row[i];
        const int y = top + i * rowH + rowH / 2;
        gUi.drawFitted(c, row.name, x, y, 80, gUi.thai20(), ui::DIM);
        gUi.drawFitted(c, row.extra, ui::W - 6, y, 86, gUi.thai20(), row.color ? row.color : ui::TEXT,
                       textdatum_t::middle_right);
    }
}
