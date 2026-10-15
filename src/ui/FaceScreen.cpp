/**
 * @file FaceScreen.cpp
 * @brief Eyes + status + subtitles home screen
 */

#include "FaceScreen.h"

#include <string.h>

#include <vector>

#include "../util/TextWrap.h"
#include "../util/Utf8.h"

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
constexpr float kEyesYIdle = 112.0f;
constexpr float kEyesYSubtitle = 90.0f;

}  // namespace

void FaceScreen::begin(std::function<void()> openMenu, std::function<void()> openRadio) {
    _openMenu = openMenu;
    _openRadio = openRadio;
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
    _textUntil = millis() + 7000;
    _textDirty = true;
}

void FaceScreen::setSystemText(const char* text, uint32_t holdMs) {
    Guard g(_lock);
    _role = 's';
    copyUtf8(_text, kTextCap, text ? text : "");
    // holdMs == 0: keep it (≈24 days); the deadline is compared with signed math, so not UINT32_MAX
    _textUntil = millis() + (holdMs ? holdMs : 0x7FFFFFF0u);
    _textDirty = true;
}

void FaceScreen::setHint(const char* text, uint32_t durationMs) {
    Guard g(_lock);
    copyUtf8(_hint, sizeof(_hint), text ? text : "");
    _hintUntil = millis() + durationMs;
}

void FaceScreen::setExpression(Emotion e, uint32_t holdMs) {
    Guard g(_lock);
    _expr = e;
    _exprUntil = millis() + holdMs;
}

// ============================================================
// Touch
// ============================================================

