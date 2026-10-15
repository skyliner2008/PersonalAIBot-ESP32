/**
 * @file RadioScreen.cpp
 * @brief Touch radio player (see RadioScreen.h)
 */

#include "RadioScreen.h"

#include <math.h>

#include "../app/Settings.h"
#include "../audio/AudioManager.h"
#include "../audio/RadioPlayer.h"
#include "../audio/RadioStations.h"

RadioScreen gRadioUi;

namespace {

constexpr int kHeaderH = 30;
constexpr int kPanelY = 34, kPanelH = 88;    // now playing (tap = assistant)
constexpr int kChipY = 128, kChipH = 46;     // station presets
constexpr int kChipW = 100, kChipGap = 6;
constexpr int kCtrlY = 180, kCtrlH = 54;     // prev / - / volume / + / next

// hit ids
enum Hit { kNone = -1, kBack = 1000, kStopPlay, kPanel, kPrev, kMinus, kPlus, kNext };

void drawTriangle(LGFX_Sprite& c, int x, int y, int s, bool right, uint16_t col) {
    if (right) c.fillTriangle(x, y - s, x, y + s, x + s, y, col);
    else c.fillTriangle(x + s, y - s, x + s, y + s, x, y, col);
}

}  // namespace

void RadioScreen::begin(AudioManager* audio) { _audio = audio; }

void RadioScreen::onEnter() {
    _dragging = _fingerDown = false;
    _pressed = kNone;
    _nextSettings = 0;
    tick(millis(), 0);
    scrollTo(_current);
}

float RadioScreen::maxScroll() const {
    const int n = (int)gStations.count();
    const float total = n * (kChipW + kChipGap) + kChipGap;
    return total > ui::W ? total - ui::W : 0;
}

void RadioScreen::scrollTo(int index) {
    if (index < 0) return;
    const float x = index * (kChipW + kChipGap) + kChipGap;
    float target = x - (ui::W - kChipW) / 2.0f;  // centre the chip
    if (target < 0) target = 0;
    if (target > maxScroll()) target = maxScroll();
    _scroll = target;
}

int RadioScreen::chipAt(int x) const {
    const float pos = x + _scroll - kChipGap;
    if (pos < 0) return -1;
    const int i = (int)(pos / (kChipW + kChipGap));
    const float within = pos - i * (kChipW + kChipGap);
    if (within > kChipW || i >= (int)gStations.count()) return -1;
    return i;
}

int RadioScreen::hitTest(int x, int y) const {
    if (y < kHeaderH) {
        if (x < 48) return kBack;
        if (x > ui::W - 52) return kStopPlay;
        return kNone;
    }
    if (y >= kPanelY && y < kPanelY + kPanelH) return kPanel;
    if (y >= kChipY && y < kChipY + kChipH) return chipAt(x);
    if (y >= kCtrlY) {
        if (x < 64) return kPrev;
        if (x < 128) return kMinus;
        if (x >= 256) return kNext;
        if (x >= 192) return kPlus;
    }
    return kNone;
}

void RadioScreen::playIndex(int index) {
    const int n = (int)gStations.count();
    if (n <= 0) return;
    index = ((index % n) + n) % n;
    if (gStations.play((size_t)index)) {
        _current = index;
        scrollTo(index);
    }
}

void RadioScreen::onTouch(const TouchEvent& e) {
    switch (e.type) {
        case TouchEvent::Down:
            if (gUi.host()) gUi.host()->uiActivity();
            _fingerDown = true;
            _dragging = false;
            _lastX = e.x;
            _velocity = 0;
            _inChips = e.y >= kChipY && e.y < kChipY + kChipH;
            _pressed = hitTest(e.x, e.y);
            break;
        case TouchEvent::Move:
            if (_inChips) {  // sideways drag scrolls the presets
                const float dx = (float)(_lastX - e.x);
                _scroll += dx;
                _velocity = dx;
                _lastX = e.x;
                if (fabsf((float)(e.x - e.startX)) > 8) {
                    _dragging = true;
                    _pressed = kNone;
                }
                if (_scroll < 0) _scroll = 0;
                if (_scroll > maxScroll()) _scroll = maxScroll();
            }
            break;
        case TouchEvent::Up:
            _fingerDown = false;
            if (!_dragging) _velocity = 0;
            _pressed = kNone;
            break;
        case TouchEvent::Tap: {
            if (_dragging) break;
            const int hit = hitTest(e.x, e.y);
            if (hit == kBack) {
                gUi.pop();
            } else if (hit == kStopPlay) {
                if (gRadio.active()) gRadio.stop();
                else playIndex(gStations.lastIndex());
            } else if (hit == kPanel) {
                if (gUi.host()) gUi.host()->uiTap();  // the assistant, exactly like tapping the face
            } else if (hit == kPrev || hit == kNext) {
                playIndex((_current < 0 ? 0 : _current) + (hit == kNext ? 1 : -1));
            } else if (hit == kMinus || hit == kPlus) {
                const int step = hit == kPlus ? 10 : -10;
                gSettings.update([step](SettingsData& d) {
                    const int v = d.volume + step;
                    d.volume = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
                });
                _volume = gSettings.get().volume;
            } else if (hit >= 0) {
                playIndex(hit);
            }
            break;
        }
        case TouchEvent::LongPress:
            if (hitTest(e.startX, e.startY) == kPanel && gUi.host()) gUi.host()->uiLongPress();  // fresh session
            break;
    }
}

