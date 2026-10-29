/**
 * @file FaceScreen.cpp
 * @brief Eyes + status + subtitles home screen + multi-theme power-save screensaver with weather
 */

#include "FaceScreen.h"

#include <math.h>
#include <string.h>

#include <vector>

#include "../app/Settings.h"
#include "../display/fonts/ClockDigits116.h"  // Oswald Bold (SIL OFL), T135 big-digit face
#include "../display/fonts/ClockDigits64.h"
#include "../display/fonts/ClockText20.h"
#include "../display/fonts/ClockText28.h"
#include "../display/fonts/ClockText42.h"
#include "../util/TextWrap.h"
#include "../util/Utf8.h"
#include "AaIcon.h"
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
constexpr int kMicX = 23, kMicY = 216;            // mic on / off (bottom left, T137: was top left)
constexpr float kEyesYIdle = 112.0f;
constexpr float kEyesYSubtitle = 90.0f;

void drawSleepingEyes(LGFX_Sprite& c, int cx, int y, int w, int gap, uint16_t col) {
    c.fillSmoothRoundRect(cx - gap / 2 - w, y, w, 5, 2, col);
    c.fillSmoothRoundRect(cx + gap / 2, y, w, 5, 2, col);
}

// T135: LovyanGFX's VLW drawChar copies the whole glyph onto the stack (alloca w*h): a big digit is ~6 KB, as big as
// the UI task's 6 KB stack (stack canary panic on the board). The big digits are drawn straight from the font
// in flash instead, blended towards the black background.
namespace bigfont {
int32_t be32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | p[2] << 8 | p[3]); }

struct Glyph {
    int32_t h, w, adv, dY, dX;
    const uint8_t* bmp;
};

// one UTF-8 code point ("°" is 2 bytes); advances s
int32_t next(const char*& s) {
    const uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    int32_t cp = c & (0x3F >> extra);
    while (extra-- && ((uint8_t)*s & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)*s++ & 0x3F);
    return cp;
}

bool find(const uint8_t* f, int32_t cp, Glyph& g) {
    const int32_t count = be32(f);
    const uint8_t* rec = f + 24;
    const uint8_t* bmp = rec + count * 28;
    for (int32_t i = 0; i < count; i++, rec += 28) {
        const int32_t h = be32(rec + 4), w = be32(rec + 8);
        if (be32(rec) == cp) {
            g = {h, w, be32(rec + 12), be32(rec + 16), be32(rec + 20), bmp};
            return true;
        }
        bmp += h * w;
    }
    return false;
}

// the widest digit: with `tabular` every digit takes this much, like a real clock - a "1" no longer pulls the time
// off centre or moves the rest of the line when the minute changes
int digitCell(const uint8_t* f) {
    int w = 0;
    Glyph g;
    for (char d = '0'; d <= '9'; d++) {
        if (find(f, d, g) && g.adv > w) w = g.adv;
    }
    return w;
}

int width(const uint8_t* f, const char* s, bool tabular = false) {
    const int cell = tabular ? digitCell(f) : 0;
    int w = 0;
    Glyph g;
    while (*s) {
        const int32_t cp = next(s);
        if (find(f, cp, g)) w += tabular && cp >= '0' && cp <= '9' ? cell : g.adv;
    }
    return w;
}

/** `s` with its vertical middle at y; datum: 0 left, 1 centre, 2 right of x. */
void draw(LGFX_Sprite& c, const uint8_t* f, const char* s, int x, int y, int datum, uint16_t col,
          bool tabular = false) {
    const int32_t ascent = be32(f + 16), descent = be32(f + 20);
    const int baseline = y - (ascent + descent) / 2 + ascent;
    const int cell = tabular ? digitCell(f) : 0;
    if (datum) x -= width(f, s, tabular) / (datum == 1 ? 2 : 1);
    uint16_t shade[16];  // the colour at 16 alpha levels over black
    const int r = col >> 11, gr = (col >> 5) & 63, b = col & 31;
    for (int a = 0; a < 16; a++) {
        shade[a] = (uint16_t)(((r * (a + 1) / 16) << 11) | ((gr * (a + 1) / 16) << 5) | (b * (a + 1) / 16));
    }
    Glyph g;
    while (*s) {
        const int32_t cp = next(s);
        if (!find(f, cp, g)) continue;
        const bool inCell = tabular && cp >= '0' && cp <= '9';
        const int left = x + g.dX + (inCell ? (cell - g.adv) / 2 : 0), top = baseline - g.dY;
        for (int yy = 0; yy < g.h; yy++) {
            const uint8_t* row = g.bmp + yy * g.w;
            for (int xx = 0; xx < g.w; xx++) {
                if (row[xx] >= 16) c.drawPixel(left + xx, top + yy, shade[row[xx] >> 4]);
            }
        }
        x += inCell ? cell : g.adv;
    }
}
}  // namespace bigfont

