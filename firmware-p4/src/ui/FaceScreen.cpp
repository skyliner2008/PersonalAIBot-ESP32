/**
 * @file FaceScreen.cpp
 * @brief Eyes + status + subtitles home screen + multi-theme power-save screensaver with weather
 */

#include "FaceScreen.h"

#include <math.h>
#include <string.h>

#include <vector>

#include "../app/Settings.h"
#include "../util/TextWrap.h"
#include "../util/Utf8.h"
#include "CardScreen.h"

FaceScreen gFace;

namespace {

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t mutex) : m(mutex) {
        if (m) xSemaphoreTake(m, portMAX_DELAY);
    }
    ~Guard() {
        if (m) xSemaphoreGive(m);
    }
};

void copyUtf8(char* dst, size_t cap, const char* src) {
    const size_t n = utf8::clampToBoundary(src, strlen(src), cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

constexpr int kSubtitleY = 174;
constexpr int kDashX = 297, kDashY = 216;         // vehicle dash shortcut (bottom right), centre
constexpr int kRadioBtnX = 256, kRadioBtnY = 216; // radio / MP3 player shortcut next to dash button
constexpr float kEyesYIdle = 112.0f;
constexpr float kEyesYSubtitle = 90.0f;

uint16_t pm25BadgeColor(int pm25) {
    if (pm25 < 0) return ui::DIM;
    if (pm25 <= 15) return 0x2EDF;   // very good
    if (pm25 <= 25) return 0x2FE8;   // good
    if (pm25 <= 37) return 0xFFE0;   // moderate
    if (pm25 <= 75) return 0xFD20;   // unhealthy for sensitive
    return 0xF9E7;                   // unhealthy
}

void drawSleepingEyes(LGFX_Sprite& c, int cx, int y, int w, int gap, uint16_t col) {
    c.fillSmoothRoundRect(cx - gap / 2 - w, y, w, 5, 2, col);
    c.fillSmoothRoundRect(cx + gap / 2, y, w, 5, 2, col);
}

void drawWeatherCard(LGFX_Sprite& c, const UiStatus& st, int x, int y, int w, int h, uint16_t borderCol) {
    c.fillSmoothRoundRect(x, y, w, h, 10, 0x1082);
    c.drawRoundRect(x, y, w, h, 10, borderCol);
    if (!st.wxValid) {
        char waitMsg[96];
        snprintf(waitMsg, sizeof(waitMsg), "สภาพอากาศ %s • กำลังซิงค์...", st.wxCity[0] ? st.wxCity : "อัตโนมัติ");
        gUi.drawFitted(c, waitMsg, x + w / 2, y + h / 2, w - 20, gUi.thai20(), ui::DIM, textdatum_t::middle_center);
        return;
    }
    wx::drawIcon(c, x + 26, y + h / 2, 26, st.wxCode, st.wxIsDay);

    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", st.wxTemp);
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextSize(1);
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::middle_left);
    c.drawString(tempBuf, x + 52, y + h / 2 - 9);
    const int tw = c.textWidth(tempBuf);
    c.drawCircle(x + 52 + tw + 4, y + h / 2 - 16, 2, ui::TEXT);
    c.setFont(&fonts::Font2);
    c.drawString("C", x + 52 + tw + 9, y + h / 2 - 9);

    char cityLine[96];
    snprintf(cityLine, sizeof(cityLine), "%s", st.wxCity[0] ? st.wxCity : "ตำแหน่งปัจจุบัน");
    gUi.drawFitted(c, cityLine, x + 52 + tw + 26, y + h / 2 - 10, w - (52 + tw + 34), gUi.thai20(), ui::ACCENT,
                   textdatum_t::middle_left);

    char condLine[120];
    if (st.wxPm25 >= 0) {
        snprintf(condLine, sizeof(condLine), "%s • ชื้น %d%%", wx::describe(st.wxCode), (int)st.wxHumidity);
    } else {
        snprintf(condLine, sizeof(condLine), "%s • ความชื้น %d%%", wx::describe(st.wxCode), (int)st.wxHumidity);
    }
    const int rightReserve = st.wxPm25 >= 0 ? 74 : 8;
    gUi.drawFitted(c, condLine, x + 52, y + h / 2 + 14, w - 52 - rightReserve, gUi.thai20(), 0xCE79,
                   textdatum_t::middle_left);

    if (st.wxPm25 >= 0) {
        const uint16_t pmCol = pm25BadgeColor(st.wxPm25);
        const int bx = x + w - 70, by = y + h / 2 + 3;
        c.fillSmoothRoundRect(bx, by, 62, 20, 6, 0x18E3);
        c.drawRoundRect(bx, by, 62, 20, 6, pmCol);
        char pmBuf[20];
        snprintf(pmBuf, sizeof(pmBuf), "PM %d", (int)st.wxPm25);
        c.setFont(&fonts::Font2);
        c.setTextColor(pmCol);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(pmBuf, bx + 31, by + 10);
    }
}

}  // namespace