void RadioScreen::tick(uint32_t now, float dt) {
    _now = now;
    if ((int32_t)(now - _nextSettings) >= 0) {
        _nextSettings = now + 400;
        _volume = gSettings.get().volume;
        char name[64];
        gRadio.info(name, sizeof(name), nullptr, 0);
        const int playing = gRadio.active() ? gStations.indexOf(name) : -1;
        const int was = _current;
        _current = playing >= 0 ? playing : gStations.lastIndex();
        if (_current != was && !_fingerDown) scrollTo(_current);  // changed by voice / console / Studio
    }
    if (!_fingerDown && fabsf(_velocity) > 0.3f) {  // fling
        _scroll += _velocity;
        _velocity *= 0.90f;
        if (_scroll < 0) _scroll = 0, _velocity = 0;
        if (_scroll > maxScroll()) _scroll = maxScroll(), _velocity = 0;
    }
    // level bars follow the radio (after the duck gain) with a little per-bar wobble
    const float level = _audio && gRadio.state() == RadioPlayer::State::Playing ? _audio->radioOutLevel() : 0.0f;
    for (int i = 0; i < 6; i++) {
        const float wobble = 0.55f + 0.45f * sinf(now * 0.011f + i * 1.7f) * sinf(now * 0.0047f + i);
        float target = sqrtf(level) * 1.8f * wobble;  // sqrt: still visible while ducked under the voice
        if (target > 1) target = 1;
        _bars[i] += (target - _bars[i]) * (dt > 0 ? fminf(1.0f, dt * 12.0f) : 1.0f);
    }
}

void RadioScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    const bool active = gRadio.active();
    const RadioPlayer::State st = gRadio.state();

    // ---- header ----
    c.fillRect(0, 0, ui::W, kHeaderH, ui::PANEL);
    c.drawWideLine(22, 8, 13, 15, 2.5f, ui::TEXT);
    c.drawWideLine(13, 15, 22, 22, 2.5f, ui::TEXT);
    gUi.drawFitted(c, "วิทยุออนไลน์", 44, 15, 150, gUi.thai20(), ui::TEXT);
    const time_t t = time(nullptr);
    if (t > 1700000000) {
        struct tm lt;
        localtime_r(&t, &lt);
        char hm[8];
        snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
        c.setFont(&fonts::Font2);
        c.setTextColor(ui::DIM);
        c.setTextDatum(textdatum_t::middle_right);
        c.drawString(hm, ui::W - 60, 15);
    }
    const uint16_t spBg = _pressed == kStopPlay ? ui::PANEL_HI : ui::BG;
    c.fillRoundRect(ui::W - 48, 3, 44, 24, 6, spBg);
    if (active) c.fillRect(ui::W - 32, 9, 12, 12, ui::ERR);         // stop
    else drawTriangle(c, ui::W - 32, 15, 7, true, ui::OK);           // play

    // ---- now playing (tap = assistant) ----
    c.fillRoundRect(6, kPanelY, ui::W - 12, kPanelH, 10, _pressed == kPanel ? ui::PANEL_HI : ui::PANEL);
    for (int i = 0; i < 6; i++) {  // level bars
        const int h = 6 + (int)(_bars[i] * 48);
        c.fillRoundRect(18 + i * 8, kPanelY + 68 - h, 5, h, 2, active ? ui::ACCENT : ui::LINE);
    }
    char name[64] = "", title[128] = "";
    gRadio.info(name, sizeof(name), title, sizeof(title));
    if (!active) {
        RadioStation s;
        if (_current >= 0 && gStations.get((size_t)_current, s)) snprintf(name, sizeof(name), "%s", s.name);
    }
    gUi.drawFitted(c, name[0] ? name : "ยังไม่ได้เลือกสถานี", 76, kPanelY + 20, ui::W - 90, gUi.thai26(), ui::TEXT);
    const char* line2 = title;
    char err[96];
    if (st == RadioPlayer::State::Connecting) line2 = "กำลังเชื่อมต่อ...";
    else if (st == RadioPlayer::State::Error) {
        snprintf(err, sizeof(err), "เปิดไม่ได้ (%s)", gRadio.lastError());
        line2 = err;
    } else if (!active) line2 = "หยุดอยู่ - แตะสถานีด้านล่างเพื่อฟัง";
    if (line2[0]) {
        gUi.drawFitted(c, line2, 76, kPanelY + 48, ui::W - 90, gUi.thai20(),
                       st == RadioPlayer::State::Error ? ui::ERR : ui::DIM);
    }
    const AvatarState as = gUi.host() ? gUi.host()->uiState() : AVATAR_SLEEP;
    const char* hint = "แตะตรงนี้เพื่อคุยกับ AI";
    uint16_t hintCol = ui::DIM;
    if (as == AVATAR_LISTENING) hint = "AI กำลังฟัง...", hintCol = ui::ACCENT;
    else if (as == AVATAR_THINKING) hint = "AI กำลังคิด...", hintCol = ui::WARN;
    else if (as == AVATAR_SPEAKING) hint = "AI กำลังพูด...", hintCol = ui::OK;
    else if (as == AVATAR_CONNECTING) hint = "กำลังเรียก AI...", hintCol = ui::WARN;
    gUi.drawFitted(c, hint, 76, kPanelY + 72, ui::W - 90, gUi.thai20(), hintCol);

    // ---- station chips ----
    const int n = (int)gStations.count();
    c.setClipRect(0, kChipY, ui::W, kChipH);
    for (int i = 0; i < n; i++) {
        const int x = (int)(kChipGap + i * (kChipW + kChipGap) - _scroll);
        if (x > ui::W || x + kChipW < 0) continue;
        RadioStation s;
        if (!gStations.get((size_t)i, s)) continue;
        const bool cur = i == _current;
        const uint16_t bg = cur ? (active ? ui::ACCENT : 0x2B4F) : (_pressed == i ? ui::PANEL_HI : ui::PANEL);
        c.fillRoundRect(x, kChipY, kChipW, kChipH, 8, bg);
        gUi.drawFitted(c, s.name, x + kChipW / 2, kChipY + 16, kChipW - 8, gUi.thai20(), cur ? ui::BG : ui::TEXT,
                       textdatum_t::middle_center);
        gUi.drawFitted(c, s.genre, x + kChipW / 2, kChipY + 35, kChipW - 8, gUi.thai20(), cur ? ui::PANEL : ui::DIM,
                       textdatum_t::middle_center);
    }
    c.clearClipRect();
    if (!n) gUi.drawFitted(c, "ยังไม่มีสถานี - เพิ่มได้ที่ ESP32-AI Studio", ui::W / 2, kChipY + 23, ui::W - 20,
                           gUi.thai20(), ui::DIM, textdatum_t::middle_center);

    // ---- controls ----
    struct Btn {
        int hit, x, w;
    };
    static const Btn kBtns[] = {{kPrev, 6, 54}, {kMinus, 68, 56}, {kPlus, 196, 56}, {kNext, 260, 54}};
    for (const Btn& b : kBtns) {
        c.fillRoundRect(b.x, kCtrlY, b.w, kCtrlH, 10, _pressed == b.hit ? ui::PANEL_HI : ui::PANEL);
        const int cx = b.x + b.w / 2, cy = kCtrlY + kCtrlH / 2;
        if (b.hit == kPrev || b.hit == kNext) {
            const bool r = b.hit == kNext;
            drawTriangle(c, r ? cx - 9 : cx - 2, cy, 9, r, ui::TEXT);
            c.fillRect(r ? cx + 9 : cx - 12, cy - 9, 3, 18, ui::TEXT);
        } else {
            c.fillRect(cx - 10, cy - 2, 20, 4, ui::TEXT);
            if (b.hit == kPlus) c.fillRect(cx - 2, cy - 10, 4, 20, ui::TEXT);
        }
    }
    char vol[8];
    snprintf(vol, sizeof(vol), "%d%%", _volume);
    c.setFont(&fonts::Font4);
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(vol, 160, kCtrlY + kCtrlH / 2 - 6);
    gUi.drawFitted(c, "เสียง", 160, kCtrlY + kCtrlH - 10, 60, gUi.thai20(), ui::DIM, textdatum_t::middle_center);
}
