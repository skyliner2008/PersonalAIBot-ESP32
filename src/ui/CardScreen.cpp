/**
 * @file CardScreen.cpp
 * @brief Info cards: weather / oil / quotes / news (all drawn with primitives, Thai VLW fonts)
 */

#include "CardScreen.h"

#include <math.h>
#include <string.h>

#include <new>
#include <vector>

#include "../app/AppConfig.h"
#include "../util/PsramAlloc.h"
#include "../util/TextWrap.h"
#include "../util/TimeFmt.h"

// two CardData (shown + next) = 7 KB: in PSRAM, not in the internal RAM the TLS / Bluetooth need (T113)
CardScreen& gCard = *new (psramMalloc(sizeof(CardScreen))) CardScreen();

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
        case CardData::Graph: drawGraph(c); break;
        case CardData::Bars: drawBars(c); break;
        case CardData::Candles: drawCandles(c); break;
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

namespace {
void dashedHLine(LGFX_Sprite& c, int x0, int x1, int y, uint16_t col) {
    for (int x = x0; x < x1; x += 8) c.drawFastHLine(x, y, x + 4 < x1 ? 4 : x1 - x, col);
}
}  // namespace

// ---- one series over time (water level, T100): line + dashed bank / warning / critical levels ----
void CardScreen::drawGraph(LGFX_Sprite& c) {
    drawHeader(c);
    const CardData::Series& g = _cur.g;
    const uint16_t col = g.color ? g.color : ui::ACCENT;
    if (_cur.big[0]) gUi.drawFitted(c, _cur.big, 8, 68, 200, gUi.thai26(), col);
    char buf[24];
    if (g.n) {
        snprintf(buf, sizeof(buf), "%.2f %s", g.v[g.n - 1], g.unit);
        gUi.drawFitted(c, buf, ui::W - 8, 68, 110, gUi.thai20(), ui::TEXT, textdatum_t::middle_right);
    }
    // a note under the big line ("ต่ำกว่าตลิ่ง 2.41 ม."): above the bank = red
    if (_cur.body[0]) {
        gUi.drawFitted(c, _cur.body, 8, 90, ui::W - 16, gUi.thai20(),
                       strncmp(_cur.body, "สูง", strlen("สูง")) ? ui::DIM : 0xF800);
    }
    if (g.n < 2) return;

    // y range: the data, plus a reference line when it is near enough to say something (bank first)
    float lo = g.v[0], hi = g.v[0];
    for (int i = 1; i < g.n; i++) {
        lo = g.v[i] < lo ? g.v[i] : lo;
        hi = g.v[i] > hi ? g.v[i] : hi;
    }
    const float span0 = hi - lo < 0.2f ? 0.2f : hi - lo;
    for (float r : g.ref) {
        if (isnan(r) || r < lo - span0 * 2 || r > hi + span0 * 2) continue;
        lo = r < lo ? r : lo;
        hi = r > hi ? r : hi;
    }
    const float pad = (hi - lo < 0.2f ? 0.2f : hi - lo) * 0.12f;
    lo -= pad;
    hi += pad;
    const int x0 = 46, x1 = ui::W - 8, y0 = 104, y1 = 212;
    auto py = [&](float v) { return y1 - (int)((v - lo) / (hi - lo) * (y1 - y0)); };
    auto px = [&](int i) { return x0 + (x1 - x0) * i / (g.n - 1); };

    c.drawRect(x0, y0, x1 - x0, y1 - y0 + 1, ui::LINE);
    c.setFont(&fonts::Font0);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::middle_right);
    for (int k = 0; k <= 2; k++) {  // three level labels on the left
        const float v = lo + (hi - lo) * k / 2;
        snprintf(buf, sizeof(buf), "%.2f", v);
        c.drawString(buf, x0 - 3, py(v));
    }
    static const char* const kRefName[3] = {"ตลิ่ง", "เตือนภัย", "วิกฤต"};
    static const uint16_t kRefCol[3] = {0xF9E7, 0xFE60, 0xF800};
    for (int k = 0; k < 3; k++) {
        const float r = g.ref[k];
        if (isnan(r) || r < lo || r > hi) continue;
        dashedHLine(c, x0 + 1, x1, py(r), kRefCol[k]);
        gUi.drawFitted(c, kRefName[k], x0 + 4, py(r) - 9, 70, gUi.thai20(), kRefCol[k]);
    }
    for (int i = 1; i < g.n; i++) c.drawWideLine(px(i - 1), py(g.v[i - 1]), px(i), py(g.v[i]), 1.5f, col);
    c.fillCircle(px(g.n - 1), py(g.v[g.n - 1]), 3, col);

    timefmt::shortStamp(g.from, buf, sizeof(buf));
    c.setFont(&fonts::Font0);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::top_left);
    c.drawString(buf, x0, y1 + 6);
    timefmt::shortStamp(g.to, buf, sizeof(buf));
    c.setTextDatum(textdatum_t::top_right);
    c.drawString(buf, x1, y1 + 6);
}