void FaceScreen::begin(std::function<void()> openMenu, std::function<void()> openRadio,
                       std::function<void()> openDash) {
    _openMenu = openMenu;
    _openRadio = openRadio;
    _openDash = openDash;
    _lock = xSemaphoreCreateMutex();
    _eyes.begin(ui::W, ui::H);
}

// ============================================================
// Thread-safe setters
// ============================================================

void FaceScreen::setState(AvatarState s) { _state = s; }

void FaceScreen::setStatus(const UiStatus& s) {
    Guard g(_lock);
    _status = s;
}

void FaceScreen::pushTranscript(char role, const char* fragment) {
    if (!fragment || !*fragment) return;
    Guard g(_lock);
    if (role != _role) {
        _text[0] = '\0';
        _role = role;
    }
    size_t cur = strlen(_text), add = strlen(fragment);
    if (cur + add + 1 > kTextCap) {  // keep the newest text
        const size_t overflow = cur + add + 1 - kTextCap;
        if (overflow >= cur) {
            cur = 0;
            const size_t skip = utf8::alignForward(fragment, add, add - (kTextCap - 1));
            fragment += skip;
            add -= skip;
        } else {
            const size_t drop = utf8::alignForward(_text, cur, overflow);
            memmove(_text, _text + drop, cur - drop + 1);
            cur -= drop;
        }
    }
    memcpy(_text + cur, fragment, add);
    _text[cur + add] = '\0';
    _textTimer.arm(millis(), 7000);
    _textForever = false;
    _textDirty = true;
}

void FaceScreen::setSystemText(const char* text, uint32_t holdMs) {
    Guard g(_lock);
    _role = 's';
    copyUtf8(_text, kTextCap, text ? text : "");
    // holdMs == 0: keep it until replaced (was a deadline 24.8 days ahead: such errors vanished then)
    _textForever = holdMs == 0;
    _textTimer.arm(millis(), holdMs);
    _textDirty = true;
}

void FaceScreen::setHint(const char* text, uint32_t durationMs) {
    Guard g(_lock);
    copyUtf8(_hint, sizeof(_hint), text ? text : "");
    _hintTimer.arm(millis(), durationMs);
}

void FaceScreen::setExpression(Emotion e, uint32_t holdMs) {
    Guard g(_lock);
    _expr = e;
    _exprTimer.arm(millis(), holdMs);
}

// ============================================================
// Touch
// ============================================================

void FaceScreen::onTouch(const TouchEvent& e) {
    const bool gear = e.startX >= 266 && e.startY <= 56;
    const bool mic = e.startX <= 64 && e.startY <= 60;
    const bool dash = e.startX >= kDashX - 20 && e.startY >= kDashY - 26 && _dashShown;
    const bool radioBtn = e.startX >= kRadioBtnX - 22 && e.startX < kDashX - 20 && e.startY >= kRadioBtnY - 26 && _radioBtnShown;
    const bool radioBar = e.startY >= 204 && e.startX < kRadioBtnX - 22 && _radioShown;
    if (e.type == TouchEvent::Tap) {
        if (gear) {
            if (_openMenu) _openMenu();
        } else if (dash) {
            _openDash();
        } else if (radioBtn || radioBar) {
            if (_openRadio) _openRadio();
        } else if (mic) {
            if (gUi.host()) gUi.host()->uiToggleMic();
        } else if (gUi.host()) {
            gUi.host()->uiTap();
        }
    } else if (e.type == TouchEvent::LongPress && !gear && !mic && !dash && !radioBtn && gUi.host()) {
        gUi.host()->uiLongPress();
    }
}