// the colour at pct % brightness (over the black background)
uint16_t dimTo(uint16_t c, int pct) {
    const int r = (c >> 11) * pct / 100, g = ((c >> 5) & 63) * pct / 100, b = (c & 31) * pct / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void drawWeatherCard(LGFX_Sprite& c, const UiStatus& st, int x, int y, int w, int h, uint16_t P, uint16_t S,
                     uint16_t D) {
    c.fillSmoothRoundRect(x, y, w, h, 10, dimTo(P, 8));
    c.drawRoundRect(x, y, w, h, 10, D);
    if (!st.wxValid) {
        char waitMsg[96];
        snprintf(waitMsg, sizeof(waitMsg), "สภาพอากาศ %s • กำลังซิงค์...", st.wxCity[0] ? st.wxCity : "อัตโนมัติ");
        gUi.drawFitted(c, waitMsg, x + w / 2, y + h / 2, w - 20, gUi.thai20(), S, textdatum_t::middle_center);
        return;
    }
    const int my = y + h / 2;
    wx::drawIcon(c, x + 32, my, h > 64 ? 32 : 26, st.wxCode, st.wxIsDay);
    char tBuf[16];
    snprintf(tBuf, sizeof(tBuf), "%.1f°C", st.wxTemp);
    bigfont::draw(c, ClockText42, tBuf, x + 64, my + 1, 0, P);
    const int right = x + w - 12, textW = w - 64 - bigfont::width(ClockText42, tBuf) - 22;
    gUi.drawFitted(c, st.wxCity[0] ? st.wxCity : "ตำแหน่งปัจจุบัน", right, my - 12, textW, gUi.thai20(), P,
                   textdatum_t::middle_right);
    char cond[96];
    if (st.wxPm25 >= 0) snprintf(cond, sizeof(cond), "%s • PM2.5 %d", wx::describe(st.wxCode), (int)st.wxPm25);
    else snprintf(cond, sizeof(cond), "%s • ชื้น %d%%", wx::describe(st.wxCode), (int)st.wxHumidity);
    gUi.drawFitted(c, cond, right, my + 12, textW, gUi.thai20(), S, textdatum_t::middle_right);
}

// T136: crisp 7-segment digits drawn as hexagon segments at any size (Font7 scaled x2 came out stair-stepped);
// the unlit segments glow faintly like a real LED clock
namespace seg7 {
void horiz(LGFX_Sprite& c, int x0, int x1, int y, int t, uint16_t col) {  // tip (x0, y) to tip (x1, y)
    const int h = t / 2;
    if (x1 - x0 <= 2 * h) return;
    c.fillRect(x0 + h, y - h, x1 - x0 - 2 * h, 2 * h + 1, col);
    c.fillTriangle(x0, y, x0 + h, y - h, x0 + h, y + h, col);
    c.fillTriangle(x1, y, x1 - h, y - h, x1 - h, y + h, col);
}

void vert(LGFX_Sprite& c, int x, int y0, int y1, int t, uint16_t col) {  // tip (x, y0) to tip (x, y1)
    const int h = t / 2;
    if (y1 - y0 <= 2 * h) return;
    c.fillRect(x - h, y0 + h, 2 * h + 1, y1 - y0 - 2 * h, col);
    c.fillTriangle(x, y0, x - h, y0 + h, x + h, y0 + h, col);
    c.fillTriangle(x, y1, x - h, y1 - h, x + h, y1 - h, col);
}

constexpr uint8_t kMask[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};  // bits a..g

/** One digit in the box (x, y, w, h), segments t thick; unlit ones in `off` (0 = not drawn). */
void digit(LGFX_Sprite& c, char ch, int x, int y, int w, int h, int t, uint16_t on, uint16_t off) {
    const uint8_t m = ch >= '0' && ch <= '9' ? kMask[ch - '0'] : (ch == '-' ? 0x40 : 0);
    const int g = t / 3 + 1;  // the gap where two segments meet
    const int l = x + t / 2, r = x + w - t / 2 - 1, top = y + t / 2, mid = y + h / 2, bot = y + h - t / 2 - 1;
    for (int pass = 0; pass < 2; pass++) {  // unlit first, lit on top
        for (int s = 0; s < 7; s++) {
            const bool lit = m & (1 << s);
            if (lit != (pass == 1) || (!lit && !off)) continue;
            const uint16_t col = lit ? on : off;
            switch (s) {
                case 0: horiz(c, l + g, r - g, top, t, col); break;   // a
                case 1: vert(c, r, top + g, mid - g, t, col); break;  // b
                case 2: vert(c, r, mid + g, bot - g, t, col); break;  // c
                case 3: horiz(c, l + g, r - g, bot, t, col); break;   // d
                case 4: vert(c, l, mid + g, bot - g, t, col); break;  // e
                case 5: vert(c, l, top + g, mid - g, t, col); break;  // f
                default: horiz(c, l + g, r - g, mid, t, col); break;  // g
            }
        }
    }
}

/** "HH:MM" centred on (cx, cy) with the colon in the middle; the colon lit when `colon`. */
void hhmm(LGFX_Sprite& c, const char* hh, const char* mm, int cx, int cy, int w, int h, int t, uint16_t on,
          uint16_t off, bool colon) {
    const int gap = w / 5, colonW = t * 3;
    int x = cx - (4 * w + 4 * gap + colonW) / 2;
    const int y = cy - h / 2;
    digit(c, hh[0], x, y, w, h, t, on, off);
    x += w + gap;
    digit(c, hh[1], x, y, w, h, t, on, off);
    x += w + 2 * gap + colonW;
    digit(c, mm[0], x, y, w, h, t, on, off);
    x += w + gap;
    digit(c, mm[1], x, y, w, h, t, on, off);
    if (const uint16_t dot = colon ? on : off) {
        const int d = t + 1;
        c.fillRect(cx - d / 2, cy - h / 4 - d / 2, d, d, dot);
        c.fillRect(cx - d / 2, cy + h / 4 - d / 2, d, d, dot);
    }
}
}  // namespace seg7

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
    const bool mic = e.startX <= kMicX + 24 && e.startY >= kMicY - 26 && _micShown;
    const bool dash = e.startX >= kDashX - 20 && e.startY >= kDashY - 26 && _dashShown;
    const bool radioBtn = e.startX >= kRadioBtnX - 22 && e.startX < kDashX - 20 && e.startY >= kRadioBtnY - 26 && _radioBtnShown;
    const bool radioBar = e.startY >= 204 && e.startX > kMicX + 24 && e.startX < kRadioBtnX - 22 && _radioShown;
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

// T137 (the user: the status bar items were different sizes, the buttons looked jagged): every icon is drawn
// anti-aliased (AaIcon) once per state and cached; the status bar is one height (~20 px) with Oswald text
namespace {
aa::Icon gGearIcon(26, 26), gMicIcon(38, 38), gWifiIcon(30, 22), gBatteryIcon(30, 16), gRadioBtnIcon(38, 38),
    gDashIcon(38, 38), gRadioIcon(22, 18);
constexpr float kPi = 3.14159265f;
}  // namespace

void FaceScreen::drawGear(LGFX_Sprite& c, int cx, int cy, uint16_t color) {
    gGearIcon.draw(c, cx, cy, color, [color](aa::Painter& p) {
        float d = aa::circle(p.x(), p.y(), 0, 0, 7.0f);
        for (int i = 0; i < 8; i++) {
            const float a = i * kPi / 4;
            d = fminf(d, aa::segment(p.x(), p.y(), cosf(a) * 5.5f, sinf(a) * 5.5f, cosf(a) * 9.5f, sinf(a) * 9.5f, 2.1f));
        }
        p.layer(d, color);
        p.layer(aa::circle(p.x(), p.y(), 0, 0, 3.0f), ui::BG);
    });
}

// Mic button (bottom left, T137): green mic = on (also while asleep: voice wakes it), blue = hearing you,
// red + slash = off (the grey "on" looked switched off to the user)
void FaceScreen::drawMic(LGFX_Sprite& c, int cx, int cy, bool muted, bool hearing) {
    const uint16_t bg = muted ? ui::ERR : (hearing ? ui::ACCENT : ui::PANEL_HI);
    const uint16_t fg = (muted || hearing) ? ui::TEXT : ui::OK;
    gMicIcon.draw(c, cx, cy, (muted ? 1 : 0) | (hearing ? 2 : 0), [=](aa::Painter& p) {
        const float x = p.x(), y = p.y();
        p.layer(aa::circle(x, y, 0, 0, 17.0f), bg);
        float d = aa::segment(x, y, 0, -7.5f, 0, -1.5f, 4.3f);              // capsule
        d = fminf(d, aa::arc(x, y, 0, -3.0f, 8.0f, 1.15f, 10.0f, 170.0f));  // holder
        d = fminf(d, aa::segment(x, y, 0, 5.0f, 0, 9.0f, 1.15f));           // stem
        d = fminf(d, aa::segment(x, y, -4.5f, 9.5f, 4.5f, 9.5f, 1.15f));    // base
        p.layer(d, fg);
        if (muted) {
            p.layer(aa::segment(x, y, -9.5f, -9.5f, 9.5f, 9.5f, 2.8f), bg);  // a gap around the slash
            p.layer(aa::segment(x, y, -9.5f, -9.5f, 9.5f, 9.5f, 1.3f), fg);
        }
    });
}

void FaceScreen::drawStatus(LGFX_Sprite& c, const UiStatus& s) {
    const int cy = 15;
    // Wi-Fi: a fan of 3 arcs over a dot, lit by signal strength
    int bars = 0;
    if (s.wifi) bars = s.rssi > -58 ? 4 : (s.rssi > -68 ? 3 : (s.rssi > -78 ? 2 : 1));
    const uint16_t off = !s.wifi ? (s.configured ? 0x4208 : ui::ERR) : 0x2945;
    gWifiIcon.draw(c, 19, cy, bars | (s.wifi ? 8 : 0) | (s.configured ? 16 : 0), [=](aa::Painter& p) {
        const float x = p.x(), y = p.y() - 7.0f;  // the fan's origin sits low in the icon
        p.layer(aa::circle(x, y, 0, 0, 2.0f), bars >= 1 ? ui::OK : off);
        static const float kR[] = {5.5f, 10.0f, 14.5f};
        for (int i = 0; i < 3; i++) {
            p.layer(aa::arc(x, y, 0, 0, kR[i], 1.45f, 225.0f, 315.0f), bars >= i + 2 ? ui::OK : off);
        }
    });

    // the clock: Oswald, equal-width digits (nothing moves when a minute changes)
    if (s.clock[0]) bigfont::draw(c, ClockText28, s.clock, ui::W / 2, cy, 1, ui::TEXT, true);

    // battery % or USB power
    const int bx = 260;
    const bool usb = s.battery < 0;
    const int pct = usb ? 0 : (s.battery > 100 ? 100 : s.battery);
    const uint16_t batCol = usb ? ui::OK : (pct > 30 ? ui::OK : (pct > 20 ? ui::WARN : ui::ERR));
    gBatteryIcon.draw(c, bx, cy, usb ? -1 : pct, [=](aa::Painter& p) {
        const float x = p.x(), y = p.y();
        p.layer(fabsf(aa::roundRect(x, y, -1.5f, 0, 11.5f, 6.8f, 2.6f)) - 0.8f, usb ? ui::OK : ui::DIM);
        p.layer(aa::roundRect(x, y, 11.6f, 0, 1.5f, 3.0f, 1.0f), usb ? ui::OK : ui::DIM);
        if (usb) {  // a lightning bolt
            float d = aa::segment(x, y, 1.0f, -5.0f, -4.5f, 0.8f, 1.05f);
            d = fminf(d, aa::segment(x, y, -4.5f, 0.8f, 1.5f, -0.8f, 1.05f));
            d = fminf(d, aa::segment(x, y, 1.5f, -0.8f, -4.0f, 5.0f, 1.05f));
            p.layer(d, ui::OK);
        } else if (pct > 0) {
            const float w = 18.0f * pct / 100.0f;
            p.layer(aa::roundRect(x, y, -10.5f + w / 2, 0, w / 2, 4.2f, 1.2f), batCol);
        }
    });
    char buf[8];
    if (usb) snprintf(buf, sizeof(buf), "USB");
    else snprintf(buf, sizeof(buf), "%d%%", pct);
    bigfont::draw(c, ClockText20, buf, bx - 18, cy, 2, usb ? ui::OK : ui::TEXT, true);

    const bool attention = _state == AVATAR_SETUP && (_now / 600) % 2;
    drawGear(c, 302, cy, attention ? ui::ACCENT : ui::DIM);
}

// Power save: 7 clock themes (T129 / T135 / T136) in one neon colour the user picks: the clock as big as the screen
// allows, no "พูดหรือแตะเพื่อปลุก" line, nothing drifts (the user: text moving every minute looked broken)
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
    const int cx = ui::W / 2;

    UiStatus st;
    {
        Guard g(_lock);
        st = _status;
    }

    const SettingsData cfg = gSettings.get();
    const uint8_t theme = cfg.clockTheme % kClockThemeCount;
    // the neon set: P the colour, S softer text, D lines / unlit parts, U the unlit 7-segment glow
    const uint16_t P = clockColorRgb565(cfg.clockColor) ? clockColorRgb565(cfg.clockColor) : _eyes.color();
    const uint16_t S = dimTo(P, 62), D = dimTo(P, 24), U = dimTo(P, 9);
    const bool colon = !synced || !(lt.tm_sec & 1);

    char hh[4] = "--", mm[4] = "--", ss[6] = ":--";
    if (synced) {
        snprintf(hh, sizeof(hh), "%02d", lt.tm_hour);
        snprintf(mm, sizeof(mm), "%02d", lt.tm_min);
        snprintf(ss, sizeof(ss), ":%02d", lt.tm_sec);
    }
    char longDate[96] = "", shortDate[48] = "";
    if (synced) {
        snprintf(longDate, sizeof(longDate), "วัน%sที่ %d %s %d", kDays[lt.tm_wday % 7], lt.tm_mday,
                 kMonths[lt.tm_mon % 12], lt.tm_year + 1900 + 543);
        snprintf(shortDate, sizeof(shortDate), "%s %d", kShortMonths[lt.tm_mon % 12], lt.tm_year + 1900 + 543);
    }

    if (theme == 0) {
        // ============================================================
        // Theme 0: ดิจิทัล + สภาพอากาศ - 7-segment time across the screen, date, weather card
        // ============================================================
        seg7::hhmm(c, hh, mm, cx, 62, 54, 100, 11, P, U, colon);
        gUi.drawFitted(c, longDate, cx, 136, 304, gUi.thai26(), S, textdatum_t::middle_center);
        drawWeatherCard(c, st, 8, 158, ui::W - 16, 76, P, S, D);

    } else if (theme == 1) {
        // ============================================================
        // Theme 1: สมาร์ทวอช วงแหวน - 60-second ring filling the height, date + weather complications on the right
        // ============================================================
        const int rcx = 112, rcy = 120, R = 106;
        c.fillSmoothCircle(rcx, rcy, R, dimTo(P, 6));
        for (int i = 0; i < 60; i++) {
            const float a = (i * 6 - 90) * 0.0174533f;
            const bool lit = synced && i <= lt.tm_sec;
            const bool major = (i % 5) == 0;
            const float r0 = major ? R - 15.0f : R - 10.0f, r1 = R - 3.0f;
            const uint16_t col = lit ? (i == lt.tm_sec ? ui::TEXT : P) : (major ? D : U);
            c.drawWideLine(rcx + cosf(a) * r0, rcy + sinf(a) * r0, rcx + cosf(a) * r1, rcy + sinf(a) * r1,
                           major ? 2.2f : 1.4f, col);
        }
        drawSleepingEyes(c, rcx, rcy - 52, 16, 10, S);
        char hm[8];
        snprintf(hm, sizeof(hm), "%s:%s", hh, mm);
        bigfont::draw(c, ClockDigits64, hm, rcx, rcy - 2, 1, P, true);
        bigfont::draw(c, ClockText28, ss, rcx, rcy + 46, 1, S, true);

        const int rx = 226, rw = 88, rmid = rx + rw / 2;
        c.fillSmoothRoundRect(rx, 12, rw, 102, 10, dimTo(P, 8));
        c.fillSmoothRoundRect(rx, 12, rw, 28, 10, P);
        c.fillRect(rx, 28, rw, 12, P);
        if (synced) {
            gUi.drawFitted(c, kDays[lt.tm_wday % 7], rmid, 26, rw - 8, gUi.thai20(), ui::BG, textdatum_t::middle_center);
            char day[4];
            snprintf(day, sizeof(day), "%d", lt.tm_mday);
            bigfont::draw(c, ClockText42, day, rmid, 68, 1, P, true);
            gUi.drawFitted(c, shortDate, rmid, 100, rw - 8, gUi.thai20(), S, textdatum_t::middle_center);
        }
        c.fillSmoothRoundRect(rx, 124, rw, 104, 10, dimTo(P, 8));
        c.drawRoundRect(rx, 124, rw, 104, 10, D);
        if (st.wxValid) {
            wx::drawIcon(c, rmid, 152, 28, st.wxCode, st.wxIsDay);
            char tBuf[16];
            snprintf(tBuf, sizeof(tBuf), "%.0f°C", st.wxTemp);
            bigfont::draw(c, ClockText42, tBuf, rmid, 188, 1, P, true);
            char sub[32];
            if (st.wxPm25 >= 0) snprintf(sub, sizeof(sub), "PM2.5 %d", (int)st.wxPm25);
            else snprintf(sub, sizeof(sub), "ชื้น %d%%", (int)st.wxHumidity);
            gUi.drawFitted(c, sub, rmid, 214, rw - 8, gUi.thai20(), S, textdatum_t::middle_center);
        } else {
            gUi.drawFitted(c, "รออากาศ...", rmid, 176, rw - 8, gUi.thai20(), D, textdatum_t::middle_center);
        }

    } else if (theme == 2) {
        // ============================================================
        // Theme 2: ฟลิปการ์ด ไซเบอร์ - two tall flip cards with the big Oswald digits, date, weather card
        // ============================================================
        const int cardY = 6, cardH = 136, cardW = 140;
        const int lx = cx - 10 - cardW, rx = cx + 10;
        for (int k = 0; k < 2; k++) {
            const int x = k ? rx : lx;
            c.fillSmoothRoundRect(x, cardY, cardW, cardH, 14, dimTo(P, 10));
            c.drawRoundRect(x, cardY, cardW, cardH, 14, P);
            bigfont::draw(c, ClockDigits116, k ? mm : hh, x + cardW / 2, cardY + cardH / 2 + 2, 1, P, true);
            c.drawFastHLine(x + 2, cardY + cardH / 2, cardW - 4, ui::BG);  // the fold
            c.drawFastHLine(x + 2, cardY + cardH / 2 + 1, cardW - 4, ui::BG);
        }
        if (colon) {
            c.fillSmoothCircle(cx, cardY + cardH / 2 - 24, 5, P);
            c.fillSmoothCircle(cx, cardY + cardH / 2 + 24, 5, P);
        }
        gUi.drawFitted(c, longDate, cx, 160, 304, gUi.thai26(), S, textdatum_t::middle_center);
        drawWeatherCard(c, st, 8, 178, ui::W - 16, 58, P, S, D);

    } else if (theme == 4) {
        // ============================================================
        // Theme 4: อนาล็อก เข็มนาฬิกา (T135) - dial the full height, date / time / weather column on the right
        // ============================================================
        const int acx = 114, acy = 120, R = 110;
        c.fillSmoothCircle(acx, acy, R, dimTo(P, 6));
        c.drawCircle(acx, acy, R, D);
        for (int i = 0; i < 60; i++) {
            const float a = (i * 6 - 90) * 0.0174533f;
            const bool major = (i % 5) == 0;
            const float r0 = major ? R - 15.0f : R - 8.0f, r1 = R - 4.0f;
            c.drawWideLine(acx + cosf(a) * r0, acy + sinf(a) * r0, acx + cosf(a) * r1, acy + sinf(a) * r1,
                           major ? 1.8f : 0.8f, major ? S : D);
        }
        static const char* const kNum[] = {"12", "3", "6", "9"};
        for (int q = 0; q < 4; q++) {
            const float a = (q * 90 - 90) * 0.0174533f;
            bigfont::draw(c, ClockText28, kNum[q], acx + (int)(cosf(a) * (R - 30)), acy + (int)(sinf(a) * (R - 30)),
                          1, P);
        }
        drawSleepingEyes(c, acx, acy + 36, 16, 10, S);
        if (synced) {
            const float s = lt.tm_sec, m = lt.tm_min + s / 60.0f, h = (lt.tm_hour % 12) + m / 60.0f;
            const auto hand = [&](float deg, float len, float tail, float r, uint16_t col) {
                const float a = (deg - 90) * 0.0174533f;
                c.drawWideLine(acx - cosf(a) * tail, acy - sinf(a) * tail, acx + cosf(a) * len, acy + sinf(a) * len, r,
                               col);
            };
            hand(h * 30, R * 0.52f, 10, 4.5f, P);
            hand(m * 6, R * 0.78f, 12, 3.0f, P);
            hand(s * 6, R * 0.86f, 18, 1.1f, ui::TEXT);
        }
        c.fillSmoothCircle(acx, acy, 6, P);
        c.fillSmoothCircle(acx, acy, 2, ui::BG);

        const int rmid = 272, rw = 88;
        if (synced) {
            gUi.drawFitted(c, kDays[lt.tm_wday % 7], rmid, 20, rw, gUi.thai20(), P, textdatum_t::middle_center);
            char day[4];
            snprintf(day, sizeof(day), "%d", lt.tm_mday);
            bigfont::draw(c, ClockText42, day, rmid, 56, 1, P, true);
            gUi.drawFitted(c, shortDate, rmid, 88, rw, gUi.thai20(), S, textdatum_t::middle_center);
            char hm[8];
            snprintf(hm, sizeof(hm), "%s:%s", hh, mm);
            bigfont::draw(c, ClockText28, hm, rmid, 118, 1, S, true);
        }
        c.drawFastHLine(rmid - rw / 2 + 6, 138, rw - 12, D);
        if (st.wxValid) {
            wx::drawIcon(c, rmid, 162, 26, st.wxCode, st.wxIsDay);
            char tBuf[16];
            snprintf(tBuf, sizeof(tBuf), "%.0f°C", st.wxTemp);
            bigfont::draw(c, ClockText42, tBuf, rmid, 198, 1, P, true);
            char sub[32];
            if (st.wxPm25 >= 0) snprintf(sub, sizeof(sub), "PM2.5 %d", (int)st.wxPm25);
            else snprintf(sub, sizeof(sub), "ชื้น %d%%", (int)st.wxHumidity);
            gUi.drawFitted(c, sub, rmid, 226, rw, gUi.thai20(), S, textdatum_t::middle_center);
        } else {
            gUi.drawFitted(c, "รออากาศ...", rmid, 176, rw, gUi.thai20(), D, textdatum_t::middle_center);
        }

    } else if (theme == 5) {
        // ============================================================
        // Theme 5: ตัวเลขใหญ่ ตั้งโต๊ะ (T135, the user's photo of a desk clock: date / huge digits / day + weather)
        // ============================================================
        // English like the photo (the user: these faces need not be Thai - sharp and good-looking first), all Oswald
        static const char* const kMonEn[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        static const char* const kDayEn[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        if (synced) {
            char date[32];
            snprintf(date, sizeof(date), "%s %d, %d", kMonEn[lt.tm_mon % 12], lt.tm_mday, lt.tm_year + 1900);
            bigfont::draw(c, ClockText42, date, cx, 24, 1, P, true);
        }
        // HH : MM in equal digit cells around the centre colon: always centred, nothing moves when a digit changes
        const int colonW = bigfont::width(ClockDigits116, ":");
        bigfont::draw(c, ClockDigits116, hh, cx - colonW / 2 - 2, 112, 2, P, true);
        bigfont::draw(c, ClockDigits116, mm, cx + colonW / 2 + 2, 112, 0, P, true);
        if (colon) bigfont::draw(c, ClockDigits116, ":", cx, 106, 1, P);
        if (synced) bigfont::draw(c, ClockText42, kDayEn[lt.tm_wday % 7], 14, 200, 0, P);
        if (st.wxValid) {
            char tBuf[16];
            snprintf(tBuf, sizeof(tBuf), "%.0f°C", st.wxTemp);
            bigfont::draw(c, ClockText42, tBuf, 308, 200, 2, P, true);
            wx::drawIcon(c, 308 - bigfont::width(ClockText42, tBuf, true) - 30, 198, 36, st.wxCode, st.wxIsDay);
        }

    } else if (theme == 6) {
        // ============================================================
        // Theme 6: ดวงอาทิตย์ & ดวงจันทร์ (T135) - sky arc 06:00 - 18:00 - 06:00, 7-segment time under the horizon
        // ============================================================
        const int hy = 94, arx = 134, ary = 72;
        const int mod = lt.tm_hour * 60 + lt.tm_min;  // minutes since midnight
        const bool day = mod >= 360 && mod < 1080;
        const float frac = day ? (mod - 360) / 720.0f : ((mod - 1080 + 1440) % 1440) / 720.0f;
        constexpr int kDots = 48;
        for (int k = 0; k <= kDots; k++) {
            const float a = 3.14159f * k / kDots;
            const bool passed = synced && (float)k / kDots <= frac;
            c.fillCircle(cx - (int)(cosf(a) * arx), hy - (int)(sinf(a) * ary), passed ? 2 : 1, passed ? P : D);
        }
        c.drawFastHLine(cx - arx - 10, hy, 2 * arx + 20, D);
        if (synced) {
            const float a = 3.14159f * frac;
            const int bx = cx - (int)(cosf(a) * arx), by = hy - (int)(sinf(a) * ary);
            if (day) {
                for (int r = 0; r < 8; r++) {
                    const float ra = r * 0.785398f;
                    c.drawWideLine(bx + cosf(ra) * 14, by + sinf(ra) * 14, bx + cosf(ra) * 19, by + sinf(ra) * 19, 1.0f,
                                   ui::WARN);
                }
                c.fillSmoothCircle(bx, by, 11, ui::WARN);
            } else {
                c.fillSmoothCircle(bx, by, 11, 0xE71C);
                c.fillSmoothCircle(bx + 6, by - 4, 10, ui::BG);  // crescent
            }
            // time to the next sunrise / sunset (fixed 06:00 / 18:00: Thailand is near the equator)
            const int left = day ? 1080 - mod : (mod < 360 ? 360 - mod : 1800 - mod);
            char lineLeft[80];
            if (left >= 60) {
                snprintf(lineLeft, sizeof(lineLeft), "อีก %d ชม. %d นาที %s", left / 60, left % 60,
                         day ? "ตะวันตกดิน" : "ตะวันขึ้น");
            } else {
                snprintf(lineLeft, sizeof(lineLeft), "อีก %d นาที %s", left, day ? "ตะวันตกดิน" : "ตะวันขึ้น");
            }
            gUi.drawFitted(c, lineLeft, cx, hy - 36, 190, gUi.thai20(), S, textdatum_t::middle_center);
        }
        drawSleepingEyes(c, cx, hy - 14, 18, 10, P);

        seg7::hhmm(c, hh, mm, cx, 158, 46, 82, 10, P, U, colon);
        char line[128];
        if (st.wxValid) {
            snprintf(line, sizeof(line), "%s %d %s • %.0f°C %s", kDays[lt.tm_wday % 7], lt.tm_mday,
                     kShortMonths[lt.tm_mon % 12], st.wxTemp, wx::describe(st.wxCode));
        } else {
            snprintf(line, sizeof(line), "%s", longDate);
        }
        if (synced) gUi.drawFitted(c, line, cx, 222, 308, gUi.thai20(), S, textdatum_t::middle_center);

    } else {
        // ============================================================
        // Theme 3: คลาสสิก มินิมอล - sleeping eyes over the largest 7-segment time, date, one weather line
        // ============================================================
        drawSleepingEyes(c, cx, 10, 36, 40, P);
        seg7::hhmm(c, hh, mm, cx, 100, 56, 128, 12, P, U, colon);
        gUi.drawFitted(c, longDate, cx, 190, 304, gUi.thai26(), S, textdatum_t::middle_center);
        if (st.wxValid) {
            char wxLine[128];
            if (st.wxPm25 >= 0) {
                snprintf(wxLine, sizeof(wxLine), "%s %.1f°C • %s • PM2.5 %d", st.wxCity[0] ? st.wxCity : "อากาศ",
                         st.wxTemp, wx::describe(st.wxCode), (int)st.wxPm25);
            } else {
                snprintf(wxLine, sizeof(wxLine), "%s %.1f°C • %s • ชื้น %d%%", st.wxCity[0] ? st.wxCity : "อากาศ",
                         st.wxTemp, wx::describe(st.wxCode), (int)st.wxHumidity);
            }
            gUi.drawFitted(c, wxLine, cx, 222, 304, gUi.thai20(), S, textdatum_t::middle_center);
        }
    }
}

// Small radio: body, speaker grille and antenna (fonts have no emoji)
void FaceScreen::drawRadioIcon(LGFX_Sprite& c, int cx, int cy, uint16_t color) {
    gRadioIcon.draw(c, cx, cy, color, [color](aa::Painter& p) {
        const float x = p.x(), y = p.y() - 2.0f;
        float d = fabsf(aa::roundRect(x, y, 0, 3.0f, 9.0f, 6.0f, 2.0f)) - 0.8f;  // body
        d = fminf(d, aa::circle(x, y, -3.5f, 3.5f, 2.8f));                         // speaker
        d = fminf(d, aa::segment(x, y, 3.0f, 1.0f, 3.0f, 6.0f, 0.75f));            // tuner
        d = fminf(d, aa::segment(x, y, 6.0f, 1.0f, 6.0f, 6.0f, 0.75f));
        d = fminf(d, aa::segment(x, y, -6.0f, -3.0f, 4.0f, -7.0f, 0.8f));          // antenna
        p.layer(d, color);
    });
}

// Radio / MP3 player shortcut button next to the vehicle dash button
void FaceScreen::drawRadioButton(LGFX_Sprite& c, int cx, int cy, bool active) {
    const uint16_t bg = active ? 0x0A32 : ui::PANEL_HI, note = active ? ui::ACCENT : ui::TEXT;
    const uint16_t beam = active ? ui::TEXT : ui::ACCENT;
    gRadioBtnIcon.draw(c, cx, cy, active, [=](aa::Painter& p) {
        const float x = p.x(), y = p.y();
        p.layer(aa::circle(x, y, 0, 0, 17.0f), bg);
        if (active) p.layer(fabsf(aa::circle(x, y, 0, 0, 16.2f)) - 0.8f, ui::ACCENT);
        float d = aa::circle(x, y, -5.0f, 5.0f, 3.3f);  // two beamed eighth notes
        d = fminf(d, aa::circle(x, y, 4.5f, 3.0f, 3.3f));
        d = fminf(d, aa::segment(x, y, -2.1f, 5.0f, -2.1f, -5.5f, 1.0f));
        d = fminf(d, aa::segment(x, y, 7.4f, 3.0f, 7.4f, -7.5f, 1.0f));
        p.layer(d, note);
        p.layer(aa::segment(x, y, -2.1f, -5.8f, 7.4f, -7.8f, 1.8f), beam);
    });
}

// Vehicle dash shortcut: a round button with a small gauge (arc, red zone, needle)
void FaceScreen::drawDashButton(LGFX_Sprite& c, int cx, int cy) {
    gDashIcon.draw(c, cx, cy, 0, [](aa::Painter& p) {
        const float x = p.x(), y = p.y(), gy = 4.0f;  // the half dial sits a little low to look centred
        p.layer(aa::circle(x, y, 0, 0, 17.0f), ui::PANEL_HI);
        p.layer(aa::arc(x, y, 0, gy, 10.0f, 1.4f, 180.0f, 310.0f), ui::DIM);
        p.layer(aa::arc(x, y, 0, gy, 10.0f, 1.4f, 322.0f, 360.0f), ui::ERR);
        const float a = 290.0f * kPi / 180.0f;
        float d = aa::segment(x, y, 0, gy, cosf(a) * 9.0f, gy + sinf(a) * 9.0f, 1.1f);
        d = fminf(d, aa::circle(x, y, 0, gy, 2.4f));
        p.layer(d, ui::TEXT);
    });
}

void FaceScreen::draw(LGFX_Sprite& c) {
    if (_powerSave) {
        _radioShown = false;
        _radioBtnShown = false;
        _dashShown = false;
        _micShown = false;
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

    _micShown = !_subtitleVisible;
    if (_micShown) drawMic(c, kMicX, kMicY, status.muted, !status.muted && _state == AVATAR_LISTENING);

    _radioShown = status.radio[0] && !_subtitleVisible;
    if (status.radio[0] && !_subtitleVisible) {  // Internet radio: what is playing, between the mic and the buttons
        drawRadioIcon(c, kMicX + 34, 218, ui::ACCENT);
        const int rightStop = _radioBtnShown ? (kRadioBtnX - 22) : (_dashShown ? (kDashX - 22) : (ui::W - 8));
        const int left = kMicX + 48;
        gUi.drawFitted(c, status.radio, left, 220, rightStop - left, gUi.thai20(), ui::DIM, textdatum_t::middle_left);
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