// ---- candlestick chart (market_chart, T142): candles + EMA20 / EMA50 + dashed support / resistance ----
namespace {
void axisPrice(char* buf, size_t n, float v, float span) {
    if (fabsf(v) >= 1000 || span >= 50) snprintf(buf, n, "%.0f", v);
    else if (span >= 0.5f) snprintf(buf, n, "%.2f", v);
    else snprintf(buf, n, "%.4f", v);
}

void barStamp(char* buf, size_t n, uint32_t t, bool intraday) {
    const time_t lt = (time_t)t + TIME_UTC_OFFSET_SEC;
    struct tm tm;
    gmtime_r(&lt, &tm);
    if (intraday) snprintf(buf, n, "%d/%d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    else snprintf(buf, n, "%d/%d/%02d", tm.tm_mday, tm.tm_mon + 1, (tm.tm_year + 1900) % 100);
}
}  // namespace

void CardScreen::drawCandles(LGFX_Sprite& c) {
    drawHeader(c);
    const CardData::Chart& k = _cur.k;
    if (_cur.big[0]) {
        const float chg = k.n >= 2 ? k.c[k.n - 1] - k.c[k.n - 2] : 0;
        gUi.drawFitted(c, _cur.big, 8, 68, ui::W - 16, gUi.thai26(), changeColor(chg));
    }
    if (k.n < 2) return;
    constexpr uint16_t kEma20 = 0x2EDF, kEma50 = 0xFD20;
    const int x0 = 4, x1 = ui::W - 46, y0 = 86, y1 = 222;

    // y range: the candles, plus a level when it is near enough to matter
    float lo = k.l[0], hi = k.h[0];
    for (int i = 1; i < k.n; i++) {
        lo = k.l[i] < lo ? k.l[i] : lo;
        hi = k.h[i] > hi ? k.h[i] : hi;
    }
    const float span0 = hi - lo > 0 ? hi - lo : fabsf(hi) * 0.01f + 1e-6f;
    for (float v : {k.support[0], k.support[1], k.resistance[0], k.resistance[1], k.zone[0].top, k.zone[0].bottom,
                    k.zone[1].top, k.zone[1].bottom, k.zone[2].top, k.zone[2].bottom, k.zone[3].top, k.zone[3].bottom}) {
        if (isnan(v) || v < lo - span0 * 0.35f || v > hi + span0 * 0.35f) continue;
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    const float pad = (hi - lo > 0 ? hi - lo : span0) * 0.05f;
    lo -= pad;
    hi += pad;
    auto py = [&](float v) { return y1 - (int)lroundf((v - lo) / (hi - lo) * (y1 - y0)); };
    const float slot = (float)(x1 - x0) / k.n;
    auto px = [&](int i) { return x0 + (int)(slot * i + slot / 2); };

    // SMC boxes first, so the candles draw over them: a dark fill + a 1 px edge (order block solid, FVG dotted)
    constexpr uint16_t kObBull = 0x0200, kObBear = 0x4000, kFvgBull = 0x0129, kFvgBear = 0x2900;
    for (const CardData::Chart::Zone& z : k.zone) {
        if (isnan(z.top) || isnan(z.bottom)) continue;
        const float zt = z.top < hi ? z.top : hi, zb = z.bottom > lo ? z.bottom : lo;
        if (zt <= zb) continue;
        const int xa = px(z.from < k.n ? z.from : k.n - 1) - (int)(slot / 2), yt = py(zt), yb = py(zb);
        const int h = yb - yt + 1 > 2 ? yb - yt + 1 : 2;
        c.fillRect(xa, yt, x1 - xa, h, z.ob ? (z.bull ? kObBull : kObBear) : (z.bull ? kFvgBull : kFvgBear));
        const uint16_t edge = z.bull ? UP : DOWN;
        if (z.ob) {
            c.drawRect(xa, yt, x1 - xa, h, edge);
        } else {
            for (int x = xa; x < x1; x += 3) c.drawPixel(x, yt, edge), c.drawPixel(x, yt + h - 1, edge);
        }
    }

    // support (green) / resistance (red): dashed lines; their prices on the right axis, moved apart so two
    // close levels stay readable, and the grid prices only where no level label is
    char buf[24];
    struct Label {
        int y;
        float v;
        uint16_t col;
    } lab[4];
    int nl = 0;
    for (int s = 0; s < 2; s++) {
        for (int side = 0; side < 2; side++) {
            const float v = side ? k.resistance[s] : k.support[s];
            if (isnan(v) || v < lo || v > hi) continue;
            dashedHLine(c, x0, x1, py(v), side ? DOWN : UP);
            lab[nl++] = {py(v), v, side ? DOWN : UP};
        }
    }
    for (int i = 1; i < nl; i++) {  // sort by y (at most 4)
        for (int j = i; j > 0 && lab[j].y < lab[j - 1].y; j--) std::swap(lab[j], lab[j - 1]);
    }
    constexpr int kLabelH = 9;
    for (int i = 1; i < nl; i++) {
        if (lab[i].y - lab[i - 1].y < kLabelH) lab[i].y = lab[i - 1].y + kLabelH;
    }
    for (int i = nl - 1; i >= 0; i--) {  // keep them inside the chart
        const int maxY = (i == nl - 1) ? y1 : lab[i + 1].y - kLabelH;
        if (lab[i].y > maxY) lab[i].y = maxY;
    }
    c.setFont(&fonts::Font0);
    c.setTextDatum(textdatum_t::middle_left);
    for (int g = 0; g <= 3; g++) {
        const float v = lo + (hi - lo) * g / 3;
        const int y = py(v);
        for (int x = x0; x < x1; x += 6) c.drawPixel(x, y, ui::LINE);
        bool free = true;
        for (int i = 0; i < nl; i++) free = free && abs(lab[i].y - y) >= kLabelH;
        if (!free) continue;
        axisPrice(buf, sizeof(buf), v, hi - lo);
        c.setTextColor(ui::DIM);
        c.drawString(buf, x1 + 6, y);
    }
    for (int i = 0; i < nl; i++) {
        axisPrice(buf, sizeof(buf), lab[i].v, hi - lo);
        c.setTextColor(lab[i].col);
        c.drawString(buf, x1 + 6, lab[i].y);
    }
    // candles: wick 1 px, body up to 5 px
    const int bodyW = slot >= 6 ? 5 : (slot >= 4 ? 3 : 1);
    for (int i = 0; i < k.n; i++) {
        const bool up = k.c[i] >= k.o[i];
        const uint16_t col = up ? UP : DOWN;
        const int x = px(i);
        c.drawFastVLine(x, py(k.h[i]), py(k.l[i]) - py(k.h[i]) + 1, col);
        const int top = py(up ? k.c[i] : k.o[i]), bot = py(up ? k.o[i] : k.c[i]);
        c.fillRect(x - bodyW / 2, top, bodyW, bot - top + 1 > 1 ? bot - top + 1 : 1, col);
    }
    // EMA lines over the candles
    auto line = [&](const float* e, uint16_t col) {
        for (int i = 1; i < k.n; i++) {
            if (isnan(e[i - 1]) || isnan(e[i])) continue;
            c.drawWideLine(px(i - 1), py(e[i - 1]), px(i), py(e[i]), 1.0f, col);
        }
    };
    line(k.ema50, kEma50);
    line(k.ema20, kEma20);
    // the last close: a marker on the axis
    const float lastC = k.c[k.n - 1];
    c.fillTriangle(x1 + 1, py(lastC), x1 + 4, py(lastC) - 3, x1 + 4, py(lastC) + 3, ui::TEXT);

    c.setFont(&fonts::Font0);
    c.setTextColor(ui::DIM);
    barStamp(buf, sizeof(buf), k.from, k.intraday);
    c.setTextDatum(textdatum_t::top_left);
    c.drawString(buf, x0, y1 + 5);
    barStamp(buf, sizeof(buf), k.to, k.intraday);
    c.setTextDatum(textdatum_t::top_right);
    c.drawString(buf, x1, y1 + 5);
    // legend in the middle of the bottom line
    c.setTextDatum(textdatum_t::top_center);
    if (k.smc) {
        c.setTextColor(UP);
        c.drawString("DEMAND", (x0 + x1) / 2 - 24, y1 + 5);
        c.setTextColor(DOWN);
        c.drawString("SUPPLY", (x0 + x1) / 2 + 24, y1 + 5);
        return;
    }
    c.setTextColor(kEma20);
    c.drawString("EMA20", (x0 + x1) / 2 - 22, y1 + 5);
    c.setTextColor(kEma50);
    c.drawString("EMA50", (x0 + x1) / 2 + 22, y1 + 5);
}

// ---- horizontal bars, one row each (rain per station, % of the bank per station; T100) ----
void CardScreen::drawBars(LGFX_Sprite& c) {
    drawHeader(c);
    if (!_cur.rows) return;
    const float marker = _cur.g.ref[0];  // e.g. 100 % = the bank (NAN = none)
    float maxV = isnan(marker) ? 0 : marker;
    if (!isnan(_cur.g.ref[1])) maxV = _cur.g.ref[1];  // a fixed full scale (fear & greed 0-100, T143)
    for (int i = 0; i < _cur.rows; i++) maxV = _cur.row[i].value > maxV ? _cur.row[i].value : maxV;
    if (maxV <= 0) maxV = 1;
    const int top = 60, rowH = (ui::H - top - 4) / _cur.rows < 22 ? (ui::H - top - 4) / _cur.rows : 22;
    const int bx0 = 132, bx1 = 270;
    for (int i = 0; i < _cur.rows; i++) {
        const CardData::Row& r = _cur.row[i];
        const int y = top + i * rowH, cy = y + rowH / 2;
        gUi.drawFitted(c, r.name, 6, cy, bx0 - 10, gUi.thai20(), ui::TEXT);
        const int len = (int)((bx1 - bx0) * (r.value > 0 ? r.value : 0) / maxV);
        c.fillRoundRect(bx0, y + 4, len > 2 ? len : 2, rowH - 8, 2, r.color ? r.color : ui::ACCENT);
        gUi.drawFitted(c, r.extra, ui::W - 6, cy, ui::W - bx1 - 10, gUi.thai20(), r.color ? r.color : ui::TEXT,
                       textdatum_t::middle_right);
    }
    if (!isnan(marker)) {  // dashed vertical line at the marker (the bank)
        const int x = bx0 + (int)((bx1 - bx0) * marker / maxV);
        for (int y = top; y < top + _cur.rows * rowH; y += 6) c.drawFastVLine(x, y, 3, 0xF9E7);
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