// ============================================================
// Animation state
// ============================================================

void FaceScreen::tick(uint32_t now, float dt) {
    _now = now;
    Emotion expr;
    bool exprActive, relayout = false;
    {
        Guard g(_lock);
        expr = _expr;
        exprActive = _exprTimer.active(now);
        if (_textDirty) {
            memcpy(_render, _text, kTextCap);
            _renderRole = _role;
            _textDirty = false;
            relayout = true;
        }
        _subtitleVisible = _render[0] && (_textForever || _textTimer.active(now));
    }
    if (relayout) layout();

    Eyes::Mode mode = Eyes::Mode::Awake;
    Emotion base = Emotion::Neutral;
    switch (_state) {
        case AVATAR_CONNECTING: mode = Eyes::Mode::Searching; break;
        case AVATAR_LISTENING: mode = Eyes::Mode::Listening; break;
        case AVATAR_THINKING: mode = Eyes::Mode::Thinking; base = Emotion::Thinking; break;
        case AVATAR_SPEAKING: mode = Eyes::Mode::Speaking; break;
        case AVATAR_ERROR: base = Emotion::Worried; break;
        case AVATAR_SLEEP: mode = Eyes::Mode::Sleeping; base = Emotion::Sleepy; break;
        case AVATAR_SETUP: base = Emotion::Curious; break;
        default: break;
    }
    if (exprActive && _state != AVATAR_SLEEP) base = expr;

    _eyes.setMode(mode);
    _eyes.setEmotion(base);
    float level = _state == AVATAR_SPEAKING ? _speechLevel * 1.8f : 0.0f;
    _eyes.setSpeechLevel(level > 1.0f ? 1.0f : level);
    _eyes.setCenterY(_subtitleVisible ? kEyesYSubtitle : kEyesYIdle);
    if (_pokePending) {
        _pokePending = false;
        _eyes.kick(1.0f);
    }
    _eyes.update(now, dt);
}

void FaceScreen::layout() {
    const lgfx::VLWfont* font = static_cast<const lgfx::VLWfont*>(gUi.thai26());
    const bool vlw = font->getType() == lgfx::IFont::ft_vlw;
    const size_t len = strlen(_render);
    std::vector<textwrap::Line> lines = textwrap::wrap(_render, len, ui::W - 20, [&](uint32_t cp) -> int {
        if (!vlw || cp > 0xFFFF) return 10;
        lgfx::FontMetrics m;
        font->updateFontMetric(&m, (uint16_t)cp);
        return m.x_advance;
    });
    const size_t first = lines.size() > 2 ? lines.size() - 2 : 0;
    _lineCount = 0;
    for (size_t i = first; i < lines.size(); i++) {
        _lines[_lineCount++] = Line{(uint16_t)lines[i].start, (uint16_t)lines[i].len};
    }
}

// ============================================================
// Rendering
// ============================================================

void FaceScreen::drawGear(LGFX_Sprite& c, int cx, int cy, uint16_t color) {
    for (int i = 0; i < 8; i++) {
        const float a = i * 0.7854f;
        c.fillCircle(cx + (int)(cosf(a) * 8.5f), cy + (int)(sinf(a) * 8.5f), 2, color);
    }
    c.fillCircle(cx, cy, 7, color);
    c.fillCircle(cx, cy, 3, ui::BG);
}