void FaceScreen::onTouch(const TouchEvent& e) {
    const bool gear = e.startX >= 266 && e.startY <= 56;
    const bool mic = e.startX <= 64 && e.startY <= 60;
    const bool radioBar = e.startY >= 204 && _radioShown;
    if (e.type == TouchEvent::Tap) {
        if (gear) {
            if (_openMenu) _openMenu();
        } else if (mic) {
            if (gUi.host()) gUi.host()->uiToggleMic();
        } else if (radioBar && _openRadio) {
            _openRadio();  // the "now playing" line under the eyes opens the radio player
        } else if (gUi.host()) {
            gUi.host()->uiTap();
        }
    } else if (e.type == TouchEvent::LongPress && !gear && !mic && gUi.host()) {
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
        exprActive = (int32_t)(_exprUntil - now) > 0;
        if (_textDirty) {
            memcpy(_render, _text, kTextCap);
            _renderRole = _role;
            _textDirty = false;
            relayout = true;
        }
        _subtitleVisible = _render[0] && (int32_t)(_textUntil - now) > 0;
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

// Mic shortcut button (top-left): grey = on, blue = hearing you, red + slash = off
void FaceScreen::drawMic(LGFX_Sprite& c, int cx, int cy, bool muted, bool hearing) {
    const uint16_t bg = muted ? ui::ERR : (hearing ? ui::ACCENT : ui::PANEL_HI);
    const uint16_t fg = (muted || hearing) ? ui::TEXT : ui::DIM;
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
    const int cy = 11;
    drawMic(c, 21, 19, s.muted, !s.muted && _state == AVATAR_LISTENING);
    int bars = 0;
    if (s.wifi) bars = s.rssi > -60 ? 3 : (s.rssi > -72 ? 2 : 1);
    for (int i = 0; i < 3; i++) {
        const int h = 4 + i * 3;
        const uint16_t col = !s.wifi ? (s.configured ? 0x4208 : ui::ERR) : (i < bars ? ui::DIM : 0x2945);
        c.fillRect(46 + i * 5, cy + 5 - h, 3, h, col);
    }
    c.setFont(&fonts::Font2);
    if (s.clock[0]) {
        c.setTextColor(ui::DIM);
        c.setTextDatum(textdatum_t::middle_center);
        c.drawString(s.clock, ui::W / 2, cy);
    }
    // battery
    const int bx = 250;
    if (s.battery >= 0) {
        c.drawRect(bx, cy - 5, 18, 10, ui::DIM);
        c.fillRect(bx + 18, cy - 2, 2, 4, ui::DIM);
        c.fillRect(bx + 1, cy - 4, 16 * s.battery / 100, 8, s.battery > 20 ? ui::DIM : ui::ERR);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", s.battery);
        c.setTextColor(ui::DIM);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(buf, bx - 3, cy);
    }
    const bool attention = _state == AVATAR_SETUP && (_now / 600) % 2;
    drawGear(c, 302, cy + 1, attention ? ui::ACCENT : ui::DIM);
}

// Power save: big clock, Thai date and two closed eyes. The backlight is already dimmed by the agent
// and the UI task redraws only once a second. The layout drifts a few pixels every minute so the
// IPS panel never holds one static image for hours.
void FaceScreen::drawClock(LGFX_Sprite& c) {
    static const char* const kMonths[] = {"มกราคม", "กุมภาพันธ์", "มีนาคม", "เมษายน", "พฤษภาคม", "มิถุนายน",
                                          "กรกฎาคม", "สิงหาคม", "กันยายน", "ตุลาคม", "พฤศจิกายน", "ธันวาคม"};
    static const char* const kDays[] = {"อาทิตย์", "จันทร์", "อังคาร", "พุธ", "พฤหัสบดี", "ศุกร์", "เสาร์"};
    c.fillScreen(ui::BG);
    const time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    const bool synced = t > 1700000000;
    const int dx = (lt.tm_min % 5) * 3 - 6;  // pixel drift: -6..+6
    const int dy = (lt.tm_min % 3) * 3 - 3;
    const int cx = ui::W / 2 + dx;

    // closed eyes (the avatar is still "here", asleep)
    const uint16_t eye = _eyes.color();
    c.fillSmoothRoundRect(cx - 52, 34 + dy, 34, 6, 3, eye);
    c.fillSmoothRoundRect(cx + 18, 34 + dy, 34, 6, 3, eye);

    // hours and minutes at fixed spots around a blinking colon, so nothing shifts when it blinks
    char hh[4] = "--", mm[4] = "--";
    if (synced) {
        snprintf(hh, sizeof(hh), "%02d", lt.tm_hour);
        snprintf(mm, sizeof(mm), "%02d", lt.tm_min);
    }
    constexpr uint16_t kDigits = 0xCE79;  // light grey: readable on a dimmed backlight without glare
    const int y = 112 + dy;
    c.setFont(&fonts::Font7);  // 7-segment digits, 48 px
    c.setTextSize(2);
    c.setTextColor(kDigits);
    c.setTextDatum(textdatum_t::middle_right);
    c.drawString(hh, cx - 14, y);
    c.setTextDatum(textdatum_t::middle_left);
    c.drawString(mm, cx + 14, y);
    c.setTextSize(1);
    if (!synced || !(lt.tm_sec & 1)) {
        c.fillRect(cx - 4, y - 22, 8, 8, kDigits);
        c.fillRect(cx - 4, y + 14, 8, 8, kDigits);
    }

    if (synced) {
        char date[96];
        snprintf(date, sizeof(date), "วัน%s %d %s %d", kDays[lt.tm_wday % 7], lt.tm_mday, kMonths[lt.tm_mon % 12],
                 lt.tm_year + 1900 + 543);
        gUi.drawFitted(c, date, cx, 182 + dy, 300, gUi.thai20(), ui::DIM, textdatum_t::middle_center);
    }
    UiStatus st;
    {
        Guard g(_lock);
        st = _status;
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

void FaceScreen::draw(LGFX_Sprite& c) {
    if (_powerSave) {
        _radioShown = false;
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
        showHint = _hint[0] && (int32_t)(_hintUntil - _now) > 0;
        if (showHint) memcpy(hint, _hint, sizeof(hint));
    }
    drawStatus(c, status);

    _radioShown = status.radio[0] && !_subtitleVisible;
    if (status.radio[0] && !_subtitleVisible) {  // Internet radio: what is playing, under the eyes
        drawRadioIcon(c, 22, 222, ui::ACCENT);
        gUi.drawFitted(c, status.radio, 40, 222, ui::W - 48, gUi.thai20(), ui::DIM, textdatum_t::middle_left);
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