// Mic shortcut button (top-left): green mic = on (also while asleep: voice wakes it), blue = hearing
// you, red + slash = off (the grey "on" looked switched off to the user)
void FaceScreen::drawMic(LGFX_Sprite& c, int cx, int cy, bool muted, bool hearing) {
    const uint16_t bg = muted ? ui::ERR : (hearing ? ui::ACCENT : ui::PANEL_HI);
    const uint16_t fg = (muted || hearing) ? ui::TEXT : ui::OK;
    c.fillSmoothCircle(cx, cy, 15, bg);
    c.fillSmoothRoundRect(cx - 4, cy - 10, 9, 13, 4, fg);  // capsule
    c.fillArc(cx, cy - 2, 6, 8, 0, 180, fg);               // holder
    c.fillRect(cx - 1, cy + 6, 3, 3, fg);                  // stem
    c.fillRect(cx - 4, cy + 9, 9, 2, fg);                  // base
    if (muted) {
        c.drawWideLine(cx - 9, cy - 9, cx + 9, cy + 9, 2.8f, bg);  // gap around the slash
        c.drawWideLine(cx - 9, cy - 9, cx + 9, cy + 9, 1.3f, fg);
    }
}

void FaceScreen::drawStatus(LGFX_Sprite& c, const UiStatus& s) {
    const int cy = 14;
    drawMic(c, 21, 19, s.muted, !s.muted && _state == AVATAR_LISTENING);

    // Wi-Fi signal bars: green (ui::OK) when connected, grey when disconnected
    int bars = 0;
    if (s.wifi) bars = s.rssi > -58 ? 4 : (s.rssi > -68 ? 3 : (s.rssi > -78 ? 2 : 1));
    for (int i = 0; i < 4; i++) {
        const int h = 4 + i * 3;
        const uint16_t col = !s.wifi ? (s.configured ? 0x4208 : ui::ERR) : (i < bars ? ui::OK : 0x2945);
        c.fillRect(45 + i * 4, cy + 6 - h, 3, h, col);
    }

    // Center clock: bold & large for high visibility
    if (s.clock[0]) {
        c.setFont(&fonts::FreeSansBold12pt7b);
        c.setTextSize(1);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(s.clock, ui::W / 2, cy);
    }

    // Battery or USB power indicator
    const int bx = 254;
    if (s.battery >= 0) {
        const uint16_t batCol = s.battery > 30 ? ui::OK : (s.battery > 20 ? ui::WARN : ui::ERR);
        c.drawRect(bx, cy - 6, 20, 12, ui::DIM);
        c.fillRect(bx + 20, cy - 3, 2, 6, ui::DIM);
        c.fillRect(bx + 2, cy - 4, 16 * s.battery / 100, 8, batCol);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", s.battery);
        c.setFont(&fonts::Font2);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(buf, bx - 4, cy);
    } else {
        // Powered by USB (no LiPo battery discharge detected)
        c.drawRoundRect(bx, cy - 6, 20, 12, 2, ui::OK);
        c.fillRect(bx + 20, cy - 3, 2, 6, ui::OK);
        // Lightning bolt inside battery icon
        c.fillTriangle(bx + 11, cy - 4, bx + 6, cy, bx + 10, cy, ui::OK);
        c.fillTriangle(bx + 9, cy + 4, bx + 14, cy, bx + 10, cy, ui::OK);
        c.setFont(&fonts::Font2);
        c.setTextColor(ui::OK);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString("USB", bx - 4, cy);
    }
    const bool attention = _state == AVATAR_SETUP && (_now / 600) % 2;
    drawGear(c, 302, cy, attention ? ui::ACCENT : ui::DIM);
}

// Power save: 4 selectable clock/screensaver themes with date and live weather.
void FaceScreen::drawClock(LGFX_Sprite& c) {
    static const char* const kMonths[] = {"มกราคม", "กุมภาพันธ์", "มีนาคม", "เมษายน", "พฤษภาคม", "มิถุนายน",
                                          "กรกฎาคม", "สิงหาคม", "กันยายน", "ตุลาคม", "พฤศจิกายน", "ธันวาคม"};
    static const char* const kShortMonths[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.",
                                               "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};
    static const char* const kDays[] = {"อาทิตย์", "จันทร์", "อังคาร", "พุธ", "พฤหัสบดี", "ศุกร์", "เสาร์"};
    c.fillScreen(ui::BG);
    const time_t t = time(nullptr);
    struct tm lt{};
    localtime_r(&t, &lt);
    const bool synced = t > 1700000000;
    const int dx = (lt.tm_min % 5) * 2 - 4;  // gentle pixel drift: -4..+4
    const int dy = (lt.tm_min % 3) * 2 - 2;
    const int cx = ui::W / 2 + dx;

    UiStatus st;
    {
        Guard g(_lock);
        st = _status;
    }

    const uint8_t theme = gSettings.get().clockTheme % kClockThemeCount;
    const uint16_t eye = _eyes.color();
    constexpr uint16_t kDigits = 0xE73C;

    char hh[4] = "--", mm[4] = "--", ss[6] = "--";
    if (synced) {
        snprintf(hh, sizeof(hh), "%02d", lt.tm_hour);
        snprintf(mm, sizeof(mm), "%02d", lt.tm_min);
        snprintf(ss, sizeof(ss), ":%02d", lt.tm_sec);
    }

    if (theme == 0) {
        // ============================================================
        // Theme 0: ดิจิทัล + สภาพอากาศ (Modern Weather Hub)
        // ============================================================
        drawSleepingEyes(c, cx, 18 + dy, 28, 12, eye);

        const int yClock = 66 + dy;
        c.setFont(&fonts::Font7);  // 48 px 7-segment
        c.setTextSize(1);
        c.setTextColor(kDigits);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(hh, cx - 10, yClock);
        c.setTextDatum(textdatum_t::middle_left);
        c.drawString(mm, cx + 10, yClock);
        if (!synced || !(lt.tm_sec & 1)) {
            c.fillCircle(cx, yClock - 11, 3, eye);
            c.fillCircle(cx, yClock + 11, 3, eye);
        }
        c.setFont(&fonts::Font4);
        c.setTextColor(eye);
        c.setTextDatum(textdatum_t::bottom_left);
        c.drawString(ss, cx + 82, yClock + 24);

        if (synced) {
            char date[96];
            snprintf(date, sizeof(date), "วัน%sที่ %d %s %d", kDays[lt.tm_wday % 7], lt.tm_mday, kMonths[lt.tm_mon % 12],
                     lt.tm_year + 1900 + 543);
            gUi.drawFitted(c, date, cx, 114 + dy, 300, gUi.thai20(), 0xAD55, textdatum_t::middle_center);
        }

        drawWeatherCard(c, st, 14 + dx / 2, 134 + dy, ui::W - 28, 68, 0x2945);

    } else if (theme == 1) {
        // ============================================================
        // Theme 1: สมาร์ทวอช วงแหวน (Smartwatch Ring & Complications)
        // ============================================================
        const int rcx = 84 + dx / 2, rcy = 112 + dy;
        c.fillSmoothCircle(rcx, rcy, 72, 0x0841);
        for (int i = 0; i < 60; i++) {
            const float a = (i * 6 - 90) * 0.0174533f;
            const bool lit = synced && i <= lt.tm_sec;
            const bool major = (i % 5) == 0;
            const float r0 = major ? 58.0f : 61.0f;
            const float r1 = 69.0f;
            const uint16_t col = lit ? (i == lt.tm_sec ? ui::TEXT : eye) : (major ? 0x39E7 : 0x18E3);
            c.drawWideLine(rcx + cosf(a) * r0, rcy + sinf(a) * r0, rcx + cosf(a) * r1, rcy + sinf(a) * r1,
                           major ? 2.2f : 1.4f, col);
        }
        drawSleepingEyes(c, rcx, rcy - 34, 18, 8, eye);
        char hm[8];
        snprintf(hm, sizeof(hm), "%s:%s", hh, mm);
        c.setFont(&fonts::FreeSansBold12pt7b);
        c.setTextSize(1);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(hm, rcx, rcy - 2);
        c.setFont(&fonts::Font2);
        c.setTextColor(eye);
        c.drawString(ss, rcx, rcy + 24);

        // Top-right card: Date complication
        const int rx = 166 + dx / 2, rw = 142;
        c.fillSmoothRoundRect(rx, 24 + dy, rw, 74, 10, 0x1082);
        c.fillSmoothRoundRect(rx, 24 + dy, rw, 24, 10, eye);
        c.fillRect(rx, 38 + dy, rw, 10, eye);
        if (synced) {
            char dayStr[48];
            snprintf(dayStr, sizeof(dayStr), "วัน%s", kDays[lt.tm_wday % 7]);
            gUi.drawFitted(c, dayStr, rx + rw / 2, 36 + dy, rw - 12, gUi.thai20(), ui::BG, textdatum_t::middle_center);
            char dateLine[64];
            snprintf(dateLine, sizeof(dateLine), "%d %s %d", lt.tm_mday, kShortMonths[lt.tm_mon % 12],
                     lt.tm_year + 1900 + 543);
            gUi.drawFitted(c, dateLine, rx + rw / 2, 72 + dy, rw - 12, gUi.thai26(), ui::TEXT, textdatum_t::middle_center);
        }

        // Bottom-right card: Weather complication
        c.fillSmoothRoundRect(rx, 106 + dy, rw, 94, 10, 0x1082);
        c.drawRoundRect(rx, 106 + dy, rw, 94, 10, 0x2945);
        if (st.wxValid) {
            wx::drawIcon(c, rx + 26, 132 + dy, 24, st.wxCode, st.wxIsDay);
            char tBuf[16];
            snprintf(tBuf, sizeof(tBuf), "%.1f", st.wxTemp);
            c.setFont(&fonts::FreeSansBold12pt7b);
            c.setTextColor(ui::TEXT);
            c.setTextDatum(textdatum_t::middle_left);
            c.drawString(tBuf, rx + 50, 132 + dy);
            const int tw = c.textWidth(tBuf);
            c.drawCircle(rx + 54 + tw, 125 + dy, 3, ui::TEXT);
            c.setFont(&fonts::Font2);
            c.drawString("C", rx + 60 + tw, 134 + dy);
            gUi.drawFitted(c, st.wxCity[0] ? st.wxCity : "สภาพอากาศ", rx + 10, 159 + dy, rw - 20, gUi.thai20(),
                           ui::ACCENT, textdatum_t::middle_left);
            char subWx[80];
            if (st.wxPm25 >= 0) snprintf(subWx, sizeof(subWx), "%s • PM %d", wx::describe(st.wxCode), (int)st.wxPm25);
            else snprintf(subWx, sizeof(subWx), "%s • %d%%", wx::describe(st.wxCode), (int)st.wxHumidity);
            gUi.drawFitted(c, subWx, rx + 10, 183 + dy, rw - 20, gUi.thai20(), 0xCE79, textdatum_t::middle_left);
        } else {
            gUi.drawFitted(c, "กำลังซิงค์อากาศ...", rx + rw / 2, 152 + dy, rw - 16, gUi.thai20(), ui::DIM,
                           textdatum_t::middle_center);
        }

    } else if (theme == 2) {
        // ============================================================
        // Theme 2: ฟลิปการ์ด ไซเบอร์ (Cyber Flip Cards + Weather)
        // ============================================================
        const int cardY = 12 + dy, cardH = 88, cardW = 124;
        const int lx = cx - 138, rx = cx + 14;
        // Left flip card (HH)
        c.fillSmoothRoundRect(lx, cardY, cardW, cardH, 12, 0x10A3);
        c.drawRoundRect(lx, cardY, cardW, cardH, 12, eye);
        c.setFont(&fonts::Font7);
        c.setTextSize(1);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(hh, lx + cardW / 2, cardY + cardH / 2);
        c.drawFastHLine(lx + 2, cardY + cardH / 2, cardW - 4, ui::BG);
        c.drawFastHLine(lx + 2, cardY + cardH / 2 + 1, cardW - 4, 0x0841);

        // Right flip card (MM)
        c.fillSmoothRoundRect(rx, cardY, cardW, cardH, 12, 0x10A3);
        c.drawRoundRect(rx, cardY, cardW, cardH, 12, eye);
        c.drawString(mm, rx + cardW / 2, cardY + cardH / 2);
        c.drawFastHLine(rx + 2, cardY + cardH / 2, cardW - 4, ui::BG);
        c.drawFastHLine(rx + 2, cardY + cardH / 2 + 1, cardW - 4, 0x0841);

        // Center neon colon + sleeping mini-eyes
        if (!synced || !(lt.tm_sec & 1)) {
            c.fillCircle(cx, cardY + 32, 4, eye);
            c.fillCircle(cx, cardY + 56, 4, eye);
        }

        if (synced) {
            char date[96];
            snprintf(date, sizeof(date), "วัน%s %d %s %d", kDays[lt.tm_wday % 7], lt.tm_mday, kMonths[lt.tm_mon % 12],
                     lt.tm_year + 1900 + 543);
            gUi.drawFitted(c, date, cx, 116 + dy, 300, gUi.thai20(), eye, textdatum_t::middle_center);
        }
        drawWeatherCard(c, st, 14 + dx / 2, 134 + dy, ui::W - 28, 68, eye);

    } else {
        // ============================================================
        // Theme 3: คลาสสิก มินิมอล (Classic Big Clock + Compact Weather)
        // ============================================================
        drawSleepingEyes(c, cx, 22 + dy, 34, 36, eye);

        const int y = 88 + dy;
        c.setFont(&fonts::Font7);  // 7-segment digits, 48 px x2
        c.setTextSize(2);
        c.setTextColor(0xCE79);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(hh, cx - 14, y);
        c.setTextDatum(textdatum_t::middle_left);
        c.drawString(mm, cx + 14, y);
        c.setTextSize(1);
        if (!synced || !(lt.tm_sec & 1)) {
            c.fillRect(cx - 4, y - 22, 8, 8, 0xCE79);
            c.fillRect(cx - 4, y + 14, 8, 8, 0xCE79);
        }

        if (synced) {
            char date[96];
            snprintf(date, sizeof(date), "วัน%s %d %s %d", kDays[lt.tm_wday % 7], lt.tm_mday, kMonths[lt.tm_mon % 12],
                     lt.tm_year + 1900 + 543);
            gUi.drawFitted(c, date, cx, 158 + dy, 300, gUi.thai20(), ui::DIM, textdatum_t::middle_center);
        }
        if (st.wxValid) {
            char wxLine[128];
            if (st.wxPm25 >= 0) {
                snprintf(wxLine, sizeof(wxLine), "%s %.1f°C • %s • PM2.5 %d",
                         st.wxCity[0] ? st.wxCity : "อากาศ", st.wxTemp, wx::describe(st.wxCode), (int)st.wxPm25);
            } else {
                snprintf(wxLine, sizeof(wxLine), "%s %.1f°C • %s • ชื้น %d%%",
                         st.wxCity[0] ? st.wxCity : "อากาศ", st.wxTemp, wx::describe(st.wxCode), (int)st.wxHumidity);
            }
            gUi.drawFitted(c, wxLine, cx, 188 + dy, 300, gUi.thai20(), eye, textdatum_t::middle_center);
        }
    }

    if (st.radio[0]) {  // radio keeps playing under the clock
        drawRadioIcon(c, cx - 140, 222 + dy, ui::ACCENT);
        gUi.drawFitted(c, st.radio, cx - 122, 222 + dy, 262, gUi.thai20(), ui::DIM, textdatum_t::middle_left);
    } else {
        gUi.drawFitted(c, "พูดหรือแตะเพื่อปลุก", cx, 222 + dy, 300, gUi.thai20(), 0x4208, textdatum_t::middle_center);
    }
}

// Small radio: body, speaker grille and antenna (fonts have no emoji)
void FaceScreen::drawRadioIcon(LGFX_Sprite& c, int cx, int cy, uint16_t color) {
    c.drawRoundRect(cx - 9, cy - 5, 18, 12, 2, color);
    c.fillCircle(cx - 3, cy + 1, 3, color);
    c.drawFastVLine(cx + 4, cy - 2, 6, color);
    c.drawFastVLine(cx + 6, cy - 2, 6, color);
    c.drawLine(cx - 6, cy - 6, cx + 4, cy - 11, color);
}

// Radio / MP3 player shortcut button next to the vehicle dash button
void FaceScreen::drawRadioButton(LGFX_Sprite& c, int cx, int cy, bool active) {
    c.fillSmoothCircle(cx, cy, 17, active ? 0x0A32 : ui::PANEL_HI);
    if (active) c.drawCircle(cx, cy, 17, ui::ACCENT);
    const uint16_t col = active ? ui::ACCENT : ui::TEXT;
    // Beamed eighth notes (♫)
    c.fillCircle(cx - 5, cy + 5, 3, col);
    c.fillCircle(cx + 4, cy + 3, 3, col);
    c.fillRect(cx - 3, cy - 6, 2, 11, col);
    c.fillRect(cx + 6, cy - 8, 2, 11, col);
    c.drawWideLine(cx - 3, cy - 6, cx + 7, cy - 8, 2.5f, active ? ui::TEXT : ui::ACCENT);
}

// Vehicle dash shortcut: a round button with a small gauge (arc, red zone, needle)
void FaceScreen::drawDashButton(LGFX_Sprite& c, int cx, int cy) {
    c.fillSmoothCircle(cx, cy, 17, ui::PANEL_HI);
    const int gy = cy + 4;  // the half dial sits a little low to look centred
    c.fillArc(cx, gy, 9, 11, 180, 315, ui::DIM);
    c.fillArc(cx, gy, 9, 11, 315, 360, ui::ERR);
    const float a = 290 * 0.0174533f;
    c.drawWideLine(cx, gy, cx + cosf(a) * 9, gy + sinf(a) * 9, 2.0f, ui::TEXT);
    c.fillCircle(cx, gy, 2, ui::TEXT);
}

void FaceScreen::draw(LGFX_Sprite& c) {
    if (_powerSave) {
        _radioShown = false;
        _radioBtnShown = false;
        _dashShown = false;
        drawClock(c);
        return;
    }
    c.fillScreen(ui::BG);
    _eyes.draw(c);

    UiStatus status;
    char hint[sizeof(_hint)];
    bool showHint;
    {
        Guard g(_lock);
        status = _status;
        showHint = _hint[0] && _hintTimer.active(_now);
        if (showHint) memcpy(hint, _hint, sizeof(hint));
    }
    drawStatus(c, status);

    _dashShown = _openDash && !_subtitleVisible;
    if (_dashShown) drawDashButton(c, kDashX, kDashY);
    _radioBtnShown = _openRadio && !_subtitleVisible;
    if (_radioBtnShown) drawRadioButton(c, kRadioBtnX, kRadioBtnY, status.radio[0] != '\0');

    _radioShown = status.radio[0] && !_subtitleVisible;
    if (status.radio[0] && !_subtitleVisible) {  // Internet radio: what is playing, under the eyes
        drawRadioIcon(c, 22, 222, ui::ACCENT);
        const int rightStop = _radioBtnShown ? (kRadioBtnX - 22) : (_dashShown ? (kDashX - 22) : (ui::W - 8));
        const int w = rightStop - 40;
        gUi.drawFitted(c, status.radio, 40, 222, w, gUi.thai20(), ui::DIM, textdatum_t::middle_left);
    }

    if (_subtitleVisible && _lineCount > 0) {
        c.fillRect(0, kSubtitleY - 4, ui::W, ui::H - kSubtitleY + 4, ui::BG);
        c.setFont(gUi.thai26());
        c.setTextDatum(textdatum_t::top_center);
        c.setTextColor(_renderRole == 'u' ? ui::USER : (_renderRole == 'a' ? ui::TEXT : ui::DIM));
        char line[kTextCap];
        for (int i = 0; i < _lineCount; i++) {
            memcpy(line, _render + _lines[i].start, _lines[i].len);
            line[_lines[i].len] = '\0';
            c.drawString(line, ui::W / 2, kSubtitleY + i * 30);
        }
    }

    if (showHint) {
        c.setFont(gUi.thai20());
        const int w = c.textWidth(hint) + 24;
        const int x = (ui::W - w) / 2;
        c.fillSmoothRoundRect(x, 26, w, 26, 13, ui::PANEL_HI);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(hint, ui::W / 2, 39);
    }
}
